/**
 * @file buffet~.c
 * @brief Buffer manipulation external for Max 8/9.
 * Modernized for 64-bit dsp64, thread safety, and memory stability.
 */

#include "MSPd.h"
#include <string.h>

#define DENORM_WANT_FIX      1
#define CUSHION_FRAMES       (128)
#define MAX_CHANNELS         (1024)
#define MAX_RMS_BUFFER       (0.250)
#define MIN_RMS_BUFFER       (0.001)
#define MAX_EVENTS           (1024)
#define MAX_RMS_FRAMES       (32768)

#define OBJECT_NAME "buffet~"

static t_class *buffet_class;

typedef struct _buffet
{
    t_pxobject x_obj;
    t_symbol *wavename;
    t_buffer_ref *src_buffer_ref;
    t_buffer_ref *dest_buffer_ref;
    t_buffer_ref *cat_buffer_ref;
    t_buffer_ref *dub_buffer_ref;
    float sr;
    short hosed;
    float minframes;
    float maxframes;
    long storage_maxframes;
    float *storage;
    long storage_bytes;
    float fade;
    float sync;
    long swapframes;
    long r1startframe;
    long r2startframe;
    float dc_coef;
    float dc_gain;
    short initialized;
    float *rmsbuf;
    float rmschunk;
    void *list;
    void *bang;
    void *floater;
    t_atom *listdata;
    float *analbuf;
    float *onset;
} t_buffet;

/* Method Prototypes */
void *buffet_new(t_symbol *msg, long argc, t_atom *argv);
void buffet_dsp_free(t_buffet *x);
void buffet_setbuf(t_buffet *x, t_symbol *wavename);
float buffet_boundrand(float min, float max);
void buffet_assist(t_buffet *x, void *b, long msg, long arg, char *dst);
void buffet_swap(t_buffet *x);
void buffet_specswap(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_retroblock(t_buffet *x);
void buffet_nakedswap(t_buffet *x);
void buffet_overlap(t_buffet *x, double f);
void buffet_minswap(t_buffet *x, double f);
void buffet_maxswap(t_buffet *x, double f);
void buffet_nosync_setswap(t_buffet *x);
void buffet_info(t_buffet *x);
void buffet_killdc(t_buffet *x);
void buffet_fixdenorm(t_buffet *x);
void buffet_rmschunk(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_fadein(t_buffet *x, double f);
void buffet_fadeout(t_buffet *x, double f);
void buffet_internal_fadeout(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_dc_gain(t_buffet *x, double f);
void buffet_dc_coef(t_buffet *x, double f);
void buffet_normalize(t_buffet *x, double f);
void buffet_rotatetozero(t_buffet *x, double f);
void buffet_erase(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_events(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_pevents(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_copy_to_buffer(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_copy_to_mono_buffer(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_init(t_buffet *x, short initialized);
void buffet_reverse(t_buffet *x);
long ms2frames(long msdur, float sr);
void buffet_overdub(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_resize(t_buffet *x, double newsize);
void buffet_normalize_selection(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_catbuf(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_trimzero(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_update(t_buffet *x);
void buffet_spritz(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
void buffet_expenv(t_buffet *x, t_symbol *msg, long argc, t_atom *argv);
t_max_err buffet_notify(t_buffet *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void buffet_dsp64(t_buffet *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void buffet_perform64(t_buffet *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.buffet~", (method)buffet_new, (method)buffet_dsp_free, sizeof(t_buffet), 0L, A_GIMME, 0);

    class_addmethod(c, (method)buffet_swap, "swap", 0);
    class_addmethod(c, (method)buffet_specswap, "specswap", A_GIMME, 0);
    class_addmethod(c, (method)buffet_retroblock, "retroblock", 0);
    class_addmethod(c, (method)buffet_minswap, "minswap", A_FLOAT, 0);
    class_addmethod(c, (method)buffet_maxswap, "maxswap", A_FLOAT, 0);
    class_addmethod(c, (method)buffet_fadein, "fadein", A_FLOAT, 0);
    class_addmethod(c, (method)buffet_fadeout, "fadeout", A_FLOAT, 0);
    class_addmethod(c, (method)buffet_overlap, "overlap", A_FLOAT, 0);
    class_addmethod(c, (method)buffet_dc_gain, "dc_gain", A_FLOAT, 0);
    class_addmethod(c, (method)buffet_dc_coef, "dc_coef", A_FLOAT, 0);
    class_addmethod(c, (method)buffet_normalize, "normalize", A_FLOAT, 0);
    class_addmethod(c, (method)buffet_rotatetozero, "rotatetozero", A_FLOAT, 0);
    class_addmethod(c, (method)buffet_killdc, "killdc", 0);
    class_addmethod(c, (method)buffet_nakedswap, "nakedswap", 0);
    class_addmethod(c, (method)buffet_rmschunk, "rmschunk", A_GIMME, 0);
    class_addmethod(c, (method)buffet_erase, "erase", A_GIMME, 0);
    class_addmethod(c, (method)buffet_copy_to_buffer, "copy_to_buffer", A_GIMME, 0);
    class_addmethod(c, (method)buffet_copy_to_mono_buffer, "copy_to_mono_buffer", A_GIMME, 0);
    class_addmethod(c, (method)buffet_internal_fadeout, "internal_fadeout", A_GIMME, 0);
    class_addmethod(c, (method)buffet_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)buffet_setbuf, "setbuf", A_SYM, 0);
    class_addmethod(c, (method)buffet_reverse, "reverse", 0);
    class_addmethod(c, (method)buffet_fixdenorm, "fixdenorm", 0);
    class_addmethod(c, (method)buffet_trimzero, "trimzero", A_GIMME, 0);
    class_addmethod(c, (method)buffet_overdub, "overdub", A_GIMME, 0);
    class_addmethod(c, (method)buffet_events, "events", A_GIMME, 0);
    class_addmethod(c, (method)buffet_pevents, "pevents", A_GIMME, 0);
    class_addmethod(c, (method)buffet_catbuf, "catbuf", A_GIMME, 0);
    class_addmethod(c, (method)buffet_info, "info", 0);
    class_addmethod(c, (method)buffet_resize, "resize", A_FLOAT, 0);
    class_addmethod(c, (method)buffet_spritz, "spritz", A_GIMME, 0);
    class_addmethod(c, (method)buffet_expenv, "expenv", A_GIMME, 0);
    class_addmethod(c, (method)buffet_notify, "notify", A_CANT, 0);
    class_addmethod(c, (method)buffet_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)buffet_normalize_selection, "normalize_selection", A_GIMME, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    buffet_class = c;
    potpourri_announce(OBJECT_NAME);
//    post("updated version of buffet~\n");
    return 0;
}

float buffet_boundrand(float min, float max)
{
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}

long ms2frames(long msdur, float sr)
{
    return (long)((float)msdur * 0.001f * sr);
}

void buffet_update(t_buffet *x)
{
    if (!x->src_buffer_ref) {
        x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    } else {
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    t_buffer_obj *the_buffer = buffer_ref_getobject(x->src_buffer_ref);
    if (the_buffer) {
        object_method((t_object *)the_buffer, gensym("dirty"));
    }
    outlet_bang(x->bang);
}

void buffet_setbuf(t_buffet *x, t_symbol *wavename)
{
    x->wavename = wavename;
    if (x->src_buffer_ref) {
        buffer_ref_set(x->src_buffer_ref, wavename);
    } else {
        x->src_buffer_ref = buffer_ref_new((t_object *)x, wavename);
    }
}

void buffet_assist(t_buffet *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(signal/messages) Groove Sync Signal Input", 256);
    } else if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(bang) Operation Completed", 256); break;
            case 1: strncpy_zero(dst, "(list) Buffer Event Times", 256); break;
            case 2: strncpy_zero(dst, "(float) Buffer Segment RMS Value", 256); break;
        }
    }
}

void buffet_init(t_buffet *x, short initialized)
{
    if (x->minframes <= 0) x->minframes = 250;
    if (x->maxframes <= 0) x->maxframes = 1000;

    if (!initialized) {
        x->minframes *= 0.001f * x->sr;
        x->maxframes *= 0.001f * x->sr;
        x->storage_maxframes = x->maxframes;
        x->fade = 0.001f * 20.0f * x->sr;
        x->storage_bytes = (x->maxframes + 1) * 2 * sizeof(float);
        x->storage = (float *)sysmem_newptrclear(x->storage_bytes);
        x->dc_coef = 0.995f;
        x->dc_gain = 4.0f;
        x->rmsbuf = (float *)sysmem_newptrclear((long)(MAX_RMS_BUFFER * x->sr * sizeof(float)));
        x->listdata = (t_atom *)sysmem_newptrclear(MAX_EVENTS * sizeof(t_atom));
        x->analbuf = (float *)sysmem_newptrclear(MAX_RMS_FRAMES * sizeof(float));
        x->onset = (float *)sysmem_newptrclear(MAX_EVENTS * sizeof(float));
        x->initialized = 1;
    } else {
        x->storage_maxframes = x->maxframes;
        x->storage_bytes = (x->storage_maxframes + 1) * 2 * sizeof(float);
        x->storage = (float *)sysmem_resizeptr((void *)x->storage, x->storage_bytes);
        x->rmsbuf = (float *)sysmem_resizeptrclear((void *)x->rmsbuf, (long)(MAX_RMS_BUFFER * x->sr * sizeof(float)));
    }
}

void *buffet_new(t_symbol *msg, long argc, t_atom *argv)
{
    t_buffet *x = (t_buffet *)object_alloc(buffet_class);
    if (!x) return NULL;

    // 1 signal inlet for sync, 0 signal outlets
    dsp_setup((t_pxobject *)x, 1);

    // Right-to-left message outlet initialization
    x->floater = floatout((t_object *)x);
    x->list = listout((t_object *)x);
    x->bang = bangout((t_object *)x);

    x->src_buffer_ref = NULL;
    x->dest_buffer_ref = NULL;
    x->cat_buffer_ref = NULL;
    x->dub_buffer_ref = NULL;
    x->storage = NULL;
    x->rmsbuf = NULL;
    x->listdata = NULL;
    x->analbuf = NULL;
    x->onset = NULL;
    x->sync = 0.0f;

    x->sr = sys_getsr();
    if (x->sr <= 0) x->sr = 44100.0f;

    if (argc < 1) {
        object_error((t_object *)x, "%s: you must provide a valid buffer name", OBJECT_NAME);
        return NULL;
    }

    x->minframes = 0;
    x->maxframes = 0;
    x->wavename = atom_getsymarg(0, (short)argc, argv);
    x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);

    if (argc >= 2) atom_arg_getfloat(&x->minframes, 1, (short)argc, argv);
    if (argc >= 3) atom_arg_getfloat(&x->maxframes, 2, (short)argc, argv);

    if (!x->minframes) x->minframes = 100;
    if (!x->maxframes) x->maxframes = x->minframes + 10;

    x->initialized = 0;
    buffet_init(x, 0);

    return x;
}

void buffet_dsp_free(t_buffet *x)
{
    // Detach from audio chain before releasing resources
    dsp_free((t_pxobject *)x);

    if (x->storage) { sysmem_freeptr(x->storage); x->storage = NULL; }
    if (x->listdata) { sysmem_freeptr(x->listdata); x->listdata = NULL; }
    if (x->rmsbuf) { sysmem_freeptr(x->rmsbuf); x->rmsbuf = NULL; }
    if (x->analbuf) { sysmem_freeptr(x->analbuf); x->analbuf = NULL; }
    if (x->onset) { sysmem_freeptr(x->onset); x->onset = NULL; }

    if (x->src_buffer_ref) { object_free(x->src_buffer_ref); x->src_buffer_ref = NULL; }
    if (x->dest_buffer_ref) { object_free(x->dest_buffer_ref); x->dest_buffer_ref = NULL; }
    if (x->cat_buffer_ref) { object_free(x->cat_buffer_ref); x->cat_buffer_ref = NULL; }
    if (x->dub_buffer_ref) { object_free(x->dub_buffer_ref); x->dub_buffer_ref = NULL; }
}

void buffet_dsp64(t_buffet *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate > 0.0) {
        x->sr = (float)samplerate;
    }
    // Only compile perform routine into DSP chain if sync signal is connected
    if (count[0]) {
        dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)buffet_perform64, 0, NULL);
    }
}

void buffet_perform64(t_buffet *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
    // Safe reading from sync inlet without dereferencing non-existent outs[0]
    t_double *sync_in = ins[0];
    if (sampleframes > 0 && sync_in) {
        x->sync = (float)sync_in[sampleframes - 1];
    }
}

t_max_err buffet_notify(t_buffet *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (x->dest_buffer_ref) buffer_ref_notify(x->dest_buffer_ref, s, msg, sender, data);
    if (x->cat_buffer_ref)  buffer_ref_notify(x->cat_buffer_ref, s, msg, sender, data);
    if (x->dub_buffer_ref)  buffer_ref_notify(x->dub_buffer_ref, s, msg, sender, data);
    if (x->src_buffer_ref)  return buffer_ref_notify(x->src_buffer_ref, s, msg, sender, data);
    return MAX_ERR_NONE;
}

void buffet_rotatetozero(t_buffet *x, double f)
{
    float target = (float)f;
    long shiftframes = (long)(target * 0.001f * x->sr);
    t_buffer_obj *wavebuf_b;
    float *b_samples = NULL;
    float *tmpmem = NULL;

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if (!wavebuf_b) return;

    long b_nchans = buffer_getchannelcount(wavebuf_b);
    long b_frames = buffer_getframecount(wavebuf_b);
    b_samples = buffer_locksamples(wavebuf_b);
    if (!b_samples) return;

    if (shiftframes <= 0 || shiftframes >= b_frames) {
        object_error((t_object *)x, "%s: shift target %f is out of range", OBJECT_NAME, target);
        goto exit;
    }

    long shift_bytes = shiftframes * b_nchans * sizeof(float);
    long remain_bytes = (b_frames - shiftframes) * b_nchans * sizeof(float);

    tmpmem = (float *)sysmem_newptr(shift_bytes);
    if (!tmpmem) goto exit;

    sysmem_copyptr(b_samples, tmpmem, shift_bytes);
    // Use memmove for safe overlapping copy
    memmove(b_samples, b_samples + (shiftframes * b_nchans), remain_bytes);
    sysmem_copyptr(tmpmem, b_samples + ((b_frames - shiftframes) * b_nchans), shift_bytes);

    sysmem_freeptr(tmpmem);
    buffer_unlocksamples(wavebuf_b);
    buffet_update(x);
    return;

exit:
    if (wavebuf_b) buffer_unlocksamples(wavebuf_b);
}

void buffet_copy_to_mono_buffer(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 4) {
        object_error((t_object *)x, "%s: usage copy_to_mono_buffer <destname> <chan> <start_ms> <end_ms>", OBJECT_NAME);
        return;
    }

    t_symbol *destname = atom_getsymarg(0, (short)argc, argv);
    long grab_channel = (long)atom_getfloatarg(1, (short)argc, argv);
    long startframe = (long)(atom_getfloatarg(2, (short)argc, argv) * 0.001f * x->sr);
    long endframe = (long)(atom_getfloatarg(3, (short)argc, argv) * 0.001f * x->sr);

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    if (!x->dest_buffer_ref) x->dest_buffer_ref = buffer_ref_new((t_object *)x, destname);
    else buffer_ref_set(x->dest_buffer_ref, destname);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    t_buffer_obj *destbuf = buffer_ref_getobject(x->dest_buffer_ref);
    if (!srcbuf || !destbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    long b_dest_nchans = buffer_getchannelcount(destbuf);
    long b_dest_frames = buffer_getframecount(destbuf);

    if (b_dest_nchans != 1) {
        object_error((t_object *)x, "%s: destination buffer %s must be mono (has %ld channels)", OBJECT_NAME, destname->s_name, b_dest_nchans);
        return;
    }
    if (grab_channel < 0 || grab_channel >= b_nchans) {
        object_error((t_object *)x, "%s: invalid channel %ld (buffer has %ld)", OBJECT_NAME, grab_channel, b_nchans);
        return;
    }

    float *src_samples = buffer_locksamples(srcbuf);
    float *dest_samples = buffer_locksamples(destbuf);

    if (!src_samples || !dest_samples) {
        if (src_samples) buffer_unlocksamples(srcbuf);
        if (dest_samples) buffer_unlocksamples(destbuf);
        return;
    }

    long chunkframes = endframe - startframe;
    if (chunkframes <= 0 || startframe < 0 || endframe > b_frames) {
        object_error((t_object *)x, "%s: invalid frame boundaries", OBJECT_NAME);
        goto cleanup;
    }
    if (b_dest_frames < chunkframes) {
        chunkframes = b_dest_frames;
    }

    for (long i = 0; i < b_dest_frames; i++) dest_samples[i] = 0.0f;

    for (long i = 0; i < chunkframes; i++) {
        dest_samples[i] = src_samples[((startframe + i) * b_nchans) + grab_channel];
    }

    if (argc >= 6) {
        float fadein = atom_getfloatarg(4, (short)argc, argv);
        float fadeout = atom_getfloatarg(5, (short)argc, argv);
        long fadein_frames = (long)(fadein * 0.001f * x->sr);
        long fadeout_frames = (long)(fadeout * 0.001f * x->sr);

        if (fadein_frames > 0 && fadein_frames <= chunkframes) {
            for (long i = 0; i < fadein_frames; i++) {
                dest_samples[i] *= ((float)i / (float)fadein_frames);
            }
        }
        if (fadeout_frames > 0 && fadeout_frames <= chunkframes) {
            for (long i = 0; i < fadeout_frames; i++) {
                dest_samples[chunkframes - 1 - i] *= ((float)i / (float)fadeout_frames);
            }
        }
    }

    object_method((t_object *)destbuf, gensym("dirty"));
    outlet_bang(x->bang);

cleanup:
    buffer_unlocksamples(srcbuf);
    buffer_unlocksamples(destbuf);
}

void buffet_copy_to_buffer(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 3) {
        object_error((t_object *)x, "%s: usage copy_to_buffer <destname> <start_ms> <end_ms>", OBJECT_NAME);
        return;
    }

    t_symbol *destname = atom_getsymarg(0, (short)argc, argv);
    long startframe = (long)(atom_getfloatarg(1, (short)argc, argv) * 0.001f * x->sr);
    long endframe = (long)(atom_getfloatarg(2, (short)argc, argv) * 0.001f * x->sr);

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    if (!x->dest_buffer_ref) x->dest_buffer_ref = buffer_ref_new((t_object *)x, destname);
    else buffer_ref_set(x->dest_buffer_ref, destname);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    t_buffer_obj *destbuf = buffer_ref_getobject(x->dest_buffer_ref);
    if (!srcbuf || !destbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    long b_dest_nchans = buffer_getchannelcount(destbuf);
    long b_dest_frames = buffer_getframecount(destbuf);

    if (b_nchans != b_dest_nchans) {
        object_error((t_object *)x, "%s: channel mismatch between source (%ld) and dest (%ld)", OBJECT_NAME, b_nchans, b_dest_nchans);
        return;
    }

    float *src_samples = buffer_locksamples(srcbuf);
    float *dest_samples = buffer_locksamples(destbuf);
    if (!src_samples || !dest_samples) {
        if (src_samples) buffer_unlocksamples(srcbuf);
        if (dest_samples) buffer_unlocksamples(destbuf);
        return;
    }

    long chunkframes = endframe - startframe;
    if (chunkframes <= 0 || startframe < 0 || endframe > b_frames) {
        object_error((t_object *)x, "%s: invalid frame boundaries (%ld to %ld)", OBJECT_NAME, startframe, endframe);
        goto cleanup;
    }
    if (b_dest_frames < chunkframes) chunkframes = b_dest_frames;

    for (long i = 0; i < b_dest_frames * b_dest_nchans; i++) dest_samples[i] = 0.0f;
    sysmem_copyptr(src_samples + (startframe * b_nchans), dest_samples, chunkframes * b_nchans * sizeof(float));

    if (argc >= 5) {
        float fadein = atom_getfloatarg(3, (short)argc, argv);
        float fadeout = atom_getfloatarg(4, (short)argc, argv);
        long fadein_frames = (long)(fadein * 0.001f * x->sr);
        long fadeout_frames = (long)(fadeout * 0.001f * x->sr);

        if (fadein_frames > 0 && fadein_frames <= chunkframes) {
            for (long i = 0; i < fadein_frames; i++) {
                float env = (float)i / (float)fadein_frames;
                for (long j = 0; j < b_dest_nchans; j++) {
                    dest_samples[i * b_dest_nchans + j] *= env;
                }
            }
        }
        if (fadeout_frames > 0 && fadeout_frames <= chunkframes) {
            for (long i = 0; i < fadeout_frames; i++) {
                float env = (float)i / (float)fadeout_frames;
                for (long j = 0; j < b_dest_nchans; j++) {
                    dest_samples[(chunkframes - 1 - i) * b_dest_nchans + j] *= env;
                }
            }
        }
    }

    object_method((t_object *)destbuf, gensym("dirty"));
    outlet_bang(x->bang);

cleanup:
    buffer_unlocksamples(srcbuf);
    buffer_unlocksamples(destbuf);
}

void buffet_events(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 3) {
        object_error((t_object *)x, "%s: events requires <bufsize_ms> <onthresh> <offthresh>", OBJECT_NAME);
        return;
    }

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    float bufsize = 0.001f * atom_getfloatarg(0, (short)argc, argv);
    if (bufsize > MAX_RMS_BUFFER) bufsize = (float)MAX_RMS_BUFFER;
    else if (bufsize < MIN_RMS_BUFFER) bufsize = (float)MIN_RMS_BUFFER;

    float onthresh = atom_getfloatarg(1, (short)argc, argv);
    float offthresh = atom_getfloatarg(2, (short)argc, argv);
    long bufsamps = (long)(bufsize * x->sr);
    if (bufsamps <= 0) bufsamps = 1;

    float tadv = (float)bufsamps / x->sr;
    long aframes = (long)((float)b_frames / (float)bufsamps) - 1;

    if (aframes < 2) {
        object_error((t_object *)x, "%s: buffer is too short to analyze", OBJECT_NAME);
        buffer_unlocksamples(srcbuf);
        return;
    }

    short activated = 0;
    float realtime = 0.0f;
    int event_count = 0;

    for (long i = 0; i < aframes; i++) {
        float meansq = 0.0f;
        long ipos = b_nchans * i * bufsamps;
        for (long j = 0; j < bufsamps; j += b_nchans) {
            float s = b_samples[ipos + j];
            meansq += s * s;
        }
        meansq /= (float)bufsamps;
        float rmsval = sqrtf(meansq);
        realtime += tadv;

        if (rmsval > onthresh && !activated) {
            activated = 1;
            if (event_count >= MAX_EVENTS) break;
            atom_setfloat(x->listdata + (event_count * 2), realtime * 1000.0f);
        } else if (rmsval < offthresh && activated) {
            activated = 0;
            atom_setfloat(x->listdata + ((event_count * 2) + 1), realtime * 1000.0f);
            event_count++;
        }
    }

    if (activated && event_count < MAX_EVENTS) {
        atom_setfloat(x->listdata + ((event_count * 2) + 1), 1000.0f * (float)b_frames / x->sr);
        event_count++;
    }

    buffer_unlocksamples(srcbuf);
    outlet_list(x->list, NULL, (short)(event_count * 2), x->listdata);
}

void buffet_pevents(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 2) {
        object_error((t_object *)x, "%s: pevents requires <bufsize_ms> <diffthresh>", OBJECT_NAME);
        return;
    }

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    float bufsize = 0.001f * atom_getfloatarg(0, (short)argc, argv);
    if (bufsize > MAX_RMS_BUFFER) bufsize = (float)MAX_RMS_BUFFER;
    else if (bufsize < MIN_RMS_BUFFER) bufsize = (float)MIN_RMS_BUFFER;

    float diffthresh = atom_getfloatarg(1, (short)argc, argv);
    long bufsamps = (long)(bufsize * x->sr);
    if (bufsamps <= 0) bufsamps = 1;

    float tadv = (float)bufsamps / x->sr;
    long aframes = (long)((float)b_frames / (float)bufsamps);

    if (aframes < 2 || aframes > MAX_RMS_FRAMES) {
        buffer_unlocksamples(srcbuf);
        return;
    }

    x->analbuf[0] = 0.0f;
    float realtime = 0.0f;
    for (long i = 1; i < aframes; i++) {
        float meansq = 0.0f;
        long ipos = b_nchans * i * bufsamps;
        for (long j = 0; j < bufsamps; j += b_nchans) {
            float s = b_samples[ipos + j];
            meansq += s * s;
        }
        meansq /= (float)bufsamps;
        x->analbuf[i] = sqrtf(meansq);
    }

    int event_count = 0;
    realtime = 0.0f;
    for (long i = 1; i < aframes; i++) {
        float rmsdiff = x->analbuf[i] - x->analbuf[i - 1];
        if (rmsdiff > diffthresh && event_count < MAX_EVENTS) {
            x->onset[event_count] = (realtime + bufsize) * 1000.0f;
            event_count++;
        }
        realtime += tadv;
    }

    buffer_unlocksamples(srcbuf);

    for (int i = 0; i < event_count; i++) {
        atom_setfloat(x->listdata + i, x->onset[i]);
    }
    outlet_list(x->list, NULL, (short)event_count, x->listdata);
}

void buffet_erase(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    long startframe = 0;
    long endframe = b_frames;

    if (argc >= 2) {
        startframe = (long)(0.001f * x->sr * atom_getfloatarg(0, (short)argc, argv));
        endframe = (long)(0.001f * x->sr * atom_getfloatarg(1, (short)argc, argv));
    }

    if (startframe < 0) startframe = 0;
    if (endframe > b_frames) endframe = b_frames;

    if (startframe < endframe) {
        for (long i = startframe * b_nchans; i < endframe * b_nchans; i++) {
            b_samples[i] = 0.0f;
        }
    }

    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_reverse(t_buffet *x)
{
    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    long lenm1 = (b_frames - 1) * b_nchans;
    for (long i = 0; i < (b_frames * b_nchans) / 2; i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            float tmp = b_samples[(lenm1 - i) + j];
            b_samples[(lenm1 - i) + j] = b_samples[i + j];
            b_samples[i + j] = tmp;
        }
    }

    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_fixdenorm(t_buffet *x)
{
    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    for (long i = 0; i < b_frames * b_nchans; i++) {
        if (fabsf(b_samples[i]) < 1.0e-15f) {
            b_samples[i] = 0.0f;
        }
    }

    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_info(t_buffet *x)
{
    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) {
        object_error((t_object *)x, "%s: buffer %s is invalid", OBJECT_NAME, x->wavename->s_name);
        return;
    }
    long totalframes = buffer_getframecount(srcbuf);
    post("minswap: %f ms, maxswap: %f ms", 1000.0f * x->minframes / x->sr, 1000.0f * x->maxframes / x->sr);
    post("buffer size: %f ms (%ld frames)", 1000.0f * (float)totalframes / x->sr, totalframes);
}

void buffet_overlap(t_buffet *x, double f)
{
    if (f < 0.01) {
        object_error((t_object *)x, "minimum fade time is 0.01 milliseconds");
        return;
    }
    x->fade = (float)(f * 0.001 * x->sr);
}

void buffet_minswap(t_buffet *x, double f)
{
    if (f < 2.0 * 1000.0 * x->fade / x->sr) {
        object_error((t_object *)x, "minimum must be at least twice fade time (%f ms)", 1000.0 * x->fade / x->sr);
        return;
    }
    x->minframes = (float)(f * 0.001 * x->sr);
}

void buffet_maxswap(t_buffet *x, double f)
{
    long newframes = (long)(f * 0.001 * x->sr);
    if (newframes <= x->minframes) {
        object_error((t_object *)x, "%s: max blocksize must exceed minimum blocksize (%f ms)", OBJECT_NAME, 1000.0 * x->minframes / x->sr);
        return;
    }

    if (newframes > x->storage_maxframes) {
        long new_bytes = (newframes + 1) * 2 * sizeof(float);
        float *new_mem = (float *)sysmem_resizeptr((void *)x->storage, new_bytes);
        if (!new_mem) {
            object_error((t_object *)x, "%s: out of memory allocating storage", OBJECT_NAME);
            return;
        }
        x->storage = new_mem;
        x->storage_maxframes = newframes;
        x->storage_bytes = new_bytes;
    }
    x->maxframes = (float)newframes;
}

void buffet_fadein(t_buffet *x, double f)
{
    if (x->sr <= 0) return;
    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    long fadeframes = (long)(f * 0.001 * x->sr);
    if (fadeframes > b_frames) fadeframes = b_frames;

    for (long i = 0, k = 0; i < fadeframes * b_nchans; i += b_nchans, k++) {
        float env = (float)k / (float)fadeframes;
        for (long j = 0; j < b_nchans; j++) {
            b_samples[i + j] *= env;
        }
    }
    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_fadeout(t_buffet *x, double f)
{
    if (x->sr <= 0) return;
    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    long fadeframes = (long)(f * 0.001 * x->sr);
    if (fadeframes > b_frames) fadeframes = b_frames;

    for (long i = (b_frames - 1) * b_nchans, k = 0; k < fadeframes; i -= b_nchans, k++) {
        float env = (float)k / (float)fadeframes;
        for (long j = 0; j < b_nchans; j++) {
            b_samples[i + j] *= env;
        }
    }
    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_internal_fadeout(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 2) {
        object_error((t_object *)x, "%s: internal_fadeout requires <start_ms> <end_ms>", OBJECT_NAME);
        return;
    }
    if (x->sr <= 0) return;

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    long startframe = (long)(0.001f * x->sr * atom_getfloatarg(0, (short)argc, argv));
    long endframe = (long)(0.001f * x->sr * atom_getfloatarg(1, (short)argc, argv));

    if (startframe < 0 || endframe > b_frames || endframe <= startframe) {
        object_error((t_object *)x, "%s: bad frame boundaries (%ld to %ld)", OBJECT_NAME, startframe, endframe);
        buffer_unlocksamples(srcbuf);
        return;
    }

    long fadeframes = endframe - startframe;
    for (long i = (endframe - 1) * b_nchans, k = 0; k < fadeframes; i -= b_nchans, k++) {
        float env = (float)k / (float)fadeframes;
        for (long j = 0; j < b_nchans; j++) {
            b_samples[i + j] *= env;
        }
    }
    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_dc_gain(t_buffet *x, double f) { x->dc_gain = (float)f; }
void buffet_dc_coef(t_buffet *x, double f) { x->dc_coef = (float)f; }

void buffet_killdc(t_buffet *x)
{
    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    if (b_nchans > MAX_CHANNELS) {
        object_error((t_object *)x, "buffer has too many channels (%ld)", b_nchans);
        buffer_unlocksamples(srcbuf);
        return;
    }

    float a0[MAX_CHANNELS] = {0};
    float a1[MAX_CHANNELS] = {0};
    float b0[MAX_CHANNELS] = {0};
    float b1[MAX_CHANNELS] = {0};
    float dc_coef = x->dc_coef;

    for (long i = 0; i < b_frames * b_nchans; i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            a0[j] = b_samples[i + j];
            b0[j] = a0[j] - a1[j] + dc_coef * b1[j];
            b_samples[i + j] = b0[j];
            b1[j] = b0[j];
            a1[j] = a0[j];
        }
    }
    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_normalize(t_buffet *x, double f)
{
    float target = (float)f;
    if (target <= 0.0f) {
        object_error((t_object *)x, "%s: normalize target must be > 0", OBJECT_NAME);
        return;
    }

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    float maxamp = 0.0f;
    for (long i = 0; i < b_frames * b_nchans; i++) {
        float test = fabsf(b_samples[i]);
        if (test > maxamp) maxamp = test;
    }

    if (maxamp < 1.0e-9f) {
        buffer_unlocksamples(srcbuf);
        return;
    }

    float rescale = target / maxamp;
    for (long i = 0; i < b_frames * b_nchans; i++) {
        b_samples[i] *= rescale;
    }

    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_normalize_selection(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc != 3) {
        object_error((t_object *)x, "%s: usage normalize_selection <target_amp> <start_ms> <end_ms>", OBJECT_NAME);
        return;
    }

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    float target = atom_getfloatarg(0, (short)argc, argv);
    long startframe = (long)(x->sr * 0.001f * atom_getfloatarg(1, (short)argc, argv));
    long endframe = (long)(x->sr * 0.001f * atom_getfloatarg(2, (short)argc, argv));

    if (startframe < 0 || endframe > b_frames || startframe >= endframe || target <= 0.0f) {
        buffer_unlocksamples(srcbuf);
        return;
    }

    float maxamp = 0.0f;
    for (long i = startframe * b_nchans; i < endframe * b_nchans; i++) {
        float test = fabsf(b_samples[i]);
        if (test > maxamp) maxamp = test;
    }

    if (maxamp > 1.0e-9f) {
        float rescale = target / maxamp;
        for (long i = startframe * b_nchans; i < endframe * b_nchans; i++) {
            b_samples[i] *= rescale;
        }
    }

    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_expenv(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc != 2) {
        object_error((t_object *)x, "%s: usage expenv <target_amp> <lambda>", OBJECT_NAME);
        return;
    }

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    double dest = atom_getfloatarg(0, (short)argc, argv);
    double lambda = atom_getfloatarg(1, (short)argc, argv);

    for (long i = 0; i < b_frames; i++) {
        double linval = dest + ((double)i / (double)b_frames);
        double curve = exp(lambda * linval);
        for (long j = 0; j < b_nchans; j++) {
            b_samples[(i * b_nchans) + j] *= (float)curve;
        }
    }

    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_spritz(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    float density = atom_getfloatarg(0, (short)argc, argv);

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    double odds = (density <= 0.0f) ? 1.0 : (density / x->sr);

    for (long i = 0; i < b_nchans * b_frames; i++) b_samples[i] = 0.0f;

    for (long j = 0; j < b_nchans; j++) {
        for (long i = 0; i < b_frames; i++) {
            if (buffet_boundrand(0.0f, 1.0f) < odds) {
                b_samples[(b_nchans * i) + j] = buffet_boundrand(-1.0f, 1.0f);
            }
        }
    }

    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_rmschunk(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 2) return;

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    long startframe = (long)(0.001f * x->sr * atom_getfloatarg(0, (short)argc, argv));
    long endframe = (long)(0.001f * x->sr * atom_getfloatarg(1, (short)argc, argv));

    if (startframe < 0 || startframe >= b_frames || endframe <= startframe || endframe > b_frames) {
        buffer_unlocksamples(srcbuf);
        return;
    }

    long bufsamps = endframe - startframe;
    float meansq = 0.0f;
    for (long i = startframe; i < endframe; i++) {
        float s = b_samples[b_nchans * i];
        meansq += s * s;
    }
    meansq /= (float)bufsamps;
    x->rmschunk = sqrtf(meansq);

    buffer_unlocksamples(srcbuf);
    outlet_float(x->floater, (double)x->rmschunk);
}

void buffet_resize(t_buffet *x, double newsize)
{
    t_atom argv;
    atom_setfloat(&argv, (float)newsize);

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *b = buffer_ref_getobject(x->src_buffer_ref);
    if (b) {
        typedmess((void *)b, gensym("size"), 1, &argv);
    } else {
        object_error((t_object *)x, "%s: no such buffer~ %s", OBJECT_NAME, x->wavename->s_name);
    }
}

void buffet_nosync_setswap(t_buffet *x)
{
    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_frames = buffer_getframecount(srcbuf);
    long swapframes = (long)buffet_boundrand(x->minframes, x->maxframes);
    if (b_frames <= swapframes * 2) return;

    long r1startframe = (long)buffet_boundrand(0.0f, (float)(b_frames - swapframes));
    long r1endframe = r1startframe + swapframes;
    long region1 = r1startframe;
    long region2 = b_frames - r1endframe;
    long r2startframe;

    if (swapframes > region1) {
        r2startframe = (long)buffet_boundrand((float)r1endframe, (float)(b_frames - swapframes));
    } else if (swapframes > region2) {
        r2startframe = (long)buffet_boundrand(0.0f, (float)(r1startframe - swapframes));
    } else {
        if (buffet_boundrand(0.0f, 1.0f) > 0.5f) {
            r2startframe = (long)buffet_boundrand(0.0f, (float)(r1startframe - swapframes));
        } else {
            r2startframe = (long)buffet_boundrand((float)r1endframe, (float)(b_frames - swapframes));
        }
    }

    if (r1startframe >= 0 && r2startframe >= 0 && (r1startframe + swapframes) <= b_frames && (r2startframe + swapframes) <= b_frames) {
        x->swapframes = swapframes;
        x->r1startframe = r1startframe;
        x->r2startframe = r2startframe;
    }
}

void buffet_swap(t_buffet *x)
{
    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    if (b_frames < x->maxframes * 2 + 1 || b_nchans > 2) {
        buffer_unlocksamples(srcbuf);
        return;
    }

    buffet_nosync_setswap(x);
    long start_sample1 = x->r1startframe * b_nchans;
    long start_sample2 = x->r2startframe * b_nchans;
    long swapframes = x->swapframes;
    float fade = x->fade;
    float *storage = x->storage;

    for (long i = 0; i < swapframes * b_nchans; i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            storage[i + j] = b_samples[start_sample1 + i + j];
        }
    }

    for (long i = 0, k = 0; i < fade * b_nchans; i += b_nchans, k++) {
        float fadephase = ((float)k / fade) * (float)PIOVERTWO;
        float fadein_gain = sinf(fadephase);
        float fadeout_gain = cosf(fadephase);
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample1 + i + j] = fadein_gain * b_samples[start_sample2 + i + j] + fadeout_gain * b_samples[start_sample1 + i + j];
        }
    }

    for (long i = (long)(fade * b_nchans); i < (long)((swapframes - fade) * b_nchans); i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample1 + i + j] = b_samples[start_sample2 + i + j];
        }
    }

    for (long i = (long)((swapframes - fade) * b_nchans), k = 0; i < swapframes * b_nchans; i += b_nchans, k++) {
        float fadephase = ((float)k / fade) * (float)PIOVERTWO;
        float fadein_gain = sinf(fadephase);
        float fadeout_gain = cosf(fadephase);
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample1 + i + j] = fadeout_gain * b_samples[start_sample2 + i + j] + fadein_gain * b_samples[start_sample1 + i + j];
        }
    }

    for (long i = 0, k = 0; i < fade * b_nchans; i += b_nchans, k++) {
        float fadein_gain = (float)k / fade;
        float fadeout_gain = 1.0f - fadein_gain;
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample2 + i + j] = fadein_gain * storage[i + j] + fadeout_gain * b_samples[start_sample2 + i + j];
        }
    }

    for (long i = (long)(fade * b_nchans); i < (long)((swapframes - fade) * b_nchans); i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample2 + i + j] = storage[i + j];
        }
    }

    for (long i = (long)((swapframes - fade) * b_nchans), k = 0; i < swapframes * b_nchans; i += b_nchans, k++) {
        float fadein_gain = (float)k / fade;
        float fadeout_gain = 1.0f - fadein_gain;
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample2 + i + j] = fadeout_gain * storage[i + j] + fadein_gain * b_samples[start_sample2 + i + j];
        }
    }

    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_specswap(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 3) return;

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    long r1startframe = (long)(x->sr * 0.001f * atom_getfloatarg(0, (short)argc, argv));
    long r2startframe = (long)(x->sr * 0.001f * atom_getfloatarg(1, (short)argc, argv));
    long swapframes = (long)(x->sr * 0.001f * atom_getfloatarg(2, (short)argc, argv));

    if (r1startframe < 0 || r2startframe < 0 || swapframes <= 0 ||
        (r1startframe + swapframes) > b_frames || (r2startframe + swapframes) > b_frames ||
        swapframes > x->storage_maxframes || b_nchans > 2) {
        buffer_unlocksamples(srcbuf);
        return;
    }

    long start_sample1 = r1startframe * b_nchans;
    long start_sample2 = r2startframe * b_nchans;
    float fade = x->fade;
    float *storage = x->storage;

    for (long i = 0; i < swapframes * b_nchans; i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            storage[i + j] = b_samples[start_sample1 + i + j];
        }
    }

    for (long i = 0, k = 0; i < fade * b_nchans; i += b_nchans, k++) {
        float fadephase = ((float)k / fade) * (float)PIOVERTWO;
        float fadein_gain = sinf(fadephase);
        float fadeout_gain = cosf(fadephase);
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample1 + i + j] = fadein_gain * b_samples[start_sample2 + i + j] + fadeout_gain * b_samples[start_sample1 + i + j];
        }
    }

    for (long i = (long)(fade * b_nchans); i < (long)((swapframes - fade) * b_nchans); i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample1 + i + j] = b_samples[start_sample2 + i + j];
        }
    }

    for (long i = (long)((swapframes - fade) * b_nchans), k = 0; i < swapframes * b_nchans; i += b_nchans, k++) {
        float fadephase = ((float)k / fade) * (float)PIOVERTWO;
        float fadein_gain = sinf(fadephase);
        float fadeout_gain = cosf(fadephase);
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample1 + i + j] = fadeout_gain * b_samples[start_sample2 + i + j] + fadein_gain * b_samples[start_sample1 + i + j];
        }
    }

    for (long i = 0, k = 0; i < fade * b_nchans; i += b_nchans, k++) {
        float fadein_gain = (float)k / fade;
        float fadeout_gain = 1.0f - fadein_gain;
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample2 + i + j] = fadein_gain * storage[i + j] + fadeout_gain * b_samples[start_sample2 + i + j];
        }
    }

    for (long i = (long)(fade * b_nchans); i < (long)((swapframes - fade) * b_nchans); i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample2 + i + j] = storage[i + j];
        }
    }

    for (long i = (long)((swapframes - fade) * b_nchans), k = 0; i < swapframes * b_nchans; i += b_nchans, k++) {
        float fadein_gain = (float)k / fade;
        float fadeout_gain = 1.0f - fadein_gain;
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample2 + i + j] = fadeout_gain * storage[i + j] + fadein_gain * b_samples[start_sample2 + i + j];
        }
    }

    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_retroblock(t_buffet *x)
{
    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    if (b_frames < x->maxframes * 2 + 1 || b_nchans > 2) {
        buffer_unlocksamples(srcbuf);
        return;
    }

    long swapframes = (long)buffet_boundrand(x->minframes, x->maxframes);
    long r1startframe = 0;

    if (x->sync <= 0.0f) {
        r1startframe = (long)buffet_boundrand(0.0f, (float)(b_frames - swapframes));
    } else {
        long syncframe = (long)(x->sync * b_frames);
        long ub1 = syncframe - CUSHION_FRAMES;
        long lb2 = ub1 + CUSHION_FRAMES;
        long ub2 = b_frames - 1;

        if (ub1 < x->maxframes && (ub2 - lb2) < x->maxframes) {
            buffer_unlocksamples(srcbuf);
            return;
        }

        if (ub1 >= x->maxframes && (ub2 - lb2) >= x->maxframes) {
            r1startframe = (buffet_boundrand(0.0f, 1.0f) > 0.5f) ?
                           (long)buffet_boundrand(0.0f, (float)(ub1 - swapframes)) :
                           (long)buffet_boundrand((float)lb2, (float)(ub2 - swapframes));
        } else if (ub1 < x->maxframes) {
            r1startframe = (long)buffet_boundrand((float)lb2, (float)(ub2 - swapframes));
        } else {
            r1startframe = (long)buffet_boundrand(0.0f, (float)(ub1 - swapframes));
        }
    }

    if (r1startframe < 0 || (r1startframe + swapframes) > b_frames) {
        buffer_unlocksamples(srcbuf);
        return;
    }

    long start_sample1 = r1startframe * b_nchans;
    float *storage = x->storage;
    float fade = x->fade;

    for (long k = 0, i = (swapframes - 1) * b_nchans; i >= 0; i -= b_nchans, k += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            storage[i + j] = b_samples[start_sample1 + k + j];
        }
    }

    for (long i = 0, k = 0; i < fade * b_nchans; i += b_nchans, k++) {
        float fadein_gain = (float)k / fade;
        float fadeout_gain = 1.0f - fadein_gain;
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample1 + i + j] = fadein_gain * storage[i + j] + fadeout_gain * b_samples[start_sample1 + i + j];
        }
    }

    for (long i = (long)(fade * b_nchans); i < (long)((swapframes - fade) * b_nchans); i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample1 + i + j] = storage[i + j];
        }
    }

    for (long i = (long)((swapframes - fade) * b_nchans), k = 0; i < swapframes * b_nchans; i += b_nchans, k++) {
        float fadein_gain = (float)k / fade;
        float fadeout_gain = 1.0f - fadein_gain;
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample1 + i + j] = fadeout_gain * storage[i + j] + fadein_gain * b_samples[start_sample1 + i + j];
        }
    }

    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_nakedswap(t_buffet *x)
{
    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) return;

    if (b_frames < x->maxframes * 2 + 1 || b_nchans != 2) {
        buffer_unlocksamples(srcbuf);
        return;
    }

    buffet_nosync_setswap(x);
    long start_sample1 = x->r1startframe * b_nchans;
    long start_sample2 = x->r2startframe * b_nchans;
    long swapframes = x->swapframes;
    float *storage = x->storage;

    for (long i = 0; i < swapframes * b_nchans; i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            storage[i + j] = b_samples[start_sample1 + i + j];
        }
    }
    for (long i = 0; i < swapframes * b_nchans; i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample1 + i + j] = b_samples[start_sample2 + i + j];
        }
    }
    for (long i = 0; i < swapframes * b_nchans; i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            b_samples[start_sample2 + i + j] = storage[i + j];
        }
    }

    buffer_unlocksamples(srcbuf);
    buffet_update(x);
}

void buffet_overdub(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 7) {
        object_error((t_object *)x, "%s usage: overdub <dubsource> <skipin> <skipout> <gain> <segdur> <fadein> <fadeout>", OBJECT_NAME);
        return;
    }

    t_symbol *dubname = atom_getsymarg(0, (short)argc, argv);
    long skipin_frames = ms2frames((long)atom_getfloatarg(1, (short)argc, argv), x->sr);
    long skipout_frames = ms2frames((long)atom_getfloatarg(2, (short)argc, argv), x->sr);
    float gain = atom_getfloatarg(3, (short)argc, argv);
    long seg_frames = ms2frames((long)atom_getfloatarg(4, (short)argc, argv), x->sr);
    long fadein_frames = ms2frames((long)atom_getfloatarg(5, (short)argc, argv), x->sr);
    long fadeout_frames = ms2frames((long)atom_getfloatarg(6, (short)argc, argv), x->sr);

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    if (!x->dub_buffer_ref) x->dub_buffer_ref = buffer_ref_new((t_object *)x, dubname);
    else buffer_ref_set(x->dub_buffer_ref, dubname);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    t_buffer_obj *dubbuf = buffer_ref_getobject(x->dub_buffer_ref);

    if (!srcbuf || !dubbuf) return;

    if (buffer_getchannelcount(dubbuf) != buffer_getchannelcount(srcbuf)) {
        object_error((t_object *)x, "%s: channel mismatch between %s and %s", OBJECT_NAME, x->wavename->s_name, dubname->s_name);
        return;
    }

    long sust_frames = seg_frames - (fadein_frames + fadeout_frames);
    if (sust_frames < 0 || (skipin_frames + seg_frames) > buffer_getframecount(dubbuf) ||
        (skipout_frames + seg_frames) > buffer_getframecount(srcbuf) ||
        skipin_frames < 0 || skipout_frames < 0 || seg_frames <= 0) {
        object_error((t_object *)x, "%s: invalid overdub time/duration parameters", OBJECT_NAME);
        return;
    }

    long b_nchans = buffer_getchannelcount(srcbuf);
    float *thisbufsamps = buffer_locksamples(srcbuf);
    float *dubsourcesamps = buffer_locksamples(dubbuf);

    if (!thisbufsamps || !dubsourcesamps) {
        if (thisbufsamps) buffer_unlocksamples(srcbuf);
        if (dubsourcesamps) buffer_unlocksamples(dubbuf);
        return;
    }

    for (long i = 0; i < fadein_frames; i++, skipin_frames++, skipout_frames++) {
        float evpval = ((float)i / (float)fadein_frames) * gain;
        for (long j = 0; j < b_nchans; j++) {
            thisbufsamps[skipout_frames * b_nchans + j] += dubsourcesamps[skipin_frames * b_nchans + j] * evpval;
        }
    }
    for (long i = 0; i < sust_frames; i++, skipin_frames++, skipout_frames++) {
        for (long j = 0; j < b_nchans; j++) {
            thisbufsamps[skipout_frames * b_nchans + j] += dubsourcesamps[skipin_frames * b_nchans + j] * gain;
        }
    }
    for (long i = 0; i < fadeout_frames; i++, skipin_frames++, skipout_frames++) {
        float evpval = ((float)(fadeout_frames - i) / (float)fadeout_frames) * gain;
        for (long j = 0; j < b_nchans; j++) {
            thisbufsamps[skipout_frames * b_nchans + j] += dubsourcesamps[skipin_frames * b_nchans + j] * evpval;
        }
    }

    buffer_unlocksamples(srcbuf);
    buffer_unlocksamples(dubbuf);
    buffet_update(x);
}

void buffet_catbuf(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 1) return;
    t_symbol *dubsource = atom_getsymarg(0, (short)argc, argv);

    if (!x->src_buffer_ref) x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    else buffer_ref_set(x->src_buffer_ref, x->wavename);

    if (!x->cat_buffer_ref) x->cat_buffer_ref = buffer_ref_new((t_object *)x, dubsource);
    else buffer_ref_set(x->cat_buffer_ref, dubsource);

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    t_buffer_obj *catbuf = buffer_ref_getobject(x->cat_buffer_ref);
    if (!srcbuf || !catbuf) return;

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    long b_cat_nchans = buffer_getchannelcount(catbuf);
    long b_cat_frames = buffer_getframecount(catbuf);

    if (b_cat_nchans != b_nchans) {
        object_error((t_object *)x, "%s: channel mismatch for catbuf", OBJECT_NAME);
        return;
    }

    float *b_samples = buffer_locksamples(srcbuf);
    float *b_cat_samples = buffer_locksamples(catbuf);
    if (!b_samples || !b_cat_samples) {
        if (b_samples) buffer_unlocksamples(srcbuf);
        if (b_cat_samples) buffer_unlocksamples(catbuf);
        return;
    }

    long sizethis = sizeof(float) * b_nchans * b_frames;
    float *storage = (float *)sysmem_newptr(sizethis);
    if (!storage) {
        buffer_unlocksamples(srcbuf);
        buffer_unlocksamples(catbuf);
        return;
    }
    sysmem_copyptr(b_samples, storage, sizethis);

    long oldframes = b_frames;
    long newsize = b_frames + b_cat_frames;
    t_atom argvv;
    atom_setfloat(&argvv, (float)newsize);

    buffer_unlocksamples(srcbuf);
    typedmess((void *)srcbuf, gensym("sizeinsamps"), 1, &argvv);

    b_samples = buffer_locksamples(srcbuf);
    if (b_samples) {
        sysmem_copyptr(storage, b_samples, sizethis);
        sysmem_copyptr(b_cat_samples, b_samples + (oldframes * b_nchans), b_cat_frames * b_cat_nchans * sizeof(float));
        buffer_unlocksamples(srcbuf);
    }

    buffer_unlocksamples(catbuf);
    sysmem_freeptr(storage);
    buffet_update(x);
}

void buffet_trimzero(t_buffet *x, t_symbol *msg, long argc, t_atom *argv)
{
    float eppy = 0.000001f;
    if (argc > 0) {
        eppy = (float)atom_getfloat(argv);
    }
    if (eppy < 0.0f || eppy > 1.0f) {
        eppy = 0.000001f;
    }

    if (!x->src_buffer_ref) {
        x->src_buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    } else {
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }

    t_buffer_obj *srcbuf = buffer_ref_getobject(x->src_buffer_ref);
    if (!srcbuf) {
        object_error((t_object *)x, "%s: invalid buffer %s", OBJECT_NAME, x->wavename ? x->wavename->s_name : "none");
        return;
    }

    long b_nchans = buffer_getchannelcount(srcbuf);
    long b_frames = buffer_getframecount(srcbuf);
    float *b_samples = buffer_locksamples(srcbuf);
    if (!b_samples) {
        return;
    }

    long frontframe = -1;
    long backframe = -1;

    // 1. Scan forward from beginning
    for (long i = 0, j = 0; i < b_frames; i++, j += b_nchans) {
        for (long k = 0; k < b_nchans; k++) {
            if (fabsf(b_samples[j + k]) > eppy) {
                frontframe = i;
                goto found_front;
            }
        }
    }
found_front:;

    // 2. Scan backward from end
    for (long i = b_frames - 1; i >= 0; i--) {
        long j = i * b_nchans;
        for (long k = 0; k < b_nchans; k++) {
            if (fabsf(b_samples[j + k]) > eppy) {
                backframe = i;
                goto found_back;
            }
        }
    }
found_back:;

    // Check if buffer is completely silent
    if (frontframe == -1 || backframe == -1) {
        post("%s: trimzero found only silence (all samples <= %f)", OBJECT_NAME, eppy);
        buffer_unlocksamples(srcbuf);
        outlet_bang(x->bang); // Fire bang so patch cords waiting on completion still fire
        return;
    }

    long newframes = 1 + (backframe - frontframe);

    // Check if there is anything to trim
    if (frontframe == 0 && backframe == (b_frames - 1)) {
        post("%s: no silence to trim at start or end of buffer", OBJECT_NAME);
        buffer_unlocksamples(srcbuf);
        outlet_bang(x->bang);
        return;
    }

    if (newframes <= 0) {
        buffer_unlocksamples(srcbuf);
        outlet_bang(x->bang);
        return;
    }

    long newsize = newframes * sizeof(float) * b_nchans;
    float *storage = (float *)sysmem_newptr(newsize);
    if (!storage) {
        object_error((t_object *)x, "%s: out of memory allocating trim storage", OBJECT_NAME);
        buffer_unlocksamples(srcbuf);
        return;
    }

    // Copy the active region to temporary storage
    for (long i = 0; i < newframes * b_nchans; i += b_nchans) {
        for (long j = 0; j < b_nchans; j++) {
            storage[i + j] = b_samples[(frontframe * b_nchans) + i + j];
        }
    }

    // Unlock before resizing the buffer object
    buffer_unlocksamples(srcbuf);

    // Resize the buffer~ in Max
    t_atom argvv;
    atom_setfloat(&argvv, (float)newframes);
    typedmess((void *)srcbuf, gensym("sizeinsamps"), 1, &argvv);

    // Re-lock and copy trimmed samples back
    b_samples = buffer_locksamples(srcbuf);
    if (b_samples) {
        sysmem_copyptr(storage, b_samples, newsize);
        buffer_unlocksamples(srcbuf);
    }
    sysmem_freeptr(storage);

    // Mark dirty and fire completion bang
    buffet_update(x);
}
