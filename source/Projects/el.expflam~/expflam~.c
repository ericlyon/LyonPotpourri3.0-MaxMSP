/**
 * @file expflam~.c
 * @brief Exponential flam generator for Max 8/9.
 * Modernized for 64-bit dsp64, memory safety, and thread-safe heap management.
 */

#include "MSPd.h"
#include <math.h>

#define MAXFLAMS (16)
#define MAXATTACKS (128)
#define STOPGAIN (0.001f)
#define OBJECT_NAME "expflam~"

static t_class *expflam_class;

typedef struct {
    int attack_count;
    float *attack_times;
    int *attack_points;
    int fdex;
    float gainatten;
    float amp;
    int atks;
    long expflam;
    short active;
} t_flam;

typedef struct _expflam {
    t_pxobject x_obj;
    t_flam *flams;
    float start_delay;
    float end_delay;
    float atten;
    float slope;
    int atks;
    float sr;
    t_double *trigvec64; 
    t_double *bypvec64; 
    short flamgate;
    short flamgate_connected;
} t_expflam;

/* Prototypes */
void *expflam_new(t_symbol *s, long argc, t_atom *argv);
void expflam_free(t_expflam *x);
void expflam_assist(t_expflam *x, void *b, long msg, long arg, char *dst);
void expflam_flamgate(t_expflam *x, double tog);
void expflam_setflam(t_expflam *x, t_symbol *msg, long argc, t_atom *argv);
void expflam_dsp64(t_expflam *x, t_object *dsp64, short *count, double sr, long n, long flags);
void expflam_perform64(t_expflam *x, t_object *dsp64, double **ins, 
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.expflam~", (method)expflam_new, (method)expflam_free, sizeof(t_expflam), 0, A_GIMME, 0);
    
    class_addmethod(c, (method)expflam_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)expflam_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)expflam_setflam, "setflam", A_GIMME, 0);
    class_addmethod(c, (method)expflam_flamgate, "flamgate", A_FLOAT, 0);
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    expflam_class = c;
    potpourri_announce(OBJECT_NAME);
    return 0;
}

void expflam_flamgate(t_expflam *x, double tog) { x->flamgate = (short)tog; }

void expflam_assist(t_expflam *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        if (arg == 0) strncpy_zero(dst, "(signal) Trigger Click", 256);
        else strncpy_zero(dst, "(signal) Flam Gate", 256);
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Flam Clicks", 256);
    }
}

void *expflam_new(t_symbol *s, long argc, t_atom *argv)
{
    t_expflam *x = (t_expflam *)object_alloc(expflam_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 2);
    outlet_new((t_object *)x, "signal");

    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->flams = (t_flam *)sysmem_newptrclear(MAXFLAMS * sizeof(t_flam));
    for (int i = 0; i < MAXFLAMS; i++) {
        x->flams[i].attack_times = (float *)sysmem_newptrclear(MAXATTACKS * sizeof(float));
        x->flams[i].attack_points = (int *)sysmem_newptrclear(MAXATTACKS * sizeof(int));
    }

    x->trigvec64 = (t_double *)sysmem_newptrclear(8192 * sizeof(t_double));
    x->bypvec64  = (t_double *)sysmem_newptrclear(8192 * sizeof(t_double));

    x->sr = (float)sys_getsr();
    x->start_delay = 0.025f;
    x->end_delay = 0.1f;
    x->slope = -3.0f;
    x->atks = 8;
    x->atten = 0.8f;

    return x;
}

void expflam_setflam(t_expflam *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 5) return;
    x->start_delay = (float)atom_getfloat(argv) * 0.001f;
    x->end_delay = (float)atom_getfloat(argv + 1) * 0.001f;
    x->atks = (int)atom_getfloat(argv + 2);
    x->slope = (float)atom_getfloat(argv + 3);
    x->atten = (float)atom_getfloat(argv + 4);

    if (x->slope == 0.0f) x->slope = 0.0001f;
    if (x->start_delay <= 0.0f) x->start_delay = 0.00001f;
    if (x->end_delay <= 0.0f) x->end_delay = 0.00001f;
    if (x->atks < 2) x->atks = 2;
    if (x->atks > MAXATTACKS) x->atks = MAXATTACKS;
}

void expflam_free(t_expflam *x)
{
    dsp_free((t_pxobject *)x);
    if (x->trigvec64) sysmem_freeptr(x->trigvec64);
    if (x->bypvec64)  sysmem_freeptr(x->bypvec64);
    if (x->flams) {
        for (int i = 0; i < MAXFLAMS; i++) {
            sysmem_freeptr(x->flams[i].attack_times);
            sysmem_freeptr(x->flams[i].attack_points);
        }
        sysmem_freeptr(x->flams);
    }
}

void expflam_perform64(t_expflam *x, t_object *dsp64, double **ins, 
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam)
{
    t_double *in_vec = ins[0];
    t_double *in2_vec = ins[1];
    t_double *out_vec = outs[0];

    t_double *trigvec = x->trigvec64;
    t_double *flamgate_vec = x->bypvec64;
    t_flam *flams = x->flams;
    int atks = x->atks;
    float atten = x->atten;
    float slope = x->slope;
    float start_delay = x->start_delay;
    float end_delay = x->end_delay;
    float sr = x->sr;
    short flamgate_connected = x->flamgate_connected;

    if (x->flamgate) {
        memcpy(out_vec, in_vec, n * sizeof(t_double));
        return;
    }

    memcpy(flamgate_vec, in2_vec, n * sizeof(t_double));
    memcpy(trigvec, in_vec, n * sizeof(t_double));
    memset(out_vec, 0, n * sizeof(t_double));

    for (int i = 0; i < n; i++) {
        if (trigvec[i] != 0.0 && (flamgate_vec[i] != 0.0 || !flamgate_connected)) {
            int j = 0;
            while (flams[j].active && j < MAXFLAMS) j++;
            if (j < MAXFLAMS) {
                flams[j].active = 1;
                flams[j].attack_times[0] = 0.0f;
                flams[j].attack_points[0] = i;
                flams[j].gainatten = atten;
                flams[j].amp = (float)trigvec[i];
                flams[j].expflam = 0;
                flams[j].fdex = 0;
                flams[j].atks = atks;

                for (int k = 1; k < atks; k++) {
                    float numer = 1.0f - expf((float)k * slope / (float)(atks - 1));
                    float denom = 1.0f - expf(slope);
                    flams[j].attack_times[k] = start_delay + (end_delay - start_delay) * (numer / denom);
                    flams[j].attack_times[k] += flams[j].attack_times[k - 1];
                    flams[j].attack_points[k] = (int)(flams[j].attack_times[k] * sr) + i;
                }
            }
        }
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < MAXFLAMS; j++) {
            if (flams[j].active) {
                if (flams[j].expflam >= flams[j].attack_points[flams[j].fdex]) {
                    out_vec[i] += flams[j].amp;
                    flams[j].amp *= flams[j].gainatten;
                    if (flams[j].amp <= STOPGAIN) flams[j].active = 0;
                    flams[j].fdex++;
                    if (flams[j].fdex >= flams[j].atks) flams[j].active = 0;
                }
                flams[j].expflam++;
            }
        }
    }
}

void expflam_dsp64(t_expflam *x, t_object *dsp64, short *count, double samplerate, long n, long flags)
{
    if (samplerate <= 0.0) return;
    x->flamgate_connected = count[1];
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)expflam_perform64, 0, NULL);
}