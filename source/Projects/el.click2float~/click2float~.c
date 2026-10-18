/**
 * @file click2float~.c
 * @brief Converts signal clicks into Max float messages safely using qelem.
 * LyonPotpourri 3.0 - Max 8/9 Modernized
 */

#include "MSPd.h"

#define OBJECT_NAME "click2float~"

static t_class *click2float_class;

typedef struct _click2float
{
    t_pxobject x_obj;
    void *float_outlet;
    void *qelem;
    double float_value;	
} t_click2float;

/* Prototypes */
void *click2float_new(t_symbol *s, long argc, t_atom *argv);
void click2float_free(t_click2float *x);
void click2float_assist(t_click2float *x, void *b, long m, long a, char *s);
void click2float_qelem(t_click2float *x);
void click2float_perform64(t_click2float *x, t_object *dsp64, double **ins, 
                           long numins, double **outs, long numouts, long n,
                           long flags, void *userparam);
void click2float_dsp64(t_click2float *x, t_object *dsp64, short *count, 
                       double samplerate, long maxvectorsize, long flags);


int C74_EXPORT main(void)
{
    t_class *c = class_new("el.click2float~", 
                           (method)click2float_new, 
                           (method)click2float_free, 
                           (long)sizeof(t_click2float), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)click2float_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)click2float_dsp64,  "dsp64",  A_CANT, 0);
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    click2float_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

/* Executes safely on the Main (UI) thread */
void click2float_qelem(t_click2float *x) 
{
    outlet_float(x->float_outlet, x->float_value);
}

void click2float_assist(t_click2float *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(signal) Click Trigger", 256);
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(float) Click Value", 256);
    }
}

void *click2float_new(t_symbol *s, long argc, t_atom *argv)
{
    t_click2float *x = (t_click2float *)object_alloc(click2float_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1); // 1 Signal inlet, 0 signal outlets
    x->float_outlet = floatout((t_object *)x);
    x->qelem = qelem_new((t_object *)x, (method)click2float_qelem);
    x->float_value = 0.0;

    return x;
}

void click2float_free(t_click2float *x)
{
    // 1. Unlink from DSP chain first to stop perform64 execution
    dsp_free((t_pxobject *)x);

    // 2. Safely cancel and deallocate qelem
    if (x->qelem) {
        qelem_free(x->qelem);
        x->qelem = NULL;
    }
}

void click2float_perform64(t_click2float *x, t_object *dsp64, double **ins, 
                           long numins, double **outs, long numouts, long n,
                           long flags, void *userparam)
{
    t_double *in_vec = ins[0];

    while (n--) {
        double val = *in_vec++;
        if (val != 0.0) {
            x->float_value = val;
            qelem_set(x->qelem); // Audio-thread safe; coalesces automatically
        }
    }
}

void click2float_dsp64(t_click2float *x, t_object *dsp64, short *count, 
                       double samplerate, long maxvectorsize, long flags)
{
    // Only compile perform routine into the signal chain if connected
    if (!samplerate || !count[0]) {
        return;
    }
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)click2float_perform64, 0, NULL);
}
