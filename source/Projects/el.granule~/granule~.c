/**
 * @file granule~.c
 * @brief Wavetable granular synthesis external for Max 8/9.
 * Modernized for 64-bit dsp64, memory safety, and buffer lifecycle integrity.
 */

#include "MSPd.h"

#define MAXGRAINS (512)
#define MAXSCALE (8192)
#define OBJECT_NAME "granule~"

static t_class *granule_class;

typedef struct {
    float amplitude;
    float panL;
    float panR;
    long delay;        // samples to wait until event starts
    long duration;     // length in samples of event
    float phase;       // phase for frequency oscillator
    float ephase;      // phase for envelope
    float si;          // sampling increment for frequency
    float esi;         // sampling increment for envelope
} t_grain;

typedef struct _granule {
    t_pxobject x_obj;
    t_buffer_ref *wavebuf_ref;   // holds waveform samples
    t_buffer_ref *windowbuf_ref; // holds window samples
    t_symbol *wavename;          // name of waveform buffer
    t_symbol *windowname;        // name of window buffer
    float sr;                    // sampling rate
    short mute;
    short hosed;                 // buffers are bad

    /* Global grain data */
    long events;                 // number of events in a block
    long horizon;                // length of block for random events
    float minfreq;               // minimum frequency for a grain
    float maxfreq;               // maximum frequency for a grain
    float minpan;                // minimum pan for a grain
    float maxpan;                // maximum pan for a grain
    float minamp;                // minimum amplitude for a grain
    float maxamp;                // maximum amplitude for a grain
    float mindur;                // minimum duration for a grain
    float maxdur;                // maximum duration for a grain
    t_grain *grains;             // stores grain data
    float *pitchscale;           // contains a frequency grid for pitch constraint
    int pitchsteps;              // number of members in scale
    float transpose;             // factor for scaling all pitches
    float pitch_deviation;       // factor to adjust scaled pitches
    short steady;                // toggles pulsed rhythmic activity
    float lowblock_freq;         // lowest allowed frequency
    float highblock_freq;        // highest allowed frequency
    float mindur_ms;             // store duration in ms
    float maxdur_ms;
    float horizon_ms;
    short constrain_scale;       // flag to only use bounded portion of scale
} t_granule;

/* Method Prototypes */
void *granule_new(t_symbol *msg, long argc, t_atom *argv);
void granule_dsp_free(t_granule *x);
void granule_setbuf(t_granule *x, t_symbol *wavename, t_symbol *windowname);
void granule_reload(t_granule *x);
void granule_dblclick(t_granule *x);
void granule_spray(t_granule *x);
void granule_pitchspray(t_granule *x);
void granule_transpose(t_granule *x, double t);
void granule_pitchdev(t_granule *x, double d);
void granule_lowblock(t_granule *x, double f);
void granule_highblock(t_granule *x, double f);
void granule_events(t_granule *x, double e);
float granule_boundrand(float min, float max);
void granule_grist(t_granule *x, t_symbol *msg, long argc, t_atom *argv);
void granule_grain(t_granule *x, t_symbol *msg, long argc, t_atom *argv);
void granule_setscale(t_granule *x, t_symbol *msg, long argc, t_atom *argv);
void granule_info(t_granule *x);
void granule_mute(t_granule *x, double toggle);
void granule_steady(t_granule *x, double toggle);
void granule_constrain_scale(t_granule *x, double toggle);
void granule_assist(t_granule *x, void *b, long msg, long arg, char *dst);
void granule_init(t_granule *x, short initialized);
void granule_constrain(int *index_min, int *index_max, float minfreq, float maxfreq, float *scale, int steps);
t_max_err granule_notify(t_granule *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void granule_dsp64(t_granule *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void granule_perform64(t_granule *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long n, long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.granule~", 
                           (method)granule_new, 
                           (method)granule_dsp_free, 
                           (long)sizeof(t_granule), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)granule_dsp64,           "dsp64",           A_CANT, 0);
    class_addmethod(c, (method)granule_notify,          "notify",          A_CANT, 0);
    class_addmethod(c, (method)granule_assist,          "assist",          A_CANT, 0);
    class_addmethod(c, (method)granule_dblclick,        "dblclick",        A_CANT, 0);
    class_addmethod(c, (method)granule_setbuf,          "setbuf",          A_SYM, A_SYM, 0);
    class_addmethod(c, (method)granule_spray,           "spray",           0);
    class_addmethod(c, (method)granule_pitchspray,      "pitchspray",      0);
    class_addmethod(c, (method)granule_transpose,       "transpose",       A_FLOAT, 0);
    class_addmethod(c, (method)granule_events,          "events",          A_FLOAT, 0);
    class_addmethod(c, (method)granule_pitchdev,        "pitchdev",        A_FLOAT, 0);
    class_addmethod(c, (method)granule_lowblock,        "lowblock",        A_FLOAT, 0);
    class_addmethod(c, (method)granule_highblock,       "highblock",       A_FLOAT, 0);
    class_addmethod(c, (method)granule_steady,          "steady",          A_FLOAT, 0);
    class_addmethod(c, (method)granule_mute,            "mute",            A_FLOAT, 0);
    class_addmethod(c, (method)granule_constrain_scale, "constrain_scale", A_FLOAT, 0);
    class_addmethod(c, (method)granule_grist,           "grist",           A_GIMME, 0);
    class_addmethod(c, (method)granule_grain,           "grain",           A_GIMME, 0);
    class_addmethod(c, (method)granule_setscale,        "setscale",        A_GIMME, 0);
    class_addmethod(c, (method)granule_info,            "info",            0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    granule_class = c;

    potpourri_announce(OBJECT_NAME);
    // post("updated version of granule~\n");
    return 0;
}

float granule_boundrand(float min, float max)
{
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}

void granule_constrain_scale(t_granule *x, double toggle) { x->constrain_scale = (short)toggle; }
void granule_lowblock(t_granule *x, double f)             { if (f > 0.0) x->lowblock_freq = (float)f; }
void granule_highblock(t_granule *x, double f)            { if (f > 0.0) x->highblock_freq = (float)f; }
void granule_mute(t_granule *x, double toggle)            { x->mute = (short)toggle; }
void granule_steady(t_granule *x, double toggle)          { x->steady = (short)toggle; }

void granule_pitchdev(t_granule *x, double d)
{
    if (d < 0.0) {
        object_error((t_object *)x, "%s: pitch deviation must be positive", OBJECT_NAME);
        return;
    }
    x->pitch_deviation = (float)d;
}

void granule_events(t_granule *x, double e)
{
    if (e <= 0.0) {
        object_error((t_object *)x, "%s: events must be positive", OBJECT_NAME);
        return;
    }
    x->events = (long)e;
}

void granule_transpose(t_granule *x, double t)
{
    if (t <= 0.0) {
        object_error((t_object *)x, "%s: transpose factor must be greater than zero", OBJECT_NAME);
        return;
    }
    x->transpose = (float)t;
}

void granule_setscale(t_granule *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc >= MAXSCALE) {
        object_error((t_object *)x, "%s: %d is the maximum scale size", OBJECT_NAME, MAXSCALE);
        return;
    }
    if (argc < 2) {
        object_error((t_object *)x, "%s: scale requires at least 2 members", OBJECT_NAME);
        return;
    }
    for (long i = 0; i < argc; i++) {
        x->pitchscale[i] = atom_getfloat(argv + i);
    }
    x->pitchsteps = (int)argc;
}

void granule_constrain(int *index_min, int *index_max, float minfreq, float maxfreq, float *scale, int steps)
{
    int imax = steps - 1;
    int imin = 0;
    while (scale[imin] < minfreq && imin < imax) ++imin;
    if (imin == imax) {
        *index_min = 0;
        *index_max = steps - 1;
        return;
    }
    while (scale[imax] > maxfreq && imax > 0) --imax;
    if (imax < 1 || imax <= imin) {
        *index_min = 0;
        *index_max = steps - 1;
        return;
    }
    *index_min = imin;
    *index_max = imax;
}

void granule_pitchspray(t_granule *x)
{
    if (!sys_getdspstate() || x->hosed) return;

    t_buffer_obj *wavebuf = buffer_ref_getobject(x->wavebuf_ref);
    t_buffer_obj *windowbuf = buffer_ref_getobject(x->windowbuf_ref);
    if (!wavebuf || !windowbuf) return;

    long frames = buffer_getframecount(wavebuf);
    long eframes = buffer_getframecount(windowbuf);
    if (frames <= 0 || eframes <= 0 || x->pitchsteps < 2 || x->sr <= 0.0f) return;

    float pdev = 0.0f, pdev_invert = 0.0f;
    if (x->pitch_deviation > 0.0f) {
        pdev = 1.0f + x->pitch_deviation;
        pdev_invert = 1.0f / pdev;
    }

    t_grain *grains = x->grains;
    for (long i = 0; i < x->events; i++) {
        for (long j = 0; j < MAXGRAINS; j++) {
            if (grains[j].ephase >= (float)eframes) {
                grains[j].delay = x->steady ? (long)((float)(i * x->horizon) / (float)x->events)
                                            : (long)granule_boundrand(0.0f, (float)x->horizon);
                grains[j].duration = (long)granule_boundrand(x->mindur, x->maxdur);
                if (grains[j].duration <= 0) grains[j].duration = 1;

                grains[j].phase = 0.0f;
                grains[j].ephase = 0.0f;
                float pan = granule_boundrand(x->minpan, x->maxpan);
                grains[j].panL = cosf(pan * (float)PIOVERTWO);
                grains[j].panR = sinf(pan * (float)PIOVERTWO);
                grains[j].amplitude = granule_boundrand(x->minamp, x->maxamp);
                grains[j].esi = (float)eframes / (float)grains[j].duration;

                int windex;
                if (x->constrain_scale) {
                    int index_min = 0, index_max = x->pitchsteps - 1;
                    granule_constrain(&index_min, &index_max, x->minfreq, x->maxfreq, x->pitchscale, x->pitchsteps);
                    windex = (int)granule_boundrand((float)index_min, (float)index_max);
                } else {
                    windex = (int)granule_boundrand(0.0f, (float)(x->pitchsteps - 1));
                }

                grains[j].si = x->transpose * x->pitchscale[windex] * (float)frames / x->sr;
                if (x->pitch_deviation > 0.0f) {
                    grains[j].si *= granule_boundrand(pdev_invert, pdev);
                }

                if (x->lowblock_freq > 0.0f && (grains[j].si * (x->sr / (float)frames)) < x->lowblock_freq) {
                    grains[j].ephase = (float)eframes; // Abort grain
                }
                if (x->highblock_freq > 0.0f && (grains[j].si * (x->sr / (float)frames)) > x->highblock_freq) {
                    grains[j].ephase = (float)eframes; // Abort grain
                }
                break;
            }
        }
    }
}

void granule_spray(t_granule *x)
{
    if (!sys_getdspstate() || x->hosed) return;

    t_buffer_obj *wavebuf = buffer_ref_getobject(x->wavebuf_ref);
    t_buffer_obj *windowbuf = buffer_ref_getobject(x->windowbuf_ref);
    if (!wavebuf || !windowbuf) return;

    long frames = buffer_getframecount(wavebuf);
    long eframes = buffer_getframecount(windowbuf);
    if (frames <= 0 || eframes <= 0 || x->sr <= 0.0f) return;

    t_grain *grains = x->grains;
    for (long i = 0; i < x->events; i++) {
        for (long j = 0; j < MAXGRAINS; j++) {
            if (grains[j].ephase >= (float)eframes) {
                grains[j].delay = x->steady ? (long)((float)(i * x->horizon) / (float)x->events)
                                            : (long)granule_boundrand(0.0f, (float)x->horizon);
                grains[j].duration = (long)granule_boundrand(x->mindur, x->maxdur);
                if (grains[j].duration <= 0) grains[j].duration = 1;

                grains[j].phase = 0.0f;
                grains[j].ephase = 0.0f;
                float pan = granule_boundrand(x->minpan, x->maxpan);
                grains[j].panL = cosf(pan * (float)PIOVERTWO);
                grains[j].panR = sinf(pan * (float)PIOVERTWO);
                grains[j].amplitude = granule_boundrand(x->minamp, x->maxamp);
                grains[j].esi = (float)eframes / (float)grains[j].duration;
                grains[j].si = x->transpose * granule_boundrand(x->minfreq, x->maxfreq) * (float)frames / x->sr;
                break;
            }
        }
    }
}

void granule_grain(t_granule *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (!sys_getdspstate()) {
        object_error((t_object *)x, "%s: DSP must be on to deploy grains", OBJECT_NAME);
        return;
    }
    if (argc < 4) {
        object_error((t_object *)x, "%s: grain takes 4 arguments: <duration_ms> <freq> <amp> <pan>", OBJECT_NAME);
        return;
    }

    t_buffer_obj *wavebuf = buffer_ref_getobject(x->wavebuf_ref);
    t_buffer_obj *windowbuf = buffer_ref_getobject(x->windowbuf_ref);
    if (!wavebuf || !windowbuf) return;

    long frames = buffer_getframecount(wavebuf);
    long eframes = buffer_getframecount(windowbuf);
    if (frames <= 0 || eframes <= 0 || x->sr <= 0.0f) return;

    float duration_ms = atom_getfloat(argv);
    float frequency = atom_getfloat(argv + 1);
    float amplitude = atom_getfloat(argv + 2);
    float pan = atom_getfloat(argv + 3);

    if (duration_ms <= 0.0f || frequency <= 0.0f || pan < 0.0f || pan > 1.0f) {
        object_error((t_object *)x, "%s: invalid parameters for grain", OBJECT_NAME);
        return;
    }

    t_grain *grains = x->grains;
    for (long j = 0; j < MAXGRAINS; j++) {
        if (grains[j].ephase >= (float)eframes) {
            grains[j].delay = 0;
            grains[j].duration = (long)(0.001f * x->sr * duration_ms);
            if (grains[j].duration <= 0) grains[j].duration = 1;
            grains[j].phase = 0.0f;
            grains[j].ephase = 0.0f;
            grains[j].panL = cosf(pan * (float)PIOVERTWO);
            grains[j].panR = sinf(pan * (float)PIOVERTWO);
            grains[j].amplitude = amplitude;
            grains[j].esi = (float)eframes / (float)grains[j].duration;
            grains[j].si = frequency * (float)frames / x->sr;
            return;
        }
    }
}

void granule_grist(t_granule *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 10) {
        object_error((t_object *)x, "%s: grist requires 10 arguments: events horizon minfreq maxfreq minpan maxpan minamp maxamp mindur maxdur", OBJECT_NAME);
        return;
    }

    x->events = (long)atom_getfloat(argv);
    x->horizon_ms = atom_getfloat(argv + 1);
    x->minfreq = atom_getfloat(argv + 2);
    x->maxfreq = atom_getfloat(argv + 3);
    x->minpan = atom_getfloat(argv + 4);
    x->maxpan = atom_getfloat(argv + 5);
    x->minamp = atom_getfloat(argv + 6);
    x->maxamp = atom_getfloat(argv + 7);
    x->mindur_ms = atom_getfloat(argv + 8);
    x->maxdur_ms = atom_getfloat(argv + 9);

    x->mindur = 0.001f * x->sr * x->mindur_ms;
    x->maxdur = 0.001f * x->sr * x->maxdur_ms;
    x->horizon = (long)(0.001f * x->sr * x->horizon_ms);

    if (x->minfreq < 0.0f) x->minfreq = fabsf(x->minfreq);
    if (x->maxfreq < 0.0f) x->maxfreq = fabsf(x->maxfreq);
    if (x->minpan < 0.0f) x->minpan = 0.0f;
    if (x->maxpan > 1.0f) x->maxpan = 1.0f;
    if (x->events < 0) x->events = 0;
}

void *granule_new(t_symbol *msg, long argc, t_atom *argv)
{
    t_granule *x = (t_granule *)object_alloc(granule_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 0); // No signal inputs
    outlet_new((t_object *)x, "signal"); // Left signal out
    outlet_new((t_object *)x, "signal"); // Right signal out

    x->pitchscale = (float *)sysmem_newptrclear(MAXSCALE * sizeof(float));
    x->grains = (t_grain *)sysmem_newptrclear(MAXGRAINS * sizeof(t_grain));

    x->wavename = gensym("waveform");
    x->windowname = gensym("window");

    if (argc > 0) x->wavename = atom_getsymarg(0, (short)argc, argv);
    if (argc > 1) x->windowname = atom_getsymarg(1, (short)argc, argv);

    x->wavebuf_ref = buffer_ref_new((t_object *)x, x->wavename);
    x->windowbuf_ref = buffer_ref_new((t_object *)x, x->windowname);

    x->sr = sys_getsr();
    if (x->sr <= 0.0f) x->sr = 44100.0f;

    granule_init(x, 0);
    return x;
}

void granule_init(t_granule *x, short initialized)
{
    if (!initialized) {
        x->pitchsteps = 0;
        x->mute = 0;
        x->steady = 0;
        x->events = 10;
        x->horizon_ms = 1000.0f;
        x->minfreq = 220.0f;
        x->maxfreq = 880.0f;
        x->minpan = 0.1f;
        x->maxpan = 0.9f;
        x->minamp = 0.1f;
        x->maxamp = 1.0f;
        x->mindur_ms = 150.0f;
        x->maxdur_ms = 750.0f;
        x->transpose = 1.0f;
        x->pitch_deviation = 0.0f;
        x->lowblock_freq = 0.0f;
        x->highblock_freq = 0.0f;
        x->constrain_scale = 0;
    }
    x->horizon = (long)(x->horizon_ms * 0.001f * x->sr);
    x->mindur = x->mindur_ms * 0.001f * x->sr;
    x->maxdur = x->maxdur_ms * 0.001f * x->sr;

    if (x->grains) {
        for (long i = 0; i < MAXGRAINS; i++) {
            x->grains[i].ephase = 9999999999.0f;
        }
    }
}

void granule_dsp_free(t_granule *x)
{
    // 1. Detach from DSP chain first to stop perform64
    dsp_free((t_pxobject *)x);

    // 2. Free internal dynamically allocated memory
    if (x->grains) {
        sysmem_freeptr(x->grains);
        x->grains = NULL;
    }
    if (x->pitchscale) {
        sysmem_freeptr(x->pitchscale);
        x->pitchscale = NULL;
    }

    // 3. Free buffer reference handles
    if (x->wavebuf_ref) {
        object_free(x->wavebuf_ref);
        x->wavebuf_ref = NULL;
    }
    if (x->windowbuf_ref) {
        object_free(x->windowbuf_ref);
        x->windowbuf_ref = NULL;
    }
}

void granule_setbuf(t_granule *x, t_symbol *wavename, t_symbol *windowname)
{
    if (!x->wavebuf_ref) x->wavebuf_ref = buffer_ref_new((t_object *)x, wavename);
    else buffer_ref_set(x->wavebuf_ref, wavename);

    if (!x->windowbuf_ref) x->windowbuf_ref = buffer_ref_new((t_object *)x, windowname);
    else buffer_ref_set(x->windowbuf_ref, windowname);

    x->hosed = 0;

    t_buffer_obj *b_wave = buffer_ref_getobject(x->wavebuf_ref);
    if (!b_wave) {
        object_error((t_object *)x, "%s: no such buffer~ %s", OBJECT_NAME, wavename->s_name);
        x->hosed = 1;
    } else if (buffer_getchannelcount(b_wave) != 1) {
        object_error((t_object *)x, "%s: wavetable buffer %s must be mono", OBJECT_NAME, wavename->s_name);
        x->hosed = 1;
    }

    t_buffer_obj *b_win = buffer_ref_getobject(x->windowbuf_ref);
    if (!b_win) {
        object_error((t_object *)x, "%s: no such buffer~ %s", OBJECT_NAME, windowname->s_name);
        x->hosed = 1;
    } else if (buffer_getchannelcount(b_win) != 1) {
        object_error((t_object *)x, "%s: window buffer %s must be mono", OBJECT_NAME, windowname->s_name);
        x->hosed = 1;
    }

    x->wavename = wavename;
    x->windowname = windowname;
}

void granule_reload(t_granule *x)
{
    granule_setbuf(x, x->wavename, x->windowname);
}

void granule_dblclick(t_granule *x)
{
    granule_reload(x);
    if (x->wavebuf_ref) buffer_view(buffer_ref_getobject(x->wavebuf_ref));
    if (x->windowbuf_ref) buffer_view(buffer_ref_getobject(x->windowbuf_ref));
}

void granule_info(t_granule *x)
{
    t_buffer_obj *windowbuf = buffer_ref_getobject(x->windowbuf_ref);
    if (!windowbuf) {
        object_error((t_object *)x, "%s: invalid window buffer", OBJECT_NAME);
        return;
    }
    long eframes = buffer_getframecount(windowbuf);
    int tcount = 0;
    for (int i = 0; i < MAXGRAINS; i++) {
        if (x->grains[i].ephase < (float)eframes) ++tcount;
    }
    post("%s: %d active grains | wave: %s | window: %s", OBJECT_NAME, tcount, x->wavename->s_name, x->windowname->s_name);
}

t_max_err granule_notify(t_granule *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (x->wavebuf_ref) buffer_ref_notify(x->wavebuf_ref, s, msg, sender, data);
    if (x->windowbuf_ref) return buffer_ref_notify(x->windowbuf_ref, s, msg, sender, data);
    return MAX_ERR_NONE;
}

void granule_perform64(t_granule *x, t_object *dsp64, double **ins, long numins, 
                       double **outs, long numouts, long n, long flags, void *userparam)
{
    t_double *outputL = outs[0];
    t_double *outputR = outs[1];

    // Clean outputs first
    for (long i = 0; i < n; i++) {
        outputL[i] = 0.0;
        outputR[i] = 0.0;
    }

    if (x->mute || x->hosed) return;

    t_buffer_obj *wavebuf = buffer_ref_getobject(x->wavebuf_ref);
    t_buffer_obj *windowbuf = buffer_ref_getobject(x->windowbuf_ref);
    if (!wavebuf || !windowbuf) return;

    long frames = buffer_getframecount(wavebuf);
    long eframes = buffer_getframecount(windowbuf);
    if (frames <= 0 || eframes <= 0) return;

    float *wavetable = buffer_locksamples(wavebuf);
    float *window = buffer_locksamples(windowbuf);

    if (!wavetable || !window) {
        if (wavetable) buffer_unlocksamples(wavebuf);
        if (window) buffer_unlocksamples(windowbuf);
        return;
    }

    t_grain *grains = x->grains;
    for (long j = 0; j < MAXGRAINS; j++) {
        if (grains[j].ephase >= (float)eframes) continue;

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
                long idx_p = (long)phase;
                if (idx_p >= frames) idx_p = frames - 1;
                else if (idx_p < 0) idx_p = 0;

                long idx_e = (long)ephase;
                if (idx_e >= eframes) idx_e = eframes - 1;
                else if (idx_e < 0) idx_e = 0;

                double sample = wavetable[idx_p] * (amplitude * window[idx_e]);
                outputL[i] += panL * sample;
                outputR[i] += panR * sample;

                phase += si;
                ephase += esi;

                while (phase >= (float)frames) phase -= (float)frames;
                while (phase < 0.0f) phase += (float)frames;

                if (ephase >= (float)eframes) {
                    break;
                }
            }
        }
        grains[j].phase = phase;
        grains[j].ephase = ephase;
        grains[j].delay = delay;
    }

    buffer_unlocksamples(wavebuf);
    buffer_unlocksamples(windowbuf);
}

void granule_dsp64(t_granule *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;

    granule_reload(x);
    if (x->sr != (float)samplerate) {
        x->sr = (float)samplerate;
        granule_init(x, 1);
    }
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)granule_perform64, 0, NULL);
}

void granule_assist(t_granule *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(messages) Control Input", 256);
    } else if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Output 1", 256); break;
            case 1: strncpy_zero(dst, "(signal) Output 2", 256); break;
        }
    }
}
