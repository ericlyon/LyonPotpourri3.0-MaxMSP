/**
 * @file splitbank~.c
 * @brief Spectral bin splitter with multi-channel oscillator-bank resynthesis for Max 8/9.
 * Modernized for 64-bit dsp64, memory safety, bounds-checked state handling,
 * and thread-safe buffer management.
 */

#include "MSPd.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif
#ifndef TWOPI
#define TWOPI 6.283185307179586
#endif
#ifndef PIOVERTWO
#define PIOVERTWO 1.5707963267948966
#endif

#define OSCBANK_TABLE_LENGTH (8192)
#define OSCBANK_DEFAULT_TOPFREQ (15000.0)

#define MAXSTORE (1024)

#define OBJECT_NAME "splitbank~"

// from fftease.h
#define BIGGER_THAN_MSP_VECTOR 0
#define SMALLER_THAN_MSP_VECTOR 1
#define EQUAL_TO_MSP_VECTOR 2
#define INVALID_VECTOR_RELATION (-1)

#define FFTEASE_DEFAULT_OVERLAP 8

#define MAX_FFTSIZE 16384
#define MAX_D (MAX_FFTSIZE / FFTEASE_DEFAULT_OVERLAP)

#define MAX_CHANNELS 64
#define MIN_OVERLAP 2
#define MAX_OVERLAP 64
#define MAX_TRANSPOSE 1000.0
#define DEFAULT_LOFREQ 0.0
#define DEFAULT_HIFREQ 15000.0

#define SB_FREE(p) do { if (p) { sysmem_freeptr(p); (p) = NULL; } } while (0)

static t_class *splitbank_class;

typedef struct
{
    int R;
    int    N;
    int    N2;
    int    Nw;
    int    Nw2;
    int    vector_size;
    int    i;
    int    in_count;
    t_double *Wanal;
    t_double *Wsyn;
    t_double *Hwin;
    t_double *complex_spectrum;
    t_double *interleaved_spectrum;
    t_double *input_buffer;
    t_double *output_buffer;
    // for convert
    t_double *c_lastphase_in;
    t_double *c_lastphase_out;
    t_double c_fundamental;
    t_double c_factor_in;
    t_double c_factor_out;
    t_double P;
    t_double table_length;
    t_double table_si;
    int first;
    t_double i_vector_size;
    t_double *lastamp;
    t_double *lastfreq;
    t_double *index;
    t_double *table;
    t_double pitch_increment;
    int lo_bin;
    int hi_bin;
    t_double synthesis_threshold;
    int overlap;
    int winfac;
    t_double user_lofreq;
    t_double user_hifreq;
    t_double curfreq;
    t_double mult;
    t_double *trigland;
    int *bitshuffle;
    t_double *internalInputVector; // hold input data from smaller MSP buffers
    t_double *internalOutputVector; // hold output data for smaller MSP buffers
} t_oscbank;

typedef struct _splitbank
{
    t_pxobject x_obj;
    t_oscbank **obanks;
    int N;
    int Nw;
    int N2;
    int R;
    int D;                  // hop size == N / overlap
    int overlap;
    void *list_outlet;
    t_atom *list_data;
    int *bin_tmp;
    int vector_size;
    int table_offset;
    int bin_offset;
    t_double *last_mag;
    t_double *current_mag;
    int *last_binsplit;
    int *current_binsplit;
    int **stored_binsplits;
    short *stored_slots;
    t_double *in_amps;
    short new_distribution;
    short interpolation_completed;
    short manual_override;
    t_double manual_control_value;
    short mute;
    short powerfade;
    int channel_count;
    long countdown_samps;   // samps for a given fadetime
    long counter;
    int hopsamps;
    t_double frac;          // how far along we are in the interpolation
    t_double ramp_sec;      // user ramp time, kept in seconds so it survives sample-rate changes
    t_double lo_freq;       // persistent resynthesis band limits
    t_double hi_freq;
    int operationRepeat;    // how many times to do whatever on each perform call
    int operationCount;     // keep track of where we are in buffer operation
    int bufferStatus;       // relation between MSP vector size and hop size
    t_systhread_mutex lock; // guards oscbank state vs. dsp64 re-initialization
} t_splitbank;

void *splitbank_new(t_symbol *s, long argc, t_atom *argv);
void splitbank_assist(t_splitbank *x, void *b, long msg, long arg, char *dst);
void splitbank_showstate(t_splitbank *x);
void splitbank_manual_override(t_splitbank *x, double toggle);
void splitbank_setstate(t_splitbank *x, t_symbol *msg, long argc, t_atom *argv);
void splitbank_ramptime(t_splitbank *x, t_symbol *msg, long argc, t_atom *argv);
int rand_index(int max);
void splitbank_scramble(t_splitbank *x);
void splitbank_store(t_splitbank *x, double location);
void splitbank_recall(t_splitbank *x, double location);
void splitbank_powerfade(t_splitbank *x, double toggle);
void splitbank_maxfreq(t_splitbank *x, double freq);
void splitbank_minfreq(t_splitbank *x, double freq);
void splitbank_mute(t_splitbank *x, double toggle);
void splitbank_fftinfo(t_splitbank *x);
void splitbank_free(t_splitbank *x);
void splitbank_overlap(t_splitbank *x, double ofac);
void splitbank_spliti(t_splitbank *x, t_double *dest_mag, int start, int end, t_double oldfrac);
void splitbank_split(t_splitbank *x, int *binsplit, t_double *dest_mag, int start, int end);
int splitbank_closestPowerOfTwo(int p);
void splitbank_dsp64(t_splitbank *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void splitbank_perform64(t_splitbank *x, t_object *dsp64, double **ins,
                         long numins, double **outs,
                         long numouts, long vectorsize,
                         long flags, void *userparam);
void do_splitbank(t_splitbank *x);

void fftease_obank_analyze(t_oscbank *x);
int fftease_obank_initialize(t_oscbank *x, t_double lo_freq, t_double hi_freq, int overlap,
                             int R, int vector_size, int N, int first);
void fftease_obank_synthesize(t_oscbank *x);
void fftease_obank_destroy(t_oscbank *x);
void fftease_obank_topfreq(t_oscbank *x, t_double topfreq);
void fftease_obank_bottomfreq(t_oscbank *x, t_double bottomfreq);

void fold(t_double *I, t_double *W, int Nw, t_double *O, int N, int n);
void init_rdft(int n, int *ip, t_double *w);
void rdft(int n, int isgn, t_double *a, int *ip, t_double *w);
void bitrv2(int n, int *ip, t_double *a);
void cftsub(int n, t_double *a, t_double *w);
void rftsub(int n, t_double *a, int nc, t_double *c);
void makewt(int nw, int *ip, t_double *w);
void makect(int nc, int *ip, t_double *c);
void makehanning(t_double *H, t_double *A, t_double *S, int Nw, int N, int I, int odd);
void convert(t_double *S, t_double *C, int N2, t_double *lastphase, t_double fundamental, t_double factor);

/* ---------------------------------------------------------------- helpers */

// NaN -> 0, then clamp into [lo, hi]
static inline t_double splitbank_clip(t_double v, t_double lo, t_double hi)
{
    if (v != v)
        v = 0.0;
    if (v < lo)
        v = lo;
    if (v > hi)
        v = hi;
    return v;
}

// Call after current_binsplit has been replaced (last_binsplit must already hold the old
// distribution). The audio thread consumes new_distribution and restarts the ramp itself.
static void splitbank_begin_transition(t_splitbank *x)
{
    int i;
    if (x->countdown_samps <= 0) { // ramp off: no morph, last == current
        for (i = 0; i < x->N2; i++)
            x->last_binsplit[i] = x->current_binsplit[i];
    }
    x->new_distribution = 1;
}

// Moves one hop of synthesized output to dest, slides the output buffer, advances in_count.
static void splitbank_emit(t_oscbank *b, t_double *dest, int hop, int Nw, t_double mult)
{
    int j;
    t_double *ob = b->output_buffer;
    for (j = 0; j < hop; j++)
        dest[j] = ob[j] * mult;
    memmove(ob, ob + hop, (size_t)(Nw - hop) * sizeof(t_double));
    memset(ob + (Nw - hop), 0, (size_t)hop * sizeof(t_double));
    b->in_count = (b->in_count + hop) % Nw;
}

/* ------------------------------------------------------------------- main */

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.splitbank~", (method)splitbank_new, (method)splitbank_free,
                  sizeof(t_splitbank), 0L, A_GIMME, 0);

    class_addmethod(c, (method)splitbank_assist,         "assist",          A_CANT, 0);
    class_addmethod(c, (method)splitbank_mute,           "mute",            A_FLOAT, 0);
    class_addmethod(c, (method)splitbank_dsp64,          "dsp64",           A_CANT, 0);
    class_addmethod(c, (method)splitbank_showstate,      "showstate",       A_NOTHING, 0);
    class_addmethod(c, (method)splitbank_manual_override,"manual_override", A_FLOAT, 0);
    class_addmethod(c, (method)splitbank_store,          "store",           A_FLOAT, 0);
    class_addmethod(c, (method)splitbank_recall,         "recall",          A_FLOAT, 0);
    class_addmethod(c, (method)splitbank_setstate,       "setstate",        A_GIMME, 0);
    class_addmethod(c, (method)splitbank_ramptime,       "ramptime",        A_GIMME, 0);
    class_addmethod(c, (method)splitbank_powerfade,      "powerfade",       A_FLOAT, 0);
    class_addmethod(c, (method)splitbank_minfreq,        "minfreq",         A_FLOAT, 0);
    class_addmethod(c, (method)splitbank_maxfreq,        "maxfreq",         A_FLOAT, 0);
    class_addmethod(c, (method)splitbank_overlap,        "overlap",         A_FLOAT, 0);
    class_addmethod(c, (method)splitbank_scramble,       "scramble",        A_NOTHING, 0);
    class_addmethod(c, (method)splitbank_fftinfo,        "fftinfo",         A_NOTHING, 0);
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    splitbank_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

/* --------------------------------------------------------- simple messages */

void splitbank_overlap(t_splitbank *x, double ofac)
{
    // takes effect the next time the DSP chain is compiled
    if (!(ofac >= MIN_OVERLAP))
        x->overlap = MIN_OVERLAP;
    else if (ofac > MAX_OVERLAP)
        x->overlap = MAX_OVERLAP;
    else
        x->overlap = splitbank_closestPowerOfTwo((int)ofac);
}

void splitbank_powerfade(t_splitbank *x, double toggle)
{
    x->powerfade = (toggle != 0.0);
}

void splitbank_mute(t_splitbank *x, double toggle)
{
    x->mute = (toggle != 0.0);
}

void splitbank_manual_override(t_splitbank *x, double toggle)
{
    x->manual_override = (toggle != 0.0);
}

void splitbank_fftinfo(t_splitbank *x)
{
    object_post((t_object *)x, "FFT size: %d", x->N);
    object_post((t_object *)x, "Overlap Factor: %d", x->overlap);
    object_post((t_object *)x, "Hop size: %d", x->D);
}

void splitbank_maxfreq(t_splitbank *x, double freq)
{
    int i;
    x->hi_freq = freq;   // persists across DSP restarts
    for (i = 0; i < x->channel_count; i++) {
        fftease_obank_topfreq(x->obanks[i], freq);
    }
}

void splitbank_minfreq(t_splitbank *x, double freq)
{
    int i;
    x->lo_freq = freq;
    for (i = 0; i < x->channel_count; i++) {
        fftease_obank_bottomfreq(x->obanks[i], freq);
    }
}

/* -------------------------------------------------------------- lifecycle */

int splitbank_closestPowerOfTwo(int p)
{
    int base = 2;
    while (base < p && base < (1 << 20))   // bounded: no overflow on absurd input
        base *= 2;
    return base;
}

int rand_index(int max)
{
    if (max <= 0)
        return 0;
    return rand() % max;
}

void *splitbank_new(t_symbol *s, long argc, t_atom *argv)
{
    static int seeded = 0;
    t_splitbank *x = (t_splitbank *)object_alloc(splitbank_class);
    int i;
    int max_N2 = MAX_FFTSIZE / 2;
    double cc;

    if (!x)
        return NULL;

    cc = atom_getfloatarg(0, argc, argv);
    if (!(cc >= 2.0))
        cc = 2.0;
    if (cc > MAX_CHANNELS) {
        object_error((t_object *)x, "maximum of %d channels; using %d", MAX_CHANNELS, MAX_CHANNELS);
        cc = MAX_CHANNELS;
    }
    x->channel_count = splitbank_closestPowerOfTwo((int)cc);

    // The FFT size (second argument) is not user-configurable yet; it is hard coded.
    x->R = 48000; // temporary until the first dsp64 call, to avoid divide by zero in initialization
    x->N = 1024;
    x->Nw = x->N;
    x->N2 = x->N / 2;
    x->overlap = FFTEASE_DEFAULT_OVERLAP;
    x->D = x->N / x->overlap;
    x->hopsamps = x->D;
    x->ramp_sec = 1.0;
    x->lo_freq = DEFAULT_LOFREQ;
    x->hi_freq = DEFAULT_HIFREQ;
    x->bufferStatus = INVALID_VECTOR_RELATION;

    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }
    systhread_mutex_new(&x->lock, SYSTHREAD_MUTEX_NORMAL);

    dsp_setup((t_pxobject *)x, x->channel_count + 5);
    x->list_outlet = (void *)listout((void *)x);
    for (i = 0; i < x->channel_count + 1; i++) {
        outlet_new((t_object *)x, "signal");
    }
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->obanks = (t_oscbank **)sysmem_newptrclear(x->channel_count * sizeof(t_oscbank *));
    x->list_data = (t_atom *)sysmem_newptrclear((MAX_FFTSIZE + 2) * sizeof(t_atom));
    x->last_binsplit = (int *)sysmem_newptrclear(max_N2 * sizeof(int));
    x->current_binsplit = (int *)sysmem_newptrclear(max_N2 * sizeof(int));
    x->bin_tmp = (int *)sysmem_newptrclear(max_N2 * sizeof(int));
    x->last_mag = (t_double *)sysmem_newptrclear(max_N2 * sizeof(t_double));
    x->current_mag = (t_double *)sysmem_newptrclear(max_N2 * sizeof(t_double));
    x->stored_slots = (short *)sysmem_newptrclear(MAXSTORE * sizeof(short));
    x->stored_binsplits = (int **)sysmem_newptrclear(MAXSTORE * sizeof(int *));
    x->in_amps = (t_double *)sysmem_newptrclear((MAX_FFTSIZE + 2) * sizeof(t_double));
    if (!x->obanks || !x->list_data || !x->last_binsplit || !x->current_binsplit
        || !x->bin_tmp || !x->last_mag || !x->current_mag || !x->stored_slots
        || !x->stored_binsplits || !x->in_amps)
        goto fail;

    // FFT size is fixed, so each stored slot only needs N2 entries
    for (i = 0; i < MAXSTORE; i++) {
        x->stored_binsplits[i] = (int *)sysmem_newptrclear(x->N2 * sizeof(int));
        if (!x->stored_binsplits[i])
            goto fail;
    }
    for (i = 0; i < x->channel_count; i++) {
        x->obanks[i] = (t_oscbank *)sysmem_newptrclear(sizeof(t_oscbank));
        if (!x->obanks[i]
            || fftease_obank_initialize(x->obanks[i], x->lo_freq, x->hi_freq, x->overlap,
                                        x->R, x->D, x->N, 1))
            goto fail;
    }

    splitbank_scramble(x);
    return x;

fail:
    object_error((t_object *)x, "out of memory");
    object_free((t_object *)x);   // _free tolerates NULL members
    return NULL;
}

void splitbank_free(t_splitbank *x)
{
    int i;

    dsp_free((t_pxobject *)x);          // first: detach from audio thread
    if (x->lock) {
        systhread_mutex_lock(x->lock);  // wait out any in-flight perform
        systhread_mutex_unlock(x->lock);
    }
    if (x->obanks) {
        for (i = 0; i < x->channel_count; i++) {
            if (x->obanks[i]) {
                fftease_obank_destroy(x->obanks[i]);
                sysmem_freeptr(x->obanks[i]);
            }
        }
        SB_FREE(x->obanks);
    }
    if (x->stored_binsplits) {
        for (i = 0; i < MAXSTORE; i++) {
            SB_FREE(x->stored_binsplits[i]);
        }
        SB_FREE(x->stored_binsplits);
    }
    SB_FREE(x->list_data);
    SB_FREE(x->current_binsplit);
    SB_FREE(x->last_binsplit);
    SB_FREE(x->current_mag);
    SB_FREE(x->last_mag);
    SB_FREE(x->bin_tmp);
    SB_FREE(x->stored_slots);
    SB_FREE(x->in_amps);
    if (x->lock) {
        systhread_mutex_free(x->lock);
        x->lock = NULL;
    }
}

void splitbank_assist(t_splitbank *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        if (arg == 0) {
            snprintf(dst, 512, "(signal) Sound Input");
        } else if (arg > 0 && arg <= x->channel_count) {
            snprintf(dst, 512, "(signal) Transposition %ld", arg);
        } else if (arg == x->channel_count + 1) {
            snprintf(dst, 512, "(signal) Synthesis Threshold");
        } else if (arg == x->channel_count + 2) {
            snprintf(dst, 512, "(signal) Table Offset");
        } else if (arg == x->channel_count + 3) {
            snprintf(dst, 512, "(signal) Bin Offset");
        } else if (arg == x->channel_count + 4) {
            snprintf(dst, 512, "(signal) Manual Control");
        }
    } else if (msg == ASSIST_OUTLET) {
        if (arg < x->channel_count) {
            snprintf(dst, 512, "(signal) Spectrum Segment %ld", arg + 1);
        } else if (arg == x->channel_count) {
            snprintf(dst, 512, "(signal) Ramp Sync");
        } else {
            snprintf(dst, 512, "(list) Current Bin State");
        }
    }
}

/* ----------------------------------------------------- bin-state messages */

void splitbank_store(t_splitbank *x, double loc)
{
    int location;
    int i;

    if (!(loc >= 0.0 && loc <= MAXSTORE - 1)) {
        object_error((t_object *)x, "location must be between 0 and %d, but was %f", MAXSTORE - 1, loc);
        return;
    }
    location = (int)loc;
    for (i = 0; i < x->N2; i++) {
        x->stored_binsplits[location][i] = x->current_binsplit[i];
    }
    x->stored_slots[location] = 1;
}

void splitbank_recall(t_splitbank *x, double loc)
{
    int location;
    int i;

    if (!(loc >= 0.0 && loc <= MAXSTORE - 1)) {
        object_error((t_object *)x, "location must be between 0 and %d, but was %f", MAXSTORE - 1, loc);
        return;
    }
    location = (int)loc;
    if (!x->stored_slots[location]) {
        object_error((t_object *)x, "nothing stored at location %d", location);
        return;
    }
    for (i = 0; i < x->N2; i++) {
        x->last_binsplit[i] = x->current_binsplit[i];
        x->current_binsplit[i] = x->stored_binsplits[location][i];
    }
    splitbank_begin_transition(x);
}

void splitbank_setstate(t_splitbank *x, t_symbol *msg, long argc, t_atom *argv)
{
    long i, v;

    if (argc != x->N2) {
        object_error((t_object *)x, "list must be of length %d, but actually was %ld", x->N2, argc);
        return;
    }
    // validate everything first: out-of-range bin numbers would index outside the spectrum
    for (i = 0; i < argc; i++) {
        v = atom_getlong(argv + i);
        if (v < 0 || v >= x->N2) {
            object_error((t_object *)x, "bin number %ld at position %ld is out of range 0 - %d",
                         v, i, x->N2 - 1);
            return;
        }
    }
    for (i = 0; i < argc; i++) {
        x->last_binsplit[i] = x->current_binsplit[i];
        x->current_binsplit[i] = (int)atom_getlong(argv + i);
    }
    splitbank_begin_transition(x);
}

void splitbank_scramble(t_splitbank *x)
{
    int i, j, tmp;
    int max = x->N2;
    int *bin_tmp = x->bin_tmp;

    // Fisher-Yates: each bin number occurs exactly once, in a random location
    for (i = 0; i < max; i++) {
        bin_tmp[i] = i;
    }
    for (i = max - 1; i > 0; i--) {
        j = rand_index(i + 1);
        tmp = bin_tmp[i];
        bin_tmp[i] = bin_tmp[j];
        bin_tmp[j] = tmp;
    }
    for (i = 0; i < max; i++) {
        x->last_binsplit[i] = x->current_binsplit[i];
        x->current_binsplit[i] = bin_tmp[i];
    }
    splitbank_begin_transition(x);
}

void splitbank_ramptime(t_splitbank *x, t_symbol *msg, long argc, t_atom *argv)
{
    double ms = atom_getfloatarg(0, argc, argv);

    if (!(ms >= 0.0))      // also catches NaN
        ms = 0.0;
    if (ms > 600000.0)     // 10 minutes
        ms = 600000.0;
    x->ramp_sec = ms * 0.001;
    x->countdown_samps = (long)(x->ramp_sec * x->R);
}

// REPORT CURRENT SHUFFLE STATUS
void splitbank_showstate(t_splitbank *x)
{
    t_atom *list_data = x->list_data;
    int i;

    for (i = 0; i < x->N2; i++) {
        atom_setlong(list_data + i, x->current_binsplit[i]);
    }
    outlet_list(x->list_outlet, 0L, x->N2, list_data);
}

/* ------------------------------------------------------ bin distribution */

void splitbank_split(t_splitbank *x, int *binsplit, t_double *dest_mag, int start, int end)
{
    int i;
    int bindex;
    int n = x->N2;
    t_double *in_amps = x->in_amps;
    int table_offset = x->table_offset;
    int bin_offset = x->bin_offset;

    if (table_offset < 0)
        table_offset = -table_offset;
    if (bin_offset < 0)
        bin_offset = -bin_offset;

    for (i = start; i < end; i++) {
        bindex = binsplit[(i + table_offset) % n];
        bindex = (bindex + bin_offset) % n;
        dest_mag[bindex * 2] = in_amps[bindex]; // putting amps into interleaved spectrum
    }
}

void splitbank_spliti(t_splitbank *x, t_double *dest_mag, int start, int end, t_double oldfrac)
{
    int i;
    int bindex;
    int *current_binsplit = x->current_binsplit;
    int *last_binsplit = x->last_binsplit;
    t_double *current_mag = x->current_mag;
    t_double *last_mag = x->last_mag;
    t_double *in_amps = x->in_amps;
    int bin_offset = x->bin_offset;
    int table_offset = x->table_offset;
    int n = x->N2;
    t_double newfrac;
    t_double phase;

    oldfrac = splitbank_clip(oldfrac, 0.0, 1.0);

    if (x->powerfade) {
        phase = oldfrac * PIOVERTWO;
        oldfrac = sin(phase);
        newfrac = cos(phase);
    } else {
        newfrac = 1.0 - oldfrac;
    }

    if (table_offset < 0)
        table_offset = -table_offset;
    if (bin_offset < 0)
        bin_offset = -bin_offset;

    for (i = 0; i < n; i++) {
        last_mag[i] = current_mag[i] = 0.0;
    }

    for (i = start; i < end; i++) {
        bindex = current_binsplit[(i + table_offset) % n];
        bindex = (bindex + bin_offset) % n;
        current_mag[bindex] = in_amps[bindex];

        bindex = last_binsplit[(i + table_offset) % n];
        bindex = (bindex + bin_offset) % n;
        last_mag[bindex] = in_amps[bindex];
    }
    for (i = 0; i < n; i++) {
        if (!current_mag[i] && !last_mag[i]) {
            dest_mag[i * 2] = 0.0;
        } else if (current_mag[i] && last_mag[i]) {
            dest_mag[i * 2] = current_mag[i];
        } else if (current_mag[i] && !last_mag[i]) {
            dest_mag[i * 2] = newfrac * current_mag[i];
        } else {
            dest_mag[i * 2] = oldfrac * last_mag[i];
        }
    }
}

/* -------------------------------------------------------------------- DSP */

void do_splitbank(t_splitbank *x)
{
    int i, j;
    int N = x->N;
    int N2 = x->N2;
    int channel_count = x->channel_count;
    long countdown_samps = x->countdown_samps;
    t_oscbank **obanks = x->obanks;
    t_double *in_amps = x->in_amps;
    t_double frac;

    fftease_obank_analyze(obanks[0]);

    // copy input amplitudes from analyzed frame
    for (i = 0, j = 0; i < N; i += 2, j++) {
        in_amps[j] = obanks[0]->interleaved_spectrum[i];
    }

    // zero the amps next
    for (i = 0; i < channel_count; i++) {
        for (j = 0; j < N; j += 2) {
            obanks[i]->interleaved_spectrum[j] = 0.0;
        }
    }

    if (x->manual_override) {
        for (i = 0; i < channel_count; i++) {
            splitbank_spliti(x, obanks[i]->interleaved_spectrum,
                             N2 * i / channel_count, N2 * (i + 1) / channel_count,
                             x->manual_control_value);
        }
        frac = x->manual_control_value;
    }
    else if (x->new_distribution) {
        x->new_distribution = 0;
        x->counter = 0;                 // (re)start the ramp here, on the audio thread
        x->interpolation_completed = 0;
        for (i = 0; i < channel_count; i++) {
            splitbank_split(x, x->last_binsplit, obanks[i]->interleaved_spectrum,
                            N2 * i / channel_count, N2 * (i + 1) / channel_count);
        }
        frac = 0.0;
    }
    else if (x->interpolation_completed || countdown_samps <= 0) {
        for (i = 0; i < channel_count; i++) {
            splitbank_split(x, x->current_binsplit, obanks[i]->interleaved_spectrum,
                            N2 * i / channel_count, N2 * (i + 1) / channel_count);
        }
        frac = 1.0;
    } else {
        frac = splitbank_clip((t_double)x->counter / (t_double)countdown_samps, 0.0, 1.0);
        for (i = 0; i < channel_count; i++) {
            splitbank_spliti(x, obanks[i]->interleaved_spectrum,
                             N2 * i / channel_count, N2 * (i + 1) / channel_count, 1.0 - frac);
        }
        x->counter += x->hopsamps;
        if (x->counter >= countdown_samps) {
            x->counter = countdown_samps;
            x->interpolation_completed = 1;
        }
    }

    // copy frequency information to other banks
    for (i = 1; i < channel_count; i++) {
        for (j = 1; j < N; j += 2) {
            obanks[i]->interleaved_spectrum[j] = obanks[0]->interleaved_spectrum[j];
        }
    }

    // SYNTHESIS
    for (i = 0; i < channel_count; i++) {
        fftease_obank_synthesize(obanks[i]);
    }
    x->frac = frac;
}

void splitbank_perform64(t_splitbank *x, t_object *dsp64, double **ins,
                         long numins, double **outs,
                         long numouts, long n,
                         long flags, void *userparam)
{
    int i, j, k;
    int channel_count = x->channel_count;
    t_double *synthesis_threshold;
    t_double *t_offset;
    t_double *b_offset;
    t_double *manual_control;
    t_double *sync;
    int N2, Nw, D;
    long vector_size = n;
    t_oscbank **obanks = x->obanks;
    t_double mult;
    t_double *MSPInputVector;
    t_double *input_buffer;
    t_double *internalInputVector;
    int operationRepeat, operationCount;
    double p;

    // Silence if muted, the vector relation is unusable, the signal vector size differs from the
    // one dsp64 planned for, or the main thread is re-initializing (never block).
    if (x->mute || x->bufferStatus == INVALID_VECTOR_RELATION || n != x->vector_size
        || systhread_mutex_trylock(x->lock)) {
        for (i = 0; i < channel_count + 1; i++) {
            t_double *outlet = outs[i];
            for (j = 0; j < n; j++) {
                outlet[j] = 0.0;
            }
        }
        return;
    }

    N2 = x->N2;
    Nw = x->Nw;
    D = x->D;
    operationRepeat = x->operationRepeat;
    operationCount = x->operationCount;
    mult = obanks[0]->mult;

    /** ASSIGN INLETS AND OUTLETS **/

    MSPInputVector = ins[0];
    synthesis_threshold = ins[channel_count + 1]; // these come after channel_count transposition inlets
    t_offset = ins[channel_count + 2];
    b_offset = ins[channel_count + 3];
    manual_control = ins[channel_count + 4];
    sync = outs[channel_count];                   // sync is the last signal outlet

    /* ACQUIRE PARAMETERS (sanitized: NaN/inf must never reach the oscillators or index math) */

    for (i = 0; i < channel_count; i++) {
        p = splitbank_clip(ins[i + 1][0], -MAX_TRANSPOSE, MAX_TRANSPOSE);
        obanks[i]->pitch_increment = p * obanks[i]->table_si;
        obanks[i]->synthesis_threshold = splitbank_clip(synthesis_threshold[0], -1e6, 1e6);
    }
    x->table_offset = (int)(splitbank_clip(t_offset[0], -1e4, 1e4) * N2);
    x->bin_offset = (int)(splitbank_clip(b_offset[0], -1e4, 1e4) * N2);
    x->manual_control_value = splitbank_clip(manual_control[0], 0.0, 1.0);

    /* BEGIN DSP LOOP */

    input_buffer = obanks[0]->input_buffer;

    if (x->bufferStatus == EQUAL_TO_MSP_VECTOR) {
        memmove(input_buffer, input_buffer + vector_size, (size_t)(Nw - vector_size) * sizeof(t_double));
        memcpy(input_buffer + (Nw - vector_size), MSPInputVector, (size_t)vector_size * sizeof(t_double));

        do_splitbank(x);

        for (i = 0; i < channel_count; i++) {
            splitbank_emit(obanks[i], outs[i], (int)vector_size, Nw, mult);
        }
    }
    else if (x->bufferStatus == SMALLER_THAN_MSP_VECTOR) {
        for (k = 0; k < operationRepeat; k++) {
            memmove(input_buffer, input_buffer + D, (size_t)(Nw - D) * sizeof(t_double));
            memcpy(input_buffer + (Nw - D), MSPInputVector + (D * k), (size_t)D * sizeof(t_double));

            do_splitbank(x);

            for (i = 0; i < channel_count; i++) {
                splitbank_emit(obanks[i], outs[i] + (D * k), D, Nw, mult);
            }
        }
    }
    else if (x->bufferStatus == BIGGER_THAN_MSP_VECTOR) {
        internalInputVector = obanks[0]->internalInputVector;
        memcpy(internalInputVector + (operationCount * vector_size), MSPInputVector,
               (size_t)vector_size * sizeof(t_double));

        for (i = 0; i < channel_count; i++) {
            memcpy(outs[i], obanks[i]->internalOutputVector + (operationCount * vector_size),
                   (size_t)vector_size * sizeof(t_double));
        }
        operationCount = (operationCount + 1) % operationRepeat;

        if (operationCount == 0) {
            memmove(input_buffer, input_buffer + D, (size_t)(Nw - D) * sizeof(t_double));
            memcpy(input_buffer + (Nw - D), internalInputVector, (size_t)D * sizeof(t_double));

            do_splitbank(x);

            for (i = 0; i < channel_count; i++) {
                splitbank_emit(obanks[i], obanks[i]->internalOutputVector, D, Nw, mult);
            }
        }
    }
    {
        t_double frac = x->frac;   // read after processing so sync reflects this block
        for (i = 0; i < n; i++) {
            sync[i] = frac;
        }
    }
    x->operationCount = operationCount;

    systhread_mutex_unlock(x->lock);
}

void splitbank_dsp64(t_splitbank *x, t_object *dsp64, short *count, double sr, long maxvectorsize, long flags)
{
    int i;
    int R;
    int vs;
    int channel_count = x->channel_count;
    t_oscbank **obanks = x->obanks;

    if (!(sr >= 1.0) || maxvectorsize < 1)
        return;

    systhread_mutex_lock(x->lock);   // perform outputs silence while we re-initialize

    vs = (int)maxvectorsize;
    x->vector_size = vs;
    x->R = (int)sr;
    R = x->R;
    x->N2 = x->N / 2;
    x->D = x->N / x->overlap;        // hop follows the (possibly changed) overlap factor
    x->hopsamps = x->D;
    x->operationCount = 0;

    if (x->D > vs) {
        if (x->D % vs) {
            x->bufferStatus = INVALID_VECTOR_RELATION;
        } else {
            x->operationRepeat = x->D / vs;
            x->bufferStatus = BIGGER_THAN_MSP_VECTOR;
        }
    } else if (x->D < vs) {
        if (vs % x->D) {
            x->bufferStatus = INVALID_VECTOR_RELATION;
        } else {
            x->operationRepeat = vs / x->D;
            x->bufferStatus = SMALLER_THAN_MSP_VECTOR;
        }
    } else {
        x->operationRepeat = 1;
        x->bufferStatus = EQUAL_TO_MSP_VECTOR;
    }
    if (x->bufferStatus == INVALID_VECTOR_RELATION) {
        object_error((t_object *)x, "signal vector size %d and hop size %d are not multiples of one another; output muted",
                     vs, x->D);
    }

    // ramp time is kept in seconds so a user "ramptime" survives DSP restarts and rate changes
    x->countdown_samps = (long)(x->ramp_sec * R);
    if (x->counter > x->countdown_samps)
        x->counter = x->countdown_samps;

    for (i = 0; i < x->N2; i++) {
        x->last_binsplit[i] = x->current_binsplit[i];
    }
    for (i = 0; i < channel_count; i++) {
        fftease_obank_initialize(obanks[i], x->lo_freq, x->hi_freq, x->overlap, R, x->D, x->N, 0);
    }

    systhread_mutex_unlock(x->lock);

    object_method(dsp64, gensym("dsp_add64"), x, splitbank_perform64, 0, NULL);
}

/* ------------------------------------------------------- oscillator bank */

void fftease_obank_destroy(t_oscbank *x)
{
    SB_FREE(x->Wanal);
    SB_FREE(x->Wsyn);
    SB_FREE(x->Hwin);
    SB_FREE(x->complex_spectrum);
    SB_FREE(x->interleaved_spectrum);
    SB_FREE(x->input_buffer);
    SB_FREE(x->output_buffer);
    SB_FREE(x->c_lastphase_in);
    SB_FREE(x->c_lastphase_out);
    SB_FREE(x->lastamp);
    SB_FREE(x->lastfreq);
    SB_FREE(x->index);
    SB_FREE(x->table);
    SB_FREE(x->bitshuffle);
    SB_FREE(x->trigland);
    SB_FREE(x->internalInputVector);
    SB_FREE(x->internalOutputVector);
}

// Returns 0 on success, -1 if an allocation failed (first == 1 only).
int fftease_obank_initialize(t_oscbank *x, t_double lo_freq, t_double hi_freq, int overlap,
                             int R, int vector_size, int N, int first)
{
    int i;
    int max_N2;
    x->overlap = overlap;
    x->R = R;
    x->vector_size = vector_size;
    x->N = N;
    x->Nw = x->N;
    x->N2 = (x->N) >> 1;
    x->Nw2 = (x->Nw) >> 1;
    x->in_count = -(x->Nw);
    x->table_length = OSCBANK_TABLE_LENGTH;
    x->user_lofreq = lo_freq;
    x->user_hifreq = hi_freq;
    x->synthesis_threshold = .001;
    x->table_si = (t_double)x->table_length / (t_double)x->R;
    if (first) {
        max_N2 = MAX_FFTSIZE / 2;
        x->Wanal = (t_double *)sysmem_newptrclear(MAX_FFTSIZE * sizeof(t_double));
        x->Wsyn = (t_double *)sysmem_newptrclear(MAX_FFTSIZE * sizeof(t_double));
        x->Hwin = (t_double *)sysmem_newptrclear(MAX_FFTSIZE * sizeof(t_double));
        x->complex_spectrum = (t_double *)sysmem_newptrclear(MAX_FFTSIZE * sizeof(t_double));
        x->interleaved_spectrum = (t_double *)sysmem_newptrclear((MAX_FFTSIZE + 2) * sizeof(t_double));
        x->input_buffer = (t_double *)sysmem_newptrclear(MAX_FFTSIZE * sizeof(t_double));
        x->output_buffer = (t_double *)sysmem_newptrclear(MAX_FFTSIZE * sizeof(t_double));
        x->c_lastphase_in = (t_double *)sysmem_newptrclear((max_N2 + 1) * sizeof(t_double));
        x->c_lastphase_out = (t_double *)sysmem_newptrclear((max_N2 + 1) * sizeof(t_double));
        x->lastamp = (t_double *)sysmem_newptrclear((MAX_FFTSIZE + 1) * sizeof(t_double));
        x->lastfreq = (t_double *)sysmem_newptrclear((MAX_FFTSIZE + 1) * sizeof(t_double));
        x->index = (t_double *)sysmem_newptrclear((MAX_FFTSIZE + 1) * sizeof(t_double));
        x->table = (t_double *)sysmem_newptrclear(OSCBANK_TABLE_LENGTH * sizeof(t_double));
        x->bitshuffle = (int *)sysmem_newptrclear((MAX_FFTSIZE * 2) * sizeof(int));
        x->trigland = (t_double *)sysmem_newptrclear((MAX_FFTSIZE * 2) * sizeof(t_double));
        x->internalInputVector = (t_double *)sysmem_newptrclear(MAX_D * sizeof(t_double));
        x->internalOutputVector = (t_double *)sysmem_newptrclear(MAX_D * sizeof(t_double));
        if (!x->Wanal || !x->Wsyn || !x->Hwin || !x->complex_spectrum || !x->interleaved_spectrum
            || !x->input_buffer || !x->output_buffer || !x->c_lastphase_in || !x->c_lastphase_out
            || !x->lastamp || !x->lastfreq || !x->index || !x->table || !x->bitshuffle
            || !x->trigland || !x->internalInputVector || !x->internalOutputVector)
            return -1;   // caller destroys whatever was allocated
    }
    x->mult = 1. / (t_double)x->N;
    for (i = 0; i < x->N2 + 1; i++) {
        x->c_lastphase_in[i] = x->c_lastphase_out[i] = 0.0;
    }
    for (i = 0; i < x->N + 1; i++) {
        x->lastamp[i] = x->lastfreq[i] = x->index[i] = 0.0;
    }
    for (i = 0; i < x->Nw; i++) {
        x->input_buffer[i] = x->output_buffer[i] = 0.0;
    }
    for (i = 0; i < MAX_D; i++) {
        x->internalInputVector[i] = x->internalOutputVector[i] = 0.0;
    }
    init_rdft(x->N, x->bitshuffle, x->trigland);
    makehanning(x->Hwin, x->Wanal, x->Wsyn, x->Nw, x->N, x->vector_size, 0);
    x->c_fundamental = (t_double)x->R / (t_double)x->N;
    x->c_factor_in = (t_double)x->R / ((t_double)x->vector_size * TWOPI);
    x->c_factor_out = 1.0 / x->c_factor_in;

    x->curfreq = 0;
    fftease_obank_topfreq(x, x->user_hifreq);
    fftease_obank_bottomfreq(x, x->user_lofreq);

    for (i = 0; i < x->table_length; i++) {
        x->table[i] = (t_double)x->N * cos((t_double)i * TWOPI / (t_double)x->table_length);
    }
    x->P = 1.0;
    x->i_vector_size = 1. / x->vector_size;
    x->pitch_increment = x->P * x->table_length / x->R;
    return 0;
}

/**************************************************/
// hi_bin = 1 + ceil(topfreq / fundamental), clamped to N2 (closed form: no unbounded loop on huge input)
void fftease_obank_topfreq(t_oscbank *x, t_double topfreq)
{
    t_double b;
    if (!(topfreq >= x->c_fundamental))   // too low, or NaN
        topfreq = OSCBANK_DEFAULT_TOPFREQ;

    b = ceil(topfreq / x->c_fundamental);
    if (b > (t_double)x->N2)
        b = (t_double)x->N2;
    x->hi_bin = 1 + (int)b;
    if (x->hi_bin > x->N2)
        x->hi_bin = x->N2;
}
/**************************************************/
// lo_bin = ceil(bottomfreq / fundamental), clamped to [0, N2]
void fftease_obank_bottomfreq(t_oscbank *x, t_double bottomfreq)
{
    t_double b;
    if (!(bottomfreq > 0.0)) {            // zero, negative, or NaN
        x->lo_bin = 0;
        return;
    }
    b = ceil(bottomfreq / x->c_fundamental);
    if (b > (t_double)x->N2)
        b = (t_double)x->N2;
    x->lo_bin = (int)b;
}
/**************************************************/
void fftease_obank_analyze(t_oscbank *x)
{
    fold(x->input_buffer, x->Wanal, x->Nw, x->complex_spectrum, x->N, x->in_count);

    rdft(x->N, 1, x->complex_spectrum, x->bitshuffle, x->trigland);

    convert(x->complex_spectrum, x->interleaved_spectrum, x->N2, x->c_lastphase_in,
            x->c_fundamental, x->c_factor_in);
}
/**************************************************/
void fftease_obank_synthesize(t_oscbank *x)
{
    int amp, chan, freq;
    t_double a, ainc, f, finc, address, fnew;
    int n;

    t_double synthesis_threshold = x->synthesis_threshold;
    t_double *lastfreq = x->lastfreq;
    t_double *lastamp = x->lastamp;
    t_double tl = x->table_length;
    t_double *output_buffer = x->output_buffer;
    int vector_size = x->vector_size;
    t_double i_vector_size = x->i_vector_size;
    int lo_bin = x->lo_bin;
    int hi_bin = x->hi_bin;
    t_double *interleaved_spectrum = x->interleaved_spectrum;
    t_double pitch_increment = x->pitch_increment;
    t_double *index = x->index;
    t_double *table = x->table;

    for (chan = lo_bin; chan < hi_bin; chan++) {
        freq = (amp = (chan << 1)) + 1;
        // skip quiet bins, and anything non-finite or absurd so state is never poisoned
        if (!(interleaved_spectrum[amp] > synthesis_threshold && interleaved_spectrum[amp] < 1e12))
            continue;
        fnew = interleaved_spectrum[freq] * pitch_increment;
        interleaved_spectrum[freq] = fnew;
        if (!(fabs(fnew) < 1e7))
            continue;
        finc = (fnew - (f = lastfreq[chan])) * i_vector_size;
        ainc = (interleaved_spectrum[amp] - (a = lastamp[chan])) * i_vector_size;
        address = index[chan];
        for (n = 0; n < vector_size; n++) {
            output_buffer[n] += a * table[(int)address];
            address += f;
            // wrap into [0, table_length): a single subtract in the common case,
            // fmod for extreme frequencies so this can never spin or leave the table
            if (address >= tl) {
                address -= tl;
                if (address >= tl)
                    address = fmod(address, tl);
            } else if (address < 0.0) {
                address += tl;
                if (address < 0.0) {
                    address = fmod(address, tl);
                    if (address < 0.0)
                        address += tl;
                }
            }
            if (!(address >= 0.0 && address < tl))
                address = 0.0;
            a += ainc;
            f += finc;
        }
        lastfreq[chan] = interleaved_spectrum[freq];
        lastamp[chan] = interleaved_spectrum[amp];
        index[chan] = address;
    }
}
////////////////////////
void init_rdft(int n, int *ip, t_double *w)
{
    
    int    nw,
    nc;
    
    void    makewt(int nw, int *ip, t_double *w);
    void    makect(int nc, int *ip, t_double *c);
    
    nw = n >> 2;
    makewt(nw, ip, w);
    
    nc = n >> 2;
    makect(nc, ip, w + nw);
    
    return;
}


void rdft(int n, int isgn, t_double *a, int *ip, t_double *w)
{
    
    int        j,
    nw,
    nc;
    
    t_double        xi;
    
    void        bitrv2(int n, int *ip, t_double *a),
    cftsub(int n, t_double *a, t_double *w),
    rftsub(int n, t_double *a, int nc, t_double *c);
    
    
    nw = ip[0];
    nc = ip[1];
    
    if (isgn < 0) {
        a[1] = 0.5 * (a[1] - a[0]);
        a[0] += a[1];
        
        for (j = 3; j <= n - 1; j += 2) {
            a[j] = -a[j];
        }
        
        if (n > 4) {
            rftsub(n, a, nc, w + nw);
            bitrv2(n, ip + 2, a);
        }
        
        cftsub(n, a, w);
        
        for (j = 1; j <= n - 1; j += 2) {
            a[j] = -a[j];
        }
    }
    
    else {
        
        if (n > 4) {
            bitrv2(n, ip + 2, a);
        }
        
        cftsub(n, a, w);
        
        if (n > 4) {
            rftsub(n, a, nc, w + nw);
        }
        
        xi = a[0] - a[1];
        a[0] += a[1];
        a[1] = xi;
    }
}


void bitrv2(int n, int *ip, t_double *a)
{
    int j, j1, k, k1, l, m, m2;
    t_double xr, xi;
    
    ip[0] = 0;
    l = n;
    m = 1;
    
    while ((m << 2) < l) {
        l >>= 1;
        for (j = 0; j <= m - 1; j++) {
            ip[m + j] = ip[j] + l;
        }
        m <<= 1;
    }
    
    if ((m << 2) > l) {
        
        for (k = 1; k <= m - 1; k++) {
            
            for (j = 0; j <= k - 1; j++) {
                j1 = (j << 1) + ip[k];
                k1 = (k << 1) + ip[j];
                xr = a[j1];
                xi = a[j1 + 1];
                a[j1] = a[k1];
                a[j1 + 1] = a[k1 + 1];
                a[k1] = xr;
                a[k1 + 1] = xi;
            }
        }
    }
    
    else {
        m2 = m << 1;
        
        for (k = 1; k <= m - 1; k++) {
            
            for (j = 0; j <= k - 1; j++) {
                j1 = (j << 1) + ip[k];
                k1 = (k << 1) + ip[j];
                xr = a[j1];
                xi = a[j1 + 1];
                a[j1] = a[k1];
                a[j1 + 1] = a[k1 + 1];
                a[k1] = xr;
                a[k1 + 1] = xi;
                j1 += m2;
                k1 += m2;
                xr = a[j1];
                xi = a[j1 + 1];
                a[j1] = a[k1];
                a[j1 + 1] = a[k1 + 1];
                a[k1] = xr;
                a[k1 + 1] = xi;
            }
        }
    }
}


void cftsub(int n, t_double *a, t_double *w)
{
    int j, j1, j2, j3, k, k1, ks, l, m;
    t_double wk1r, wk1i, wk2r, wk2i, wk3r, wk3i;
    t_double x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;
    
    l = 2;
    
    while ((l << 1) < n) {
        m = l << 2;
        
        for (j = 0; j <= l - 2; j += 2) {
            j1 = j + l;
            j2 = j1 + l;
            j3 = j2 + l;
            x0r = a[j] + a[j1];
            x0i = a[j + 1] + a[j1 + 1];
            x1r = a[j] - a[j1];
            x1i = a[j + 1] - a[j1 + 1];
            x2r = a[j2] + a[j3];
            x2i = a[j2 + 1] + a[j3 + 1];
            x3r = a[j2] - a[j3];
            x3i = a[j2 + 1] - a[j3 + 1];
            a[j] = x0r + x2r;
            a[j + 1] = x0i + x2i;
            a[j2] = x0r - x2r;
            a[j2 + 1] = x0i - x2i;
            a[j1] = x1r - x3i;
            a[j1 + 1] = x1i + x3r;
            a[j3] = x1r + x3i;
            a[j3 + 1] = x1i - x3r;
        }
        
        if (m < n) {
            wk1r = w[2];
            
            for (j = m; j <= l + m - 2; j += 2) {
                j1 = j + l;
                j2 = j1 + l;
                j3 = j2 + l;
                x0r = a[j] + a[j1];
                x0i = a[j + 1] + a[j1 + 1];
                x1r = a[j] - a[j1];
                x1i = a[j + 1] - a[j1 + 1];
                x2r = a[j2] + a[j3];
                x2i = a[j2 + 1] + a[j3 + 1];
                x3r = a[j2] - a[j3];
                x3i = a[j2 + 1] - a[j3 + 1];
                a[j] = x0r + x2r;
                a[j + 1] = x0i + x2i;
                a[j2] = x2i - x0i;
                a[j2 + 1] = x0r - x2r;
                x0r = x1r - x3i;
                x0i = x1i + x3r;
                a[j1] = wk1r * (x0r - x0i);
                a[j1 + 1] = wk1r * (x0r + x0i);
                x0r = x3i + x1r;
                x0i = x3r - x1i;
                a[j3] = wk1r * (x0i - x0r);
                a[j3 + 1] = wk1r * (x0i + x0r);
            }
            
            k1 = 1;
            ks = -1;
            
            for (k = (m << 1); k <= n - m; k += m) {
                k1++;
                ks = -ks;
                wk1r = w[k1 << 1];
                wk1i = w[(k1 << 1) + 1];
                wk2r = ks * w[k1];
                wk2i = w[k1 + ks];
                wk3r = wk1r - 2 * wk2i * wk1i;
                wk3i = 2 * wk2i * wk1r - wk1i;
                
                for (j = k; j <= l + k - 2; j += 2) {
                    j1 = j + l;
                    j2 = j1 + l;
                    j3 = j2 + l;
                    x0r = a[j] + a[j1];
                    x0i = a[j + 1] + a[j1 + 1];
                    x1r = a[j] - a[j1];
                    x1i = a[j + 1] - a[j1 + 1];
                    x2r = a[j2] + a[j3];
                    x2i = a[j2 + 1] + a[j3 + 1];
                    x3r = a[j2] - a[j3];
                    x3i = a[j2 + 1] - a[j3 + 1];
                    a[j] = x0r + x2r;
                    a[j + 1] = x0i + x2i;
                    x0r -= x2r;
                    x0i -= x2i;
                    a[j2] = wk2r * x0r - wk2i * x0i;
                    a[j2 + 1] = wk2r * x0i + wk2i * x0r;
                    x0r = x1r - x3i;
                    x0i = x1i + x3r;
                    a[j1] = wk1r * x0r - wk1i * x0i;
                    a[j1 + 1] = wk1r * x0i + wk1i * x0r;
                    x0r = x1r + x3i;
                    x0i = x1i - x3r;
                    a[j3] = wk3r * x0r - wk3i * x0i;
                    a[j3 + 1] = wk3r * x0i + wk3i * x0r;
                }
            }
        }
        
        l = m;
    }
    
    if (l < n) {
        
        for (j = 0; j <= l - 2; j += 2) {
            j1 = j + l;
            x0r = a[j] - a[j1];
            x0i = a[j + 1] - a[j1 + 1];
            a[j] += a[j1];
            a[j + 1] += a[j1 + 1];
            a[j1] = x0r;
            a[j1 + 1] = x0i;
        }
    }
}


void rftsub(int n, t_double *a, int nc, t_double *c)
{
    int j, k, kk, ks;
    t_double wkr, wki, xr, xi, yr, yi;
    
    ks = (nc << 2) / n;
    kk = 0;
    
    for (k = (n >> 1) - 2; k >= 2; k -= 2) {
        j = n - k;
        kk += ks;
        wkr = 0.5 - c[kk];
        wki = c[nc - kk];
        xr = a[k] - a[j];
        xi = a[k + 1] + a[j + 1];
        yr = wkr * xr - wki * xi;
        yi = wkr * xi + wki * xr;
        a[k] -= yr;
        a[k + 1] -= yi;
        a[j] += yr;
        a[j + 1] -= yi;
    }
}


void makewt(int nw, int *ip, t_double *w)
{
    void bitrv2(int n, int *ip, t_double *a);
    int nwh, j;
    t_double delta, x, y;
    
    ip[0] = nw;
    ip[1] = 1;
    if (nw > 2) {
        nwh = nw >> 1;
        delta = atan(1.0) / nwh;
        w[0] = 1;
        w[1] = 0;
        w[nwh] = cos(delta * nwh);
        w[nwh + 1] = w[nwh];
        for (j = 2; j <= nwh - 2; j += 2) {
            x = cos(delta * j);
            y = sin(delta * j);
            w[j] = x;
            w[j + 1] = y;
            w[nw - j] = y;
            w[nw - j + 1] = x;
        }
        bitrv2(nw, ip + 2, w);
    }
}


void makect(int nc, int *ip, t_double *c)
{
    int nch, j;
    t_double delta;
    
    ip[1] = nc;
    if (nc > 1) {
        nch = nc >> 1;
        delta = atan(1.0) / nch;
        c[0] = 0.5;
        c[nch] = 0.5 * cos(delta * nch);
        for (j = 1; j <= nch - 1; j++) {
            c[j] = 0.5 * cos(delta * j);
            c[nc - j] = 0.5 * sin(delta * j);
        }
    }
}
void convert(t_double *S, t_double *C, int N2, t_double *lastphase, t_double fundamental, t_double factor )
{
    t_double     phase,
    phasediff;
    int         real,
    imag,
    amp,
    freq;
    t_double     a,
    b;
    int         i;
    
    /*  t_double myTWOPI, myPI; */
    /*  double sin(), cos(), atan(), hypot();*/
    
    /*  myTWOPI = 8.*atan(1.);
     myPI = 4.*atan(1.); */
    
    
    for ( i = 0; i <= N2; i++ ) {
        imag = freq = ( real = amp = i<<1 ) + 1;
        a = ( i == N2 ? S[1] : S[real] );
        b = ( i == 0 || i == N2 ? 0. : S[imag] );
        
        C[amp] = hypot( a, b );
        if ( C[amp] == 0. )
            phasediff = 0.;
        else {
            phasediff = ( phase = -atan2( b, a ) ) - lastphase[i];
            lastphase[i] = phase;
            
            while ( phasediff > PI )
                phasediff -= TWOPI;
            while ( phasediff < -PI )
                phasediff += TWOPI;
        }
        C[freq] = phasediff*factor + i*fundamental;
    }
}

void fold( t_double *I, t_double *W, int Nw, t_double *O, int N, int n )
{
    
    int i;
    
    for ( i = 0; i < N; i++ )
        O[i] = 0.;
    
    while ( n < 0 )
        n += N;
    n %= N;
    for ( i = 0; i < Nw; i++ ) {
        O[n] += I[i]*W[i];
        if ( ++n == N )
            n = 0;
    }
}


void makehanning( t_double *H, t_double *A, t_double *S, int Nw, int N, int I, int odd )
{
    int i;
    t_double sum ;
    
    
    if (odd) {
        for ( i = 0 ; i < Nw ; i++ )
            H[i] = A[i] = S[i] = sqrt(0.5 * (1. + cos(PI + TWOPI * i / (Nw - 1))));
    }
    
    else {
        
        for ( i = 0 ; i < Nw ; i++ )
            H[i] = A[i] = S[i] = 0.5 * (1. + cos(PI + TWOPI * i / (Nw - 1)));
        
    }
    
    if ( Nw > N ) {
        t_double x ;
        
        x = -(Nw - 1)/2. ;
        for ( i = 0 ; i < Nw ; i++, x += 1. )
            if ( x != 0. ) {
                A[i] *= N*sin( PI*x/N )/(PI*x) ;
                if ( I )
                    S[i] *= I*sin( PI*x/I )/(PI*x) ;
            }
    }
    for ( sum = i = 0 ; i < Nw ; i++ )
        sum += A[i] ;
    
    for ( i = 0 ; i < Nw ; i++ ) {
        t_double afac = 2./sum ;
        t_double sfac = Nw > N ? 1./afac : afac ;
        A[i] *= afac ;
        S[i] *= sfac ;
    }
    
    if ( Nw <= N && I ) {
        for ( sum = i = 0 ; i < Nw ; i += I )
            sum += S[i]*S[i] ;
        for ( sum = 1./sum, i = 0 ; i < Nw ; i++ )
            S[i] *= sum ;
    }
}
