/**
 * @file clickhold~.c
 * @brief Sample-and-hold unit triggered by non-zero signal clicks for Max 8/9.
 * Modernized for 64-bit dsp64 precision, buffer safety, and direct engine API linkage.
 */

#include "MSPd.h"

#define OBJECT_NAME "clickhold~"

static t_class *clickhold_class;

typedef struct _clickhold {
    t_pxobject x_obj;
    double hold_value;
} t_clickhold;

/* Method Prototypes */
void *clickhold_new(t_symbol *s, long argc, t_atom *argv);
void clickhold_assist(t_clickhold *x, void *b, long m, long a, char *s);
void clickhold_dsp64(t_clickhold *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void clickhold_perform64(t_clickhold *x, t_object *dsp64, double **ins, 
                         long numins, double **outs, long numouts, long vectorsize,
                         long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.clickhold~", 
                           (method)clickhold_new, 
                           (method)dsp_free, 
                           (long)sizeof(t_clickhold), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)clickhold_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)clickhold_assist, "assist", A_CANT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    clickhold_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void clickhold_assist(t_clickhold *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(signal) Non-Zero Trigger / Value Input", 256);
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Held Output Signal", 256);
    }
}

void *clickhold_new(t_symbol *s, long argc, t_atom *argv)
{
    t_clickhold *x = (t_clickhold *)object_alloc(clickhold_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);
    outlet_new((t_object *)x, "signal");

    x->x_obj.z_misc |= Z_NO_INPLACE;
    x->hold_value = 0.0;

    return x;
}

void clickhold_perform64(t_clickhold *x, t_object *dsp64, double **ins, 
                         long numins, double **outs, long numouts, long vectorsize,
                         long flags, void *userparam)
{
    t_double *in_vec  = ins[0];
    t_double *out_vec = outs[0];
    long n = vectorsize;
    double hold_value = x->hold_value;

    while (n--) {
        double val = *in_vec++;
        if (val != 0.0) {
            hold_value = val;
        }
        *out_vec++ = hold_value;
    }

    x->hold_value = hold_value;
}

void clickhold_dsp64(t_clickhold *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)clickhold_perform64, 0, NULL);
}