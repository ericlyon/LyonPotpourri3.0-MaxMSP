/**
 * @file npan~.c
 * @brief Multi-channel circular audio panner for Max 8/9.
 * Modernized for 64-bit dsp64, safe channel count bounds, and smooth boundary panner behavior.
 */

#include "MSPd.h"
#include <math.h>

#define OBJECT_NAME "npan~"
#define SPEAKERMAX (1024)

static t_class *npan_class;

typedef struct _npan {
    t_pxobject x_obj;
    double pi_over_two;
    double twopi;
    int outcount;
} t_npan;

/* Method Prototypes */
void *npan_new(t_symbol *s, long argc, t_atom *argv);
void npan_assist(t_npan *x, void *b, long m, long a, char *s);
void npan_dsp64(t_npan *x, t_object *dsp64, short *count, double sr, long n, long flags);
void npan_perform64(t_npan *x, t_object *dsp64, double **ins, 
                    long numins, double **outs, long numouts, long n,
                    long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.npan~", 
                   (method)npan_new, 
                   (method)dsp_free, 
                   (long)sizeof(t_npan), 
                   0L, 
                   A_GIMME, 
                   0);
    
    class_addmethod(c, (method)npan_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)npan_assist, "assist", A_CANT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    npan_class = c;
    
    potpourri_announce(OBJECT_NAME);
    return 0;
}

void npan_assist(t_npan *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        if (arg == 0) {
            strncpy_zero(dst, "(signal) Audio Input", 256);
        } else if (arg == 1) {
            strncpy_zero(dst, "(signal) Pan Controller [0.0 - 1.0]", 256);
        }
    } else if (msg == ASSIST_OUTLET) {
        snprintf(dst, 256, "(signal) Audio Output %ld", arg + 1);
    }
}

void *npan_new(t_symbol *s, long argc, t_atom *argv)
{
    t_npan *x = (t_npan *)object_alloc(npan_class);
    if (!x) return NULL;

    x->outcount = (argc > 0) ? (int)atom_getfloat(argv) : 2;
    if (x->outcount < 2) {
        x->outcount = 2;
    } else if (x->outcount > SPEAKERMAX) {
        object_error((t_object *)x, "%s: output count %d exceeded maximum limit of %d, clamped", 
                     OBJECT_NAME, x->outcount, SPEAKERMAX);
        x->outcount = SPEAKERMAX;
    }

    dsp_setup((t_pxobject *)x, 2); // Inlet 0: Audio, Inlet 1: Panner
    for (int i = 0; i < x->outcount; i++) {
        outlet_new((t_object *)x, "signal");
    }

    x->x_obj.z_misc |= Z_NO_INPLACE;
    x->pi_over_two = 1.5707963267948966;
    x->twopi = 6.283185307179586;

    return x;
}

void npan_perform64(t_npan *x, t_object *dsp64, double **ins,
                    long numins, double **outs, long numouts, long n,
                    long flags, void *userparam)
{
    t_double *input  = ins[0];
    t_double *panner = ins[1];
    int outcount = x->outcount;
    double pi_over_two = x->pi_over_two;

    // Clean all output channels first
    for (int i = 0; i < numouts; i++) {
        memset((void *)outs[i], 0, n * sizeof(t_double));
    }

    if (!input || !panner) return;

    for (long i = 0; i < n; i++) {
        double panloc = panner[i];
        if (panloc < 0.0) panloc = 0.0;
        else if (panloc > 1.0) panloc = 1.0;

        // Map [0.0 - 1.0] across outcount speakers
        panloc *= (double)outcount;
        int chan1 = (int)floor(panloc);
        if (chan1 >= outcount) chan1 = outcount - 1;
        
        int chan2 = (chan1 + 1) % outcount;
        double frak = (panloc - (double)chan1) * pi_over_two;

        double gain1 = cos(frak);
        double gain2 = sin(frak);

        double insamp = input[i];

        if (chan1 >= 0 && chan1 < numouts) {
            outs[chan1][i] += insamp * gain1;
        }
        if (chan2 >= 0 && chan2 < numouts) {
            outs[chan2][i] += insamp * gain2;
        }
    }
}

void npan_dsp64(t_npan *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)npan_perform64, 0, NULL);
}