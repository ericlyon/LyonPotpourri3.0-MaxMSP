/**
 * @file player~.c
 * @brief Click-driven multi-overlap buffer player for Max 8/9.
 * Modernized for 64-bit dsp64, real-time thread safety, and zero-leak heap teardown.
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "ext_buffer.h"
#include <string.h>

#define DEFAULT_MAX_OVERLAP (8)
#define ACTIVE 0
#define INACTIVE 1
#define MAX_OUTLETS (4)
#define OBJECT_NAME "el.player~"

typedef struct {
    double phase;        // playback phase in frames
    double gain;         // gain factor
    short  status;       // ACTIVE or INACTIVE
    double increment;    // voice playback increment
} t_event;

typedef struct _player {
    t_pxobject    x_obj;
    t_buffer_ref *wavebuf;
    t_symbol     *wavename;
    
    double        sr;
    short         mute;
    short         connections[2];
    long          outlet_count;
    int           overlap_max;
    t_event      *events;
    
    char          static_increment;
    double        user_increment;
} t_player;

static t_class *player_class = NULL;

// Prototypes
void *player_new(t_symbol *s, short argc, t_atom *argv);
void  player_free(t_player *x);
void  player_dsp64(t_player *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  player_perform64(t_player *x, t_object *dsp64, double **ins, long numins, 
                       double **outs, long numouts, long n, long flags, void *userparam);
void  player_setbuf(t_player *x, t_symbol *s);
void  player_stop(t_player *x);
void  player_mute(t_player *x, double f);
void  player_float(t_player *x, double f);
void  player_assist(t_player *x, void *b, long msg, long arg, char *dst);
t_max_err player_notify(t_player *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

// Helper inline to prevent implicit declaration and truncation warnings
static inline t_atom_long clamp_long(t_atom_long val, t_atom_long min_val, t_atom_long max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r) {
    t_class *c = class_new("el.player~", 
                           (method)player_new, 
                           (method)player_free, 
                           sizeof(t_player), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)player_dsp64,   "dsp64",    A_CANT, 0);
    class_addmethod(c, (method)player_setbuf,  "setbuf",   A_SYM,  0);
    class_addmethod(c, (method)player_stop,    "stop",     0);
    class_addmethod(c, (method)player_mute,    "mute",     A_FLOAT, 0);
    class_addmethod(c, (method)player_float,   "float",    A_FLOAT, 0);
    class_addmethod(c, (method)player_assist,  "assist",   A_CANT, 0);
    class_addmethod(c, (method)player_notify,  "notify",   A_CANT, 0);

    CLASS_ATTR_CHAR(c, "static_increment", 0, t_player, static_increment);
    CLASS_ATTR_DEFAULT_SAVE(c, "static_increment", 0, "0");
    CLASS_ATTR_ENUMINDEX(c, "static_increment", 0, "Off On");
    CLASS_ATTR_LABEL(c, "static_increment", 0, "Static Increment");

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    player_class = c;
}

void *player_new(t_symbol *s, short argc, t_atom *argv) {
    if (argc < 1 || !argv) {
        object_error(NULL, "[%s] buffer name required as first argument", OBJECT_NAME);
        return NULL;
    }

    t_player *x = (t_player *)object_alloc(player_class);
    if (!x) return NULL;

    // Default configuration
    x->outlet_count = 2;
    x->overlap_max = DEFAULT_MAX_OVERLAP;
    x->static_increment = 0;
    x->user_increment = 1.0;
    x->mute = 0;
    x->wavebuf = NULL;

    // Parse positional arguments with full 64-bit width
    long argoffset = attr_args_offset(argc, argv);
    atom_arg_getsym(&x->wavename, 0, argc, argv);

    if (argoffset >= 2) {
        t_atom_long outlets = 2;
        atom_arg_getlong(&outlets, 1, argc, argv);
        x->outlet_count = (long)clamp_long(outlets, 1, MAX_OUTLETS);
    }

    if (argoffset >= 3) {
        t_atom_long overlaps = DEFAULT_MAX_OVERLAP;
        atom_arg_getlong(&overlaps, 2, argc, argv);
        x->overlap_max = (int)clamp_long(overlaps, 1, 128);
    }

    attr_args_process(x, argc, argv);

    // Setup MSP audio I/O
    dsp_setup((t_pxobject *)x, 2);
    x->x_obj.z_misc |= Z_NO_INPLACE;

    for (long i = 0; i < x->outlet_count; i++) {
        outlet_new((t_pxobject *)x, "signal");
    }

    // Allocate internal event slots
    x->events = (t_event *)sysmem_newptr((size_t)x->overlap_max * sizeof(t_event));
    if (!x->events) {
        object_free(x);
        return NULL;
    }
    player_stop(x);

    // Register buffer reference safely on main thread
    x->wavebuf = buffer_ref_new((t_object *)x, x->wavename);

    return x;
}

void player_free(t_player *x) {
    dsp_free((t_pxobject *)x);
    
    if (x->wavebuf) {
        object_free(x->wavebuf);
        x->wavebuf = NULL;
    }
    if (x->events) {
        sysmem_freeptr(x->events);
        x->events = NULL;
    }
}

// -------------------------------------------------------------------------
// Control Methods
// -------------------------------------------------------------------------

t_max_err player_notify(t_player *x, t_symbol *s, t_symbol *msg, void *sender, void *data) {
    return buffer_ref_notify(x->wavebuf, s, msg, sender, data);
}

void player_setbuf(t_player *x, t_symbol *s) {
    if (!s || s == gensym("")) return;
    x->wavename = s;
    if (x->wavebuf) {
        buffer_ref_set(x->wavebuf, s);
    }
    player_stop(x);
}

void player_stop(t_player *x) {
    if (!x->events) return;
    for (int i = 0; i < x->overlap_max; i++) {
        x->events[i].status = INACTIVE;
        x->events[i].phase = 0.0;
        x->events[i].gain = 0.0;
        x->events[i].increment = 1.0;
    }
}

void player_mute(t_player *x, double f) {
    x->mute = (short)(f != 0.0);
}

void player_float(t_player *x, double f) {
    long inlet = ((t_pxobject *)x)->z_in;
    if (inlet == 1) {
        if (f == 0.0) {
            object_error((t_object *)x, "%s: zero increment rejected", OBJECT_NAME);
            return;
        }
        x->user_increment = f;
    }
}

void player_assist(t_player *x, void *b, long msg, long arg, char *dst) {
    if (msg == 1) {
        switch (arg) {
            case 0: snprintf(dst, 256, "(signal) Click Trigger / Gain"); break;
            case 1: snprintf(dst, 256, "(signal/float) Increment / Pitch"); break;
        }
    } else if (msg == 2) {
        snprintf(dst, 256, "(signal) Channel %ld Output", arg + 1);
    }
}

// -------------------------------------------------------------------------
// DSP & Real-Time Performance Methods
// -------------------------------------------------------------------------

void player_dsp64(t_player *x, t_object *dsp64, short *count, double sr, long n, long flags) {
    x->sr = sr;
    x->connections[0] = count[0];
    x->connections[1] = count[1];
    object_method(dsp64, gensym("dsp_add64"), x, player_perform64, 0, NULL);
}

void player_perform64(t_player *x, t_object *dsp64, double **ins, long numins, 
                      double **outs, long numouts, long n, long flags, void *userparam) {
    // 1. Zero all outputs first using standard memset
    for (long chan = 0; chan < numouts; chan++) {
        memset(outs[chan], 0, (size_t)n * sizeof(double));
    }

    if (x->mute || !x->wavebuf) return;

    // 2. Fetch and validate buffer object safely without audio-thread allocations
    t_buffer_obj *b = buffer_ref_getobject(x->wavebuf);
    if (!b) return;

    long b_frames = buffer_getframecount(b);
    long b_nchans = buffer_getchannelcount(b);

    if (b_frames <= 0 || b_nchans <= 0) return;

    float *b_samples = buffer_locksamples(b);
    if (!b_samples) return;

    const double *trigger_vec   = ins[0];
    const double *increment_vec = ins[1];
    short  inc_connected        = x->connections[1];
    long   active_outlets       = (numouts < b_nchans) ? numouts : b_nchans;
    int    overlap_max          = x->overlap_max;
    t_event *events             = x->events;

    // 3. Process currently active voices
    for (int i = 0; i < overlap_max; i++) {
        if (events[i].status != ACTIVE) continue;

        double gain = events[i].gain;

        for (long j = 0; j < n; j++) {
            double cur_inc = x->static_increment ? events[i].increment 
                           : (inc_connected ? increment_vec[j] : x->user_increment);

            double fphase = events[i].phase;
            long iphase = (long)fphase;
            double frac = fphase - (double)iphase;

            if (iphase >= 0 && iphase < b_frames) {
                const float *frame_ptr = &b_samples[iphase * b_nchans];

                if (cur_inc >= 0.0) {
                    if (iphase == b_frames - 1) {
                        for (long chan = 0; chan < active_outlets; chan++) {
                            outs[chan][j] += (double)frame_ptr[chan] * gain;
                        }
                    } else {
                        const float *next_frame = frame_ptr + b_nchans;
                        for (long chan = 0; chan < active_outlets; chan++) {
                            double s1 = (double)frame_ptr[chan];
                            double s2 = (double)next_frame[chan];
                            outs[chan][j] += gain * (s1 + frac * (s2 - s1));
                        }
                    }
                } else {
                    if (iphase == 0) {
                        for (long chan = 0; chan < active_outlets; chan++) {
                            outs[chan][j] += (double)frame_ptr[chan] * gain;
                        }
                    } else {
                        const float *prev_frame = frame_ptr - b_nchans;
                        for (long chan = 0; chan < active_outlets; chan++) {
                            double s1 = (double)prev_frame[chan];
                            double s2 = (double)frame_ptr[chan];
                            outs[chan][j] += gain * (s1 + frac * (s2 - s1));
                        }
                    }
                }
            }

            events[i].phase += cur_inc;

            if (events[i].phase < 0.0 || events[i].phase >= (double)b_frames || cur_inc == 0.0) {
                events[i].status = INACTIVE;
                break;
            }
        }
    }

    // 4. Trigger responder & voice allocation
    for (long j = 0; j < n; j++) {
        if (trigger_vec[j] == 0.0) continue;

        double gain = trigger_vec[j];
        double initial_inc = inc_connected ? increment_vec[j] : x->user_increment;
        if (initial_inc == 0.0) initial_inc = 1.0;

        int slot = -1;

        // Find inactive voice slot
        for (int v = 0; v < overlap_max; v++) {
            if (events[v].status == INACTIVE) {
                slot = v;
                break;
            }
        }

        // Steal voice if all slots full (highest playback phase stolen)
        if (slot == -1) {
            double maxphase = -1.0;
            slot = 0;
            for (int v = 0; v < overlap_max; v++) {
                if (events[v].phase > maxphase) {
                    maxphase = events[v].phase;
                    slot = v;
                }
            }
        }

        // Initialize stolen/allocated slot
        events[slot].status    = ACTIVE;
        events[slot].gain      = gain;
        events[slot].increment = initial_inc;
        events[slot].phase     = (initial_inc >= 0.0) ? 0.0 : (double)(b_frames - 1);

        // Render remainder of current signal vector for this new voice
        for (long k = j; k < n; k++) {
            double cur_inc = x->static_increment ? events[slot].increment 
                           : (inc_connected ? increment_vec[k] : x->user_increment);

            double fphase = events[slot].phase;
            long iphase = (long)fphase;
            double frac = fphase - (double)iphase;

            if (iphase >= 0 && iphase < b_frames) {
                const float *frame_ptr = &b_samples[iphase * b_nchans];

                if (cur_inc >= 0.0) {
                    if (iphase == b_frames - 1) {
                        for (long chan = 0; chan < active_outlets; chan++) {
                            outs[chan][k] += (double)frame_ptr[chan] * gain;
                        }
                    } else {
                        const float *next_frame = frame_ptr + b_nchans;
                        for (long chan = 0; chan < active_outlets; chan++) {
                            double s1 = (double)frame_ptr[chan];
                            double s2 = (double)next_frame[chan];
                            outs[chan][k] += gain * (s1 + frac * (s2 - s1));
                        }
                    }
                } else {
                    if (iphase == 0) {
                        for (long chan = 0; chan < active_outlets; chan++) {
                            outs[chan][k] += (double)frame_ptr[chan] * gain;
                        }
                    } else {
                        const float *prev_frame = frame_ptr - b_nchans;
                        for (long chan = 0; chan < active_outlets; chan++) {
                            double s1 = (double)prev_frame[chan];
                            double s2 = (double)frame_ptr[chan];
                            outs[chan][k] += gain * (s1 + frac * (s2 - s1));
                        }
                    }
                }
            }

            events[slot].phase += cur_inc;

            if (events[slot].phase < 0.0 || events[slot].phase >= (double)b_frames || cur_inc == 0.0) {
                events[slot].status = INACTIVE;
                break;
            }
        }
    }

    buffer_unlocksamples(b);
}