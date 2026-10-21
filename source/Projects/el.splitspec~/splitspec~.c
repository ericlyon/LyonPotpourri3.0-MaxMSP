/**
 * @file splitspec~.c
 * @brief Spectral bin splitter for pfft~: distributes FFT bins among channel pairs for Max 8/9.
 * Modernized for 64-bit dsp64, memory safety, bounds-checked state handling,
 * and thread-safe buffer management.
 */

#include "MSPd.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#ifndef PIOVERTWO
#define PIOVERTWO 1.5707963267948966
#endif

#define MAXSTORE (128)
#define MAX_CHANNELS 64
#define MIN_OVERLAP 2
#define MAX_OVERLAP 4096
#define OBJECT_NAME "splitspec~"

#define SS_FREE(p) do { if (p) { sysmem_freeptr(p); (p) = NULL; } } while (0)

static t_class *splitspec_class;

// Everything sized by the pfft~ frame size; rebuilt as a unit when the frame size changes.
typedef struct _ssbufs
{
    int *last_binsplit;
    int *current_binsplit;
    t_double *last_mag;
    t_double *current_mag;
    short *stored_slots;
    int **stored_binsplits;
} t_ssbufs;

typedef struct _splitspec
{
    t_pxobject x_obj;
    int N;
    int N2;
    void *list_outlet;
    void *phase_outlet;
    t_ssbufs b;
    short new_distribution;
    short interpolation_completed;
    short bypass;
    short manual_override;
    long countdown_samps;   // samps for a given fadetime
    long counter;
    t_double ramp_sec;      // user ramp time, kept in seconds so it survives sample-rate changes
    int overlap_factor;     // compensate for overlap in fade
    t_double sr;
    t_double fl_phase;      // show phase as float
    int hopsamps;           // number of samples to hop
    int channel_count;      // number of channels to split
    t_clock *phase_clock;
    t_double **magvecs;     // point to mag output vectors
    t_double **phasevecs;   // point to phase output vectors
    t_systhread_mutex lock; // guards buffers/state vs. main-thread edits and dsp64 rebuilds
} t_splitspec;

void *splitspec_new(t_symbol *s, long argc, t_atom *argv);
void splitspec_free(t_splitspec *x);
void splitspec_perform64(t_splitspec *x, t_object *dsp64, double **ins,
                         long numins, double **outs,
                         long numouts, long n,
                         long flags, void *userparam);
void splitspec_dsp64(t_splitspec *x, t_object *dsp64, short *count, double R,
                     long vector_size, long flags);
void splitspec_assist(t_splitspec *x, void *b, long msg, long arg, char *dst);
void splitspec_showstate(t_splitspec *x);
void splitspec_bypass(t_splitspec *x, double toggle);
void splitspec_manual_override(t_splitspec *x, double toggle);
void splitspec_setstate(t_splitspec *x, t_symbol *msg, long argc, t_atom *argv);
void splitspec_ramptime(t_splitspec *x, t_symbol *msg, long argc, t_atom *argv);
int rand_index(int max);
void splitspec_scramble(t_splitspec *x);
void splitspec_spiral(t_splitspec *x);
void splitspec_squantize(t_splitspec *x, double blockbins);
void splitspec_overlap(t_splitspec *x, double factor);
void splitspec_store(t_splitspec *x, double floc);
void splitspec_recall(t_splitspec *x, double floc);
void splitspeci(int *current_binsplit, int *last_binsplit, int bin_offset, int table_offset,
                t_double *current_mag, t_double *last_mag, t_double *inmag, t_double *dest_mag,
                int start, int end, int n, t_double oldfrac, t_double newfrac);
void splitspec(int *binsplit, int bin_offset, int table_offset,
               t_double *inmag, t_double *dest_mag, int start, int end, int n);
void splitspec_phaseout(t_splitspec *x);
int splitspec_closestPowerOfTwo(int p);

/* ---------------------------------------------------------------- helpers */

// NaN -> 0, then clamp into [lo, hi]
static inline t_double splitspec_clip(t_double v, t_double lo, t_double hi)
{
    if (v != v)
        v = 0.0;
    if (v < lo)
        v = lo;
    if (v > hi)
        v = hi;
    return v;
}

static void splitspec_bufs_free(t_ssbufs *b)
{
    int i;
    SS_FREE(b->last_binsplit);
    SS_FREE(b->current_binsplit);
    SS_FREE(b->last_mag);
    SS_FREE(b->current_mag);
    SS_FREE(b->stored_slots);
    if (b->stored_binsplits) {
        for (i = 0; i < MAXSTORE; i++) {
            SS_FREE(b->stored_binsplits[i]);
        }
        SS_FREE(b->stored_binsplits);
    }
}

// Allocates a fully zeroed set of buffers for an N2-bin frame. Returns 0 on success.
static int splitspec_bufs_alloc(t_ssbufs *b, int N2)
{
    int i;
    memset(b, 0, sizeof(t_ssbufs));
    b->last_binsplit = (int *)sysmem_newptrclear(N2 * sizeof(int));
    b->current_binsplit = (int *)sysmem_newptrclear(N2 * sizeof(int));
    b->last_mag = (t_double *)sysmem_newptrclear(N2 * sizeof(t_double));
    b->current_mag = (t_double *)sysmem_newptrclear(N2 * sizeof(t_double));
    b->stored_slots = (short *)sysmem_newptrclear(MAXSTORE * sizeof(short));  // indexed by slot, not bin
    b->stored_binsplits = (int **)sysmem_newptrclear(MAXSTORE * sizeof(int *));
    if (!b->last_binsplit || !b->current_binsplit || !b->last_mag || !b->current_mag
        || !b->stored_slots || !b->stored_binsplits) {
        splitspec_bufs_free(b);
        return -1;
    }
    for (i = 0; i < MAXSTORE; i++) {
        b->stored_binsplits[i] = (int *)sysmem_newptrclear(N2 * sizeof(int));
        if (!b->stored_binsplits[i]) {
            splitspec_bufs_free(b);
            return -1;
        }
    }
    return 0;
}

// Buffers only exist once dsp64 has told us the frame size.
static int splitspec_ready(t_splitspec *x)
{
    return x->N2 > 0 && x->b.current_binsplit != NULL;
}

// Call with the lock held, after current_binsplit has been replaced (last_binsplit must already
// hold the old distribution). The audio thread consumes new_distribution and restarts the ramp.
static void splitspec_begin_transition(t_splitspec *x)
{
    int i;
    if (x->countdown_samps <= 0) { // ramp off: no morph, last == current
        for (i = 0; i < x->N2; i++)
            x->b.last_binsplit[i] = x->b.current_binsplit[i];
    }
    x->new_distribution = 1;
}

// Call with the lock held. Fisher-Yates in place: the array is a valid permutation at every step.
static void splitspec_shuffle(t_splitspec *x)
{
    int i, swapi, tmp;
    int N2 = x->N2;
    int *cur = x->b.current_binsplit;
    int max = N2;

    for (i = 0; i < N2; i++) {
        cur[i] = i;
    }
    for (i = 0; i < N2; i++) {
        swapi = rand_index(max);
        tmp = cur[swapi];
        cur[swapi] = cur[max - 1];
        cur[max - 1] = tmp;
        --max;
    }
}

static int splitspec_lock_ready(t_splitspec *x)
{
    systhread_mutex_lock(x->lock);
    if (!splitspec_ready(x)) {
        systhread_mutex_unlock(x->lock);
        object_error((t_object *)x, "not ready yet: turn audio on (or compile the patch) first");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------- main */

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.splitspec~", (method)splitspec_new, (method)splitspec_free,
                  sizeof(t_splitspec), 0L, A_GIMME, 0);

    class_addmethod(c, (method)splitspec_assist,          "assist",          A_CANT, 0);
    class_addmethod(c, (method)splitspec_dsp64,           "dsp64",           A_CANT, 0);
    class_addmethod(c, (method)splitspec_showstate,       "showstate",       A_NOTHING, 0);
    class_addmethod(c, (method)splitspec_manual_override, "manual_override", A_FLOAT, 0);
    class_addmethod(c, (method)splitspec_bypass,          "bypass",          A_FLOAT, 0);
    class_addmethod(c, (method)splitspec_store,           "store",           A_FLOAT, 0);
    class_addmethod(c, (method)splitspec_recall,          "recall",          A_FLOAT, 0);
    class_addmethod(c, (method)splitspec_setstate,        "setstate",        A_GIMME, 0);
    class_addmethod(c, (method)splitspec_ramptime,        "ramptime",        A_GIMME, 0);
    class_addmethod(c, (method)splitspec_overlap,         "overlap",         A_FLOAT, 0);
    class_addmethod(c, (method)splitspec_scramble,        "scramble",        A_NOTHING, 0);
    class_addmethod(c, (method)splitspec_spiral,          "spiral",          A_NOTHING, 0);
    class_addmethod(c, (method)splitspec_squantize,       "squantize",       A_DEFFLOAT, 0);
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    splitspec_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

/* -------------------------------------------------------------- lifecycle */

int splitspec_closestPowerOfTwo(int p)
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

void *splitspec_new(t_symbol *s, long argc, t_atom *argv)
{
    static int seeded = 0;
    int i;
    double cc;
    t_splitspec *x = (t_splitspec *)object_alloc(splitspec_class);

    if (!x)
        return NULL;

    cc = atom_getfloatarg(0, argc, argv);
    if (!(cc >= 2.0))
        cc = 2.0;
    if (cc > MAX_CHANNELS) {
        object_error((t_object *)x, "maximum of %d channels; using %d", MAX_CHANNELS, MAX_CHANNELS);
        cc = MAX_CHANNELS;
    }
    x->channel_count = splitspec_closestPowerOfTwo((int)cc);

    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }
    systhread_mutex_new(&x->lock, SYSTHREAD_MUTEX_NORMAL);

    dsp_setup((t_pxobject *)x, 5);

    x->list_outlet = (void *)listout((void *)x);
    x->phase_outlet = (void *)floatout((void *)x);
    x->phase_clock = (t_clock *)clock_new((void *)x, (method)splitspec_phaseout);
    for (i = 0; i < x->channel_count * 2; i++) {
        outlet_new((t_object *)x, "signal");
    }
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->sr = sys_getsr();
    if (!(x->sr > 0.0))
        x->sr = 44100.0;
    x->ramp_sec = 1.0;                                  // 1 second fade time by default
    x->countdown_samps = (long)(x->ramp_sec * x->sr);
    x->overlap_factor = 8;                              // default

    x->magvecs = (t_double **)sysmem_newptrclear(x->channel_count * sizeof(t_double *));
    x->phasevecs = (t_double **)sysmem_newptrclear(x->channel_count * sizeof(t_double *));
    if (!x->phase_clock || !x->magvecs || !x->phasevecs) {
        object_error((t_object *)x, "out of memory");
        object_free((t_object *)x);   // _free tolerates NULL members
        return NULL;
    }
    return x;
}

void splitspec_free(t_splitspec *x)
{
    dsp_free((t_pxobject *)x);          // first: detach from audio thread
    if (x->phase_clock) {
        clock_unset(x->phase_clock);    // a pending clock must not fire into freed memory
        object_free(x->phase_clock);
        x->phase_clock = NULL;
    }
    if (x->lock) {
        systhread_mutex_lock(x->lock);  // wait out any in-flight perform
        systhread_mutex_unlock(x->lock);
    }
    splitspec_bufs_free(&x->b);
    SS_FREE(x->magvecs);
    SS_FREE(x->phasevecs);
    if (x->lock) {
        systhread_mutex_free(x->lock);
        x->lock = NULL;
    }
}

void splitspec_assist(t_splitspec *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
        case 0: snprintf(dst, 512, "(signal) Magnitude Input"); break;
        case 1: snprintf(dst, 512, "(signal) Phase Input"); break;
        case 2: snprintf(dst, 512, "(signal) Table Offset"); break;
        case 3: snprintf(dst, 512, "(signal) Bin Offset"); break;
        case 4: snprintf(dst, 512, "(signal) Manual Control"); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        if (arg < x->channel_count * 2) {
            snprintf(dst, 512, "(signal) Spectrum Segment %ld %s", arg / 2 + 1,
                     (arg % 2) ? "Phase" : "Magnitude");
        } else if (arg == x->channel_count * 2) {
            snprintf(dst, 512, "(float) Ramp Sync");
        } else {
            snprintf(dst, 512, "(list) Current Bin State");
        }
    }
}

/* --------------------------------------------------------- simple messages */

void splitspec_phaseout(t_splitspec *x)
{
    outlet_float(x->phase_outlet, x->fl_phase);
}

void splitspec_overlap(t_splitspec *x, double fol)
{
    if (!(fol >= MIN_OVERLAP && fol <= MAX_OVERLAP)) {
        object_error((t_object *)x, "illegal overlap %f (must be %d - %d)", fol, MIN_OVERLAP, MAX_OVERLAP);
        return;
    }
    x->overlap_factor = (int)fol;
    if (x->N > 0) {
        int hop = x->N / x->overlap_factor;
        x->hopsamps = hop < 1 ? 1 : hop;
    }
}

void splitspec_bypass(t_splitspec *x, double toggle)
{
    x->bypass = (toggle != 0.0);
}

void splitspec_manual_override(t_splitspec *x, double toggle)
{
    x->manual_override = (toggle != 0.0);
}

void splitspec_ramptime(t_splitspec *x, t_symbol *msg, long argc, t_atom *argv)
{
    double ms = atom_getfloatarg(0, argc, argv);   // milliseconds

    if (!(ms >= 0.0))      // also catches NaN
        ms = 0.0;
    if (ms > 600000.0)     // 10 minutes
        ms = 600000.0;
    x->ramp_sec = ms * 0.001;
    x->countdown_samps = (long)(x->ramp_sec * x->sr);
}

/* ----------------------------------------------------- bin-state messages */

void splitspec_spiral(t_splitspec *x)
{
    int i, j, k;
    int offset;
    int channel_count = x->channel_count;
    int N2;

    if (!splitspec_lock_ready(x))
        return;
    N2 = x->N2;
    offset = N2 / channel_count;

    for (i = 0; i < N2; i++) {
        x->b.last_binsplit[i] = x->b.current_binsplit[i];
    }
    k = 0;
    for (i = 0; i < N2 / channel_count; i++) {
        for (j = 0; j < channel_count; j++) {
            x->b.current_binsplit[i + (j * offset)] = k++;
        }
    }
    splitspec_begin_transition(x);
    systhread_mutex_unlock(x->lock);
}

void splitspec_squantize(t_splitspec *x, double bb)
{
    int i, j, k;
    int maxblock;
    int iterations;
    int bincount = 0;
    int blockbins;
    int N2;
    int channel_count = x->channel_count;

    if (!(bb >= 1.0 && bb <= 1e6)) {
        object_error((t_object *)x, "block size must be at least 1");
        return;
    }
    blockbins = splitspec_closestPowerOfTwo((int)bb);

    if (!splitspec_lock_ready(x))
        return;
    N2 = x->N2;
    maxblock = N2 / channel_count;
    if (blockbins < 1 || blockbins > maxblock) {
        object_error((t_object *)x, "%d is out of bounds - must be between 1 and %d", blockbins, maxblock);
        systhread_mutex_unlock(x->lock);
        return;
    }

    iterations = N2 / channel_count / blockbins;

    for (i = 0; i < N2; i++) {
        x->b.last_binsplit[i] = x->b.current_binsplit[i];
    }

    if (iterations == 1) {
        for (i = 0; i < N2; i++) {
            x->b.current_binsplit[i] = i;
        }
    } else {
        for (k = 0; k < iterations; k++) {
            for (i = 0; i < N2; i += maxblock) {
                for (j = 0; j < blockbins; j++) {
                    if (i + j + k * blockbins < N2) {
                        x->b.current_binsplit[i + j + k * blockbins] = bincount++;
                    }
                }
            }
        }
    }
    splitspec_begin_transition(x);
    systhread_mutex_unlock(x->lock);
}

void splitspec_scramble(t_splitspec *x)
{
    int i;

    if (!splitspec_lock_ready(x))
        return;
    // Copy current mapping to last mapping, then shuffle
    for (i = 0; i < x->N2; i++) {
        x->b.last_binsplit[i] = x->b.current_binsplit[i];
    }
    splitspec_shuffle(x);
    splitspec_begin_transition(x);
    systhread_mutex_unlock(x->lock);
}

void splitspec_store(t_splitspec *x, double floc)
{
    int location, i;

    if (!(floc >= 0.0 && floc <= MAXSTORE - 1)) {
        object_error((t_object *)x, "location must be between 0 and %d, but was %f", MAXSTORE - 1, floc);
        return;
    }
    location = (int)floc;
    if (!splitspec_lock_ready(x))
        return;
    for (i = 0; i < x->N2; i++) {
        x->b.stored_binsplits[location][i] = x->b.current_binsplit[i];
    }
    x->b.stored_slots[location] = 1;
    systhread_mutex_unlock(x->lock);
}

void splitspec_recall(t_splitspec *x, double floc)
{
    int location, i;

    if (!(floc >= 0.0 && floc <= MAXSTORE - 1)) {
        object_error((t_object *)x, "location must be between 0 and %d, but was %f", MAXSTORE - 1, floc);
        return;
    }
    location = (int)floc;
    if (!splitspec_lock_ready(x))
        return;
    if (!x->b.stored_slots[location]) {
        systhread_mutex_unlock(x->lock);
        object_error((t_object *)x, "nothing stored at location %d", location);
        return;
    }
    for (i = 0; i < x->N2; i++) {
        x->b.last_binsplit[i] = x->b.current_binsplit[i];
        x->b.current_binsplit[i] = x->b.stored_binsplits[location][i];
    }
    splitspec_begin_transition(x);
    systhread_mutex_unlock(x->lock);
}

void splitspec_setstate(t_splitspec *x, t_symbol *msg, long argc, t_atom *argv)
{
    long i, v;

    if (!splitspec_lock_ready(x))
        return;
    if (argc != x->N2) {
        object_error((t_object *)x, "list must be of length %d, but actually was %ld", x->N2, argc);
        systhread_mutex_unlock(x->lock);
        return;
    }
    // validate everything first: out-of-range bin numbers would index outside the spectrum
    for (i = 0; i < argc; i++) {
        v = atom_getlong(argv + i);
        if (v < 0 || v >= x->N2) {
            object_error((t_object *)x, "bin number %ld at position %ld is out of range 0 - %d",
                         v, i, x->N2 - 1);
            systhread_mutex_unlock(x->lock);
            return;
        }
    }
    for (i = 0; i < argc; i++) {
        x->b.last_binsplit[i] = x->b.current_binsplit[i];
        x->b.current_binsplit[i] = (int)atom_getlong(argv + i);
    }
    splitspec_begin_transition(x);
    systhread_mutex_unlock(x->lock);
}

// REPORT CURRENT SHUFFLE STATUS
void splitspec_showstate(t_splitspec *x)
{
    t_atom *list_data;
    int i, N2;

    if (!splitspec_lock_ready(x))
        return;
    N2 = x->N2;
    list_data = (t_atom *)sysmem_newptrclear(N2 * sizeof(t_atom));
    if (!list_data) {
        systhread_mutex_unlock(x->lock);
        object_error((t_object *)x, "out of memory");
        return;
    }
    for (i = 0; i < N2; i++) {
        atom_setfloat(list_data + i, (t_double)x->b.current_binsplit[i]);
    }
    systhread_mutex_unlock(x->lock);   // never output while holding the lock: downstream may message us back
    outlet_list(x->list_outlet, 0L, N2, list_data);
    sysmem_freeptr(list_data);
}

/* ------------------------------------------------------ bin distribution */

void splitspeci(int *current_binsplit, int *last_binsplit, int bin_offset, int table_offset,
                t_double *current_mag, t_double *last_mag, t_double *inmag, t_double *dest_mag,
                int start, int end, int n, t_double oldfrac, t_double newfrac)
{
    int i;
    int bindex;

    for (i = 0; i < n; i++) {
        last_mag[i] = current_mag[i] = 0.0;
    }
    for (i = start; i < end; i++) {
        bindex = current_binsplit[(i + table_offset) % n];
        bindex = (bindex + bin_offset) % n;
        current_mag[bindex] = inmag[bindex];
        bindex = last_binsplit[(i + table_offset) % n];
        bindex = (bindex + bin_offset) % n;
        last_mag[bindex] = inmag[bindex];
    }
    for (i = 0; i < n; i++) {
        if (!current_mag[i] && !last_mag[i]) {
            dest_mag[i] = 0.0;
        } else {
            dest_mag[i] = oldfrac * last_mag[i] + newfrac * current_mag[i];
        }
    }
}

void splitspec(int *binsplit, int bin_offset, int table_offset,
               t_double *inmag, t_double *dest_mag, int start, int end, int n)
{
    int i;
    int bindex;
    // n is actually N2
    for (i = start; i < end; i++) {
        bindex = binsplit[(i + table_offset) % n];
        bindex = (bindex + bin_offset) % n;
        dest_mag[bindex] = inmag[bindex];
    }
}

/* -------------------------------------------------------------------- DSP */

static void splitspec_process(t_splitspec *x, double **ins, double **outs, long n)
{
    int i, j;
    int channel_count = x->channel_count;

    t_double *inmag = ins[0];
    t_double *inphase = ins[1];
    t_double *t_offset = ins[2];
    t_double *b_offset = ins[3];
    t_double *manual_control = ins[4];

    t_double **magvecs = x->magvecs;
    t_double **phasevecs = x->phasevecs;

    int table_offset, bin_offset;

    int *current_binsplit = x->b.current_binsplit;
    int *last_binsplit = x->b.last_binsplit;
    t_double *last_mag = x->b.last_mag;
    t_double *current_mag = x->b.current_mag;
    long counter = x->counter;
    long countdown_samps = x->countdown_samps;
    t_double frac, oldgain, newgain;

    int hopsamps = x->hopsamps;
    int N2 = x->N2;

    // n == N2: the number of "bins", and also the number of values in each signal vector

    // assign local vector pointers
    for (i = 0, j = 0; i < channel_count * 2; i += 2, j++) {
        magvecs[j] = outs[i];
        phasevecs[j] = outs[i + 1];
    }

    // sanitized: NaN/inf/huge offsets must never reach the modulo index math
    table_offset = (int)(splitspec_clip(*t_offset, -1e4, 1e4) * n);
    bin_offset = (int)(splitspec_clip(*b_offset, -1e4, 1e4) * n);
    if (table_offset < 0)
        table_offset = -table_offset;
    if (bin_offset < 0)
        bin_offset = -bin_offset;

    if (x->bypass) {
        for (i = 0; i < n; i++) {
            for (j = 0; j < channel_count; j++) {
                magvecs[j][i] = inmag[i] * 0.5;
                phasevecs[j][i] = inphase[i];
            }
        }
        return;
    }

    // ZERO OUT MAGNITUDES AND COPY PHASES TO OUTPUT
    for (i = 0; i < n; i++) {
        for (j = 0; j < channel_count; j++) {
            magvecs[j][i] = 0.0;
            phasevecs[j][i] = inphase[i];
        }
    }

    // Special case of live control over interpolation
    if (x->manual_override) {
        frac = splitspec_clip(*manual_control, 0.0, 1.0);
        oldgain = cos(frac * PIOVERTWO);
        newgain = sin(frac * PIOVERTWO);

        for (j = 0; j < channel_count; j++) {
            splitspeci(current_binsplit, last_binsplit, bin_offset, table_offset,
                       current_mag, last_mag, inmag, magvecs[j],
                       N2 * j / channel_count, N2 * (j + 1) / channel_count, N2, oldgain, newgain);
        }
        return;
    }

    // Normal operation
    if (x->new_distribution) {
        x->new_distribution = 0;
        counter = 0;                      // (re)start the ramp here, on the audio thread
        x->interpolation_completed = 0;
        // put out contents of last distribution
        for (j = 0; j < channel_count; j++) {
            splitspec(last_binsplit, bin_offset, table_offset, inmag, magvecs[j],
                      N2 * j / channel_count, N2 * (j + 1) / channel_count, N2);
        }
        frac = 0.0;

    } else if (x->interpolation_completed || countdown_samps <= 0) {
        // put out contents of current distribution
        for (j = 0; j < channel_count; j++) {
            splitspec(current_binsplit, bin_offset, table_offset, inmag, magvecs[j],
                      N2 * j / channel_count, N2 * (j + 1) / channel_count, N2);
        }
        frac = 1.0;
    } else {
        // do interpolation
        frac = splitspec_clip((t_double)counter / (t_double)countdown_samps, 0.0, 1.0);
        oldgain = cos(frac * PIOVERTWO);
        newgain = sin(frac * PIOVERTWO);
        for (j = 0; j < channel_count; j++) {
            splitspeci(current_binsplit, last_binsplit, bin_offset, table_offset,
                       current_mag, last_mag, inmag, magvecs[j],
                       N2 * j / channel_count, N2 * (j + 1) / channel_count, N2, oldgain, newgain);
        }
        // end of interpolation
        counter += hopsamps;
        if (counter >= countdown_samps) {
            counter = countdown_samps;
            x->interpolation_completed = 1;
        }
    }

    x->fl_phase = frac;
    if (x->phase_clock)
        clock_delay(x->phase_clock, 0.0);   // send current phase to float outlet from the scheduler thread
    x->counter = counter;
}

void splitspec_perform64(t_splitspec *x, t_object *dsp64, double **ins,
                         long numins, double **outs,
                         long numouts, long n,
                         long flags, void *userparam)
{
    long i, j;

    // Silence if the frame size doesn't match what dsp64 planned for, the buffers aren't built,
    // the vector counts are wrong, or the main thread is editing/rebuilding (never block).
    if (numins < 5 || numouts < x->channel_count * 2 || n != x->N2 || !x->b.current_binsplit
        || systhread_mutex_trylock(x->lock)) {
        for (i = 0; i < numouts; i++) {
            for (j = 0; j < n; j++) {
                outs[i][j] = 0.0;
            }
        }
        return;
    }
    splitspec_process(x, ins, outs, n);
    systhread_mutex_unlock(x->lock);
}

void splitspec_dsp64(t_splitspec *x, t_object *dsp64, short *count, double R,
                     long vector_size, long flags)
{
    int hop;
    int N2;
    t_ssbufs fresh, old;
    int rebuilt = 0;

    if (!(R >= 1.0) || vector_size < 1)
        return;

    N2 = (int)vector_size;

    if (!splitspec_ready(x) || x->N2 != N2) {
        // build the new buffers off the audio path, then swap them in under the lock
        if (splitspec_bufs_alloc(&fresh, N2)) {
            object_error((t_object *)x, "out of memory sizing buffers for %d bins; output muted", N2);
        } else {
            systhread_mutex_lock(x->lock);
            old = x->b;
            x->b = fresh;
            x->N2 = N2;
            x->N = N2 * 2;
            splitspec_shuffle(x);
            memcpy(x->b.last_binsplit, x->b.current_binsplit, (size_t)N2 * sizeof(int));
            x->new_distribution = 1;
            x->interpolation_completed = 0;
            x->counter = 0;
            systhread_mutex_unlock(x->lock);
            splitspec_bufs_free(&old);
            rebuilt = 1;
        }
    }
    (void)rebuilt;

    systhread_mutex_lock(x->lock);
    x->sr = R;
    // ramp time is kept in seconds so a user "ramptime" survives rate changes
    x->countdown_samps = (long)(x->ramp_sec * R);
    if (x->counter > x->countdown_samps)
        x->counter = x->countdown_samps;
    if (x->N > 0) {
        hop = x->N / x->overlap_factor;
        x->hopsamps = hop < 1 ? 1 : hop;
    }
    systhread_mutex_unlock(x->lock);

    object_method(dsp64, gensym("dsp_add64"), x, splitspec_perform64, 0, NULL);
}
