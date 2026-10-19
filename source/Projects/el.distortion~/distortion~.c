/**
 * @file distortion~.c
 * @brief Waveshaping distortion unit with variable knee and cut parameters for Max 8/9.
 * Modernized for 64-bit dsp64, mute functionality, and input signal safety.
 */

#include "MSPd.h"
#include <math.h>

#define OBJECT_NAME "distortion~"

static t_class *distortion_class;

typedef struct _distortion {
    t_pxobject x_obj;
    double knee;
    double cut;
    double rescale;
    short mute;
    short case1;
    short *connections;
} t_distortion;

/* Method Prototypes */
void *distortion_new(double knee, double cut);
void distortion_assist(t_distortion *x, void *b, long m, long a, char *s);
void distortion_float(t_distortion *x, double f);
void distortion_mute(t_distortion *x, double f);
void distortion_dsp64(t_distortion *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void distortion1_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs, long numouts, long n,
                           long flags, void *userparam);
void distortion2_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs, long numouts, long n,
                           long flags, void *userparam);
void distortion3_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs, long numouts, long n,
                           long flags, void *userparam);
void distortion4_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs, long numouts, long n,
                           long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.distortion~", 
                           (method)distortion_new, 
                           (method)dsp_free, 
                           (long)sizeof(t_distortion), 
                           0L,
                           A_FLOAT, A_FLOAT, 0);

    class_addmethod(c, (method)distortion_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)distortion_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)distortion_mute,   "mute",   A_FLOAT, 0);
    class_addmethod(c, (method)distortion_float,  "float",  A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    distortion_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void distortion_assist(t_distortion *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Audio Input", 256); break;
            case 1: strncpy_zero(dst, "(signal/float) Knee Threshold", 256); break;
            case 2: strncpy_zero(dst, "(signal/float) Cut / Ceiling", 256); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Distorted Output", 256);
    }
}

void *distortion_new(double knee, double cut)
{
    t_distortion *x = (t_distortion *)object_alloc(distortion_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 3);       // 3 Signal inlets: Audio, Knee, Cut
    outlet_new((t_object *)x, "signal"); // 1 Signal output

    x->x_obj.z_misc |= Z_NO_INPLACE;

    if (knee >= cut || knee <= 0.0 || cut <= 0.0) {
        x->knee = 0.1;
        x->cut = 0.3;
    } else {
        x->knee = knee;
        x->cut = cut;
    }

    if (x->cut < 0.001) x->cut = 0.001;
    x->rescale = 1.0 / x->cut;
    x->mute = 0;

    return x;
}

void distortion_mute(t_distortion *x, double f) 
{
    x->mute = (short)f;
}

void distortion_float(t_distortion *x, double f)
{
    int inlet = ((t_pxobject *)x)->z_in;
    switch (inlet) {
        case 1:
            x->knee = f;
            if (x->knee < 0.000001) x->knee = 0.000001;
            break;
        case 2:
            x->cut = f;
            if (x->cut < 0.001) x->cut = 0.001;
            else if (x->cut > 1.0) x->cut = 1.0;
            x->rescale = 1.0 / x->cut;
            break;
    }
}

/* Case 1: Only Audio Connected (Uses object attributes for knee and cut) */
void distortion1_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs, long numouts, long n,
                           long flags, void *userparam)
{
    t_double *in  = ins[0];
    t_double *out = outs[0];

    if (x->mute || !in || !out) {
        for (long i = 0; i < n; i++) out[i] = 0.0;
        return;
    }

    double knee = x->knee;
    double cut = x->cut;
    double rescale = (cut > 0.001) ? (1.0 / cut) : 1.0;

    while (n--) {
        double in_sample = *in++;
        double rectified_sample = fabs(in_sample);

        if (rectified_sample < knee) {
            *out++ = in_sample;
        } else {
            double shaped = rescale * (knee + (rectified_sample - knee) * (cut - knee));
            *out++ = (in_sample > 0.0) ? shaped : -shaped;
        }
    }
}

/* Case 2: Audio, Knee, and Cut All Connected via Signals */
void distortion2_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs, long numouts, long n,
                           long flags, void *userparam)
{
    t_double *in    = ins[0];
    t_double *data1 = ins[1];
    t_double *data2 = ins[2];
    t_double *out   = outs[0];

    if (x->mute || !in || !data1 || !data2 || !out) {
        for (long i = 0; i < n; i++) out[i] = 0.0;
        return;
    }

    while (n--) {
        double in_sample = *in++;
        double knee = *data1++;
        double cut = *data2++;
        if (cut < 0.001) cut = 0.001;
        double rescale = 1.0 / cut;

        double rectified_sample = fabs(in_sample);

        if (rectified_sample < knee) {
            *out++ = in_sample;
        } else {
            double shaped = rescale * (knee + (rectified_sample - knee) * (cut - knee));
            *out++ = (in_sample > 0.0) ? shaped : -shaped;
        }
    }
}

/* Case 3: Audio and Knee Connected, Cut Disconnected */
void distortion3_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs, long numouts, long n,
                           long flags, void *userparam)
{
    t_double *in    = ins[0];
    t_double *data1 = ins[1];
    t_double *out   = outs[0];

    if (x->mute || !in || !data1 || !out) {
        for (long i = 0; i < n; i++) out[i] = 0.0;
        return;
    }

    double cut = x->cut;
    double rescale = (cut > 0.001) ? (1.0 / cut) : 1.0;

    while (n--) {
        double in_sample = *in++;
        double knee = *data1++;
        double rectified_sample = fabs(in_sample);

        if (rectified_sample < knee) {
            *out++ = in_sample;
        } else {
            double shaped = rescale * (knee + (rectified_sample - knee) * (cut - knee));
            *out++ = (in_sample > 0.0) ? shaped : -shaped;
        }
    }
}

/* Case 4: Audio and Cut Connected, Knee Disconnected */
void distortion4_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs, long numouts, long n,
                           long flags, void *userparam)
{
    t_double *in    = ins[0];
    t_double *data2 = ins[2];
    t_double *out   = outs[0];

    if (x->mute || !in || !data2 || !out) {
        for (long i = 0; i < n; i++) out[i] = 0.0;
        return;
    }

    double knee = x->knee;

    while (n--) {
        double in_sample = *in++;
        double cut = *data2++;
        if (cut < 0.001) cut = 0.001;
        double rescale = 1.0 / cut;

        double rectified_sample = fabs(in_sample);

        if (rectified_sample < knee) {
            *out++ = in_sample;
        } else {
            double shaped = rescale * (knee + (rectified_sample - knee) * (cut - knee));
            *out++ = (in_sample > 0.0) ? shaped : -shaped;
        }
    }
}

void distortion_dsp64(t_distortion *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;

    x->connections = count;

    if (count[0] && !count[1] && !count[2]) {
        dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)distortion1_perform64, 0, NULL);
    } else if (count[0] && count[1] && count[2]) {
        dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)distortion2_perform64, 0, NULL);
    } else if (count[0] && count[1] && !count[2]) {
        dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)distortion3_perform64, 0, NULL);
    } else if (count[0] && !count[1] && count[2]) {
        dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)distortion4_perform64, 0, NULL);
    } else {
        // Fallback: Audio input connected alone or default
        dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)distortion1_perform64, 0, NULL);
    }
}