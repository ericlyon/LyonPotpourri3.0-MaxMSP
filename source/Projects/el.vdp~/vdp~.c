/**
 * @file vdp~.c
 * @brief Pointer-based variable delay line with feedback, filtering, and buffer export for Max 8/9.
 * Modernized for 64-bit dsp64, double-precision buffer clearing, and zero-leak memory management.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define MAX_DELAY_TIME (3600.0) // 1 hour maximum delay buffer (in seconds)
#define OBJECT_NAME "el.vdp~"

static t_class *vdp_class = NULL;

typedef struct
{
    double coef;
    double cutoff;
    double x1;
} t_lpf;

typedef struct _vdp
{
    t_pxobject    x_obj;
    double        sr;
    t_lpf         lpf;
    short         filter;
    double        speed;
    double        feedback;
    double        delay_time;      // In seconds
    double        delay_samps;     // In sample frames
    double        maxdel;          // In milliseconds
    double       *delay_line;
    double       *write_ptr;       // Location to write current input
    double       *startmem;        // First address in delay line
    double       *endmem;          // End boundary of delay line
    long          len;             // Frame length of buffer
    long          phs;

    double        tap;
    short         connections[4];

    short         feedback_protect;
    short         mute;
    short         interpolate;
    short         inf_hold;

    /* Copy to buffer */
    t_buffer_ref *destbuf;
    t_symbol     *destbuf_name;

    long          taper_count;
    double        taper_feedback;
} t_vdp;

// Prototypes
void *vdp_new(t_symbol *s, short argc, t_atom *argv);
void  vdp_free(t_vdp *x);
void  vdp_assist(t_vdp *x, void *b, long m, long a, char *dst);
void  vdp_float(t_vdp *x, double f);
void  vdp_int(t_vdp *x, long f);
void  vdp_mute(t_vdp *x, double t);
void  vdp_interpolate(t_vdp *x, long t);
void  vdp_show(t_vdp *x);
void  vdp_coef(t_vdp *x, double f);
void  vdp_filter(t_vdp *x, long t);
void  vdp_init(t_vdp *x, short initialized);
void  vdp_clear(t_vdp *x);
void  vdp_inf_hold(t_vdp *x, double state);
void  vdp_protect(t_vdp *x, double state);
void  vdp_copy_to_buffer(t_vdp *x, t_symbol *msg, short argc, t_atom *argv);
t_max_err vdp_notify(t_vdp *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

void  vdp_dsp64(t_vdp *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  vdp_perform64(t_vdp *x, t_object *dsp64, double **ins, 
                    long numins, double **outs, long numouts, long n,
                    long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.vdp~", 
                           (method)vdp_new, 
                           (method)vdp_free, 
                           sizeof(t_vdp), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)vdp_dsp64,          "dsp64",          A_CANT,  0);
    class_addmethod(c, (method)vdp_assist,         "assist",         A_CANT,  0);
    class_addmethod(c, (method)vdp_notify,         "notify",         A_CANT,  0);
    class_addmethod(c, (method)vdp_protect,        "protect",        A_FLOAT, 0);
    class_addmethod(c, (method)vdp_filter,         "filter",         A_LONG,  0);
    class_addmethod(c, (method)vdp_coef,           "coef",           A_FLOAT, 0);
    class_addmethod(c, (method)vdp_float,          "float",          A_FLOAT, 0);
    class_addmethod(c, (method)vdp_int,            "int",            A_LONG,  0);
    class_addmethod(c, (method)vdp_show,           "show",           0);
    class_addmethod(c, (method)vdp_clear,          "clear",          0);
    class_addmethod(c, (method)vdp_inf_hold,       "inf_hold",       A_FLOAT, 0);
    class_addmethod(c, (method)vdp_interpolate,    "interpolate",    A_LONG,  0);
    class_addmethod(c, (method)vdp_copy_to_buffer, "copy_to_buffer", A_GIMME, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    vdp_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *vdp_new(t_symbol *s, short argc, t_atom *argv)
{
    t_vdp *x = (t_vdp *)object_alloc(vdp_class);
    if (!x) return NULL;

    x->sr = sys_getsr();
    if (x->sr <= 0.0) x->sr = 44100.0;

    dsp_setup((t_pxobject *)x, 3);
    x->x_obj.z_misc |= Z_NO_INPLACE;
    outlet_new((t_pxobject *)x, "signal");

    x->maxdel      = 50.0; // 50 ms default
    x->delay_time  = 0.0;
    x->feedback    = 0.5;
    x->interpolate = 1;

    if (argc > 0) x->maxdel     = atom_getfloatarg(0, argc, argv);
    if (argc > 1) x->delay_time = atom_getfloatarg(1, argc, argv) * 0.001; // ms to seconds
    if (argc > 2) x->feedback   = atom_getfloatarg(2, argc, argv);

    if (x->maxdel < 1.0) x->maxdel = 1.0;
    if (x->maxdel > MAX_DELAY_TIME * 1000.0) x->maxdel = MAX_DELAY_TIME * 1000.0;

    x->delay_line   = NULL;
    x->destbuf      = NULL;
    x->destbuf_name = gensym("");

    vdp_init(x, 0);
    return x;
}

void vdp_free(t_vdp *x)
{
    dsp_free((t_pxobject *)x);
    if (x->destbuf) {
        object_free(x->destbuf);
        x->destbuf = NULL;
    }
    if (x->delay_line) {
        sysmem_freeptr(x->delay_line);
        x->delay_line = NULL;
    }
}

t_max_err vdp_notify(t_vdp *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    return buffer_ref_notify(x->destbuf, s, msg, sender, data);
}

void vdp_clear(t_vdp *x)
{
    if (x->delay_line) {
        memset(x->delay_line, 0, (size_t)(x->len + 2) * sizeof(double));
    }
}

void vdp_init(t_vdp *x, short initialized)
{
    if (!initialized) {
        x->feedback_protect = 0;
        x->interpolate      = 1;
        x->filter           = 0;
        x->inf_hold         = 0;
        x->mute             = 0;
        x->tap              = 0.0;
        x->phs              = 0;
        x->lpf.coef         = 0.5;
        x->lpf.x1           = 0.0;

        if (x->feedback > 0.999)  x->feedback = 0.999;
        if (x->feedback < -0.999) x->feedback = -0.999;

        double maxdel_sec = x->maxdel * 0.001;
        if (x->delay_time >= maxdel_sec || x->delay_time < 0.0) {
            x->delay_time = 0.0;
        }
        x->delay_samps = x->delay_time * x->sr;

        x->len = (long)(x->maxdel * 0.001 * x->sr);
        if (x->len < 4) x->len = 4;

        x->delay_line = (double *)sysmem_newptrclear((size_t)(x->len + 2) * sizeof(double));
    } else {
        x->len = (long)(x->maxdel * 0.001 * x->sr);
        if (x->len < 4) x->len = 4;
        x->delay_line = (double *)sysmem_resizeptrclear(x->delay_line, (size_t)(x->len + 2) * sizeof(double));
    }

    x->startmem  = x->delay_line;
    x->endmem    = x->startmem + x->len;
    x->write_ptr = x->startmem;
}

// -------------------------------------------------------------------------
// Control Methods
// -------------------------------------------------------------------------

void vdp_mute(t_vdp *x, double t)        { x->mute = (short)(t != 0.0); }
void vdp_filter(t_vdp *x, long t)        { x->filter = (short)(t != 0); }
void vdp_coef(t_vdp *x, double f)        { x->lpf.coef = f; }
void vdp_protect(t_vdp *x, double state) { x->feedback_protect = (short)(state != 0.0); }
void vdp_interpolate(t_vdp *x, long t)   { x->interpolate = (short)(t != 0); }

void vdp_inf_hold(t_vdp *x, double t)
{
    x->inf_hold = (short)(t != 0.0);
    x->taper_feedback = 1.0;
    x->taper_count = 0;
}

void vdp_show(t_vdp *x)
{
    post("feedback %.4f delay %.4f ms", x->feedback, x->delay_time * 1000.0);
}

void vdp_int(t_vdp *x, long tog)
{
    vdp_float(x, (double)tog);
}

void vdp_float(t_vdp *x, double df)
{
    long inlet = x->x_obj.z_in;

    if (inlet == 1) { // Delay time in ms
        double f = df * 0.001; // convert to seconds
        double maxdel_sec = x->maxdel * 0.001;
        if (f >= 0.0 && f <= maxdel_sec) {
            x->delay_time  = f;
            x->delay_samps = f * x->sr;
            x->tap         = 0.0;
        } else {
            object_error((t_object *)x, "%s: delay time %.4f out of range [0.0 - %.4f ms]", 
                         OBJECT_NAME, df, x->maxdel);
        }
    } else if (inlet == 2) { // Feedback
        x->feedback = df;
        if (x->feedback_protect) {
            if (x->feedback > 0.99)  x->feedback = 0.99;
            if (x->feedback < -0.99) x->feedback = -0.99;
        }
    }
}

void vdp_copy_to_buffer(t_vdp *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 1 || !argv) return;

    t_symbol *destname = atom_getsymarg(0, argc, argv);
    if (!x->destbuf) {
        x->destbuf = buffer_ref_new((t_object *)x, destname);
    } else {
        buffer_ref_set(x->destbuf, destname);
    }

    t_buffer_obj *b = buffer_ref_getobject(x->destbuf);
    if (!b) {
        object_error((t_object *)x, "invalid destination buffer %s", destname->s_name);
        return;
    }

    long b_dest_frames = buffer_getframecount(b);
    long b_dest_nchans = buffer_getchannelcount(b);
    long copy_frames   = x->len;

    if (b_dest_nchans != 1 || b_dest_frames < copy_frames) {
        object_error((t_object *)x, "destination buffer must be mono and >= %ld frames", copy_frames);
        return;
    }

    float *b_dest_samples = buffer_locksamples(b);
    if (!b_dest_samples) return;

    for (long i = 0; i < copy_frames; i++) {
        b_dest_samples[i] = (float)x->delay_line[i];
    }
    for (long i = copy_frames; i < b_dest_frames; i++) {
        b_dest_samples[i] = 0.0f;
    }

    buffer_unlocksamples(b);
    buffer_setdirty(b);
}

void vdp_assist(t_vdp *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: snprintf(dst, 256, "(signal) Audio Input"); break;
            case 1: snprintf(dst, 256, "(signal/float) Delay Time (ms)"); break;
            case 2: snprintf(dst, 256, "(signal/float) Feedback [-1.0 to 1.0]"); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        snprintf(dst, 256, "(signal) Delayed Output");
    }
}

// -------------------------------------------------------------------------
// Real-Time DSP Perform Routine
// -------------------------------------------------------------------------

void vdp_dsp64(t_vdp *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;

    x->connections[1] = count[1];
    x->connections[2] = count[2];

    if (x->sr != sr) {
        x->sr = sr;
        vdp_init(x, 1);
    }

    object_method(dsp64, gensym("dsp_add64"), x, vdp_perform64, 0, NULL);
}

void vdp_perform64(t_vdp *x, t_object *dsp64, double **ins, 
                   long numins, double **outs, long numouts, long n,
                   long flags, void *userparam)
{
    double *output = outs[0];

    if (x->mute || !x->delay_line) {
        memset(output, 0, (size_t)n * sizeof(double));
        return;
    }

    const double *input        = ins[0];
    const double *delay_vec    = ins[1];
    const double *feedback_vec = ins[2];

    double *write_ptr = x->write_ptr;
    double *startmem  = x->startmem;
    double *endmem    = x->endmem;
    long    len       = x->len;

    double  feedback         = x->feedback;
    double  fdelay           = x->delay_samps;
    double  sr               = x->sr;
    double  msr              = sr * 0.001;
    short   feedback_protect = x->feedback_protect;
    short   interpolate      = x->interpolate;
    t_lpf   lpf              = x->lpf;
    short   filter           = x->filter;
    short   inf_hold         = x->inf_hold;
    const short *conn        = x->connections;

    if (inf_hold) {
        for (long s = 0; s < n; s++) {
            double outsamp = *write_ptr;
            output[s] = outsamp;

            if (++write_ptr >= endmem) {
                write_ptr = startmem;
            }
        }
        x->write_ptr = write_ptr;
        return;
    }

    for (long s = 0; s < n; s++) {
        double insamp = input[s];

        if (conn[1]) {
            fdelay = delay_vec[s] * msr;
            if (fdelay < 0.0) fdelay = 0.0;
            if (fdelay >= (double)len) fdelay = (double)(len - 1);
        }

        if (conn[2]) {
            feedback = feedback_vec[s];
            if (feedback_protect) {
                if (feedback > 0.99)  feedback = 0.99;
                if (feedback < -0.99) feedback = -0.99;
            }
        }

        long idelay = (long)fdelay;
        double outsamp;

        if (interpolate) {
            double frac = fdelay - (double)idelay;
            double *read_ptr = write_ptr - idelay;
            if (read_ptr < startmem) read_ptr += len;

            double x1 = *read_ptr--;
            if (read_ptr < startmem) read_ptr += len;

            double x2 = *read_ptr;
            outsamp = x1 + frac * (x2 - x1);
        } else {
            double *read_ptr = write_ptr - idelay;
            if (read_ptr < startmem) read_ptr += len;
            outsamp = *read_ptr;
        }

        if (filter) {
            outsamp = (outsamp + lpf.x1 * lpf.coef) / (1.0 + lpf.coef);
            lpf.x1 = outsamp;
        }

        double feedsamp = outsamp * feedback;
        if (fabs(feedsamp) < 1.0e-15) feedsamp = 0.0; // Denormal protection

        *write_ptr++ = insamp + feedsamp;
        if (write_ptr >= endmem) {
            write_ptr = startmem;
        }

        output[s] = outsamp;
    }

    x->feedback    = feedback;
    x->delay_samps = fdelay;
    x->delay_time  = fdelay / sr;
    x->write_ptr   = write_ptr;
    x->lpf         = lpf;
}