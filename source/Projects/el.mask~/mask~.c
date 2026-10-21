/**
 * @file mask~.c
 * @brief Dynamic multi-pattern impulse mask and rhythmic gate for Max 8/9.
 * Modernized for 64-bit dsp64, full pattern matrix deallocation, and audio-thread safety.
 */

#include "MSPd.h"

#define MAXLEN (512)
#define MAXMASKS (1024)
#define MAXSEQ (1024)
#define OBJECT_NAME "mask~"

static t_class *mask_class;

typedef struct {
    t_double *pat; // Mask pattern values
    int length;    // Length of pattern
} t_maskpat;

typedef struct {
    int *seq;      // Sequence array of pattern indices
    int length;    // Length of sequence
    int phase;     // Playhead position in sequence
} t_sequence;

typedef struct _mask {
    t_pxobject x_obj;
    short mute;
    short gate;
    short phaselock;
    long indexmode;
    int phase;
    int current_mask;
    t_maskpat *masks;
    t_sequence sequence;
    int *stored_masks;
    int pattern_count;
    short noloop;
    t_double *in_vec;
} t_mask;

/* Method Prototypes */
void *mask_new(t_symbol *msg, long argc, t_atom *argv);
void mask_free(t_mask *x);
void mask_assist(t_mask *x, void *b, long m, long a, char *s);
void mask_mute(t_mask *x, double f);
void mask_phaselock(t_mask *x, double f);
void mask_gate(t_mask *x, double f);
void mask_addmask(t_mask *x, t_symbol *msg, long argc, t_atom *argv);
void mask_recall(t_mask *x, double p);
void mask_showmask(t_mask *x, double p);
void mask_gozero(t_mask *x);
void mask_sequence(t_mask *x, t_symbol *msg, long argc, t_atom *argv);
void mask_noloop(t_mask *x, double f);
void mask_playonce(t_mask *x, double pnum);
void mask_dsp64(t_mask *x, t_object *dsp64, short *count, double sr, long n, long flags);
void mask_perform64(t_mask *x, t_object *dsp64, double **ins, 
                    long numins, double **outs, long numouts, long n,
                    long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.mask~", 
                           (method)mask_new, 
                           (method)mask_free, 
                           (long)sizeof(t_mask), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)mask_dsp64,     "dsp64",     A_CANT, 0);
    class_addmethod(c, (method)mask_assist,    "assist",    A_CANT, 0);
    class_addmethod(c, (method)mask_mute,      "mute",      A_FLOAT, 0);
    class_addmethod(c, (method)mask_phaselock, "phaselock", A_FLOAT, 0);
    class_addmethod(c, (method)mask_gate,      "gate",      A_FLOAT, 0);
    class_addmethod(c, (method)mask_addmask,   "addmask",   A_GIMME, 0);
    class_addmethod(c, (method)mask_sequence,  "sequence",  A_GIMME, 0);
    class_addmethod(c, (method)mask_recall,    "recall",    A_FLOAT, 0);
    class_addmethod(c, (method)mask_showmask,  "showmask",  A_FLOAT, 0);
    class_addmethod(c, (method)mask_noloop,    "noloop",    A_FLOAT, 0);
    class_addmethod(c, (method)mask_playonce,  "playonce",  A_FLOAT, 0);
    class_addmethod(c, (method)mask_gozero,    "gozero",    0);

    CLASS_ATTR_LONG(c, "indexmode", 0, t_mask, indexmode);
    CLASS_ATTR_DEFAULT_SAVE(c, "indexmode", 0, "0");
    CLASS_ATTR_ENUMINDEX(c, "indexmode", 0, "Off On");
    CLASS_ATTR_LABEL(c, "indexmode", 0, "Index Mode");

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    mask_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void mask_playonce(t_mask *x, double pnum)
{
    x->noloop = 1;
    x->mute = 0;
    mask_recall(x, pnum);
}

void mask_gozero(t_mask *x)
{
    x->phase = 0;
}

void mask_mute(t_mask *x, double f)
{
    x->mute = (short)(f != 0.0);
}

void mask_noloop(t_mask *x, double f)
{
    x->noloop = (short)(f != 0.0);
}

void mask_phaselock(t_mask *x, double f)
{
    x->phaselock = (short)(f != 0.0);
}

void mask_gate(t_mask *x, double f)
{
    x->gate = (short)(f != 0.0);
}

void mask_showmask(t_mask *x, double p)
{
    int location = (int)p;
    if (location < 0 || location >= MAXMASKS || !x->masks[location].pat) {
        object_error((t_object *)x, "%s: no pattern stored at location %d", OBJECT_NAME, location);
        return;
    }

    int len = x->masks[location].length;
    post("%s: pattern %d length is %d", OBJECT_NAME, location, len);
    for (int i = 0; i < len; i++) {
        post("%d: %f", i, x->masks[location].pat[i]);
    }
}

void mask_recall(t_mask *x, double p)
{
    int location = (int)p;
    if (location < 0 || location >= MAXMASKS || !x->masks[location].pat) {
        object_error((t_object *)x, "%s: no pattern stored at location %d", OBJECT_NAME, location);
        return;
    }

    x->current_mask = location;
    if (!x->phaselock) {
        x->phase = 0;
    }
}

void mask_sequence(t_mask *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 1) {
        object_error((t_object *)x, "%s: sequence requires at least 1 mask index", OBJECT_NAME);
        return;
    }
    if (argc > MAXSEQ) argc = MAXSEQ;

    for (int i = 0; i < argc; i++) {
        x->sequence.seq[i] = (int)atom_getfloat(argv + i);
    }

    if (x->sequence.seq[0] < 0) {
        x->sequence.length = 0;
        return;
    }

    x->sequence.phase = 0;
    x->sequence.length = (int)argc;
    mask_recall(x, (double)x->sequence.seq[x->sequence.phase++]);
}

void mask_addmask(t_mask *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 2) {
        object_error((t_object *)x, "%s: must specify location and mask values", OBJECT_NAME);
        return;
    }
    if (argc > MAXLEN + 1) {
        argc = MAXLEN + 1;
    }

    int location = (int)atom_getfloat(argv);
    if (location < 0 || location >= MAXMASKS) {
        object_error((t_object *)x, "%s: illegal location index %d", OBJECT_NAME, location);
        return;
    }

    if (x->masks[location].pat == NULL) {
        x->masks[location].pat = (t_double *)sysmem_newptrclear(MAXLEN * sizeof(t_double));
        if (x->pattern_count < MAXMASKS) {
            x->stored_masks[x->pattern_count++] = location;
        }
    }

    x->masks[location].length = (int)(argc - 1);
    for (int i = 1; i < argc; i++) {
        x->masks[location].pat[i - 1] = atom_getfloat(argv + i);
    }
}

void mask_free(t_mask *x)
{
    dsp_free((t_pxobject *)x);

    // Completely free all allocated patterns across all possible slots
    if (x->masks) {
        for (int i = 0; i < MAXMASKS; i++) {
            if (x->masks[i].pat) {
                sysmem_freeptr(x->masks[i].pat);
                x->masks[i].pat = NULL;
            }
        }
        sysmem_freeptr(x->masks);
        x->masks = NULL;
    }

    if (x->stored_masks)  { sysmem_freeptr(x->stored_masks); x->stored_masks = NULL; }
    if (x->sequence.seq)  { sysmem_freeptr(x->sequence.seq); x->sequence.seq = NULL; }
    if (x->in_vec)        { sysmem_freeptr(x->in_vec); x->in_vec = NULL; }
}

void *mask_new(t_symbol *msg, long argc, t_atom *argv)
{
    t_mask *x = (t_mask *)object_alloc(mask_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);
    outlet_new((t_object *)x, "signal");
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->masks        = (t_maskpat *)sysmem_newptrclear(MAXMASKS * sizeof(t_maskpat));
    x->stored_masks = (int *)sysmem_newptrclear(MAXMASKS * sizeof(int));
    x->sequence.seq = (int *)sysmem_newptrclear(MAXSEQ * sizeof(int));
    x->in_vec       = (t_double *)sysmem_newptrclear(8192 * sizeof(t_double));

    x->sequence.length = 0;
    x->sequence.phase  = 0;
    x->current_mask    = -1;
    x->pattern_count   = 0;

    for (int i = 0; i < MAXMASKS; i++) {
        x->stored_masks[i] = -1;
        x->masks[i].pat = NULL;
        x->masks[i].length = 0;
    }

    int argoffset = attr_args_offset((short)argc, argv);
    if (argoffset > 0) {
        if (argoffset > MAXLEN) argoffset = MAXLEN;
        x->masks[0].pat = (t_double *)sysmem_newptrclear(MAXLEN * sizeof(t_double));
        x->masks[0].length = argoffset;
        for (int i = 0; i < argoffset; i++) {
            x->masks[0].pat[i] = atom_getfloat(argv + i);
        }
        x->current_mask = 0;
        x->stored_masks[0] = 0;
        x->pattern_count = 1;
    }

    x->indexmode = 0;
    x->mute      = 0;
    x->gate      = 1;
    x->phaselock = 0;
    x->phase     = 0;
    x->noloop    = 0;

    attr_args_process(x, (short)argc, argv);
    return x;
}

void mask_perform64(t_mask *x, t_object *dsp64, double **ins, 
                    long numins, double **outs, long numouts, long n,
                    long flags, void *userparam)
{
    t_double *inlet  = ins[0];
    t_double *outlet = outs[0];

    // Clear outlet buffer
    for (long i = 0; i < n; i++) {
        outlet[i] = 0.0;
    }

    int current_mask = x->current_mask;
    if (x->mute || current_mask < 0 || current_mask >= MAXMASKS || 
        !x->masks[current_mask].pat || x->masks[current_mask].length <= 0) {
        return;
    }

    int phase         = x->phase;
    short gate        = x->gate;
    long indexmode    = x->indexmode;
    short noloop      = x->noloop;
    t_maskpat *masks  = x->masks;
    int seq_len       = x->sequence.length;
    int seq_phs       = x->sequence.phase;
    int *seq_arr      = x->sequence.seq;

    for (long i = 0; i < n; i++) {
        double in_val = inlet[i];

        if (in_val != 0.0) {
            int pat_len = masks[current_mask].length;
            if (pat_len <= 0) pat_len = 1;

            if (indexmode) {
                long p = (long)in_val - 1;
                while (p < 0) p += pat_len;
                phase = (int)(p % pat_len);
            }

            if (gate && masks[current_mask].pat) {
                outlet[i] = masks[current_mask].pat[phase];
            }

            ++phase;
            if (phase >= pat_len) {
                phase = 0;
                if (noloop) {
                    x->mute = 1;
                    break;
                }
                // Step sequence internally without triggering external error methods
                if (seq_len > 0) {
                    int next_loc = seq_arr[seq_phs++];
                    if (seq_phs >= seq_len) seq_phs = 0;

                    if (next_loc >= 0 && next_loc < MAXMASKS && masks[next_loc].pat) {
                        current_mask = next_loc;
                        x->current_mask = next_loc;
                        if (!x->phaselock) {
                            phase = 0;
                        }
                    }
                }
            }
        }
    }

    x->phase = phase;
    x->sequence.phase = seq_phs;
}

void mask_dsp64(t_mask *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)mask_perform64, 0, NULL);
}

void mask_assist(t_mask *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(signal) Trigger Impulses", 256);
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Masked Impulses", 256);
    }
}