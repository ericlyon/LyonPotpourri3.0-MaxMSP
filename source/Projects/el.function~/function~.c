/**
 * @file function~.c
 * @brief Writes synthesized functions (additive waveforms, envelopes, windows) into a buffer~ for Max 8/9.
 * Modernized for buffer_ref lifecycle safety, balanced buffer locking,
 * and bounds-checked envelope generation.
 */

#include "MSPd.h"
#include <math.h>
#include <string.h>

#ifndef TWOPI
#define TWOPI 6.283185307179586
#endif

static t_class *function_class;
#define OBJECT_NAME "function~"

typedef struct _function
{
    t_object x_obj;               // not a signal object: no inlets/outlets of its own, no dsp_setup
    t_symbol *wavename;           // name of target buffer~
    t_buffer_ref *src_buffer_ref; // MSP reference to the buffer
    short normalize;
} t_function;

void *function_new(t_symbol *msg, long argc, t_atom *argv);
void function_free(t_function *x);
void function_addsyn(t_function *x, t_symbol *msg, long argc, t_atom *argv);
void function_aenv(t_function *x, t_symbol *msg, long argc, t_atom *argv);
void function_adenv(t_function *x, t_symbol *msg, long argc, t_atom *argv);
void function_adrenv(t_function *x, t_symbol *msg, long argc, t_atom *argv);
void function_rcos(t_function *x);
void function_gaussian(t_function *x, t_symbol *msg, long argc, t_atom *argv);
void function_normalize(t_function *x, double f);
t_max_err function_notify(t_function *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

/* ---------------------------------------------------------------- helpers */

/*
 * Acquires the target buffer: validates the reference, channel count and frame count,
 * and locks the samples. On success (returns 1) the caller MUST call function_release().
 * On failure (returns 0) nothing is locked.
 */
static int function_acquire(t_function *x, t_buffer_obj **buf, float **samples, long *frames)
{
    long nchans;

    *buf = NULL;
    *samples = NULL;
    *frames = 0;

    if (!x->src_buffer_ref) {
        object_error((t_object *)x, "no buffer~ name specified");
        return 0;
    }
    *buf = buffer_ref_getobject(x->src_buffer_ref);
    if (!*buf) {
        object_error((t_object *)x, "no buffer~ named %s", x->wavename->s_name);
        return 0;
    }
    *samples = buffer_locksamples(*buf);
    if (!*samples) {
        object_error((t_object *)x, "could not lock buffer~ %s", x->wavename->s_name);
        return 0;
    }
    // query geometry only after the lock, so it cannot change underneath us
    nchans = buffer_getchannelcount(*buf);
    *frames = buffer_getframecount(*buf);
    if (nchans != 1) {
        object_error((t_object *)x, "buffer must have only 1 channel, this one has %ld", nchans);
        buffer_unlocksamples(*buf);
        return 0;
    }
    if (*frames < 2) {
        object_error((t_object *)x, "buffer~ %s is empty or too short", x->wavename->s_name);
        buffer_unlocksamples(*buf);
        return 0;
    }
    return 1;
}

// Unlock first, then notify listeners (waveform~ etc.) that the contents changed.
static void function_release(t_buffer_obj *buf, int modified)
{
    buffer_unlocksamples(buf);
    if (modified)
        object_method(buf, gensym("dirty"));
}

/* ------------------------------------------------------------------- main */

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.function~", (method)function_new, (method)function_free,
                  sizeof(t_function), 0L, A_GIMME, 0);

    class_addmethod(c, (method)function_addsyn,   "addsyn",   A_GIMME, 0);
    class_addmethod(c, (method)function_aenv,     "aenv",     A_GIMME, 0);
    class_addmethod(c, (method)function_adenv,    "adenv",    A_GIMME, 0);
    class_addmethod(c, (method)function_adrenv,   "adrenv",   A_GIMME, 0);
    class_addmethod(c, (method)function_gaussian, "gaussian", A_GIMME, 0);
    class_addmethod(c, (method)function_normalize,"normalize",A_FLOAT, 0);
    class_addmethod(c, (method)function_rcos,     "rcos",     A_NOTHING, 0);
    class_addmethod(c, (method)function_notify,   "notify",   A_CANT, 0);
    class_register(CLASS_BOX, c);
    function_class = c;
    potpourri_announce(OBJECT_NAME);
    return 0;
}

/* -------------------------------------------------------------- lifecycle */

void *function_new(t_symbol *msg, long argc, t_atom *argv)
{
    t_function *x = (t_function *)object_alloc(function_class);
    if (!x)
        return NULL;

    x->wavename = atom_getsymarg(0, argc, argv);
    x->normalize = 1;
    x->src_buffer_ref = NULL;

    if (x->wavename && x->wavename->s_name[0]) {
        x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    } else {
        object_error((t_object *)x, "no buffer~ name given as an argument");
    }
    return x;
}

void function_free(t_function *x)
{
    if (x->src_buffer_ref) {
        object_free(x->src_buffer_ref);
        x->src_buffer_ref = NULL;
    }
}

t_max_err function_notify(t_function *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (!x->src_buffer_ref)
        return MAX_ERR_NONE;
    return buffer_ref_notify(x->src_buffer_ref, s, msg, sender, data);
}

void function_normalize(t_function *x, double f)
{
    x->normalize = (f != 0.0);
}

/* --------------------------------------------------------------- functions */

void function_addsyn(t_function *x, t_symbol *msg, long argc, t_atom *argv)
{
    long i, j;
    t_buffer_obj *buf;
    long b_frames;
    float *b_samples;
    double amp, maxamp, rescale, w;

    if (!function_acquire(x, &buf, &b_samples, &b_frames))
        return;

    amp = atom_getfloatarg(0, argc, argv);
    for (i = 0; i < b_frames; i++)
        b_samples[i] = (float)amp;

    for (j = 1; j < argc; j++) {
        amp = atom_getfloatarg(j, argc, argv);
        if (amp != 0.0) {
            w = TWOPI * (double)j / (double)b_frames;
            for (i = 0; i < b_frames; i++)
                b_samples[i] += (float)(amp * sin(w * (double)i));
        }
    }
    if (x->normalize) {
        maxamp = 0.0;
        for (i = 0; i < b_frames; i++) {
            double a = fabs((double)b_samples[i]);
            if (maxamp < a)
                maxamp = a;
        }
        if (maxamp > 0.0) {
            rescale = 1.0 / maxamp;
            for (i = 0; i < b_frames; i++)
                b_samples[i] = (float)(b_samples[i] * rescale);
        } else {
            object_post((t_object *)x, "* zero maxamp! not normalizing");
        }
    }
    function_release(buf, 1);
}

void function_rcos(t_function *x)
{
    long i;
    t_buffer_obj *buf;
    long b_frames;
    float *b_samples;

    if (!function_acquire(x, &buf, &b_samples, &b_frames))
        return;

    for (i = 0; i < b_frames; i++)
        b_samples[i] = (float)(0.5 - 0.5 * cos(TWOPI * (double)i / (double)b_frames));

    function_release(buf, 1);
}

void function_gaussian(t_function *x, t_symbol *msg, long argc, t_atom *argv)
{
    long i;
    t_buffer_obj *buf;
    long b_frames;
    float *b_samples;
    double step, in;

    if (!function_acquire(x, &buf, &b_samples, &b_frames))
        return;

    step = 12.0 / (double)b_frames;
    in = -6.0;
    for (i = 0; i < b_frames; i++) {
        b_samples[i] = (float)exp(-(in * in) / 2.0);
        in += step;
    }
    function_release(buf, 1);
}

void function_adrenv(t_function *x, t_symbol *msg, long argc, t_atom *argv)
{
    long i, j;
    t_buffer_obj *buf;
    long b_frames;
    float *b_samples;
    double fa, fd, fr, downgain;
    long al, dl, sl, rl;

    fa = atom_getfloatarg(0, argc, argv);
    fd = atom_getfloatarg(1, argc, argv);
    fr = atom_getfloatarg(2, argc, argv);
    downgain = atom_getfloatarg(3, argc, argv);
    if (!(downgain > 0.0))     // also catches NaN
        downgain = 0.333;
    if (!(fa >= 0.0 && fd >= 0.0 && fr >= 0.0)) {
        object_error((t_object *)x, "attack, decay and release must be non-negative fractions");
        return;
    }

    if (!function_acquire(x, &buf, &b_samples, &b_frames))
        return;

    al = (long)((double)b_frames * fa);
    dl = (long)((double)b_frames * fd);
    rl = (long)((double)b_frames * fr);
    if (al + dl + rl >= b_frames) {
        object_error((t_object *)x, "attack, decay and release are too long");
        function_release(buf, 0);
        return;
    }
    sl = b_frames - (al + dl + rl);

    for (i = 0; i < al; i++)
        b_samples[i] = (float)((double)i / (double)al);
    for (i = al, j = dl; i < al + dl; i++, j--)
        b_samples[i] = (float)(downgain + (1.0 - downgain) * (double)j / (double)dl);
    for (i = al + dl; i < al + dl + sl; i++)
        b_samples[i] = (float)downgain;
    for (i = al + dl + sl, j = rl; i < b_frames; i++, j--)
        b_samples[i] = (float)(downgain * (double)j / (double)rl);

    function_release(buf, 1);
}

void function_adenv(t_function *x, t_symbol *msg, long argc, t_atom *argv)
{
    long i, j;
    t_buffer_obj *buf;
    long b_frames;
    float *b_samples;
    double fa, fd, downgain;
    long al, dl, rl;

    fa = atom_getfloatarg(0, argc, argv);
    fd = atom_getfloatarg(1, argc, argv);
    downgain = atom_getfloatarg(2, argc, argv);
    if (!(downgain > 0.0))
        downgain = 0.333;
    if (!(fa >= 0.0 && fd >= 0.0)) {
        object_error((t_object *)x, "attack and decay must be non-negative fractions");
        return;
    }

    if (!function_acquire(x, &buf, &b_samples, &b_frames))
        return;

    al = (long)((double)b_frames * fa);
    dl = (long)((double)b_frames * fd);
    if (al + dl >= b_frames) {
        object_error((t_object *)x, "attack and decay are too long");
        function_release(buf, 0);
        return;
    }
    rl = b_frames - (al + dl);

    for (i = 0; i < al; i++)
        b_samples[i] = (float)((double)i / (double)al);
    for (i = al, j = dl; i < al + dl; i++, j--)
        b_samples[i] = (float)(downgain + (1.0 - downgain) * (double)j / (double)dl);
    for (i = al + dl, j = rl; i < b_frames; i++, j--)
        b_samples[i] = (float)(downgain * (double)j / (double)rl);

    function_release(buf, 1);
}

void function_aenv(t_function *x, t_symbol *msg, long argc, t_atom *argv)
{
    long i, j;
    t_buffer_obj *buf;
    long b_frames;
    float *b_samples;
    double frac;
    long al, dl;

    frac = atom_getfloatarg(0, argc, argv);
    if (!(frac > 0.0 && frac < 1.0)) {
        object_error((t_object *)x, "attack time must range from 0.0 - 1.0 (exclusive), rather than %f", frac);
        return;
    }

    if (!function_acquire(x, &buf, &b_samples, &b_frames))
        return;

    al = (long)((double)b_frames * frac);
    dl = b_frames - al;     // al < b_frames, so dl >= 1

    for (i = 0; i < al; i++)
        b_samples[i] = (float)((double)i / (double)al);
    for (i = al, j = dl; i < b_frames; i++, j--)
        b_samples[i] = (float)((double)j / (double)dl);

    function_release(buf, 1);
}
