/**
 * @file channel~.c
 * @brief Extracts a specific sample channel/index from an incoming audio vector.
 * Modernized for Max 8/9: 64-bit dsp64, vector bounds safety, and output zeroing.
 */

#include "MSPd.h"

#define OBJECT_NAME "channel~"

static t_class *channel_class;

typedef struct _channel {
    t_pxobject x_obj;
    void *float_outlet;
    long channel;
} t_channel;

/* Method Prototypes */
void *channel_new(t_symbol *s, long argc, t_atom *argv);
void channel_assist(t_channel *x, void *b, long m, long a, char *s);
void channel_channel(t_channel *x, double chan);
void channel_int(t_channel *x, long chan);
void channel_perform64(t_channel *x, t_object *dsp64, double **ins, 
                       long numins, double **outs, long numouts, long vectorsize,
                       long flags, void *userparam);
void channel_dsp64(t_channel *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.channel~", 
                           (method)channel_new, 
                           (method)dsp_free, 
                           (long)sizeof(t_channel), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)channel_dsp64,   "dsp64",   A_CANT, 0);
    class_addmethod(c, (method)channel_assist,  "assist",  A_CANT, 0);
    class_addmethod(c, (method)channel_channel, "channel", A_FLOAT, 0);
    class_addmethod(c, (method)channel_int,     "int",     A_LONG,  0);
    class_addmethod(c, (method)channel_int,     "in1",     A_LONG,  0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    channel_class = c;

    potpourri_announce(OBJECT_NAME);   
    return 0;
}

void channel_channel(t_channel *x, double chan) 
{
    if (chan >= 0.0) {
        x->channel = (long)chan;
    }
}

void channel_int(t_channel *x, long chan) 
{
    if (chan >= 0) {
        x->channel = chan;
    }
}

void channel_assist(t_channel *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Input Vector", 256); break;
            case 1: strncpy_zero(dst, "(int) Target Sample Index within Vector", 256); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Extracted Channel Value", 256);
    }
}

void *channel_new(t_symbol *s, long argc, t_atom *argv)
{
    t_channel *x = (t_channel *)object_alloc(channel_class);
    if (!x) return NULL;

    intin(x, 1);                  // Int inlet on the right for channel index
    dsp_setup((t_pxobject *)x, 1); // 1 Signal inlet
    outlet_new((t_object *)x, "signal"); // 1 Signal output

    x->channel = (argc > 0) ? (long)atom_getfloat(argv) : 0;
    if (x->channel < 0) x->channel = 0;

    return x;
}

void channel_perform64(t_channel *x, t_object *dsp64, double **ins, 
                       long numins, double **outs, long numouts, long vectorsize,
                       long flags, void *userparam)
{
    t_double *in_vec  = ins[0];
    t_double *out_vec = outs[0];
    long n = vectorsize;
    long channel = x->channel;

    // Safety check: if channel index is out of vector bounds, zero output and return
    if (channel < 0 || channel >= n || !in_vec || !out_vec) {
        for (long i = 0; i < n; i++) {
            out_vec[i] = 0.0;
        }
        return;
    }

    t_double value = in_vec[channel];
    
    while (n--) {
        *out_vec++ = value;
    }
}

void channel_dsp64(t_channel *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)channel_perform64, 0, NULL);
}