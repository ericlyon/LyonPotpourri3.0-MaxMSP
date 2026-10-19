/**
 * @file clean_selector~.c
 * @brief Multi-channel signal selector with click-free equal-power or linear crossfades for Max 8/9.
 * Modernized for 64-bit dsp64, inlet array bounds safety, and zero-leak teardown.
 */

#include "MSPd.h"
#include <math.h>

#define MAX_CHANS (64)
#define CS_LINEAR (0)
#define CS_POWER (1)
#define OBJECT_NAME "clean_selector~"

static t_class *clean_selector_class;

typedef struct _clean_selector {
    t_pxobject x_obj;
    short input_chans;
    short active_chan;
    short last_chan;
    int samps_to_fade;
    int fadesamps;
    t_double fadetime;
    t_double pi_over_two;
    short fadetype;
    short *connected_list;
    t_double sr;
    t_double vs;
    int inlet_count;
    short linear; // Flag to use linear crossfade instead of power
} t_clean_selector;

/* Method Prototypes */
void *clean_selector_new(t_symbol *s, long argc, t_atom *argv);
void clean_selector_dsp_free(t_clean_selector *x);
void clean_selector_assist(t_clean_selector *x, void *b, long m, long a, char *s);
void clean_selector_float(t_clean_selector *x, double f);
void clean_selector_int(t_clean_selector *x, long i);
void clean_selector_channel(t_clean_selector *x, double i);
void clean_selector_fadetime(t_clean_selector *x, double f);
void clean_selector_linear(t_clean_selector *x, double tog);
void clean_selector_perform64(t_clean_selector *x, t_object *dsp64, double **ins, 
                              long numins, double **outs, long numouts, long n,
                              long flags, void *userparam);
void clean_selector_dsp64(t_clean_selector *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.clean_selector~", 
                           (method)clean_selector_new, 
                           (method)clean_selector_dsp_free, 
                           (long)sizeof(t_clean_selector), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)clean_selector_assist,   "assist",   A_CANT, 0);
    class_addmethod(c, (method)clean_selector_dsp64,    "dsp64",    A_CANT, 0);
    class_addmethod(c, (method)clean_selector_fadetime, "fadetime", A_FLOAT, 0);
    class_addmethod(c, (method)clean_selector_channel,  "channel",  A_FLOAT, 0);
    class_addmethod(c, (method)clean_selector_linear,   "linear",   A_FLOAT, 0);
    class_addmethod(c, (method)clean_selector_float,    "float",    A_FLOAT, 0);
    class_addmethod(c, (method)clean_selector_int,      "int",      A_LONG,  0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    clean_selector_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void clean_selector_assist(t_clean_selector *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        if (arg == 0) {
            strncpy_zero(dst, "(signal/int) Input 0, Channel Select (0 to N-1)", 256);
        } else {
            snprintf(dst, 256, "(signal) Audio Input %ld", arg);
        }
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Selected Output", 256);
    }
}

void *clean_selector_new(t_symbol *s, long argc, t_atom *argv)
{
    t_clean_selector *x = (t_clean_selector *)object_alloc(clean_selector_class);
    if (!x) return NULL;

    x->sr = sys_getsr();
    if (x->sr <= 0.0) x->sr = 44100.0;

    x->fadetime = 0.05; // 50 ms default
    x->inlet_count = 8;
    x->linear = 0;

    if (argc >= 1) {
        x->inlet_count = (int)atom_getfloat(argv);
        if (x->inlet_count < 2 || x->inlet_count > MAX_CHANS) {
            object_error((t_object *)x, "%s: %d is an illegal number of inlets (2 - %d)", 
                         OBJECT_NAME, x->inlet_count, MAX_CHANS);
            return NULL;
        }
    }
    if (argc >= 2) {
        x->fadetime = atom_getfloat(argv + 1) * 0.001;
    }

    if (x->fadetime <= 0.0) x->fadetime = 0.05;

    x->fadesamps = (int)(x->fadetime * x->sr);
    if (x->fadesamps < 1) x->fadesamps = 1;

    dsp_setup((t_pxobject *)x, x->inlet_count);
    outlet_new((t_object *)x, "signal");

    x->x_obj.z_misc |= Z_NO_INPLACE;
    x->fadetype = CS_POWER;
    x->pi_over_two = 1.5707963267948966;
    x->active_chan = 0;
    x->last_chan = 0;
    x->samps_to_fade = 0;

    x->connected_list = (short *)sysmem_newptrclear(MAX_CHANS * sizeof(short));

    return x;
}

void clean_selector_dsp_free(t_clean_selector *x)
{
    dsp_free((t_pxobject *)x);
    if (x->connected_list) {
        sysmem_freeptr(x->connected_list);
        x->connected_list = NULL;
    }
}

void clean_selector_fadetime(t_clean_selector *x, double f)
{
    double fades = f * 0.001;
    if (fades < 0.0001 || fades > 1000.0) {
        object_error((t_object *)x, "%s: fade time constrained to 0.1 - 1000000 ms", OBJECT_NAME);
        return;
    }
    x->fadetime = fades;
    x->fadesamps = (int)(x->sr * x->fadetime);
    if (x->fadesamps < 1) x->fadesamps = 1;
    x->samps_to_fade = 0;
}

void clean_selector_linear(t_clean_selector *x, double tog)
{
    x->linear = (tog != 0.0);
}

void clean_selector_channel(t_clean_selector *x, double i)
{
    int chan = (int)i;
    if (chan < 0 || chan >= x->inlet_count) {
        object_error((t_object *)x, "%s: channel %d out of range (0 - %d)", OBJECT_NAME, chan, x->inlet_count - 1);
        return;
    }
    if (chan != x->active_chan) {
        x->last_chan = x->active_chan;
        x->active_chan = (short)chan;
        x->samps_to_fade = x->fadesamps;
    }
}

void clean_selector_int(t_clean_selector *x, long i)
{
    clean_selector_channel(x, (double)i);
}

void clean_selector_float(t_clean_selector *x, double f)
{
    clean_selector_channel(x, f);
}

void clean_selector_perform64(t_clean_selector *x, t_object *dsp64, double **ins, 
                              long numins, double **outs, long numouts, long n,
                              long flags, void *userparam)
{
    t_double *out = outs[0];
    short active_chan = x->active_chan;
    short last_chan   = x->last_chan;
    int fadesamps     = x->fadesamps;
    int samps_to_fade = x->samps_to_fade;
    double pi_over_two = x->pi_over_two;
    short linear      = x->linear;

    if (active_chan < 0 || active_chan >= numins) {
        for (long i = 0; i < n; i++) {
            out[i] = 0.0;
        }
        return;
    }

    short last_valid = (last_chan >= 0 && last_chan < numins);
    double inv_fadesamps = (fadesamps > 0) ? (1.0 / (double)fadesamps) : 1.0;

    for (long i = 0; i < n; i++) {
        double in_active = ins[active_chan][i];
        double in_last   = last_valid ? ins[last_chan][i] : 0.0;

        if (samps_to_fade >= 0) {
            double frac = (double)samps_to_fade * inv_fadesamps; // 1.0 down to 0.0

            if (linear) {
                out[i] = in_active + frac * (in_last - in_active);
            } else {
                double phase = pi_over_two * (1.0 - frac);
                double m1 = sin(phase);
                double m2 = cos(phase);
                out[i] = (in_active * m1) + (in_last * m2);
            }
            --samps_to_fade;
        } else {
            out[i] = in_active;
        }
    }

    x->samps_to_fade = samps_to_fade;
}

void clean_selector_dsp64(t_clean_selector *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;

    if (x->sr != samplerate) {
        x->sr = samplerate;
        x->fadesamps = (int)(x->fadetime * x->sr);
        if (x->fadesamps < 1) x->fadesamps = 1;
        x->samps_to_fade = 0;
    }

    // Safety: only copy up to active inlet count to prevent overreading count array
    for (int i = 0; i < x->inlet_count && i < MAX_CHANS; i++) {
        x->connected_list[i] = count[i];
    }

    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)clean_selector_perform64, 0, NULL);
}