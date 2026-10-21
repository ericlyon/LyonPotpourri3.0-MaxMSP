/**
 * @file kbuffer~.c
 * @brief Control-rate and audio-rate gesture capture buffer for Max 8/9.
 * Modernized for 64-bit dsp64, input signal connection tracking, and vector alignment.
 */

#include "MSPd.h"
#include <stdlib.h>
#include <string.h>

#define OBJECT_NAME "kbuffer~"

static t_class *kbuffer_class;

typedef struct _kbuffer {
    t_pxobject x_obj;
    float ksrate;
    float srate;
    double si;
    double phase;
    double duration;
    long iphase;
    long lastphase;
    long length;
    double *data;
    double fval;
    short record_flag;
    short play_flag;
    short dump_flag;
    short loop_flag;
    double sync;
    double speed;
    short in_connected;
    long memsize;
} t_kbuffer;

/* Method Prototypes */
void *kbuffer_new(t_symbol *s, long argc, t_atom *argv);
void kbuffer_free(t_kbuffer *x);
void kbuffer_assist(t_kbuffer *x, void *b, long m, long a, char *dst);
void kbuffer_record(t_kbuffer *x);
void kbuffer_play(t_kbuffer *x);
void kbuffer_loop(t_kbuffer *x);
void kbuffer_dump(t_kbuffer *x);
void kbuffer_stop(t_kbuffer *x);
void kbuffer_info(t_kbuffer *x);
void kbuffer_speed(t_kbuffer *x, double speed);
void kbuffer_size(t_kbuffer *x, double ms);
void kbuffer_ksrate(t_kbuffer *x, double ksrate);
void kbuffer_float(t_kbuffer *x, double f);
void kbuffer_int(t_kbuffer *x, long i);
void kbuffer_init(t_kbuffer *x, short initialized);
void kbuffer_dsp64(t_kbuffer *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void kbuffer_perform64(t_kbuffer *x, t_object *dsp64, double **ins, 
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.kbuffer~", 
                           (method)kbuffer_new, 
                           (method)kbuffer_free, 
                           (long)sizeof(t_kbuffer), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)kbuffer_dsp64,  "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)kbuffer_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)kbuffer_record, "record", 0);
    class_addmethod(c, (method)kbuffer_play,   "play",   0);
    class_addmethod(c, (method)kbuffer_loop,   "loop",   0);
    class_addmethod(c, (method)kbuffer_stop,   "stop",   0);
    class_addmethod(c, (method)kbuffer_dump,   "dump",   0);
    class_addmethod(c, (method)kbuffer_info,   "info",   0);
    class_addmethod(c, (method)kbuffer_speed,  "speed",  A_FLOAT, 0);
    class_addmethod(c, (method)kbuffer_size,   "size",   A_FLOAT, 0);
    class_addmethod(c, (method)kbuffer_ksrate, "ksrate", A_FLOAT, 0);
    class_addmethod(c, (method)kbuffer_float,  "float",  A_FLOAT, 0);
    class_addmethod(c, (method)kbuffer_int,    "int",    A_LONG,  0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    kbuffer_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void kbuffer_speed(t_kbuffer *x, double speed)
{
    x->speed = speed;
}

void kbuffer_size(t_kbuffer *x, double ms)
{
    if (ms < 1.0) ms = 1.0;
    x->duration = ms * 0.001;
    x->length = (long)(x->duration * (double)x->ksrate);
    if (x->length < 1) x->length = 1;
    x->memsize = x->length * sizeof(double);

    if (x->data) {
        x->data = (double *)sysmem_resizeptrclear(x->data, x->memsize);
    } else {
        x->data = (double *)sysmem_newptrclear(x->memsize);
    }
}

void kbuffer_ksrate(t_kbuffer *x, double ksrate)
{
    if (ksrate < 1.0) ksrate = 1.0;
    x->ksrate = (float)ksrate;
    x->length = (long)(x->duration * (double)x->ksrate);
    if (x->length < 1) x->length = 1;
    x->memsize = x->length * sizeof(double);

    if (x->srate > 0.0f) {
        x->si = (double)x->ksrate / (double)x->srate;
    }

    if (x->data) {
        x->data = (double *)sysmem_resizeptrclear(x->data, x->memsize);
    } else {
        x->data = (double *)sysmem_newptrclear(x->memsize);
    }
}

void kbuffer_info(t_kbuffer *x)
{
    post("%s: length: %ld samples | control rate: %.2f Hz | duration: %.2f s | memory: %ld bytes",
         OBJECT_NAME, x->length, x->ksrate, x->duration, x->memsize);
}

void kbuffer_record(t_kbuffer *x)
{
    x->record_flag = 1;
    x->play_flag = 0;
    x->dump_flag = 0;
    x->loop_flag = 0;
    x->sync = 0.0;
    x->phase = 0.0;
    x->iphase = 0;
    x->lastphase = -1;
}

void kbuffer_stop(t_kbuffer *x)
{
    x->record_flag = 0;
    x->play_flag = 0;
    x->dump_flag = 0;
    x->loop_flag = 0;
    x->sync = 0.0;
    x->phase = 0.0;
    x->iphase = 0;
    x->lastphase = -1;
}

void kbuffer_dump(t_kbuffer *x)
{
    x->record_flag = 0;
    x->play_flag = 0;
    x->loop_flag = 0;
    x->dump_flag = 1;
    x->sync = 0.0;
    x->phase = 0.0;
    x->iphase = 0;
    x->lastphase = -1;
}

void kbuffer_play(t_kbuffer *x)
{
    x->record_flag = 0;
    x->play_flag = 1;
    x->dump_flag = 0;
    x->loop_flag = 0;
    x->sync = 0.0;
    x->phase = 0.0;
    x->iphase = 0;
    x->lastphase = -1;
}

void kbuffer_loop(t_kbuffer *x)
{
    x->record_flag = 0;
    x->play_flag = 0;
    x->dump_flag = 0;
    x->loop_flag = 1;
    x->sync = 0.0;
    x->phase = 0.0;
    x->iphase = 0;
    x->lastphase = -1;
}

void kbuffer_free(t_kbuffer *x)
{
    dsp_free((t_pxobject *)x);
    if (x->data) {
        sysmem_freeptr(x->data);
        x->data = NULL;
    }
}

void kbuffer_init(t_kbuffer *x, short initialized)
{
    if (!initialized) {
        x->record_flag = 0;
        x->play_flag = 0;
        x->dump_flag = 0;
        x->loop_flag = 0;
        x->fval = 0.0;
        x->speed = 1.0;
        x->length = (long)(x->duration * (double)x->ksrate);
        if (x->length < 1) x->length = 1;
        x->memsize = x->length * sizeof(double);
        x->data = (double *)sysmem_newptrclear(x->memsize);
    }
    if (x->srate > 0.0f) {
        x->si = (double)x->ksrate / (double)x->srate;
    }
}

void *kbuffer_new(t_symbol *s, long argc, t_atom *argv)
{
    t_kbuffer *x = (t_kbuffer *)object_alloc(kbuffer_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);
    outlet_new((t_object *)x, "signal"); // Signal Out
    outlet_new((t_object *)x, "signal"); // Sync Out

    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->srate = (float)sys_getsr();
    if (x->srate <= 0.0f) x->srate = 44100.0f;

    x->ksrate = (argc > 0) ? (float)atom_getfloat(argv) : 128.0f;
    x->duration = (argc > 1) ? ((double)atom_getfloat(argv + 1) * 0.001) : 10.0;

    if (x->ksrate <= 0.0f) x->ksrate = 128.0f;
    if (x->duration <= 0.0) x->duration = 10.0;

    x->data = NULL;
    kbuffer_init(x, 0);
    return x;
}

void kbuffer_perform64(t_kbuffer *x, t_object *dsp64, double **ins, 
                       long numins, double **outs, long numouts, long n,
                       long flags, void *userparam)
{
    t_double *in = ins[0];
    t_double *out = outs[0];
    t_double *sync_out = outs[1];

    short record_flag = x->record_flag;
    short play_flag = x->play_flag;
    short dump_flag = x->dump_flag;
    short loop_flag = x->loop_flag;
    long length = x->length;
    long iphase = x->iphase;
    long lastphase = x->lastphase;
    double phase = x->phase;
    double *data = x->data;
    double si = x->si;
    double speed = x->speed;
    short in_connected = x->in_connected;
    double fval = x->fval;

    double inv_len = (length > 0) ? (1.0 / (double)length) : 1.0;

    while (n--) {
        double sample = in_connected ? *in++ : fval;

        if (record_flag) {
            iphase = (long)phase;
            phase += si;
            if (iphase >= length) {
                record_flag = 0;
                *sync_out++ = 1.0;
                *out++ = sample;
            } else {
                if (iphase > lastphase && iphase >= 0) {
                    lastphase = iphase;
                    data[iphase] = sample;
                }
                *sync_out++ = phase * inv_len;
                *out++ = sample;
            }
        } else if (play_flag) {
            iphase = (long)phase;
            phase += (si * speed);
            if (iphase >= length) {
                play_flag = 0;
                *out++ = data[length - 1];
                *sync_out++ = 1.0;
            } else if (iphase < 0) {
                play_flag = 0;
                *out++ = data[0];
                *sync_out++ = 0.0;
            } else {
                *out++ = data[iphase];
                *sync_out++ = phase * inv_len;
            }
        } else if (loop_flag) {
            iphase = (long)phase;
            phase += (si * speed);
            if (iphase >= length) {
                phase = 0.0;
                iphase = 0;
            } else if (iphase < 0) {
                phase = (double)(length - 1);
                iphase = length - 1;
            }
            *out++ = data[iphase];
            *sync_out++ = phase * inv_len;
        } else if (dump_flag) {
            iphase = (long)phase;
            phase += 1.0;
            if (iphase >= length) {
                dump_flag = 0;
                *out++ = 0.0;
                *sync_out++ = 1.0;
            } else {
                *out++ = data[iphase];
                *sync_out++ = phase * inv_len;
            }
        } else {
            *sync_out++ = 0.0;
            *out++ = 0.0;
        }
    }

    x->phase = phase;
    x->iphase = iphase;
    x->lastphase = lastphase;
    x->record_flag = record_flag;
    x->play_flag = play_flag;
    x->dump_flag = dump_flag;
    x->loop_flag = loop_flag;
}

void kbuffer_dsp64(t_kbuffer *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;
    x->in_connected = count[0];

    if (x->srate != (float)samplerate) {
        x->srate = (float)samplerate;
        kbuffer_init(x, 1);
    }
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)kbuffer_perform64, 0, NULL);
}

void kbuffer_assist(t_kbuffer *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(signal/float) Input Data to Record", 256);
    } else if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Recorded Output", 256); break;
            case 1: strncpy_zero(dst, "(signal) Sync Phasor [0.0 - 1.0]", 256); break;
        }
    }
}

void kbuffer_float(t_kbuffer *x, double f)
{
    int inlet = ((t_pxobject *)x)->z_in;
    if (inlet == 0) {
        x->fval = f;
    }
}

void kbuffer_int(t_kbuffer *x, long i)
{
    int inlet = ((t_pxobject *)x)->z_in;
    if (inlet == 0) {
        x->fval = (double)i;
    }
}