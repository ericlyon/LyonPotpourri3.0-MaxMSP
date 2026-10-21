/**
 * @file quadpanin~.c
 * @brief 4-channel vector/distance panning external for Max 8/9.
 * Modernized for 64-bit dsp64, input coordinate clamping, and zero-leak heap teardown.
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "MSPd.h"
#include <math.h>
#include <stdio.h>

#define OBJECT_NAME "el.quadpanin~"

static t_class *quadpanin_class = NULL;

typedef struct _quadpanin
{
    t_pxobject x_obj;
} t_quadpanin;

// Prototypes
void *quadpanin_new(t_symbol *s, short argc, t_atom *argv);
void  quadpanin_free(t_quadpanin *x);
void  quadpanin_assist(t_quadpanin *x, void *b, long msg, long arg, char *dst);
void  quadpanin_dsp64(t_quadpanin *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  quadpanin_perform64(t_quadpanin *x, t_object *dsp64, double **ins,
                          long numins, double **outs, long numouts, long n,
                          long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.quadpanin~", 
                           (method)quadpanin_new, 
                           (method)quadpanin_free, 
                           sizeof(t_quadpanin), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)quadpanin_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)quadpanin_assist, "assist", A_CANT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    quadpanin_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *quadpanin_new(t_symbol *s, short argc, t_atom *argv)
{
    t_quadpanin *x = (t_quadpanin *)object_alloc(quadpanin_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 3);
    x->x_obj.z_misc |= Z_NO_INPLACE;

    outlet_new((t_pxobject *)x, "signal"); // Out 1: Left Front
    outlet_new((t_pxobject *)x, "signal"); // Out 2: Right Front
    outlet_new((t_pxobject *)x, "signal"); // Out 3: Right Rear
    outlet_new((t_pxobject *)x, "signal"); // Out 4: Left Rear

    return x;
}

void quadpanin_free(t_quadpanin *x)
{
    dsp_free((t_pxobject *)x);
}

void quadpanin_assist(t_quadpanin *x, void *b, long msg, long arg, char *dst)
{
    if (msg == 1) {
        switch (arg) {
            case 0: snprintf(dst, 256, "(signal) Audio Input"); break;
            case 1: snprintf(dst, 256, "(signal) X Coordinate (0-1)"); break;
            case 2: snprintf(dst, 256, "(signal) Y Coordinate (0-1)"); break;
        }
    } else if (msg == 2) {
        switch (arg) {
            case 0: snprintf(dst, 256, "(signal) Output 1 (Front Left)"); break;
            case 1: snprintf(dst, 256, "(signal) Output 2 (Front Right)"); break;
            case 2: snprintf(dst, 256, "(signal) Output 3 (Rear Right)"); break;
            case 3: snprintf(dst, 256, "(signal) Output 4 (Rear Left)"); break;
        }
    }
}

// -------------------------------------------------------------------------
// DSP Perform Routine
// -------------------------------------------------------------------------

void quadpanin_dsp64(t_quadpanin *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    object_method(dsp64, gensym("dsp_add64"), x, quadpanin_perform64, 0, NULL);
}

void quadpanin_perform64(t_quadpanin *x, t_object *dsp64, double **ins,
                          long numins, double **outs, long numouts, long n,
                          long flags, void *userparam)
{
    const double *in   = ins[0];
    const double *Xin  = ins[1];
    const double *Yin  = ins[2];

    double *out1 = outs[0];
    double *out2 = outs[1];
    double *out3 = outs[2];
    double *out4 = outs[3];

    while (n--) {
        double xval = *Xin++;
        double yval = *Yin++;

        // Symmetrical boundary clamping [0.0, 1.0]
        if (xval < 0.0) xval = 0.0;
        else if (xval > 1.0) xval = 1.0;

        if (yval < 0.0) yval = 0.0;
        else if (yval > 1.0) yval = 1.0;

        double xsquared  = xval * xval;
        double ysquared  = yval * yval;
        double ix        = 1.0 - xval;
        double iy        = 1.0 - yval;
        double ixsquared = ix * ix;
        double iysquared = iy * iy;

        // Distance from (0, 0) -> Left Rear
        double gain1 = sqrt(xsquared + ysquared);
        if (gain1 > 1.0) gain1 = 1.0;
        gain1 = 1.0 - gain1;

        // Distance from (1, 0) -> Right Rear
        double gain2 = sqrt(ixsquared + ysquared);
        if (gain2 > 1.0) gain2 = 1.0;
        gain2 = 1.0 - gain2;

        // Distance from (0, 1) -> Left Front
        double gain3 = sqrt(xsquared + iysquared);
        if (gain3 > 1.0) gain3 = 1.0;
        gain3 = 1.0 - gain3;

        // Distance from (1, 1) -> Right Front
        double gain4 = sqrt(ixsquared + iysquared);
        if (gain4 > 1.0) gain4 = 1.0;
        gain4 = 1.0 - gain4;

        double input = *in++;

        *out1++ = input * gain3;
        *out2++ = input * gain4;
        *out3++ = input * gain2;
        *out4++ = input * gain1;
    }
}