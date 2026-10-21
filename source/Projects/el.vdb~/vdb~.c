/**
 * @file vdb~.c
 * @brief Multichannel variable delay line with feedback and infinite hold for Max 8/9.
 * Modernized for 64-bit dsp64, clock-deferred phase reporting, rate-limited waveform updates, and real-time thread safety.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define OBJECT_NAME "el.vdb~"
#define MAX_CHANS (64)
#define WAVEFORM_UPDATE_SAMPS (2205) // ~50 ms / 20 Hz update rate to throttle UI redraws

static t_class *vdb_class = NULL;

typedef struct
{
    float coef;
    float cutoff;
    float x1;
} t_lpf;

typedef struct _vdb
{
    t_pxobject    x_obj;
    double        sr;
    t_lpf         lpf;
    short         filter;

    double        speed;
    double        feedback;
    double        delay_time;
    double        delay_samps;
    long          len;
    long          phs;

    double        tap;
    short        *connections;
    short         feedback_protect;
    short         mute;
    short         inf_hold;
    t_buffer_ref *delay_buffer_ref;

    long          inlet_count;
    long          outlet_count;
    long          delay_inlet;
    long          feedback_inlet;
    short         verbose;
    long          inf_hold_phs;
    void         *phase_outlet;
    t_clock      *phase_clock;
    double        phase_report_ms;
    long          display_counter;

    /* ATTRIBUTES */
    t_symbol     *buffername_attr;
    double        maxdelay_attr;
    long          chans_attr;
} t_vdb;

// Prototypes
void *vdb_new(t_symbol *s, short argc, t_atom *argv);
void  vdb_free(t_vdb *x);
void  vdb_assist(t_vdb *x, void *b, long m, long a, char *dst);
void  vdb_float(t_vdb *x, double f);
void  vdb_int(t_vdb *x, long f);
void  vdb_mute(t_vdb *x, double t);
void  vdb_show(t_vdb *x);
void  vdb_coef(t_vdb *x, double f);
void  vdb_filter(t_vdb *x, double t);
void  vdb_protect(t_vdb *x, double state);
void  vdb_inf_hold(t_vdb *x, double state);
void  vdb_phase_tick(t_vdb *x);
t_max_err vdb_notify(t_vdb *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

void  vdb_dsp64(t_vdb *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  vdb_perform64(t_vdb *x, t_object *dsp64, double **ins, 
                    long numins, double **outs, long numouts, long n,
                    long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.vdb~", 
                           (method)vdb_new, 
                           (method)vdb_free, 
                           sizeof(t_vdb), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)vdb_dsp64,    "dsp64",    A_CANT,  0);
    class_addmethod(c, (method)vdb_assist,   "assist",   A_CANT,  0);
    class_addmethod(c, (method)vdb_notify,   "notify",   A_CANT,  0);
    class_addmethod(c, (method)vdb_protect,  "protect",  A_FLOAT, 0);
    class_addmethod(c, (method)vdb_inf_hold, "inf_hold", A_FLOAT, 0);
    class_addmethod(c, (method)vdb_mute,     "mute",     A_FLOAT, 0);
    class_addmethod(c, (method)vdb_float,    "float",    A_FLOAT, 0);
    class_addmethod(c, (method)vdb_int,      "int",      A_LONG,  0);
    class_addmethod(c, (method)vdb_show,     "show",     0);

    CLASS_ATTR_SYM(c, "buffername", 0, t_vdb, buffername_attr);
    CLASS_ATTR_DOUBLE(c, "maxdelay", 0, t_vdb, maxdelay_attr);
    CLASS_ATTR_LONG(c, "chans", 0, t_vdb, chans_attr);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    vdb_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *vdb_new(t_symbol *s, short argc, t_atom *argv)
{
    t_vdb *x = (t_vdb *)object_alloc(vdb_class);
    if (!x) return NULL;

    x->buffername_attr = gensym("");
    x->maxdelay_attr   = 5000.0; // 5000 ms default
    x->chans_attr      = 1;      // 1 channel default

    attr_args_process(x, argc, argv);

    if (x->chans_attr < 1) x->chans_attr = 1;
    if (x->chans_attr > MAX_CHANS) x->chans_attr = MAX_CHANS;
    if (x->maxdelay_attr <= 0.0) x->maxdelay_attr = 5000.0;

    x->inlet_count    = x->chans_attr + 2;
    x->outlet_count   = x->chans_attr;
    x->delay_inlet    = x->chans_attr;
    x->feedback_inlet = x->delay_inlet + 1;

    // Outlets: Rightmost float outlet for phase, then signal outlets
    x->phase_outlet = floatout((t_object *)x);
    for (long i = 0; i < x->outlet_count; i++) {
        outlet_new((t_pxobject *)x, "signal");
    }

    dsp_setup((t_pxobject *)x, x->inlet_count);
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->sr = sys_getsr();
    if (x->sr <= 0.0) x->sr = 44100.0;

    x->phase_clock      = (t_clock *)clock_new((void *)x, (method)vdb_phase_tick);
    x->phase_report_ms  = 0.0;
    x->display_counter  = 0;
    x->feedback_protect = 0;
    x->inf_hold         = 0;
    x->phs              = 0;
    x->inf_hold_phs     = 0;
    x->mute             = 0;
    x->feedback         = 0.0;
    x->delay_time       = 100.0; // 100 ms default

    x->connections = (short *)sysmem_newptrclear((size_t)x->inlet_count * sizeof(short));
    if (!x->connections) {
        object_free(x);
        return NULL;
    }

    if (x->buffername_attr && x->buffername_attr != gensym("")) {
        x->delay_buffer_ref = buffer_ref_new((t_object *)x, x->buffername_attr);
    } else {
        x->delay_buffer_ref = NULL;
    }

    return x;
}

void vdb_free(t_vdb *x)
{
    dsp_free((t_pxobject *)x);

    if (x->phase_clock) {
        clock_unset(x->phase_clock);
        object_free((t_object *)x->phase_clock);
        x->phase_clock = NULL;
    }
    if (x->delay_buffer_ref) {
        object_free(x->delay_buffer_ref);
        x->delay_buffer_ref = NULL;
    }
    if (x->connections) {
        sysmem_freeptr(x->connections);
        x->connections = NULL;
    }
}

t_max_err vdb_notify(t_vdb *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    return buffer_ref_notify(x->delay_buffer_ref, s, msg, sender, data);
}

// -------------------------------------------------------------------------
// Control Methods
// -------------------------------------------------------------------------

void vdb_phase_tick(t_vdb *x)
{
    outlet_float(x->phase_outlet, x->phase_report_ms);
}

void vdb_mute(t_vdb *x, double t)        { x->mute = (short)(t != 0.0); }
void vdb_inf_hold(t_vdb *x, double state) { x->inf_hold = (short)(state != 0.0); }
void vdb_filter(t_vdb *x, double t)      { x->filter = (short)(t != 0.0); }
void vdb_coef(t_vdb *x, double f)        { x->lpf.coef = (float)f; }
void vdb_protect(t_vdb *x, double state) { x->feedback_protect = (short)(state != 0.0); }

void vdb_show(t_vdb *x)
{
    post("feedback: %f, delay: %f ms", x->feedback, x->delay_time);
}

void vdb_int(t_vdb *x, long f)
{
    vdb_float(x, (double)f);
}

void vdb_float(t_vdb *x, double df)
{
    long inlet = x->x_obj.z_in;

    if (inlet == x->delay_inlet) {
        if (df > 0.0 && df <= x->maxdelay_attr) {
            x->delay_time = df;
            x->delay_samps = df * 0.001 * x->sr;
        } else {
            object_error((t_object *)x, "%s: delay time %f out of range [0.0 - %f]", OBJECT_NAME, df, x->maxdelay_attr);
        }
    } else if (inlet == x->feedback_inlet) {
        x->feedback = df;
        if (x->feedback_protect) {
            if (x->feedback > 0.99) x->feedback = 0.99;
            if (x->feedback < -0.99) x->feedback = -0.99;
        }
    }
}

void vdb_assist(t_vdb *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        if (arg < x->chans_attr) {
            snprintf(dst, 256, "(signal) Audio Input Channel %ld", arg + 1);
        } else if (arg == x->delay_inlet) {
            snprintf(dst, 256, "(signal/float) Delay Time (ms)");
        } else if (arg == x->feedback_inlet) {
            snprintf(dst, 256, "(signal/float) Feedback [-1.0 to 1.0]");
        }
    } else if (msg == ASSIST_OUTLET) {
        if (arg < x->chans_attr) {
            snprintf(dst, 256, "(signal) Delayed Output Channel %ld", arg + 1);
        } else {
            snprintf(dst, 256, "(float) Read Head Phase (ms)");
        }
    }
}

// -------------------------------------------------------------------------
// DSP & Real-Time Synthesis
// -------------------------------------------------------------------------

void vdb_dsp64(t_vdb *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    x->sr = sr;

    for (long i = 0; i < x->inlet_count; i++) {
        x->connections[i] = count[i];
    }

    if (!x->delay_buffer_ref) {
        x->delay_buffer_ref = buffer_ref_new((t_object *)x, x->buffername_attr);
    } else {
        buffer_ref_set(x->delay_buffer_ref, x->buffername_attr);
    }

    object_method(dsp64, gensym("dsp_add64"), x, vdb_perform64, 0, NULL);
}

void vdb_perform64(t_vdb *x, t_object *dsp64, double **ins,
                   long numins, double **outs, long numouts, long n,
                   long flags, void *userparam)
{
    long ochans = x->chans_attr;

    // 1. Vectorized zeroing of outputs if muted or unconfigured
    if (x->mute || !x->delay_buffer_ref) {
        for (long i = 0; i < ochans; i++) {
            memset(outs[i], 0, (size_t)n * sizeof(double));
        }
        return;
    }

    t_buffer_obj *dbuf = buffer_ref_getobject(x->delay_buffer_ref);
    if (!dbuf) {
        for (long i = 0; i < ochans; i++) memset(outs[i], 0, (size_t)n * sizeof(double));
        return;
    }

    long b_frames = buffer_getframecount(dbuf);
    long b_nchans = buffer_getchannelcount(dbuf);
    if (b_frames <= 1 || b_nchans != ochans) {
        for (long i = 0; i < ochans; i++) memset(outs[i], 0, (size_t)n * sizeof(double));
        return;
    }

    float *delay_line = buffer_locksamples(dbuf);
    if (!delay_line) {
        for (long i = 0; i < ochans; i++) memset(outs[i], 0, (size_t)n * sizeof(double));
        return;
    }

    double sr = x->sr;
    long maxdelay_samps = (long)(x->maxdelay_attr * sr * 0.001);
    if (maxdelay_samps <= 1 || maxdelay_samps > b_frames - 1) {
        maxdelay_samps = b_frames - 1;
    }

    long   phs              = x->phs;
    long   inf_hold_phs     = x->inf_hold_phs;
    double feedback         = x->feedback;
    double fdelay           = x->delay_time * 0.001 * sr;
    short  feedback_protect = x->feedback_protect;
    short  inf_hold         = x->inf_hold;
    const short *connections = x->connections;

    const double *delay_vec    = ins[x->delay_inlet];
    const double *feedback_vec = ins[x->feedback_inlet];

    // Compute playback phase report once per block and schedule safely on clock
    double dphs = (double)phs - (fdelay > 0.0 ? fdelay : 0.0);
    dphs = fmod(dphs, (double)maxdelay_samps);
    if (dphs < 0.0) dphs += (double)maxdelay_samps;

    x->phase_report_ms = (dphs / sr) * 1000.0;
    if (x->phase_clock) {
        clock_delay(x->phase_clock, 0);
    }

    // Main DSP Processing
    if (inf_hold) {
        for (long i = 0; i < b_nchans; i++) {
            long cur_hold = inf_hold_phs;
            double *output = outs[i];

            for (long j = 0; j < n; j++) {
                if (++cur_hold >= maxdelay_samps) cur_hold = 0;
                output[j] = (double)delay_line[cur_hold * b_nchans + i];
            }
        }
        inf_hold_phs = (inf_hold_phs + n) % maxdelay_samps;
        phs          = (phs + n) % maxdelay_samps;
    } else {
        for (long i = 0; i < b_nchans; i++) {
            const double *input  = ins[i];
            double       *output = outs[i];
            long cur_phs = phs;

            for (long j = 0; j < n; j++) {
                if (connections[x->delay_inlet]) {
                    fdelay = delay_vec[j] * 0.001 * sr;
                    if (fdelay < 1.0) fdelay = 1.0;
                    if (fdelay > (double)(maxdelay_samps - 1)) fdelay = (double)(maxdelay_samps - 1);
                    x->delay_time = fdelay * (1000.0 / sr);
                }

                if (connections[x->feedback_inlet]) {
                    feedback = feedback_vec[j];
                    if (feedback_protect) {
                        if (feedback > 0.99) feedback = 0.99;
                        if (feedback < -0.99) feedback = -0.99;
                    }
                    x->feedback = feedback;
                }

                long idelay = (long)fdelay;
                double frac = fdelay - (double)idelay;

                long dphs1 = cur_phs - idelay;
                dphs1 = (dphs1 % maxdelay_samps + maxdelay_samps) % maxdelay_samps;

                long dphs2 = (dphs1 - 1 + maxdelay_samps) % maxdelay_samps;

                if (i == 0) inf_hold_phs = dphs1;

                double x1 = (double)delay_line[dphs1 * b_nchans + i];
                double x2 = (double)delay_line[dphs2 * b_nchans + i];
                double outsamp = x1 + frac * (x2 - x1);

                output[j] = outsamp;

                double insamp = input[j];
                double feedsamp = outsamp * feedback;
                if (fabs(feedsamp) < 1.0e-15) feedsamp = 0.0; // Denormal protection

                delay_line[cur_phs * b_nchans + i] = (float)(insamp + feedsamp);

                if (++cur_phs >= maxdelay_samps) cur_phs = 0;
            }
        }
        phs = (phs + n) % maxdelay_samps;
    }

    buffer_unlocksamples(dbuf);

    // Rate-limited buffer dirty notification (refreshes waveform~ and buffer~ display cache)
    if (!inf_hold) {
        x->display_counter += n;
        if (x->display_counter >= WAVEFORM_UPDATE_SAMPS) {
            buffer_setdirty(dbuf);
            x->display_counter = 0;
        }
    }

    x->phs          = phs;
    x->inf_hold_phs = inf_hold_phs;
}