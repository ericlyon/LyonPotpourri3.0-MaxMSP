/**
 * @file counter~.c
 * @brief Signal-triggered sample-rate counter external for Max 8/9.
 * Modernized for 64-bit dsp64, initial-state consistency, and direct engine API linkage.
 */

#include "MSPd.h"

#define OBJECT_NAME "counter~"

#define COUNTER_UP (1)
#define COUNTER_DOWN (-1)

static t_class *counter_class;

typedef struct _counter {
    t_pxobject x_obj;
    t_atom_long current;
    t_atom_long min;
    t_atom_long max;
    short direction;
} t_counter;

/* Method Prototypes */
void *counter_new(t_symbol *s, long argc, t_atom *argv);
void counter_assist(t_counter *x, void *b, long m, long a, char *s);
void counter_setnext(t_counter *x, double val);
void counter_direction(t_counter *x, double d);
void counter_minmax(t_counter *x, double min, double max);
void counter_dsp64(t_counter *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void counter_perform64(t_counter *x, t_object *dsp64, double **ins, 
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.counter~", 
                           (method)counter_new, 
                           (method)dsp_free, 
                           (long)sizeof(t_counter), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)counter_dsp64,     "dsp64",     A_CANT, 0);
    class_addmethod(c, (method)counter_assist,    "assist",    A_CANT, 0);
    class_addmethod(c, (method)counter_minmax,    "minmax",    A_FLOAT, A_FLOAT, 0);
    class_addmethod(c, (method)counter_direction, "direction", A_FLOAT, 0);
    class_addmethod(c, (method)counter_setnext,   "setnext",   A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    counter_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void counter_setnext(t_counter *x, double val)
{
    long v = (long)val;
    if (v < x->min || v > x->max) return;
    x->current = v;
}

void counter_direction(t_counter *x, double d)
{
    short dir = (short)d;
    if (dir != COUNTER_UP && dir != COUNTER_DOWN) return;
    x->direction = dir;
}

void counter_minmax(t_counter *x, double min, double max)
{
    long imin = (long)min;
    long imax = (long)max;

    if (imin < 1 || imin >= imax) return;

    x->min = imin;
    x->max = imax;

    if (x->current < x->min) x->current = x->min;
    else if (x->current > x->max) x->current = x->max;
}

void counter_assist(t_counter *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(signal) Non-Zero Trigger Input", 256);
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Counter Output Values", 256);
    }
}

void *counter_new(t_symbol *s, long argc, t_atom *argv)
{
    t_counter *x = (t_counter *)object_alloc(counter_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);
    outlet_new((t_object *)x, "signal");

    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->min = 1;
    x->max = 10;
    x->direction = COUNTER_UP;

    if (argc > 0) x->min = atom_getlong(argv);
    if (argc > 1) x->max = atom_getlong(argv + 1);

    if (x->min < 1) x->min = 1;
    if (x->max <= x->min) x->max = x->min + 9;

    x->current = x->min;

    return x;
}

void counter_perform64(t_counter *x, t_object *dsp64, double **ins, 
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam)
{
    t_double *in_vec  = ins[0];
    t_double *out_vec = outs[0];

    long min = x->min;
    long max = x->max;
    long current = x->current;
    short direction = x->direction;

    for (long i = 0; i < n; i++) {
        if (in_vec[i] != 0.0) {
            out_vec[i] = (double)current;
            current += direction;

            if (direction == COUNTER_UP) {
                if (current > max) current = min;
            } else if (direction == COUNTER_DOWN) {
                if (current < min) current = max;
            }
        } else {
            out_vec[i] = 0.0;
        }
    }

    x->current = current;
}

void counter_dsp64(t_counter *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)counter_perform64, 0, NULL);
}