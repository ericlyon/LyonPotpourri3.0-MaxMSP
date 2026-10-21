/**
 * @file sigseq~.c
 * @brief Signal-rate step sequencer with ADSR envelope generator for Max 8/9.
 * Modernized for 64-bit dsp64, division-by-zero envelope safety, and zero-leak teardown.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define MAX_SEQ (1024)
#define INTERNAL_CLOCK (1)
#define EXTERNAL_CLOCK (2)
#define OBJECT_NAME "el.sigseq~"

static t_class *sigseq_class = NULL;

typedef struct _sigseq
{
    t_pxobject x_obj;

    double    *sequence;
    long       seq_len;
    long       seq_ptr;
    long       bang_ptr;
    double     tempo;
    long       beat_subdiv;
    long       tsamps;
    long       counter;
    double     val;
    void      *mybang;
    void      *m_clock;
    double     flat_gain;
    double     last_val;
    short      retro_state;
    short      rand_state;

    // ADSR Enveloping
    double     a;
    double     d;
    double     s;
    double     r;
    long       ebreak1;
    long       ebreak2;
    long       ebreak3;
    long       asamps;
    long       dsamps;
    long       ssamps;
    long       rsamps;
    double     egain;
    int        do_envelope;
    int        bang_on;
    short      mute;
    int        method;
    double     sr;
    uint32_t   rand_seed;
} t_sigseq;

// Prototypes
void *sigseq_new(t_symbol *s, short argc, t_atom *argv);
void  sigseq_free(t_sigseq *x);
void  sigseq_assist(t_sigseq *x, void *b, long m, long a, char *s);
void  sigseq_list(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv);
void  sigseq_adsr(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv);
void  sigseq_adsrgate(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv);
void  sigseq_banggate(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv);
void  sigseq_tempo(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv);
void  sigseq_retro(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv);
void  sigseq_rand(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv);
void  sigseq_mute(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv);
void  sigseq_tick(t_sigseq *x);
void  sigseq_internal_clock(t_sigseq *x, double toggle);
void  sigseq_external_clock(t_sigseq *x, double toggle);
void  sigseq_gozero(t_sigseq *x);
void  sigseq_recompute_durations(t_sigseq *x);
void  sigseq_dsp64(t_sigseq *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  sigseq_perform64(t_sigseq *x, t_object *dsp64, double **ins, 
                        long numins, double **outs, long numouts, long n,
                        long flags, void *userparam);
void  sigseq_perform_clickin64(t_sigseq *x, t_object *dsp64, double **ins, 
                                long numins, double **outs, long numouts, long n,
                                long flags, void *userparam);

static inline double xorshift32_norm(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return (double)x * (1.0 / 4294967296.0);
}

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.sigseq~", 
                           (method)sigseq_new, 
                           (method)sigseq_free, 
                           sizeof(t_sigseq), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)sigseq_dsp64,          "dsp64",          A_CANT,  0);
    class_addmethod(c, (method)sigseq_assist,         "assist",         A_CANT,  0);
    class_addmethod(c, (method)sigseq_list,           "list",           A_GIMME, 0);
    class_addmethod(c, (method)sigseq_adsr,           "sigseq",         A_GIMME, 0);
    class_addmethod(c, (method)sigseq_adsrgate,       "adsrgate",       A_GIMME, 0);
    class_addmethod(c, (method)sigseq_banggate,       "banggate",       A_GIMME, 0);
    class_addmethod(c, (method)sigseq_tempo,          "tempo",          A_GIMME, 0);
    class_addmethod(c, (method)sigseq_retro,          "retro",          A_GIMME, 0);
    class_addmethod(c, (method)sigseq_rand,           "rand",           A_GIMME, 0);
    class_addmethod(c, (method)sigseq_mute,           "mute",           A_GIMME, 0);
    class_addmethod(c, (method)sigseq_gozero,         "gozero",         0);
    class_addmethod(c, (method)sigseq_internal_clock, "internal_clock", A_FLOAT, 0);
    class_addmethod(c, (method)sigseq_external_clock, "external_clock", A_FLOAT, 0);

    class_dspinit(c); // Crucial: installs MSP "signal" method binding
    class_register(CLASS_BOX, c);
    sigseq_class = c;

    potpourri_announce(OBJECT_NAME);
}

void *sigseq_new(t_symbol *s, short argc, t_atom *argv)
{
    t_sigseq *x = (t_sigseq *)object_alloc(sigseq_class);
    if (!x) return NULL;

    // Outlets are created from right to left in Max
    x->mybang = bangout((t_object *)x);             // Outlet 2: Bang on start
    outlet_new((t_pxobject *)x, "signal");          // Outlet 1: ADSR envelope
    outlet_new((t_pxobject *)x, "signal");          // Outlet 0: Sequence value

    dsp_setup((t_pxobject *)x, 1);                  // Inlet 0: Audio trigger input
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->m_clock = clock_new(x, (method)sigseq_tick);

    x->sequence = (double *)sysmem_newptr(MAX_SEQ * sizeof(double));
    if (!x->sequence) {
        object_free(x);
        return NULL;
    }

    x->tempo       = (argc > 0) ? atom_getfloatarg(0, argc, argv) : 120.0;
    x->beat_subdiv = (argc > 1) ? (long)atom_getfloatarg(1, argc, argv) : 2;
    x->flat_gain   = (argc > 2) ? atom_getfloatarg(2, argc, argv) : 0.5;

    if (x->tempo <= 0.0) x->tempo = 120.0;
    if (x->beat_subdiv <= 0) x->beat_subdiv = 2;
    if (x->flat_gain <= 0.0) x->flat_gain = 0.5;

    x->sr = sys_getsr();
    if (x->sr <= 0.0) x->sr = 44100.0;

    x->seq_len     = 3;
    x->seq_ptr     = 0;
    x->bang_ptr    = 0;
    x->sequence[0] = 313.0;
    x->sequence[1] = 511.0;
    x->sequence[2] = 71.0;
    x->method      = INTERNAL_CLOCK;
    x->a           = 0.005;
    x->d           = 0.01;
    x->r           = 0.2;
    x->egain       = 0.707;
    x->do_envelope = 1;
    x->bang_on     = 0;
    x->retro_state = 0;
    x->rand_state  = 0;
    x->mute        = 0;
    x->val         = x->sequence[0];
    x->last_val    = -1.0;

    x->rand_seed   = (uint32_t)((uintptr_t)x ^ 0x12345678U);

    sigseq_recompute_durations(x);
    x->counter = x->tsamps;

    return x;
}

void sigseq_free(t_sigseq *x)
{
    dsp_free((t_pxobject *)x);

    if (x->m_clock) {
        object_free((t_object *)x->m_clock);
        x->m_clock = NULL;
    }
    if (x->sequence) {
        sysmem_freeptr(x->sequence);
        x->sequence = NULL;
    }
}

// -------------------------------------------------------------------------
// Timing & Parameters
// -------------------------------------------------------------------------

void sigseq_recompute_durations(t_sigseq *x)
{
    double beatdur = (60.0 / x->tempo) / (double)x->beat_subdiv;
    x->tsamps = (long)(x->sr * beatdur);
    if (x->tsamps < 1) x->tsamps = 1;

    x->asamps = (long)(x->sr * x->a);
    x->dsamps = (long)(x->sr * x->d);
    x->rsamps = (long)(x->sr * x->r);

    if (x->asamps < 0) x->asamps = 0;
    if (x->dsamps < 0) x->dsamps = 0;
    if (x->rsamps < 0) x->rsamps = 0;

    x->ssamps = x->tsamps - (x->asamps + x->dsamps + x->rsamps);
    if (x->ssamps < 0) x->ssamps = 0;

    x->ebreak1 = x->asamps;
    x->ebreak2 = x->asamps + x->dsamps;
    x->ebreak3 = x->asamps + x->dsamps + x->ssamps;
}

void sigseq_tick(t_sigseq *x)
{
    outlet_bang(x->mybang);
}

void sigseq_gozero(t_sigseq *x)
{
    if (x->seq_len > 0) {
        x->seq_ptr = x->seq_len - 1;
    }
}

void sigseq_internal_clock(t_sigseq *x, double toggle)
{
    x->method = (toggle != 0.0) ? INTERNAL_CLOCK : EXTERNAL_CLOCK;
}

void sigseq_external_clock(t_sigseq *x, double toggle)
{
    x->method = (toggle != 0.0) ? EXTERNAL_CLOCK : INTERNAL_CLOCK;
}

void sigseq_mute(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc > 0) x->mute = (short)(atom_getfloatarg(0, argc, argv) != 0.0);
}

void sigseq_rand(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc > 0) x->rand_state = (short)(atom_getfloatarg(0, argc, argv) != 0.0);
}

void sigseq_retro(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc > 0) x->retro_state = (short)(atom_getfloatarg(0, argc, argv) != 0.0);
}

void sigseq_adsrgate(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc > 0) x->do_envelope = (int)(atom_getfloatarg(0, argc, argv) != 0.0);
}

void sigseq_banggate(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc > 0) x->bang_on = (int)(atom_getfloatarg(0, argc, argv) != 0.0);
}

void sigseq_tempo(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc > 0) {
        double t = atom_getfloatarg(0, argc, argv);
        if (t > 0.0) {
            x->tempo = t;
            sigseq_recompute_durations(x);
        }
    }
}

void sigseq_list(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 1 || !argv) return;

    long count = (argc > MAX_SEQ) ? MAX_SEQ : argc;
    for (long i = 0; i < count; i++) {
        x->sequence[i] = atom_getfloatarg(i, argc, argv);
    }
    x->seq_len = count;
    x->seq_ptr = 0;
    x->val     = x->sequence[0];
    x->counter = 0;
}

void sigseq_adsr(t_sigseq *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc != 4 || !argv) {
        object_error((t_object *)x, "%s: requires 4 arguments (attack, decay, release, gain)", OBJECT_NAME);
        return;
    }
    x->a     = atom_getfloatarg(0, argc, argv) * 0.001;
    x->d     = atom_getfloatarg(1, argc, argv) * 0.001;
    x->r     = atom_getfloatarg(2, argc, argv) * 0.001;
    x->egain = atom_getfloatarg(3, argc, argv);

    sigseq_recompute_durations(x);
}

void sigseq_assist(t_sigseq *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        snprintf(dst, 256, "(signal) External Clock Trigger");
    } else if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0: snprintf(dst, 256, "(signal) Sequence Value Output"); break;
            case 1: snprintf(dst, 256, "(signal) ADSR Envelope Output"); break;
            case 2: snprintf(dst, 256, "(bang) On Sequence Loop Start"); break;
        }
    }
}

// -------------------------------------------------------------------------
// Real-Time DSP Perform Routines
// -------------------------------------------------------------------------

void sigseq_dsp64(t_sigseq *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;

    if (x->sr != sr) {
        x->sr = sr;
        sigseq_recompute_durations(x);
    }

    if (x->method == EXTERNAL_CLOCK) {
        object_method(dsp64, gensym("dsp_add64"), x, sigseq_perform_clickin64, 0, NULL);
    } else {
        object_method(dsp64, gensym("dsp_add64"), x, sigseq_perform64, 0, NULL);
    }
}

void sigseq_perform64(t_sigseq *x, t_object *dsp64, double **ins, 
                      long numins, double **outs, long numouts, long n,
                      long flags, void *userparam)
{
    double *out    = outs[0];
    double *sigseq = outs[1];

    if (x->mute) {
        memset(out, 0, (size_t)n * sizeof(double));
        memset(sigseq, 0, (size_t)n * sizeof(double));
        return;
    }

    const double *sequence    = x->sequence;
    long          seq_len     = x->seq_len;
    long          seq_ptr     = x->seq_ptr;
    long          bang_ptr    = x->bang_ptr;
    long          tsamps      = x->tsamps;
    long          counter     = x->counter;
    double        val         = x->val;
    long          ebreak1     = x->ebreak1;
    long          ebreak2     = x->ebreak2;
    long          ebreak3     = x->ebreak3;
    long          asamps      = x->asamps;
    long          dsamps      = x->dsamps;
    long          rsamps      = x->rsamps;
    double        egain       = x->egain;
    double        flat_gain   = x->flat_gain;
    int           do_envelope = x->do_envelope;
    int           bang_on     = x->bang_on;
    short         retro_state = x->retro_state;
    short         rand_state  = x->rand_state;
    uint32_t      seed        = x->rand_seed;

    for (long i = 0; i < n; i++) {
        if (counter >= tsamps) {
            counter = 0;
            bang_ptr = (bang_ptr + 1) % seq_len;

            if (rand_state) {
                seq_ptr = (long)(xorshift32_norm(&seed) * (double)seq_len) % seq_len;
            } else if (retro_state) {
                seq_ptr = (seq_ptr - 1 + seq_len) % seq_len;
            } else {
                seq_ptr = (seq_ptr + 1) % seq_len;
            }

            val = sequence[seq_ptr];
            if (bang_ptr == 0 && bang_on) {
                clock_delay(x->m_clock, 0);
            }
        }

        out[i] = val;

        if (do_envelope) {
            double env_val;
            if (counter < ebreak1) {
                env_val = (asamps > 0) ? ((double)counter / (double)asamps) : 1.0;
            } else if (counter < ebreak2) {
                double etmp = (dsamps > 0) ? ((double)(counter - ebreak1) / (double)dsamps) : 1.0;
                env_val = (1.0 - etmp) + (egain * etmp);
            } else if (counter < ebreak3) {
                env_val = egain;
            } else if (counter < tsamps) {
                env_val = (rsamps > 0) ? (((double)(tsamps - counter) / (double)rsamps) * egain) : 0.0;
            } else {
                env_val = 0.0;
            }
            sigseq[i] = env_val;
        } else {
            sigseq[i] = flat_gain;
        }
        counter++;
    }

    x->seq_ptr   = seq_ptr;
    x->bang_ptr  = bang_ptr;
    x->counter   = counter;
    x->val       = val;
    x->rand_seed = seed;
}

void sigseq_perform_clickin64(t_sigseq *x, t_object *dsp64, double **ins, 
                              long numins, double **outs, long numouts, long n,
                              long flags, void *userparam)
{
    const double *trigger = ins[0];
    double       *out     = outs[0];
    double       *sigseq  = outs[1];

    if (x->mute) {
        memset(out, 0, (size_t)n * sizeof(double));
        memset(sigseq, 0, (size_t)n * sizeof(double));
        return;
    }

    const double *sequence    = x->sequence;
    long          seq_len     = x->seq_len;
    long          seq_ptr     = x->seq_ptr;
    long          tsamps      = x->tsamps;
    long          counter     = x->counter;
    double        val         = x->val;
    long          ebreak1     = x->ebreak1;
    long          ebreak2     = x->ebreak2;
    long          ebreak3     = x->ebreak3;
    long          asamps      = x->asamps;
    long          dsamps      = x->dsamps;
    long          rsamps      = x->rsamps;
    double        egain       = x->egain;
    double        flat_gain   = x->flat_gain;
    int           do_envelope = x->do_envelope;
    int           bang_on     = x->bang_on;
    short         retro_state = x->retro_state;
    short         rand_state  = x->rand_state;
    uint32_t      seed        = x->rand_seed;

    for (long i = 0; i < n; i++) {
        if (trigger[i] != 0.0) {
            counter = 0;

            if (rand_state) {
                seq_ptr = (long)(xorshift32_norm(&seed) * (double)seq_len) % seq_len;
            } else if (retro_state) {
                seq_ptr = (seq_ptr - 1 + seq_len) % seq_len;
            } else {
                seq_ptr = (seq_ptr + 1) % seq_len;
            }

            val = sequence[seq_ptr];
            if (seq_ptr == 0 && bang_on) {
                clock_delay(x->m_clock, 0);
            }
        }

        out[i] = val;

        if (do_envelope) {
            double env_val;
            if (counter < ebreak1) {
                env_val = (asamps > 0) ? ((double)counter / (double)asamps) : 1.0;
            } else if (counter < ebreak2) {
                double etmp = (dsamps > 0) ? ((double)(counter - ebreak1) / (double)dsamps) : 1.0;
                env_val = (1.0 - etmp) + (egain * etmp);
            } else if (counter < ebreak3) {
                env_val = egain;
            } else if (counter < tsamps) {
                env_val = (rsamps > 0) ? (((double)(tsamps - counter) / (double)rsamps) * egain) : 0.0;
            } else {
                env_val = 0.0;
            }
            sigseq[i] = env_val;
        } else {
            sigseq[i] = flat_gain;
        }
        counter++;
    }

    x->seq_ptr   = seq_ptr;
    x->counter   = counter;
    x->val       = val;
    x->rand_seed = seed;
}