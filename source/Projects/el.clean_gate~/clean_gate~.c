/**
 * @file clean_gate~.c
 * @brief Dynamic signal router and gate with click-free crossfades for Max 8/9.
 * Modernized for 64-bit dsp64, outlet boundary safety, and division-by-zero protection.
 */

#include "MSPd.h"
#include <math.h>

#define MAX_CHANS (8192)
#define CS_LINEAR (0)
#define CS_POWER (1)
#define OBJECT_NAME "clean_gate~"

static t_class *clean_gate_class;

typedef struct _clean_gate {
    t_pxobject x_obj;
    long output_chans;
    long active_chan;
    long last_chan;
    long gate;
    long current_gate;
    long last_gate;
    int samps_to_fade;
    int fadesamps;
    t_double fadetime;
    t_double pi_over_two;
    long fadetype;
    t_double sr;
    t_double vs;
    int outlet_count;
    long linear; // Flag to use linear crossfade instead of power
} t_clean_gate;

/* Method Prototypes */
void *clean_gate_new(t_symbol *s, long argc, t_atom *argv);
void clean_gate_dsp_free(t_clean_gate *x);
void clean_gate_assist(t_clean_gate *x, void *b, long m, long a, char *s);
void clean_gate_float(t_clean_gate *x, double f);
void clean_gate_int(t_clean_gate *x, long i);
void clean_gate_channel(t_clean_gate *x, double i);
void clean_gate_fadetime(t_clean_gate *x, double f);
void clean_gate_linear(t_clean_gate *x, double tog);
void clean_gate_perform64(t_clean_gate *x, t_object *dsp64, double **ins,
                          long numins, double **outs, long numouts, long n,
                          long flags, void *userparam);
void clean_gate_perform_sa64(t_clean_gate *x, t_object *dsp64, double **ins,
                             long numins, double **outs, long numouts, long n,
                             long flags, void *userparam);
void clean_gate_dsp64(t_clean_gate *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.clean_gate~", 
                           (method)clean_gate_new, 
                           (method)clean_gate_dsp_free, 
                           (long)sizeof(t_clean_gate), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)clean_gate_assist,   "assist",   A_CANT, 0);
    class_addmethod(c, (method)clean_gate_dsp64,    "dsp64",    A_CANT, 0);
    class_addmethod(c, (method)clean_gate_fadetime, "fadetime", A_FLOAT, 0);
    class_addmethod(c, (method)clean_gate_channel,  "channel",  A_FLOAT, 0);
    class_addmethod(c, (method)clean_gate_linear,   "linear",   A_FLOAT, 0);
    class_addmethod(c, (method)clean_gate_float,    "float",    A_FLOAT, 0);
    class_addmethod(c, (method)clean_gate_int,      "int",      A_LONG,  0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    clean_gate_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void clean_gate_assist(t_clean_gate *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal/int) Gate Select (0=Off, 1-N=Outlet)", 256); break;
            case 1: strncpy_zero(dst, "(signal) Audio Input", 256); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        snprintf(dst, 256, "(signal) Output %ld", arg + 1);
    }
}

void *clean_gate_new(t_symbol *s, long argc, t_atom *argv)
{
    t_clean_gate *x = (t_clean_gate *)object_alloc(clean_gate_class);
    if (!x) return NULL;

    x->sr = sys_getsr();
    if (x->sr <= 0.0) x->sr = 44100.0;

    x->fadetime = 0.05; // 50 ms default
    x->current_gate = 0;
    x->last_gate = 0;
    x->linear = 0;
    x->outlet_count = 2; // Default to 2 outlets

    if (argc >= 1) {
        x->outlet_count = (int)atom_getfloat(argv);
        if (x->outlet_count < 2 || x->outlet_count > MAX_CHANS) {
            object_error((t_object *)x, "%s: %d is an illegal number of outlets (2 - %d)", 
                         OBJECT_NAME, x->outlet_count, MAX_CHANS);
            return NULL;
        }
    }
    if (argc >= 2) {
        x->fadetime = atom_getfloat(argv + 1) * 0.001;
    }

    if (x->fadetime <= 0.0) x->fadetime = 0.05;

    x->fadesamps = (int)(x->fadetime * x->sr);
    if (x->fadesamps < 1) x->fadesamps = 1;

    dsp_setup((t_pxobject *)x, 2); // Inlet 0: Gate, Inlet 1: Signal
    for (int i = 0; i < x->outlet_count; i++) {
        outlet_new((t_object *)x, "signal");
    }

    x->x_obj.z_misc |= Z_NO_INPLACE;
    x->fadetype = CS_POWER;
    x->pi_over_two = 1.5707963267948966;
    x->active_chan = 0;
    x->last_chan = 0;
    x->samps_to_fade = 0;

    return x;
}

void clean_gate_dsp_free(t_clean_gate *x)
{
    dsp_free((t_pxobject *)x);
}

void clean_gate_fadetime(t_clean_gate *x, double f)
{
    double fades = f * 0.001;
    if (fades < 0.0001 || fades > 100.0) {
        object_error((t_object *)x, "%s: fade time constrained to 0.1 - 100000 ms", OBJECT_NAME);
        return;
    }
    x->fadetime = fades;
    x->fadesamps = (int)(x->sr * x->fadetime);
    if (x->fadesamps < 1) x->fadesamps = 1;
    x->samps_to_fade = 0;
}

void clean_gate_linear(t_clean_gate *x, double tog)
{
    x->linear = (tog != 0.0);
}

void clean_gate_channel(t_clean_gate *x, double i)
{
    long chan = (long)i;
    if (chan < 0 || chan > x->outlet_count) {
        object_error((t_object *)x, "%s: gate %ld out of range (0 - %d)", OBJECT_NAME, chan, x->outlet_count);
        return;
    }
    if (chan != x->current_gate) {
        x->last_gate = x->current_gate;
        x->current_gate = chan;
        x->samps_to_fade = x->fadesamps;
    }
}

void clean_gate_int(t_clean_gate *x, long i)
{
    clean_gate_channel(x, (double)i);
}

void clean_gate_float(t_clean_gate *x, double f)
{
    clean_gate_channel(x, f);
}

void clean_gate_perform64(t_clean_gate *x, t_object *dsp64, double **ins,
                          long numins, double **outs, long numouts, long n,
                          long flags, void *userparam)
{
    t_double *input = ins[1];
    long current_gate = x->current_gate;
    long last_gate = x->last_gate;
    int fadesamps = x->fadesamps;
    int samps_to_fade = x->samps_to_fade;
    double pi_over_two = x->pi_over_two;
    short linear = (short)x->linear;

    long current_chan = current_gate - 1;
    long last_chan = last_gate - 1;

    // Clear all output channels
    for (long i = 0; i < numouts; i++) {
        for (long j = 0; j < n; j++) {
            outs[i][j] = 0.0;
        }
    }

    // Gate remains off
    if (current_gate == 0 && last_gate == 0) {
        return;
    }

    // Safe bounds check
    short current_valid = (current_chan >= 0 && current_chan < numouts);
    short last_valid    = (last_chan >= 0 && last_chan < numouts);

    double inv_fadesamps = (fadesamps > 0) ? (1.0 / (double)fadesamps) : 1.0;

    for (long i = 0; i < n; i++) {
        double in_sample = input[i];

        if (samps_to_fade >= 0) {
            double frac = 1.0 - ((double)samps_to_fade * inv_fadesamps);
            double m1, m2;

            if (linear) {
                m1 = frac;
                m2 = 1.0 - frac;
            } else {
                double phase = pi_over_two * frac;
                m1 = sin(phase);
                m2 = cos(phase);
            }

            if (current_valid && current_gate != 0) {
                outs[current_chan][i] = in_sample * m1;
            }
            if (last_valid && last_gate != 0) {
                outs[last_chan][i] = in_sample * m2;
            }
            --samps_to_fade;
        } else {
            if (current_valid && current_gate != 0) {
                outs[current_chan][i] = in_sample;
            }
        }
    }

    x->samps_to_fade = samps_to_fade;
}

void clean_gate_perform_sa64(t_clean_gate *x, t_object *dsp64, double **ins,
                             long numins, double **outs, long numouts, long n,
                             long flags, void *userparam)
{
    t_double *gate_sig = ins[0];
    t_double *input    = ins[1];
    long current_gate  = x->current_gate;
    long last_gate     = x->last_gate;
    int fadesamps      = x->fadesamps;
    int samps_to_fade  = x->samps_to_fade;
    double pi_over_two = x->pi_over_two;
    short linear       = (short)x->linear;
    long outlet_count  = x->outlet_count;

    for (long i = 0; i < numouts; i++) {
        for (long j = 0; j < n; j++) {
            outs[i][j] = 0.0;
        }
    }

    double inv_fadesamps = (fadesamps > 0) ? (1.0 / (double)fadesamps) : 1.0;

    for (long i = 0; i < n; i++) {
        long in_chan = (long)gate_sig[i];

        if (in_chan >= 0 && in_chan <= outlet_count && in_chan != current_gate) {
            last_gate = current_gate;
            current_gate = in_chan;
            samps_to_fade = fadesamps;
        }

        double in_sample = input[i];
        long current_chan = current_gate - 1;
        long last_chan = last_gate - 1;

        short current_valid = (current_chan >= 0 && current_chan < numouts);
        short last_valid    = (last_chan >= 0 && last_chan < numouts);

        if (samps_to_fade >= 0) {
            double frac = 1.0 - ((double)samps_to_fade * inv_fadesamps);
            double m1, m2;

            if (linear) {
                m1 = frac;
                m2 = 1.0 - frac;
            } else {
                double phase = pi_over_two * frac;
                m1 = sin(phase);
                m2 = cos(phase);
            }

            if (current_valid && current_gate != 0) {
                outs[current_chan][i] = in_sample * m1;
            }
            if (last_valid && last_gate != 0) {
                outs[last_chan][i] = in_sample * m2;
            }
            --samps_to_fade;
        } else {
            if (current_valid && current_gate != 0) {
                outs[current_chan][i] = in_sample;
            }
        }
    }

    x->last_gate = last_gate;
    x->current_gate = current_gate;
    x->samps_to_fade = samps_to_fade;
}

void clean_gate_dsp64(t_clean_gate *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;

    if (x->sr != samplerate) {
        x->sr = samplerate;
        x->fadesamps = (int)(x->fadetime * x->sr);
        if (x->fadesamps < 1) x->fadesamps = 1;
        x->samps_to_fade = 0;
    }

    if (count[0]) {
        dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)clean_gate_perform_sa64, 0, NULL);
    } else {
        dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)clean_gate_perform64, 0, NULL);
    }
}