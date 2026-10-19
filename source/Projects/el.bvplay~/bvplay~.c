/**
 * @file bvplay~.c
 * @brief Pre-scheduled buffer player with linear interpolation and envelope tapering.
 * Modernized for Max 8/9: 64-bit dsp64, bounds safety, and buffer lock symmetry.
 */

#include "MSPd.h"

#define OBJECT_NAME "bvplay~"

static t_class *bvplay_class;

typedef struct _bvplay {
    t_pxobject l_obj;
    t_symbol *l_sym;
    t_buffer_ref *bufref;
    long l_chan;
    double R;
    long framesize;
    float notedata[4]; // Embedded array: [start_time, duration, increment, amplitude]
    int active;
    double buffer_duration;
    long taper_frames;
    double amp;
    long start_frame;
    long note_frames;
    long end_frame;
    double increment;
    double findex;
    long index;
    short verbose;
    short mute;
    double a_taper_dur; // Attribute for taper duration in ms
} t_bvplay;

/* Method Prototypes */
void *bvplay_new(t_symbol *s, long argc, t_atom *argv);
void bvplay_free(t_bvplay *x);
void bvplay_set(t_bvplay *x, t_symbol *s);
void bvplay_notelist(t_bvplay *x, t_symbol *msg, long argc, t_atom *argv);
void bvplay_assist(t_bvplay *x, void *b, long m, long a, char *s);
void bvplay_dblclick(t_bvplay *x);
void bvplay_perform_mono64(t_bvplay *x, t_object *dsp64, double **ins, 
                           long numins, double **outs, long numouts, long vectorsize,
                           long flags, void *userparam);
void bvplay_perform_stereo64(t_bvplay *x, t_object *dsp64, double **ins, 
                             long numins, double **outs, long numouts, long vectorsize,
                             long flags, void *userparam);
void bvplay_dsp64(t_bvplay *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
t_max_err bvplay_notify(t_bvplay *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.bvplay~", 
                           (method)bvplay_new, 
                           (method)bvplay_free, 
                           (long)sizeof(t_bvplay), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)bvplay_dsp64,    "dsp64",    A_CANT, 0);
    class_addmethod(c, (method)bvplay_notify,   "notify",   A_CANT, 0);
    class_addmethod(c, (method)bvplay_notelist, "list",     A_GIMME, 0);
    class_addmethod(c, (method)bvplay_assist,   "assist",   A_CANT, 0);
    class_addmethod(c, (method)bvplay_dblclick, "dblclick", A_CANT, 0);

    CLASS_ATTR_DOUBLE(c, "taper", 0, t_bvplay, a_taper_dur);
    CLASS_ATTR_LABEL(c, "taper", 0, "Taper Duration (ms)");
    CLASS_ATTR_DEFAULT_SAVE(c, "taper", 0, "10.0");

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    bvplay_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

t_max_err bvplay_notify(t_bvplay *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (x->bufref) {
        return buffer_ref_notify(x->bufref, s, msg, sender, data);
    }
    return MAX_ERR_NONE;
}

void bvplay_set(t_bvplay *x, t_symbol *s)
{
    x->l_sym = s;
    if (!x->bufref) {
        x->bufref = buffer_ref_new((t_object *)x, s);
    } else {
        buffer_ref_set(x->bufref, s);
    }
}

void bvplay_dblclick(t_bvplay *x)
{
    bvplay_set(x, x->l_sym);
    if (x->bufref) {
        buffer_view(buffer_ref_getobject(x->bufref));
    }
}

void bvplay_assist(t_bvplay *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(list) Note Data [start_ms, dur_ms, speed, amp]", 256);
    } else if (msg == ASSIST_OUTLET) {
        if (x->l_chan == 2) {
            switch (arg) {
                case 0: strncpy_zero(dst, "(signal) Output 1 (L)", 256); break;
                case 1: strncpy_zero(dst, "(signal) Output 2 (R)", 256); break;
            }
        } else {
            strncpy_zero(dst, "(signal) Output", 256);
        }
    }
}

void bvplay_notelist(t_bvplay *x, t_symbol *msg, long argc, t_atom *argv) 
{
    if (!sys_getdspstate()) {
        if (x->verbose) object_error((t_object *)x, "%s: DSP is off!", OBJECT_NAME);
        return;
    }
    if (x->active) {
        if (x->verbose) object_post((t_object *)x, "%s: already playing, cannot trigger note", OBJECT_NAME);
        return;
    }
    if (argc < 4) {
        if (x->verbose) {
            object_error((t_object *)x, "%s: requires 4 parameters: start_ms dur_ms increment amp", OBJECT_NAME);
        }
        return;
    }

    bvplay_set(x, x->l_sym);
    t_buffer_obj *b = buffer_ref_getobject(x->bufref);
    if (!b) {
        if (x->verbose) object_error((t_object *)x, "%s: buffer %s is invalid", OBJECT_NAME, x->l_sym->s_name);
        return;
    }

    x->framesize = buffer_getframecount(b);
    if (x->R <= 0.0) x->R = 44100.0;
    x->buffer_duration = (double)x->framesize / x->R;

    if (x->framesize <= 0 || x->buffer_duration <= 0.0) {
        if (x->verbose) object_post((t_object *)x, "%s: buffer contains no samples", OBJECT_NAME);
        return;
    }

    x->notedata[0] = (float)(atom_getfloat(argv) * 0.001);
    x->notedata[1] = (float)(atom_getfloat(argv + 1) * 0.001);
    x->notedata[2] = (float)atom_getfloat(argv + 2);
    x->notedata[3] = (float)atom_getfloat(argv + 3);

    x->increment = x->notedata[2];
    if (x->increment == 0.0) {
        if (x->verbose) object_error((t_object *)x, "%s: zero increment is illegal", OBJECT_NAME);
        return;
    }

    x->start_frame = (long)(x->notedata[0] * x->R);
    x->findex = (double)x->start_frame;
    x->index = x->start_frame;

    x->note_frames = (long)(x->notedata[1] * x->increment * x->R);
    x->end_frame = x->start_frame + x->note_frames;
    x->amp = x->notedata[3];

    if (x->start_frame < 0 || x->start_frame >= x->framesize) {
        if (x->verbose) object_error((t_object *)x, "%s: start frame out of range (%ld)", OBJECT_NAME, x->start_frame);
        return;
    }
    if (x->end_frame < 0 || x->end_frame >= x->framesize) {
        if (x->verbose) object_error((t_object *)x, "%s: end frame out of range (%ld)", OBJECT_NAME, x->end_frame);
        return;
    }

    x->active = 1;
}

void bvplay_perform_mono64(t_bvplay *x, t_object *dsp64, double **ins, 
                           long numins, double **outs, long numouts, long vectorsize,
                           long flags, void *userparam)
{
    t_double *out = outs[0];
    long n = vectorsize;

    if (!x->active || x->mute) {
        while (n--) *out++ = 0.0;
        return;
    }

    t_buffer_obj *b = buffer_ref_getobject(x->bufref);
    if (!b) {
        x->active = 0;
        while (n--) *out++ = 0.0;
        return;
    }

    float *tab = buffer_locksamples(b);
    if (!tab) {
        x->active = 0;
        while (n--) *out++ = 0.0;
        return;
    }

    long b_frames = buffer_getframecount(b);
    long index = x->index;
    double findex = x->findex;
    long end_frame = x->end_frame;
    double increment = x->increment;
    long start_frame = x->start_frame;
    long taper_frames = (long)(x->a_taper_dur * x->R * 0.001);
    if (taper_frames < 1) taper_frames = 1;

    double noteamp = x->amp;

    while (n--) {
        if ((increment > 0.0 && index < end_frame) || (increment < 0.0 && index > end_frame)) {
            double amp;

            // Envelope calculation
            if (increment > 0.0) {
                if (findex < (double)(start_frame + taper_frames)) {
                    amp = noteamp * ((findex - (double)start_frame) / (double)taper_frames);
                } else if (findex > (double)(end_frame - taper_frames)) {
                    amp = noteamp * (((double)end_frame - findex) / (double)taper_frames);
                } else {
                    amp = noteamp;
                }
            } else {
                if (findex > (double)(start_frame - taper_frames)) {
                    amp = noteamp * (((double)start_frame - findex) / (double)taper_frames);
                } else if (findex < (double)(end_frame + taper_frames)) {
                    amp = noteamp * ((findex - (double)end_frame) / (double)taper_frames);
                } else {
                    amp = noteamp;
                }
            }

            if (index >= 0 && index < b_frames) {
                double frac = findex - (double)index;
                long next_idx = (index < b_frames - 1) ? (index + 1) : index;
                *out++ = amp * ((double)tab[index] + frac * ((double)tab[next_idx] - (double)tab[index]));
            } else {
                *out++ = 0.0;
            }

            findex += increment;
            index = (long)findex;
        } else {
            *out++ = 0.0;
            x->active = 0;
        }
    }

    x->index = index;
    x->findex = findex;
    buffer_unlocksamples(b);
}

void bvplay_perform_stereo64(t_bvplay *x, t_object *dsp64, double **ins,
                             long numins, double **outs, long numouts, long vectorsize,
                             long flags, void *userparam)
{
    t_double *out1 = outs[0];
    t_double *out2 = outs[1];
    long n = vectorsize;

    if (!x->active || x->mute) {
        while (n--) {
            *out1++ = 0.0;
            *out2++ = 0.0;
        }
        return;
    }

    t_buffer_obj *b = buffer_ref_getobject(x->bufref);
    if (!b) {
        x->active = 0;
        while (n--) {
            *out1++ = 0.0;
            *out2++ = 0.0;
        }
        return;
    }

    float *tab = buffer_locksamples(b);
    if (!tab) {
        x->active = 0;
        while (n--) {
            *out1++ = 0.0;
            *out2++ = 0.0;
        }
        return;
    }

    long b_frames = buffer_getframecount(b);
    long index = x->index;
    double findex = x->findex;
    long end_frame = x->end_frame;
    double increment = x->increment;
    long start_frame = x->start_frame;
    long taper_frames = (long)(x->a_taper_dur * x->R * 0.001);
    if (taper_frames < 1) taper_frames = 1;

    double noteamp = x->amp;

    while (n--) {
        if ((increment > 0.0 && index < end_frame) || (increment < 0.0 && index > end_frame)) {
            double amp;

            // Envelope calculation
            if (increment > 0.0) {
                if (findex < (double)(start_frame + taper_frames)) {
                    amp = noteamp * ((findex - (double)start_frame) / (double)taper_frames);
                } else if (findex > (double)(end_frame - taper_frames)) {
                    amp = noteamp * (((double)end_frame - findex) / (double)taper_frames);
                } else {
                    amp = noteamp;
                }
            } else {
                if (findex > (double)(start_frame - taper_frames)) {
                    amp = noteamp * (((double)start_frame - findex) / (double)taper_frames);
                } else if (findex < (double)(end_frame + taper_frames)) {
                    amp = noteamp * ((findex - (double)end_frame) / (double)taper_frames);
                } else {
                    amp = noteamp;
                }
            }

            if (index >= 0 && index < b_frames) {
                double frac = findex - (double)index;
                long idx_L = index * 2;
                long idx_R = idx_L + 1;
                long next_L = (index < b_frames - 1) ? (idx_L + 2) : idx_L;
                long next_R = (index < b_frames - 1) ? (idx_R + 2) : idx_R;

                *out1++ = amp * ((double)tab[idx_L] + frac * ((double)tab[next_L] - (double)tab[idx_L]));
                *out2++ = amp * ((double)tab[idx_R] + frac * ((double)tab[next_R] - (double)tab[idx_R]));
            } else {
                *out1++ = 0.0;
                *out2++ = 0.0;
            }

            findex += increment;
            index = (long)findex;
        } else {
            *out1++ = 0.0;
            *out2++ = 0.0;
            x->active = 0;
        }
    }

    x->index = index;
    x->findex = findex;
    buffer_unlocksamples(b);
}

void *bvplay_new(t_symbol *s, long argc, t_atom *argv)
{
    t_bvplay *x = (t_bvplay *)object_alloc(bvplay_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 0); // 0 signal inlets

    x->l_sym = gensym("");
    x->bufref = NULL;
    x->l_chan = 1;
    x->a_taper_dur = 10.0;

    if (argc > 0) x->l_sym = atom_getsymarg(0, (short)argc, argv);
    if (argc > 1) x->l_chan = (long)atom_getfloat(argv + 1);
    if (argc > 2) x->a_taper_dur = atom_getfloat(argv + 2);

    if (x->l_chan < 1) x->l_chan = 1;
    if (x->l_chan > 2) x->l_chan = 2;

    if (x->l_chan == 1) {
        outlet_new((t_object *)x, "signal");
    } else {
        outlet_new((t_object *)x, "signal"); // Out L
        outlet_new((t_object *)x, "signal"); // Out R
    }

    if (x->a_taper_dur < 1.0) x->a_taper_dur = 1.0;

    x->R = sys_getsr();
    if (x->R <= 0.0) x->R = 44100.0;

    x->taper_frames = (long)(x->R * x->a_taper_dur * 0.001);
    x->buffer_duration = 0.0;
    x->framesize = 0;
    x->active = 0;
    x->verbose = 0;
    x->mute = 0;

    attr_args_process(x, (short)argc, argv);
    bvplay_set(x, x->l_sym);
    return x;
}

void bvplay_free(t_bvplay *x)
{
    dsp_free((t_pxobject *)x);
    if (x->bufref) {
        object_free(x->bufref);
        x->bufref = NULL;
    }
}

void bvplay_dsp64(t_bvplay *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;

    bvplay_set(x, x->l_sym);

    if (x->R != samplerate) {
        x->R = samplerate;
        x->taper_frames = (long)(x->R * x->a_taper_dur * 0.001);
    }

    if (x->l_chan == 1) {
        dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)bvplay_perform_mono64, 0, NULL);
    } else if (x->l_chan == 2) {
        dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)bvplay_perform_stereo64, 0, NULL);
    }
}