/**
 * @file latch~.c
 * @brief Impulse-triggered sample and hold latch with duration timeout for Max 8/9.
 * Modernized for 64-bit dsp64, accurate inlet message tracking, and direct engine API linkage.
 */

#include "MSPd.h"

#define OBJECT_NAME "latch~"
#define LATCH_WAITING 0
#define LATCH_RUNNING 1

static t_class *latch_class;

typedef struct _latch {
    t_pxobject x_obj;
    double current;
    long latch;
    long dsamps;
    long sr;
    double duration;
    short status;
    short connected;
} t_latch;

/* Method Prototypes */
void *latch_new(t_symbol *s, long argc, t_atom *argv);
void latch_assist(t_latch *x, void *b, long m, long a, char *dst);
void latch_float(t_latch *x, double f);
void latch_int(t_latch *x, long n);
void latch_dsp64(t_latch *x, t_object *dsp64, short *count, double sr, long n, long flags);
void latch_perform64(t_latch *x, t_object *dsp64, double **ins, 
                     long numins, double **outs, long numouts, long n,
                     long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.latch~", 
                           (method)latch_new, 
                           (method)dsp_free, 
                           (long)sizeof(t_latch), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)latch_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)latch_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)latch_float,  "float",  A_FLOAT, 0);
    class_addmethod(c, (method)latch_int,    "int",    A_LONG,  0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    latch_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void latch_assist(t_latch *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Non-Zero Trigger Input", 256); break;
            case 1: strncpy_zero(dst, "(signal/float) Latch Duration (ms)", 256); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Latched Output Signal", 256);
    }
}

void *latch_new(t_symbol *s, long argc, t_atom *argv)
{
    t_latch *x = (t_latch *)object_alloc(latch_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 2); // Inlet 0: Trigger, Inlet 1: Duration
    outlet_new((t_object *)x, "signal");

    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->sr = sys_getsr();
    if (x->sr <= 0) x->sr = 44100;

    double dur_ms = (argc > 0) ? atom_getfloat(argv) : 1000.0;
    if (dur_ms <= 1.0) dur_ms = 1.0;

    x->duration = dur_ms * 0.001;
    x->dsamps = (long)(x->sr * x->duration);
    if (x->dsamps < 1) x->dsamps = 1;

    x->status = LATCH_WAITING;
    x->latch = 0;
    x->current = 0.0;
    x->connected = 0;

    return x;
}

void latch_float(t_latch *x, double f)
{
    int inletnum = ((t_pxobject *)x)->z_in;
    if (inletnum == 1 && f > 0.0) {
        x->duration = f * 0.001;
        x->dsamps = (long)(x->sr * x->duration);
        if (x->dsamps < 1) x->dsamps = 1;
    }
}

void latch_int(t_latch *x, long n)
{
    latch_float(x, (double)n);
}

void latch_perform64(t_latch *x, t_object *dsp64, double **ins, 
                     long numins, double **outs, long numouts, long n,
                     long flags, void *userparam)
{
    t_double *trigger      = ins[0];
    t_double *vec_duration = ins[1];
    t_double *output       = outs[0];

    long latch      = x->latch;
    long dsamps     = x->dsamps;
    short status    = x->status;
    double current  = x->current;
    double duration = x->duration;
    long sr         = x->sr;
    short connected = x->connected;

    for (long i = 0; i < n; i++) {
        if (connected) {
            double sig_dur = vec_duration[i];
            if (sig_dur > 0.0) {
                duration = sig_dur * 0.001;
                dsamps = (long)(sr * duration);
                if (dsamps < 1) dsamps = 1;
            }
        }

        if (trigger[i] != 0.0) {
            latch = 0;
            status = LATCH_RUNNING;
            current = trigger[i];
        } else {
            latch++;
            if (latch >= dsamps) {
                status = LATCH_WAITING;
                current = 0.0;
            }
        }
        output[i] = current;
    }

    x->current = current;
    x->status = status;
    x->latch = latch;
    x->dsamps = dsamps;
    x->duration = duration;
}

void latch_dsp64(t_latch *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;

    if (x->sr != (long)sr) {
        x->sr = (long)sr;
        x->dsamps = (long)(x->duration * x->sr);
        if (x->dsamps < 1) x->dsamps = 1;
    }

    x->connected = count[1];
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)latch_perform64, 0, NULL);
}