/**
 * @file markov~.c
 * @brief Signal-rate Markov chain event generator and rhythmic sequencer for Max 8/9.
 * Modernized for 64-bit dsp64, full matrix memory management, and manual override safety.
 */

#include "MSPd.h"
#include <stdlib.h>
#include <math.h>

#define OBJECT_NAME "markov~"

static t_class *markov_class;

typedef struct _markov {
    t_pxobject x_obj;
    int event_count;
    int maximum_length;
    t_double **event_weights;
    int current_event;
    t_double *values;
    t_double current_value;
    int count;
    int event_samples;
    int subdiv;
    t_double tempo;
    float sr;
    short manual_override;
    short trigger;
} t_markov;

/* Method Prototypes */
void *markov_new(t_symbol *s, long argc, t_atom *argv);
void markov_free(t_markov *x);
void markov_assist(t_markov *x, void *b, long m, long a, char *dst);
int markov_domarkov(int current_event, t_double **event_weights, int event_count);
void markov_subdiv(t_markov *x, double subdiv);
void markov_tempo(t_markov *x, double tempo);
void markov_set_length(t_markov *x, double length);
void markov_manual_override(t_markov *x, double toggle);
void markov_values(t_markov *x, t_symbol *msg, long argc, t_atom *argv);
void markov_event_odds(t_markov *x, t_symbol *msg, long argc, t_atom *argv);
void markov_bang(t_markov *x);
void markov_dsp64(t_markov *x, t_object *dsp64, short *count, double sr, long n, long flags);
void markov_perform64(t_markov *x, t_object *dsp64, double **ins, 
                      long numins, double **outs, long numouts, long n,
                      long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.markov~", 
                           (method)markov_new, 
                           (method)markov_free, 
                           (long)sizeof(t_markov), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)markov_dsp64,           "dsp64",           A_CANT, 0);
    class_addmethod(c, (method)markov_assist,          "assist",          A_CANT, 0);
    class_addmethod(c, (method)markov_bang,            "bang",            0);
    class_addmethod(c, (method)markov_subdiv,          "subdiv",          A_FLOAT, 0);
    class_addmethod(c, (method)markov_tempo,           "tempo",           A_FLOAT, 0);
    class_addmethod(c, (method)markov_values,          "values",          A_GIMME, 0);
    class_addmethod(c, (method)markov_event_odds,      "event_odds",      A_GIMME, 0);
    class_addmethod(c, (method)markov_set_length,      "set_length",      A_FLOAT, 0);
    class_addmethod(c, (method)markov_manual_override, "manual_override", A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    markov_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void markov_free(t_markov *x)
{
    dsp_free((t_pxobject *)x);

    if (x->event_weights) {
        for (int i = 0; i < x->maximum_length; i++) {
            if (x->event_weights[i]) {
                sysmem_freeptr(x->event_weights[i]);
            }
        }
        sysmem_freeptr(x->event_weights);
        x->event_weights = NULL;
    }

    if (x->values) {
        sysmem_freeptr(x->values);
        x->values = NULL;
    }
}

void markov_manual_override(t_markov *x, double toggle)
{
    x->manual_override = (short)(toggle != 0.0);
}

void markov_bang(t_markov *x)
{
    x->trigger = 1;
}

void markov_values(t_markov *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc != x->event_count) {
        object_error((t_object *)x, "%s: requires exactly %d values", OBJECT_NAME, x->event_count);
        return;
    }
    for (int i = 0; i < x->event_count; i++) {
        x->values[i] = atom_getfloat(argv + i);
    }
}

void markov_event_odds(t_markov *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc != x->event_count + 1) {
        object_error((t_object *)x, "%s: event_odds requires %d values (event index followed by weights)", 
                     OBJECT_NAME, x->event_count + 1);
        return;
    }

    int event = (int)atom_getfloat(argv);
    if (event < 0 || event >= x->event_count) {
        object_error((t_object *)x, "%s: event index %d out of range (0 to %d)", 
                     OBJECT_NAME, event, x->event_count - 1);
        return;
    }

    t_double sum = 0.0;
    for (int i = 0; i < x->event_count; i++) {
        x->event_weights[event][i] = atom_getfloat(argv + 1 + i);
        sum += x->event_weights[event][i];
    }

    if (sum <= 0.0) {
        object_error((t_object *)x, "%s: weights must sum to a positive number", OBJECT_NAME);
        return;
    }

    // Normalize weights to sum to 1.0
    double inv_sum = 1.0 / sum;
    for (int i = 0; i < x->event_count; i++) {
        x->event_weights[event][i] *= inv_sum;
    }
}

void markov_set_length(t_markov *x, double length)
{
    int len = (int)length;
    if (len < 1 || len > x->maximum_length) {
        object_error((t_object *)x, "%s: length %d out of range (1 to %d)", OBJECT_NAME, len, x->maximum_length);
        return;
    }
    x->event_count = len;
    if (x->current_event >= x->event_count) {
        x->current_event = 0;
        x->current_value = x->values[0];
    }
}

void markov_tempo(t_markov *x, double tempo)
{
    if (tempo <= 0.0) return;
    x->tempo = (t_double)tempo;
    if (x->subdiv < 1) x->subdiv = 1;
    x->event_samples = (int)((double)x->sr * (60.0 / x->tempo) / (double)x->subdiv);
    if (x->event_samples < 1) x->event_samples = 1;
}

void markov_subdiv(t_markov *x, double subdiv)
{
    int s = (int)subdiv;
    if (s < 1) s = 1;
    if (s > 128) s = 128;
    x->subdiv = s;
    x->event_samples = (int)((double)x->sr * (60.0 / x->tempo) / (double)x->subdiv);
    if (x->event_samples < 1) x->event_samples = 1;
}

void markov_assist(t_markov *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(bang/messages) Trigger Next Event / Control Messages", 256);
    } else if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Event Value Output", 256); break;
            case 1: strncpy_zero(dst, "(signal) Rhythmic Phase Sync [0.0 - 1.0]", 256); break;
        }
    }
}

void *markov_new(t_symbol *s, long argc, t_atom *argv)
{
    t_markov *x = (t_markov *)object_alloc(markov_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 0); // Control-rate message inlet only
    outlet_new((t_object *)x, "signal"); // Output values
    outlet_new((t_object *)x, "signal"); // Sync phasor
    x->x_obj.z_misc |= Z_NO_INPLACE;

    long max_len = (argc > 0) ? (long)atom_getfloat(argv) : 16;
    if (max_len < 2 || max_len > 256) max_len = 16;

    x->maximum_length = (int)max_len;
    x->event_count = 4;
    x->count = 0;

    x->event_weights = (t_double **)sysmem_newptrclear(x->maximum_length * sizeof(t_double *));
    for (int i = 0; i < x->maximum_length; i++) {
        x->event_weights[i] = (t_double *)sysmem_newptrclear(x->maximum_length * sizeof(t_double));
    }
    x->values = (t_double *)sysmem_newptrclear(x->maximum_length * sizeof(t_double));

    x->values[0] = 300.0;
    x->values[1] = 400.0;
    x->values[2] = 500.0;
    x->values[3] = 600.0;

    // Default transition matrix
    x->event_weights[0][1] = 0.5; x->event_weights[0][2] = 0.5;
    x->event_weights[1][0] = 0.25; x->event_weights[1][1] = 0.5; x->event_weights[1][3] = 0.25;
    x->event_weights[2][0] = 1.0;
    x->event_weights[3][0] = 0.33; x->event_weights[3][1] = 0.33; x->event_weights[3][2] = 0.34;

    x->current_event = 0;
    x->current_value = x->values[0];
    x->tempo = 60.0;
    x->subdiv = 1;
    x->manual_override = 0;
    x->trigger = 0;

    x->sr = (float)sys_getsr();
    if (x->sr <= 0.0f) x->sr = 44100.0f;

    x->event_samples = (int)((double)x->sr * (60.0 / x->tempo) / (double)x->subdiv);
    if (x->event_samples < 1) x->event_samples = 1;

    return x;
}

int markov_domarkov(int current_event, t_double **event_weights, int event_count)
{
    double randval = (double)rand() / (double)RAND_MAX;

    for (int i = 0; i < event_count; i++) {
        if (i == event_count - 1 || randval < event_weights[current_event][i]) {
            return i;
        }
        randval -= event_weights[current_event][i];
    }
    return 0;
}

void markov_perform64(t_markov *x, t_object *dsp64, double **ins, 
                      long numins, double **outs, long numouts, long n,
                      long flags, void *userparam)
{
    t_double *out  = outs[0];
    t_double *sync = outs[1];

    int count = x->count;
    int event_samples = x->event_samples;
    int event_count = x->event_count;
    t_double **event_weights = x->event_weights;
    int current_event = x->current_event;
    t_double *values = x->values;
    t_double current_value = x->current_value;

    if (event_samples < 1) event_samples = 1;
    double inv_samples = 1.0 / (double)event_samples;

    if (x->manual_override) {
        while (n--) {
            if (x->trigger) {
                current_event = markov_domarkov(current_event, event_weights, event_count);
                current_value = values[current_event];
                x->trigger = 0;
                *sync++ = 1.0;
            } else {
                *sync++ = 0.0;
            }
            *out++ = current_value;
        }
        x->current_value = current_value;
        x->current_event = current_event;
        return; // Guard prevents fall-through into automatic loop
    }

    while (n--) {
        if (++count >= event_samples) {
            current_event = markov_domarkov(current_event, event_weights, event_count);
            current_value = values[current_event];
            count = 0;
        }
        *sync++ = (double)count * inv_samples;
        *out++  = current_value;
    }

    x->current_value = current_value;
    x->count = count;
    x->current_event = current_event;
}

void markov_dsp64(t_markov *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    if (x->sr != (float)sr) {
        x->sr = (float)sr;
        x->event_samples = (int)((double)x->sr * (60.0 / x->tempo) / (double)x->subdiv);
        if (x->event_samples < 1) x->event_samples = 1;
        x->count = 0;
    }
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)markov_perform64, 0, NULL);
}