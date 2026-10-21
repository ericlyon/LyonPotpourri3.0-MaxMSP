/**
 * @file waveshape~.c
 * @brief Chebyshev polynomial waveshaper and distortion synthesis for Max 8/9.
 * Modernized for 64-bit dsp64, harmonic array bounds protection, and zero-leak teardown.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define OBJECT_NAME "el.waveshape~"
#define WS_MAXHARMS (256)
#define WS_TABLE_LEN (65536) // 1 << 16

static t_class *waveshape_class = NULL;

typedef struct _waveshape
{
    t_pxobject x_obj;
    long       flen;
    double    *wavetab;
    double    *tempeh;
    long       hcount;
    double    *harms;
    short      mute;
} t_waveshape;

// Prototypes
void *waveshape_new(t_symbol *s, short argc, t_atom *argv);
void  waveshape_free(t_waveshape *x);
void  waveshape_assist(t_waveshape *x, void *b, long m, long a, char *dst);
void  waveshape_list(t_waveshape *x, t_symbol *msg, short argc, t_atom *argv);
void  waveshape_mute(t_waveshape *x, double tog);
void  update_waveshape_function(t_waveshape *x);

void  waveshape_dsp64(t_waveshape *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  waveshape_perform64(t_waveshape *x, t_object *dsp64, double **ins, 
                         long numins, double **outs, long numouts, long n,
                         long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.waveshape~", 
                           (method)waveshape_new, 
                           (method)waveshape_free, 
                           sizeof(t_waveshape), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)waveshape_dsp64,  "dsp64",  A_CANT,  0);
    class_addmethod(c, (method)waveshape_assist, "assist", A_CANT,  0);
    class_addmethod(c, (method)waveshape_list,   "list",   A_GIMME, 0);
    class_addmethod(c, (method)waveshape_mute,   "mute",   A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    waveshape_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *waveshape_new(t_symbol *s, short argc, t_atom *argv)
{
    t_waveshape *x = (t_waveshape *)object_alloc(waveshape_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);
    x->x_obj.z_misc |= Z_NO_INPLACE;
    outlet_new((t_pxobject *)x, "signal");

    x->flen    = WS_TABLE_LEN;
    x->wavetab = (double *)sysmem_newptrclear((size_t)x->flen * sizeof(double));
    x->tempeh  = (double *)sysmem_newptrclear((size_t)x->flen * sizeof(double));
    x->harms   = (double *)sysmem_newptrclear(WS_MAXHARMS * sizeof(double));

    if (!x->wavetab || !x->tempeh || !x->harms) {
        object_free(x);
        return NULL;
    }

    x->hcount = 4;
    x->harms[0] = 0.0;
    x->harms[1] = 0.33;
    x->harms[2] = 0.33;
    x->harms[3] = 0.33;
    x->mute = 0;

    if (argc > 0 && argv) {
        waveshape_list(x, gensym("list"), argc, argv);
    } else {
        update_waveshape_function(x);
    }

    return x;
}

void waveshape_free(t_waveshape *x)
{
    dsp_free((t_pxobject *)x);
    if (x->wavetab) {
        sysmem_freeptr(x->wavetab);
        x->wavetab = NULL;
    }
    if (x->tempeh) {
        sysmem_freeptr(x->tempeh);
        x->tempeh = NULL;
    }
    if (x->harms) {
        sysmem_freeptr(x->harms);
        x->harms = NULL;
    }
}

// -------------------------------------------------------------------------
// Control Methods & Polynomial Synthesis
// -------------------------------------------------------------------------

void waveshape_mute(t_waveshape *x, double tog)
{
    x->mute = (short)(tog != 0.0);
}

void waveshape_list(t_waveshape *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc <= 0 || !argv) return;

    long count = (argc > WS_MAXHARMS) ? WS_MAXHARMS : argc;
    for (long i = 0; i < count; i++) {
        x->harms[i] = atom_getfloat(&argv[i]);
    }
    x->hcount = count;
    update_waveshape_function(x);
}

void update_waveshape_function(t_waveshape *x)
{
    long flen = x->flen;
    long hcount = x->hcount;
    double *tempeh = x->tempeh;
    double *harms = x->harms;

    memset(tempeh, 0, (size_t)flen * sizeof(double));

    for (long i = 0; i < hcount; i++) {
        double weight = harms[i];
        if (weight != 0.0) {
            for (long j = 0; j < flen; j++) {
                double point = -1.0 + 2.0 * ((double)j / (double)(flen - 1));
                if (point < -1.0) point = -1.0;
                else if (point > 1.0) point = 1.0;

                // Chebyshev polynomial: T_i(x) = cos(i * acos(x))
                tempeh[j] += weight * cos((double)i * acos(point));
            }
        }
    }

    double min_val = tempeh[0];
    double max_val = tempeh[0];
    for (long j = 1; j < flen; j++) {
        if (tempeh[j] < min_val) min_val = tempeh[j];
        if (tempeh[j] > max_val) max_val = tempeh[j];
    }

    double range = max_val - min_val;
    if (range < 1e-12) {
        // Flat function guard: passthrough line
        for (long j = 0; j < flen; j++) {
            x->wavetab[j] = -1.0 + 2.0 * ((double)j / (double)(flen - 1));
        }
        return;
    }

    // Normalize from -1.0 to +1.0
    double inv_range = 2.0 / range;
    for (long j = 0; j < flen; j++) {
        x->wavetab[j] = -1.0 + (tempeh[j] - min_val) * inv_range;
    }
}

void waveshape_assist(t_waveshape *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        snprintf(dst, 256, "(signal) Audio Input [-1.0 to 1.0]");
    } else if (msg == ASSIST_OUTLET) {
        snprintf(dst, 256, "(signal) Waveshaped Output");
    }
}

// -------------------------------------------------------------------------
// Real-Time DSP Perform Routine
// -------------------------------------------------------------------------

void waveshape_dsp64(t_waveshape *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    object_method(dsp64, gensym("dsp_add64"), x, waveshape_perform64, 0, NULL);
}

void waveshape_perform64(t_waveshape *x, t_object *dsp64, double **ins, 
                         long numins, double **outs, long numouts, long n,
                         long flags, void *userparam)
{
    double *out = outs[0];

    if (x->mute || !x->wavetab) {
        memset(out, 0, (size_t)n * sizeof(double));
        return;
    }

    const double *in      = ins[0];
    const double *wavetab = x->wavetab;
    long          flenm1  = x->flen - 1;

    for (long s = 0; s < n; s++) {
        double insamp = in[s];

        if (insamp > 1.0) insamp = 1.0;
        else if (insamp < -1.0) insamp = -1.0;

        // Symmetric index mapping [0, flen - 1]
        long windex = (long)((insamp + 1.0) * 0.5 * (double)flenm1 + 0.5);
        if (windex < 0) windex = 0;
        else if (windex > flenm1) windex = flenm1;

        out[s] = wavetab[windex];
    }
}