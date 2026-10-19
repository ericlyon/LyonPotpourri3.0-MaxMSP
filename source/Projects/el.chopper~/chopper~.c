/**
 * @file chopper~.c
 * @brief Granular/loop slicing and playback audio unit for Max 8/9.
 * Modernized for 64-bit dsp64, buffer lock symmetry, and memory safety.
 */

#include "../include/MSPd.h"
#include <stdlib.h>
#include <math.h>

#define MAXSTORE 1024
#define OBJECT_NAME "chopper~"

static t_class *chopper_class;

typedef struct _chopper
{
    t_pxobject x_obj;
    t_buffer_ref *buffer_ref;
    t_symbol *wavename;
    long l_chan;
    float increment;
    double fbindex;
    float buffer_duration;
    int already_failed;
    long last_modtime;
    float minseg;
    float maxseg;
    float segdur;
    float minincr;
    float maxincr;
    int loop_samps;
    int samps_to_go;
    int loop_start;
    int bindex;
    int taper_samps;
    int loop_min_samps;
    int loop_max_samps;
    float R;
    float ldev;
    float st_dev;
    int lock_loop;
    int force_new_loop;
    int framesize;
    short mute;
    short disabled;
    int outlet_count;
    int *stored_starts;
    int *stored_samps;
    float *stored_increments;
    short preempt;
    short loop_engaged;
    short data_recalled;
    short initialize_loop;
    short fixed_increment_on;
    float fixed_increment;
    float retro_odds;
    float fade_level;
    int transp_loop_samps;
    float taper_duration;
    short lock_terminated;
    int preempt_samps;
    int preempt_count;
    short recalling_loop;
    float jitter_factor;
    float rdur_factor;
    float rinc_factor;
    short increment_adjusts_loop;
    short loop_adjust_inverse;
    float saved_increment;
    long b_frames;
    long b_nchans;
    float *b_samples;
    void *sync_outlet;
    float dropout_odds;
    short dropout_state;
    float amplitude_deviation;
    float loop_gain;
    t_float **msp_outlets;
    float minseg_attr;
    float maxseg_attr;
} t_chopper;

/* Method Prototypes */
void *chopper_new(t_symbol *msg, long argc, t_atom *argv);
void chopper_free(t_chopper *x);
void chopper_assist(t_chopper *x, void *b, long m, long a, char *s);
void chopper_dblclick(t_chopper *x);
void chopper_show_loop(t_chopper *x);
void chopper_set_loop(t_chopper *x, t_symbol *msg, short argc, t_atom *argv);
void chopper_randloop(t_chopper *x);
void chopper_store_loop(t_chopper *x, t_floatarg f);
void chopper_recall_loop(t_chopper *x, t_floatarg f);
void chopper_taper(t_chopper *x, t_floatarg f);
void chopper_fixed_increment(t_chopper *x, t_floatarg f);
void chopper_lockme(t_chopper *x, t_floatarg n);
void chopper_force_new(t_chopper *x);
float chopper_boundrand(float min, float max);
void chopper_mute(t_chopper *x, t_floatarg toggle);
void chopper_seed(t_chopper *x, t_floatarg seed);
void chopper_increment_adjust(t_chopper *x, t_floatarg toggle);
void chopper_adjust_inverse(t_chopper *x, t_floatarg toggle);
void chopper_jitter(t_chopper *x, t_floatarg f);
void chopper_rdur(t_chopper *x, t_floatarg f);
void chopper_rinc(t_chopper *x, t_floatarg f);
void chopper_retro_odds(t_chopper *x, t_floatarg f);
void chopper_set_minincr(t_chopper *x, double n);
void chopper_set_maxincr(t_chopper *x, double n);
void chopper_set_minseg(t_chopper *x, double n);
void chopper_set_maxseg(t_chopper *x, double n);
void chopper_init(t_chopper *x, short initialized);
void chopper_sync_out(t_chopper *x);
void chopper_sync_in(t_chopper *x, t_symbol *msg, short argc, t_atom *argv);
void chopper_chopspec(t_chopper *x, t_symbol *msg, short argc, t_atom *argv);
void chopper_setbuf(t_chopper *x, t_symbol *s);
void chopper_doset(t_chopper *x, t_symbol *s);
void chopper_set(t_chopper *x, t_symbol *s);
void chopper_attach_buffer(t_chopper *x);
t_max_err chopper_notify(t_chopper *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void chopper_dsp64(t_chopper *x, t_object *dsp64, short *count, double sr, long n, long flags);
void chopper_perform_universal64(t_chopper *x, t_object *dsp64, double **ins,
                                 long numins, double **outs, long numouts, long n,
                                 long flags, void *userparam);
t_max_err get_minseg(t_chopper *x, void *attr, long *ac, t_atom **av);
t_max_err set_minseg(t_chopper *x, void *attr, long *ac, t_atom *av);
t_max_err get_maxseg(t_chopper *x, void *attr, long *ac, t_atom **av);
t_max_err set_maxseg(t_chopper *x, void *attr, long *ac, t_atom *av);

static unsigned int chopper_rand_seed = 1;
float chopper_boundrand(float min, float max)
{
    chopper_rand_seed = chopper_rand_seed * 1103515245 + 12345;
    float rnd = (float)(chopper_rand_seed & 0x7fffffff) / (float)0x7fffffff;
    return min + (max - min) * rnd;
}

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.chopper~", 
                           (method)chopper_new, 
                           (method)chopper_free, 
                           (long)sizeof(t_chopper), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)chopper_dsp64,          "dsp64",            A_CANT, 0);
    class_addmethod(c, (method)chopper_set_minincr,    "ft1",              A_FLOAT, 0);
    class_addmethod(c, (method)chopper_set_maxincr,    "ft2",              A_FLOAT, 0);
    class_addmethod(c, (method)chopper_set_minseg,     "ft3",              A_FLOAT, 0);
    class_addmethod(c, (method)chopper_set_maxseg,     "ft4",              A_FLOAT, 0);
    class_addmethod(c, (method)chopper_lockme,         "ft5",              A_FLOAT, 0);
    class_addmethod(c, (method)chopper_force_new,      "bang",             0);
    class_addmethod(c, (method)chopper_force_new,      "newloop",          0);
    class_addmethod(c, (method)chopper_assist,         "assist",           A_CANT, 0);
    class_addmethod(c, (method)chopper_dblclick,       "dblclick",         A_CANT, 0);
    class_addmethod(c, (method)chopper_notify,         "notify",           A_CANT, 0);
    class_addmethod(c, (method)chopper_taper,          "taper",            A_FLOAT, 0);
    class_addmethod(c, (method)chopper_fixed_increment,"fixed_increment",  A_FLOAT, 0);
    class_addmethod(c, (method)chopper_retro_odds,     "retro_odds",       A_FLOAT, 0);
    class_addmethod(c, (method)chopper_show_loop,      "show_loop",        0);
    class_addmethod(c, (method)chopper_set_loop,       "set_loop",         A_GIMME, 0);
    class_addmethod(c, (method)chopper_store_loop,     "store_loop",       A_FLOAT, 0);
    class_addmethod(c, (method)chopper_recall_loop,    "recall_loop",      A_FLOAT, 0);
    class_addmethod(c, (method)chopper_increment_adjust,"increment_adjust",A_FLOAT, 0);
    class_addmethod(c, (method)chopper_adjust_inverse, "adjust_inverse",   A_FLOAT, 0);
    class_addmethod(c, (method)chopper_jitter,         "jitter",           A_FLOAT, 0);
    class_addmethod(c, (method)chopper_rdur,           "rdur",             A_FLOAT, 0);
    class_addmethod(c, (method)chopper_rinc,           "rinc",             A_FLOAT, 0);
    class_addmethod(c, (method)chopper_mute,           "mute",             A_FLOAT, 0);
    class_addmethod(c, (method)chopper_set,            "set",              A_SYM,   0);
    class_addmethod(c, (method)chopper_sync_out,       "sync_out",         0);
    class_addmethod(c, (method)chopper_sync_in,        "sync_in",          A_GIMME, 0);
    class_addmethod(c, (method)chopper_chopspec,       "chopspec",         A_GIMME, 0);

    class_dspinit(c);

    CLASS_ATTR_FLOAT(c, "dropout_odds", 0, t_chopper, dropout_odds);
    CLASS_ATTR_DEFAULT_SAVE(c, "dropout_odds", 0, "0.0");
    CLASS_ATTR_LABEL(c, "dropout_odds", 0, "Dropout Odds");

    CLASS_ATTR_FLOAT(c, "amplitude_deviation", 0, t_chopper, amplitude_deviation);
    CLASS_ATTR_DEFAULT_SAVE(c, "amplitude_deviation", 0, "0.0");
    CLASS_ATTR_LABEL(c, "amplitude_deviation", 0, "Amplitude Deviation");

    CLASS_ATTR_FLOAT(c, "minimum_increment", 0, t_chopper, minincr);
    CLASS_ATTR_DEFAULT_SAVE(c, "minimum_increment", 0, "0.8");
    CLASS_ATTR_LABEL(c, "minimum_increment", 0, "Minimum Increment");

    CLASS_ATTR_FLOAT(c, "maximum_increment", 0, t_chopper, maxincr);
    CLASS_ATTR_DEFAULT_SAVE(c, "maximum_increment", 0, "1.2");
    CLASS_ATTR_LABEL(c, "maximum_increment", 0, "Maximum Increment");

    CLASS_ATTR_FLOAT(c, "minimum_segdur", 0, t_chopper, minseg);
    CLASS_ATTR_DEFAULT_SAVE(c, "minimum_segdur", 0, "250.0");
    CLASS_ATTR_ACCESSORS(c, "minimum_segdur", (method)get_minseg, (method)set_minseg);
    CLASS_ATTR_LABEL(c, "minimum_segdur", 0, "Minimum Segment Duration");

    CLASS_ATTR_FLOAT(c, "maximum_segdur", 0, t_chopper, maxseg);
    CLASS_ATTR_DEFAULT_SAVE(c, "maximum_segdur", 0, "2000.0");
    CLASS_ATTR_ACCESSORS(c, "maximum_segdur", (method)get_maxseg, (method)set_maxseg);
    CLASS_ATTR_LABEL(c, "maximum_segdur", 0, "Maximum Segment Duration");

    class_register(CLASS_BOX, c);
    chopper_class = c;
    potpourri_announce(OBJECT_NAME);

    return 0;
}

void chopper_dblclick(t_chopper *x)
{
    chopper_attach_buffer(x);
    if (x->buffer_ref) {
        buffer_view(buffer_ref_getobject(x->buffer_ref));
    }
}

void chopper_sync_out(t_chopper *x)
{
    t_atom synclist[8];
    t_buffer_obj *buffer = x->buffer_ref ? buffer_ref_getobject(x->buffer_ref) : NULL;
    if (!buffer || x->x_obj.z_disabled) {
        object_error((t_object *)x, "%s: no valid buffer attached", OBJECT_NAME);
        return;
    }
    long frames = buffer_getframecount(buffer);

    atom_setlong(synclist, frames);
    atom_setlong(synclist + 1, x->loop_start);
    atom_setlong(synclist + 2, x->loop_samps);
    atom_setlong(synclist + 3, x->transp_loop_samps);
    atom_setlong(synclist + 4, x->samps_to_go);
    atom_setfloat(synclist + 5, x->increment);
    atom_setfloat(synclist + 6, x->segdur);
    atom_setfloat(synclist + 7, (float)x->fbindex);
    outlet_anything(x->sync_outlet, gensym("sync_in"), 8, synclist);
}

void chopper_chopspec(t_chopper *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 4) {
        object_error((t_object *)x, "chopspec requires: mininc maxinc minseg maxseg");
        return;
    }
    x->minincr = (float)atom_getfloat(argv);
    x->maxincr = (float)atom_getfloat(argv + 1);
    x->minseg  = (float)atom_getfloat(argv + 2) * 0.001f;
    x->maxseg  = (float)atom_getfloat(argv + 3) * 0.001f;
}

void chopper_sync_in(t_chopper *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 8) return;
    t_atom_long b_frames, loop_start, loop_samps, transp_loop_samps, samps_to_go;
    float increment, segdur, fbindex;

    t_buffer_obj *buffer = x->buffer_ref ? buffer_ref_getobject(x->buffer_ref) : NULL;
    if (!buffer) return;

    b_frames = atom_getlong(argv);
    loop_start = atom_getlong(argv + 1);
    loop_samps = atom_getlong(argv + 2);
    transp_loop_samps = atom_getlong(argv + 3);
    samps_to_go = atom_getlong(argv + 4);
    increment = (float)atom_getfloat(argv + 5);
    segdur = (float)atom_getfloat(argv + 6);
    fbindex = (float)atom_getfloat(argv + 7);

    if (b_frames != buffer_getframecount(buffer)) {
        object_error((t_object *)x, "%s: buffer mismatch", OBJECT_NAME);
        return;
    }
    x->loop_start = (int)loop_start;
    x->loop_samps = (int)loop_samps;
    x->transp_loop_samps = (int)transp_loop_samps;
    x->samps_to_go = (int)samps_to_go;
    x->increment = increment;
    x->segdur = segdur;
    x->fbindex = (double)fbindex;
}

void chopper_assist(t_chopper *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(bang/commands) Force New Loop", 256); break;
            case 1: strncpy_zero(dst, "(float) Minimum Increment", 256); break;
            case 2: strncpy_zero(dst, "(float) Maximum Increment", 256); break;
            case 3: strncpy_zero(dst, "(float) Minimum Segdur (ms)", 256); break;
            case 4: strncpy_zero(dst, "(float) Maximum Segdur (ms)", 256); break;
            case 5: strncpy_zero(dst, "(int) Lock Loop Switch", 256); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        if (arg < x->outlet_count) {
            strncpy_zero(dst, "(signal) Audio Output", 256);
        } else {
            strncpy_zero(dst, "(list) Sync Data Outlet", 256);
        }
    }
}

void chopper_mute(t_chopper *x, t_floatarg toggle) { x->mute = (short)toggle; }
void chopper_seed(t_chopper *x, t_floatarg seed)   { chopper_rand_seed = (unsigned int)seed; }
void chopper_increment_adjust(t_chopper *x, t_floatarg toggle) { x->increment_adjusts_loop = (short)toggle; }
void chopper_adjust_inverse(t_chopper *x, t_floatarg toggle)   { x->loop_adjust_inverse = (short)toggle; }

void chopper_fixed_increment(t_chopper *x, t_floatarg f)
{
    float new_samps = 0.0f;
    float rectf = (float)fabs(f);

    x->fixed_increment = f;
    if (f != 0.0f) {
        x->fixed_increment_on = 1;
        x->saved_increment = x->increment;
    } else {
        x->fixed_increment_on = 0;
        x->increment = x->saved_increment;
    }

    if (x->lock_loop && rectf > 0.0f) {
        if (x->loop_adjust_inverse) {
            new_samps = (float)x->loop_samps * rectf;
        } else {
            new_samps = (float)x->loop_samps / rectf;
        }
        if (f > 0.0f) {
            if (x->loop_start + (int)new_samps < x->framesize) {
                x->increment = x->fixed_increment;
            }
        } else {
            if (x->loop_start - (int)new_samps >= 0) {
                x->increment = x->fixed_increment;
            }
        }
    }
}

void chopper_jitter(t_chopper *x, t_floatarg f)
{
    f *= 0.1f;
    if (f >= 0.0f && f <= 1.0f) x->jitter_factor = f;
}

void chopper_rdur(t_chopper *x, t_floatarg f)
{
    if (f >= 0.0f && f <= 1.0f) x->rdur_factor = f;
}

void chopper_rinc(t_chopper *x, t_floatarg f)
{
    if (f >= 0.0f && f <= 1.0f) x->rinc_factor = f;
}

void chopper_retro_odds(t_chopper *x, t_floatarg f)
{
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    x->retro_odds = f;
}

void chopper_show_loop(t_chopper *x)
{
    post("%s: start: %d, samps: %d, increment: %f", OBJECT_NAME, x->loop_start, x->transp_loop_samps, x->increment);
}

void chopper_store_loop(t_chopper *x, t_floatarg f)
{
    int loop_bindex = (int)f;
    if (loop_bindex < 0 || loop_bindex >= MAXSTORE) {
        object_error((t_object *)x, "%s: bindex %d out of range", OBJECT_NAME, loop_bindex);
        return;
    }
    x->stored_starts[loop_bindex] = x->loop_start;
    x->stored_samps[loop_bindex] = x->transp_loop_samps;
    x->stored_increments[loop_bindex] = x->increment;
}

void chopper_recall_loop(t_chopper *x, t_floatarg f)
{
    int loop_bindex = (int)f;
    if (loop_bindex < 0 || loop_bindex >= MAXSTORE) {
        object_error((t_object *)x, "%s: bindex %d out of range", OBJECT_NAME, loop_bindex);
        return;
    }
    if (!x->stored_samps[loop_bindex]) {
        object_error((t_object *)x, "%s: no loop stored at position %d", OBJECT_NAME, loop_bindex);
        return;
    }

    x->loop_start = x->stored_starts[loop_bindex];
    x->samps_to_go = x->transp_loop_samps = x->stored_samps[loop_bindex];

    if (x->loop_min_samps > x->transp_loop_samps) x->loop_min_samps = x->transp_loop_samps;
    if (x->loop_max_samps < x->transp_loop_samps) x->loop_max_samps = x->transp_loop_samps;

    x->increment = x->stored_increments[loop_bindex];
    x->preempt_count = x->preempt_samps;
    x->recalling_loop = 1;
}

void chopper_set_loop(t_chopper *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 3) {
        object_error((t_object *)x, "%s: set_loop format: <start_frame> <samples> <increment>", OBJECT_NAME);
        return;
    }
    x->loop_start = (int)atom_getintarg(0, argc, argv);
    x->transp_loop_samps = (int)atom_getintarg(1, argc, argv);
    x->increment = (float)atom_getfloatarg(2, argc, argv);
    x->data_recalled = 1;

    x->samps_to_go = x->transp_loop_samps;
    x->fbindex = (double)(x->bindex = x->loop_start);
}

void chopper_taper(t_chopper *x, t_floatarg f)
{
    f *= 0.001f;
    if (f > 0.0f) {
        x->taper_samps = (int)(x->R * f);
    }
    if (x->taper_samps < 2) x->taper_samps = 2;
}

void *chopper_new(t_symbol *msg, long argc, t_atom *argv)
{
    t_atom_long tint = 1;
    t_chopper *x = (t_chopper *)object_alloc(chopper_class);
    if (!x) return NULL;

    x->outlet_count = 1;
    x->already_failed = 0;

    if (argc < 1) {
        object_error((t_object *)x, "%s: must provide buffer name", OBJECT_NAME);
        return x;
    }

    x->wavename = atom_getsymarg(0, (short)argc, argv);
    x->taper_duration = 20.0f;

    if (argc > 1) tint = (t_atom_long)atom_getfloat(argv + 1);
    if (argc > 2) x->taper_duration = (float)atom_getfloat(argv + 2);

    x->outlet_count = (int)tint;
    if (x->outlet_count < 1) x->outlet_count = 1;

    dsp_setup((t_pxobject *)x, 0);
    x->x_obj.z_misc |= Z_NO_INPLACE;

    floatin((t_object *)x, 5);
    floatin((t_object *)x, 4);
    floatin((t_object *)x, 3);
    floatin((t_object *)x, 2);
    floatin((t_object *)x, 1);

    x->sync_outlet = outlet_new((t_object *)x, NULL);
    for (int i = 0; i < x->outlet_count; i++) {
        outlet_new((t_object *)x, "signal");
    }

    x->R = (float)sys_getsr();
    if (x->R <= 0.0f) x->R = 44100.0f;

    chopper_init(x, 0);
    attr_args_process(x, (short)argc, argv);
    return x;
}

void chopper_init(t_chopper *x, short initialized)
{
    if (!initialized) {
        x->msp_outlets = (t_float **)sysmem_newptrclear(x->outlet_count * sizeof(t_float *));
        x->minseg = 0.1f;
        x->maxseg = 0.8f;
        x->minincr = 0.5f;
        x->maxincr = 2.0f;
        x->increment = 1.0f;
        x->data_recalled = 0;
        x->segdur = 0.0f;
        x->bindex = 0;
        x->taper_duration *= 0.001f;
        if (x->taper_duration < 0.0001f || x->taper_duration > 10.0f)
            x->taper_duration = 0.0001f;

        x->increment_adjusts_loop = 0;
        x->taper_samps = (int)(x->R * x->taper_duration);
        if (x->taper_samps < 2) x->taper_samps = 2;

        x->preempt_samps = 5;
        x->loop_adjust_inverse = 0;
        x->preempt = 1;
        x->recalling_loop = 0;
        x->ldev = 0.0f;
        x->lock_loop = 0;
        x->buffer_duration = 0.0f;
        x->st_dev = 0.0f;
        x->framesize = 0;
        x->force_new_loop = 0;
        x->mute = 0;
        x->disabled = 0;
        x->initialize_loop = 1;
        x->loop_engaged = 0;
        x->fixed_increment_on = 0;
        x->retro_odds = 0.5f;
        x->fade_level = 1.0f;
        x->lock_terminated = 0;
        x->dropout_odds = 0.0f;
        x->dropout_state = 0;
        x->amplitude_deviation = 0.0f;
        x->loop_gain = 1.0f;

        x->stored_starts = (int *)sysmem_newptrclear(MAXSTORE * sizeof(int));
        x->stored_samps = (int *)sysmem_newptrclear(MAXSTORE * sizeof(int));
        x->stored_increments = (float *)sysmem_newptrclear(MAXSTORE * sizeof(float));

        chopper_setbuf(x, x->wavename);
    } else {
        x->taper_samps = (int)(x->R * x->taper_duration);
        if (x->taper_samps < 2) x->taper_samps = 2;
    }
}

void chopper_free(t_chopper *x)
{
    dsp_free((t_pxobject *)x);

    if (x->stored_increments) { sysmem_freeptr(x->stored_increments); x->stored_increments = NULL; }
    if (x->stored_samps)      { sysmem_freeptr(x->stored_samps); x->stored_samps = NULL; }
    if (x->stored_starts)     { sysmem_freeptr(x->stored_starts); x->stored_starts = NULL; }
    if (x->msp_outlets)       { sysmem_freeptr(x->msp_outlets); x->msp_outlets = NULL; }
    if (x->buffer_ref)        { object_free(x->buffer_ref); x->buffer_ref = NULL; }
}

void chopper_jitterme(t_chopper *x)
{
    float jitter_factor = x->jitter_factor;
    float new_start = (1.0f + chopper_boundrand(-jitter_factor, jitter_factor)) * (float)x->loop_start;

    if (new_start < 0.0f) {
        new_start = 0.0f;
    } else if (new_start + (float)x->transp_loop_samps >= (float)x->framesize) {
        new_start = (float)(x->framesize - x->transp_loop_samps);
    }
    if (new_start >= 0.0f) x->loop_start = (int)new_start;
}

void chopper_rdurme(t_chopper *x)
{
    float rdur_factor = x->rdur_factor;
    if (rdur_factor < 0.0f || rdur_factor > 1.0f) return;

    float durmult = 1.0f + chopper_boundrand(-rdur_factor, rdur_factor);
    float new_dur = durmult * (float)x->transp_loop_samps;

    x->loop_min_samps = (int)(x->minseg * x->R);
    x->loop_max_samps = (int)(x->maxseg * x->R);

    if (new_dur > (float)x->loop_max_samps) new_dur = (float)x->loop_max_samps;
    if (new_dur < (float)x->loop_min_samps) new_dur = (float)x->loop_min_samps;

    x->transp_loop_samps = (int)new_dur;
}

void chopper_rincme(t_chopper *x)
{
    float rinc_factor = x->rinc_factor;
    float new_inc = 1.0f + chopper_boundrand(0.0f, rinc_factor);
    if (chopper_boundrand(0.0f, 1.0f) > 0.5f) {
        new_inc = 1.0f / new_inc;
    }

    if (fabsf(new_inc * x->increment) < x->minincr) {
        new_inc = x->minincr / fabsf(x->increment);
    } else if (fabsf(new_inc * x->increment) > x->maxincr) {
        new_inc = x->maxincr / fabsf(x->increment);
    }

    int new_samps = x->transp_loop_samps;
    if (x->increment_adjusts_loop && new_inc != 0.0f) {
        new_samps = (int)((float)x->transp_loop_samps / new_inc);
    }

    new_inc *= x->increment;
    if (x->increment > 0.0f) {
        if (x->loop_start + new_samps >= x->framesize) {
            new_samps = (x->framesize - 1) - x->loop_start;
        }
    } else {
        if (x->loop_start - new_samps < 0) {
            new_samps = x->loop_start + 1;
        }
    }
    x->transp_loop_samps = new_samps;
    x->increment = new_inc;
}

void chopper_randloop(t_chopper *x)
{
    t_buffer_obj *buffer = x->buffer_ref ? buffer_ref_getobject(x->buffer_ref) : NULL;
    if (!buffer) return;

    long framesize = buffer_getframecount(buffer);
    if (framesize <= 0) return;

    float increment = x->fixed_increment_on ? x->fixed_increment : chopper_boundrand(x->minincr, x->maxincr);
    if (fabsf(increment) < 0.00001f) increment = 0.01f;

    float segdur = chopper_boundrand(x->minseg, x->maxseg);
    int loop_samps = (int)(segdur * x->R * fabsf(increment));
    int transp_loop_samps = (int)(segdur * x->R);
    int loop_start = 0;

    if (loop_samps >= framesize) {
        loop_samps = (int)(framesize - 1);
        loop_start = 0;
    } else {
        float max_start = x->buffer_duration - segdur;
        if (max_start < 0.0f) max_start = 0.0f;
        loop_start = (int)(x->R * chopper_boundrand(0.0f, max_start));
        if (loop_start + loop_samps >= framesize) {
            loop_start = (int)(framesize - loop_samps);
            if (loop_start < 0) loop_start = 0;
        }
    }

    if (chopper_boundrand(0.0f, 1.0f) < x->retro_odds) {
        increment *= -1.0f;
        loop_start += (loop_samps - 1);
    }

    x->samps_to_go = transp_loop_samps;
    x->fbindex = (double)(x->bindex = loop_start);
    x->loop_start = loop_start;
    x->loop_samps = loop_samps;
    x->transp_loop_samps = transp_loop_samps;
    x->increment = increment;
    x->segdur = segdur;
}

void chopper_perform_universal64(t_chopper *x, t_object *dsp64, double **ins,
                                 long numins, double **outs, long numouts, long n,
                                 long flags, void *userparam)
{
    int bindex;
    int i, k;
    float sample = 0.0f;
    float tmp1, tmp2;

    t_buffer_obj *the_buffer = x->buffer_ref ? buffer_ref_getobject(x->buffer_ref) : NULL;
    if (!the_buffer) {
        if (!x->already_failed) {
            x->already_failed = 1;
            object_post((t_object *)x, "\"%s\" is an invalid buffer", x->wavename ? x->wavename->s_name : "none");
        }
        goto zero;
    }

    float *tab = buffer_locksamples(the_buffer);
    if (!tab) {
        goto zero;
    }

    long frames = buffer_getframecount(the_buffer);
    long nc = buffer_getchannelcount(the_buffer);

    if (frames <= 0 || x->outlet_count != nc) {
        if (!x->already_failed) {
            x->already_failed = 1;
            object_post((t_object *)x, "buffer mismatch or empty buffer");
        }
        buffer_unlocksamples(the_buffer);
        goto zero;
    }

    long active_outlets = nc;

    if (x->mute || x->x_obj.z_disabled) {
        buffer_unlocksamples(the_buffer);
        for (i = 0; i < numouts; i++) {
            memset((void *)outs[i], 0, sizeof(t_double) * n);
        }
        return;
    }

    if (x->initialize_loop) {
        x->buffer_duration = (double)frames / x->R;
        x->framesize = (int)frames;
        chopper_randloop(x);
        x->initialize_loop = 0;
    }

    if (x->maxseg > x->buffer_duration) x->maxseg = x->buffer_duration;
    if (x->minseg < 2.0f * x->taper_duration) x->minseg = x->taper_duration;

    bindex = (int)x->fbindex;
    float segdur = x->segdur;
    int taper_samps = x->taper_samps;
    int lock_loop = x->lock_loop;
    int force_new_loop = x->force_new_loop;
    float fade_level = x->fade_level;
    short preempt = x->preempt;
    int preempt_count = x->preempt_count;
    int preempt_samps = x->preempt_samps;
    short recalling_loop = x->recalling_loop;
    float preempt_gain = 1.0f;
    float jitter_factor = x->jitter_factor;
    float rdur_factor = x->rdur_factor;
    float rinc_factor = x->rinc_factor;
    double fbindex = x->fbindex;
    float increment = x->increment;
    int transp_loop_samps = x->transp_loop_samps;
    int loop_start = x->loop_start;
    int loop_samps = x->loop_samps;
    int samps_to_go = x->samps_to_go;
    float loop_gain = x->loop_gain;

    for (k = 0; k < n; k++) {
        if (lock_loop) {
            if (recalling_loop) {
                bindex = (int)floor(fbindex);
                if (bindex < 0 || bindex >= frames - 1) bindex = loop_start;
                double frac = fbindex - (double)bindex;
                fbindex += (double)increment;
                --preempt_count;
                preempt_gain = fade_level * ((float)preempt_count / (float)preempt_samps);

                for (i = 0; i < numouts && i < active_outlets; i++) {
                    if (x->dropout_state) {
                        outs[i][k] = 0.0;
                    } else {
                        if (bindex < 0 || bindex >= frames - 1) bindex = 0;
                        if (bindex < frames - 1) {
                            tmp1 = tab[bindex * nc + i];
                            tmp2 = tab[(bindex + 1) * nc + i];
                            sample = (tmp1 + (float)frac * (tmp2 - tmp1)) * preempt_gain;
                        } else {
                            sample = tab[bindex * nc + i] * preempt_gain;
                        }
                        outs[i][k] = (t_double)(sample * loop_gain);
                    }
                }
                if (preempt_count <= 0) {
                    bindex = (int)(fbindex = (double)loop_start);
                    recalling_loop = 0;
                }
            } else if (force_new_loop) {
                if (bindex < 0 || bindex >= frames) fbindex = bindex = (int)(frames / 2);
                if (preempt && preempt_samps > samps_to_go) {
                    --preempt_count;
                    preempt_gain = fade_level * ((float)preempt_count / (float)preempt_samps);
                    double frac = fbindex - (double)bindex;
                    fbindex += (double)increment;

                    for (i = 0; i < numouts && i < active_outlets; i++) {
                        if (x->dropout_state) {
                            outs[i][k] = 0.0;
                        } else {
                            if (bindex < 0 || bindex >= frames - 1) bindex = 0;
                            if (bindex < frames - 1) {
                                tmp1 = tab[bindex * nc + i];
                                tmp2 = tab[(bindex + 1) * nc + i];
                                sample = (tmp1 + (float)frac * (tmp2 - tmp1)) * preempt_gain;
                            } else {
                                sample = tab[bindex * nc + i] * preempt_gain;
                            }
                            outs[i][k] = (t_double)(sample * loop_gain);
                        }
                    }
                    if (preempt_count <= 0) {
                        chopper_randloop(x);
                        bindex = (int)(fbindex = x->fbindex);
                        samps_to_go = x->samps_to_go;
                        loop_start = x->loop_start;
                        loop_samps = x->loop_samps;
                        transp_loop_samps = x->transp_loop_samps;
                        increment = x->increment;
                        segdur = x->segdur;
                        force_new_loop = 0;
                    }
                } else {
                    chopper_randloop(x);
                    bindex = (int)(fbindex = x->fbindex);
                    samps_to_go = x->samps_to_go;
                    loop_start = x->loop_start;
                    loop_samps = x->loop_samps;
                    transp_loop_samps = x->transp_loop_samps;
                    increment = x->increment;
                    segdur = x->segdur;
                    force_new_loop = 0;
                    fbindex += (double)increment;
                    --samps_to_go;
                    if (samps_to_go <= 0) {
                        bindex = (int)(fbindex = (double)loop_start);
                        samps_to_go = transp_loop_samps;
                    }
                    bindex = (int)fbindex;
                    double frac = fbindex - (double)bindex;
                    fbindex += (double)increment;

                    for (i = 0; i < numouts && i < active_outlets; i++) {
                        if (x->dropout_state) {
                            outs[i][k] = 0.0;
                        } else {
                            if (bindex < 0 || bindex >= frames - 1) bindex = 0;
                            if (bindex < frames - 1) {
                                tmp1 = tab[bindex * nc + i];
                                tmp2 = tab[(bindex + 1) * nc + i];
                                sample = (tmp1 + (float)frac * (tmp2 - tmp1)) * preempt_gain;
                            } else {
                                sample = tab[bindex * nc + i] * preempt_gain;
                            }
                            sample *= loop_gain;
                            if (samps_to_go > transp_loop_samps - taper_samps) {
                                fade_level = (float)(transp_loop_samps - samps_to_go) / (float)taper_samps;
                                outs[i][k] = (t_double)(sample * fade_level);
                            } else if (samps_to_go < taper_samps) {
                                fade_level = (float)samps_to_go / (float)taper_samps;
                                outs[i][k] = (t_double)(sample * fade_level);
                            } else {
                                fade_level = 1.0f;
                                outs[i][k] = (t_double)sample;
                            }
                        }
                    }
                }
            } else {
                if (bindex < 0 || bindex >= frames) {
                    chopper_randloop(x);
                    bindex = (int)(fbindex = x->fbindex);
                    samps_to_go = x->samps_to_go;
                    loop_start = x->loop_start;
                    loop_samps = x->loop_samps;
                    transp_loop_samps = x->transp_loop_samps;
                    increment = x->increment;
                    segdur = x->segdur;
                }
                bindex = (int)floor(fbindex);
                double frac = fbindex - (double)bindex;
                fbindex += (double)increment;
                --samps_to_go;

                if (samps_to_go <= 0) {
                    if (chopper_boundrand(0.0f, 1.0f) < x->dropout_odds) {
                        x->dropout_state = 1;
                    } else {
                        x->dropout_state = 0;
                    }
                    if (x->amplitude_deviation <= 0.0f) {
                        loop_gain = 1.0f;
                    } else {
                        loop_gain = chopper_boundrand(1.0f - x->amplitude_deviation, 1.0f);
                    }
                    if (rdur_factor)  chopper_rdurme(x);
                    if (jitter_factor) chopper_jitterme(x);
                    if (rinc_factor)  chopper_rincme(x);

                    bindex = (int)(fbindex = (double)(loop_start = x->loop_start));
                    samps_to_go = transp_loop_samps = x->transp_loop_samps;
                    increment = x->increment;
                }

                for (i = 0; i < numouts && i < active_outlets; i++) {
                    if (x->dropout_state) {
                        outs[i][k] = 0.0;
                    } else {
                        if (bindex < 0 || bindex >= frames - 1) bindex = 0;
                        if (bindex < frames - 1) {
                            tmp1 = tab[bindex * nc + i];
                            tmp2 = tab[(bindex + 1) * nc + i];
                            sample = (tmp1 + (float)frac * (tmp2 - tmp1)) * preempt_gain;
                        } else {
                            sample = tab[bindex * nc + i] * preempt_gain;
                        }
                        sample *= loop_gain;
                        if (samps_to_go > transp_loop_samps - taper_samps) {
                            fade_level = (float)(transp_loop_samps - samps_to_go) / (float)taper_samps;
                            outs[i][k] = (t_double)(sample * fade_level);
                        } else if (samps_to_go < taper_samps) {
                            fade_level = (float)samps_to_go / (float)taper_samps;
                            outs[i][k] = (t_double)(sample * fade_level);
                        } else {
                            fade_level = 1.0f;
                            outs[i][k] = (t_double)sample;
                        }
                    }
                }
            }
        } else if (recalling_loop) {
            --preempt_count;
            if (preempt_samps <= 0) {
                preempt_gain = fade_level;
            } else {
                preempt_gain = fade_level * ((float)preempt_count / (float)preempt_samps);
            }
            bindex = (int)fbindex;
            double frac = fbindex - (double)bindex;
            fbindex += (double)increment;

            if (fbindex < 0.0 || fbindex >= (double)(frames - 1)) {
                bindex = (int)(fbindex = (double)loop_start);
            }

            for (i = 0; i < numouts && i < active_outlets; i++) {
                if (x->dropout_state) {
                    outs[i][k] = 0.0;
                } else {
                    if (bindex < 0 || bindex >= frames - 1) bindex = 0;
                    if (bindex < frames - 1) {
                        tmp1 = tab[bindex * nc + i];
                        tmp2 = tab[(bindex + 1) * nc + i];
                        sample = (tmp1 + (float)frac * (tmp2 - tmp1)) * preempt_gain;
                    } else {
                        sample = tab[bindex * nc + i] * preempt_gain;
                    }
                    sample *= loop_gain;
                    outs[i][k] = (t_double)(sample * preempt_gain);
                }
            }
            if (preempt_count <= 0) {
                bindex = (int)(fbindex = (double)loop_start);
                recalling_loop = 0;
            }
        } else {
            fade_level = 1.0f;
            if (bindex < 0 || bindex >= frames) {
                chopper_randloop(x);
                bindex = (int)(fbindex = x->fbindex);
                samps_to_go = x->samps_to_go;
                loop_start = x->loop_start;
                loop_samps = x->loop_samps;
                transp_loop_samps = x->transp_loop_samps;
                increment = x->increment;
                segdur = x->segdur;
            }
            bindex = (int)fbindex;
            double frac = fbindex - (double)bindex;
            fbindex += (double)increment;
            if (fbindex < 0.0 || fbindex >= (double)frames) {
                bindex = (int)(fbindex = (double)loop_start);
            }

            for (i = 0; i < numouts && i < active_outlets; i++) {
                if (x->dropout_state) {
                    outs[i][k] = 0.0;
                } else {
                    if (bindex < 0 || bindex >= frames - 1) bindex = 0;
                    if (bindex < frames - 1) {
                        if (bindex < 0 || bindex > frames - 1) {
                            bindex = 0;
                        }
                        tmp1 = tab[bindex * nc + i];
                        tmp2 = tab[(bindex + 1) * nc + i];
                        sample = (tmp1 + (float)frac * (tmp2 - tmp1)) * preempt_gain;
                    } else {
                        sample = tab[bindex * nc + i] * preempt_gain;
                    }
                    sample *= loop_gain;
                    if (samps_to_go > transp_loop_samps - taper_samps) {
                        fade_level = (float)(transp_loop_samps - samps_to_go) / (float)taper_samps;
                        outs[i][k] = (t_double)(sample * fade_level);
                    } else if (samps_to_go < taper_samps) {
                        fade_level = (float)samps_to_go / (float)taper_samps;
                        outs[i][k] = (t_double)(sample * fade_level);
                    } else {
                        fade_level = 1.0f;
                        outs[i][k] = (t_double)sample;
                    }
                }
            }

            --samps_to_go;
            if (samps_to_go <= 0) {
                if (chopper_boundrand(0.0f, 1.0f) < x->dropout_odds) {
                    x->dropout_state = 1;
                } else {
                    x->dropout_state = 0;
                }
                if (x->amplitude_deviation <= 0.0f) {
                    loop_gain = 1.0f;
                } else {
                    loop_gain = chopper_boundrand(1.0f - x->amplitude_deviation, 1.0f);
                }
                chopper_randloop(x);
                bindex = (int)(fbindex = x->fbindex);
                samps_to_go = x->samps_to_go;
                loop_start = x->loop_start;
                loop_samps = x->loop_samps;
                transp_loop_samps = x->transp_loop_samps;
                increment = x->increment;
                segdur = x->segdur;
            }
        }
    }

    x->loop_gain = loop_gain;
    x->recalling_loop = recalling_loop;
    x->fade_level = fade_level;
    x->segdur = segdur;
    x->force_new_loop = force_new_loop;
    x->fbindex = fbindex;
    x->samps_to_go = samps_to_go;
    x->loop_start = loop_start;
    x->loop_samps = loop_samps;
    x->increment = increment;

    buffer_unlocksamples(the_buffer);
    return;

zero:
    for (k = 0; k < n; k++) {
        for (i = 0; i < numouts; i++) {
            outs[i][k] = 0.0;
        }
    }
}

void chopper_force_new(t_chopper *x)
{
    x->preempt_count = x->preempt_samps;
    x->force_new_loop = 1;
}

void chopper_lockme(t_chopper *x, t_floatarg n)
{
    x->lock_loop = (short)n;
}

void chopper_set_minincr(t_chopper *x, double n)
{
    if (n < 0.005) n = 0.005;
    x->minincr = (float)n;
}

void chopper_set_maxincr(t_chopper *x, double n)
{
    if (n > 20.0) n = 20.0;
    x->maxincr = (float)n;
}

void chopper_set_maxseg(t_chopper *x, double n)
{
    n *= 0.001;
    if (n > 120.0) n = 120.0;
    x->maxseg = (float)n;
    x->loop_max_samps = (int)(x->maxseg * x->R);
}

void chopper_set_minseg(t_chopper *x, double n)
{
    n *= 0.001;
    if (n < 0.03) n = 0.03;
    x->minseg = (float)n;
    x->loop_min_samps = (int)(x->minseg * x->R);
}

t_max_err get_maxseg(t_chopper *x, void *attr, long *ac, t_atom **av)
{
    if (ac && av) {
        char alloc;
        if (atom_alloc(ac, av, &alloc)) return MAX_ERR_GENERIC;
        x->maxseg_attr = x->maxseg * 1000.0f;
        atom_setfloat(*av, x->maxseg_attr);
    }
    return MAX_ERR_NONE;
}

t_max_err set_maxseg(t_chopper *x, void *attr, long *ac, t_atom *av)
{
    if (ac && av) {
        float val = atom_getfloat(av);
        x->maxseg = val * 0.001f;
    }
    return MAX_ERR_NONE;
}

t_max_err get_minseg(t_chopper *x, void *attr, long *ac, t_atom **av)
{
    if (ac && av) {
        char alloc;
        if (atom_alloc(ac, av, &alloc)) return MAX_ERR_GENERIC;
        x->minseg_attr = x->minseg * 1000.0f;
        atom_setfloat(*av, x->minseg_attr);
    }
    return MAX_ERR_NONE;
}

t_max_err set_minseg(t_chopper *x, void *attr, long *ac, t_atom *av)
{
    if (ac && av) {
        float val = atom_getfloat(av);
        x->minseg = val * 0.001f;
    }
    return MAX_ERR_NONE;
}

void chopper_setbuf(t_chopper *x, t_symbol *s)
{
    if (s != x->wavename) {
        x->wavename = s;
        x->initialize_loop = 1;
        x->already_failed = 0;
        chopper_attach_buffer(x);
    }
}

void chopper_attach_buffer(t_chopper *x)
{
    if (!x->buffer_ref)
        x->buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else
        buffer_ref_set(x->buffer_ref, x->wavename);
}

t_max_err chopper_notify(t_chopper *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    return buffer_ref_notify(x->buffer_ref, s, msg, sender, data);
}

void chopper_dsp64(t_chopper *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (!sr) return;
    if (x->R != (float)sr) {
        x->R = (float)sr;
        chopper_init(x, 1);
    }
    chopper_attach_buffer(x);
    object_method(dsp64, gensym("dsp_add64"), x, chopper_perform_universal64, 0, NULL);
}

void chopper_doset(t_chopper *x, t_symbol *s)
{
    chopper_setbuf(x, s);
}

void chopper_set(t_chopper *x, t_symbol *s)
{
    defer(x, (method)chopper_doset, s, 0, NULL);
}