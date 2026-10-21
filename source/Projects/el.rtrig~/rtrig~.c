/**
 * @file rtrig~.c
 * @brief Random trigger generator with variable odds and amplitude scaling for Max 8/9.
 * Modernized for 64-bit dsp64, instance-local PRNG, and thread-safe real-time performance.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define OBJECT_NAME "el.rtrig~"

static t_class *rtrig_class = NULL;

typedef struct _rtrig
{
    t_pxobject x_obj;
    short      mute;
    double     min;
    double     max;
    double     odds;
    double     sr;
    uint32_t   rand_state; // Instance-local PRNG state
} t_rtrig;

// Prototypes
void *rtrig_new(t_symbol *s, short argc, t_atom *argv);
void  rtrig_free(t_rtrig *x);
void  rtrig_assist(t_rtrig *x, void *b, long msg, long arg, char *dst);
void  rtrig_mute(t_rtrig *x, double tog);
void  rtrig_min(t_rtrig *x, double v);
void  rtrig_max(t_rtrig *x, double v);
void  rtrig_odds(t_rtrig *x, double v);
void  rtrig_density(t_rtrig *x, double v);
void  rtrig_dsp64(t_rtrig *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  rtrig_perform64(t_rtrig *x, t_object *dsp64, double **ins, 
                      long numins, double **outs, long numouts, long n,
                      long flags, void *userparam);

// Fast, thread-safe, instance-local 32-bit XorShift PRNG
static inline double xorshift32_normalized(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return (double)x * (1.0 / 4294967296.0); // Normalize to [0.0, 1.0)
}

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.rtrig~", 
                           (method)rtrig_new, 
                           (method)rtrig_free, 
                           sizeof(t_rtrig), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)rtrig_dsp64,   "dsp64",   A_CANT,  0);
    class_addmethod(c, (method)rtrig_assist,  "assist",  A_CANT,  0);
    class_addmethod(c, (method)rtrig_min,     "min",     A_FLOAT, 0);
    class_addmethod(c, (method)rtrig_max,     "max",     A_FLOAT, 0);
    class_addmethod(c, (method)rtrig_odds,    "odds",    A_FLOAT, 0);
    class_addmethod(c, (method)rtrig_density, "density", A_FLOAT, 0);
    class_addmethod(c, (method)rtrig_mute,    "mute",    A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    rtrig_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *rtrig_new(t_symbol *s, short argc, t_atom *argv)
{
    t_rtrig *x = (t_rtrig *)object_alloc(rtrig_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);
    x->x_obj.z_misc |= Z_NO_INPLACE;
    outlet_new((t_pxobject *)x, "signal");

    x->mute = 0;
    x->min = (argc > 0) ? atom_getfloatarg(0, argc, argv) : 0.0;
    x->max = (argc > 1) ? atom_getfloatarg(1, argc, argv) : 1.0;
    x->odds = (argc > 2) ? atom_getfloatarg(2, argc, argv) : 0.1;

    x->sr = sys_getsr();
    if (x->sr <= 0.0) {
        x->sr = 44100.0;
    }

    // Seed local PRNG with mix of system time and object address (no global srand)
    x->rand_state = (uint32_t)((uintptr_t)x ^ 0xA5A5A5A5U);
    if (x->rand_state == 0) x->rand_state = 123456789U;

    return x;
}

void rtrig_free(t_rtrig *x)
{
    dsp_free((t_pxobject *)x);
}

// -------------------------------------------------------------------------
// Control Methods
// -------------------------------------------------------------------------

void rtrig_mute(t_rtrig *x, double tog)
{
    x->mute = (short)(tog != 0.0);
}

void rtrig_min(t_rtrig *x, double v)
{
    x->min = v;
}

void rtrig_max(t_rtrig *x, double v)
{
    x->max = v;
}

void rtrig_odds(t_rtrig *x, double v)
{
    x->odds = (v < 0.0) ? 0.0 : ((v > 1.0) ? 1.0 : v);
}

void rtrig_density(t_rtrig *x, double v)
{
    if (x->sr > 0.0) {
        double odds = v / x->sr;
        x->odds = (odds < 0.0) ? 0.0 : ((odds > 1.0) ? 1.0 : odds);
    }
}

void rtrig_assist(t_rtrig *x, void *b, long msg, long arg, char *dst)
{
    if (msg == 1) {
        snprintf(dst, 256, "(signal) Pass-through / Inactive");
    } else if (msg == 2) {
        snprintf(dst, 256, "(signal) Trigger Output");
    }
}

// -------------------------------------------------------------------------
// Real-Time DSP Perform Routine
// -------------------------------------------------------------------------

void rtrig_dsp64(t_rtrig *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    x->sr = sr;
    object_method(dsp64, gensym("dsp_add64"), x, rtrig_perform64, 0, NULL);
}

void rtrig_perform64(t_rtrig *x, t_object *dsp64, double **ins, 
                     long numins, double **outs, long numouts, long n,
                     long flags, void *userparam)
{
    double *out_vec = outs[0];

    if (x->mute) {
        memset(out_vec, 0, (size_t)n * sizeof(double));
        return;
    }

    const double min = x->min;
    const double max = x->max;
    const double range = max - min;
    const double odds = x->odds;
    uint32_t state = x->rand_state;

    for (long j = 0; j < n; j++) {
        double rval = xorshift32_normalized(&state);
        if (rval < odds) {
            out_vec[j] = min + range * xorshift32_normalized(&state);
        } else {
            out_vec[j] = 0.0;
        }
    }

    x->rand_state = state;
}