/**
 * @file flanjah~.c
 * @brief Dual-delay flanger with sinusoidal LFO modulation for Max 8/9.
 * Modernized for 64-bit dsp64, memory safety, NaN/denormal protection,
 * and thread-safe buffer management on sample-rate changes.
 */

#include "MSPd.h"
#include <math.h>
#include <string.h>

#define F_LEN 65536
#define F_MASK (F_LEN - 1)
#define MIN_DEL_SEC 0.0001
#define MAX_DEL_SEC 360.0
#define MIN_DEL_SAMPS 4
#define FB_LIMIT 0.425

#ifndef TWOPI
#define TWOPI 6.283185307179586
#endif

static t_class *flanjah_class;

#define OBJECT_NAME "flanjah~"

typedef struct _flanjah
{
    t_pxobject x_obj;

    float *sinetab;
    double si_factor;

    double osc1_phs;
    double osc2_phs;

    // parameters (written by main thread, read by audio thread)
    double speed1;
    double speed2;
    double feedback;
    double maxdel;      // seconds
    double depth;

    float *ddl1;
    long ddl1_len;
    long ddl1_phs;
    float *ddl2;
    long ddl2_len;
    long ddl2_phs;

    double tap1;
    double tap2;

    int feedback_connected;
    int speed1_connected;
    int speed2_connected;
    int depth_connected;
    int feedback_protect;
    int mute;
    double sr;

    t_systhread_mutex lock; // guards buffer pointers/lengths/phases vs. dsp64 resize
} t_flanjah;

void *flanjah_new(t_symbol *s, long argc, t_atom *argv);
void flanjah_free(t_flanjah *x);
void flanjah_assist(t_flanjah *x, void *b, long m, long a, char *s);
void flanjah_report(t_flanjah *x);
void flanjah_protect(t_flanjah *x, double state);
void flanjah_mute(t_flanjah *x, double state);
void flanjah_float(t_flanjah *x, double f);
void flanjah_int(t_flanjah *x, long n);
void flanjah_dsp64(t_flanjah *x, t_object *dsp64, short *count, double samplerate, long n, long flags);
void flanjah_perform64(t_flanjah *x, t_object *dsp64, double **ins,
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam);

/* ---------------------------------------------------------------- helpers */

// Returns def for NaN / inf / out-of-range; written so NaN takes the fallback branch.
static inline double flanjah_clampd(double v, double lo, double hi, double def)
{
    if (!(v >= lo && v <= hi))
        return def;
    return v;
}

// Zero out NaN, inf, huge, and denormal-range values (applied to values stored in recirculating paths).
static inline double flanjah_clean(double v)
{
    double av = fabs(v);
    if (!(av >= 1e-15 && av < 1e10))
        return 0.0;
    return v;
}

static inline double flanjah_fb(double fb, int protect)
{
    if (!(fabs(fb) < 1e6))   // NaN / inf
        fb = 0.0;
    if (protect) {
        if (fb > FB_LIMIT)  fb = FB_LIMIT;
        if (fb < -FB_LIMIT) fb = -FB_LIMIT;
    }
    return fb;
}

static inline double flanjah_advance(double phs, double speed, double si_factor)
{
    double inc = speed * si_factor;
    if (!(inc > -(double)F_LEN && inc < (double)F_LEN))   // NaN / inf / absurd
        inc = 0.0;
    phs += inc;
    if (phs >= (double)F_LEN)
        phs -= (double)F_LEN;
    else if (phs < 0.0)
        phs += (double)F_LEN;
    if (!(phs >= 0.0 && phs < (double)F_LEN))   // rounding corner case or NaN
        phs = 0.0;
    return phs;
}

static long flanjah_calc_len(double maxdel, double sr)
{
    long len = (long)(maxdel * sr);
    if (len < MIN_DEL_SAMPS)
        len = MIN_DEL_SAMPS;
    return len;
}

// Allocates two zeroed delay buffers. Returns 0 on success.
static int flanjah_alloc_buffers(long len, float **d1, float **d2)
{
    *d1 = (float *)sysmem_newptrclear((len + 2) * sizeof(float));
    *d2 = (float *)sysmem_newptrclear((len + 2) * sizeof(float));
    if (!*d1 || !*d2) {
        if (*d1) sysmem_freeptr(*d1);
        if (*d2) sysmem_freeptr(*d2);
        *d1 = *d2 = NULL;
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------- main */

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.flanjah~", (method)flanjah_new, (method)flanjah_free,
                  sizeof(t_flanjah), 0L, A_GIMME, 0);
    class_addmethod(c, (method)flanjah_dsp64,   "dsp64",   A_CANT, 0);
    class_addmethod(c, (method)flanjah_assist,  "assist",  A_CANT, 0);
    class_addmethod(c, (method)flanjah_report,  "report",  A_NOTHING, 0);
    class_addmethod(c, (method)flanjah_protect, "protect", A_DEFFLOAT, 0);
    class_addmethod(c, (method)flanjah_mute,    "mute",    A_DEFFLOAT, 0);
    class_addmethod(c, (method)flanjah_float,   "float",   A_FLOAT, 0);
    class_addmethod(c, (method)flanjah_int,     "int",     A_LONG, 0);
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    flanjah_class = c;
    potpourri_announce(OBJECT_NAME);
    return 0;
}

/* ------------------------------------------------------------ lifecycle */

void *flanjah_new(t_symbol *s, long argc, t_atom *argv)
{
    int i;
    t_flanjah *x = (t_flanjah *)object_alloc(flanjah_class);
    if (!x)
        return NULL;

    dsp_setup((t_pxobject *)x, 5);
    outlet_new((t_object *)x, "signal");

    systhread_mutex_new(&x->lock, SYSTHREAD_MUTEX_NORMAL);

    x->sr = sys_getsr();
    if (!(x->sr > 0.0))
        x->sr = 44100.0;

    // defaults
    x->maxdel = 0.05; // seconds
    x->feedback = 0.7;
    x->speed1 = 0.136;
    x->speed2 = 0.183;
    x->feedback_protect = 1;
    x->depth = 1.0;

    if (argc > 0) x->maxdel   = atom_getfloatarg(0, argc, argv) / 1000.0;
    if (argc > 1) x->feedback = atom_getfloatarg(1, argc, argv);
    if (argc > 2) x->speed1   = atom_getfloatarg(2, argc, argv);
    if (argc > 3) x->speed2   = atom_getfloatarg(3, argc, argv);
    if (argc > 4) x->depth    = atom_getfloatarg(4, argc, argv);

    // validate
    if (!(x->maxdel >= MIN_DEL_SEC)) {
        object_error((t_object *)x, "below minimum of 0.1 ms; using 0.1 ms");
        x->maxdel = MIN_DEL_SEC;
    }
    if (x->maxdel > MAX_DEL_SEC) {
        object_error((t_object *)x, "above maximum of 360 seconds; using 360 s");
        x->maxdel = MAX_DEL_SEC;
    }
    x->feedback = flanjah_clampd(x->feedback, -1e6, 1e6, 0.7);
    if (!(x->speed1 > 0.0 && x->speed1 < 1e5)) x->speed1 = 0.136;
    if (!(x->speed2 > 0.0 && x->speed2 < 1e5)) x->speed2 = 0.183;
    x->depth = flanjah_clampd(x->depth, 0.0001, 1.0, 1.0);

    x->si_factor = (double)F_LEN / x->sr;
    x->ddl1_len = x->ddl2_len = flanjah_calc_len(x->maxdel, x->sr);

    x->sinetab = (float *)sysmem_newptrclear(F_LEN * sizeof(float));
    if (!x->sinetab || flanjah_alloc_buffers(x->ddl1_len, &x->ddl1, &x->ddl2)) {
        object_error((t_object *)x, "out of memory");
        object_free((t_object *)x);   // _free tolerates NULL members
        return NULL;
    }
    for (i = 0; i < F_LEN; i++)
        x->sinetab[i] = (float)(0.51 - 0.47 * cos(TWOPI * (double)i / (double)F_LEN));

    return x;
}

void flanjah_free(t_flanjah *x)
{
    dsp_free((t_pxobject *)x);   // first: detach from audio thread
    if (x->lock) {
        systhread_mutex_lock(x->lock);   // wait out any in-flight perform
        systhread_mutex_unlock(x->lock);
    }
    if (x->sinetab) sysmem_freeptr(x->sinetab);
    if (x->ddl1)    sysmem_freeptr(x->ddl1);
    if (x->ddl2)    sysmem_freeptr(x->ddl2);
    x->sinetab = NULL;
    x->ddl1 = x->ddl2 = NULL;
    if (x->lock) {
        systhread_mutex_free(x->lock);
        x->lock = NULL;
    }
}

/* --------------------------------------------------------- main-thread API */

void flanjah_report(t_flanjah *x)
{
    object_post((t_object *)x, "feedback: %f", x->feedback);
    object_post((t_object *)x, "depth: %f", x->depth);
    object_post((t_object *)x, "speed1: %f", x->speed1);
    object_post((t_object *)x, "speed2: %f", x->speed2);
    object_post((t_object *)x, "phase1: %f", x->osc1_phs);
    object_post((t_object *)x, "phase2: %f", x->osc2_phs);
}

void flanjah_float(t_flanjah *x, double f)
{
    long inlet = ((t_pxobject *)x)->z_in;

    if (!(fabs(f) < 1e6))   // reject NaN / inf
        return;

    if (inlet == 1) {
        x->feedback = f;
    } else if (inlet == 2) {
        if (f > 0.0)        // no negative speeds
            x->speed1 = f;
    } else if (inlet == 3) {
        if (f > 0.0)
            x->speed2 = f;
    } else if (inlet == 4) {
        x->depth = f;
    }
}

void flanjah_int(t_flanjah *x, long n)
{
    flanjah_float(x, (double)n);
}

void flanjah_mute(t_flanjah *x, double state)
{
    x->mute = (state != 0.0);
}

void flanjah_protect(t_flanjah *x, double state)
{
    x->feedback_protect = (state != 0.0);
}

void flanjah_assist(t_flanjah *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
        case 0: strncpy_zero(dst, "(signal/float) Input", 512); break;
        case 1: strncpy_zero(dst, "(signal/float) Feedback", 512); break;
        case 2: strncpy_zero(dst, "(signal/float) Speed1", 512); break;
        case 3: strncpy_zero(dst, "(signal/float) Speed2", 512); break;
        case 4: strncpy_zero(dst, "(signal/float) Delay Depth", 512); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        if (arg == 0)
            strncpy_zero(dst, "(signal) Output", 512);
    }
}

/* -------------------------------------------------------------------- DSP */

void flanjah_dsp64(t_flanjah *x, t_object *dsp64, short *count, double samplerate, long n, long flags)
{
    if (!(samplerate > 0.0))
        return;

    x->feedback_connected = count[1];
    x->speed1_connected   = count[2];
    x->speed2_connected   = count[3];
    x->depth_connected    = count[4];

    if (x->sr != samplerate) {
        // allocate new (zeroed) buffers off the audio path, then swap under the lock
        long newlen = flanjah_calc_len(x->maxdel, samplerate);
        float *n1 = NULL, *n2 = NULL;
        float *o1, *o2;

        if (flanjah_alloc_buffers(newlen, &n1, &n2)) {
            object_error((t_object *)x, "out of memory resizing delay lines; keeping old sample rate state");
            // old buffers stay valid; run with them rather than crash
        } else {
            systhread_mutex_lock(x->lock);
            o1 = x->ddl1;
            o2 = x->ddl2;
            x->ddl1 = n1;
            x->ddl2 = n2;
            x->ddl1_len = x->ddl2_len = newlen;
            x->ddl1_phs = x->ddl2_phs = 0;
            x->tap1 = x->tap2 = 0.0;
            x->sr = samplerate;
            x->si_factor = (double)F_LEN / samplerate;
            systhread_mutex_unlock(x->lock);
            sysmem_freeptr(o1);
            sysmem_freeptr(o2);
        }
    }

    object_method(dsp64, gensym("dsp_add64"), x, flanjah_perform64, 0, NULL);
}

void flanjah_perform64(t_flanjah *x, t_object *dsp64, double **ins,
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam)
{
    t_double *in1          = ins[0];
    t_double *feedback_vec = ins[1];
    t_double *speed1_vec   = ins[2];
    t_double *speed2_vec   = ins[3];
    t_double *depth_vec    = ins[4];
    t_double *out1         = outs[0];

    // If muted, or the main thread is swapping buffers, output silence (never block).
    if (x->mute || systhread_mutex_trylock(x->lock)) {
        while (n--)
            *out1++ = 0.0;
        return;
    }

    float *sinetab = x->sinetab;
    float *ddl1 = x->ddl1;
    float *ddl2 = x->ddl2;
    long ddl1_len = x->ddl1_len;
    long ddl2_len = x->ddl2_len;
    long ddl1_phs = x->ddl1_phs;
    long ddl2_phs = x->ddl2_phs;
    double osc1_phs = x->osc1_phs;
    double osc2_phs = x->osc2_phs;
    double tap1 = x->tap1;
    double tap2 = x->tap2;
    double si_factor = x->si_factor;
    int feedback_connected = x->feedback_connected;
    int speed1_connected = x->speed1_connected;
    int speed2_connected = x->speed2_connected;
    int depth_connected = x->depth_connected;
    int feedback_protect = x->feedback_protect;
    double speed1 = x->speed1;
    double speed2 = x->speed2;
    double feedback = flanjah_fb(x->feedback, feedback_protect);
    double depth = flanjah_clampd(x->depth, 0.0001, 1.0, 1.0);
    double len1_d = (double)ddl1_len;
    double len2_d = (double)ddl2_len;

    double insamp, fdelay1, fdelay2, frac, v;
    long idelay1, idelay2, index1, index2;

    while (n--) {
        insamp = *in1++;
        if (!(fabs(insamp) < 1e10))
            insamp = 0.0;

        if (feedback_connected)
            feedback = flanjah_fb(*feedback_vec++, feedback_protect);
        if (speed1_connected)
            speed1 = *speed1_vec++;
        if (speed2_connected)
            speed2 = *speed2_vec++;
        if (depth_connected)
            depth = flanjah_clampd(*depth_vec++, 0.0001, 1.0, 1.0);

        // LFO -> delay times (phases are always in [0, F_LEN); mask is belt-and-braces)
        fdelay1 = sinetab[(int)osc1_phs & F_MASK] * len1_d * depth;
        fdelay2 = sinetab[(int)osc2_phs & F_MASK] * len2_d * depth;

        osc1_phs = flanjah_advance(osc1_phs, speed1, si_factor);
        osc2_phs = flanjah_advance(osc2_phs, speed2, si_factor);

        idelay1 = (long)fdelay1;
        idelay2 = (long)fdelay2;
        if (idelay1 > ddl1_len - 2) idelay1 = ddl1_len - 2;
        if (idelay2 > ddl2_len - 2) idelay2 = ddl2_len - 2;

        // delay line 1
        v = flanjah_clean(insamp + feedback * (tap1 + tap2));
        ddl1[ddl1_phs] = (float)v;
        if (++ddl1_phs >= ddl1_len) ddl1_phs = 0;
        index1 = ddl1_phs + idelay1;
        if (index1 >= ddl1_len) index1 -= ddl1_len;
        index2 = index1 + 1;
        if (index2 >= ddl1_len) index2 = 0;
        frac = fdelay1 - (double)idelay1;
        tap1 = (1.0 - frac) * ddl1[index1] + frac * ddl1[index2];

        // delay line 2
        ddl2[ddl2_phs] = (float)flanjah_clean(tap1);
        if (++ddl2_phs >= ddl2_len) ddl2_phs = 0;
        index1 = ddl2_phs + idelay2;
        if (index1 >= ddl2_len) index1 -= ddl2_len;
        index2 = index1 + 1;
        if (index2 >= ddl2_len) index2 = 0;
        frac = fdelay2 - (double)idelay2;
        tap2 = (1.0 - frac) * ddl2[index1] + frac * ddl2[index2];

        *out1++ = (insamp + tap2) * 0.2;
    }

    x->ddl1_phs = ddl1_phs;
    x->ddl2_phs = ddl2_phs;
    x->osc1_phs = osc1_phs;
    x->osc2_phs = osc2_phs;
    x->tap1 = flanjah_clean(tap1);
    x->tap2 = flanjah_clean(tap2);

    systhread_mutex_unlock(x->lock);
}
