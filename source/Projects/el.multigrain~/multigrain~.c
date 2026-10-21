/**
 * @file multigrain~.c
 * @brief Multi-channel granular synthesizer for Max 8/9.
 * Modernized for 64-bit dsp64, stereo waveform buffer support, and user-specified maxskip control.
 */

#include "MSPd.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define MAXGRAINS (512)
#define MAXSCALE (8192)
#define OBJECT_NAME "multigrain~"

static t_class *multigrain_class;

typedef struct {
    double amplitude;
    long delay;
    long duration;
    double phase;
    double ephase;
    double si;
    double esi;
    double endframe;
    short active;
    long output_channel;
} t_grain;

typedef struct _multigrain {
    t_pxobject x_obj;
    t_buffer_ref *wavebuf;
    t_buffer_ref *windowbuf;
    t_symbol *wavename;
    t_symbol *windowname;
    
    float sr;
    short mute;
    short hosed;
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
    short mixmode; // 0=Summed Mono, 1=Discrete Stereo, 2=Stereo Spread
    long minskip;
    long maxskip;
    float minskip_ms;
    float maxskip_ms; // User-controllable maxskip in ms (-1 = full buffer)
    long b_nchans;
    long b_frames;
    float retro_odds;
    short interpolate;
    short interpolate_envelope;
    long output_channels;
    long minchanr;
    long maxchanr;
    long groupflag;
    long groupsize;
    long *changroup;
    long changroup_length;
} t_multigrain;

/* Method Prototypes */
void *multigrain_new(t_symbol *msg, long argc, t_atom *argv);
void multigrain_dsp_free(t_multigrain *x);
void multigrain_assist(t_multigrain *x, void *b, long msg, long arg, char *dst);
void multigrain_setbuf(t_multigrain *x, t_symbol *wavename, t_symbol *windowname);
void multigrain_reload(t_multigrain *x);
void multigrain_dblclick(t_multigrain *x);
void multigrain_spray(t_multigrain *x);
void multigrain_pitchspray(t_multigrain *x);
void multigrain_transpose(t_multigrain *x, double t);
void multigrain_pitchdev(t_multigrain *x, double d);
void multigrain_lowblock(t_multigrain *x, double f);
void multigrain_highblock(t_multigrain *x, double f);
void multigrain_events(t_multigrain *x, double e);
float multigrain_boundrand(float min, float max);
void *multigrain_grist(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv);
void *multigrain_grain(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv);
void *multigrain_setscale(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv);
void *multigrain_chanrange(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv);
void *multigrain_setgroup(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv);
void multigrain_info(t_multigrain *x);
void multigrain_mute(t_multigrain *x, double toggle);
void multigrain_steady(t_multigrain *x, double toggle);
void multigrain_constrain_scale(t_multigrain *x, double toggle);
void multigrain_interpolate(t_multigrain *x, double toggle);
void multigrain_nopan(t_multigrain *x, double toggle);
void multigrain_retro_odds(t_multigrain *x, double o);
void multigrain_seed(t_multigrain *x, double seed);
void multigrain_interpolate_envelope(t_multigrain *x, double toggle);
void multigrain_mixmode(t_multigrain *x, double mode);
void multigrain_maxskip(t_multigrain *x, double ms);
void multigrain_minskip(t_multigrain *x, double ms);
void multigrain_init(t_multigrain *x, short initialized);
void multigrain_constrain(int *index_min, int *index_max, float min_incr, float max_incr, float *scale, int steps);
t_max_err multigrain_notify(t_multigrain *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void multigrain_dsp64(t_multigrain *x, t_object *dsp64, short *count, double sr, long n, long flags);
void multigrain_perform64(t_multigrain *x, t_object *dsp64, double **ins,
                          long numins, double **outs, long numouts, long n,
                          long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.multigrain~", 
                           (method)multigrain_new, 
                           (method)multigrain_dsp_free, 
                           (long)sizeof(t_multigrain), 
                           0L, 
                           A_GIMME, 
                           0);
    
    class_addmethod(c, (method)multigrain_dsp64,           "dsp64",               A_CANT, 0);
    class_addmethod(c, (method)multigrain_assist,          "assist",              A_CANT, 0);
    class_addmethod(c, (method)multigrain_dblclick,        "dblclick",            A_CANT, 0);
    class_addmethod(c, (method)multigrain_setbuf,          "setbuf",              A_SYM, A_SYM, 0);
    class_addmethod(c, (method)multigrain_spray,           "spray",               0);
    class_addmethod(c, (method)multigrain_pitchspray,      "pitchspray",          0);
    class_addmethod(c, (method)multigrain_transpose,       "transpose",           A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_events,          "events",              A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_pitchdev,        "pitchdev",            A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_lowblock,        "lowblock",            A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_highblock,       "highblock",           A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_interpolate,     "interpolate",         A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_retro_odds,      "retro_odds",          A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_nopan,           "nopan",               A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_seed,            "seed",                A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_mute,            "mute",                A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_steady,          "steady",              A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_interpolate_envelope, "interpolate_envelope", A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_constrain_scale, "constrain_scale",     A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_grist,           "grist",               A_GIMME, 0);
    class_addmethod(c, (method)multigrain_grain,           "grain",               A_GIMME, 0);
    class_addmethod(c, (method)multigrain_setscale,        "setscale",            A_GIMME, 0);
    class_addmethod(c, (method)multigrain_chanrange,       "chanrange",           A_GIMME, 0);
    class_addmethod(c, (method)multigrain_setgroup,        "setgroup",            A_GIMME, 0);
    class_addmethod(c, (method)multigrain_info,            "info",                0);
    class_addmethod(c, (method)multigrain_notify,          "notify",              A_CANT,  0);
    class_addmethod(c, (method)multigrain_mixmode,         "mixmode",             A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_maxskip,         "maxskip",             A_FLOAT, 0);
    class_addmethod(c, (method)multigrain_minskip,         "minskip",             A_FLOAT, 0);

    CLASS_ATTR_LONG(c, "mixmode", 0, t_multigrain, mixmode);
    CLASS_ATTR_DEFAULT_SAVE(c, "mixmode", 0, "0");
    CLASS_ATTR_LABEL(c, "mixmode", 0, "Mixing Mode (0=Mono, 1=Discrete, 2=Spread)");
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    multigrain_class = c;
    
    potpourri_announce(OBJECT_NAME);
    return 0;
}

void multigrain_mixmode(t_multigrain *x, double mode) 
{ 
    short m = (short)mode;
    if (m < 0) m = 0;
    if (m > 2) m = 2;
    x->mixmode = m; 
}


void multigrain_maxskip(t_multigrain *x, double ms)
{
    x->maxskip_ms = (float)ms;
    t_buffer_obj *b = x->wavebuf ? buffer_ref_getobject(x->wavebuf) : NULL;
    if (b) {
        x->b_frames = buffer_getframecount(b);
        if (ms < 0.0 || x->b_frames <= 0) {
            x->maxskip = (x->b_frames > 0) ? x->b_frames - 1 : 0;
        } else {
            long s_frames = (long)(x->maxskip_ms * 0.001f * x->sr);
            x->maxskip = (s_frames < x->b_frames - 1) ? s_frames : (x->b_frames - 1);
        }
    } else {
        x->maxskip = -1;
    }
}

void multigrain_minskip(t_multigrain *x, double ms)
{
    if (ms < 0.0) ms = 0.0;
    x->minskip_ms = (float)ms;
    if (x->b_frames > 0 && x->sr > 0.0f) {
        long s_frames = (long)(x->minskip_ms * 0.001f * x->sr);
        x->minskip = (s_frames < x->b_frames - 1) ? s_frames : 0;
    }
}

void multigrain_interpolate_envelope(t_multigrain *x, double toggle) { x->interpolate_envelope = (short)toggle; }
void multigrain_seed(t_multigrain *x, double seed)                  { srand((unsigned int)seed); }
void multigrain_interpolate(t_multigrain *x, double toggle)          { x->interpolate = (short)toggle; }
void multigrain_nopan(t_multigrain *x, double toggle)                { x->nopan = (short)toggle; }
void multigrain_constrain_scale(t_multigrain *x, double toggle)      { x->constrain_scale = (short)toggle; }
void multigrain_lowblock(t_multigrain *x, double f)                  { if (f > 0.0) x->lowblock_increment = (float)f; }
void multigrain_highblock(t_multigrain *x, double f)                 { if (f > 0.0) x->highblock_increment = (float)f; }
void multigrain_mute(t_multigrain *x, double toggle)                 { x->mute = (short)toggle; }
void multigrain_steady(t_multigrain *x, double toggle)               { x->steady = (short)toggle; }

void multigrain_retro_odds(t_multigrain *x, double o)
{
    if (o < 0.0 || o > 1.0) return;
    x->retro_odds = (float)o;
}

void multigrain_pitchdev(t_multigrain *x, double d)
{
    if (d < 0.0) return;
    x->pitch_deviation = (float)d;
}

void multigrain_events(t_multigrain *x, double e)
{
    if (e <= 0.0) return;
    x->events = (long)e;
}

void multigrain_transpose(t_multigrain *x, double t)
{
    if (t <= 0.0) return;
    x->transpose = (float)t;
}

void *multigrain_setscale(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc >= MAXSCALE || argc < 2) return 0;
    for (short i = 0; i < argc; i++) x->pitchscale[i] = atom_getfloatarg(i, argc, argv);
    x->pitchsteps = (int)argc;
    return 0;
}

void *multigrain_chanrange(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 2) return 0;
    long minchanr = (long)atom_getfloatarg(0, argc, argv);
    long maxchanr = (long)atom_getfloatarg(1, argc, argv);
    x->minchanr = (minchanr < 0) ? 0 : minchanr;
    x->maxchanr = (maxchanr >= x->output_channels) ? x->output_channels - 1 : maxchanr;
    x->groupflag = 0;
    return 0;
}

void *multigrain_setgroup(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc > x->output_channels) return 0;
    for (int i = 0; i < argc; i++) {
        long chan = (long)atom_getfloatarg(i, argc, argv);
        if (chan < 0) chan = 0;
        if (chan >= x->output_channels) chan = x->output_channels - 1;
        x->changroup[i] = chan;
    }
    x->changroup_length = argc;
    x->groupflag = (argc > 0) ? 1 : 0;
    return 0;
}

void multigrain_constrain(int *index_min, int *index_max, float min_incr, float max_incr, float *scale, int steps)
{
    int imax = steps - 1;
    int imin = 0;
    while (scale[imin] < min_incr && imin < imax) ++imin;
    while (scale[imax] > max_incr && imax > 0) --imax;
    *index_min = imin;
    *index_max = (imax < imin) ? imin : imax;
}

void multigrain_pitchspray(t_multigrain *x)
{
    if (!sys_getdspstate() || x->hosed) return;
    t_buffer_obj *wbuf = buffer_ref_getobject(x->wavebuf);
    t_buffer_obj *winbuf = buffer_ref_getobject(x->windowbuf);
    if (!wbuf || !winbuf) return;

    long b_frames = buffer_getframecount(wbuf);
    long eframes = buffer_getframecount(winbuf);
    if (b_frames <= 0 || eframes <= 0 || x->sr <= 0.0f) return;

    float pdev = 0.0f, pdev_invert = 0.0f;
    if (x->pitch_deviation > 0.0f) {
        pdev = 1.0f + x->pitch_deviation;
        pdev_invert = 1.0f / pdev;
    }

    long minskip = x->minskip;
    long maxskip = x->maxskip;
    if (maxskip < 0 || maxskip >= b_frames) maxskip = b_frames - 1;
    if (minskip < 0) minskip = 0;
    if (minskip >= b_frames) minskip = 0;

    t_grain *grains = x->grains;

    for (int i = 0; i < x->events; i++) {
        for (int j = 0; j < MAXGRAINS; j++) {
            if (!grains[j].active) {
                grains[j].delay = x->steady ? (long)((float)(i * x->horizon) / (float)x->events) : (long)multigrain_boundrand(0.0f, (float)x->horizon);
                grains[j].duration = (long)multigrain_boundrand(x->mindur, x->maxdur);
                if (grains[j].duration <= 0) grains[j].duration = 1;

                grains[j].phase = 0.0;
                grains[j].ephase = 0.0;
                grains[j].amplitude = (double)(multigrain_boundrand(x->minamp, x->maxamp) * 0.707f);
                
                if (x->groupflag && x->changroup_length > 0) grains[j].output_channel = x->changroup[rand() % x->changroup_length];
                else grains[j].output_channel = x->minchanr + (rand() % (x->maxchanr - x->minchanr + 1));
                
                int index_min = 0, index_max = x->pitchsteps - 1;
                // if (x->constrain_scale && x->pitchsteps >= 2) {
                if (x->pitchsteps >= 2) {
                    multigrain_constrain(&index_min, &index_max, x->min_incr, x->max_incr, x->pitchscale, x->pitchsteps);
                    int windex = (int)multigrain_boundrand((float)index_min, (float)index_max);
                    grains[j].si = (double)(x->transpose * x->pitchscale[windex]);
                } else {
                    grains[j].si = (double)(x->transpose * multigrain_boundrand(x->min_incr, x->max_incr));
                }
                
                if (x->pitch_deviation > 0.0f) grains[j].si *= (double)multigrain_boundrand(pdev_invert, pdev);
                
                long grainframes = (long)(grains[j].duration * fabs(grains[j].si));
                grains[j].esi = (double)eframes / (double)grains[j].duration;

                if (grainframes >= b_frames) { grains[j].active = 0; goto nextgrain; }
                long upper = (maxskip > b_frames - grainframes) ? (b_frames - grainframes) : maxskip;
                if (upper < minskip) upper = minskip;
                grains[j].phase = (double)multigrain_boundrand((float)minskip, (float)upper);
                grains[j].endframe = grains[j].phase + (double)(grainframes - 1);

                if (multigrain_boundrand(0.0f, 1.0f) < x->retro_odds) {
                    grains[j].si *= -1.0;
                    double tmp = grains[j].phase; grains[j].phase = grains[j].endframe; grains[j].endframe = tmp;
                }
                grains[j].active = 1;
                goto nextgrain;
            }
        }
    nextgrain:;
    }
}

void multigrain_spray(t_multigrain *x)
{
    if (!sys_getdspstate() || x->hosed) return;
    t_buffer_obj *wbuf = buffer_ref_getobject(x->wavebuf);
    t_buffer_obj *winbuf = buffer_ref_getobject(x->windowbuf);
    if (!wbuf || !winbuf) return;

    long b_frames = buffer_getframecount(wbuf);
    long eframes = buffer_getframecount(winbuf);
    if (b_frames <= 0 || eframes <= 0 || x->sr <= 0.0f) return;

    long minskip = x->minskip;
    long maxskip = x->maxskip;
    if (maxskip < 0 || maxskip >= b_frames) maxskip = b_frames - 1;
    if (minskip < 0) minskip = 0;
    if (minskip >= b_frames) minskip = 0;

    t_grain *grains = x->grains;
    for (int i = 0; i < x->events; i++) {
        for (int j = 0; j < MAXGRAINS; j++) {
            if (!grains[j].active) {
                grains[j].active = 1;
                grains[j].delay = x->steady ? (long)((float)(i * x->horizon) / (float)x->events) : (long)multigrain_boundrand(0.0f, (float)x->horizon);
                grains[j].duration = (long)multigrain_boundrand(x->mindur, x->maxdur);
                if (grains[j].duration <= 0) grains[j].duration = 1;
                grains[j].ephase = 0.0;
                grains[j].amplitude = (double)(multigrain_boundrand(x->minamp, x->maxamp) * 0.707f);
                grains[j].si = (double)(x->transpose * multigrain_boundrand(x->min_incr, x->max_incr));
                
                if (x->groupflag && x->changroup_length > 0) grains[j].output_channel = x->changroup[rand() % x->changroup_length];
                else grains[j].output_channel = x->minchanr + (rand() % (x->maxchanr - x->minchanr + 1));
                
                long grainframes = (long)(grains[j].duration * fabs(grains[j].si));
                grains[j].esi = (double)eframes / (double)grains[j].duration;

                if (grainframes >= b_frames) { grains[j].active = 0; goto nextgrain; }
                long upper = (maxskip > b_frames - grainframes) ? (b_frames - grainframes) : maxskip;
                if (upper < minskip) upper = minskip;
                grains[j].phase = (double)multigrain_boundrand((float)minskip, (float)upper);
                grains[j].endframe = grains[j].phase + (double)(grainframes - 1);

                if (multigrain_boundrand(0.0f, 1.0f) < x->retro_odds) {
                    grains[j].si *= -1.0;
                    double tmp = grains[j].phase; grains[j].phase = grains[j].endframe; grains[j].endframe = tmp;
                }
                goto nextgrain;
            }
        }
    nextgrain:;
    }
}

void *multigrain_grain(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (!sys_getdspstate() || argc < 5) return 0;

    t_buffer_obj *wbuf = buffer_ref_getobject(x->wavebuf);
    t_buffer_obj *winbuf = buffer_ref_getobject(x->windowbuf);
    if (!wbuf || !winbuf) return 0;

    long frames = buffer_getframecount(wbuf);
    long eframes = buffer_getframecount(winbuf);

    float duration = atom_getfloatarg(0, argc, argv);
    float incr = atom_getfloatarg(1, argc, argv);
    float amplitude = atom_getfloatarg(2, argc, argv);
    float pan = atom_getfloatarg(3, argc, argv);
    float skip = atom_getfloatarg(4, argc, argv) * 0.001f * x->sr;

    if (duration <= 0.0f || incr == 0.0f || skip < 0.0f || skip >= (float)frames) return 0;

    for (int j = 0; j < MAXGRAINS; j++) {
        if (!x->grains[j].active) {
            x->grains[j].delay = 0;
            x->grains[j].duration = (long)(0.001f * x->sr * duration);
            if (x->grains[j].duration <= 0) x->grains[j].duration = 1;
            x->grains[j].phase = (double)skip;
            x->grains[j].ephase = 0.0;
            x->grains[j].amplitude = (double)(amplitude * 0.707f);
            x->grains[j].esi = (double)eframes / (double)x->grains[j].duration;
            x->grains[j].si = (double)incr;
            x->grains[j].output_channel = (long)(pan * (float)(x->output_channels - 1));
            x->grains[j].active = 1;
            return 0;
        }
    }
    return 0;
}

float multigrain_boundrand(float min, float max)
{
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}

void *multigrain_new(t_symbol *msg, long argc, t_atom *argv)
{
    float f = 2.0f;
    t_multigrain *x = (t_multigrain *)object_alloc(multigrain_class);
    if (!x) return NULL;

    if (argc < 2) {
        object_error((t_object *)x, "%s: must enter wave buffer, window buffer, and output channel count", OBJECT_NAME);
        return x;
    }

    if (argc > 0) atom_arg_getsym(&x->wavename, 0, (short)argc, argv);
    if (argc > 1) atom_arg_getsym(&x->windowname, 1, (short)argc, argv);
    if (argc > 2) atom_arg_getfloat(&f, 2, (short)argc, argv);

    x->output_channels = (long)f;
    if (x->output_channels < 1) x->output_channels = 2;

    dsp_setup((t_pxobject *)x, 0); // Control-rate messages only
    x->x_obj.z_misc |= Z_NO_INPLACE;

    for (int i = 0; i < x->output_channels; i++) {
        outlet_new((t_object *)x, "signal");
    }

    x->pitchscale = (float *)sysmem_newptrclear(MAXSCALE * sizeof(float));
    x->grains = (t_grain *)sysmem_newptrclear(MAXGRAINS * sizeof(t_grain));

    x->wavebuf = buffer_ref_new((t_object *)x, x->wavename);
    x->windowbuf = buffer_ref_new((t_object *)x, x->windowname);

    x->sr = (float)sys_getsr();
    if (x->sr <= 0.0f) x->sr = 44100.0f;

    multigrain_init(x, 0);
    return x;
}

void multigrain_init(t_multigrain *x, short initialized)
{
    if (!initialized) {
        x->pitchsteps = 0;
        x->mute = 0;
        x->steady = 0;
        x->events = 10;
        x->horizon_ms = 1000.0f;
        x->min_incr = 0.5f;
        x->max_incr = 2.0f;
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
        x->minskip_ms = 0.0f;
        x->maxskip_ms = -1.0f; // -1 indicates full buffer size
        x->minskip = 0;
        x->maxskip = -1;
        x->nopan = 0;
        x->mixmode = 0; // default summed mono
        x->interpolate = 1;
        x->interpolate_envelope = 0;
        x->groupflag = 0;
        x->changroup = (long *)sysmem_newptrclear(x->output_channels * sizeof(long));
        x->minchanr = 0;
        x->maxchanr = x->output_channels - 1;
    }
    x->horizon = (long)(x->horizon_ms * 0.001f * x->sr);
    x->mindur = x->mindur_ms * 0.001f * x->sr;
    x->maxdur = x->maxdur_ms * 0.001f * x->sr;

    if (x->grains) {
        for (int i = 0; i < MAXGRAINS; i++) {
            x->grains[i].active = 0;
        }
    }
}

void multigrain_info(t_multigrain *x)
{
    t_buffer_obj *buffer = x->wavebuf ? buffer_ref_getobject(x->wavebuf) : NULL;
    int tcount = 0;
    for (int i = 0; i < MAXGRAINS; i++) {
        if (x->grains[i].active) ++tcount;
    }
    post("%s: %d active grains | wave: %s | win: %s | maxskip: %.2f ms", 
         OBJECT_NAME, tcount, x->wavename->s_name, x->windowname->s_name, x->maxskip_ms);
}

void multigrain_dblclick(t_multigrain *x)
{
    multigrain_reload(x);
    if (x->wavebuf) buffer_view(buffer_ref_getobject(x->wavebuf));
    if (x->windowbuf) buffer_view(buffer_ref_getobject(x->windowbuf));
}

void *multigrain_grist(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 10) {
        object_error((t_object *)x, "%s: grist requires 10 arguments", OBJECT_NAME);
        return 0;
    }
    x->events = (long)atom_getfloatarg(0, argc, argv);
    x->horizon_ms = atom_getfloatarg(1, argc, argv);
    x->min_incr = atom_getfloatarg(2, argc, argv);
    x->max_incr = atom_getfloatarg(3, argc, argv);
    x->minamp = atom_getfloatarg(6, argc, argv);
    x->maxamp = atom_getfloatarg(7, argc, argv);
    x->mindur_ms = atom_getfloatarg(8, argc, argv);
    x->maxdur_ms = atom_getfloatarg(9, argc, argv);
    
    x->mindur = 0.001f * x->sr * x->mindur_ms;
    x->maxdur = 0.001f * x->sr * x->maxdur_ms;
    x->horizon = (long)(0.001f * x->sr * x->horizon_ms);
    
    if (x->min_incr < 0.0f) x->min_incr = fabsf(x->min_incr);
    if (x->max_incr < 0.0f) x->max_incr = fabsf(x->max_incr);
    if (x->events < 0) x->events = 0;
    return 0;
}

void multigrain_reload(t_multigrain *x)
{
    multigrain_setbuf(x, x->wavename, x->windowname);
}

void multigrain_setbuf(t_multigrain *x, t_symbol *wavename, t_symbol *windowname)
{
    if (!x->wavebuf) x->wavebuf = buffer_ref_new((t_object *)x, wavename);
    else buffer_ref_set(x->wavebuf, wavename);

    if (!x->windowbuf) x->windowbuf = buffer_ref_new((t_object *)x, windowname);
    else buffer_ref_set(x->windowbuf, windowname);

    x->hosed = 0;
    for (int i = 0; i < MAXGRAINS; i++) x->grains[i].active = 0;

    t_buffer_obj *b = buffer_ref_getobject(x->wavebuf);
    if (!b) {
        object_error((t_object *)x, "%s: invalid sound buffer %s", OBJECT_NAME, wavename->s_name);
        x->hosed = 1;
        return;
    }
    x->b_frames = buffer_getframecount(b);
    x->b_nchans = buffer_getchannelcount(b);
    if (x->b_nchans > 2 || x->b_nchans < 1) {
        object_error((t_object *)x, "%s: wavetable must be mono or stereo", OBJECT_NAME);
        x->hosed = 1;
    }

    b = buffer_ref_getobject(x->windowbuf);
    if (!b) {
        object_error((t_object *)x, "%s: invalid window buffer %s", OBJECT_NAME, windowname->s_name);
        x->hosed = 1;
        return;
    }
    if (buffer_getchannelcount(b) != 1) {
        object_error((t_object *)x, "%s: window must be a mono buffer", OBJECT_NAME);
        x->hosed = 1;
    }
    x->wavename = wavename;
    x->windowname = windowname;

    // Apply maxskip limit bounded by buffer size
    if (x->b_frames > 0) {
        if (x->maxskip_ms >= 0.0f) {
            long s_frames = (long)(x->maxskip_ms * 0.001f * x->sr);
            x->maxskip = (s_frames < x->b_frames - 1) ? s_frames : (x->b_frames - 1);
        } else {
            x->maxskip = x->b_frames - 1;
        }
        if (x->minskip_ms > 0.0f) {
            long s_frames = (long)(x->minskip_ms * 0.001f * x->sr);
            x->minskip = (s_frames < x->maxskip) ? s_frames : 0;
        } else {
            x->minskip = 0;
        }
    }
}

t_max_err multigrain_notify(t_multigrain *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (x->windowbuf) buffer_ref_notify(x->windowbuf, s, msg, sender, data);
    if (x->wavebuf)   return buffer_ref_notify(x->wavebuf, s, msg, sender, data);
    return MAX_ERR_NONE;
}

void multigrain_perform64(t_multigrain *x, t_object *dsp64, double **ins,
                          long numins, double **outs, long numouts, long n,
                          long flags, void *userparam)
{
    if (x->mute || x->hosed) {
        for (long i = 0; i < numouts; i++) memset(outs[i], 0, n * sizeof(t_double));
        return;
    }

    for (long i = 0; i < numouts; i++) memset(outs[i], 0, n * sizeof(t_double));

    t_buffer_obj *wavebuf_b = x->wavebuf ? buffer_ref_getobject(x->wavebuf) : NULL;
    t_buffer_obj *windowbuf_b = x->windowbuf ? buffer_ref_getobject(x->windowbuf) : NULL;
    if (!wavebuf_b || !windowbuf_b) return;

    long b_frames = buffer_getframecount(wavebuf_b);
    long eframes = buffer_getframecount(windowbuf_b);
    long b_nchans = buffer_getchannelcount(wavebuf_b);
    if (b_nchans < 1 || b_nchans > 2 || eframes <= 0 || b_frames <= 0) return;

    float *wavetable = buffer_locksamples(wavebuf_b);
    float *window = buffer_locksamples(windowbuf_b);
    if (!wavetable || !window) {
        if (wavetable) buffer_unlocksamples(wavebuf_b);
        if (window) buffer_unlocksamples(windowbuf_b);
        return;
    }

    t_grain *grains = x->grains;
    short interp_env = x->interpolate_envelope;
    long num_out_chans = x->output_channels;

    for (int j = 0; j < MAXGRAINS; j++) {
        if (!grains[j].active) continue;

        double amplitude = grains[j].amplitude;
        double si = grains[j].si;
        double esi = grains[j].esi;
        double phase = grains[j].phase;
        double ephase = grains[j].ephase;
        long delay = grains[j].delay;
        long out_chan = grains[j].output_channel;

        if (out_chan < 0 || out_chan >= num_out_chans) continue;
        t_double *channel_out = outs[out_chan];

        for (int i = 0; i < n; i++) {
            if (delay > 0) {
                --delay;
            } else if (ephase < (double)eframes) {
                float envelope;
                if (interp_env) {
                    long c_idx = (long)ephase;
                    double frac = ephase - (double)c_idx;
                    envelope = (c_idx <= 0) ? window[0] : ((c_idx >= eframes - 1) ? window[eframes - 1] : window[c_idx] + (float)frac * (window[c_idx + 1] - window[c_idx]));
                } else {
                    long idx_e = (long)ephase;
                    if (idx_e < 0) idx_e = 0; else if (idx_e >= eframes) idx_e = eframes - 1;
                    envelope = window[idx_e];
                }

                long c_idx = (long)phase;
                if (c_idx < 0 || c_idx >= b_frames) { grains[j].active = 0; break; }

                double frac = phase - (double)c_idx;
                float sampleL, sampleR;

                if (b_nchans == 1) {
                    sampleL = (c_idx >= b_frames - 1 || frac == 0.0) ? wavetable[c_idx] : (wavetable[c_idx] + (float)frac * (wavetable[c_idx + 1] - wavetable[c_idx]));
                    sampleL *= (float)(amplitude * envelope);
                    channel_out[i] += (t_double)sampleL;
                } else {
                    long s_idx = c_idx * 2;
                    sampleL = (c_idx >= b_frames - 1 || frac == 0.0) ? wavetable[s_idx] : (wavetable[s_idx] + (float)frac * (wavetable[s_idx + 2] - wavetable[s_idx]));
                    sampleR = (c_idx >= b_frames - 1 || frac == 0.0) ? wavetable[s_idx + 1] : (wavetable[s_idx + 1] + (float)frac * (wavetable[s_idx + 3] - wavetable[s_idx + 1]));
                    
                    sampleL *= (float)(amplitude * envelope);
                    sampleR *= (float)(amplitude * envelope);

                    if (x->mixmode == 0) {
                        channel_out[i] += (t_double)((sampleL + sampleR) * 0.707f);
                    } else if (x->mixmode == 1) {
                        channel_out[i] += (t_double)sampleL;
                        if (out_chan + 1 < num_out_chans) outs[out_chan + 1][i] += (t_double)sampleR;
                    } else {
                        channel_out[i] += (t_double)sampleL;
                        long r_chan = (out_chan + 2 < num_out_chans) ? (out_chan + 2) : 0;
                        outs[r_chan][i] += (t_double)sampleR;
                    }
                }
                phase += si; ephase += esi;
                if (ephase >= (double)eframes || phase < 0.0 || phase >= (double)b_frames) { grains[j].active = 0; break; }
            }
        }
        grains[j].phase = phase;
        grains[j].ephase = ephase;
        grains[j].delay = delay;
    }
    buffer_unlocksamples(wavebuf_b);
    buffer_unlocksamples(windowbuf_b);
}

void multigrain_dsp_free(t_multigrain *x)
{
    dsp_free((t_pxobject *)x);
    if (x->grains)     { sysmem_freeptr(x->grains); x->grains = NULL; }
    if (x->pitchscale) { sysmem_freeptr(x->pitchscale); x->pitchscale = NULL; }
    if (x->changroup)  { sysmem_freeptr(x->changroup); x->changroup = NULL; }
    if (x->wavebuf)    { object_free(x->wavebuf); x->wavebuf = NULL; }
    if (x->windowbuf)  { object_free(x->windowbuf); x->windowbuf = NULL; }
}

void multigrain_dsp64(t_multigrain *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    if (x->sr != (float)sr) {
        x->sr = (float)sr;
        multigrain_init(x, 1);
    }
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)multigrain_perform64, 0, NULL);
}

void multigrain_assist(t_multigrain *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(messages) Control Input", 256);
    } else if (msg == ASSIST_OUTLET) {
        snprintf(dst, 256, "(signal) Audio Output Channel %ld", arg + 1);
    }
}
