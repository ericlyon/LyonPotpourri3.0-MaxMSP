/**
 * @file epluribus~.c
 * @brief Multi-inlet signal selector (maximum/minimum amplitude picker) for Max 8/9.
 * Modernized for 64-bit dsp64, pre-allocation validation, and thread safety.
 */

#include "MSPd.h"
#include <math.h>

#define MAXBEATS (256)
#define OBJECT_NAME "epluribus~"
#define COMPILE_DATE "5.3.08"
#define OBJECT_VERSION "2.0"

static t_class *epluribus_class;

typedef struct _epluribus {
    t_pxobject x_obj; 
    int incount;   // How many inlets (must be between 2 and 256)
    short inverse; // Flag to look for minimum amplitude instead of maximum
} t_epluribus;

/* Method Prototypes */
void *epluribus_new(t_symbol *msg, long argc, t_atom *argv);
void epluribus_assist(t_epluribus *x, void *b, long m, long a, char *s);
void epluribus_inverse(t_epluribus *x, double tog);
void epluribus_dsp64(t_epluribus *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void epluribus_perform64(t_epluribus *x, t_object *dsp64, double **ins, 
                         long numins, double **outs, long numouts, long n,
                         long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.epluribus~", 
                   (method)epluribus_new, 
                   (method)dsp_free, 
                   (long)sizeof(t_epluribus), 
                   0L, 
                   A_GIMME, 
                   0);
    
    class_addmethod(c, (method)epluribus_assist, "assist",  A_CANT, 0);
    class_addmethod(c, (method)epluribus_inverse,"inverse", A_FLOAT, 0);
    class_addmethod(c, (method)epluribus_dsp64,  "dsp64",   A_CANT,  0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    epluribus_class = c;    

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void epluribus_inverse(t_epluribus *x, double tog)
{
    x->inverse = (short)(tog != 0.0);
}

void epluribus_assist(t_epluribus *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        snprintf(dst, 256, "(signal) Input %ld", arg + 1); 
    } else if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Selected Sample Value", 256); break;
            case 1: strncpy_zero(dst, "(signal) Selected Inlet Number (1-indexed)", 256); break;
        }
    }
}

void *epluribus_new(t_symbol *msg, long argc, t_atom *argv)
{
    int incount = 2; // Default inlet count

    if (argc > 0) {
        incount = (int)atom_getfloat(argv);
    }

    if (incount < 2 || incount > MAXBEATS) {
        object_error(NULL, "%s: inlet count must be between 2 and %d (got %d)", OBJECT_NAME, MAXBEATS, incount);
        return NULL;
    }

    t_epluribus *x = (t_epluribus *)object_alloc(epluribus_class);
    if (!x) return NULL;

    x->incount = incount;
    x->inverse = 0;

    dsp_setup((t_pxobject *)x, x->incount);
    x->x_obj.z_misc |= Z_NO_INPLACE;

    outlet_new((t_object *)x, "signal"); // Selected sample value
    outlet_new((t_object *)x, "signal"); // Selected inlet number

    return x;
}

void epluribus_perform64(t_epluribus *x, t_object *dsp64, double **ins, 
                         long numins, double **outs, long numouts, long n,
                         long flags, void *userparam)
{
    t_double *outlet = outs[0];
    t_double *selection = outs[1];

    if (!outlet || !selection || numins < 1) {
        for (long k = 0; k < n; k++) {
            if (outs[0]) outs[0][k] = 0.0;
            if (outs[1]) outs[1][k] = 0.0;
        }
        return;
    }

    if (x->inverse) {
        for (long k = 0; k < n; k++) {
            t_double minamp = 1.0e10;
            int minloc = 1;
            t_double minout = 0.0;

            for (int i = 0; i < numins; i++) {
                if (ins[i]) {
                    t_double val = ins[i][k];
                    t_double absval = fabs(val);
                    if (absval < minamp) {
                        minamp = absval;
                        minout = val;
                        minloc = i + 1;
                    }
                }
            }
            outlet[k] = minout;
            selection[k] = (t_double)minloc;
        }
    } else {
        for (long k = 0; k < n; k++) {
            t_double maxamp = -1.0;
            int maxloc = 1;
            t_double maxout = 0.0;

            for (int i = 0; i < numins; i++) {
                if (ins[i]) {
                    t_double val = ins[i][k];
                    t_double absval = fabs(val);
                    if (absval > maxamp) {
                        maxamp = absval;
                        maxout = val;
                        maxloc = i + 1;
                    }
                }
            }
            outlet[k] = maxout;
            selection[k] = (t_double)maxloc;
        }
    }    
}

void epluribus_dsp64(t_epluribus *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;

    if (x->incount < 2 || x->incount > MAXBEATS) {
        return;
    }

    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)epluribus_perform64, 0, NULL);
}