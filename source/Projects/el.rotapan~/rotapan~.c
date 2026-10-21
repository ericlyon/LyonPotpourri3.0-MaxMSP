/**
 * @file rotapan~.c
 * @brief Multichannel circular rotation panning external for Max 8/9.
 * Modernized for 64-bit dsp64, continuous modular phase wrapping, and zero-leak teardown.
 */

#include "MSPd.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#define OBJECT_NAME "el.rotapan~"
#define MIN_CHANNELS (2)
#define MAX_CHANNELS (1024)

static t_class *rotapan_class = NULL;

typedef struct _rotapan
{
    t_pxobject x_obj;
    double     sr;
    long       rchans;
    double     pio2;
} t_rotapan;

// Prototypes
void *rotapan_new(t_symbol *s, short argc, t_atom *argv);
void  rotapan_free(t_rotapan *x);
void  rotapan_assist(t_rotapan *x, void *b, long msg, long arg, char *dst);
void  rotapan_dsp64(t_rotapan *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  rotapan_perform64(t_rotapan *x, t_object *dsp64, double **ins, 
                        long numins, double **outs, long numouts, long n,
                        long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.rotapan~", 
                           (method)rotapan_new, 
                           (method)rotapan_free, 
                           sizeof(t_rotapan), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)rotapan_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)rotapan_assist, "assist", A_CANT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    rotapan_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *rotapan_new(t_symbol *s, short argc, t_atom *argv)
{
    t_rotapan *x = (t_rotapan *)object_alloc(rotapan_class);
    if (!x) return NULL;

    t_atom_long chans = 2;
    if (argc > 0 && argv) {
        atom_arg_getlong(&chans, 0, argc, argv);
    }

    if (chans < MIN_CHANNELS) {
        object_warn((t_object *)x, "%s: channel count clamped to minimum of %d", OBJECT_NAME, MIN_CHANNELS);
        chans = MIN_CHANNELS;
    } else if (chans > MAX_CHANNELS) {
        object_warn((t_object *)x, "%s: channel count clamped to maximum of %d", OBJECT_NAME, MAX_CHANNELS);
        chans = MAX_CHANNELS;
    }
    x->rchans = (long)chans;

    // Signal inlets: [chans audio inputs] + [1 pan controller inlet]
    dsp_setup((t_pxobject *)x, x->rchans + 1);
    x->x_obj.z_misc |= Z_NO_INPLACE;

    for (long i = 0; i < x->rchans; i++) {
        outlet_new((t_pxobject *)x, "signal");
    }

    x->sr = sys_getsr();
    x->pio2 = PI / 2.0;

    return x;
}

void rotapan_free(t_rotapan *x)
{
    dsp_free((t_pxobject *)x);
}

void rotapan_assist(t_rotapan *x, void *b, long msg, long arg, char *dst)
{
    if (msg == 1) { // Inlets
        if (arg < x->rchans) {
            snprintf(dst, 256, "(signal) Audio Input %ld", arg + 1);
        } else if (arg == x->rchans) {
            snprintf(dst, 256, "(signal) Pan Controller [0.0 - 1.0]");
        }
    } else if (msg == 2) { // Outlets
        if (arg < x->rchans) {
            snprintf(dst, 256, "(signal) Audio Output %ld", arg + 1);
        }
    }
}

// -------------------------------------------------------------------------
// Real-Time DSP Perform Routine
// -------------------------------------------------------------------------

void rotapan_dsp64(t_rotapan *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    x->sr = sr;
    object_method(dsp64, gensym("dsp_add64"), x, rotapan_perform64, 0, NULL);
}

void rotapan_perform64(t_rotapan *x, t_object *dsp64, double **ins, 
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam)
{
    long rchans = x->rchans;
    if (rchans < MIN_CHANNELS) return;

    // Vectorized zeroing of all output buffers
    for (long chan = 0; chan < rchans; chan++) {
        memset(outs[chan], 0, (size_t)n * sizeof(double));
    }

    const double pio2 = x->pio2;
    const double *pan_vec = ins[rchans];

    for (long j = 0; j < n; j++) {
        // Continuous circular modular wrapping [0.0, 1.0)
        double pan = fmod(pan_vec[j], 1.0);
        if (pan < 0.0) pan += 1.0;

        double scaledIndex = pan * (double)rchans;
        long offset = (long)scaledIndex;
        if (offset >= rchans) offset = rchans - 1;

        double frac = scaledIndex - (double)offset;
        double panloc = frac * pio2;

        double amp1 = cos(panloc);
        double amp2 = sin(panloc);

        long out_idx1 = offset;
        long out_idx2 = (offset + 1) % rchans;

        for (long chan = 0; chan < rchans; chan++) {
            double in_val = ins[chan][j];
            outs[(chan + out_idx1) % rchans][j] += amp1 * in_val;
            outs[(chan + out_idx2) % rchans][j] += amp2 * in_val;
        }
    }
}