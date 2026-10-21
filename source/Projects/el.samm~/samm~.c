/**
 * @file samm~.c
 * @brief Sample-accurate multi-stream metronome external for Max 8/9.
 * Modernized for 64-bit dsp64, sample-synchronized stream processing, and zero-leak heap teardown.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>

#define MAXBEATS (256)
#define OBJECT_NAME "el.samm~"

static t_class *samm_class = NULL;

typedef struct _samm
{
    t_pxobject x_obj;

    double  tempo;           // Current tempo in BPM
    double  onebeat_samps;   // Number of samples for a single beat
    double *metro_samps;     // Samples per interval countdown per stream
    double *metro_beatdurs;  // Duration in beats per stream
    double *metro;           // Running countdown per stream
    long    metro_count;     // Number of active metronome streams
    double  sr;              // Current sampling rate
    short   pause;
    short   mute;
    short   tap_listen;      // Flag: listening for tap tempo
    long    tap_samples;     // Accumulated samples during tap listening
} t_samm;

// Prototypes
void *samm_new(t_symbol *msg, short argc, t_atom *argv);
void  samm_free(t_samm *x);
void  samm_assist(t_samm *x, void *b, long m, long a, char *s);
void  samm_tempo(t_samm *x, double f);
void  samm_divbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv);
void  samm_msbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv);
void  samm_sampbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv);
void  samm_ratiobeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv);
void  samm_beatinfo(t_samm *x);
void  samm_mute(t_samm *x, double f);
void  samm_pause(t_samm *x);
void  samm_arm(t_samm *x);
void  samm_resume(t_samm *x);
void  samm_beats(t_samm *x, t_symbol *msg, short argc, t_atom *argv);
void  samm_tap_tempo(t_samm *x);
void  samm_tap_end(t_samm *x);
void  samm_dsp64(t_samm *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  samm_perform64(t_samm *x, t_object *dsp64, double **ins, 
                     long numins, double **outs, long numouts, long n,
                     long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.samm~", 
                           (method)samm_new, 
                           (method)samm_free, 
                           sizeof(t_samm), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)samm_dsp64,      "dsp64",      A_CANT,  0);
    class_addmethod(c, (method)samm_assist,     "assist",     A_CANT,  0);
    class_addmethod(c, (method)samm_mute,       "mute",       A_FLOAT, 0);
    class_addmethod(c, (method)samm_tempo,      "tempo",      A_FLOAT, 0);
    class_addmethod(c, (method)samm_beatinfo,   "beatinfo",   0);
    class_addmethod(c, (method)samm_beats,      "beats",      A_GIMME, 0);
    class_addmethod(c, (method)samm_divbeats,   "divbeats",   A_GIMME, 0);
    class_addmethod(c, (method)samm_msbeats,    "msbeats",    A_GIMME, 0);
    class_addmethod(c, (method)samm_sampbeats,  "sampbeats",  A_GIMME, 0);
    class_addmethod(c, (method)samm_ratiobeats, "ratiobeats", A_GIMME, 0);
    class_addmethod(c, (method)samm_pause,      "pause",      0);
    class_addmethod(c, (method)samm_arm,        "arm",        0);
    class_addmethod(c, (method)samm_resume,     "resume",     0);
    class_addmethod(c, (method)samm_tap_tempo,  "tap_tempo",  0);
    class_addmethod(c, (method)samm_tap_end,    "tap_end",    0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    samm_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *samm_new(t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 2) {
        object_error(NULL, "%s: requires tempo and at least one beat stream", OBJECT_NAME);
        return NULL;
    }
    if (argc > MAXBEATS + 1) {
        object_error(NULL, "%s: exceeded maximum of %d beat values", OBJECT_NAME, MAXBEATS);
        return NULL;
    }

    t_samm *x = (t_samm *)object_alloc(samm_class);
    if (!x) return NULL;

    x->metro_count = argc - 1;
    dsp_setup((t_pxobject *)x, 1);
    x->x_obj.z_misc |= Z_NO_INPLACE;

    for (long i = 0; i < x->metro_count; i++) {
        outlet_new((t_pxobject *)x, "signal");
    }

    x->sr = sys_getsr();
    if (x->sr <= 0.0) {
        x->sr = 44100.0;
    }

    x->pause = 0;
    x->mute = 0;
    x->tap_listen = 0;
    x->tap_samples = 0;

    x->metro_samps    = (double *)sysmem_newptr((size_t)x->metro_count * sizeof(double));
    x->metro_beatdurs = (double *)sysmem_newptr((size_t)x->metro_count * sizeof(double));
    x->metro          = (double *)sysmem_newptr((size_t)x->metro_count * sizeof(double));

    if (!x->metro_samps || !x->metro_beatdurs || !x->metro) {
        object_error((t_object *)x, "memory allocation failed");
        samm_free(x);
        return NULL;
    }

    x->tempo = atom_getfloatarg(0, argc, argv);
    if (x->tempo <= 0.0) {
        x->tempo = 120.0;
    }

    x->onebeat_samps = (60.0 / x->tempo) * x->sr;

    for (int i = 1, j = 0; i < argc; i++, j++) {
        double divisor = atom_getfloatarg(i, argc, argv);
        if (divisor == 0.0) {
            object_warn((t_object *)x, "%s: zero divisor given for stream %d, defaulting to 1.0", OBJECT_NAME, j + 1);
            divisor = 1.0;
        }
        x->metro_beatdurs[j] = 1.0 / divisor;
        x->metro_samps[j]    = x->metro_beatdurs[j] * x->onebeat_samps;
        x->metro[j]          = 1.0;
    }

    return x;
}

void samm_free(t_samm *x)
{
    dsp_free((t_pxobject *)x);

    if (x->metro_samps) {
        sysmem_freeptr(x->metro_samps);
        x->metro_samps = NULL;
    }
    if (x->metro_beatdurs) {
        sysmem_freeptr(x->metro_beatdurs);
        x->metro_beatdurs = NULL;
    }
    if (x->metro) {
        sysmem_freeptr(x->metro);
        x->metro = NULL;
    }
}

// -------------------------------------------------------------------------
// Control Methods
// -------------------------------------------------------------------------

void samm_tap_tempo(t_samm *x)
{
    x->tap_listen = 1;
    x->tap_samples = 0;
    samm_arm(x);
}

void samm_tap_end(t_samm *x)
{
    if (x->tap_samples <= 0) {
        object_error((t_object *)x, "%s: zero tap samples accumulated", OBJECT_NAME);
        return;
    }
    double new_tempo = 60.0 * x->sr / (double)x->tap_samples;
    x->tap_listen = 0;
    samm_tempo(x, new_tempo);
    samm_resume(x);
}

void samm_beatinfo(t_samm *x)
{
    post("tempo: %.4f BPM", x->tempo);
    post("samples per beat: %.4f", x->onebeat_samps);
    for (long i = 0; i < x->metro_count; i++) {
        post("stream %ld: duration %.4f beats, %.2f samples", 
             i + 1, x->metro_beatdurs[i], x->metro_samps[i]);
    }
}

void samm_pause(t_samm *x)
{
    x->pause = 1;
}

void samm_resume(t_samm *x)
{
    x->pause = 0;
}

void samm_arm(t_samm *x)
{
    x->pause = 1;
    for (long i = 0; i < x->metro_count; i++) {
        x->metro[i] = 1.0;
    }
}

void samm_mute(t_samm *x, double f)
{
    x->mute = (short)(f != 0.0);
}

void samm_tempo(t_samm *x, double f)
{
    if (f <= 0.0) {
        object_error((t_object *)x, "illegal tempo: %f", f);
        return;
    }
    double last_tempo = x->tempo;
    x->tempo = f;
    double tempo_fac = last_tempo / x->tempo;
    x->onebeat_samps = (60.0 / x->tempo) * x->sr;

    for (long i = 0; i < x->metro_count; i++) {
        x->metro_samps[i] = x->metro_beatdurs[i] * x->onebeat_samps;
        x->metro[i] *= tempo_fac;
    }
}

void samm_beats(t_samm *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc != x->metro_count) {
        object_error((t_object *)x, "%s: argument count (%d) must match stream count (%ld)", OBJECT_NAME, argc, x->metro_count);
        return;
    }
    for (int i = 0; i < argc; i++) {
        double beatdur = atom_getfloatarg(i, argc, argv);
        if (beatdur <= 0.0) beatdur = 1.0;
        x->metro_beatdurs[i] = beatdur;
        x->metro_samps[i]    = x->metro_beatdurs[i] * x->onebeat_samps;
        x->metro[i]          = 1.0;
    }
}

void samm_divbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc != x->metro_count) {
        object_error((t_object *)x, "%s: argument count (%d) must match stream count (%ld)", OBJECT_NAME, argc, x->metro_count);
        return;
    }
    for (int i = 0; i < argc; i++) {
        double divisor = atom_getfloatarg(i, argc, argv);
        if (divisor == 0.0) divisor = 1.0;
        x->metro_beatdurs[i] = 1.0 / divisor;
        x->metro_samps[i]    = x->metro_beatdurs[i] * x->onebeat_samps;
        x->metro[i]          = 1.0;
    }
}

void samm_msbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc != x->metro_count) {
        object_error((t_object *)x, "%s: argument count (%d) must match stream count (%ld)", OBJECT_NAME, argc, x->metro_count);
        return;
    }
    for (int i = 0; i < argc; i++) {
        double msecs = atom_getfloatarg(i, argc, argv);
        if (msecs <= 0.0) msecs = 1000.0;
        x->metro_samps[i]    = x->sr * 0.001 * msecs;
        x->metro_beatdurs[i] = x->metro_samps[i] / x->onebeat_samps;
        x->metro[i]          = 1.0;
    }
}

void samm_sampbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc != x->metro_count) {
        object_error((t_object *)x, "%s: argument count (%d) must match stream count (%ld)", OBJECT_NAME, argc, x->metro_count);
        return;
    }
    for (int i = 0; i < argc; i++) {
        double samples = atom_getfloatarg(i, argc, argv);
        if (samples <= 0.0) samples = x->sr;
        x->metro_samps[i]    = samples;
        x->metro_beatdurs[i] = x->metro_samps[i] / x->onebeat_samps;
        x->metro[i]          = 1.0;
    }
}

void samm_ratiobeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc != x->metro_count * 2) {
        object_error((t_object *)x, "%s: requires %ld numerator/denominator pairs", OBJECT_NAME, x->metro_count);
        return;
    }
    for (int i = 0, j = 0; i < argc; i += 2, j++) {
        double num   = atom_getfloatarg(i, argc, argv);
        double denom = atom_getfloatarg(i + 1, argc, argv);
        if (denom == 0.0) denom = 1.0;

        x->metro_beatdurs[j] = 4.0 * (num / denom);
        x->metro_samps[j]    = x->metro_beatdurs[j] * x->onebeat_samps;
        x->metro[j]          = 1.0;
    }
}

void samm_assist(t_samm *x, void *b, long msg, long arg, char *dst)
{
    if (msg == 1) {
        snprintf(dst, 256, "(signal) Sync/Reset Impulse Input");
    } else if (msg == 2) {
        snprintf(dst, 256, "(signal) Beat Stream %ld Output", arg + 1);
    }
}

// -------------------------------------------------------------------------
// Real-Time DSP Perform Routine
// -------------------------------------------------------------------------

void samm_dsp64(t_samm *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;

    if (x->sr != sr) {
        x->sr = sr;
        x->onebeat_samps = (60.0 / x->tempo) * x->sr;
        for (long i = 0; i < x->metro_count; i++) {
            x->metro_samps[i] = x->metro_beatdurs[i] * x->onebeat_samps;
            x->metro[i]       = 1.0;
        }
    }

    object_method(dsp64, gensym("dsp_add64"), x, samm_perform64, 0, NULL);
}

void samm_perform64(t_samm *x, t_object *dsp64, double **ins, 
                    long numins, double **outs, long numouts, long n,
                    long flags, void *userparam)
{
    long metro_count = x->metro_count;

    // Vectorized zeroing of all outlets
    for (long i = 0; i < metro_count; i++) {
        memset(outs[i], 0, (size_t)n * sizeof(double));
    }

    if (x->mute) return;

    if (x->tap_listen) {
        x->tap_samples += n;
        return;
    }

    const double *inlet       = ins[0];
    double       *metro       = x->metro;
    const double *metro_samps = x->metro_samps;
    short         pause       = x->pause;

    // Outer loop: samples; Inner loop: streams (guarantees sample-accurate alignment)
    for (long k = 0; k < n; k++) {
        if (inlet[k] != 0.0) {
            pause = 0;
            for (long i = 0; i < metro_count; i++) {
                metro[i] = 1.0;
            }
        }

        if (!pause) {
            for (long i = 0; i < metro_count; i++) {
                metro[i] -= 1.0;
                if (metro[i] <= 0.0) {
                    outs[i][k] = 1.0;
                    metro[i] += metro_samps[i];
                }
            }
        }
    }

    x->pause = pause;
}