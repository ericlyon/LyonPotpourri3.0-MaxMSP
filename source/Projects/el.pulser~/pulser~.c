/**
 * @file pulser~.c
 * @brief Pulse waveform synthesizer with band-limited harmonic summation for Max 8/9.
 * Modernized for 64-bit dsp64, O(1) wavetable wrapping, and zero-leak heap teardown.
 */

#include "MSPd.h"
#include <string.h>
#include <math.h>

#define FUNC_LEN (16384)
#define FUNC_LEN_MASK (16383)
#define FUNC_LEN_OVER2 (8192)
#define MAX_COMPONENTS (256)
#define OBJECT_NAME "el.pulser~"

static t_class *pulser_class = NULL;

typedef struct _pulser
{
    t_pxobject  x_obj;
    int         components;
    double      global_gain;
    double     *wavetab;
    double     *phases;
    double      frequency;
    double      pulsewidth;
    double      si_fac;
    short       mute;
    short       connected[2];
    double      sr;
} t_pulser;

// Prototypes
void *pulser_new(t_symbol *s, short argc, t_atom *argv);
void  pulser_free(t_pulser *x);
void  pulser_assist(t_pulser *x, void *b, long m, long a, char *s);
void  pulser_mute(t_pulser *x, double toggle);
void  pulser_harmonics(t_pulser *x, double c);
void  pulser_float(t_pulser *x, double f);
void  pulser_dsp64(t_pulser *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  pulser_perform64(t_pulser *x, t_object *dsp64, double **ins,
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.pulser~", 
                           (method)pulser_new, 
                           (method)pulser_free, 
                           sizeof(t_pulser), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)pulser_dsp64,     "dsp64",     A_CANT,  0);
    class_addmethod(c, (method)pulser_assist,    "assist",    A_CANT,  0);
    class_addmethod(c, (method)pulser_harmonics, "harmonics", A_FLOAT, 0);
    class_addmethod(c, (method)pulser_mute,      "mute",      A_FLOAT, 0);
    class_addmethod(c, (method)pulser_float,     "float",     A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    pulser_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *pulser_new(t_symbol *s, short argc, t_atom *argv)
{
    t_pulser *x = (t_pulser *)object_alloc(pulser_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 2);
    x->x_obj.z_misc |= Z_NO_INPLACE;
    outlet_new((t_pxobject *)x, "signal");

    x->sr = sys_getsr();
    if (x->sr <= 0.0) {
        x->sr = 44100.0;
    }

    x->mute = 0;
    x->components = 8;
    x->frequency = 440.0;
    x->pulsewidth = 0.5;

    if (argc > 0)
        x->frequency = atom_getfloatarg(0, argc, argv);
    if (argc > 1)
        x->components = (int)atom_getfloatarg(1, argc, argv);

    if (x->components < 1 || x->components > MAX_COMPONENTS) {
        object_error((t_object *)x, "%d is an illegal number of components, setting to 8", x->components);
        x->components = 8;
    }

    x->si_fac = (double)FUNC_LEN / x->sr;
    x->global_gain = 1.0 / (double)x->components;

    x->phases  = (double *)sysmem_newptr(MAX_COMPONENTS * sizeof(double));
    x->wavetab = (double *)sysmem_newptr(FUNC_LEN * sizeof(double));

    if (!x->phases || !x->wavetab) {
        object_error((t_object *)x, "memory allocation failed");
        pulser_free(x);
        return NULL;
    }

    memset(x->phases, 0, MAX_COMPONENTS * sizeof(double));

    for (int i = 0; i < FUNC_LEN; i++) {
        x->wavetab[i] = sin(TWOPI * ((double)i / (double)FUNC_LEN));
    }

    return x;
}

void pulser_free(t_pulser *x)
{
    dsp_free((t_pxobject *)x);
    if (x->phases) {
        sysmem_freeptr(x->phases);
        x->phases = NULL;
    }
    if (x->wavetab) {
        sysmem_freeptr(x->wavetab);
        x->wavetab = NULL;
    }
}

// -------------------------------------------------------------------------
// Control Handlers
// -------------------------------------------------------------------------

void pulser_mute(t_pulser *x, double toggle)
{
    x->mute = (short)(toggle != 0.0);
}

void pulser_assist(t_pulser *x, void *b, long msg, long arg, char *dst)
{
    if (msg == 1) {
        switch (arg) {
            case 0: snprintf(dst, 256, "(signal/float) Frequency"); break;
            case 1: snprintf(dst, 256, "(signal/float) Pulse Width"); break;
        }
    } else if (msg == 2) {
        snprintf(dst, 256, "(signal) Output");
    }
}

void pulser_harmonics(t_pulser *x, double c)
{
    int val = (int)c;
    if (val < 1 || val > MAX_COMPONENTS) {
        object_error((t_object *)x, "harmonic count %d out of bounds (1 - %d)", val, MAX_COMPONENTS);
        return;
    }
    x->components = val;
    x->global_gain = 1.0 / (double)x->components;
}

void pulser_float(t_pulser *x, double f)
{
    long inlet = ((t_pxobject *)x)->z_in;
    if (inlet == 0) {
        x->frequency = f;
    } else if (inlet == 1) {
        x->pulsewidth = f;
    }
}

// -------------------------------------------------------------------------
// Real-Time Perform Routine
// -------------------------------------------------------------------------

void pulser_dsp64(t_pulser *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;

    if (x->sr != sr) {
        x->sr = sr;
        x->si_fac = (double)FUNC_LEN / x->sr;
    }

    x->connected[0] = count[0];
    x->connected[1] = count[1];

    object_method(dsp64, gensym("dsp_add64"), x, pulser_perform64, 0, NULL);
}

void pulser_perform64(t_pulser *x, t_object *dsp64, double **ins,
                      long numins, double **outs, long numouts, long n,
                      long flags, void *userparam)
{
    double *out = outs[0];

    if (x->mute) {
        memset(out, 0, (size_t)n * sizeof(double));
        return;
    }

    const double *frequency_vec  = ins[0];
    const double *pulsewidth_vec = ins[1];
    const double *wavetab        = x->wavetab;
    double       *phases         = x->phases;

    const double  si_fac         = x->si_fac;
    const double  global_gain    = x->global_gain;
    const int     components     = x->components;
    const short   conn_freq      = x->connected[0];
    const short   conn_pw        = x->connected[1];

    double pulsewidth = x->pulsewidth;
    double frequency  = x->frequency;

    for (long s = 0; s < n; s++) {
        if (conn_pw) {
            pulsewidth = pulsewidth_vec[s];
        }
        if (pulsewidth < 0.0) pulsewidth = 0.0;
        if (pulsewidth > 1.0) pulsewidth = 1.0;

        if (conn_freq) {
            frequency = frequency_vec[s];
        }

        double incr = frequency * si_fac;
        double outsamp = 0.0;

        for (int i = 0; i < components; i++) {
            double harmonic = (double)(i + 1);

            // O(1) bitwise lookup index wrapping
            long lookdex = (long)((double)FUNC_LEN_OVER2 * pulsewidth * harmonic);
            double gain = wavetab[lookdex & FUNC_LEN_MASK];

            phases[i] += incr * harmonic;

            // Accurate phase wrapping without linear subtract loops
            if (phases[i] >= (double)FUNC_LEN) {
                phases[i] = fmod(phases[i], (double)FUNC_LEN);
            } else if (phases[i] < 0.0) {
                phases[i] = (double)FUNC_LEN + fmod(phases[i], (double)FUNC_LEN);
            }

            long phase_idx = (long)phases[i];
            outsamp += gain * wavetab[phase_idx & FUNC_LEN_MASK];
        }

        out[s] = outsamp * global_gain;
    }
}