/**
 * @file sel~.c
 * @brief Sample-accurate signal value selector and click router for Max 8/9.
 * Modernized for 64-bit dsp64, zero-value match capability, and SIMD outlet clearing.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>

#define OBJECT_NAME "el.sel~"
#define MAX_SELECTORS (256)

static t_class *sel_class = NULL;

typedef struct _sel
{
    t_pxobject  x_obj;
    double     *matches; // Array of numeric targets to match against
    long        length;  // Number of match targets / signal outlets
} t_sel;

// Prototypes
void *sel_new(t_symbol *msg, short argc, t_atom *argv);
void  sel_free(t_sel *x);
void  sel_assist(t_sel *x, void *b, long msg, long arg, char *dst);
void  sel_dsp64(t_sel *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  sel_perform64(t_sel *x, t_object *dsp64, double **ins, 
                    long numins, double **outs, long numouts, long n,
                    long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.sel~", 
                           (method)sel_new, 
                           (method)sel_free, 
                           sizeof(t_sel), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)sel_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)sel_assist, "assist", A_CANT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    sel_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *sel_new(t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 1 || !argv) {
        object_error(NULL, "%s: requires at least one match target value", OBJECT_NAME);
        return NULL;
    }

    if (argc > MAX_SELECTORS) {
        object_error(NULL, "%s: exceeded maximum of %d match targets", OBJECT_NAME, MAX_SELECTORS);
        return NULL;
    }

    t_sel *x = (t_sel *)object_alloc(sel_class);
    if (!x) return NULL;

    x->length = argc;

    // Inlet 0 = Input signal to evaluate
    dsp_setup((t_pxobject *)x, 1);
    x->x_obj.z_misc |= Z_NO_INPLACE;

    for (long i = 0; i < x->length; i++) {
        outlet_new((t_pxobject *)x, "signal");
    }

    x->matches = (double *)sysmem_newptr((size_t)x->length * sizeof(double));
    if (!x->matches) {
        object_free(x);
        return NULL;
    }

    for (long i = 0; i < argc; i++) {
        x->matches[i] = atom_getfloatarg(i, argc, argv);
    }

    return x;
}

void sel_free(t_sel *x)
{
    dsp_free((t_pxobject *)x);
    if (x->matches) {
        sysmem_freeptr(x->matches);
        x->matches = NULL;
    }
}

// -------------------------------------------------------------------------
// Assist Tooltips
// -------------------------------------------------------------------------

void sel_assist(t_sel *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        if (arg == 0) {
            snprintf(dst, 256, "(signal) Value to Match");
        }
    } else if (msg == ASSIST_OUTLET) {
        if (arg < x->length) {
            snprintf(dst, 256, "(signal) Click if Matches %g", x->matches[arg]);
        }
    }
}

// -------------------------------------------------------------------------
// Real-Time DSP Perform Routine
// -------------------------------------------------------------------------

void sel_dsp64(t_sel *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    object_method(dsp64, gensym("dsp_add64"), x, sel_perform64, 0, NULL);
}

void sel_perform64(t_sel *x, t_object *dsp64, double **ins, 
                    long numins, double **outs, long numouts, long n,
                    long flags, void *userparam)
{
    long length = x->length;

    // Vectorized zeroing of all match outlets
    for (long j = 0; j < length; j++) {
        memset(outs[j], 0, (size_t)n * sizeof(double));
    }

    const double *inlet   = ins[0];
    const double *matches = x->matches;

    // Sample-accurate match testing (allows 0.0 matches)
    for (long i = 0; i < n; i++) {
        double val = inlet[i];
        for (long j = 0; j < length; j++) {
            if (val == matches[j]) {
                outs[j][i] = 1.0;
            }
        }
    }
}