/**
 * @file phasemod~.c
 * @brief Phase-distortion / phase-modulation oscillator for Max 8/9.
 * Modernized for 64-bit dsp64, division-by-zero protection, and zero-leak heap teardown.
 */

#include "MSPd.h"
#include <math.h>
#include <string.h>

#define FUNC_LEN (32768)
#define OBJECT_NAME "phasemod~"

static t_class *phasemod_class;

typedef struct _phasemod {
    t_pxobject x_obj;
    t_float x_val;
    t_double mygain;
    t_double *wavetab;
    t_double phs;
    t_double bendphs;
    t_double frequency;
    t_double alpha;
    short mute;
    short connections[4];
    t_double si_fac;
    t_double sr;
} t_phasemod;

/* Method Prototypes */
void *phasemod_new(t_symbol *s, long argc, t_atom *argv);
void phasemod_free(t_phasemod *x);
void phasemod_assist(t_phasemod *x, void *b, long m, long a, char *s);
void phasemod_float(t_phasemod *x, double f);
void phasemod_int(t_phasemod *x, long n);
void phasemod_mute(t_phasemod *x, double toggle);
void phasemod_dsp64(t_phasemod *x, t_object *dsp64, short *count, double sr, long n, long flags);
void phasemod_perform64(t_phasemod *x, t_object *dsp64, double **ins, 
                        long numins, double **outs, long numouts, long n,
                        long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.phasemod~", 
                   (method)phasemod_new, 
                   (method)phasemod_free, 
                   (long)sizeof(t_phasemod), 
                   0L, 
                   A_GIMME, 
                   0);
    
    class_addmethod(c, (method)phasemod_dsp64,  "dsp64",  A_CANT,  0);
    class_addmethod(c, (method)phasemod_assist, "assist", A_CANT,  0);
    class_addmethod(c, (method)phasemod_mute,   "mute",   A_FLOAT, 0);
    class_addmethod(c, (method)phasemod_float,  "float",  A_FLOAT, 0);
    class_addmethod(c, (method)phasemod_int,    "int",    A_LONG,  0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    phasemod_class = c;
    
    potpourri_announce(OBJECT_NAME);
    return 0;
}

void phasemod_free(t_phasemod *x)
{
    dsp_free((t_pxobject *)x);
    if (x->wavetab) {
        sysmem_freeptr(x->wavetab);
        x->wavetab = NULL;
    }
}

void phasemod_mute(t_phasemod *x, double toggle)
{
    x->mute = (short)(toggle != 0.0);
}

void phasemod_assist(t_phasemod *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal/float) Frequency", 256); break;
            case 1: strncpy_zero(dst, "(signal/float) Slope Factor (Alpha)", 256); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Phase-Modulated Output", 256);
    }
}

void *phasemod_new(t_symbol *s, long argc, t_atom *argv)
{
    t_phasemod *x = (t_phasemod *)object_alloc(phasemod_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 2); // Inlet 0: Frequency, Inlet 1: Alpha
    outlet_new((t_object *)x, "signal");

    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->phs = 0.0;
    x->mute = 0;
    x->frequency = 440.0;
    x->alpha = 0.5;

    if (argc > 0) x->frequency = atom_getfloat(argv);
    if (argc > 1) x->alpha = atom_getfloat(argv + 1);

    x->wavetab = (t_double *)sysmem_newptrclear(FUNC_LEN * sizeof(t_double));
    for (int i = 0; i < FUNC_LEN; i++) {
        x->wavetab[i] = sin(TWOPI * ((t_double)i / (t_double)FUNC_LEN));
    }

    x->bendphs = 0.0;
    x->sr = sys_getsr();
    if (x->sr <= 0.0) x->sr = 44100.0;
    x->si_fac = 1.0 / x->sr;

    return x;
}

void phasemod_float(t_phasemod *x, double f)
{
    int inlet = (int)x->x_obj.z_in;
    if (inlet == 0) {
        x->frequency = f;
    } else if (inlet == 1) {
        x->alpha = f;
    }
}

void phasemod_int(t_phasemod *x, long n)
{
    phasemod_float(x, (double)n);
}

void phasemod_perform64(t_phasemod *x, t_object *dsp64, double **ins, 
                        long numins, double **outs, long numouts, long n,
                        long flags, void *userparam)
{
    t_double *frequency_vec = ins[0];
    t_double *alpha_vec     = ins[1];
    t_double *out           = outs[0];

    if (x->mute) {
        memset(out, 0, n * sizeof(t_double));
        return;
    }

    short *connections = x->connections;
    t_double bendphs = x->bendphs;
    t_double *wavetab = x->wavetab;
    t_double si_fac = x->si_fac;
    t_double incr = x->frequency * si_fac;
    t_double alpha = x->alpha;

    for (long i = 0; i < n; i++) {
        if (connections[1]) {
            alpha = alpha_vec[i];
        }

        // Guard against division by zero or log/exp domain collapse
        if (fabs(alpha) < 1.0e-5) alpha = 1.0e-5;
        if (fabs(alpha - 1.0) < 1.0e-5) alpha = 1.00001;

        if (connections[0]) {
            incr = frequency_vec[i] * si_fac;
        }

        if (incr < 0.0) incr = -incr;

        bendphs += incr;
        while (bendphs >= 1.0) bendphs -= 1.0;
        while (bendphs < 0.0)  bendphs += 1.0;

        t_double denom = 1.0 - exp(alpha);
        t_double phs = (denom != 0.0) ? ((1.0 - exp(bendphs * alpha)) / denom) : 0.0;
        phs *= (t_double)FUNC_LEN;

        while (phs < 0.0)           phs += (t_double)FUNC_LEN;
        while (phs >= (t_double)FUNC_LEN) phs -= (t_double)FUNC_LEN;

        long idx = (long)phs;
        if (idx < 0) idx = 0;
        else if (idx >= FUNC_LEN) idx = FUNC_LEN - 1;

        out[i] = wavetab[idx];
    }

    x->bendphs = bendphs;
}

void phasemod_dsp64(t_phasemod *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;

    x->connections[0] = count[0];
    x->connections[1] = count[1];

    if (x->sr != sr) {
        x->sr = sr;
        x->si_fac = 1.0 / x->sr;
    }
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)phasemod_perform64, 0, NULL);
}