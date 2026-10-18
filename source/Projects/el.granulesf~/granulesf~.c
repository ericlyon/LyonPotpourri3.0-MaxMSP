/**
 * @file granulesf~.c
 * @brief Soundfile/buffer-based granular synthesis external for Max 8/9.
 */

#include "MSPd.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAXGRAINS (512)
#define MAXSCALE (8192)
#define OBJECT_NAME "granulesf~"

static t_class *granulesf_class;

typedef struct {
    float amplitude;
    float panL;
    float panR;
    long delay;        // samples to wait until event starts
    long duration;     // length in samples of event
    float phase;       // phase for reading buffer
    float ephase;      // phase for envelope
    float si;          // sampling increment for frequency
    float esi;         // sampling increment for envelope
    float endframe;    // boundary frame
    short active;      // 1 if active, 0 if available
} t_grain;

typedef struct _granulesf {
    t_pxobject x_obj;
    t_buffer_ref *wavebuf;   // waveform buffer reference
    t_buffer_ref *windowbuf; // window buffer reference
    t_symbol *wavename;
    t_symbol *windowname;

    float sr;
    short mute;
    short hosed;

    /* Grain generation settings */
    long events;
    long horizon;
    float min_incr;
    float max_incr;
    float minpan;
    float maxpan;
    float minamp;
    float maxamp;
    float mindur;
    float maxdur;
    t_grain *grains;
    float *pitchscale;
    int pitchsteps;
    float transpose;
    float pitch_deviation;
    short steady;
    float lowblock_increment;
    float highblock_increment;
    float mindur_ms;
    float maxdur_ms;
    float horizon_ms;
    short constrain_scale;
    short nopan;
    long minskip;
    long maxskip;
    long b_nchans;
    long b_frames;
    float retro_odds;
    short interpolate;
    short interpolate_envelope;
} t_granulesf;

/* Prototypes */
void *granulesf_new(t_symbol *msg, short argc, t_atom *argv);
void granulesf_dsp_free(t_granulesf *x);
void granulesf_setbuf(t_granulesf *x, t_symbol *wavename, t_symbol *windowname);
void granulesf_reload(t_granulesf *x);
void granulesf_dblclick(t_granulesf *x);
void granulesf_spray(t_granulesf *x);
void granulesf_pitchspray(t_granulesf *x);
void granulesf_transpose(t_granulesf *x, t_floatarg t);
void granulesf_pitchdev(t_granulesf *x, t_floatarg d);
void granulesf_lowblock(t_granulesf *x, t_floatarg f);
void granulesf_highblock(t_granulesf *x, t_floatarg f);
void granulesf_events(t_granulesf *x, t_floatarg e);
float granulesf_boundrand(float min, float max);
void *granulesf_grist(t_granulesf *x, t_symbol *msg, short argc, t_atom *argv);
void *granulesf_grain(t_granulesf *x, t_symbol *msg, short argc, t_atom *argv);
void *granulesf_setscale(t_granulesf *x, t_symbol *msg, short argc, t_atom *argv);
void granulesf_info(t_granulesf *x);
void granulesf_mute(t_granulesf *x, t_floatarg toggle);
void granulesf_steady(t_granulesf *x, t_floatarg toggle);
void granulesf_constrain_scale(t_granulesf *x, t_floatarg toggle);
void granulesf_assist(t_granulesf *x, void *b, long msg, long arg, char *dst);
void granulesf_init(t_granulesf *x, short initialized);
void granulesf_constrain(int *index_min, int *index_max, float min_incr, float max_incr, float *scale, int steps);
void granulesf_interpolate(t_granulesf *x, t_floatarg toggle);
void granulesf_nopan(t_granulesf *x, t_floatarg toggle);
void granulesf_retro_odds(t_granulesf *x, t_floatarg o);
void granulesf_seed(t_granulesf *x, t_floatarg seed);
void granulesf_interpolate_envelope(t_granulesf *x, t_floatarg toggle);
t_max_err granulesf_notify(t_granulesf *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void granulesf_dsp64(t_granulesf *x, t_object *dsp64, short *count, double sr, long n, long flags);
void granulesf_perform64(t_granulesf *x, t_object *dsp64, double **ins,
                         long numins, double **outs, long numouts, long n,
                         long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.granulesf~",
                           (method)granulesf_new,
                           (method)granulesf_dsp_free,
                           (long)sizeof(t_granulesf),
                           0L,
                           A_GIMME,
                           0);

    class_addmethod(c, (method)granulesf_dsp64,                "dsp64",                A_CANT, 0);
    class_addmethod(c, (method)granulesf_notify,               "notify",               A_CANT, 0);
    class_addmethod(c, (method)granulesf_assist,               "assist",               A_CANT, 0);
    class_addmethod(c, (method)granulesf_dblclick,             "dblclick",             A_CANT, 0);
    class_addmethod(c, (method)granulesf_setbuf,               "setbuf",               A_SYM, A_SYM, 0);
    class_addmethod(c, (method)granulesf_spray,                "spray",                0);
    class_addmethod(c, (method)granulesf_pitchspray,           "pitchspray",           0);
    class_addmethod(c, (method)granulesf_transpose,            "transpose",            A_DEFFLOAT, 0);
    class_addmethod(c, (method)granulesf_events,               "events",               A_DEFFLOAT, 0);
    class_addmethod(c, (method)granulesf_pitchdev,             "pitchdev",             A_DEFFLOAT, 0);
    class_addmethod(c, (method)granulesf_lowblock,             "lowblock",             A_DEFFLOAT, 0);
    class_addmethod(c, (method)granulesf_highblock,            "highblock",            A_DEFFLOAT, 0);
    class_addmethod(c, (method)granulesf_interpolate,          "interpolate",          A_FLOAT, 0);
    class_addmethod(c, (method)granulesf_retro_odds,           "retro_odds",           A_FLOAT, 0);
    class_addmethod(c, (method)granulesf_nopan,                "nopan",                A_FLOAT, 0);
    class_addmethod(c, (method)granulesf_seed,                 "seed",                 A_FLOAT, 0);
    class_addmethod(c, (method)granulesf_mute,                 "mute",                 A_DEFFLOAT, 0);
    class_addmethod(c, (method)granulesf_steady,               "steady",               A_DEFFLOAT, 0);
    class_addmethod(c, (method)granulesf_interpolate_envelope, "interpolate_envelope", A_FLOAT, 0);
    class_addmethod(c, (method)granulesf_constrain_scale,      "constrain_scale",      A_DEFFLOAT, 0);
    class_addmethod(c, (method)granulesf_grist,                "grist",                A_GIMME, 0);
    class_addmethod(c, (method)granulesf_grain,                "grain",                A_GIMME, 0);
    class_addmethod(c, (method)granulesf_setscale,             "setscale",             A_GIMME, 0);
    class_addmethod(c, (method)granulesf_info,                 "info",                 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    granulesf_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

float granulesf_boundrand(float min, float max)
{
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}

void granulesf_interpolate_envelope(t_granulesf *x, t_floatarg toggle) { x->interpolate_envelope = (short)toggle; }
void granulesf_seed(t_granulesf *x, t_floatarg seed)                  { srand((unsigned int)seed); }
void granulesf_interpolate(t_granulesf *x, t_floatarg toggle)          { x->interpolate = (short)toggle; }
void granulesf_nopan(t_granulesf *x, t_floatarg toggle)                { x->nopan = (short)toggle; }
void granulesf_constrain_scale(t_granulesf *x, t_floatarg toggle)      { x->constrain_scale = (short)toggle; }
void granulesf_mute(t_granulesf *x, t_floatarg toggle)                 { x->mute = (short)toggle; }
void granulesf_steady(t_granulesf *x, t_floatarg toggle)               { x->steady = (short)toggle; }

void granulesf_retro_odds(t_granulesf *x, t_floatarg o)
{
    if (o < 0.0f || o > 1.0f) {
        object_error((t_object *)x, "%s: retro odds must be within [0.0 - 1.0]", OBJECT_NAME);
        return;
    }
    x->retro_odds = (float)o;
}

void granulesf_lowblock(t_granulesf *x, t_floatarg f)
{
    if (f > 0.0f) x->lowblock_increment = (float)f;
}

void granulesf_highblock(t_granulesf *x, t_floatarg f)
{
    if (f > 0.0f) x->highblock_increment = (float)f;
}

void granulesf_pitchdev(t_granulesf *x, t_floatarg d)
{
    if (d < 0.0f) {
        object_error((t_object *)x, "%s: pitch deviation must be positive", OBJECT_NAME);
        return;
    }
    x->pitch_deviation = (float)d;
}

void granulesf_events(t_granulesf *x, t_floatarg e)
{
    if (e <= 0.0f) {
        object_error((t_object *)x, "%s: events must be positive", OBJECT_NAME);
        return;
    }
    x->events = (long)e;
}

void granulesf_transpose(t_granulesf *x, t_floatarg t)
{
    if (t <= 0.0f) {
        object_error((t_object *)x, "%s: transpose factor must be greater than zero", OBJECT_NAME);
        return;
    }
    x->transpose = (float)t;
}

void *granulesf_setscale(t_granulesf *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc >= MAXSCALE) {
        object_error((t_object *)x, "%s: %d is the maximum scale size", OBJECT_NAME, MAXSCALE);
        return 0;
    }
    if (argc < 2) {
        object_error((t_object *)x, "%s: scale requires at least 2 members", OBJECT_NAME);
        return 0;
    }
    for (short i = 0; i < argc; i++) {
        x->pitchscale[i] = atom_getfloatarg(i, argc, argv);
    }
    x->pitchsteps = (int)argc;
    return 0;
}

void granulesf_constrain(int *index_min, int *index_max, float min_incr, float max_incr, float *scale, int steps)
{
    int imax = steps - 1;
    int imin = 0;
    while (scale[imin] < min_incr && imin < imax) ++imin;
    if (imin == imax) {
        *index_min = 0;
        *index_max = steps - 1;
        return;
    }
    while (scale[imax] > max_incr && imax > 0) --imax;
    if (imax < 1 || imax <= imin) {
        *index_min = 0;
        *index_max = steps - 1;
        return;
    }
    *index_min = imin;
    *index_max = imax;
}

void granulesf_pitchspray(t_granulesf *x)
{
    if (!sys_getdspstate() || x->hosed) return;

    t_buffer_obj *wbuf = buffer_ref_getobject(x->wavebuf);
    t_buffer_obj *winbuf = buffer_ref_getobject(x->windowbuf);
    if (!wbuf || !winbuf) return;

    long b_frames = buffer_getframecount(wbuf);
    long eframes = buffer_getframecount(winbuf);
    if (b_frames <= 0 || eframes <= 0 || x->pitchsteps < 2 || x->sr <= 0.0f) return;

    float pdev = 0.0f, pdev_invert = 0.0f;
    if (x->pitch_deviation > 0.0f) {
        pdev = 1.0f + x->pitch_deviation;
        pdev_invert = 1.0f / pdev;
    }

    t_grain *grains = x->grains;
    for (long i = 0; i < x->events; i++) {
        for (long j = 0; j < MAXGRAINS; j++) {
            if (!grains[j].active) {
                grains[j].delay = x->steady ? (long)((float)(i * x->horizon) / (float)x->events)
                                            : (long)granulesf_boundrand(0.0f, (float)x->horizon);
                grains[j].duration = (long)granulesf_boundrand(x->mindur, x->maxdur);
                if (grains[j].duration <= 0) grains[j].duration = 1;

                grains[j].phase = 0.0f;
                grains[j].ephase = 0.0f;
                float pan = granulesf_boundrand(x->minpan, x->maxpan);
                grains[j].amplitude = granulesf_boundrand(x->minamp, x->maxamp);
                grains[j].panL = grains[j].amplitude * cosf(pan * (float)PIOVERTWO);
                grains[j].panR = grains[j].amplitude * sinf(pan * (float)PIOVERTWO);
                grains[j].amplitude *= 0.707f;

                int windex;
                if (x->constrain_scale) {
                    int index_min = 0, index_max = x->pitchsteps - 1;
                    granulesf_constrain(&index_min, &index_max, x->min_incr, x->max_incr, x->pitchscale, x->pitchsteps);
                    windex = (int)granulesf_boundrand((float)index_min, (float)index_max);
                } else {
                    windex = (int)granulesf_boundrand(0.0f, (float)(x->pitchsteps - 1));
                }

                grains[j].si = x->transpose * x->pitchscale[windex];
                if (x->pitch_deviation > 0.0f) {
                    grains[j].si *= granulesf_boundrand(pdev_invert, pdev);
                }

                if (x->lowblock_increment > 0.0f && grains[j].si < x->lowblock_increment) goto nextgrain;
                if (x->highblock_increment > 0.0f && grains[j].si > x->highblock_increment) goto nextgrain;

                long grainframes = (long)(grains[j].duration * fabsf(grains[j].si));
                grains[j].esi = (float)eframes / (float)grains[j].duration;

                if (grainframes >= b_frames) goto nextgrain;

                if (x->minskip > (b_frames - grainframes)) {
                    grains[j].phase = 0.0f;
                    grains[j].endframe = (float)(grainframes - 1);
                } else {
                    long upper = (x->maxskip > (b_frames - grainframes)) ? (b_frames - grainframes) : x->maxskip;
                    grains[j].phase = granulesf_boundrand((float)x->minskip, (float)upper);
                    grains[j].endframe = grains[j].phase + (float)(grainframes - 1);
                }

                if (granulesf_boundrand(0.0f, 1.0f) < x->retro_odds) {
                    grains[j].si *= -1.0f;
                    float tmp = grains[j].phase;
                    grains[j].phase = grains[j].endframe;
                    grains[j].endframe = tmp;
                }

                grains[j].active = 1;
                break;
            }
        }
    nextgrain:;
    }
}

void granulesf_spray(t_granulesf *x)
{
    if (!sys_getdspstate() || x->hosed) return;

    t_buffer_obj *wbuf = buffer_ref_getobject(x->wavebuf);
    t_buffer_obj *winbuf = buffer_ref_getobject(x->windowbuf);
    if (!wbuf || !winbuf) return;

    long b_frames = buffer_getframecount(wbuf);
    long eframes = buffer_getframecount(winbuf);
    if (b_frames <= 0 || eframes <= 0 || x->sr <= 0.0f) return;

    t_grain *grains = x->grains;
    for (long i = 0; i < x->events; i++) {
        for (long j = 0; j < MAXGRAINS; j++) {
            if (!grains[j].active) {
                grains[j].delay = x->steady ? (long)((float)(i * x->horizon) / (float)x->events)
                                            : (long)granulesf_boundrand(0.0f, (float)x->horizon);
                grains[j].duration = (long)granulesf_boundrand(x->mindur, x->maxdur);
                if (grains[j].duration <= 0) grains[j].duration = 1;

                grains[j].ephase = 0.0f;
                float pan = granulesf_boundrand(x->minpan, x->maxpan);
                grains[j].amplitude = granulesf_boundrand(x->minamp, x->maxamp);
                grains[j].panL = grains[j].amplitude * cosf(pan * (float)PIOVERTWO);
                grains[j].panR = grains[j].amplitude * sinf(pan * (float)PIOVERTWO);
                grains[j].amplitude *= 0.707f;
                grains[j].si = x->transpose * granulesf_boundrand(x->min_incr, x->max_incr);

                long grainframes = (long)(grains[j].duration * fabsf(grains[j].si));
                grains[j].esi = (float)eframes / (float)grains[j].duration;

                if (grainframes >= b_frames) goto nextgrain;

                if (x->minskip > (b_frames - grainframes)) {
                    grains[j].phase = 0.0f;
                    grains[j].endframe = (float)(grainframes - 1);
                } else {
                    long upper = (x->maxskip > (b_frames - grainframes)) ? (b_frames - grainframes) : x->maxskip;
                    grains[j].phase = granulesf_boundrand((float)x->minskip, (float)upper);
                    grains[j].endframe = grains[j].phase + (float)(grainframes - 1);
                }

                if (granulesf_boundrand(0.0f, 1.0f) < x->retro_odds) {
                    grains[j].si *= -1.0f;
                    float tmp = grains[j].phase;
                    grains[j].phase = grains[j].endframe;
                    grains[j].endframe = tmp;
                }

                grains[j].active = 1;
                break;
            }
        }
    nextgrain:;
    }
}

void *granulesf_grain(t_granulesf *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (!sys_getdspstate()) {
        object_error((t_object *)x, "%s: DSP must be active to deploy grains", OBJECT_NAME);
        return 0;
    }
    if (argc < 5) {
        object_error((t_object *)x, "%s: grain requires: duration incr amp pan skip_ms", OBJECT_NAME);
        return 0;
    }

    t_buffer_obj *wbuf = buffer_ref_getobject(x->wavebuf);
    t_buffer_obj *winbuf = buffer_ref_getobject(x->windowbuf);
    if (!wbuf || !winbuf) return 0;

    long frames = buffer_getframecount(wbuf);
    long eframes = buffer_getframecount(winbuf);
    if (frames <= 0 || eframes <= 0 || x->sr <= 0.0f) return 0;

    float duration = atom_getfloatarg(0, argc, argv);
    float incr = atom_getfloatarg(1, argc, argv);
    float amplitude = atom_getfloatarg(2, argc, argv);
    float pan = atom_getfloatarg(3, argc, argv);
    float skip = atom_getfloatarg(4, argc, argv) * 0.001f * x->sr;

    if (duration <= 0.0f || incr == 0.0f || pan < 0.0f || pan > 1.0f || skip < 0.0f || skip >= (float)frames) {
        object_error((t_object *)x, "%s: invalid parameters for grain", OBJECT_NAME);
        return 0;
    }

    t_grain *grains = x->grains;
    for (long j = 0; j < MAXGRAINS; j++) {
        if (!grains[j].active) {
            grains[j].delay = 0;
            grains[j].duration = (long)(0.001f * x->sr * duration);
            if (grains[j].duration <= 0) grains[j].duration = 1;
            grains[j].phase = skip;
            grains[j].ephase = 0.0f;
            grains[j].amplitude = amplitude * 0.707f;
            grains[j].panL = amplitude * cosf(pan * (float)PIOVERTWO);
            grains[j].panR = amplitude * sinf(pan * (float)PIOVERTWO);
            grains[j].esi = (float)eframes / (float)grains[j].duration;
            grains[j].si = incr;
            grains[j].active = 1;
            return 0;
        }
    }
    return 0;
}

void *granulesf_grist(t_granulesf *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 10) {
        object_error((t_object *)x, "%s: grist requires 10 arguments", OBJECT_NAME);
        return 0;
    }

    x->events = (long)atom_getfloatarg(0, argc, argv);
    x->horizon_ms = atom_getfloatarg(1, argc, argv);
    x->min_incr = atom_getfloatarg(2, argc, argv);
    x->max_incr = atom_getfloatarg(3, argc, argv);
    x->minpan = atom_getfloatarg(4, argc, argv);
    x->maxpan = atom_getfloatarg(5, argc, argv);
    x->minamp = atom_getfloatarg(6, argc, argv);
    x->maxamp = atom_getfloatarg(7, argc, argv);
    x->mindur_ms = atom_getfloatarg(8, argc, argv);
    x->maxdur_ms = atom_getfloatarg(9, argc, argv);

    x->mindur = 0.001f * x->sr * x->mindur_ms;
    x->maxdur = 0.001f * x->sr * x->maxdur_ms;
    x->horizon = (long)(0.001f * x->sr * x->horizon_ms);

    if (x->min_incr < 0.0f) x->min_incr = fabsf(x->min_incr);
    if (x->max_incr < 0.0f) x->max_incr = fabsf(x->max_incr);
    if (x->minpan < 0.0f) x->minpan = 0.0f;
    if (x->maxpan > 1.0f) x->maxpan = 1.0f;
    if (x->events < 0) x->events = 0;
    return 0;
}

void *granulesf_new(t_symbol *msg, short argc, t_atom *argv)
{
    t_granulesf *x = (t_granulesf *)object_alloc(granulesf_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 0); // 0 signal inlets
    outlet_new((t_object *)x, "signal"); // Out L
    outlet_new((t_object *)x, "signal"); // Out R

    x->pitchscale = (float *)sysmem_newptrclear(MAXSCALE * sizeof(float));
    x->grains = (t_grain *)sysmem_newptrclear(MAXGRAINS * sizeof(t_grain));

    x->wavename = gensym("waveform");
    x->windowname = gensym("window");

    if (argc > 0) atom_arg_getsym(&x->wavename, 0, argc, argv);
    if (argc > 1) atom_arg_getsym(&x->windowname, 1, argc, argv);

    x->wavebuf = buffer_ref_new((t_object *)x, x->wavename);
    x->windowbuf = buffer_ref_new((t_object *)x, x->windowname);

    x->sr = sys_getsr();
    if (x->sr <= 0.0f) x->sr = 44100.0f;

    granulesf_init(x, 0);
    return x;
}

void granulesf_init(t_granulesf *x, short initialized)
{
    if (!initialized) {
        x->pitchsteps = 0;
        x->mute = 0;
        x->steady = 0;
        x->events = 1;
        x->horizon_ms = 1000.0f;
        x->min_incr = 0.5f;
        x->max_incr = 2.0f;
        x->minpan = 0.1f;
        x->maxpan = 0.9f;
        x->minamp = 0.1f;
        x->maxamp = 1.0f;
        x->mindur_ms = 150.0f;
        x->maxdur_ms = 750.0f;
        x->transpose = 1.0f;
        x->pitch_deviation = 0.0f;
        x->lowblock_increment = 0.0f;
        x->highblock_increment = 0.0f;
        x->constrain_scale = 0;
        x->retro_odds = 0.0f;
        x->maxskip = -1;
        x->nopan = 0;
        x->interpolate = 1;
        x->interpolate_envelope = 0;
    }
    x->horizon = (long)(x->horizon_ms * 0.001f * x->sr);
    x->mindur = x->mindur_ms * 0.001f * x->sr;
    x->maxdur = x->maxdur_ms * 0.001f * x->sr;

    if (x->grains) {
        for (long i = 0; i < MAXGRAINS; i++) {
            x->grains[i].active = 0;
        }
    }
}

void granulesf_dsp_free(t_granulesf *x)
{
    dsp_free((t_pxobject *)x);

    if (x->grains) {
        sysmem_freeptr(x->grains);
        x->grains = NULL;
    }
    if (x->pitchscale) {
        sysmem_freeptr(x->pitchscale);
        x->pitchscale = NULL;
    }
    if (x->wavebuf) {
        object_free(x->wavebuf);
        x->wavebuf = NULL;
    }
    if (x->windowbuf) {
        object_free(x->windowbuf);
        x->windowbuf = NULL;
    }
}

void granulesf_setbuf(t_granulesf *x, t_symbol *wavename, t_symbol *windowname)
{
    if (!x->wavebuf) x->wavebuf = buffer_ref_new((t_object *)x, wavename);
    else buffer_ref_set(x->wavebuf, wavename);

    if (!x->windowbuf) x->windowbuf = buffer_ref_new((t_object *)x, windowname);
    else buffer_ref_set(x->windowbuf, windowname);

    x->hosed = 0;
    for (int i = 0; i < MAXGRAINS; i++) x->grains[i].active = 0;

    t_buffer_obj *b_wave = buffer_ref_getobject(x->wavebuf);
    if (!b_wave) {
        object_error((t_object *)x, "%s: invalid sound buffer %s", OBJECT_NAME, wavename->s_name);
        x->hosed = 1;
    } else {
        x->b_frames = buffer_getframecount(b_wave);
        x->b_nchans = buffer_getchannelcount(b_wave);
        if (x->b_nchans > 2 || x->b_nchans < 1) {
            object_error((t_object *)x, "%s: soundfile buffer must be mono or stereo", OBJECT_NAME);
            x->hosed = 1;
        }
    }

    t_buffer_obj *b_win = buffer_ref_getobject(x->windowbuf);
    if (!b_win) {
        object_error((t_object *)x, "%s: invalid window buffer %s", OBJECT_NAME, windowname->s_name);
        x->hosed = 1;
    } else if (buffer_getchannelcount(b_win) != 1) {
        object_error((t_object *)x, "%s: window buffer must be mono", OBJECT_NAME);
        x->hosed = 1;
    }

    x->wavename = wavename;
    x->windowname = windowname;
    x->maxskip = (x->b_frames > 0) ? (x->b_frames - 1) : 0;
}

void granulesf_reload(t_granulesf *x)
{
    granulesf_setbuf(x, x->wavename, x->windowname);
}

void granulesf_dblclick(t_granulesf *x)
{
    granulesf_reload(x);
    if (x->wavebuf) buffer_view(buffer_ref_getobject(x->wavebuf));
    if (x->windowbuf) buffer_view(buffer_ref_getobject(x->windowbuf));
}

void granulesf_info(t_granulesf *x)
{
    t_buffer_obj *buffer = buffer_ref_getobject(x->wavebuf);
    long b_len = buffer ? buffer_getframecount(buffer) : 0;
    int tcount = 0;
    for (int i = 0; i < MAXGRAINS; i++) {
        if (x->grains[i].active) ++tcount;
    }
    post("%s: %d active grains | wave: %s (%ld frames) | win: %s",
         OBJECT_NAME, tcount, x->wavename->s_name, b_len, x->windowname->s_name);
}

t_max_err granulesf_notify(t_granulesf *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (x->windowbuf) buffer_ref_notify(x->windowbuf, s, msg, sender, data);
    if (x->wavebuf)   return buffer_ref_notify(x->wavebuf, s, msg, sender, data);
    return MAX_ERR_NONE;
}

void granulesf_perform64(t_granulesf *x, t_object *dsp64, double **ins,
                         long numins, double **outs, long numouts, long n,
                         long flags, void *userparam)
{
    t_double *outputL = outs[0];
    t_double *outputR = outs[1];

    for (long i = 0; i < n; i++) {
        outputL[i] = 0.0;
        outputR[i] = 0.0;
    }

    if (x->mute || x->hosed) return;

    t_buffer_obj *wavebuf_b = buffer_ref_getobject(x->wavebuf);
    t_buffer_obj *windowbuf_b = buffer_ref_getobject(x->windowbuf);
    if (!wavebuf_b || !windowbuf_b) return;

    long b_frames = buffer_getframecount(wavebuf_b);
    long eframes = buffer_getframecount(windowbuf_b);
    if (b_frames <= 0 || eframes <= 0) return;

    long b_nchans = buffer_getchannelcount(wavebuf_b);
    float *wavetable = buffer_locksamples(wavebuf_b);
    float *window = buffer_locksamples(windowbuf_b);

    if (!wavetable || !window) {
        if (wavetable) buffer_unlocksamples(wavebuf_b);
        if (window) buffer_unlocksamples(windowbuf_b);
        return;
    }

    t_grain *grains = x->grains;
    short nopan = x->nopan;
    short interp_env = x->interpolate_envelope;

    for (long j = 0; j < MAXGRAINS; j++) {
        if (!grains[j].active) continue;

        float amplitude = grains[j].amplitude;
        float si = grains[j].si;
        float esi = grains[j].esi;
        float phase = grains[j].phase;
        float ephase = grains[j].ephase;
        long delay = grains[j].delay;
        float panL = grains[j].panL;
        float panR = grains[j].panR;

        for (long i = 0; i < n; i++) {
            if (delay > 0) {
                --delay;
            } else if (ephase < (float)eframes) {
                // Envelope read
                float envelope;
                if (interp_env) {
                    long c_idx = (long)ephase;
                    if (c_idx >= eframes - 1) {
                        envelope = window[eframes - 1];
                    } else if (c_idx < 0) {
                        envelope = window[0];
                    } else {
                        float frac = ephase - (float)c_idx;
                        envelope = window[c_idx] + frac * (window[c_idx + 1] - window[c_idx]);
                    }
                } else {
                    long idx_e = (long)ephase;
                    if (idx_e < 0) idx_e = 0;
                    else if (idx_e >= eframes) idx_e = eframes - 1;
                    envelope = window[idx_e];
                }

                // Waveform read
                if (phase < 0.0f || phase >= (float)b_frames) {
                    grains[j].active = 0;
                    break;
                }

                long c_idx = (long)phase;
                float frac = phase - (float)c_idx;
                float sample1 = 0.0f, sample2 = 0.0f;

                if (b_nchans == 1) {
                    if (c_idx >= b_frames - 1 || frac == 0.0f) {
                        sample1 = wavetable[c_idx];
                    } else {
                        sample1 = wavetable[c_idx] + frac * (wavetable[c_idx + 1] - wavetable[c_idx]);
                    }
                    sample1 *= envelope;
                    if (nopan) {
                        outputL[i] += sample1 * amplitude;
                        outputR[i] += sample1 * amplitude;
                    } else {
                        outputL[i] += panL * sample1;
                        outputR[i] += panR * sample1;
                    }
                } else if (b_nchans == 2) {
                    long s_idx = c_idx * 2;
                    if (c_idx >= b_frames - 1 || frac == 0.0f) {
                        sample1 = wavetable[s_idx];
                        sample2 = wavetable[s_idx + 1];
                    } else {
                        sample1 = wavetable[s_idx] + frac * (wavetable[s_idx + 2] - wavetable[s_idx]);
                        sample2 = wavetable[s_idx + 1] + frac * (wavetable[s_idx + 3] - wavetable[s_idx + 1]);
                    }
                    sample1 *= envelope;
                    sample2 *= envelope;
                    if (nopan) {
                        outputL[i] += sample1 * amplitude;
                        outputR[i] += sample2 * amplitude;
                    } else {
                        outputL[i] += panL * sample1;
                        outputR[i] += panR * sample2;
                    }
                }

                phase += si;
                ephase += esi;

                if (ephase >= (float)eframes || phase < 0.0f || phase >= (float)b_frames) {
                    grains[j].active = 0;
                    break;
                }
            }
        }
        grains[j].phase = phase;
        grains[j].ephase = ephase;
        grains[j].delay = delay;
    }

    buffer_unlocksamples(wavebuf_b);
    buffer_unlocksamples(windowbuf_b);
}

void granulesf_dsp64(t_granulesf *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;

    if (x->sr != (float)sr) {
        x->sr = (float)sr;
        granulesf_init(x, 1);
    }
    granulesf_reload(x);
    if (x->hosed) return;

    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)granulesf_perform64, 0, NULL);
}

void granulesf_assist(t_granulesf *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(messages) Control Input", 256);
    } else if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Output 1 (L)", 256); break;
            case 1: strncpy_zero(dst, "(signal) Output 2 (R)", 256); break;
        }
    }
}