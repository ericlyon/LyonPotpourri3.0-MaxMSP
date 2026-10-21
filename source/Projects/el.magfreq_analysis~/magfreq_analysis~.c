/**
 * @file magfreq_analysis~.c
 * @brief FFT magnitude and frequency analysis external for Max 8/9.
 * Modernized for 64-bit dsp64, zero-leak destructor cleanup, and vector overrun protection.
 */

#include "MSPd.h"
#include "fftease_oldskool.h"

static t_class *magfreq_analysis_class;

#define OBJECT_NAME "magfreq_analysis~"

typedef struct _magfreq_analysis {
    t_pxobject x_obj;
    int R;
    int N;
    int N2;
    int Nw;
    int Nw2; 
    int D; 
    int inCount;
    float *Wanal;
    float *Wsyn;
    float *input;
    float *Hwin;
    float *buffer;
    float *channel;
    float *output;
    float *c_lastphase_in;
    float *c_lastphase_out;
    float c_fundamental;
    float c_factor_in;
    float c_factor_out;
    int NP;
    float P;
    int L;
    int first;
    float Iinv;
    float *lastamp;
    float *lastfreq;
    float *index;
    float *table;
    float myPInc;
    float ffac;
    float lofreq;
    float hifreq;
    int lo_bin;
    int hi_bin;
    float topfreq;
    float synt;
    float mult; 
    float *trigland;
    int *bitshuffle;
    int bypass_state;
    int overlap;
    int winfac;
    short mute;
} t_magfreq_analysis;

/* Method Prototypes */
void *magfreq_analysis_new(t_symbol *s, long argc, t_atom *argv);
void magfreq_analysis_free(t_magfreq_analysis *x);
void magfreq_analysis_assist(t_magfreq_analysis *x, void *b, long m, long a, char *s);
void magfreq_analysis_bypass(t_magfreq_analysis *x, double state);
void magfreq_analysis_mute(t_magfreq_analysis *x, double tog);
void magfreq_analysis_init(t_magfreq_analysis *x, short initialized);
void magfreq_analysis_lowfreq(t_magfreq_analysis *x, double f);
void magfreq_analysis_highfreq(t_magfreq_analysis *x, double f);
void magfreq_analysis_overlap(t_magfreq_analysis *x, double o);
void magfreq_analysis_winfac(t_magfreq_analysis *x, double f);
void magfreq_analysis_fftinfo(t_magfreq_analysis *x);
void magfreq_analysis_dsp64(t_magfreq_analysis *x, t_object *dsp64, short *count, double sr, long n, long flags);
void magfreq_analysis_perform64(t_magfreq_analysis *x, t_object *dsp64, double **ins, 
                                long numins, double **outs, long numouts, long n,
                                long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.magfreq_analysis~", 
                           (method)magfreq_analysis_new, 
                           (method)magfreq_analysis_free, 
                           (long)sizeof(t_magfreq_analysis), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)magfreq_analysis_dsp64,   "dsp64",   A_CANT, 0);
    class_addmethod(c, (method)magfreq_analysis_assist,  "assist",  A_CANT, 0);
    class_addmethod(c, (method)magfreq_analysis_mute,    "mute",    A_FLOAT, 0);
    class_addmethod(c, (method)magfreq_analysis_bypass,  "bypass",  A_FLOAT, 0);
    class_addmethod(c, (method)magfreq_analysis_fftinfo, "fftinfo", 0);
    class_addmethod(c, (method)magfreq_analysis_overlap, "overlap", A_FLOAT, 0);
    class_addmethod(c, (method)magfreq_analysis_winfac,  "winfac",  A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    magfreq_analysis_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void magfreq_analysis_mute(t_magfreq_analysis *x, double tog)
{
    x->mute = (short)(tog != 0.0);
}

void magfreq_analysis_bypass(t_magfreq_analysis *x, double state)
{
    x->bypass_state = (int)(state != 0.0);
}

void magfreq_analysis_overlap(t_magfreq_analysis *x, double f)
{
    int i = (int)f;
    if (!power_of_two(i)) {
        object_error((t_object *)x, "%f is not a power of two", f);
        return;
    }
    x->overlap = i;
    magfreq_analysis_init(x, 1);
}

void magfreq_analysis_winfac(t_magfreq_analysis *x, double f)
{
    int i = (int)f;
    if (!power_of_two(i)) {
        object_error((t_object *)x, "%f is not a power of two", f);
        return;
    }
    x->winfac = i;
    magfreq_analysis_init(x, 2);
}

void magfreq_analysis_fftinfo(t_magfreq_analysis *x)
{
    if (x->overlap <= 0) return;
    post("%s: FFT size %d, hopsize %d, windowsize %d", OBJECT_NAME, x->N, x->N / x->overlap, x->Nw);
}

void magfreq_analysis_free(t_magfreq_analysis *x)
{
    dsp_free((t_pxobject *)x);

    if (x->Wanal)           sysmem_freeptr(x->Wanal);
    if (x->Wsyn)            sysmem_freeptr(x->Wsyn);
    if (x->Hwin)            sysmem_freeptr(x->Hwin);
    if (x->input)           sysmem_freeptr(x->input);
    if (x->output)          sysmem_freeptr(x->output);
    if (x->buffer)          sysmem_freeptr(x->buffer);
    if (x->channel)         sysmem_freeptr(x->channel);
    if (x->bitshuffle)      sysmem_freeptr(x->bitshuffle);
    if (x->trigland)        sysmem_freeptr(x->trigland);
    if (x->c_lastphase_in)  sysmem_freeptr(x->c_lastphase_in);
    if (x->c_lastphase_out) sysmem_freeptr(x->c_lastphase_out);
    if (x->lastamp)         sysmem_freeptr(x->lastamp);
    if (x->lastfreq)        sysmem_freeptr(x->lastfreq);
    if (x->index)           sysmem_freeptr(x->index);
    if (x->table)           sysmem_freeptr(x->table);
}

void magfreq_analysis_assist(t_magfreq_analysis *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(signal) Audio Input", 256);
    } else if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Magnitude Vector", 256); break;
            case 1: strncpy_zero(dst, "(signal) Frequency Vector", 256); break;
            case 2: strncpy_zero(dst, "(signal) Bin Index", 256); break;
        }
    }
}

void magfreq_analysis_highfreq(t_magfreq_analysis *x, double f)
{
    if (f < (double)x->lofreq) return;
    if (f > (double)x->R * 0.5) f = (double)x->R * 0.5;
    x->hifreq = (float)f;
    x->hi_bin = 1;
    float curfreq = 0.0f;
    while (curfreq < x->hifreq) {
        ++(x->hi_bin);
        curfreq += x->c_fundamental;
    }
}

void magfreq_analysis_lowfreq(t_magfreq_analysis *x, double f)
{
    if (f > (double)x->hifreq) return;
    if (f < 0.0) f = 0.0;
    x->lofreq = (float)f;
    x->lo_bin = 0;
    float curfreq = 0.0f;
    while (curfreq < x->lofreq) {
        ++(x->lo_bin);
        curfreq += x->c_fundamental;
    }
}

void magfreq_analysis_init(t_magfreq_analysis *x, short initialized)
{
    if (x->R <= 0) x->R = 44100;
    if (x->D <= 0) x->D = 256;
    if (x->P <= 0.0f) x->P = 1.0f;
    if (!power_of_two(x->overlap)) x->overlap = 2;
    if (!power_of_two(x->winfac))  x->winfac = 2;

    x->N = x->D * x->overlap;
    x->Nw = x->N * x->winfac;
    limit_fftsize(&x->N, &x->Nw, OBJECT_NAME);

    x->N2 = x->N / 2;
    x->Nw2 = x->Nw / 2;
    x->inCount = -(x->Nw);
    x->bypass_state = 0;
    x->mult = 1.0f / (float)x->N;

    x->L = 8192;
    x->c_fundamental = (float)x->R / (float)(x->N2 << 1);
    x->c_factor_in = (float)x->R / ((float)x->D * (float)TWOPI);
    x->c_factor_out = (float)TWOPI * (float)x->D / (float)x->R;
    x->Iinv = 1.0f / (float)x->D;
    x->myPInc = x->P * (float)x->L / (float)x->R;
    x->ffac = x->P * (float)PI / (float)x->N;

    if (!initialized) {
        x->Wanal           = (float *)sysmem_newptrclear(MAX_Nw * sizeof(float));
        x->Wsyn            = (float *)sysmem_newptrclear(MAX_Nw * sizeof(float));
        x->Hwin            = (float *)sysmem_newptrclear(MAX_Nw * sizeof(float));
        x->input           = (float *)sysmem_newptrclear(MAX_Nw * sizeof(float));
        x->output          = (float *)sysmem_newptrclear(MAX_Nw * sizeof(float));
        x->buffer          = (float *)sysmem_newptrclear(MAX_N * sizeof(float));
        x->channel         = (float *)sysmem_newptrclear((MAX_N + 2) * sizeof(float));
        x->bitshuffle      = (int *)sysmem_newptrclear(MAX_N * 2 * sizeof(int));
        x->trigland        = (float *)sysmem_newptrclear(MAX_N * 2 * sizeof(float));
        x->c_lastphase_in  = (float *)sysmem_newptrclear((MAX_N2 + 1) * sizeof(float));
        x->c_lastphase_out = (float *)sysmem_newptrclear((MAX_N2 + 1) * sizeof(float));
        x->lastamp         = (float *)sysmem_newptrclear((MAX_N + 1) * sizeof(float));
        x->lastfreq        = (float *)sysmem_newptrclear((MAX_N + 1) * sizeof(float));
        x->index           = (float *)sysmem_newptrclear((MAX_N + 1) * sizeof(float));
        x->table           = (float *)sysmem_newptrclear(x->L * sizeof(float));
        x->P = 1.0f;
        x->mute = 0;
    }

    memset((char *)x->input, 0, x->Nw * sizeof(float));
    memset((char *)x->output, 0, x->Nw * sizeof(float));
    memset((char *)x->c_lastphase_in, 0, (x->N2 + 1) * sizeof(float));
    memset((char *)x->c_lastphase_out, 0, (x->N2 + 1) * sizeof(float));
    memset((char *)x->lastamp, 0, (x->N + 1) * sizeof(float));
    memset((char *)x->lastfreq, 0, (x->N + 1) * sizeof(float));
    memset((char *)x->index, 0, (x->N + 1) * sizeof(float));

    for (int i = 0; i < x->L; i++) {
        x->table[i] = (float)x->N * cosf((float)i * (float)TWOPI / (float)x->L);
    }

    init_rdft(x->N, x->bitshuffle, x->trigland);
    makehanning(x->Hwin, x->Wanal, x->Wsyn, x->Nw, x->N, x->D, 0);

    if (x->hifreq < x->c_fundamental) x->hifreq = 3000.0f;

    x->hi_bin = 1;
    float curfreq = 0.0f;
    while (curfreq < x->hifreq) {
        ++(x->hi_bin);
        curfreq += x->c_fundamental;
    }

    x->lo_bin = 0;
    curfreq = 0.0f;
    while (curfreq < x->lofreq) {
        ++(x->lo_bin);
        curfreq += x->c_fundamental;
    }
}

void *magfreq_analysis_new(t_symbol *s, long argc, t_atom *argv)
{
    t_magfreq_analysis *x = (t_magfreq_analysis *)object_alloc(magfreq_analysis_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);
    outlet_new((t_object *)x, "signal"); // Magnitude Vector
    outlet_new((t_object *)x, "signal"); // Frequency Vector
    outlet_new((t_object *)x, "signal"); // Bin Index

    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->lofreq  = (argc > 0) ? (float)atom_getfloat(argv) : 0.0f;
    x->hifreq  = (argc > 1) ? (float)atom_getfloat(argv + 1) : 4000.0f;
    x->overlap = (argc > 2) ? (int)atom_getfloat(argv + 2) : 4;
    x->winfac  = (argc > 3) ? (int)atom_getfloat(argv + 3) : 2;

    if (x->lofreq < 0.0f || x->lofreq > 22050.0f) x->lofreq = 0.0f;
    if (x->hifreq < 50.0f || x->hifreq > 22050.0f) x->hifreq = 4000.0f;

    if (!power_of_two(x->overlap)) x->overlap = 4;
    if (!power_of_two(x->winfac))  x->winfac = 2;

    x->R = sys_getsr();
    x->D = sys_getblksize();

    magfreq_analysis_init(x, 0);
    return x;
}

void magfreq_analysis_perform64(t_magfreq_analysis *x, t_object *dsp64, double **ins, 
                                long numins, double **outs, long numouts, long n,
                                long flags, void *userparam)
{
    t_double *inbuf         = ins[0];
    t_double *magnitude_vec = outs[0];
    t_double *frequency_vec = outs[1];
    t_double *index_vec     = outs[2];

    if (x->mute || x->bypass_state || !inbuf) {
        for (long j = 0; j < n; j++) {
            magnitude_vec[j] = 0.0;
            frequency_vec[j] = 0.0;
            index_vec[j]     = (t_double)j;
        }
        return;
    }

    int D = x->D;
    int Nw = x->Nw;
    int N = x->N;
    int N2 = x->N2;
    float fundamental = x->c_fundamental;
    float factor_in = x->c_factor_in;
    int *bitshuffle = x->bitshuffle;
    float *trigland = x->trigland;
    float *lastphase_in = x->c_lastphase_in;
    float *Wanal = x->Wanal;
    float *input = x->input;
    float *buffer = x->buffer;
    float *channel = x->channel;

    int in = x->inCount;
    in += D;

    for (int j = 0; j < (Nw - D); j++) {
        input[j] = input[j + D];
    }
    for (int j = (Nw - D); j < Nw; j++) {
        input[j] = (float)*inbuf++;
    }

    fold(input, Wanal, Nw, buffer, N, in);
    rdft(N, 1, buffer, bitshuffle, trigland);
    convert(buffer, channel, N2, lastphase_in, fundamental, factor_in);

    // Vector bounds check: channel has (N2 + 1) bins
    for (long chan = 0; chan < n; chan++) {
        if (chan <= N2) {
            long amp = chan << 1;
            long freq = amp + 1;
            frequency_vec[chan] = (t_double)channel[freq];
            magnitude_vec[chan] = (t_double)channel[amp];
        } else {
            frequency_vec[chan] = 0.0;
            magnitude_vec[chan] = 0.0;
        }
        index_vec[chan] = (t_double)chan;
    }

    while (in < 0) in += Nw;
    x->inCount = in % Nw;
}

void magfreq_analysis_dsp64(t_magfreq_analysis *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;

    if (x->D != (int)n || x->R != (int)sr) {
        x->D = (int)n;
        x->R = (int)sr;
        magfreq_analysis_init(x, 1);
    }
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)magfreq_analysis_perform64, 0, NULL);
}