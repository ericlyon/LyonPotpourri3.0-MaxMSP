/**
 * @file oscil~.c
 * @brief Additive waveform and wavetable oscillator for Max 8/9.
 * Modernized for 64-bit dsp64, zero-leak heap teardown, and click-free crossfade stability.
 */

#include "MSPd.h"
#include <math.h>
#include <string.h>

#define OSCIL_MAXIMUM_HARMONICS (1024)
#define OSCIL_DEFAULT_FLEN (8192)
#define OSCIL_MAX_FLEN (1048576)
#define OSCIL_DEFAULT_HARMONICS (10)
#define OSCIL_INIT_FREQ (440.0)
#define OSCIL_DEFAULT_WAVEFORM "sine"
#define OSCIL_NOFADE (0)
#define OSCIL_LINEAR (1)
#define OSCIL_POWER (2)

#define OBJECT_NAME "oscil~"

static t_class *oscil_class;

typedef struct _oscil {
    t_pxobject x_obj;
    int table_length;
    t_double *wavetable64;
    int harmonic_count;
    float *harmonic_weights;
    float *harmonic_phases;
    double phase;
    double phase_offset;
    double si_factor;
    double si;
    int bl_harms;
    t_double piotwo;
    t_double twopi;
    float sr;
    short mute;
    short connected[4];
    t_double *old_wavetable64;
    short dirty;
    float fade_ms;
    int fade_samples;
    int fade_countdown;
    short fadetype;
    short firsttime;
    short fade_in_progress;
    short interpolate;
} t_oscil;

/* Method Prototypes */
void *oscil_new(t_symbol *s, long argc, t_atom *argv);
void oscil_dsp_free(t_oscil *x);
void build_waveform64(t_oscil *x);
void build_amph_waveform64(t_oscil *x);
void oscil_mute(t_oscil *x, double flag);
void oscil_assist(t_oscil *x, void *b, long msg, long arg, char *dst);
void oscil_float(t_oscil *x, double f);
void oscil_int(t_oscil *x, long i);
void oscil_sine(t_oscil *x);
void oscil_triangle(t_oscil *x);
void oscil_square(t_oscil *x);
void oscil_sawtooth(t_oscil *x);
void oscil_buzz(t_oscil *x);
void oscil_list(t_oscil *x, t_symbol *msg, long argc, t_atom *argv);
void oscil_amph(t_oscil *x, t_symbol *msg, long argc, t_atom *argv);
void oscil_fadetime(t_oscil *x, double fade_ms);
void oscil_fadetype(t_oscil *x, double ftype);
void oscil_harmcount(t_oscil *x, double harms);
void oscil_interpolate(t_oscil *x, double tog);
void oscil_dsp64(t_oscil *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void oscil_perform_interpolate64(t_oscil *x, t_object *dsp64, double **ins,
                                 long numins, double **outs, long numouts, long n,
                                 long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.oscil~", 
                           (method)oscil_new, 
                           (method)oscil_dsp_free, 
                           (long)sizeof(t_oscil), 
                           0L, 
                           A_GIMME, 
                           0);
    
    class_addmethod(c, (method)oscil_dsp64,       "dsp64",       A_CANT, 0);
    class_addmethod(c, (method)oscil_assist,      "assist",      A_CANT, 0);
    class_addmethod(c, (method)oscil_sine,        "sine",        0);
    class_addmethod(c, (method)oscil_triangle,    "triangle",    0);
    class_addmethod(c, (method)oscil_square,      "square",      0);
    class_addmethod(c, (method)oscil_sawtooth,    "sawtooth",    0);
    class_addmethod(c, (method)oscil_buzz,        "buzz",        0);
    class_addmethod(c, (method)oscil_list,        "list",        A_GIMME, 0);
    class_addmethod(c, (method)oscil_amph,        "amph",        A_GIMME, 0);
    class_addmethod(c, (method)oscil_float,       "float",       A_FLOAT, 0);
    class_addmethod(c, (method)oscil_int,         "int",         A_LONG,  0);
    class_addmethod(c, (method)oscil_fadetype,    "fadetype",    A_FLOAT, 0);
    class_addmethod(c, (method)oscil_harmcount,   "harmcount",   A_FLOAT, 0);
    class_addmethod(c, (method)oscil_fadetime,    "fadetime",    A_FLOAT, 0);
    class_addmethod(c, (method)oscil_interpolate, "interpolate", A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    oscil_class = c;
    
    potpourri_announce(OBJECT_NAME);
    return 0;
}

void oscil_list(t_oscil *x, t_symbol *msg, long argc, t_atom *argv)
{
    int harmonic_count = 0;
    float *harmonic_weights = x->harmonic_weights;
    for (int i = 0; i < argc && harmonic_count < OSCIL_MAXIMUM_HARMONICS; i++) {
        harmonic_weights[harmonic_count] = (float)atom_getfloat(argv + i);
        ++harmonic_count;
    }
    x->harmonic_count = harmonic_count;
    build_waveform64(x);
}

void oscil_amph(t_oscil *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 1) return;
    int harmonic_count = 1;
    float *harmonic_weights = x->harmonic_weights;
    float *harmonic_phases = x->harmonic_phases;

    harmonic_weights[0] = (float)atom_getfloat(argv);
    harmonic_phases[0] = 0.0f;

    for (int i = 1; i < argc && harmonic_count < OSCIL_MAXIMUM_HARMONICS; i += 2) {
        harmonic_weights[harmonic_count] = (float)atom_getfloat(argv + i);
        harmonic_phases[harmonic_count] = (i + 1 < argc) ? (float)atom_getfloat(argv + i + 1) : 0.0f;
        ++harmonic_count;
    }
    x->harmonic_count = harmonic_count;
    build_amph_waveform64(x);
}

void oscil_fadetime(t_oscil *x, double fade_ms)
{
    if (!sys_getdspstate()) return;
    if (x->fade_countdown) {
        object_error((t_object *)x, "%s: crossfade in progress, cannot update fade time", OBJECT_NAME);
        return;
    }
    if (fade_ms < 0.0 || fade_ms > 600000.0) {
        fade_ms = 50.0;
    }
    x->fade_ms = (float)fade_ms;
    x->fade_samples = (int)(x->fade_ms * x->sr / 1000.0f);
}

void oscil_fadetype(t_oscil *x, double ftype)
{
    int t = (int)ftype;
    if (t < 0 || t > 2) t = 0;
    x->fadetype = (short)t;
}

void oscil_harmcount(t_oscil *x, double harms)
{
    int h = (int)harms;
    if (h < 1 || h > OSCIL_MAXIMUM_HARMONICS - 1) return;
    x->bl_harms = h + 1;
}

void oscil_mute(t_oscil *x, double flag) { x->mute = (short)(flag != 0.0); }
void oscil_interpolate(t_oscil *x, double flag) { x->interpolate = (short)(flag != 0.0); }

void oscil_assist(t_oscil *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal/float) Frequency", 256); break;
            case 1: strncpy_zero(dst, "(signal/float) Phase Offset [0.0 - 1.0]", 256); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Oscillator Output", 256);
    }
}

void oscil_int(t_oscil *x, long i) { oscil_float(x, (double)i); }

void oscil_float(t_oscil *x, double f)
{
    int inlet = x->x_obj.z_in;
    if (inlet == 0) {
        x->si = f * x->si_factor;
    } else if (inlet == 1) {
        x->phase_offset = (double)x->table_length * f;
        while (x->phase_offset >= (double)x->table_length) x->phase_offset -= (double)x->table_length;
        while (x->phase_offset < 0.0) x->phase_offset += (double)x->table_length;
    }
}

void *oscil_new(t_symbol *s, long argc, t_atom *argv)
{
    float init_freq = OSCIL_INIT_FREQ;
    t_symbol *init_waveform_symbol = gensym(OSCIL_DEFAULT_WAVEFORM);

    t_oscil *x = (t_oscil *)object_alloc(oscil_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 2);
    outlet_new((t_object *)x, "signal");
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->table_length = OSCIL_DEFAULT_FLEN;
    x->bl_harms = OSCIL_DEFAULT_HARMONICS;
    x->phase_offset = 0.0;
    x->interpolate = 0;

    if (argc > 0) init_freq = (float)atom_getfloat(argv);
    if (argc > 1) x->table_length = (int)atom_getfloat(argv + 1);
    if (argc > 2) atom_arg_getsym(&init_waveform_symbol, 2, (short)argc, argv);
    if (argc > 3) x->bl_harms = (int)atom_getfloat(argv + 3);

    if (x->table_length < 4) x->table_length = OSCIL_DEFAULT_FLEN;
    if (x->table_length > OSCIL_MAX_FLEN) x->table_length = OSCIL_MAX_FLEN;
    if (x->bl_harms < 1 || x->bl_harms > OSCIL_MAXIMUM_HARMONICS) x->bl_harms = OSCIL_DEFAULT_HARMONICS;

    x->fade_in_progress = 0;
    x->bl_harms += 1;
    x->piotwo = 2.0 * atan(1.0);
    x->twopi = 8.0 * atan(1.0);

    x->wavetable64       = (t_double *)sysmem_newptrclear(x->table_length * sizeof(t_double));
    x->old_wavetable64   = (t_double *)sysmem_newptrclear(x->table_length * sizeof(t_double));
    x->harmonic_weights  = (float *)sysmem_newptrclear(OSCIL_MAXIMUM_HARMONICS * sizeof(float));
    x->harmonic_phases   = (float *)sysmem_newptrclear(OSCIL_MAXIMUM_HARMONICS * sizeof(float));

    x->phase = 0.0;
    x->mute = 0;
    x->dirty = 0;
    x->sr = (float)sys_getsr();
    if (x->sr <= 0.0f) x->sr = 44100.0f;

    x->si_factor = (double)x->table_length / (double)x->sr;
    x->si = (double)init_freq * x->si_factor;
    x->fade_countdown = 0;
    x->fade_ms = 50.0f;
    x->fade_samples = (int)(x->fade_ms * x->sr / 1000.0f);
    x->fadetype = OSCIL_LINEAR;
    x->firsttime = 1;

    if (init_waveform_symbol == gensym("triangle")) oscil_triangle(x);
    else if (init_waveform_symbol == gensym("square")) oscil_square(x);
    else if (init_waveform_symbol == gensym("sawtooth")) oscil_sawtooth(x);
    else if (init_waveform_symbol == gensym("buzz")) oscil_buzz(x);
    else oscil_sine(x);

    x->firsttime = 0;
    return x;
}

void build_amph_waveform64(t_oscil *x)
{
    if (x->fade_in_progress) return;
    if (x->harmonic_count < 1) return;

    if (x->fadetype && !x->firsttime) {
        x->fade_countdown = x->fade_samples;
        x->fade_in_progress = 1;
    }

    memcpy(x->old_wavetable64, x->wavetable64, x->table_length * sizeof(t_double));
    x->dirty = 1;

    for (int i = 0; i < x->table_length; i++) {
        x->wavetable64[i] = (double)x->harmonic_weights[0];
    }

    for (int i = 1; i < x->harmonic_count; i++) {
        if (x->harmonic_weights[i] != 0.0f) {
            double addphase = x->twopi * (double)x->harmonic_phases[i];
            for (int j = 0; j < x->table_length; j++) {
                x->wavetable64[j] += (double)x->harmonic_weights[i] * sin(x->twopi * ((double)i * ((double)j / (double)x->table_length)) + addphase);
            }
        }
    }

    double max = 0.0;
    for (int j = 0; j < x->table_length; j++) {
        if (max < fabs(x->wavetable64[j])) max = fabs(x->wavetable64[j]);
    }

    if (max == 0.0) {
        memcpy(x->wavetable64, x->old_wavetable64, x->table_length * sizeof(t_double));
        x->dirty = 0;
        return;
    }

    double rescale = 1.0 / max;
    for (int j = 0; j < x->table_length; j++) {
        x->wavetable64[j] *= rescale;
    }
    x->dirty = 0;
}

void build_waveform64(t_oscil *x)
{
    if (x->fade_in_progress) return;
    if (x->harmonic_count < 1) return;

    if (x->fadetype && !x->firsttime) {
        x->fade_countdown = x->fade_samples;
        x->fade_in_progress = 1;
    }

    memcpy(x->old_wavetable64, x->wavetable64, x->table_length * sizeof(t_double));
    x->dirty = 1;

    for (int i = 0; i < x->table_length; i++) {
        x->wavetable64[i] = (double)x->harmonic_weights[0];
    }

    for (int i = 1; i < x->harmonic_count; i++) {
        if (x->harmonic_weights[i] != 0.0f) {
            for (int j = 0; j < x->table_length; j++) {
                x->wavetable64[j] += (double)x->harmonic_weights[i] * sin(x->twopi * ((double)i * ((double)j / (double)x->table_length)));
            }
        }
    }

    double max = 0.0;
    for (int j = 0; j < x->table_length; j++) {
        if (max < fabs(x->wavetable64[j])) max = fabs(x->wavetable64[j]);
    }

    if (max == 0.0) {
        memcpy(x->wavetable64, x->old_wavetable64, x->table_length * sizeof(t_double));
        x->dirty = 0;
        return;
    }

    double rescale = 1.0 / max;
    for (int j = 0; j < x->table_length; j++) {
        x->wavetable64[j] *= rescale;
    }
    x->dirty = 0;
}

void oscil_perform_interpolate64(t_oscil *x, t_object *dsp64, double **ins,
                                 long numins, double **outs, long numouts, long n,
                                 long flags, void *userparam)
{
    t_double *freq_vec  = ins[0];
    t_double *phase_vec = ins[1];
    t_double *out       = outs[0];

    if (x->mute) {
        memset(out, 0, n * sizeof(t_double));
        return;
    }

    t_double si_factor    = x->si_factor;
    t_double si           = x->si;
    t_double phase        = x->phase;
    t_double phase_offset = x->phase_offset;
    int table_length      = x->table_length;
    t_double *wavetable   = x->wavetable64;
    t_double *old_table   = x->old_wavetable64;
    int fade_countdown    = x->fade_countdown;
    int fade_samples      = x->fade_samples;
    short fadetype        = x->fadetype;
    t_double piotwo       = x->piotwo;
    short interpolate     = x->interpolate;
    short connected_freq  = x->connected[0];
    short connected_phase = x->connected[1];

    if (interpolate) {
        for (long i = 0; i < n; i++) {
            if (connected_freq)  si = freq_vec[i] * si_factor;
            if (connected_phase) {
                phase_offset = (double)table_length * phase_vec[i];
                while (phase_offset >= (double)table_length) phase_offset -= (double)table_length;
                while (phase_offset < 0.0) phase_offset += (double)table_length;
            }

            double fphase = phase + phase_offset;
            while (fphase >= (double)table_length) fphase -= (double)table_length;
            while (fphase < 0.0) fphase += (double)table_length;

            long iphase = (long)floor(fphase);
            long iphase2 = (iphase + 1) % table_length;
            double frac = fphase - (double)iphase;

            double sample;
            if (x->dirty) {
                sample = old_table[iphase] + frac * (old_table[iphase2] - old_table[iphase]);
            } else if (fade_countdown > 0) {
                double s1 = wavetable[iphase] + frac * (wavetable[iphase2] - wavetable[iphase]);
                double s2 = old_table[iphase] + frac * (old_table[iphase2] - old_table[iphase]);
                double m2 = (double)fade_countdown / (double)fade_samples;
                double m1 = 1.0 - m2;
                --fade_countdown;

                if (fadetype == OSCIL_LINEAR) {
                    sample = m1 * s1 + m2 * s2;
                } else if (fadetype == OSCIL_POWER) {
                    double m_rad = m1 * piotwo;
                    sample = sin(m_rad) * s1 + cos(m_rad) * s2;
                } else {
                    sample = s1;
                }
            } else {
                sample = wavetable[iphase] + frac * (wavetable[iphase2] - wavetable[iphase]);
            }

            out[i] = sample;
            phase += si;
            while (phase >= (double)table_length) phase -= (double)table_length;
            while (phase < 0.0) phase += (double)table_length;
        }
    } else {
        // Non-interpolated loop
        for (long i = 0; i < n; i++) {
            if (connected_freq)  si = freq_vec[i] * si_factor;
            if (connected_phase) {
                phase_offset = (double)table_length * phase_vec[i];
                while (phase_offset >= (double)table_length) phase_offset -= (double)table_length;
                while (phase_offset < 0.0) phase_offset += (double)table_length;
            }

            double fphase = phase + phase_offset;
            long iphase = (long)(fphase);
            while (iphase >= table_length) iphase -= table_length;
            while (iphase < 0) iphase += table_length;

            double sample;
            if (x->dirty) {
                sample = old_table[iphase];
            } else if (fade_countdown > 0) {
                double m2 = (double)fade_countdown / (double)fade_samples;
                double m1 = 1.0 - m2;
                --fade_countdown;

                if (fadetype == OSCIL_LINEAR) {
                    sample = m1 * wavetable[iphase] + m2 * old_table[iphase];
                } else if (fadetype == OSCIL_POWER) {
                    double m_rad = m1 * piotwo;
                    sample = sin(m_rad) * wavetable[iphase] + cos(m_rad) * old_table[iphase];
                } else {
                    sample = wavetable[iphase];
                }
            } else {
                sample = wavetable[iphase];
            }

            out[i] = sample;
            phase += si;
            while (phase >= (double)table_length) phase -= (double)table_length;
            while (phase < 0.0) phase += (double)table_length;
        }
    }

    if (fade_countdown <= 0) {
        x->fade_in_progress = 0;
    }
    x->fade_countdown = fade_countdown;
    x->phase = phase;
    x->phase_offset = phase_offset;
}

void oscil_sawtooth(t_oscil *x)
{
    float sign = 1.0f;
    x->harmonic_weights[0] = 0.0f;
    x->harmonic_count = x->bl_harms;
    for (int i = 1; i < x->bl_harms; i++) {
        x->harmonic_weights[i] = sign * 1.0f / (float)i;
        sign *= -1.0f;
    }
    build_waveform64(x);
}

void oscil_triangle(t_oscil *x)
{
    float sign = 1.0f;
    x->harmonic_weights[0] = 0.0f;
    x->harmonic_count = x->bl_harms;
    for (int i = 1; i < x->bl_harms; i += 2) {
        x->harmonic_weights[i] = sign * 1.0f / ((float)i * (float)i);
        if (i + 1 < x->bl_harms) x->harmonic_weights[i + 1] = 0.0f;
        sign *= -1.0f;
    }
    build_waveform64(x);
}

void oscil_sine(t_oscil *x)
{
    x->harmonic_weights[0] = 0.0f;
    x->harmonic_weights[1] = 1.0f;
    x->harmonic_count = 2;
    build_waveform64(x);
}

void oscil_square(t_oscil *x)
{
    x->harmonic_weights[0] = 0.0f;
    x->harmonic_count = x->bl_harms;
    for (int i = 1; i < x->bl_harms; i += 2) {
        x->harmonic_weights[i] = 1.0f / (float)i;
        if (i + 1 < x->bl_harms) x->harmonic_weights[i + 1] = 0.0f;
    }
    build_waveform64(x);
}

void oscil_buzz(t_oscil *x)
{
    x->harmonic_weights[0] = 0.0f;
    x->harmonic_count = x->bl_harms;
    for (int i = 1; i < x->bl_harms; i++) {
        x->harmonic_weights[i] = 1.0f;
    }
    build_waveform64(x);
}

void oscil_dsp_free(t_oscil *x)
{
    dsp_free((t_pxobject *)x);
    if (x->wavetable64)      { sysmem_freeptr(x->wavetable64); x->wavetable64 = NULL; }
    if (x->old_wavetable64)  { sysmem_freeptr(x->old_wavetable64); x->old_wavetable64 = NULL; }
    if (x->harmonic_weights) { sysmem_freeptr(x->harmonic_weights); x->harmonic_weights = NULL; }
    if (x->harmonic_phases)  { sysmem_freeptr(x->harmonic_phases); x->harmonic_phases = NULL; }
}

void oscil_dsp64(t_oscil *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;
    if (x->sr != (float)sr) {
        x->sr = (float)sr;
        x->si_factor = (double)x->table_length / (double)x->sr;
    }
    x->connected[0] = count[0];
    x->connected[1] = count[1];

    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)oscil_perform_interpolate64, 0, NULL);
}