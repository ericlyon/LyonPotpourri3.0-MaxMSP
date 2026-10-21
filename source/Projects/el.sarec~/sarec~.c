/**
 * @file sarec~.c
 * @brief Multichannel sample-accurate buffer recorder and punch-in editor for Max 8/9.
 * Modernized for 64-bit dsp64, real-time audio thread safety, and zero-leak teardown.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define OBJECT_NAME "el.sarec~"
#define SAREC_RECORD 1
#define SAREC_OVERDUB 2
#define SAREC_PUNCH 3
#define WAVEFORM_UPDATE (2205)
#define MAX_CHANS (256)

static t_class *sarec_class = NULL;

typedef struct _sarec
{
    t_pxobject    x_obj;
    t_buffer_ref *recbuf;
    t_symbol     *bufname;
    
    long          display_counter;
    int           status;           // 0: idle, 1: recording
    int           mode;             // SAREC_RECORD, SAREC_OVERDUB, SAREC_PUNCH
    int          *armed_chans;      // Array of flags: 1 = armed, 0 = protected
    long          counter;          // Current frame write index
    long          start_frame;      // Region start frame
    long          end_frame;        // Region end frame
    long          fadesamps;        // Number of samples for punch fade
    long          regionsamps;      // Total frames in active punch region
    long          channel_count;    // External signal inlet channel count
    double        sr;
} t_sarec;

// Prototypes
void *sarec_new(t_symbol *msg, short argc, t_atom *argv);
void  sarec_free(t_sarec *x);
void  sarec_assist(t_sarec *x, void *b, long m, long a, char *s);
void  sarec_arm(t_sarec *x, double chan);
void  sarec_disarm(t_sarec *x, double chan);
void  sarec_overdub(t_sarec *x);
void  sarec_record(t_sarec *x);
void  sarec_punch(t_sarec *x);
void  sarec_punchfade(t_sarec *x, double fadetime);
void  sarec_region(t_sarec *x, t_symbol *msg, short argc, t_atom *argv);
void  sarec_regionsamps(t_sarec *x, t_symbol *msg, short argc, t_atom *argv);
void  sarec_setbuf(t_sarec *x, t_symbol *s);
t_max_err sarec_notify(t_sarec *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void  sarec_dsp64(t_sarec *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  sarec_perform64(t_sarec *x, t_object *dsp64, double **ins,
                      long numins, double **outs, long numouts, long n,
                      long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.sarec~", 
                           (method)sarec_new, 
                           (method)sarec_free, 
                           sizeof(t_sarec), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)sarec_dsp64,       "dsp64",       A_CANT,  0);
    class_addmethod(c, (method)sarec_assist,      "assist",      A_CANT,  0);
    class_addmethod(c, (method)sarec_notify,      "notify",      A_CANT,  0);
    class_addmethod(c, (method)sarec_arm,         "arm",         A_FLOAT, 0);
    class_addmethod(c, (method)sarec_disarm,      "disarm",      A_FLOAT, 0);
    class_addmethod(c, (method)sarec_record,      "record",      0);
    class_addmethod(c, (method)sarec_overdub,     "overdub",     0);
    class_addmethod(c, (method)sarec_punch,       "punch",       0);
    class_addmethod(c, (method)sarec_punchfade,   "punchfade",   A_FLOAT, 0);
    class_addmethod(c, (method)sarec_region,      "region",      A_GIMME, 0);
    class_addmethod(c, (method)sarec_regionsamps, "regionsamps", A_GIMME, 0);
    class_addmethod(c, (method)sarec_setbuf,      "setbuf",      A_SYM,   0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    sarec_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *sarec_new(t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 2 || !argv) {
        object_error(NULL, "%s: must specify buffer name and channel count", OBJECT_NAME);
        return NULL;
    }

    t_sarec *x = (t_sarec *)object_alloc(sarec_class);
    if (!x) return NULL;

    atom_arg_getsym(&x->bufname, 0, argc, argv);
    t_atom_long chans = 1;
    atom_arg_getlong(&chans, 1, argc, argv);

    if (chans < 1) chans = 1;
    if (chans > MAX_CHANS) chans = MAX_CHANS;
    x->channel_count = (long)chans;

    // Inlet 0 = Record Click Trigger, Inlets 1..N = Input Signals
    dsp_setup((t_pxobject *)x, (long)(1 + x->channel_count));
    x->x_obj.z_misc |= Z_NO_INPLACE;

    // Outlet 0 = Sync ramp [0.0 - 1.0]
    outlet_new((t_pxobject *)x, "signal");

    x->armed_chans = (int *)sysmem_newptr((size_t)x->channel_count * sizeof(int));
    if (!x->armed_chans) {
        object_free(x);
        return NULL;
    }

    for (long i = 0; i < x->channel_count; i++) {
        x->armed_chans[i] = 1; // All channels armed by default
    }

    x->status          = 0;
    x->counter         = 0;
    x->display_counter = 0;
    x->mode            = SAREC_RECORD;
    x->start_frame     = 0;
    x->end_frame       = 0;
    x->regionsamps     = 0;

    x->sr = sys_getsr();
    if (x->sr <= 0.0) x->sr = 44100.0;
    x->fadesamps = (long)(x->sr * 0.05); // 50 ms default fade

    x->recbuf = buffer_ref_new((t_object *)x, x->bufname);

    return x;
}

void sarec_free(t_sarec *x)
{
    dsp_free((t_pxobject *)x);

    if (x->recbuf) {
        object_free(x->recbuf);
        x->recbuf = NULL;
    }
    if (x->armed_chans) {
        sysmem_freeptr(x->armed_chans);
        x->armed_chans = NULL;
    }
}

// -------------------------------------------------------------------------
// Control Methods
// -------------------------------------------------------------------------

t_max_err sarec_notify(t_sarec *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    return buffer_ref_notify(x->recbuf, s, msg, sender, data);
}

void sarec_setbuf(t_sarec *x, t_symbol *s)
{
    if (!s || s == gensym("")) return;
    x->bufname = s;
    if (x->recbuf) {
        buffer_ref_set(x->recbuf, s);
    }
    x->status = 0;
    x->counter = 0;
}

void sarec_record(t_sarec *x)   { x->mode = SAREC_RECORD; }
void sarec_overdub(t_sarec *x)  { x->mode = SAREC_OVERDUB; }
void sarec_punch(t_sarec *x)    { x->mode = SAREC_PUNCH; }

void sarec_punchfade(t_sarec *x, double fadetime)
{
    if (fadetime < 0.0) fadetime = 0.0;
    x->fadesamps = (long)(x->sr * fadetime * 0.001);
    if (x->fadesamps < 1) x->fadesamps = 1;
}

void sarec_arm(t_sarec *x, double chan)
{
    long ichan = (long)chan;
    if (ichan == -1) {
        for (long i = 0; i < x->channel_count; i++) x->armed_chans[i] = 1;
    } else if (ichan == 0) {
        for (long i = 0; i < x->channel_count; i++) x->armed_chans[i] = 0;
    } else if (ichan >= 1 && ichan <= x->channel_count) {
        x->armed_chans[ichan - 1] = 1;
    }
}

void sarec_disarm(t_sarec *x, double chan)
{
    long ichan = (long)chan;
    if (ichan >= 1 && ichan <= x->channel_count) {
        x->armed_chans[ichan - 1] = 0;
    }
}

void sarec_region(t_sarec *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (!x->recbuf) return;
    t_buffer_obj *b = buffer_ref_getobject(x->recbuf);
    if (!b) return;

    long b_frames = buffer_getframecount(b);
    if (b_frames <= 0) return;

    double ms_start = (argc > 0) ? atom_getfloatarg(0, argc, argv) : 0.0;
    double ms_end   = (argc > 1) ? atom_getfloatarg(1, argc, argv) : 0.0;

    long start_frame = (long)(x->sr * 0.001 * ms_start);
    long end_frame   = (long)(x->sr * 0.001 * ms_end);

    if (start_frame < 0) start_frame = 0;
    if (start_frame >= b_frames) start_frame = b_frames - 1;
    if (end_frame <= start_frame || end_frame > b_frames) end_frame = b_frames;

    x->start_frame  = start_frame;
    x->end_frame    = end_frame;
    x->regionsamps  = end_frame - start_frame;
}

void sarec_regionsamps(t_sarec *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (!x->recbuf) return;
    t_buffer_obj *b = buffer_ref_getobject(x->recbuf);
    if (!b) return;

    long b_frames = buffer_getframecount(b);
    if (b_frames <= 0) return;

    long start_frame = (argc > 0) ? (long)atom_getfloatarg(0, argc, argv) : 0;
    long end_frame   = (argc > 1) ? (long)atom_getfloatarg(1, argc, argv) : b_frames;

    if (start_frame < 0) start_frame = 0;
    if (start_frame >= b_frames) start_frame = b_frames - 1;
    if (end_frame <= start_frame || end_frame > b_frames) end_frame = b_frames;

    x->start_frame  = start_frame;
    x->end_frame    = end_frame;
    x->regionsamps  = end_frame - start_frame;
}

void sarec_assist(t_sarec *x, void *b, long msg, long arg, char *dst)
{
    if (msg == 1) {
        if (arg == 0) {
            snprintf(dst, 256, "(signal) Record Click Trigger / Control");
        } else {
            snprintf(dst, 256, "(signal) Audio Input Channel %ld", arg);
        }
    } else if (msg == 2) {
        snprintf(dst, 256, "(signal) Recording Sync Position [0.0 - 1.0]");
    }
}

// -------------------------------------------------------------------------
// Real-Time DSP Perform Routine
// -------------------------------------------------------------------------

void sarec_dsp64(t_sarec *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    x->sr = sr;

    if (x->recbuf) {
        t_buffer_obj *b = buffer_ref_getobject(x->recbuf);
        if (b) {
            long b_frames = buffer_getframecount(b);
            if (x->end_frame <= 0 || x->end_frame > b_frames) {
                x->start_frame = 0;
                x->end_frame   = b_frames;
                x->regionsamps = b_frames;
            }
        }
    }

    object_method(dsp64, gensym("dsp_add64"), x, sarec_perform64, 0, NULL);
}

void sarec_perform64(t_sarec *x, t_object *dsp64, double **ins,
                      long numins, double **outs, long numouts, long n,
                      long flags, void *userparam)
{
    double *sync = outs[0];

    if (!x->recbuf) {
        memset(sync, 0, (size_t)n * sizeof(double));
        return;
    }

    t_buffer_obj *b = buffer_ref_getobject(x->recbuf);
    if (!b) {
        memset(sync, 0, (size_t)n * sizeof(double));
        return;
    }

    long b_frames = buffer_getframecount(b);
    long b_nchans = buffer_getchannelcount(b);
    if (b_frames <= 0 || b_nchans <= 0) {
        memset(sync, 0, (size_t)n * sizeof(double));
        return;
    }

    float *samples = buffer_locksamples(b);
    if (!samples) {
        memset(sync, 0, (size_t)n * sizeof(double));
        return;
    }

    const double *click_inlet  = ins[0];
    long  channel_count        = x->channel_count;
    long  active_chans         = (b_nchans < channel_count) ? b_nchans : channel_count;
    int  *armed_chans          = x->armed_chans;
    int   status               = x->status;
    long  counter              = x->counter;
    long  start_frame          = x->start_frame;
    long  end_frame            = x->end_frame;
    long  fadesamps            = (x->fadesamps < 1) ? 1 : x->fadesamps;
    long  regionsamps          = (x->regionsamps < 1) ? 1 : x->regionsamps;
    int   mode                 = x->mode;

    if (end_frame <= start_frame || end_frame > b_frames) {
        end_frame = b_frames;
    }

    for (long i = 0; i < n; i++) {
        if (click_inlet[i] != 0.0) {
            int clickval = (int)click_inlet[i];
            if (clickval == -2) {
                // Stop recording
                status = 0;
                counter = 0;
            } else {
                if (clickval > 0 && clickval <= channel_count) {
                    // Arm only specified channel
                    for (long j = 0; j < channel_count; j++) armed_chans[j] = 0;
                    armed_chans[clickval - 1] = 1;
                }
                counter = start_frame;
                status = 1;
            }
        }

        if (counter >= end_frame) {
            counter = 0;
            status = 0;
        }

        if (status) {
            long frame_offset = counter * b_nchans;

            if (mode == SAREC_RECORD) {
                for (long j = 0; j < active_chans; j++) {
                    if (armed_chans[j]) {
                        samples[frame_offset + j] = (float)ins[j + 1][i];
                    }
                }
                counter++;
            } else if (mode == SAREC_OVERDUB) {
                for (long j = 0; j < active_chans; j++) {
                    if (armed_chans[j]) {
                        samples[frame_offset + j] += (float)ins[j + 1][i];
                    }
                }
                counter++;
            } else if (mode == SAREC_PUNCH) {
                long counter_msf = counter - start_frame;
                for (long j = 0; j < active_chans; j++) {
                    if (armed_chans[j]) {
                        double in_val = ins[j + 1][i];
                        if (counter_msf < fadesamps) {
                            // Fade in
                            double frak = (double)counter_msf / (double)fadesamps;
                            double goin_up = sin(PIOVERTWO * frak);
                            double goin_down = cos(PIOVERTWO * frak);
                            samples[frame_offset + j] = (float)((samples[frame_offset + j] * goin_down) + (in_val * goin_up));
                        } else if (counter_msf >= (regionsamps - fadesamps)) {
                            // Fade out
                            double frak = (double)(regionsamps - counter_msf) / (double)fadesamps;
                            if (frak < 0.0) frak = 0.0;
                            double goin_up = cos(PIOVERTWO * frak);
                            double goin_down = sin(PIOVERTWO * frak);
                            samples[frame_offset + j] = (float)((samples[frame_offset + j] * goin_up) + (in_val * goin_down));
                        } else {
                            // Direct write
                            samples[frame_offset + j] = (float)in_val;
                        }
                    }
                }
                counter++;
            }
        }

        sync[i] = (b_frames > 0) ? ((double)counter / (double)b_frames) : 0.0;
    }

    buffer_unlocksamples(b);

    if (status) {
        x->display_counter += n;
        if (x->display_counter > WAVEFORM_UPDATE) {
            buffer_setdirty(b);
            x->display_counter = 0;
        }
    }

    x->status  = status;
    x->counter = counter;
}