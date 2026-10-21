/**
 * @file killdc~.c
 * @brief DC blocking filter based on Paul Lansky's cascaded elliptic filter design.
 * Modernized for Max 8/9: 64-bit dsp64, denormal-safe filter states, and active mute handling.
 */

#include "MSPd.h"
#include <math.h>

#define OBJECT_NAME "killdc~"
#define MAXSECTS (4)

static t_class *killdc_class;

typedef struct {
    double ps[4][MAXSECTS];
    double c[4][MAXSECTS];
    int nsects;
    double xnorm;
} COEFS64;

typedef struct _killdc {
    t_pxobject x_obj;
    COEFS64 fdata64;
    short mute;
} t_killdc;

/* Method Prototypes */
void *killdc_new(void);
void killdc_mute(t_killdc *x, double state);
void killdc_clear(t_killdc *x);
void killdc_assist(t_killdc *x, void *b, long m, long a, char *s);
void killdc_dsp64(t_killdc *x, t_object *dsp64, short *count, double samplerate, long n, long flags);
void killdc_perform64(t_killdc *x, t_object *dsp64, double **ins, 
                      long numins, double **outs, long numouts, long n,
                      long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.killdc~", 
                           (method)killdc_new, 
                           (method)dsp_free, 
                           (long)sizeof(t_killdc), 
                           0L, 
                           0);
    
    class_addmethod(c, (method)killdc_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)killdc_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)killdc_mute,   "mute",   A_FLOAT, 0);
    class_addmethod(c, (method)killdc_clear,  "clear",  0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    killdc_class = c;
    
    potpourri_announce(OBJECT_NAME);
    return 0;
}

void killdc_mute(t_killdc *x, double state)
{
    x->mute = (short)(state != 0.0);
}

void killdc_clear(t_killdc *x)
{
    for (int i = 0; i < x->fdata64.nsects; i++) {
        for (int j = 0; j < 4; j++) {
            x->fdata64.ps[j][i] = 0.0;
        }
    }
}

void killdc_assist(t_killdc *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(signal) Audio Input", 256);
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) DC-Filtered Output", 256);
    }
}

void *killdc_new(void)
{
    t_killdc *x = (t_killdc *)object_alloc(killdc_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);
    outlet_new((t_object *)x, "signal");
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->fdata64.nsects = 3;
    x->fdata64.c[0][0] = -1.9999996;
    x->fdata64.c[1][0] = -1.9997695;
    x->fdata64.c[2][0] =  1.0000000;
    x->fdata64.c[3][0] =  0.99977354;

    x->fdata64.c[0][1] = -1.9999998;
    x->fdata64.c[1][1] = -1.9985010;
    x->fdata64.c[2][1] =  1.0000000;
    x->fdata64.c[3][1] =  0.99851080;

    x->fdata64.c[0][2] = -1.0000000;
    x->fdata64.c[1][2] = -0.98978878;
    x->fdata64.c[2][2] =  0.0;
    x->fdata64.c[3][2] =  0.0;

    for (int i = 0; i < x->fdata64.nsects; i++) {
        for (int j = 0; j < 4; j++) {
            x->fdata64.ps[j][i] = 0.0;
        }
    }

    x->fdata64.xnorm = 1.3669191;
    x->mute = 0;

    return x;
}

void killdc_perform64(t_killdc *x, t_object *dsp64, double **ins, 
                      long numins, double **outs, long numouts, long n,
                      long flags, void *userparam)
{
    t_double *in1 = ins[0];
    t_double *out = outs[0];

    if (x->mute || !in1 || !out) {
        while (n--) *out++ = 0.0;
        return;
    }

    int nsects = x->fdata64.nsects;
    double xnorm = x->fdata64.xnorm;

    while (n--) {
        double sample = *in1++;

        for (int m = 0; m < nsects; m++) {
            double op = sample + x->fdata64.c[0][m] * x->fdata64.ps[0][m]
                               + x->fdata64.c[2][m] * x->fdata64.ps[1][m]
                               - x->fdata64.c[1][m] * x->fdata64.ps[2][m]
                               - x->fdata64.c[3][m] * x->fdata64.ps[3][m];

            // Anti-denormal protection: clamp subnormal filter states to zero
            if (fabs(op) < 1.0e-15) {
                op = 0.0;
                x->fdata64.ps[0][m] = 0.0;
                x->fdata64.ps[1][m] = 0.0;
                x->fdata64.ps[2][m] = 0.0;
                x->fdata64.ps[3][m] = 0.0;
            } else {
                x->fdata64.ps[1][m] = x->fdata64.ps[0][m];
                x->fdata64.ps[0][m] = sample;
                x->fdata64.ps[3][m] = x->fdata64.ps[2][m];
                x->fdata64.ps[2][m] = op;
            }

            sample = op;
        }

        if (fabs(sample) < 1.0e-15) {
            sample = 0.0;
        }

        *out++ = sample * xnorm;
    }
}

void killdc_dsp64(t_killdc *x, t_object *dsp64, short *count, double samplerate, long n, long flags)
{
    if (!samplerate || !count[0]) return;
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)killdc_perform64, 0, NULL);
}