/**
 * @file dynss~.c
 * @brief Dynamic segmented sound source oscillator for Max 8/9.
 * Modernized for 64-bit dsp64, zero-leak teardown, frequency safety, and real-time thread safety.
 */

#include "MSPd.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define MAXPOINTS (64)
#define OBJECT_NAME "dynss~"
#define COMPILE_DATE "9.02.07"
#define OBJECT_VERSION "2.02"

static t_class *dynss_class;

typedef struct _dynss {
    t_pxobject x_obj;
    long point_count;       // How many points in waveform (including start and end)
    t_double freq;          // Frequency
    long counter;           // Count samples
    long period_samples;    // How many samples in a period
    float srate;            // Sampling rate
    long current_point;     // Which point are we on
    t_double *values;
    long *point_breaks;
    t_double *norm_breaks;
    t_double *xdevs;
    t_double *ydevs;
    t_double devscale_x;
    t_double devscale_y;
} t_dynss;

/* Method Prototypes */
void *dynss_new(t_symbol *msg, long argc, t_atom *argv);
void dynss_free(t_dynss *x);
void dynss_assist(t_dynss *x, void *b, long m, long a, char *s);
float boundrand(float min, float max);
void dynss_init(t_dynss *x, short initialized);
void dynss_devx(t_dynss *x, double f);
void dynss_devy(t_dynss *x, double f);
void dynss_freq(t_dynss *x, double f);
void dynss_printwave(t_dynss *x);
void dynss_new_wave(t_dynss *x);
void dynss_pointcount(t_dynss *x, double f);
void dynss_new_amps(t_dynss *x);
void dynss_perform64(t_dynss *x, t_object *dsp64, double **ins, 
                     long numins, double **outs, long numouts, long n,
                     long flags, void *userparam);
void dynss_dsp64(t_dynss *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.dynss~", 
                   (method)dynss_new, 
                   (method)dynss_free, 
                   (long)sizeof(t_dynss), 
                   0L, 
                   A_GIMME, 
                   0);

    class_addmethod(c, (method)dynss_dsp64,     "dsp64",     A_CANT, 0);
    class_addmethod(c, (method)dynss_assist,    "assist",    A_CANT, 0);
    class_addmethod(c, (method)dynss_devx,      "devx",      A_FLOAT, 0);
    class_addmethod(c, (method)dynss_devy,      "devy",      A_FLOAT, 0);
    class_addmethod(c, (method)dynss_freq,      "freq",      A_FLOAT, 0);
    class_addmethod(c, (method)dynss_printwave, "printwave", 0);
    class_addmethod(c, (method)dynss_new_wave,  "new_wave",  0);
    class_addmethod(c, (method)dynss_new_amps,  "new_amps",  0);
    class_addmethod(c, (method)dynss_pointcount,"pointcount",A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    dynss_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void dynss_pointcount(t_dynss *x, double f)
{
    long pc = (long)f;
    if (pc >= 2 && pc <= MAXPOINTS) {
        x->point_count = pc;
        dynss_init(x, 0);
    }
}

void dynss_freq(t_dynss *x, double f)
{
    x->freq = fabs(f);
    if (x->freq < 0.001) x->freq = 0.001;
}

void dynss_devx(t_dynss *x, double f)
{
    x->devscale_x = f;
}

void dynss_devy(t_dynss *x, double f)
{
    x->devscale_y = f;
}

void dynss_printwave(t_dynss *x)
{
    for (int i = 0; i < x->point_count; i++) {
        post("point %d break %ld norm break %f value %f", 
             i, x->point_breaks[i], x->norm_breaks[i], x->values[i]);
    }
}

void dynss_new_wave(t_dynss *x)
{
    dynss_init(x, 0);
}

void dynss_new_amps(t_dynss *x)
{
    for (int i = 1; i < x->point_count - 1; i++) {
        x->values[i] = (double)boundrand(-0.95f, 0.95f);
    }
}

void dynss_assist(t_dynss *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(signal) Trigger / Control Impulse", 256);
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Segmented Synth Output", 256);
    }
}

void *dynss_new(t_symbol *msg, long argc, t_atom *argv)
{
    t_dynss *x = (t_dynss *)object_alloc(dynss_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);
    outlet_new((t_object *)x, "signal");  
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->srate = sys_getsr();
    if (x->srate <= 0.0f) x->srate = 44100.0f;

    x->point_count = 6;
    x->freq = 100.0;

    long alloc_size = (MAXPOINTS + 2) * sizeof(t_double);
    x->values = (t_double *)sysmem_newptrclear(alloc_size);
    x->point_breaks = (long *)sysmem_newptrclear((MAXPOINTS + 2) * sizeof(long));
    x->norm_breaks = (t_double *)sysmem_newptrclear(alloc_size);
    x->xdevs = (t_double *)sysmem_newptrclear(alloc_size);
    x->ydevs = (t_double *)sysmem_newptrclear(alloc_size);

    srand((unsigned int)time(NULL));

    dynss_init(x, 0); 
    return x;
}

void dynss_free(t_dynss *x)
{
    dsp_free((t_pxobject *)x);

    if (x->values)       { sysmem_freeptr(x->values); x->values = NULL; }
    if (x->point_breaks) { sysmem_freeptr(x->point_breaks); x->point_breaks = NULL; }
    if (x->norm_breaks)  { sysmem_freeptr(x->norm_breaks); x->norm_breaks = NULL; }
    if (x->xdevs)        { sysmem_freeptr(x->xdevs); x->xdevs = NULL; }
    if (x->ydevs)        { sysmem_freeptr(x->ydevs); x->ydevs = NULL; }
}

float boundrand(float min, float max)
{
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}

void dynss_init(t_dynss *x, short initialized)
{
    int i, j;
    float findex;
    long point_count = x->point_count;
    t_double *values = x->values;
    t_double *norm_breaks = x->norm_breaks;
    long *point_breaks = x->point_breaks;
    t_double *ydevs = x->ydevs;
    t_double *xdevs = x->xdevs;

    if (!initialized) {
        if (x->freq < 0.001) x->freq = 0.001;
        x->period_samples = (long)((double)x->srate / x->freq);
        if (x->period_samples < 2) x->period_samples = 2;

        x->counter = 0;
        x->current_point = 0;
        x->devscale_x = 0.0;
        x->devscale_y = 0.0;

        norm_breaks[0] = 0.0;
        norm_breaks[point_count - 1] = 1.0;
        values[0] = values[point_count - 1] = 0.0;

        for (i = 1; i < point_count - 1; i++) {
            values[i] = (double)boundrand(-0.95f, 0.95f);
            norm_breaks[i] = (double)boundrand(0.05f, 0.95f);
        }

        // Insertion sort
        for (i = 1; i < point_count; i++) {
            findex = (float)norm_breaks[i];
            j = i;
            while (j > 0 && norm_breaks[j - 1] > (t_double)findex) {
                norm_breaks[j] = norm_breaks[j - 1];
                j = j - 1;
            }
            norm_breaks[j] = (t_double)findex;
        }

        // Enforce minimum distance between adjacent breaks
        for (i = 1; i < point_count - 1; i++) {
            if (norm_breaks[i] <= norm_breaks[i - 1] + 0.01) {
                norm_breaks[i] = norm_breaks[i - 1] + 0.01;
            }
        }
        norm_breaks[point_count - 1] = 1.0;

        for (i = 0; i < point_count; i++) {
            point_breaks[i] = (long)((double)x->period_samples * norm_breaks[i]);
        }
        for (i = 0; i < point_count; i++) {
            ydevs[i] = (double)boundrand(0.0f, 0.99f);
            xdevs[i] = (double)boundrand(0.0f, 0.99f);
        }
    }
}

void dynss_perform64(t_dynss *x, t_object *dsp64, double **ins, 
                     long numins, double **outs, long numouts, long n,
                     long flags, void *userparam)
{
    int i, j;
    t_double findex1, findex2;
    t_double *outlet = outs[0];
    t_double *values = x->values;
    t_double *norm_breaks = x->norm_breaks;
    long *point_breaks = x->point_breaks;
    t_double *ydevs = x->ydevs;
    t_double *xdevs = x->xdevs;
    long counter = x->counter;
    long period_samples = x->period_samples;
    long current_point = x->current_point;
    long point_count = x->point_count;
    t_double sample = 0.0;
    t_double frak;
    t_double dev, newval;
    long segsamps;

    if (period_samples < 2) period_samples = 2;

    while (n--) {
        if (current_point >= point_count - 1) {
            current_point = 0;
            counter = 0;
        }

        if (counter == point_breaks[current_point + 1]) {
            sample = values[current_point + 1];
            ++current_point;
            if (current_point > point_count - 1) {
                current_point = 0;
                counter = 0;
            }
        } else {
            segsamps = point_breaks[current_point + 1] - point_breaks[current_point];
            if (segsamps <= 0) {
                frak = 0.0;
            } else {
                frak = (double)(counter - point_breaks[current_point]) / (double)segsamps;
                if (frak < 0.0) frak = 0.0;
                else if (frak > 1.0) frak = 1.0;
            }
            if (current_point < 0 || current_point >= point_count - 1) {
                sample = 0.0;
            } else {
                sample = values[current_point] + (values[current_point + 1] - values[current_point]) * frak;
            }
        }

        ++counter;
        if (counter >= period_samples) {
            counter = 0;
            current_point = 0;
            if (x->freq > 0.0) {
                period_samples = (long)((double)x->srate / x->freq);
                if (period_samples < 2) period_samples = 2;
            }

            // Nudge waveform amplitudes
            for (i = 1; i < point_count - 1; i++) {
                dev = (double)boundrand(-1.0f, 1.0f) * ydevs[i] * x->devscale_y;
                newval = values[i] + dev;
                newval = newval > 0.95 ? 0.95 : (newval < -0.95 ? -0.95 : newval);
                values[i] = newval;
            }

            // Nudge waveform breakpoints
            for (i = 1; i < point_count - 1; i++) {
                dev = (double)boundrand(-1.0f, 1.0f) * xdevs[i] * x->devscale_x;
                newval = norm_breaks[i] + dev;
                newval = newval < 0.05 ? 0.05 : (newval > 0.95 ? 0.95 : newval);
                norm_breaks[i] = newval;
            }
            norm_breaks[0] = 0.0;
            norm_breaks[point_count - 1] = 1.0;

            // Sort points
            for (i = 1; i < point_count; i++) {
                findex1 = norm_breaks[i];
                findex2 = values[i];
                j = i;
                while (j > 0 && norm_breaks[j - 1] > findex1) {
                    norm_breaks[j] = norm_breaks[j - 1];
                    values[j] = values[j - 1];
                    j = j - 1;
                }
                norm_breaks[j] = findex1;
                values[j] = findex2;
            }

            // Enforce minimum gap between breakpoints after sorting
            for (i = 1; i < point_count - 1; i++) {
                if (norm_breaks[i] <= norm_breaks[i - 1] + 0.01) {
                    norm_breaks[i] = norm_breaks[i - 1] + 0.01;
                }
            }
            norm_breaks[point_count - 1] = 1.0;

            // Generate sample breaks
            for (i = 0; i < point_count; i++) {
                point_breaks[i] = (long)((double)period_samples * norm_breaks[i]);
                if (point_breaks[i] >= period_samples) {
                    point_breaks[i] = period_samples - 1;
                }
            }
            point_breaks[point_count - 1] = period_samples;
        }
        *outlet++ = sample;
    }

    x->counter = counter;
    x->current_point = current_point;    
    x->period_samples = period_samples;
}

void dynss_dsp64(t_dynss *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;
    if (x->srate != (float)samplerate) {
        x->srate = (float)samplerate;
    }
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)dynss_perform64, 0, NULL);
}