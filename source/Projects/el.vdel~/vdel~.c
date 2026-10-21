/**
 * @file vdel~.c
 * @brief Flanger, chorus, and variable delay processor for Max 8/9.
 * Modernized for 64-bit dsp64, message binding fixes, and zero-leak memory management.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define F_LEN (16384)
#define F_LEN_MASK (16383)
#define MAX_DELAY_TIME (60.0) // 60 seconds maximum delay buffer
#define OBJECT_NAME "el.vdel~"

static t_class *vdel_class = NULL;

typedef struct _vdel
{
    t_pxobject l_obj;

    double     R;
    double    *sinetab;
    double     si_factor;
    double     osc1_phs;
    double     osc1_si;

    double     speed;
    double     feedback;
    double     depth;
    double     maxdel;
    double    *ddl1;
    long       ddl1_len;
    long       ddl1_phs;

    double     tap1;

    short      feedback_connected;
    short      speed_connected;
    short      depth_connected;
    short      feedback_protect;
    short      delay_connected;
    short      init;
} t_vdel;

// Prototypes
void *vdel_new(t_symbol *s, short argc, t_atom *argv);
void  vdel_free(t_vdel *x);
void  vdel_assist(t_vdel *x, void *b, long m, long a, char *dst);
void  vdel_float(t_vdel *x, double f);
void  vdel_protect(t_vdel *x, double state);
void  vdel_init(t_vdel *x, short initialized);

void  vdel_dsp64(t_vdel *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  vdel_perform64(t_vdel *x, t_object *dsp64, double **ins,
                     long numins, double **outs, long numouts, long n,
                     long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.vdel~", 
                           (method)vdel_new, 
                           (method)vdel_free, 
                           sizeof(t_vdel), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)vdel_dsp64,   "dsp64",   A_CANT,  0);
    class_addmethod(c, (method)vdel_assist,  "assist",  A_CANT,  0);
    class_addmethod(c, (method)vdel_float,   "float",   A_FLOAT, 0);
    class_addmethod(c, (method)vdel_protect, "protect", A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    vdel_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *vdel_new(t_symbol *s, short argc, t_atom *argv)
{
    t_vdel *x = (t_vdel *)object_alloc(vdel_class);
    if (!x) return NULL;

    x->R = 0.0;
    x->init = 0;

    // 5 Signal Inlets: [0: In, 1: Speed, 2: Depth, 3: Feedback, 4: Delay Override]
    dsp_setup((t_pxobject *)x, 5);
    x->l_obj.z_misc |= Z_NO_INPLACE;

    outlet_new((t_pxobject *)x, "signal");

    // Standardized defaults (50 ms max delay)
    x->maxdel   = 0.05; // 50 ms in seconds
    x->speed    = 0.1;
    x->depth    = 0.25;
    x->feedback = 0.0;

    if (argc > 0) x->maxdel   = atom_getfloatarg(0, argc, argv) / 1000.0;
    if (argc > 1) x->speed    = atom_getfloatarg(1, argc, argv);
    if (argc > 2) x->depth    = atom_getfloatarg(2, argc, argv);
    if (argc > 3) x->feedback = atom_getfloatarg(3, argc, argv);

    if (x->maxdel < 0.0001) x->maxdel = 0.0001;
    if (x->maxdel > MAX_DELAY_TIME) x->maxdel = MAX_DELAY_TIME;

    x->ddl1 = NULL;
    x->sinetab = NULL;

    return x;
}

void vdel_init(t_vdel *x, short initialized)
{
    x->si_factor = (double)F_LEN / x->R;
    x->ddl1_phs  = 0;
    x->osc1_si   = x->si_factor * x->speed;
    x->osc1_phs  = 0.0;
    x->ddl1_len  = (long)(x->maxdel * x->R);
    if (x->ddl1_len < 4) x->ddl1_len = 4;
    x->tap1 = 0.0;

    if (!initialized) {
        x->feedback_protect = 1;
        if (x->depth < 0.0) x->depth = 0.0;
        if (x->depth > 1.0) x->depth = 1.0;
        if (x->feedback < -0.999) x->feedback = -0.999;
        if (x->feedback > 0.999)  x->feedback = 0.999;

        x->ddl1    = (double *)sysmem_newptrclear((size_t)(x->ddl1_len + 32) * sizeof(double));
        x->sinetab = (double *)sysmem_newptrclear(F_LEN * sizeof(double));

        for (int i = 0; i < F_LEN; i++) {
            x->sinetab[i] = 0.5 - 0.49 * cos(TWOPI * (double)i / (double)F_LEN);
        }
    } else {
        x->ddl1 = (double *)sysmem_resizeptrclear(x->ddl1, (size_t)(x->ddl1_len + 32) * sizeof(double));
    }
}

void vdel_free(t_vdel *x)
{
    dsp_free((t_pxobject *)x);
    if (x->sinetab) {
        sysmem_freeptr(x->sinetab);
        x->sinetab = NULL;
    }
    if (x->ddl1) {
        sysmem_freeptr(x->ddl1);
        x->ddl1 = NULL;
    }
}

// -------------------------------------------------------------------------
// Control Methods
// -------------------------------------------------------------------------

void vdel_protect(t_vdel *x, double state)
{
    x->feedback_protect = (short)(state != 0.0);
}

void vdel_float(t_vdel *x, double f)
{
    long inlet = x->l_obj.z_in;

    if (inlet == 1) { // Speed
        x->speed = f;
        x->osc1_si = f * x->si_factor;
    } else if (inlet == 2) { // Depth
        if (f < 0.0) f = 0.0;
        if (f > 1.0) f = 1.0;
        x->depth = f;
    } else if (inlet == 3) { // Feedback
        x->feedback = f;
        if (x->feedback_protect) {
            if (x->feedback > 0.99)  x->feedback = 0.99;
            if (x->feedback < -0.99) x->feedback = -0.99;
        }
    }
}

void vdel_assist(t_vdel *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: snprintf(dst, 256, "(signal) Audio Input"); break;
            case 1: snprintf(dst, 256, "(signal/float) LFO Modulation Speed (Hz)"); break;
            case 2: snprintf(dst, 256, "(signal/float) Modulation Depth [0.0 - 1.0]"); break;
            case 3: snprintf(dst, 256, "(signal/float) Feedback [-1.0 to 1.0]"); break;
            case 4: snprintf(dst, 256, "(signal) Direct Delay Override [0.0 - 1.0]"); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        snprintf(dst, 256, "(signal) Variable Delayed Output");
    }
}

// -------------------------------------------------------------------------
// Real-Time DSP Perform Routine
// -------------------------------------------------------------------------

void vdel_dsp64(t_vdel *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;

    x->speed_connected    = count[1];
    x->depth_connected    = count[2];
    x->feedback_connected = count[3];
    x->delay_connected    = count[4];

    if (x->R != sr) {
        x->R = sr;
        vdel_init(x, x->init);
        x->init = 1;
    }

    object_method(dsp64, gensym("dsp_add64"), x, vdel_perform64, 0, NULL);
}

void vdel_perform64(t_vdel *x, t_object *dsp64, double **ins,
                    long numins, double **outs, long numouts, long n,
                    long flags, void *userparam)
{
    const double *in1          = ins[0];
    const double *speed_vec    = ins[1];
    const double *depth_vec    = ins[2];
    const double *feedback_vec = ins[3];
    const double *delay_vec    = ins[4];
    double       *out1         = outs[0];

    double       *ddl1               = x->ddl1;
    const double *sinetab            = x->sinetab;
    long          ddl1_phs           = x->ddl1_phs;
    long          ddl1_len           = x->ddl1_len;
    double        osc1_phs           = x->osc1_phs;
    double        osc1_si            = x->osc1_si;
    double        tap1               = x->tap1;
    double        feedback           = x->feedback;
    double        depth              = x->depth;
    double        si_factor          = x->si_factor;
    short         feedback_connected = x->feedback_connected;
    short         speed_connected    = x->speed_connected;
    short         depth_connected    = x->depth_connected;
    short         delay_connected    = x->delay_connected;
    short         feedback_protect   = x->feedback_protect;

    if (!ddl1 || !sinetab || ddl1_len <= 1) {
        memset(out1, 0, (size_t)n * sizeof(double));
        return;
    }

    for (long s = 0; s < n; s++) {
        double insamp1 = in1[s];

        if (feedback_connected) {
            feedback = feedback_vec[s];
            if (feedback_protect) {
                if (feedback > 0.99)  feedback = 0.99;
                if (feedback < -0.99) feedback = -0.99;
            }
        }

        double fdelay;
        if (delay_connected) {
            fdelay = delay_vec[s] * (double)ddl1_len;
            if (fdelay < 1.0) fdelay = 1.0;
            if (fdelay > (double)(ddl1_len - 1)) fdelay = (double)(ddl1_len - 1);
        } else {
            if (speed_connected) {
                osc1_si = speed_vec[s] * si_factor;
            }
            if (depth_connected) {
                depth = depth_vec[s];
                if (depth < 0.0001) depth = 0.0001;
                if (depth > 1.0)    depth = 1.0;
            }

            long phs_idx = (long)osc1_phs & F_LEN_MASK;
            fdelay = sinetab[phs_idx] * (double)ddl1_len * depth;

            osc1_phs += osc1_si;
            if (osc1_phs >= (double)F_LEN) {
                osc1_phs = fmod(osc1_phs, (double)F_LEN);
            } else if (osc1_phs < 0.0) {
                osc1_phs = (double)F_LEN + fmod(osc1_phs, (double)F_LEN);
            }
        }

        double feedsamp = tap1 * feedback;
        if (fabs(feedsamp) < 1.0e-15) feedsamp = 0.0; // Denormal protection

        ddl1[ddl1_phs] = insamp1 + feedsamp;
        ddl1_phs = (ddl1_phs + 1) % ddl1_len;

        // Linear interpolated delay lookup
        long idelay1 = (long)fdelay;
        double frac  = fdelay - (double)idelay1;

        long index1 = (ddl1_phs + idelay1) % ddl1_len;
        if (index1 < 0) index1 += ddl1_len;
        long index2 = (index1 + 1) % ddl1_len;

        tap1 = (1.0 - frac) * ddl1[index1] + frac * ddl1[index2];
        out1[s] = tap1;
    }

    x->ddl1_phs = ddl1_phs;
    x->osc1_phs = osc1_phs;
    x->depth    = depth;
    x->feedback = feedback;
    x->osc1_si  = osc1_si;
    x->tap1     = tap1;
}