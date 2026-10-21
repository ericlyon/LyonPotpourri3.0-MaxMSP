/**
 * @file squash~.c
 * @brief Spectral compressor/expander audio processor for Max 8/9.
 * Modernized for 64-bit dsp64, dynamic vector reallocation, and division-by-zero NaN protection.
 */

#include "MSPd.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define OBJECT_NAME "el.squash~"

static t_class *squash_class = NULL;

typedef struct _squash
{
    t_pxobject x_obj;
    
    long    D;
    long    N;
    long    Nw;
    long    N2;
    long    incnt;
    double *Wanal;
    double *Wsyn;
    double *Hwin;
    double *buffer;
    double *input;
    double *output;
    double  thresh;
    double  ratio;
    double  nt;
    double  nmult;
    short   mute;
} t_squash;

// Prototypes
void *squash_new(t_symbol *msg, short argc, t_atom *argv);
void  squash_dsp_free(t_squash *x);
void  squash_assist(t_squash *x, void *b, long msg, long arg, char *dst);
void  squash_mute(t_squash *x, double f);
void  squash_thresh(t_squash *x, double f);
void  squash_nt(t_squash *x, double f);
void  squash_ratio(t_squash *x, double f);
void  squash_nmult(t_squash *x, double f);
void  squash_reinit_buffers(t_squash *x, long new_D);
double squash_squat(double *buffer, double thresh, double ratio, double nt, double nmult, long N);

void  fold(double *I, double *W, long Nw, double *O, long N, long n);
void  overlapadd(double *I, long N, double *W, double *O, long Nw, long n);
void  makehanning(double *H, double *A, double *S, long Nw, long N, long I, int odd);

void  squash_dsp64(t_squash *x, t_object *dsp64, short *count, double sr, long n, long flags);
void  squash_perform64(t_squash *x, t_object *dsp64, double **ins, 
                        long numins, double **outs, long numouts, long n,
                        long flags, void *userparam);

// -------------------------------------------------------------------------
// Initialization & Lifecycle
// -------------------------------------------------------------------------

void C74_EXPORT ext_main(void *r)
{
    t_class *c = class_new("el.squash~", 
                           (method)squash_new, 
                           (method)squash_dsp_free, 
                           sizeof(t_squash), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)squash_dsp64,  "dsp64",  A_CANT,  0);
    class_addmethod(c, (method)squash_assist, "assist", A_CANT,  0);
    class_addmethod(c, (method)squash_thresh, "thresh", A_FLOAT, 0);
    class_addmethod(c, (method)squash_nt,     "nt",     A_FLOAT, 0);
    class_addmethod(c, (method)squash_nmult,  "nmult",  A_FLOAT, 0);
    class_addmethod(c, (method)squash_ratio,  "ratio",  A_FLOAT, 0);
    class_addmethod(c, (method)squash_mute,   "mute",   A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    squash_class = c;

    potpourri_announce(OBJECT_NAME);
}

void squash_reinit_buffers(t_squash *x, long new_D)
{
    if (new_D <= 0) new_D = 64;

    // Free existing memory if allocated
    sysmem_freeptr(x->Wanal);
    sysmem_freeptr(x->Wsyn);
    sysmem_freeptr(x->Hwin);
    sysmem_freeptr(x->input);
    sysmem_freeptr(x->output);
    sysmem_freeptr(x->buffer);

    x->D     = new_D;
    x->N     = x->D * 4;
    x->Nw    = x->N;
    x->N2    = x->N / 2;
    x->incnt = -x->Nw;

    x->Wanal  = (double *)sysmem_newptrclear((size_t)x->Nw * sizeof(double));
    x->Wsyn   = (double *)sysmem_newptrclear((size_t)x->Nw * sizeof(double));
    x->Hwin   = (double *)sysmem_newptrclear((size_t)x->Nw * sizeof(double));
    x->input  = (double *)sysmem_newptrclear((size_t)x->Nw * sizeof(double));
    x->output = (double *)sysmem_newptrclear((size_t)x->Nw * sizeof(double));
    x->buffer = (double *)sysmem_newptrclear((size_t)x->N * sizeof(double));

    makehanning(x->Hwin, x->Wanal, x->Wsyn, x->Nw, x->N, x->D, 0);
}

void *squash_new(t_symbol *msg, short argc, t_atom *argv)
{
    t_squash *x = (t_squash *)object_alloc(squash_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);
    x->x_obj.z_misc |= Z_NO_INPLACE;
    outlet_new((t_pxobject *)x, "signal");

    x->Wanal  = NULL;
    x->Wsyn   = NULL;
    x->Hwin   = NULL;
    x->input  = NULL;
    x->output = NULL;
    x->buffer = NULL;

    long blksize = sys_getblksize();
    if (blksize <= 0) blksize = 64;
    squash_reinit_buffers(x, blksize);

    x->thresh = 0.1;
    x->ratio  = 1.0;
    x->nt     = 0.0000001;
    x->nmult  = 0.1;
    x->mute   = 0;

    return x;
}

void squash_dsp_free(t_squash *x)
{
    dsp_free((t_pxobject *)x);
    sysmem_freeptr(x->Wanal);
    sysmem_freeptr(x->Wsyn);
    sysmem_freeptr(x->Hwin);
    sysmem_freeptr(x->input);
    sysmem_freeptr(x->output);
    sysmem_freeptr(x->buffer);
}

// -------------------------------------------------------------------------
// Control Handlers
// -------------------------------------------------------------------------

void squash_mute(t_squash *x, double f)   { x->mute = (short)(f != 0.0); }
void squash_thresh(t_squash *x, double f) { x->thresh = (f > 1e-6) ? f : 1e-6; }
void squash_nt(t_squash *x, double f)     { x->nt = (f > 0.0) ? f : 1e-9; }
void squash_ratio(t_squash *x, double f)  { x->ratio = (f >= 0.0) ? f : 0.0; }
void squash_nmult(t_squash *x, double f)  { x->nmult = f; }

void squash_assist(t_squash *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        snprintf(dst, 256, "(signal) Audio Input");
    } else if (msg == ASSIST_OUTLET) {
        snprintf(dst, 256, "(signal) Compressed Audio Output");
    }
}

// -------------------------------------------------------------------------
// DSP & Real-Time Synthesis
// -------------------------------------------------------------------------

void squash_dsp64(t_squash *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if (sr <= 0.0) return;

    if (n != x->D) {
        squash_reinit_buffers(x, n);
    }

    object_method(dsp64, gensym("dsp_add64"), x, squash_perform64, 0, NULL);
}

void squash_perform64(t_squash *x, t_object *dsp64, double **ins, 
                      long numins, double **outs, long numouts, long n,
                      long flags, void *userparam)
{
    double *out = outs[0];

    if (x->mute) {
        memset(out, 0, (size_t)n * sizeof(double));
        return;
    }

    const double *in     = ins[0];
    double       *input  = x->input;
    double       *output = x->output;
    double       *buffer = x->buffer;
    const double *Wanal  = x->Wanal;
    const double *Wsyn   = x->Wsyn;
    long          D      = x->D;
    long          Nw     = x->Nw;
    long          N      = x->N;

    double thresh = x->thresh;
    double ratio  = x->ratio;
    double nt     = x->nt;
    double nmult  = x->nmult;

    x->incnt += D;

    for (long j = 0; j < Nw - D; j++) {
        input[j] = input[j + D];
    }

    for (long j = Nw - D; j < Nw; j++) {
        input[j] = *in++;
    }

    fold(input, (double *)Wanal, Nw, buffer, N, x->incnt);
    squash_squat(buffer, thresh, ratio, nt, nmult, Nw);
    overlapadd(buffer, N, (double *)Wsyn, output, Nw, x->incnt);

    for (long j = 0; j < D; j++) {
        *out++ = output[j];
    }

    for (long j = 0; j < Nw - D; j++) {
        output[j] = output[j + D];
    }

    for (long j = Nw - D; j < Nw; j++) {
        output[j] = 0.0;
    }

    x->incnt = x->incnt % Nw;
}

// -------------------------------------------------------------------------
// Compression Engine & Spectral Routines
// -------------------------------------------------------------------------

double squash_squat(double *buffer, double thresh, double ratio, double nt, double nmult, long N)
{
    double sum = 0.0;
    for (long i = 0; i < N; i++) {
        sum += buffer[i] * buffer[i];
    }

    double rms = sqrt(sum / (double)N);

    // Guard against log10(0) and division-by-zero on silent blocks
    if (rms < 1e-9) {
        memset(buffer, 0, (size_t)N * sizeof(double));
        return 0.0;
    }

    double mult;
    if (rms < nt && ratio < 1.0) {
        mult = nmult;
    } else {
        double dbthr = 10.0 * log10(thresh);
        double dbrms = 10.0 * log10(rms);
        mult = pow(10.0, (dbthr - ((dbthr - dbrms) * ratio)) / 10.0) / rms;
    }

    for (long i = 0; i < N; i++) {
        buffer[i] *= mult;
    }

    return rms;
}

void fold(double *I, double *W, long Nw, double *O, long N, long n)
{
    for (long i = 0; i < N; i++) O[i] = 0.0;
    while (n < 0) n += N;
    n %= N;
    for (long i = 0; i < Nw; i++) {
        O[n] += I[i] * W[i];
        if (++n == N) n = 0;
    }
}

void overlapadd(double *I, long N, double *W, double *O, long Nw, long n)
{
    while (n < 0) n += N;
    n %= N;
    for (long i = 0; i < Nw; i++) {
        O[i] += I[n] * W[i];
        if (++n == N) n = 0;
    }
}

void makehanning(double *H, double *A, double *S, long Nw, long N, long I, int odd)
{
    if (odd) {
        for (long i = 0; i < Nw; i++) {
            H[i] = A[i] = S[i] = sqrt(0.5 * (1.0 + cos(PI + TWOPI * (double)i / (double)(Nw - 1))));
        }
    } else {
        for (long i = 0; i < Nw; i++) {
            H[i] = A[i] = S[i] = 0.5 * (1.0 + cos(PI + TWOPI * (double)i / (double)(Nw - 1)));
        }
    }

    if (Nw > N) {
        double x = -(double)(Nw - 1) / 2.0;
        for (long i = 0; i < Nw; i++, x += 1.0) {
            if (x != 0.0) {
                A[i] *= (double)N * sin(PI * x / (double)N) / (PI * x);
                if (I) S[i] *= (double)I * sin(PI * x / (double)I) / (PI * x);
            }
        }
    }

    double sum = 0.0;
    for (long i = 0; i < Nw; i++) sum += A[i];

    for (long i = 0; i < Nw; i++) {
        double afac = 2.0 / sum;
        double sfac = (Nw > N) ? (1.0 / afac) : afac;
        A[i] *= afac;
        S[i] *= sfac;
    }

    if (Nw <= N && I) {
        sum = 0.0;
        for (long i = 0; i < Nw; i += I) sum += S[i] * S[i];
        sum = 1.0 / sum;
        for (long i = 0; i < Nw; i++) S[i] *= sum;
    }
}