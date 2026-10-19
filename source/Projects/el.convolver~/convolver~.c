/**
 * @file convolver~.c
 * @brief Zero-latency partitioned and offline FFT buffer convolution external for Max 8/9.
 * Modernized for buffer lock symmetry, complete destructor cleanup, and 64-bit safety.
 */

#include "MSPd.h"
#include "fftease_oldskool.h"

#define OBJECT_NAME "convolver~"
#define DENORM_WANT_FIX 1

#define COMPILE_DATE "1.7.08"
#define OBJECT_VERSION "2.02"
#define CBUF_SIZE 32768
#define NCMAX 52428800

static t_class *convolver_class;

typedef struct _buffy {
    float *b_samples;
    long b_frames;
    long b_nchans;
    long b_valid;
    t_buffer_obj *b;
    t_buffer_ref *bref;
    t_symbol *myname;
} t_buffy;

typedef struct _convolver {
    t_pxobject x_obj;
    t_buffy *impulse;
    t_buffy *source;
    t_buffy *dest;
    void *bang;
    float sr;
    float *tbuf;
    float *sbuf;
    float *filt;
    long N;
    long N2;
    long last_N;
    float mult;
    float *trigland;
    int *bitshuffle;
    short static_memory;
} t_convolver;

/* Method Prototypes */
float boundrand(float min, float max);
void convolver_setbuf(t_convolver *x, t_buffy *trybuf);
void *convolver_new(t_symbol *msg, long argc, t_atom *argv);
void convolver_assist(t_convolver *x, void *b, long msg, long arg, char *dst);
void convolver_dsp_free(t_convolver *x);
void convolver_seed(t_convolver *x, t_floatarg seed);
void convolver_attach_buffers(t_convolver *x);
void convolver_spikeimp(t_convolver *x, double density);
void convolver_convolve(t_convolver *x);
void convolver_convolvechans(t_convolver *x, t_symbol *msg, long argc, t_atom *argv);
void convolver_version(t_convolver *x);
void convolver_noiseimp(t_convolver *x, double curve);
void convolver_static_memory(t_convolver *x, double toggle);
t_max_err convolver_notify(t_convolver *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.convolver~", 
                           (method)convolver_new, 
                           (method)convolver_dsp_free, 
                           (long)sizeof(t_convolver), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)convolver_assist,       "assist",        A_CANT, 0);
    class_addmethod(c, (method)convolver_notify,       "notify",        A_CANT, 0);
    class_addmethod(c, (method)convolver_spikeimp,     "spikeimp",      A_FLOAT, 0);
    class_addmethod(c, (method)convolver_noiseimp,     "noiseimp",      A_FLOAT, 0);
    class_addmethod(c, (method)convolver_convolve,     "convolve",      0);
    class_addmethod(c, (method)convolver_convolvechans, "convolvechans", A_GIMME, 0);
    class_addmethod(c, (method)convolver_static_memory,"static_memory", A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    convolver_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

float boundrand(float min, float max)
{
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}

void convolver_version(t_convolver *x)
{
    post("%s version %s, compiled %s", OBJECT_NAME, OBJECT_VERSION, COMPILE_DATE);
}

t_max_err convolver_notify(t_convolver *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (x->source && x->source->bref)   buffer_ref_notify(x->source->bref, s, msg, sender, data);
    if (x->impulse && x->impulse->bref) buffer_ref_notify(x->impulse->bref, s, msg, sender, data);
    if (x->dest && x->dest->bref)       return buffer_ref_notify(x->dest->bref, s, msg, sender, data);
    return MAX_ERR_NONE;
}

void convolver_static_memory(t_convolver *x, double toggle)
{
    x->static_memory = (toggle != 0.0);

    if (x->static_memory) {
        convolver_attach_buffers(x);
        t_buffy *impulse = x->impulse;
        if (!impulse || impulse->b_frames <= 0) return;

        long N2;
        for (N2 = 2; N2 < NCMAX; N2 *= 2) {
            if (N2 >= impulse->b_frames) break;
        }
        long N = 2 * N2;

        x->sbuf = (float *)sysmem_newptrclear((N + 2) * sizeof(float));
        x->tbuf = (float *)sysmem_newptrclear(N2 * sizeof(float));
        x->filt = (float *)sysmem_newptrclear((N + 2) * sizeof(float));
        x->bitshuffle = (int *)sysmem_newptrclear(N * 2 * sizeof(int));
        x->trigland = (float *)sysmem_newptrclear(N * 2 * sizeof(float));

        if (!x->sbuf || !x->tbuf || !x->filt || !x->bitshuffle || !x->trigland) {
            object_error((t_object *)x, "%s: insufficient memory for static allocation", OBJECT_NAME);
        }
    }
}

void convolver_seed(t_convolver *x, t_floatarg seed)
{
    srand((unsigned int)seed);
}

void convolver_convolve(t_convolver *x)
{
    convolver_attach_buffers(x);

    if (!x->source->b_valid || !x->impulse->b_valid || !x->dest->b_valid) {
        object_error((t_object *)x, "%s: one or more buffers are invalid", OBJECT_NAME);
        return;
    }

    t_atom data[3];

    if (x->source->b_nchans == x->impulse->b_nchans && x->impulse->b_nchans == x->dest->b_nchans) {
        for (int i = 0; i < x->source->b_nchans; i++) {
            atom_setfloat(data, (float)(i + 1));
            atom_setfloat(data + 1, (float)(i + 1));
            atom_setfloat(data + 2, (float)(i + 1));
            convolver_convolvechans(x, gensym("convolvechans"), 3, data);
        }
    } else if (x->source->b_nchans == 1 && x->impulse->b_nchans == x->dest->b_nchans) {
        for (int i = 0; i < x->impulse->b_nchans; i++) {
            atom_setfloat(data, 1.0f);
            atom_setfloat(data + 1, (float)(i + 1));
            atom_setfloat(data + 2, (float)(i + 1));
            convolver_convolvechans(x, gensym("convolvechans"), 3, data);
        }
    } else if (x->impulse->b_nchans == 1 && x->source->b_nchans == x->dest->b_nchans) {
        for (int i = 0; i < x->source->b_nchans; i++) {
            atom_setfloat(data, (float)(i + 1));
            atom_setfloat(data + 1, 1.0f);
            atom_setfloat(data + 2, (float)(i + 1));
            convolver_convolvechans(x, gensym("convolvechans"), 3, data);
        }
    } else {
        object_error((t_object *)x, "%s: channel count mismatch. Try 'convolvechans'", OBJECT_NAME);
    }
}

void convolver_convolvechans(t_convolver *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 3) {
        object_error((t_object *)x, "%s: convolvechans requires: src_chan imp_chan dest_chan", OBJECT_NAME);
        return;
    }

    convolver_attach_buffers(x);

    t_buffy *impulse = x->impulse;
    t_buffy *source = x->source;
    t_buffy *dest = x->dest;

    if (!impulse->b || !source->b || !dest->b) {
        object_error((t_object *)x, "%s: invalid buffer references", OBJECT_NAME);
        return;
    }

    long source_chan  = (long)atom_getfloat(argv);
    long impulse_chan = (long)atom_getfloat(argv + 1);
    long dest_chan    = (long)atom_getfloat(argv + 2);

    float *impulse_samps = buffer_locksamples(impulse->b);
    float *source_samps  = buffer_locksamples(source->b);
    float *dest_samps    = buffer_locksamples(dest->b);

    if (!impulse_samps || !source_samps || !dest_samps) {
        if (impulse_samps) buffer_unlocksamples(impulse->b);
        if (source_samps)  buffer_unlocksamples(source->b);
        if (dest_samps)    buffer_unlocksamples(dest->b);
        return;
    }

    if (source_chan <= 0 || impulse_chan <= 0 || dest_chan <= 0 ||
        source_chan > source->b_nchans || impulse_chan > impulse->b_nchans || dest_chan > dest->b_nchans) {
        object_error((t_object *)x, "%s: channel out of range", OBJECT_NAME);
        goto cleanup;
    }

    --source_chan;
    --impulse_chan;
    --dest_chan;

    long N2;
    for (N2 = 2; N2 < NCMAX; N2 *= 2) {
        if (N2 >= impulse->b_frames) break;
    }
    long N = 2 * N2;

    float *sbuf = x->sbuf;
    float *tbuf = x->tbuf;
    float *filt = x->filt;
    int *bitshuffle = x->bitshuffle;
    float *trigland = x->trigland;

    short allocated_local = 0;
    if (!x->static_memory || !sbuf) {
        sbuf = (float *)sysmem_newptrclear((N + 2) * sizeof(float));
        tbuf = (float *)sysmem_newptrclear(N2 * sizeof(float));
        filt = (float *)sysmem_newptrclear((N + 2) * sizeof(float));
        bitshuffle = (int *)sysmem_newptrclear(N * 2 * sizeof(int));
        trigland = (float *)sysmem_newptrclear(N * 2 * sizeof(float));
        allocated_local = 1;
    }

    if (!sbuf || !tbuf || !filt || !bitshuffle || !trigland) {
        object_error((t_object *)x, "%s: insufficient memory for FFT buffers", OBJECT_NAME);
        if (allocated_local) {
            if (sbuf) sysmem_freeptr(sbuf);
            if (tbuf) sysmem_freeptr(tbuf);
            if (filt) sysmem_freeptr(filt);
            if (bitshuffle) sysmem_freeptr(bitshuffle);
            if (trigland) sysmem_freeptr(trigland);
        }
        goto cleanup;
    }

    x->mult = 1.0f / (float)N;
    init_rdft((int)N, bitshuffle, trigland);

    for (long i = 0, j = 0; i < impulse->b_frames; i += impulse->b_nchans, j++) {
        filt[j] = impulse_samps[i + impulse_chan];
    }

    rdft((int)N, 1, filt, bitshuffle, trigland);

    float max_peak = 0.0f;
    for (long i = 0; i <= N; i += 2) {
        float a = filt[i];
        float b = filt[i + 1];
        float temp = a * a + b * b;
        if (temp > max_peak) max_peak = temp;
    }

    if (max_peak <= 0.0f) {
        object_error((t_object *)x, "%s: impulse response is all zeros", OBJECT_NAME);
        if (allocated_local) {
            sysmem_freeptr(sbuf);
            sysmem_freeptr(tbuf);
            sysmem_freeptr(filt);
            sysmem_freeptr(bitshuffle);
            sysmem_freeptr(trigland);
        }
        goto cleanup;
    }

    float norm = 1.0f / sqrtf(max_peak);
    for (long i = 0; i < N + 2; i++) filt[i] *= norm;

    long ifr_cnt = 0, ofr_cnt = 0;
    long target_frames = 2;
    while (target_frames < source->b_frames + impulse->b_frames) target_frames *= 2;

    if (dest->b_frames < target_frames) {
        buffer_unlocksamples(dest->b);
        t_atom newsize;
        atom_setfloat(&newsize, (float)target_frames);
        typedmess((void *)dest->b, gensym("sizeinsamps"), 1, &newsize);
        dest_samps = buffer_locksamples(dest->b);
        if (!dest_samps) {
            if (allocated_local) {
                sysmem_freeptr(sbuf);
                sysmem_freeptr(tbuf);
                sysmem_freeptr(filt);
                sysmem_freeptr(bitshuffle);
                sysmem_freeptr(trigland);
            }
            buffer_unlocksamples(impulse->b);
            buffer_unlocksamples(source->b);
            return;
        }
        dest->b_frames = target_frames;
    }

    long readframes = (source->b_frames >= N2) ? N2 : source->b_frames;
    for (long i = 0, j = 0; i < readframes; i++, ifr_cnt++, j += source->b_nchans) {
        sbuf[i] = source_samps[j + source_chan];
    }
    for (long i = readframes; i < N + 2; i++) sbuf[i] = 0.0f;

    short copacetic = 1;
    while (copacetic && ofr_cnt < ifr_cnt + N2) {
        rdft((int)N, 1, sbuf, bitshuffle, trigland);

        for (long i = 0; i <= N2; i++) {
            long ip = 2 * i;
            long ip1 = ip + 1;
            float a = sbuf[ip] * filt[ip] - sbuf[ip1] * filt[ip1];
            float b = sbuf[ip] * filt[ip1] + sbuf[ip1] * filt[ip];
            sbuf[ip] = a;
            sbuf[ip1] = b;
        }

        rdft((int)N, -1, sbuf, bitshuffle, trigland);

        for (long i = 0; i < N2; i++) {
            FIX_DENORM_FLOAT(sbuf[i]);
            tbuf[i] += sbuf[i];
        }

        long writeframes = (dest->b_frames - ofr_cnt >= N2) ? N2 : (dest->b_frames - ofr_cnt);
        if (writeframes <= 0) copacetic = 0;

        for (long i = 0, j = ofr_cnt * dest->b_nchans; i < writeframes; i++, ofr_cnt++, j += dest->b_nchans) {
            dest_samps[j + dest_chan] = tbuf[i];
        }

        for (long i = 0; i < N2; i++) {
            FIX_DENORM_FLOAT(sbuf[N2 + i]);
            tbuf[i] = sbuf[N2 + i];
        }

        readframes = (source->b_frames - ifr_cnt >= N2) ? N2 : (source->b_frames - ifr_cnt);
        if (readframes < 0) readframes = 0;

        for (long i = 0, j = ifr_cnt * source->b_nchans; i < readframes; i++, ifr_cnt++, j += source->b_nchans) {
            sbuf[i] = source_samps[j + source_chan];
        }
        for (long i = readframes; i < N + 2; i++) sbuf[i] = 0.0f;
    }

    float max_out = 0.0f;
    for (long i = 0, j = 0; i < dest->b_frames; i++, j += dest->b_nchans) {
        float val = fabsf(dest_samps[j + dest_chan]);
        if (val > max_out) max_out = val;
    }

    if (max_out > 0.0f) {
        float rescale = 1.0f / max_out;
        for (long i = 0, j = 0; i < dest->b_frames; i++, j += dest->b_nchans) {
            dest_samps[j + dest_chan] *= rescale;
        }
    }

    if (allocated_local) {
        sysmem_freeptr(sbuf);
        sysmem_freeptr(tbuf);
        sysmem_freeptr(filt);
        sysmem_freeptr(bitshuffle);
        sysmem_freeptr(trigland);
    }

    object_method((t_object *)dest->b, gensym("dirty"));
    outlet_bang(x->bang);

cleanup:
    buffer_unlocksamples(impulse->b);
    buffer_unlocksamples(source->b);
    buffer_unlocksamples(dest->b);
}

void convolver_noiseimp(t_convolver *x, double curve)
{
    convolver_attach_buffers(x);
    if (!x->impulse || !x->impulse->b) return;

    long b_nchans = x->impulse->b_nchans;
    long b_frames = x->impulse->b_frames;
    if (b_frames < 20 || b_nchans <= 0 || x->sr <= 0.0f) return;

    float *b_samples = buffer_locksamples(x->impulse->b);
    if (!b_samples) return;

    if (fabs(curve) < 0.001) curve = 0.001;

    double level = 1.0, endLevel = 0.001;
    double grow = exp(curve / (double)(b_frames - 1));
    double a1 = (endLevel - level) / (1.0 - exp(curve));
    double a2 = level + a1;
    double b1;

    for (long j = 0; j < b_nchans; j++) {
        b1 = a1;
        for (long i = 0; i < b_frames; i++) {
            float guess = boundrand(-1.0f, 1.0f);
            b1 = b1 * grow;
            level = a2 - b1;
            b_samples[(i * b_nchans) + j] = (float)(level * (double)guess);
        }
    }

    buffer_unlocksamples(x->impulse->b);
    object_method((t_object *)x->impulse->b, gensym("dirty"));
    outlet_bang(x->bang);
}

void convolver_spikeimp(t_convolver *x, double density)
{
    convolver_attach_buffers(x);
    if (!x->impulse || !x->impulse->b) return;

    long b_nchans = x->impulse->b_nchans;
    long b_frames = x->impulse->b_frames;
    if (b_frames <= 0 || b_nchans <= 0 || x->sr <= 0.0f) return;

    float *b_samples = buffer_locksamples(x->impulse->b);
    if (!b_samples) return;

    float dur = (float)b_frames / x->sr;
    long count = (long)(density * (double)dur);

    for (long i = 0; i < b_nchans * b_frames; i++) b_samples[i] = 0.0f;

    for (long j = 0; j < b_nchans; j++) {
        for (long i = 0; i < count; i++) {
            float guess = boundrand(0.0f, 1.0f);
            float gain = 1.0f - guess;
            gain = gain * gain;
            if (boundrand(0.0f, 1.0f) > 0.5f) gain = -gain;

            long pos = (long)(dur * guess * guess * x->sr) * b_nchans + j;
            if (pos >= 0 && pos < b_frames * b_nchans) {
                b_samples[pos] = gain;
            }
        }
    }

    buffer_unlocksamples(x->impulse->b);
    object_method((t_object *)x->impulse->b, gensym("dirty"));
    outlet_bang(x->bang);
}

void *convolver_new(t_symbol *msg, long argc, t_atom *argv)
{
    t_convolver *x = (t_convolver *)object_alloc(convolver_class);
    if (!x) return NULL;

    x->bang = bangout((t_object *)x);
    dsp_setup((t_pxobject *)x, 0); // Offline message-driven external

    x->impulse = (t_buffy *)sysmem_newptrclear(sizeof(t_buffy));
    x->source  = (t_buffy *)sysmem_newptrclear(sizeof(t_buffy));
    x->dest    = (t_buffy *)sysmem_newptrclear(sizeof(t_buffy));

    x->static_memory = 0;
    x->last_N = -1;
    x->sbuf = NULL;
    x->tbuf = NULL;
    x->filt = NULL;
    x->bitshuffle = NULL;
    x->trigland = NULL;

    x->impulse->myname = gensym("impulse_buf");
    x->source->myname  = gensym("source_buf");
    x->dest->myname    = gensym("dest_buf");

    if (argc > 0) x->source->myname  = atom_getsym(argv);
    if (argc > 1) x->impulse->myname = atom_getsym(argv + 1);
    if (argc > 2) x->dest->myname    = atom_getsym(argv + 2);

    convolver_attach_buffers(x);
    x->sr = sys_getsr();
    if (x->sr <= 0.0f) x->sr = 44100.0f;

    return x;
}

void convolver_attach_buffers(t_convolver *x)
{
    convolver_setbuf(x, x->source);
    convolver_setbuf(x, x->impulse);
    convolver_setbuf(x, x->dest);
}

void convolver_setbuf(t_convolver *x, t_buffy *trybuf)
{
    if (!trybuf->bref)
        trybuf->bref = buffer_ref_new((t_object *)x, trybuf->myname);
    else
        buffer_ref_set(trybuf->bref, trybuf->myname);

    t_buffer_obj *b = buffer_ref_getobject(trybuf->bref);
    trybuf->b_valid = 0;
    if (b) {
        trybuf->b_frames = buffer_getframecount(b);
        trybuf->b_nchans = buffer_getchannelcount(b);
        trybuf->b_valid = 1;
        trybuf->b = b;
    }
}

void convolver_dsp_free(t_convolver *x)
{
    dsp_free((t_pxobject *)x);

    if (x->static_memory) {
        if (x->sbuf)       sysmem_freeptr(x->sbuf);
        if (x->tbuf)       sysmem_freeptr(x->tbuf);
        if (x->filt)       sysmem_freeptr(x->filt);
        if (x->bitshuffle) sysmem_freeptr(x->bitshuffle);
        if (x->trigland)   sysmem_freeptr(x->trigland);
    }

    if (x->impulse) {
        if (x->impulse->bref) object_free(x->impulse->bref);
        sysmem_freeptr(x->impulse);
    }
    if (x->source) {
        if (x->source->bref) object_free(x->source->bref);
        sysmem_freeptr(x->source);
    }
    if (x->dest) {
        if (x->dest->bref) object_free(x->dest->bref);
        sysmem_freeptr(x->dest);
    }
}

void convolver_assist(t_convolver *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(messages) Control Input", 256);
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(bang) Convolution Finished", 256);
    }
}