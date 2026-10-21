/**
 * @file stutter~.c
 * @brief Granular stutter and loop repeater buffer player for Max 8/9.
 * Modernized for 64-bit dsp64, channel-adaptive buffer access, and thread-safe PRNG.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define OBJECT_NAME "el.stutter~"
#define MAX_LOOPS (1024)

static t_class *stutter_class = NULL;

typedef struct _stutter
{
    t_pxobject    l_obj;
    t_symbol     *l_sym;
    t_buffer_ref *l_buf;
    long          l_chan;

    long          loop_samps;
    long          samps_to_go;
    long          loop_start;
    long          echos;
    long          min_echo;
    long          max_echo;
    long          index;

    long          taper_samps;
    double        taper_dur;
    long          loop_min_samps;
    long          loop_max_samps;
    double        loop_min_duration;
    double        loop_max_duration;
    double        taper_duration;
    double        R;
    double        ldev;
    double        st_dev;
    short         lock_loop;
    short         force_new_loop;
    double        buffer_duration;
    long          framesize;
    long          bufchans;
    short         mute_me;
    long         *stored_starts;
    long         *stored_samps;

    uint32_t      rand_state; // Instance-local PRNG
} t_stutter;

// Prototypes
void *stutter_new(t_symbol *msg, short argc, t_atom *argv);
void  stutter_free(t_stutter *x);
void  stutter_assist(t_stutter *x, void *b, long msg, long arg, char *dst);
void  stutter_dblclick(t_stutter *x);
void  stutter_float(t_stutter *x, double f);
void  stutter_int(t_stutter *x, long n);
void  stutter_bang(t_stutter *x);
void  stutter_set(t_stutter *x, t_symbol *s);
void  stutter_show_loop(t_stutter *x);
void  stutter_set_loop(t_stutter *x, t_symbol *msg, short argc, t_atom *argv);
void  stutter_store_loop(t_stutter *x, double loop_index);
void  stutter_recall_loop(t_stutter *x, double loop_index);
void  stutter_taper(t_stutter *x, double f);
void  stutter_min_echo(t_stutter *x, double f);
void  stutter_max_echo(t_stutter *x, double f);
void  stutter_init(t_stutter *x);
void stutter_lock_loop(t_stutter *x, long n);
void stutter_new_loop(t_stutter *x);
void stutter_minloop(t_stutter *x, double f);
void stutter_maxloop(t_stutter *x, double f);
void stutter_rand_dur(t_stutter *x, double f);
void stutter_rand_start(t_stutter *x, double f);

t_max_err stutter_notify(t_stutter *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

void  stutter_dsp64(t_stutter *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  stutter_perform64(t_stutter *x, t_object *dsp64, double **ins,
                        long numins, double **outs, long numouts, long n,
                        long flags, void *userparam);

// Fast, thread-safe instance-local PRNG
static inline double stutter_prng_normalized(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return (double)x * (1.0 / 4294967296.0);
}

static inline double stutter_boundrand(uint32_t *state, double min_val, double max_val)
{
    return min_val + (max_val - min_val) * stutter_prng_normalized(state);
}

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.stutter~", 
                           (method)stutter_new, 
                           (method)stutter_free, 
                           sizeof(t_stutter), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)stutter_dsp64,       "dsp64",       A_CANT,     0);
    class_addmethod(c, (method)stutter_assist,      "assist",      A_CANT,     0);
    class_addmethod(c, (method)stutter_dblclick,    "dblclick",    A_CANT,     0);
    class_addmethod(c, (method)stutter_notify,      "notify",      A_CANT,     0);
    class_addmethod(c, (method)stutter_float,       "float",       A_FLOAT,    0);
    class_addmethod(c, (method)stutter_int,         "int",         A_LONG,     0);
    class_addmethod(c, (method)stutter_bang,        "bang",        0);
    class_addmethod(c, (method)stutter_show_loop,   "show_loop",   0);
    class_addmethod(c, (method)stutter_set_loop,    "set_loop",    A_GIMME,    0);
    class_addmethod(c, (method)stutter_store_loop,  "store_loop",  A_DEFFLOAT, 0);
    class_addmethod(c, (method)stutter_recall_loop, "recall_loop", A_DEFFLOAT, 0);
    class_addmethod(c, (method)stutter_taper,       "taper",       A_DEFFLOAT, 0);
    class_addmethod(c, (method)stutter_min_echo,    "min_echo",    A_DEFFLOAT, 0);
    class_addmethod(c, (method)stutter_max_echo,    "max_echo",    A_DEFFLOAT, 0);
    class_addmethod(c, (method)stutter_lock_loop,    "lock_loop",    A_DEFLONG, 0);
    class_addmethod(c, (method)stutter_new_loop,   "new_loop",   0);
    class_addmethod(c, (method)stutter_minloop,    "minloop",    A_DEFFLOAT, 0);
    class_addmethod(c, (method)stutter_maxloop,    "maxloop",    A_DEFFLOAT, 0);
    class_addmethod(c, (method)stutter_rand_dur,    "rand_dur",    A_DEFFLOAT, 0);
    class_addmethod(c, (method)stutter_rand_start,    "rand_start",    A_DEFFLOAT, 0);
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    stutter_class = c;

    potpourri_announce(OBJECT_NAME);
}


void *stutter_new(t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 1 || !argv) {
        object_error(NULL, "%s: requires a buffer name as first argument", OBJECT_NAME);
        return NULL;
    }

    t_stutter *x = (t_stutter *)object_alloc(stutter_class);
    if (!x) return NULL;

    x->l_sym = atom_getsymarg(0, argc, argv);
    x->bufchans = (argc > 1) ? atom_getintarg(1, argc, argv) : 2;
    if (x->bufchans < 1) x->bufchans = 1;
    if (x->bufchans > 2) x->bufchans = 2;

    x->loop_min_duration = (argc > 2) ? (atom_getfloatarg(2, argc, argv) / 1000.0) : 0.02;
    x->loop_max_duration = (argc > 3) ? (atom_getfloatarg(3, argc, argv) / 1000.0) : 0.2;
    x->taper_duration    = (argc > 4) ? (atom_getfloatarg(4, argc, argv) / 1000.0) : 0.002;

    if (x->loop_min_duration <= 0.0) x->loop_min_duration = 0.02;
    if (x->loop_max_duration <= x->loop_min_duration) x->loop_max_duration = x->loop_min_duration + 0.1;
    if (x->taper_duration <= 0.0) x->taper_duration = 0.002;

    // Control inlets: 0: bang, 1: min_dur, 2: max_dur, 3: ldev, 4: st_dev, 5: lock
    dsp_setup((t_pxobject *)x, 0);
    /*
    intin((t_object *)x, 5);
    floatin((t_object *)x, 4);
    floatin((t_object *)x, 3);
    floatin((t_object *)x, 2);
    floatin((t_object *)x, 1);
*/
    outlet_new((t_pxobject *)x, "signal"); // Out 1 (Left / Mono)
    if (x->bufchans == 2) {
        outlet_new((t_pxobject *)x, "signal"); // Out 2 (Right)
    }
    x->l_obj.z_misc |= Z_NO_INPLACE;

    x->R = sys_getsr();
    if (x->R <= 0.0) x->R = 44100.0;

    x->rand_state = (uint32_t)((uintptr_t)x ^ 0x9E3779B9U);
    if (!x->rand_state) x->rand_state = 123456789U;

    x->stored_starts = (long *)sysmem_newptrclear(MAX_LOOPS * sizeof(long));
    x->stored_samps  = (long *)sysmem_newptrclear(MAX_LOOPS * sizeof(long));

    if (!x->stored_starts || !x->stored_samps) {
        object_free(x);
        return NULL;
    }

    x->l_buf = buffer_ref_new((t_object *)x, x->l_sym);
    stutter_init(x);

    return x;
}

void stutter_free(t_stutter *x)
{
    dsp_free((t_pxobject *)x);

    if (x->l_buf) {
        object_free(x->l_buf);
        x->l_buf = NULL;
    }
    if (x->stored_starts) {
        sysmem_freeptr(x->stored_starts);
        x->stored_starts = NULL;
    }
    if (x->stored_samps) {
        sysmem_freeptr(x->stored_samps);
        x->stored_samps = NULL;
    }
}

t_max_err stutter_notify(t_stutter *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    return buffer_ref_notify(x->l_buf, s, msg, sender, data);
}

// -------------------------------------------------------------------------
// Control Methods
// -------------------------------------------------------------------------

void stutter_init(t_stutter *x)
{
    x->loop_min_samps = (long)(x->loop_min_duration * x->R);
    x->loop_max_samps = (long)(x->loop_max_duration * x->R);
    if (x->loop_min_samps < 1) x->loop_min_samps = 1;
    if (x->loop_max_samps <= x->loop_min_samps) x->loop_max_samps = x->loop_min_samps + 1;

    x->samps_to_go = x->loop_samps = (long)stutter_boundrand(&x->rand_state, x->loop_min_samps, x->loop_max_samps);
    x->loop_start = 0;
    x->min_echo   = 2;
    x->max_echo   = 12;
    x->echos      = (long)stutter_boundrand(&x->rand_state, x->min_echo, x->max_echo);
    x->index      = x->loop_start;

    x->taper_samps = (long)(x->R * x->taper_duration);
    if (x->taper_samps < 1) x->taper_samps = 1;

    x->ldev            = 0.0;
    x->lock_loop       = 0;
    x->buffer_duration = 0.0;
    x->st_dev          = 0.0;
    x->framesize       = 0;
    x->force_new_loop  = 0;
    x->mute_me         = 0;
}

void stutter_set(t_stutter *x, t_symbol *s)
{
    if (!s || s == gensym("")) return;
    x->l_sym = s;
    if (x->l_buf) {
        buffer_ref_set(x->l_buf, s);
    }
}

void stutter_dblclick(t_stutter *x)
{
    stutter_set(x, x->l_sym);
    if (x->l_buf) {
        t_buffer_obj *b = buffer_ref_getobject(x->l_buf);
        if (b) buffer_view(b);
    }
}

void stutter_bang(t_stutter *x)
{
    x->echos = (long)stutter_boundrand(&x->rand_state, x->min_echo, x->max_echo);
    x->samps_to_go = x->loop_samps = (long)stutter_boundrand(&x->rand_state, x->loop_min_samps, x->loop_max_samps);
    long max_start = x->framesize - x->loop_samps;
    x->loop_start = (max_start > 0) ? (long)(stutter_prng_normalized(&x->rand_state) * (double)max_start) : 0;
    x->index = x->loop_start;
}

void stutter_float(t_stutter *x, double f)
{
    long inlet = proxy_getinlet((t_object *)x);

    if (inlet == 1) { // Min duration
        f *= 0.001;
        if (f < 0.0001) f = 0.0001;
        x->loop_min_samps = (long)(x->R * f);
        if (x->loop_min_samps >= x->loop_max_samps) {
            x->loop_min_samps = x->loop_max_samps - 1;
        }
    } else if (inlet == 2) { // Max duration
        f *= 0.001;
        if (x->buffer_duration > 0.0 && f >= x->buffer_duration * 0.49) {
            f = x->buffer_duration * 0.49;
        }
        x->loop_max_samps = (long)(x->R * f);
        if (x->loop_max_samps <= x->loop_min_samps) {
            x->loop_max_samps = x->loop_min_samps + 1;
        }
    } else if (inlet == 3) { // Duration deviation
        if (f < 0.0) f = 0.0;
        if (f > 1.0) f = 1.0;
        x->ldev = f;
    } else if (inlet == 4) { // Start deviation
        if (f < 0.0) f = 0.0;
        if (f > 1.0) f = 1.0;
        x->st_dev = f;
    }
}

/** Replace inlet functions */



void stutter_rand_start(t_stutter *x, double f)
{
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    x->st_dev = f;
}

void stutter_rand_dur(t_stutter *x, double f)
{
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    x->ldev = f;
}

// argument in milliseconds
void stutter_maxloop(t_stutter *x, double f)
{
    f *= 0.001;
    if (x->buffer_duration > 0.0 && f >= x->buffer_duration * 0.49) {
        f = x->buffer_duration * 0.49;
    }
    x->loop_max_samps = (long)(x->R * f);
    if (x->loop_max_samps <= x->loop_min_samps) {
        x->loop_max_samps = x->loop_min_samps + 1;
    }
}

// argument in milliseconds
void stutter_minloop(t_stutter *x, double f)
{
    f *= 0.001;
    if (f < 0.0001) f = 0.0001;
    x->loop_min_samps = (long)(x->R * f);
    if (x->loop_min_samps >= x->loop_max_samps) {
        x->loop_min_samps = x->loop_max_samps - 1;
    }
}

void stutter_lock_loop(t_stutter *x, long n)
{
    x->lock_loop = (short)(n > 0);
}

void stutter_new_loop(t_stutter *x)
{
    x->echos = (long)stutter_boundrand(&x->rand_state, x->min_echo, x->max_echo);
    x->samps_to_go = x->loop_samps = (long)stutter_boundrand(&x->rand_state, x->loop_min_samps, x->loop_max_samps);
    long max_start = x->framesize - x->loop_samps;
    x->loop_start = (max_start > 0) ? (long)(stutter_prng_normalized(&x->rand_state) * (double)max_start) : 0;
    x->index = x->loop_start;
}

void stutter_int(t_stutter *x, long n)
{
    long inlet = proxy_getinlet((t_object *)x);
    if (inlet == 5) {
        x->lock_loop = (short)(n > 0);
    }
}

void stutter_taper(t_stutter *x, double f)
{
    f *= 0.001;
    if (f > 0.0) {
        x->taper_duration = f;
        x->taper_samps = (long)(x->R * x->taper_duration);
        if (x->taper_samps < 1) x->taper_samps = 1;
    }
}

void stutter_min_echo(t_stutter *x, double f)
{
    long ec = (long)f;
    if (ec > 0 && ec < x->max_echo) {
        x->min_echo = ec;
    }
}

void stutter_max_echo(t_stutter *x, double f)
{
    long ec = (long)f;
    if (ec > x->min_echo) {
        x->max_echo = ec;
    }
}

void stutter_show_loop(t_stutter *x)
{
    post("loop start: %ld, samples: %ld", x->loop_start, x->loop_samps);
}

void stutter_store_loop(t_stutter *x, double loop_index)
{
    long i = (long)loop_index;
    if (i < 0 || i >= MAX_LOOPS) {
        object_error((t_object *)x, "store_loop index %ld out of bounds (0-%d)", i, MAX_LOOPS - 1);
        return;
    }
    x->stored_starts[i] = x->loop_start;
    x->stored_samps[i]  = x->loop_samps;
}

void stutter_recall_loop(t_stutter *x, double loop_index)
{
    long i = (long)loop_index;
    if (i < 0 || i >= MAX_LOOPS) {
        object_error((t_object *)x, "recall_loop index %ld out of bounds (0-%d)", i, MAX_LOOPS - 1);
        return;
    }
    if (x->stored_samps[i] <= 0) {
        object_error((t_object *)x, "no loop stored at slot %ld", i);
        return;
    }
    x->loop_start  = x->stored_starts[i];
    x->samps_to_go = x->loop_samps = x->stored_samps[i];
    if (x->loop_min_samps > x->loop_samps) x->loop_min_samps = x->loop_samps;
    if (x->loop_max_samps < x->loop_samps) x->loop_max_samps = x->loop_samps;
}

void stutter_set_loop(t_stutter *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 2 || !argv) {
        object_error((t_object *)x, "set_loop requires [start_sample] and [duration_samples]");
        return;
    }
    x->loop_start = (long)atom_getintarg(0, argc, argv);
    x->loop_samps = (long)atom_getintarg(1, argc, argv);
}

void stutter_assist(t_stutter *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: snprintf(dst, 256, "(bang) Force New Loop"); break;
            case 1: snprintf(dst, 256, "(float) Minimum Loop Size (ms)"); break;
            case 2: snprintf(dst, 256, "(float) Maximum Loop Size (ms)"); break;
            case 3: snprintf(dst, 256, "(float) Duration Randomizing Factor [0-1]"); break;
            case 4: snprintf(dst, 256, "(float) Start Time Randomizing Factor [0-1]"); break;
            case 5: snprintf(dst, 256, "(int) Loop Lock [0/1]"); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0: snprintf(dst, 256, "(signal) Output 1 (Left/Mono)"); break;
            case 1: snprintf(dst, 256, "(signal) Output 2 (Right)"); break;
        }
    }
}

// -------------------------------------------------------------------------
// DSP Perform Routine
// -------------------------------------------------------------------------

void stutter_dsp64(t_stutter *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;

    if (x->R != sr) {
        x->R = sr;
        x->taper_samps    = (long)(x->R * x->taper_duration);
        if (x->taper_samps < 1) x->taper_samps = 1;
        x->loop_min_samps = (long)(x->loop_min_duration * x->R);
        x->loop_max_samps = (long)(x->loop_max_duration * x->R);
    }

    object_method(dsp64, gensym("dsp_add64"), x, stutter_perform64, 0, NULL);
}

void stutter_perform64(t_stutter *x, t_object *dsp64, double **ins,
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam)
{
    double *out1 = outs[0];
    double *out2 = (numouts > 1) ? outs[1] : NULL;

    if (x->mute_me || !x->l_buf) {
        memset(out1, 0, (size_t)n * sizeof(double));
        if (out2) memset(out2, 0, (size_t)n * sizeof(double));
        return;
    }

    t_buffer_obj *b = buffer_ref_getobject(x->l_buf);
    if (!b) {
        memset(out1, 0, (size_t)n * sizeof(double));
        if (out2) memset(out2, 0, (size_t)n * sizeof(double));
        return;
    }

    float *tab = buffer_locksamples(b);
    if (!tab) {
        memset(out1, 0, (size_t)n * sizeof(double));
        if (out2) memset(out2, 0, (size_t)n * sizeof(double));
        return;
    }

    long frames = buffer_getframecount(b);
    long nc     = buffer_getchannelcount(b);

    if (frames <= 0 || nc <= 0) {
        buffer_unlocksamples(b);
        memset(out1, 0, (size_t)n * sizeof(double));
        if (out2) memset(out2, 0, (size_t)n * sizeof(double));
        return;
    }

    if (x->framesize != frames) {
        x->framesize = frames;
        x->buffer_duration = (double)frames / x->R;
    }

    long   index          = x->index;
    long   loop_start     = x->loop_start;
    long   loop_samps     = x->loop_samps;
    long   samps_to_go    = x->samps_to_go;
    long   echos          = x->echos;
    long   taper_samps    = (x->taper_samps < 1) ? 1 : x->taper_samps;
    long   loop_max_samps = x->loop_max_samps;
    long   loop_min_samps = x->loop_min_samps;
    double ldev           = x->ldev;
    short  lock_loop      = x->lock_loop;
    double st_dev         = x->st_dev;
    uint32_t rand_state   = x->rand_state;

    short is_stereo = (nc > 1 && numouts > 1);

    for (long s = 0; s < n; s++) {
        if (index < 0 || index >= frames) {
            index = 0;
        }

        double env;
        if (samps_to_go > loop_samps - taper_samps) {
            env = (double)(loop_samps - samps_to_go) / (double)taper_samps;
        } else if (samps_to_go < taper_samps) {
            env = (double)samps_to_go / (double)taper_samps;
        } else {
            env = 1.0;
        }

        if (is_stereo) {
            long idx2 = index * nc;
            out1[s] = (double)tab[idx2] * env;
            out2[s] = (double)tab[idx2 + 1] * env;
        } else {
            out1[s] = (double)tab[index * nc] * env;
            if (out2) out2[s] = out1[s];
        }
        index++;

        if (--samps_to_go <= 0) {
            index = loop_start;
            double sdev = ldev * (double)loop_samps;
            if (stutter_prng_normalized(&rand_state) < 0.5) sdev = -sdev;

            if (--echos <= 0 && !lock_loop) {
                echos = (long)stutter_boundrand(&rand_state, x->min_echo, x->max_echo);
                loop_samps = (long)stutter_boundrand(&rand_state, loop_min_samps, loop_max_samps);
                samps_to_go = loop_samps;
                long max_start = frames - loop_samps;
                loop_start = (max_start > 0) ? (long)(stutter_prng_normalized(&rand_state) * (double)max_start) : 0;
            } else {
                loop_samps += (long)sdev;
                if (loop_samps < loop_min_samps) loop_samps = loop_min_samps;
                if (loop_samps > loop_max_samps) loop_samps = loop_max_samps;
                samps_to_go = loop_samps;

                if (st_dev > 0.0) {
                    double startdev = stutter_prng_normalized(&rand_state) * st_dev * (double)loop_samps;
                    if (stutter_prng_normalized(&rand_state) < 0.5) startdev = -startdev;
                    loop_start += (long)startdev;
                    if (loop_start < 0) loop_start = 0;
                    if (loop_start + loop_samps > frames) loop_start = frames - loop_samps;
                }
                if (loop_start + loop_samps >= frames) {
                    loop_start = (frames > loop_samps) ? (frames - loop_samps - 1) : 0;
                }
            }
        }
    }

    buffer_unlocksamples(b);

    x->index       = index;
    x->loop_start  = loop_start;
    x->loop_samps  = loop_samps;
    x->samps_to_go = samps_to_go;
    x->echos       = echos;
    x->rand_state  = rand_state;
}
