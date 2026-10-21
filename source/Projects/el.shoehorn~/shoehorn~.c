/**
 * @file shoehorn~.c
 * @brief Multichannel audio downmixing and upmixing spatializer for Max 8/9.
 * Modernized for 64-bit dsp64, unified output accumulation, and zero-leak teardown.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define OBJECT_NAME "el.shoehorn~"
#define MIN_CHANS (2)
#define MAX_CHANS (1024)

static t_class *shoehorn_class = NULL;

typedef struct _shoehorn
{
    t_pxobject x_obj;
    long       inChans;
    long       outChans;
    double     pio2;
    double     advFrac;
    double    *pangains1;
    double    *pangains2;
    long      *indexList;
} t_shoehorn;

// Prototypes
void *shoehorn_new(t_symbol *s, short argc, t_atom *argv);
void  shoehorn_free(t_shoehorn *x);
void  shoehorn_assist(t_shoehorn *x, void *b, long msg, long arg, char *dst);
void  shoehorn_dsp64(t_shoehorn *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  shoehorn_perform64(t_shoehorn *x, t_object *dsp64, double **ins,
                        long numins, double **outs, long numouts, long n,
                        long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.shoehorn~", 
                           (method)shoehorn_new, 
                           (method)shoehorn_free, 
                           sizeof(t_shoehorn), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)shoehorn_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)shoehorn_assist, "assist", A_CANT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    shoehorn_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *shoehorn_new(t_symbol *s, short argc, t_atom *argv)
{
    t_atom_long in_c = 2;
    t_atom_long out_c = 2;

    if (argc > 0 && argv) atom_arg_getlong(&in_c, 0, argc, argv);
    if (argc > 1 && argv) atom_arg_getlong(&out_c, 1, argc, argv);

    if (in_c < MIN_CHANS || out_c < MIN_CHANS) {
        object_error(NULL, "%s: illegal channel count: [in = %ld] [out = %ld] (minimum %d)", 
                     OBJECT_NAME, (long)in_c, (long)out_c, MIN_CHANS);
        return NULL;
    }
    if (in_c > MAX_CHANS || out_c > MAX_CHANS) {
        object_error(NULL, "%s: channel count exceeds maximum of %d", OBJECT_NAME, MAX_CHANS);
        return NULL;
    }

    t_shoehorn *x = (t_shoehorn *)object_alloc(shoehorn_class);
    if (!x) return NULL;

    x->inChans  = (long)in_c;
    x->outChans = (long)out_c;

    dsp_setup((t_pxobject *)x, x->inChans);
    x->x_obj.z_misc |= Z_NO_INPLACE;

    for (long i = 0; i < x->outChans; i++) {
        outlet_new((t_pxobject *)x, "signal");
    }

    x->pio2 = PI / 2.0;
    x->advFrac = (double)(x->outChans - 1) / (double)(x->inChans - 1);

    x->pangains1 = (double *)sysmem_newptr((size_t)x->inChans * sizeof(double));
    x->pangains2 = (double *)sysmem_newptr((size_t)x->inChans * sizeof(double));
    x->indexList = (long *)sysmem_newptr((size_t)x->inChans * sizeof(long));

    if (!x->pangains1 || !x->pangains2 || !x->indexList) {
        object_error((t_object *)x, "memory allocation failed");
        shoehorn_free(x);
        return NULL;
    }

    // Precalculate fixed spatial panning tables for intermediate input channels
    for (long i = 1; i < x->inChans - 1; i++) {
        double fullFrac = (double)i * x->advFrac;
        long outIndex = (long)floor(fullFrac);
        double thisFrac = fullFrac - (double)outIndex;
        double panloc = thisFrac * x->pio2;

        x->indexList[i] = outIndex;
        x->pangains1[i] = cos(panloc);
        x->pangains2[i] = sin(panloc);
    }

    return x;
}

void shoehorn_free(t_shoehorn *x)
{
    dsp_free((t_pxobject *)x);
    if (x->pangains1) {
        sysmem_freeptr(x->pangains1);
        x->pangains1 = NULL;
    }
    if (x->pangains2) {
        sysmem_freeptr(x->pangains2);
        x->pangains2 = NULL;
    }
    if (x->indexList) {
        sysmem_freeptr(x->indexList);
        x->indexList = NULL;
    }
}

// -------------------------------------------------------------------------
// Assist Tooltips
// -------------------------------------------------------------------------

void shoehorn_assist(t_shoehorn *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        if (arg < x->inChans) {
            snprintf(dst, 256, "(signal) Audio Input %ld", arg + 1);
        }
    } else if (msg == ASSIST_OUTLET) {
        if (arg < x->outChans) {
            snprintf(dst, 256, "(signal) Audio Output %ld", arg + 1);
        }
    }
}

// -------------------------------------------------------------------------
// Real-Time DSP Perform Routine
// -------------------------------------------------------------------------

void shoehorn_dsp64(t_shoehorn *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    object_method(dsp64, gensym("dsp_add64"), x, shoehorn_perform64, 0, NULL);
}

void shoehorn_perform64(t_shoehorn *x, t_object *dsp64, double **ins,
                        long numins, double **outs, long numouts, long n,
                        long flags, void *userparam)
{
    const long inChans  = x->inChans;
    const long outChans = x->outChans;

    // Vectorized zeroing of all output channels
    for (long chan = 0; chan < outChans; chan++) {
        memset(outs[chan], 0, (size_t)n * sizeof(double));
    }

    const double *pangains1 = x->pangains1;
    const double *pangains2 = x->pangains2;
    const long   *indexList = x->indexList;

    const double *in_first = ins[0];
    const double *in_last  = ins[inChans - 1];
    double       *out_first = outs[0];
    double       *out_last  = outs[outChans - 1];

    for (long j = 0; j < n; j++) {
        // Accumulate boundary channels
        out_first[j] += in_first[j];
        out_last[j]  += in_last[j];

        // Spread intermediate channels across their mapped output pair
        for (long chan = 1; chan < inChans - 1; chan++) {
            long outIndex = indexList[chan];
            double sample = ins[chan][j];

            outs[outIndex][j]     += pangains1[chan] * sample;
            outs[outIndex + 1][j] += pangains2[chan] * sample;
        }
    }
}