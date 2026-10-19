/**
 * @file buffi~.c
 * @brief Dynamic buffer interpolation and function generation external for Max 8/9.
 * Modernized for 64-bit dsp64, memory safety, and buffer lock symmetry.
 */

#include "MSPd.h"
#include <stdlib.h>
#include <math.h>

#define OBJECT_NAME "buffi~"

static t_class *buffi_class;

typedef struct _buffi {
    t_pxobject x_obj;
    t_symbol *src1;
    t_symbol *src2;
    t_symbol *dest;
    t_buffer_ref *src1_buffer_ref;
    t_buffer_ref *src2_buffer_ref;
    t_buffer_ref *dest_buffer_ref;
} t_buffi;

/* Prototypes */
void *buffi_new(t_symbol *msg, long argc, t_atom *argv);
void buffi_free(t_buffi *x);
void buffi_assist(t_buffi *x, void *b, long msg, long arg, char *dst);
void buffi_dsp64(t_buffi *x, t_object *dsp64, short *count, double sr, long n, long flags);
void buffi_perform64(t_buffi *x, t_object *dsp64, double **ins,
                      long numins, double **outs, long numouts, long n,
                      long flags, void *userparam);
t_max_err buffi_notify(t_buffi *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void buffi_doset(t_buffi *x);
void buffi_reset(t_buffi *x, t_symbol *src1, t_symbol *src2, t_symbol *dest);
double buffi_randf(double min, double max);
void buffi_autofunc(t_buffi *x, double minval, double maxval, t_symbol *buffername);
void buffi_autoexpfunc(t_buffi *x, t_symbol *msg, long argc, t_atom *argv);
void buffi_autocurvefunc(t_buffi *x, t_symbol *msg, long argc, t_atom *argv);
void buffi_autospike(t_buffi *x, t_symbol *msg, long argc, t_atom *argv);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.buffi~", 
                           (method)buffi_new, 
                           (method)buffi_free, 
                           (long)sizeof(t_buffi), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)buffi_assist,        "assist",        A_CANT, 0);
    class_addmethod(c, (method)buffi_dsp64,         "dsp64",         A_CANT, 0);
    class_addmethod(c, (method)buffi_notify,        "notify",        A_CANT, 0);
    class_addmethod(c, (method)buffi_autofunc,      "autofunc",      A_FLOAT, A_FLOAT, A_SYM, 0);
    class_addmethod(c, (method)buffi_autospike,     "autospike",     A_GIMME, 0);
    class_addmethod(c, (method)buffi_autoexpfunc,   "autoexpfunc",   A_GIMME, 0);
    class_addmethod(c, (method)buffi_autocurvefunc, "autocurvefunc", A_GIMME, 0);
    class_addmethod(c, (method)buffi_reset,         "reset",         A_SYM, A_SYM, A_SYM, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    buffi_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

double buffi_randf(double min, double max)
{
    return min + (max - min) * ((double)rand() / (double)RAND_MAX);
}

void *buffi_new(t_symbol *msg, long argc, t_atom *argv)
{
    t_buffi *x = (t_buffi *)object_alloc(buffi_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);       // 1 Signal inlet (interpolation phase)
    outlet_new((t_object *)x, "signal"); // 1 Signal output

    // CRITICAL: Prevent Max from sharing memory between ins[0] and outs[0]!
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->src1 = gensym("");
    x->src2 = gensym("");
    x->dest = gensym("");
    x->src1_buffer_ref = NULL;
    x->src2_buffer_ref = NULL;
    x->dest_buffer_ref = NULL;

    if (argc < 3) {
        object_error((t_object *)x, "%s: requires three buffer names: src1, src2, dest", OBJECT_NAME);
        return x;
    }

    atom_arg_getsym(&x->src1, 0, argc, argv);
    atom_arg_getsym(&x->src2, 1, argc, argv);
    atom_arg_getsym(&x->dest, 2, argc, argv);

    buffi_doset(x);
    return x;
}

void buffi_free(t_buffi *x)
{
    dsp_free((t_pxobject *)x);

    if (x->src1_buffer_ref) {
        object_free(x->src1_buffer_ref);
        x->src1_buffer_ref = NULL;
    }
    if (x->src2_buffer_ref) {
        object_free(x->src2_buffer_ref);
        x->src2_buffer_ref = NULL;
    }
    if (x->dest_buffer_ref) {
        object_free(x->dest_buffer_ref);
        x->dest_buffer_ref = NULL;
    }
}

void buffi_reset(t_buffi *x, t_symbol *src1, t_symbol *src2, t_symbol *dest)
{
    x->src1 = src1;
    x->src2 = src2;
    x->dest = dest;
    buffi_doset(x);
}

void buffi_doset(t_buffi *x)
{
    if (!x->src1_buffer_ref)
        x->src1_buffer_ref = buffer_ref_new((t_object *)x, x->src1);
    else
        buffer_ref_set(x->src1_buffer_ref, x->src1);

    if (!x->src2_buffer_ref)
        x->src2_buffer_ref = buffer_ref_new((t_object *)x, x->src2);
    else
        buffer_ref_set(x->src2_buffer_ref, x->src2);

    if (!x->dest_buffer_ref)
        x->dest_buffer_ref = buffer_ref_new((t_object *)x, x->dest);
    else
        buffer_ref_set(x->dest_buffer_ref, x->dest);
}

void buffi_perform64(t_buffi *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
    t_double *in  = ins[0];
    t_double *out = outs[0];

    // STEP 1: Read incoming interpolation phase FIRST before zeroing output memory
    double phase = in[0];
    if (phase < 0.0) phase = 0.0;
    else if (phase > 1.0) phase = 1.0;

    // STEP 2: Clear output buffer
    for (long i = 0; i < sampleframes; i++) {
        out[i] = 0.0;
    }

    t_buffer_obj *src1 = x->src1_buffer_ref ? buffer_ref_getobject(x->src1_buffer_ref) : NULL;
    t_buffer_obj *src2 = x->src2_buffer_ref ? buffer_ref_getobject(x->src2_buffer_ref) : NULL;
    t_buffer_obj *dest = x->dest_buffer_ref ? buffer_ref_getobject(x->dest_buffer_ref) : NULL;

    if (!src1 || !src2 || !dest) return;

    float *src1tab = buffer_locksamples(src1);
    float *src2tab = buffer_locksamples(src2);
    float *desttab = buffer_locksamples(dest);

    if (!src1tab || !src2tab || !desttab) {
        if (src1tab) buffer_unlocksamples(src1);
        if (src2tab) buffer_unlocksamples(src2);
        if (desttab) buffer_unlocksamples(dest);
        return;
    }

    long src1frames = buffer_getframecount(src1);
    long src1nc     = buffer_getchannelcount(src1);
    long src2frames = buffer_getframecount(src2);
    long src2nc     = buffer_getchannelcount(src2);
    long destframes = buffer_getframecount(dest);
    long destnc     = buffer_getchannelcount(dest);

    // Verify dimension matching
    if (src1frames != destframes || src2frames != destframes || 
        src1nc != destnc || src2nc != destnc || destframes <= 0) {
        buffer_unlocksamples(src1);
        buffer_unlocksamples(src2);
        buffer_unlocksamples(dest);
        return;
    }

    float p1 = (float)phase;
    float p2 = 1.0f - p1;

    long total_samples = src1frames * src1nc;
    for (long i = 0; i < total_samples; i++) {
        desttab[i] = (src1tab[i] * p1) + (src2tab[i] * p2);
    }

    buffer_unlocksamples(src1);
    buffer_unlocksamples(src2);
    buffer_unlocksamples(dest);
}

void buffi_dsp64(t_buffi *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;
    buffi_doset(x);
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)buffi_perform64, 0, NULL);
}

void buffi_assist(t_buffi *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(signal) Buffer Interpolation Phase [0.0 - 1.0]", 256);
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Pass-Through (Unused)", 256);
    }
}

void buffi_autofunc(t_buffi *x, double minval, double maxval, t_symbol *buffername)
{
    if (!buffername || buffername == gensym("")) return;

    t_buffer_ref *tablebuf_ref = buffer_ref_new((t_object *)x, buffername);
    if (!tablebuf_ref) return;

    t_buffer_obj *dbuf = buffer_ref_getobject(tablebuf_ref);
    if (!dbuf) {
        object_error((t_object *)x, "%s: nonexistent buffer (%s)", OBJECT_NAME, buffername->s_name);
        object_free(tablebuf_ref);
        return;
    }

    long b_nchans = buffer_getchannelcount(dbuf);
    long b_frames = buffer_getframecount(dbuf);
    if (b_nchans != 1 || b_frames < 10) {
        object_error((t_object *)x, "%s: buffer must be mono and have at least 10 frames", OBJECT_NAME);
        object_free(tablebuf_ref);
        return;
    }

    float *warpfunc = (float *)sysmem_newptr(b_frames * sizeof(float));
    if (!warpfunc) {
        object_free(tablebuf_ref);
        return;
    }

    long minpoints = (long)(0.01 * (double)b_frames);
    long maxpoints = (long)(0.10 * (double)b_frames);
    if (minpoints < 1) minpoints = 1;
    if (maxpoints <= minpoints) maxpoints = minpoints + 1;

    double lastval = buffi_randf(minval, maxval);
    long pointcount = 0;

    while (pointcount < b_frames) {
        double target = buffi_randf(minval, maxval);
        long range = maxpoints - minpoints;
        long segpoints = minpoints + (range > 0 ? (rand() % range) : 0);

        if (pointcount + segpoints > b_frames) {
            segpoints = b_frames - pointcount;
        }

        for (long i = 0; i < segpoints; i++) {
            double m2 = (double)i / (double)segpoints;
            double m1 = 1.0 - m2;
            warpfunc[pointcount + i] = (float)(m1 * lastval + m2 * target);
        }
        lastval = target;
        pointcount += segpoints;
    }

    float *b_samples = buffer_locksamples(dbuf);
    if (b_samples) {
        for (long i = 0; i < b_frames; i++) {
            b_samples[i] = warpfunc[i];
        }
        buffer_unlocksamples(dbuf);
        object_method((t_object *)dbuf, gensym("dirty"));
    }

    sysmem_freeptr(warpfunc);
    object_free(tablebuf_ref);
}

void buffi_autoexpfunc(t_buffi *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 3) {
        object_error((t_object *)x, "%s: autoexpfunc requires minval, maxval, buffername", OBJECT_NAME);
        return;
    }

    double minval = atom_getfloat(argv);
    double maxval = atom_getfloat(argv + 1);
    t_symbol *buffername = atom_getsym(argv + 2);

    if (minval <= 0.0) minval = 0.000001;
    if (maxval <= minval) maxval = minval + 0.01;

    t_buffer_ref *tablebuf_ref = buffer_ref_new((t_object *)x, buffername);
    if (!tablebuf_ref) return;

    t_buffer_obj *dbuf = buffer_ref_getobject(tablebuf_ref);
    if (!dbuf) {
        object_error((t_object *)x, "%s: nonexistent buffer (%s)", OBJECT_NAME, buffername->s_name);
        object_free(tablebuf_ref);
        return;
    }

    long b_nchans = buffer_getchannelcount(dbuf);
    long b_frames = buffer_getframecount(dbuf);
    if (b_nchans != 1 || b_frames < 10) {
        object_error((t_object *)x, "%s: buffer must be mono and have at least 10 frames", OBJECT_NAME);
        object_free(tablebuf_ref);
        return;
    }

    float *warpfunc = (float *)sysmem_newptr(b_frames * sizeof(float));
    if (!warpfunc) {
        object_free(tablebuf_ref);
        return;
    }

    long minpoints = (long)(0.01 * (double)b_frames);
    long maxpoints = (long)(0.10 * (double)b_frames);
    if (minpoints < 1) minpoints = 1;
    if (maxpoints <= minpoints) maxpoints = minpoints + 1;

    double lastval = buffi_randf(minval, maxval);
    long pointcount = 0;

    while (pointcount < b_frames) {
        double target = buffi_randf(minval, maxval);
        long range = maxpoints - minpoints;
        long segpoints = minpoints + (range > 0 ? (rand() % range) : 0);

        if (pointcount + segpoints > b_frames) {
            segpoints = b_frames - pointcount;
        }

        double oMin = (lastval < target) ? lastval : target;
        double oMax = (lastval < target) ? target : lastval;
        double oDiff = oMax - oMin;
        if (oDiff <= 0.0) oDiff = 0.00001;

        for (long i = 0; i < segpoints; i++) {
            double m2 = (double)i / (double)segpoints;
            double m1 = 1.0 - m2;
            double linval = m1 * lastval + m2 * target;
            double curved = pow(oMax / oMin, (linval - oMin) / oDiff) * oMin;
            warpfunc[pointcount + i] = (float)curved;
        }
        lastval = target;
        pointcount += segpoints;
    }

    float *b_samples = buffer_locksamples(dbuf);
    if (b_samples) {
        for (long i = 0; i < b_frames; i++) {
            b_samples[i] = warpfunc[i];
        }
        buffer_unlocksamples(dbuf);
        object_method((t_object *)dbuf, gensym("dirty"));
    }

    sysmem_freeptr(warpfunc);
    object_free(tablebuf_ref);
}

void buffi_autocurvefunc(t_buffi *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 4) {
        object_error((t_object *)x, "%s: autocurvefunc requires minval, maxval, slope, buffername", OBJECT_NAME);
        return;
    }

    double minval = atom_getfloat(argv);
    double maxval = atom_getfloat(argv + 1);
    double slope  = 1.0 - atom_getfloat(argv + 2);
    t_symbol *buffername = atom_getsym(argv + 3);

    t_buffer_ref *tablebuf_ref = buffer_ref_new((t_object *)x, buffername);
    if (!tablebuf_ref) return;

    t_buffer_obj *dbuf = buffer_ref_getobject(tablebuf_ref);
    if (!dbuf) {
        object_error((t_object *)x, "%s: nonexistent buffer (%s)", OBJECT_NAME, buffername->s_name);
        object_free(tablebuf_ref);
        return;
    }

    long b_nchans = buffer_getchannelcount(dbuf);
    long b_frames = buffer_getframecount(dbuf);
    if (b_nchans != 1 || b_frames < 10) {
        object_error((t_object *)x, "%s: buffer must be mono and have at least 10 frames", OBJECT_NAME);
        object_free(tablebuf_ref);
        return;
    }

    float *warpfunc = (float *)sysmem_newptr(b_frames * sizeof(float));
    if (!warpfunc) {
        object_free(tablebuf_ref);
        return;
    }

    long minpoints = (long)(0.01 * (double)b_frames);
    long maxpoints = (long)(0.10 * (double)b_frames);
    if (minpoints < 1) minpoints = 1;
    if (maxpoints <= minpoints) maxpoints = minpoints + 1;

    double lastval = buffi_randf(minval, maxval);
    long pointcount = 0;

    while (pointcount < b_frames) {
        double target = buffi_randf(minval, maxval);
        long range = maxpoints - minpoints;
        long segpoints = minpoints + (range > 0 ? (rand() % range) : 0);

        if (pointcount + segpoints > b_frames) {
            segpoints = b_frames - pointcount;
        }

        for (long i = 0; i < segpoints; i++) {
            double m2 = (double)i / (double)segpoints;
            double m1 = 1.0 - m2;
            double linval = m1 * lastval + m2 * target;
            if (linval < 0.0) linval = 0.0;
            warpfunc[pointcount + i] = (float)pow(linval, slope);
        }
        lastval = target;
        pointcount += segpoints;
    }

    float *b_samples = buffer_locksamples(dbuf);
    if (b_samples) {
        for (long i = 0; i < b_frames; i++) {
            b_samples[i] = warpfunc[i];
        }
        buffer_unlocksamples(dbuf);
        object_method((t_object *)dbuf, gensym("dirty"));
    }

    sysmem_freeptr(warpfunc);
    object_free(tablebuf_ref);
}

void buffi_autospike(t_buffi *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 4) {
        object_error((t_object *)x, "%s: autospike requires minval, maxval, odds, buffername", OBJECT_NAME);
        return;
    }

    double minval = atom_getfloat(argv);
    double maxval = atom_getfloat(argv + 1);
    double odds   = 1.0 - atom_getfloat(argv + 2);
    t_symbol *buffername = atom_getsym(argv + 3);

    t_buffer_ref *tablebuf_ref = buffer_ref_new((t_object *)x, buffername);
    if (!tablebuf_ref) return;

    t_buffer_obj *dbuf = buffer_ref_getobject(tablebuf_ref);
    if (!dbuf) {
        object_error((t_object *)x, "%s: nonexistent buffer (%s)", OBJECT_NAME, buffername->s_name);
        object_free(tablebuf_ref);
        return;
    }

    long b_nchans = buffer_getchannelcount(dbuf);
    long b_frames = buffer_getframecount(dbuf);
    if (b_nchans != 1 || b_frames <= 0) {
        object_error((t_object *)x, "%s: buffer must be mono and non-empty", OBJECT_NAME);
        object_free(tablebuf_ref);
        return;
    }

    float *warpfunc = (float *)sysmem_newptr(b_frames * sizeof(float));
    if (!warpfunc) {
        object_free(tablebuf_ref);
        return;
    }

    for (long i = 0; i < b_frames; i++) {
        if (buffi_randf(0.0, 1.0) > odds) {
            warpfunc[i] = (float)buffi_randf(minval, maxval);
        } else {
            warpfunc[i] = 0.0f;
        }
    }

    float *b_samples = buffer_locksamples(dbuf);
    if (b_samples) {
        for (long i = 0; i < b_frames; i++) {
            b_samples[i] = warpfunc[i];
        }
        buffer_unlocksamples(dbuf);
        object_method((t_object *)dbuf, gensym("dirty"));
    }

    sysmem_freeptr(warpfunc);
    object_free(tablebuf_ref);
}

t_max_err buffi_notify(t_buffi *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (x->src1_buffer_ref) buffer_ref_notify(x->src1_buffer_ref, s, msg, sender, data);
    if (x->src2_buffer_ref) buffer_ref_notify(x->src2_buffer_ref, s, msg, sender, data);
    if (x->dest_buffer_ref) return buffer_ref_notify(x->dest_buffer_ref, s, msg, sender, data);
    return MAX_ERR_NONE;
}