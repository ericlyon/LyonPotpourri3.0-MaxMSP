#ifndef CHAMELEON_H
#define CHAMELEON_H

#include "MSPd.h"
#include "ugens.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BENDY 0
#define RINGMOD 1
#define FLANGE 2
#define BUTTER 3
#define TRUNCATE 4
#define SWEEPRESON 5
#define COMB 6
#define SLIDECOMB 7
#define REVERB1 8
#define ELLIPSE 9
#define COMPDIST 10
#define FEED1 11
#define BITCRUSH 12
#define FLAM1 13
#define SLIDEFLAM 14
#define RINGMOD4 15
#define COMB4 16
#define RINGFEED 17
#define RESONADSR 18
#define STV 19	

#define ROOT2 (1.4142135623730950488)
#define PI2 (6.2831853071795862319959)
#define LOPASS 0
#define HIPASS 1
#define BANDPASS 2
#define COMBFADE (0.04)
#define MAXFILTER 12
#define ELLIPSE_FILTER_COUNT 11
#define MAX_COEF 48
#define MY_MAX 2147483647.0
#define MAX_DSP_UNITS 12
#define MAX_SLIDECOMB_DELAY 0.05
#define MAX_SLIDEFLAM_DELAY 0.5
#define MAX_MINI_DELAY 0.25
#define FEEDFUNCLEN 8192
#define TONECOMB_MAX_DELAY 0.02
#define BENDY_MINSEG 0.025
#define BENDY_MAXSEG 0.2
#define BENDY_MAXDEL 0.25
#define FLAM1_MAX_DELAY 2.0
#define STV_MAX_DELAY 0.011

#define FLUSH_DENORM(v) if (fabs(v) < 1.0e-15) (v) = 0.0

typedef struct _flange_unit {
    double *flange_dl1;
    double *flange_dl2;
    int *dv1;
    int *dv2;
    double phase;
} t_flange_unit;

typedef struct _butterworth_unit {
    int ftype;
    double cutoff;
    double cf;
    double bw;
    double *data1;
    double *data2;
} t_butterworth_unit;

typedef struct _truncate_unit {
    long counter;
    long state;
    long segsamples;
} t_truncate_unit;

typedef struct _sweepreson_unit {
    double minfreq;
    double maxfreq;
    double bwfac;
    double speed;
    double phase;
    double *q1;
    double *q2;
} t_sweepreson_unit;

typedef struct _slidecomb_unit {
    double *delayline1;
    double *delayline2;
    double start_delay;
    double end_delay;
    double feedback;
    long sample_length;
    long counter;
    int *dv1;
    int *dv2;
} t_slidecomb_unit;

typedef struct _reverb1_unit {
    double *dels;
    double **alpo1;
    double **alpo2;
    double xnorm;
    LSTRUCT *eel1, *eel2;
    double wet;
    double dry;
    double revtime;
    int nsects;
} t_reverb1_unit;

typedef struct _ellipseme_unit {
    LSTRUCT *eel1, *eel2;
    double xnorm;
    int nsects;
    int filtercode;
} t_ellipseme_unit;

typedef struct _feed1_unit {
    double *func1;
    double *func2;
    double *func3;
    double *func4;
    double mindelay;
    double maxdelay;
    double speed1;
    double speed2;
    double *delayLine1a;
    double *delayLine2a;
    double *delayLine1b;
    double *delayLine2b;
    int *dv1a, *dv2a;
    int *dv1b, *dv2b;
    double funcPhs;
    double duration;
} t_feed1_unit;

typedef struct _flam1_unit {
    double *delayline1;
    double *delayline2;
    double dt;
    long sample_length;
    long counter;
    int *dv1;
    int *dv2;
} t_flam1_unit;

typedef struct _slideflam_unit {
    double *delayline1;
    double *delayline2;
    double dt1, dt2;
    double feedback;
    long sample_length;
    long counter;
    int *dv1;
    int *dv2;
} t_slideflam_unit;

typedef struct _comb4_unit {
    double **combs1;
    double **combs2;
    double revtime;
} t_comb4_unit;

typedef struct _resonfeed_unit {
    double *res1q;
    double *res2q;
    double osc1phs;
    double osc2phs;
    double osc1si;
    double osc2si;
    double *comb1arr;
    double *comb2arr;
} t_resonfeed_unit;

typedef struct _resonadsr_unit {
    double *q1;
    double *q2;
    double phs;
    double si;
    double bwfac;
    CMIXADSR *adsr;
} t_resonadsr_unit;

typedef struct _stv_unit {
    double osc1phs;
    double osc2phs;
    double osc1si;
    double osc2si;
    double *delayline1;
    double *delayline2;
    int *dv1;
    int *dv2;
    double osc1amp;
    double osc2amp;
    double fac1;
    double fac2;
} t_stv_unit;

typedef struct _bendy_unit {
    double val1;
    double val2;
    long counter;
    long segment_samples;
    double min_delay;
    double max_delay;
    double min_speed;
    double max_speed;
    double *delayline1;
    double *delayline2;
    int *dv1;
    int *dv2;
} t_bendy_unit;

typedef struct _slot {
    long pcount;
    double *params;
} t_slot;

typedef struct _chameleon {
    t_pxobject x_obj;
    float sr;
    long vs;
    long vecsize;
    long pcount;
    double *params;
    float fadeout;
    t_double *chan1buf;
    t_double *chan2buf;
    float *odds;
    int max_process_per_note;
    int min_process_per_note;
    int new_slot;
    float new_gain;
    short verbose;
    void *listo;
    t_atom *data;
    char sound_name[256];
    double *distortion_function;
    int distortion_length;
    int set_parameters_flag;
    int recall_parameters_flag;
    t_slot *slots;
    long recall_slot;
    double *sinewave;
    int sinelen;
    short mute;
    float maxdelay;
    double **comb_delay_pool1;
    double **comb_delay_pool2;
    double *ringmod_phases;
    double *ringmod4_phases;
    
    t_flange_unit *flange_units;
    t_butterworth_unit *butterworth_units;
    t_truncate_unit *truncate_units;
    t_sweepreson_unit *sweepreson_units;
    t_slidecomb_unit *slidecomb_units;
    t_reverb1_unit *reverb1_units;
    t_ellipseme_unit *ellipseme_units;
    t_feed1_unit *feed1_units;
    t_flam1_unit *flam1_units;
    t_comb4_unit *comb4_units;
    t_resonfeed_unit *resonfeed_units;
    t_resonadsr_unit *resonadsr_units;
    t_stv_unit *stv_units;
    t_bendy_unit *bendy_units;
    t_slideflam_unit *slideflam_units;
    
    int *flange_dv1;
    int *flange_dv2;
    double flange_phase;
    double max_flangedelay;
    
    LSTRUCT *eel;
    float *mini_delay[4];
    float max_mini_delay;
    float *transfer_function;
    int tf_len;
    float *feedfunc1;
    float *feedfunc2;
    float *feedfunc3;
    float *feedfunc4;
    int feedfunclen;
    int flamfunc1len;
    float *flamfunc1;
    CMIXADSR *adsr;
    float max_comb_lpt;
    double *reverb_ellipse_data;
    double **ellipse_data;
    double *dcflt;
    CMIXOSC oscar;
    CMIXRESON resies[2];
    double *ratios;
    double *bitcrush_factors;
} t_chameleon;

/* Function prototypes */
void putsine(double *arr, long len);
float boundrand(float min, float max);
void mycombset(double loopt, double rvt, int init, double *a, double srate);
double mycomb(double samp, double *a);
void setweights(float *a, int len);
void delset2(double *a, int *l, double xmax, double srate);
void delset3(double *a, int *l, double xmax, double srate, double alloc_max);
void delput2(double x, double *a, int *l);
float dliget2(double *a, double dwait, int *l, double srate);
void lobut(double *a, double cutoff, double SR);
void hibut(double *a, double cutoff, double SR);
void bpbut(double *a, double formant, double bandwidth, double SR);
void butter_filter(double *buf, double *a, long frames);
void rsnset2(double cf, double bw, double scl, double xinit, double *a, double srate);
double reson(double x, double *a);
void resonadsr(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void ellipset(double *list, LSTRUCT *eel, int *nsects, double *xnorm);
double ellipse(double x, LSTRUCT *eel, int nsects, double xnorm);
double allpass(double samp, double *a);
void init_reverb_data(double *a);
void init_ellipse_data(double **a);
void setflamfunc1(float *arr, int flen);
void setExpFlamFunc(float *arr, int flen, float v1, float v2, float alpha);
void funcgen1(double *outArray, int outlen, double duration, double outMin, double outMax,
              double speed1, double speed2, double gain1, double gain2, double *phs1, double *phs2,
              double *sine, int sinelen);
void normtab(double *inarr, double *outarr, double min, double max, int len);
double mapp(double insamp, double imin, double imax, double omin, double omax);
double oscil(double amp, double si, double *farray, int len, double *phs);
void set_dcflt(double *a);
void set_distortion_table(double *arr, double cut, double max, int len);
double dlookup(double samp, double *arr, int len);
void do_compdist(double *in, double *out, int sampFrames, int nchans, int channel,
                 double cutoff, double maxmult, int lookupflag, double *table, int range, double bufMaxamp);
double getmaxamp(double *arr, int len);
void buildadsr(CMIXADSR *a);

#endif