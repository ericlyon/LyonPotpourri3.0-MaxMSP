/**
 * @file granola~.c
 * @brief Three-voice overlapping granular pitch shifter with a delay-line buffer for Max 8/9.
 * Modernized for 64-bit dsp64, memory safety, signal-inlet handling,
 * and thread-safe grain/buffer management.
 */

#include "MSPd.h"
#include <math.h>
#include <string.h>

#ifndef TWOPI
#define TWOPI 6.283185307179586
#endif

#define MIN_GRAIN_SAMPS 8
#define MAX_GRAIN_SAMPS 441000
#define DEFAULT_GRAIN_SAMPS 2048

static t_class *granola_class;

#define OBJECT_NAME "granola~"

typedef struct _granola
{
    t_pxobject x_obj;

    t_double *gbuf;        // circular input buffer (4 grains long)
    t_double *grainenv;    // Hann window, one grain long
    long grainsamps;       // current grain length in samples
    long buflen;           // current circular buffer length in samples
    long maxgrainsamps;    // allocated grain length (upper limit for "size")
    double grain_duration; // user grain duration in seconds
    long gpt1, gpt2, gpt3; // position within each grain
    double phs1, phs2, phs3; // read phases into gbuf
    double incr;           // read increment (playback rate)
    long curdel;           // write position
    short mute_me;
    short iconnect;
    double sr;

    t_systhread_mutex lock; // guards buffers/lengths/phases vs. main-thread resize
} t_granola;

void *granola_new(double val);
void granola_free(t_granola *x);
void granola_assist(t_granola *x, void *b, long m, long a, char *s);
void granola_mute(t_granola *x, double toggle);
void granola_size(t_granola *x, double newsize);
void granola_clear(t_granola *x);
void granola_float(t_granola *x, double f);
void granola_int(t_granola *x, long f);
void granola_dsp64(t_granola *x, t_object *dsp64, short *count, double samplerate, long n, long flags);
void granola_perform64(t_granola *x, t_object *dsp64, double **ins,
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam);

/* ---------------------------------------------------------------- helpers */

// Resets grain positions/phases. Caller holds the lock (or the object is not yet live).
static void granola_reset_state(t_granola *x)
{
    x->gpt1 = 0;
    x->gpt2 = x->grainsamps / 3;
    x->gpt3 = (2 * x->grainsamps) / 3;
    x->phs1 = 0.0;
    x->phs2 = (double)x->grainsamps / 3.0;
    x->phs3 = 2.0 * (double)x->grainsamps / 3.0;
    x->curdel = 0;
}

// Allocates a zeroed buffer + Hann window for the given sample rate. Returns 0 on success.
static int granola_build(t_granola *x, double sr, t_double **gbuf, t_double **env,
                         long *grainsamps, long *buflen)
{
    double gs = x->grain_duration * sr;
    long n, i;

    if (!(gs > 5.0 && gs <= (double)MAX_GRAIN_SAMPS))   // also catches NaN
        gs = DEFAULT_GRAIN_SAMPS;
    n = (long)gs;

    *grainsamps = n;
    *buflen = n * 4;
    *gbuf = (t_double *)sysmem_newptrclear(*buflen * sizeof(t_double));
    *env  = (t_double *)sysmem_newptrclear(n * sizeof(t_double));
    if (!*gbuf || !*env) {
        if (*gbuf) sysmem_freeptr(*gbuf);
        if (*env)  sysmem_freeptr(*env);
        *gbuf = *env = NULL;
        return -1;
    }
    for (i = 0; i < n; i++)
        (*env)[i] = 0.5 - 0.5 * cos(TWOPI * (double)i / (double)n);
    return 0;
}

// One grain voice: interpolated read, windowing, and advance. phs is always in [0, buflen).
static inline double granola_voice(const t_double *buf, long buflen,
                                   const t_double *env, long grainsamps,
                                   double *phs, long *gpt, long curdel, double incr)
{
    double p = *phs;
    long ia = (long)p;
    long ib = ia + 1;
    double m2 = p - (double)ia;
    double out;

    if (ia >= buflen) ia -= buflen;
    if (ib >= buflen) ib -= buflen;
    out = ((1.0 - m2) * buf[ia] + m2 * buf[ib]) * env[*gpt];

    if (++(*gpt) >= grainsamps) {
        *gpt = 0;
        p = (double)curdel;
    }
    p += incr;                       // incr < buflen, so a single subtract wraps
    if (p >= (double)buflen)
        p -= (double)buflen;
    *phs = p;
    return out;
}

/* ------------------------------------------------------------------- main */

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.granola~", (method)granola_new,
                  (method)granola_free, sizeof(t_granola), 0L, A_DEFFLOAT, 0);

    class_addmethod(c, (method)granola_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)granola_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)granola_clear,  "clear",  A_NOTHING, 0);
    class_addmethod(c, (method)granola_size,   "size",   A_FLOAT, 0);
    class_addmethod(c, (method)granola_mute,   "mute",   A_DEFFLOAT, 0);
    class_addmethod(c, (method)granola_float,  "float",  A_FLOAT, 0);
    class_addmethod(c, (method)granola_int,    "int",    A_LONG, 0);
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    granola_class = c;
    potpourri_announce(OBJECT_NAME);
    return 0;
}

/* -------------------------------------------------------------- lifecycle */

void *granola_new(double val)
{
    t_granola *x = (t_granola *)object_alloc(granola_class);
    if (!x)
        return NULL;

    dsp_setup((t_pxobject *)x, 2);
    outlet_new((t_object *)x, "signal");

    systhread_mutex_new(&x->lock, SYSTHREAD_MUTEX_NORMAL);

    x->sr = sys_getsr();
    if (!(x->sr > 0.0))
        x->sr = 44100.0;
    x->grain_duration = val * 0.001;   // argument is in ms
    x->incr = 0.5;
    x->mute_me = 0;

    if (granola_build(x, x->sr, &x->gbuf, &x->grainenv, &x->grainsamps, &x->buflen)) {
        object_error((t_object *)x, "out of memory");
        object_free((t_object *)x);    // _free tolerates NULL members
        return NULL;
    }
    x->maxgrainsamps = x->grainsamps;
    granola_reset_state(x);
    return x;
}

void granola_free(t_granola *x)
{
    dsp_free((t_pxobject *)x);          // first: detach from audio thread
    if (x->lock) {
        systhread_mutex_lock(x->lock);  // wait out any in-flight perform
        systhread_mutex_unlock(x->lock);
    }
    if (x->gbuf)     sysmem_freeptr(x->gbuf);
    if (x->grainenv) sysmem_freeptr(x->grainenv);
    x->gbuf = x->grainenv = NULL;
    if (x->lock) {
        systhread_mutex_free(x->lock);
        x->lock = NULL;
    }
}

/* --------------------------------------------------------- main-thread API */

void granola_int(t_granola *x, long f)
{
    granola_float(x, (double)f);
}

void granola_float(t_granola *x, double f)
{
    if (f > 0.0 && f < 1e6)    // rejects NaN, inf, and non-positive increments
        x->incr = f;
}

void granola_clear(t_granola *x)
{
    systhread_mutex_lock(x->lock);
    memset(x->gbuf, 0, (size_t)x->buflen * sizeof(t_double));
    systhread_mutex_unlock(x->lock);
}

void granola_size(t_granola *x, double newsize)
{
    long i, newsamps;
    double nd = newsize * 0.001 * x->sr;

    if (!(nd >= MIN_GRAIN_SAMPS)) {     // also catches NaN
        object_error((t_object *)x, "grain size too small");
        return;
    }
    if (nd > (double)x->maxgrainsamps) {
        object_error((t_object *)x, "specified size over preset maximum, no action taken");
        return;
    }
    newsamps = (long)nd;

    systhread_mutex_lock(x->lock);
    x->grainsamps = newsamps;
    x->buflen = newsamps * 4;           // within the allocated maxgrainsamps * 4
    for (i = 0; i < newsamps; i++)
        x->grainenv[i] = 0.5 - 0.5 * cos(TWOPI * (double)i / (double)newsamps);
    granola_reset_state(x);
    systhread_mutex_unlock(x->lock);
}

void granola_mute(t_granola *x, double toggle)
{
    x->mute_me = (toggle != 0.0);
}

void granola_assist(t_granola *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
        case 0: strncpy_zero(dst, "(signal) Input", 512); break;
        case 1: strncpy_zero(dst, "(signal/float) Increment", 512); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Output", 512);
    }
}

/* -------------------------------------------------------------------- DSP */

void granola_dsp64(t_granola *x, t_object *dsp64, short *count, double samplerate, long n, long flags)
{
    if (!(samplerate > 0.0))
        return;

    x->iconnect = count[1];

    if (x->sr != samplerate) {
        // build new buffers off the audio path, then swap under the lock
        t_double *ngbuf = NULL, *nenv = NULL, *ogbuf, *oenv;
        long ngs, nbl;

        if (granola_build(x, samplerate, &ngbuf, &nenv, &ngs, &nbl)) {
            object_error((t_object *)x, "out of memory resizing grain buffers; keeping old sample rate state");
        } else {
            systhread_mutex_lock(x->lock);
            ogbuf = x->gbuf;
            oenv = x->grainenv;
            x->gbuf = ngbuf;
            x->grainenv = nenv;
            x->grainsamps = ngs;
            x->buflen = nbl;
            x->maxgrainsamps = ngs;
            x->sr = samplerate;
            granola_reset_state(x);
            systhread_mutex_unlock(x->lock);
            sysmem_freeptr(ogbuf);
            sysmem_freeptr(oenv);
        }
    }

    object_method(dsp64, gensym("dsp_add64"), x, granola_perform64, 0, NULL);
}

void granola_perform64(t_granola *x, t_object *dsp64, double **ins,
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam)
{
    t_double *in = ins[0];
    t_double *increment = ins[1];
    t_double *out = outs[0];

    // If muted, or the main thread is resizing, output silence (never block).
    if (x->mute_me || systhread_mutex_trylock(x->lock)) {
        while (n--)
            *out++ = 0.0;
        return;
    }

    t_double *gbuf = x->gbuf;
    t_double *env = x->grainenv;
    long buflen = x->buflen;
    long grainsamps = x->grainsamps;
    long curdel = x->curdel;
    long gpt1 = x->gpt1, gpt2 = x->gpt2, gpt3 = x->gpt3;
    double phs1 = x->phs1, phs2 = x->phs2, phs3 = x->phs3;
    int iconnect = x->iconnect;
    double incr = x->incr;
    double maxincr = (double)(buflen - 1);
    double outsamp;

    while (n--) {
        if (iconnect)
            incr = *increment++;
        else
            incr = x->incr;     // pick up float/int messages from the main thread

        if (!(incr > 0.0))      // zero, negative, or NaN
            incr = 0.5;
        else if (incr > maxincr)
            incr = maxincr;

        if (curdel >= buflen)
            curdel = 0;
        gbuf[curdel] = *in++;

        outsamp  = granola_voice(gbuf, buflen, env, grainsamps, &phs1, &gpt1, curdel, incr);
        outsamp += granola_voice(gbuf, buflen, env, grainsamps, &phs2, &gpt2, curdel, incr);
        outsamp += granola_voice(gbuf, buflen, env, grainsamps, &phs3, &gpt3, curdel, incr);

        ++curdel;
        *out++ = outsamp;       // output may well need to be attenuated
    }

    x->curdel = curdel;
    x->gpt1 = gpt1; x->gpt2 = gpt2; x->gpt3 = gpt3;
    x->phs1 = phs1; x->phs2 = phs2; x->phs3 = phs3;

    systhread_mutex_unlock(x->lock);
}
