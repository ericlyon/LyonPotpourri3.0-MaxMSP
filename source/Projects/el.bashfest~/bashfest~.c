/**
 * @file bashfest~.c
 * @brief Algorithmic DSP processing external for Max 8/9.
 * Modernized for 64-bit dsp64, thread safety, and memory stability.
 */

#include "bashfest.h"
#include <string.h>

#define DEFAULT_MAX_OVERLAP (8)
#define ACTIVE 0
#define INACTIVE 1
#define WAITING 2
#define MAX_VEC 4096
#define DEFAULT_BUFFER_SIZE 4000.0
#define DEFAULT_LATENCY 8192
#define MAX_PARAMETERS 2048
#define PROCESS_COUNT 20
#define CYCLE_MAX 1024
#define BUF_PAD (8192)
#define OBJECT_NAME "bashfest~"

static t_class *bashfest_class;

/* External DSP routine prototypes from bashfest_dsp.c */
void transpose(t_bashfest *x, int slot, int *pcount);
void ringmod(t_bashfest *x, int slot, int *pcount);
void retrograde(t_bashfest *x, int slot, int *pcount);
void comber(t_bashfest *x, int slot, int *pcount);
void flange(t_bashfest *x, int slot, int *pcount);
void butterme(t_bashfest *x, int slot, int *pcount);
void truncateme(t_bashfest *x, int slot, int *pcount);
void sweepreson(t_bashfest *x, int slot, int *pcount);
void slidecomb(t_bashfest *x, int slot, int *pcount);
void reverb1(t_bashfest *x, int slot, int *pcount);
void ellipseme(t_bashfest *x, int slot, int *pcount);
void feed1me(t_bashfest *x, int slot, int *pcount);
void flam1(t_bashfest *x, int slot, int *pcount);
void flam2(t_bashfest *x, int slot, int *pcount);
void expflam(t_bashfest *x, int slot, int *pcount);
void comb4(t_bashfest *x, int slot, int *pcount);
void compdist(t_bashfest *x, int slot, int *pcount);
void ringfeed(t_bashfest *x, int slot, int *pcount);
void resonadsr(t_bashfest *x, int slot, int *pcount);
void stv(t_bashfest *x, int slot, int *pcount);

/* Internal Method Prototypes */
void *bashfest_new(t_symbol *msg, short argc, t_atom *argv);
void bashfest_assist(t_bashfest *x, void *b, long msg, long arg, char *dst);
void bashfest_dsp_free(t_bashfest *x);
int bashfest_set_parameters(t_bashfest *x, float *params, float transpose_factor);
void bashfest_deploy_dsp(t_bashfest *x);
void bashfest_copy_to_MSP_buffer(t_bashfest *x);
void bashfest_stop(t_bashfest *x);
void bashfest_info(t_bashfest *x);
void bashfest_dblclick(t_bashfest *x);
void bashfest_mute(t_bashfest *x, double t);
void bashfest_maximum_process(t_bashfest *x, double n);
void bashfest_minimum_process(t_bashfest *x, double n);
void bashfest_setbuf(t_bashfest *x, t_symbol *wavename);
void attach_buffer(t_bashfest *x);
void bashfest_flatodds(t_bashfest *x);
void bashfest_killproc(t_bashfest *x, double p);
void bashfest_soloproc(t_bashfest *x, double p);
void bashfest_latency(t_bashfest *x, double n);
void bashfest_verbose(t_bashfest *x, double t);
void bashfest_gozero(t_bashfest *x);
void bashfest_grab(t_bashfest *x);
void bashfest_printmem(t_bashfest *x);
void bashfest_setodds(t_bashfest *x, t_symbol *msg, short argc, t_atom *argv);
void bashfest_tcycle(t_bashfest *x, t_symbol *msg, short argc, t_atom *argv);
void bashfest_dsp64(t_bashfest *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
t_max_err bashfest_notify(t_bashfest *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
t_max_err bashfest_latency_get(t_bashfest *x, void *attr, long *ac, t_atom **av);
t_max_err bashfest_latency_set(t_bashfest *x, void *attr, long ac, t_atom *av);
void bashfest_perform64(t_bashfest *x, t_object *dsp64, double **ins, 
                        long numins, double **outs, long numouts, long vectorsize,
                        long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.bashfest~", 
                           (method)bashfest_new, 
                           (method)bashfest_dsp_free, 
                           (long)sizeof(t_bashfest), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)bashfest_assist,          "assist",          A_CANT, 0);
    class_addmethod(c, (method)bashfest_dblclick,        "dblclick",        A_CANT, 0);
    class_addmethod(c, (method)bashfest_setbuf,          "setbuf",          A_SYM, 0);
    class_addmethod(c, (method)bashfest_stop,            "stop",            0);
    class_addmethod(c, (method)bashfest_flatodds,        "flatodds",        0);
    class_addmethod(c, (method)bashfest_soloproc,        "soloproc",        A_FLOAT, 0);
    class_addmethod(c, (method)bashfest_killproc,        "killproc",        A_FLOAT, 0);
    class_addmethod(c, (method)bashfest_latency,         "latency",         A_FLOAT, 0);
    class_addmethod(c, (method)bashfest_mute,            "mute",            A_FLOAT, 0);
    class_addmethod(c, (method)bashfest_verbose,         "verbose",         A_FLOAT, 0);
    class_addmethod(c, (method)bashfest_setodds,         "setodds",         A_GIMME, 0);
    class_addmethod(c, (method)bashfest_tcycle,          "tcycle",          A_GIMME, 0);
    class_addmethod(c, (method)bashfest_gozero,          "gozero",          0);
    class_addmethod(c, (method)bashfest_grab,            "grab",            0);
    class_addmethod(c, (method)bashfest_maximum_process, "maximum_process", A_FLOAT, 0);
    class_addmethod(c, (method)bashfest_minimum_process, "minimum_process", A_FLOAT, 0);
    class_addmethod(c, (method)bashfest_dsp64,           "dsp64",           A_CANT, 0);
    class_addmethod(c, (method)bashfest_notify,          "notify",          A_CANT, 0);
    class_addmethod(c, (method)bashfest_printmem,        "printmem",        0);

    CLASS_ATTR_LONG(c, "latency", 0, t_bashfest, latency_samples);
    CLASS_ATTR_DEFAULT_SAVE(c, "latency", 0, "8192");
    CLASS_ATTR_ACCESSORS(c, "latency", (method)bashfest_latency_get, (method)bashfest_latency_set);
    CLASS_ATTR_LABEL(c, "latency", 0, "Latency (Samples)");

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    bashfest_class = c;

    potpourri_announce(OBJECT_NAME);
    // post("new version\n");
    return 0;
}

t_max_err bashfest_notify(t_bashfest *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (x->buffer_ref) {
        return buffer_ref_notify(x->buffer_ref, s, msg, sender, data);
    }
    return MAX_ERR_NONE;
}

void bashfest_maximum_process(t_bashfest *x, double n)
{
    if (n < 0.0) return;
    x->max_process_per_note = (int)n;
}

void bashfest_minimum_process(t_bashfest *x, double n)
{
    if (n < 0.0) return;
    x->min_process_per_note = (int)n;
}

void bashfest_verbose(t_bashfest *x, double t) { x->verbose = (short)t; }
void bashfest_mute(t_bashfest *x, double t)    { x->mute = (short)t; }
void bashfest_grab(t_bashfest *x)              { x->grab = 1; }
void bashfest_gozero(t_bashfest *x)            { x->tcycle.p = 0; }

void bashfest_printmem(t_bashfest *x)
{
    post("Memory (MB) for this el.bashfest~ unit: %.2f", x->memcnt);
}

t_max_err bashfest_latency_get(t_bashfest *x, void *attr, long *ac, t_atom **av)
{
    if (ac && av) {
        char alloc;
        if (atom_alloc(ac, av, &alloc)) return MAX_ERR_GENERIC;
        atom_setlong(*av, (t_atom_long)x->latency_samples);
    }
    return MAX_ERR_NONE;
}

t_max_err bashfest_latency_set(t_bashfest *x, void *attr, long ac, t_atom *av)
{
    if (ac && av) {
        bashfest_latency(x, (double)atom_getlong(av));
    }
    return MAX_ERR_NONE;
}

void bashfest_latency(t_bashfest *x, double fp)
{
    int n = (int)fp;
    if (n < x->vs) {
        object_error((t_object *)x, "%s: latency %d cannot be less than vector size (%d)", OBJECT_NAME, n, x->vs);
        return;
    }
    x->latency_samples = n;
}

void bashfest_stop(t_bashfest *x)
{
    for (int i = 0; i < x->overlap_max; i++) {
        x->events[i].status = INACTIVE;
    }
}

void bashfest_tcycle(t_bashfest *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc < 1) {
        object_error((t_object *)x, "%s: no data provided for tcycle", OBJECT_NAME);
        return;
    } else if (argc > CYCLE_MAX) {
        object_error((t_object *)x, "%s: %d is the maximum tcycle size", OBJECT_NAME, CYCLE_MAX);
        return;
    }

    x->tcycle.len = (int)argc;
    x->tcycle.p = 0;
    for (int i = 0; i < argc; i++) {
        float data = 1.0f;
        atom_arg_getfloat(&data, i, argc, argv);
        if (data <= 0.0f) {
            object_error((t_object *)x, "%s: invalid tcycle data (%f)", OBJECT_NAME, data);
            x->tcycle.data[i] = 1.0f;
        } else {
            x->tcycle.data[i] = data;
        }
    }
}

void bashfest_setodds(t_bashfest *x, t_symbol *msg, short argc, t_atom *argv)
{
    if (argc > PROCESS_COUNT) {
        object_error((t_object *)x, "%s: maximum %d processes supported", OBJECT_NAME, PROCESS_COUNT);
        return;
    }
    for (int i = 0; i < PROCESS_COUNT; i++) {
        x->odds[i] = 0.0f;
    }
    for (int i = 0; i < argc; i++) {
        x->odds[i] = atom_getfloatarg(i, argc, argv);
    }
    setweights(x->odds, PROCESS_COUNT);
}

void bashfest_soloproc(t_bashfest *x, double fp)
{
    int p = (int)fp;
    if (p < 0 || p >= PROCESS_COUNT) {
        object_error((t_object *)x, "%s: invalid process index %d", OBJECT_NAME, p);
        return;
    }
    for (int i = 0; i < PROCESS_COUNT; i++) {
        x->odds[i] = 0.0f;
    }
    x->odds[p] = 1.0f;
    setweights(x->odds, PROCESS_COUNT);
}

void bashfest_killproc(t_bashfest *x, double fp)
{
    int p = (int)fp;
    if (p < 0 || p >= PROCESS_COUNT) {
        object_error((t_object *)x, "%s: invalid process index %d", OBJECT_NAME, p);
        return;
    }
    for (int i = 0; i < PROCESS_COUNT; i++) {
        x->odds[i] = 1.0f;
    }
    x->odds[p] = 0.0f;
    setweights(x->odds, PROCESS_COUNT);
}

void bashfest_flatodds(t_bashfest *x)
{
    for (int i = 0; i < PROCESS_COUNT; i++) {
        x->odds[i] = 1.0f;
    }
    setweights(x->odds, PROCESS_COUNT);
}

void *bashfest_new(t_symbol *msg, short argc, t_atom *argv)
{
    t_bashfest *x = (t_bashfest *)object_alloc(bashfest_class);
    if (!x) return NULL;

    long membytes = 0;
    float tmpfloat;

    x->sr = sys_getsr();
    x->vs = sys_getblksize();
    if (x->sr <= 0.0f) x->sr = 48000.0f;
    if (x->vs <= 0)    x->vs = 64;

    x->work_buffer_size = DEFAULT_BUFFER_SIZE;
    if (argc < 1) {
        object_error((t_object *)x, "%s: must specify a buffer name", OBJECT_NAME);
        x->hosed = 1;
        return NULL;
    }

    atom_arg_getsym(&x->wavename, 0, argc, argv);
    atom_arg_getfloat(&x->work_buffer_size, 1, argc, argv);
    tmpfloat = DEFAULT_LATENCY;
    atom_arg_getfloat(&tmpfloat, 2, argc, argv);
    x->latency_samples = (int)tmpfloat;
    tmpfloat = DEFAULT_MAX_OVERLAP;
    atom_arg_getfloat(&tmpfloat, 3, argc, argv);
    x->overlap_max = (int)tmpfloat;

    if (x->overlap_max < 1) x->overlap_max = 1;
    if (x->latency_samples < x->vs) x->latency_samples = x->vs;

    dsp_setup((t_pxobject *)x, 2); // 2 signal inputs
    outlet_new((t_object *)x, "signal"); // Output L
    outlet_new((t_object *)x, "signal"); // Output R

    // CRITICAL: Prevent Max from sharing ins[0] and outs[0] memory!
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->sinelen = 8192;
    x->verbose = 0;
    x->most_recent_event = 0;
    x->active_events = 0;
    x->increment = 1.0f;
    x->grab = 0;
    x->grab_slot = 0;
    x->grab_start = 0;
    x->hosed = 0;
    x->mute = 0;
    x->already_failed = 0;
    x->buffer_ref = NULL;

    x->buf_frames = (int)(x->work_buffer_size * 0.001f * x->sr);
    x->buf_samps = x->buf_frames * 4;
    x->halfbuffer = x->buf_samps / 2;
    x->maxdelay = 1.0f;

    // Allocate resources
    x->events = (t_event *)sysmem_newptrclear(x->overlap_max * sizeof(t_event));
    x->sinewave = (float *)sysmem_newptrclear((x->sinelen + BUF_PAD) * sizeof(float));
    x->params = (float *)sysmem_newptrclear(MAX_PARAMETERS * sizeof(float));
    x->odds = (float *)sysmem_newptrclear(64 * sizeof(float));

    putsine(x->sinewave, x->sinelen);

    for (int i = 0; i < x->overlap_max; i++) {
        x->events[i].workbuffer = (float *)sysmem_newptrclear((x->buf_samps + BUF_PAD) * sizeof(float));
        x->events[i].delayline1 = (float *)sysmem_newptrclear(((int)(x->maxdelay * x->sr) + 2 + BUF_PAD) * sizeof(float));
        x->events[i].delayline2 = (float *)sysmem_newptrclear(((int)(x->maxdelay * x->sr) + 2 + BUF_PAD) * sizeof(float));
        x->events[i].eel = (LSTRUCT *)sysmem_newptrclear(MAXSECTS * sizeof(LSTRUCT));
        x->events[i].mini_delay = (float **)sysmem_newptrclear(4 * sizeof(float *));
        x->max_mini_delay = 0.25f;

        for (int j = 0; j < 4; j++) {
            x->events[i].mini_delay[j] = (float *)sysmem_newptrclear(((int)(x->sr * x->max_mini_delay) + BUF_PAD) * sizeof(float));
        }

        x->tf_len = 65536;
        x->events[i].transfer_function = (float *)sysmem_newptrclear(x->tf_len * sizeof(float));
        x->feedfunclen = 8192;
        x->events[i].feedfunc1 = (float *)sysmem_newptrclear((x->feedfunclen + BUF_PAD) * sizeof(float));
        x->events[i].feedfunc2 = (float *)sysmem_newptrclear((x->feedfunclen + BUF_PAD) * sizeof(float));
        x->events[i].feedfunc3 = (float *)sysmem_newptrclear((x->feedfunclen + BUF_PAD) * sizeof(float));
        x->events[i].feedfunc4 = (float *)sysmem_newptrclear((x->feedfunclen + BUF_PAD) * sizeof(float));

        x->max_comb_lpt = 0.15f;
        x->events[i].combies = (CMIXCOMB **)sysmem_newptrclear(4 * sizeof(CMIXCOMB *));
        for (int j = 0; j < 4; j++) {
            x->events[i].combies[j] = (CMIXCOMB *)sysmem_newptrclear(sizeof(CMIXCOMB));
            x->events[i].combies[j]->len = (int)(x->sr * x->max_comb_lpt) + 2;
            x->events[i].combies[j]->arr = (float *)sysmem_newptrclear(x->events[i].combies[j]->len * sizeof(float));
        }

        x->events[i].resies = (CMIXRESON **)sysmem_newptrclear(4 * sizeof(CMIXRESON *));
        for (int j = 0; j < 4; j++) {
            x->events[i].resies[j] = (CMIXRESON *)sysmem_newptrclear(sizeof(CMIXRESON));
        }

        x->events[i].adsr = (CMIXADSR *)sysmem_newptrclear(sizeof(CMIXADSR));
        x->events[i].adsr->len = 32768;
        x->events[i].adsr->func = (float *)sysmem_newptrclear(x->events[i].adsr->len * sizeof(float));
        x->events[i].status = INACTIVE;
    }

    x->reverb_ellipse_data = (float *)sysmem_newptrclear(16 * sizeof(float));
    x->ellipse_data = (float **)sysmem_newptrclear(MAXFILTER * sizeof(float *));
    for (int i = 0; i < MAXFILTER; i++) {
        x->ellipse_data[i] = (float *)sysmem_newptrclear(MAX_COEF * sizeof(float));
    }

    x->flamfunc1len = 8192;
    x->flamfunc1 = (float *)sysmem_newptrclear(x->flamfunc1len * sizeof(float));
    setflamfunc1(x->flamfunc1, x->flamfunc1len);

    x->dcflt = (float *)sysmem_newptrclear(16 * sizeof(float));
    x->tcycle.data = (float *)sysmem_newptrclear(CYCLE_MAX * sizeof(float));
    x->tcycle.len = 0;

    x->qelem = qelem_new(x, (method)bashfest_deploy_dsp);
    x->qelem_grab = qelem_new(x, (method)bashfest_copy_to_MSP_buffer);

    set_dcflt(x->dcflt);
    init_reverb_data(x->reverb_ellipse_data);
    init_ellipse_data(x->ellipse_data);

    for (int i = 0; i < PROCESS_COUNT; i++) {
        x->odds[i] = 1.0f;
    }
    x->min_process_per_note = 0;
    x->max_process_per_note = 2;
    setweights(x->odds, PROCESS_COUNT);

    attach_buffer(x);
    return x;
}

void bashfest_dsp_free(t_bashfest *x)
{
    if (!x) return;
    x->hosed = 1;

    dsp_free((t_pxobject *)x);

    if (x->qelem) {
        qelem_free(x->qelem);
        x->qelem = NULL;
    }
    if (x->qelem_grab) {
        qelem_free(x->qelem_grab);
        x->qelem_grab = NULL;
    }

    if (x->events) {
        for (int i = 0; i < x->overlap_max; i++) {
            if (x->events[i].workbuffer)        sysmem_freeptr(x->events[i].workbuffer);
            if (x->events[i].eel)               sysmem_freeptr(x->events[i].eel);
            if (x->events[i].delayline1)        sysmem_freeptr(x->events[i].delayline1);
            if (x->events[i].delayline2)        sysmem_freeptr(x->events[i].delayline2);
            if (x->events[i].transfer_function) sysmem_freeptr(x->events[i].transfer_function);
            if (x->events[i].feedfunc1)         sysmem_freeptr(x->events[i].feedfunc1);
            if (x->events[i].feedfunc2)         sysmem_freeptr(x->events[i].feedfunc2);
            if (x->events[i].feedfunc3)         sysmem_freeptr(x->events[i].feedfunc3);
            if (x->events[i].feedfunc4)         sysmem_freeptr(x->events[i].feedfunc4);

            if (x->events[i].mini_delay) {
                for (int j = 0; j < 4; j++) {
                    if (x->events[i].mini_delay[j]) sysmem_freeptr(x->events[i].mini_delay[j]);
                }
                sysmem_freeptr(x->events[i].mini_delay);
            }

            if (x->events[i].combies) {
                for (int j = 0; j < 4; j++) {
                    if (x->events[i].combies[j]) {
                        if (x->events[i].combies[j]->arr) sysmem_freeptr(x->events[i].combies[j]->arr);
                        sysmem_freeptr(x->events[i].combies[j]);
                    }
                }
                sysmem_freeptr(x->events[i].combies);
            }

            if (x->events[i].resies) {
                for (int j = 0; j < 4; j++) {
                    if (x->events[i].resies[j]) sysmem_freeptr(x->events[i].resies[j]);
                }
                sysmem_freeptr(x->events[i].resies);
            }

            if (x->events[i].adsr) {
                if (x->events[i].adsr->func) sysmem_freeptr(x->events[i].adsr->func);
                sysmem_freeptr(x->events[i].adsr);
            }
        }
        sysmem_freeptr(x->events);
        x->events = NULL;
    }

    if (x->sinewave)            sysmem_freeptr(x->sinewave);
    if (x->params)              sysmem_freeptr(x->params);
    if (x->odds)                sysmem_freeptr(x->odds);
    if (x->flamfunc1)           sysmem_freeptr(x->flamfunc1);
    if (x->reverb_ellipse_data) sysmem_freeptr(x->reverb_ellipse_data);
    if (x->dcflt)               sysmem_freeptr(x->dcflt);
    if (x->tcycle.data)         sysmem_freeptr(x->tcycle.data);

    if (x->ellipse_data) {
        for (int i = 0; i < MAXFILTER; i++) {
            if (x->ellipse_data[i]) sysmem_freeptr(x->ellipse_data[i]);
        }
        sysmem_freeptr(x->ellipse_data);
    }

    if (x->buffer_ref) {
        object_free(x->buffer_ref);
        x->buffer_ref = NULL;
    }
}

void bashfest_dblclick(t_bashfest *x)
{
    attach_buffer(x);
    if (x->buffer_ref) {
        buffer_view(buffer_ref_getobject(x->buffer_ref));
    }
}

void bashfest_setbuf(t_bashfest *x, t_symbol *wavename)
{
    x->wavename = wavename;
    attach_buffer(x);
    bashfest_stop(x);
}

void attach_buffer(t_bashfest *x)
{
    if (!x->buffer_ref) {
        x->buffer_ref = buffer_ref_new((t_object *)x, x->wavename);
    } else {
        buffer_ref_set(x->buffer_ref, x->wavename);
    }
    strncpy_zero(x->sound_name, x->wavename ? x->wavename->s_name : "none", 256);
}

void bashfest_perform64(t_bashfest *x, t_object *dsp64, double **ins, 
                        long numins, double **outs, long numouts, long n,
                        long flags, void *userparam)
{
    t_double *trigger_vec   = ins[0];
    t_double *transpose_vec = ins[1];
    t_double *outchanL      = outs[0];
    t_double *outchanR      = outs[1];

    t_event *events = x->events;
    long overlap_max = x->overlap_max;

    // STEP 1: Scan trigger inputs FIRST before touching output buffers
    if (!x->mute && !x->hosed) {
        for (long i = 0; i < n; i++) {
            if (trigger_vec[i] != 0.0) {
                double gain = trigger_vec[i];
                double transpose_factor = transpose_vec[i];

                short insert_success = 0;
                long new_insert = 0;

                for (long slot_i = 0; slot_i < overlap_max; slot_i++) {
                    if (events[slot_i].status == INACTIVE) {
                        events[slot_i].status = WAITING;
                        events[slot_i].gain = (float)gain;
                        events[slot_i].transpose = (float)transpose_factor;
                        insert_success = 1;
                        new_insert = slot_i;
                        break;
                    }
                }

                // Steal oldest active voice if saturated
                if (!insert_success) {
                    long maxphase = 0;
                    long theft_candidate = 0;
                    for (long slot_i = 0; slot_i < overlap_max; slot_i++) {
                        if (events[slot_i].phase > maxphase) {
                            maxphase = events[slot_i].phase;
                            theft_candidate = slot_i;
                        }
                    }
                    new_insert = theft_candidate;
                    events[new_insert].status = WAITING;
                    events[new_insert].gain = (float)gain;
                    events[new_insert].transpose = (float)transpose_factor;
                }

                events[new_insert].countdown = x->latency_samples;
                x->new_slot = (int)new_insert;
                x->new_gain = (float)gain;

                qelem_set(x->qelem);
            }
        }
    }

    // STEP 2: Clear output buffers
    for (long i = 0; i < n; i++) {
        outchanL[i] = 0.0;
        outchanR[i] = 0.0;
    }

    if (x->mute || x->hosed) return;

    // STEP 3: Accumulate active notes into output channels
    for (long slot_i = 0; slot_i < overlap_max; slot_i++) {
        if (events[slot_i].status == ACTIVE) {
            int out_channels = events[slot_i].out_channels;
            float *processed_drum = events[slot_i].workbuffer + events[slot_i].in_start;

            for (long j = 0; j < n; j++) {
                if (events[slot_i].countdown > 0) {
                    --events[slot_i].countdown;
                } else {
                    if (out_channels == 1) {
                        float s = processed_drum[events[slot_i].phase];
                        outchanL[j] += s * events[slot_i].gainL;
                        outchanR[j] += s * events[slot_i].gainR;
                    } else if (out_channels == 2) {
                        long iphase = events[slot_i].phase * 2;
                        outchanL[j] += processed_drum[iphase]     * events[slot_i].gainL;
                        outchanR[j] += processed_drum[iphase + 1] * events[slot_i].gainR;
                    }

                    events[slot_i].phase++;
                    if (events[slot_i].phase >= events[slot_i].sample_frames) {
                        events[slot_i].status = INACTIVE;
                        break;
                    }
                }
            }
        }
    }
}

void bashfest_copy_to_MSP_buffer(t_bashfest *x)
{
    int grab_slot = x->grab_slot;
    int grab_start = x->grab_start;

    if (grab_slot < 0 || grab_slot >= x->overlap_max) return;

    attach_buffer(x);
    t_buffer_obj *the_buffer = buffer_ref_getobject(x->buffer_ref);
    if (!the_buffer) return;

    float *b_samples = buffer_locksamples(the_buffer);
    if (!b_samples) return;

    long b_frames = buffer_getframecount(the_buffer);
    long b_nchans = buffer_getchannelcount(the_buffer);

    float *processed_drum = x->events[grab_slot].workbuffer + grab_start;

    if (x->events[grab_slot].out_channels == b_nchans) {
        if (b_nchans == 1) {
            for (long i = 0; i < b_frames; i++) {
                b_samples[i] = processed_drum[i];
            }
        } else if (b_nchans == 2) {
            for (long i = 0; i < b_frames * 2; i += 2) {
                b_samples[i]     = processed_drum[i];
                b_samples[i + 1] = processed_drum[i + 1];
            }
        }
        object_method((t_object *)the_buffer, gensym("dirty"));
    }

    buffer_unlocksamples(the_buffer);
}

void bashfest_deploy_dsp(t_bashfest *x)
{
    if (x->hosed) return;

    attach_buffer(x);
    t_buffer_obj *the_buffer = buffer_ref_getobject(x->buffer_ref);
    if (!the_buffer) return;

    float *b_samples = buffer_locksamples(the_buffer);
    if (!b_samples) return;

    long b_frames = buffer_getframecount(the_buffer);
    long b_nchans = buffer_getchannelcount(the_buffer);

    if (b_nchans < 1 || b_nchans > 2 || b_frames <= 0) {
        buffer_unlocksamples(the_buffer);
        return;
    }

    t_event *events = x->events;
    float *params = x->params;

    for (int slot = 0; slot < x->overlap_max; slot++) {
        if (events[slot].status == WAITING) {
            float gain = events[slot].gain;
            float transpose_factor = events[slot].transpose;

            float pan = boundrand(0.1f, 0.9f);
            events[slot].gainL = cosf((float)PIOVERTWO * pan) * gain;
            events[slot].gainR = sinf((float)PIOVERTWO * pan) * gain;

            events[slot].phase = 0;
            events[slot].out_channels = (int)b_nchans;

            int copy_count = (int)(b_frames * b_nchans);
            if (copy_count > x->halfbuffer) {
                copy_count = x->halfbuffer;
            }
            events[slot].sample_frames = copy_count / (int)b_nchans;

            for (int i = 0; i < copy_count; i++) {
                events[slot].workbuffer[i] = b_samples[i];
            }
            for (int i = copy_count; i < x->buf_samps; i++) {
                events[slot].workbuffer[i] = 0.0f;
            }

            events[slot].in_start = 0;
            events[slot].out_start = x->halfbuffer;

            int pcount = bashfest_set_parameters(x, params, transpose_factor);
            int curarg = 0;

            while (curarg < pcount) {
                int type = (int)params[curarg];

                if      (type == TRANSPOSE)  transpose(x, slot, &curarg);
                else if (type == RINGMOD)    ringmod(x, slot, &curarg);
                else if (type == RETRO)      retrograde(x, slot, &curarg);
                else if (type == COMB)       comber(x, slot, &curarg);
                else if (type == FLANGE)     flange(x, slot, &curarg);
                else if (type == BUTTER)     butterme(x, slot, &curarg);
                else if (type == TRUNCATE)   truncateme(x, slot, &curarg);
                else if (type == SWEEPRESON) sweepreson(x, slot, &curarg);
                else if (type == SLIDECOMB)  slidecomb(x, slot, &curarg);
                else if (type == REVERB1)    reverb1(x, slot, &curarg);
                else if (type == ELLIPSE)    ellipseme(x, slot, &curarg);
                else if (type == FEED1)      feed1me(x, slot, &curarg);
                else if (type == FLAM1)      flam1(x, slot, &curarg);
                else if (type == FLAM2)      flam2(x, slot, &curarg);
                else if (type == EXPFLAM)    expflam(x, slot, &curarg);
                else if (type == COMB4)      comb4(x, slot, &curarg);
                else if (type == COMPDIST)   compdist(x, slot, &curarg);
                else if (type == RINGFEED)   ringfeed(x, slot, &curarg);
                else if (type == RESONADSR)  resonadsr(x, slot, &curarg);
                else if (type == STV)        stv(x, slot, &curarg);
                else {
                    curarg++;
                }
            }

            // Normalization
            float maxamp = 0.0f;
            float *processed_ptr = events[slot].workbuffer + events[slot].in_start;
            int total_samples = events[slot].sample_frames * events[slot].out_channels;

            if (total_samples > x->halfbuffer) {
                total_samples = x->halfbuffer;
            }

            for (int i = 0; i < total_samples; i++) {
                float abs_val = fabsf(processed_ptr[i]);
                if (abs_val > maxamp) maxamp = abs_val;
            }

            if (maxamp > 0.00001f) {
                float rescale = 1.0f / maxamp;
                for (int i = 0; i < total_samples; i++) {
                    processed_ptr[i] *= rescale;
                }
            }

            events[slot].status = ACTIVE;

            if (x->grab) {
                x->grab = 0;
                x->grab_slot = slot;
                x->grab_start = events[slot].in_start;
                qelem_set(x->qelem_grab);
            }
        }
    }

    buffer_unlocksamples(the_buffer);
}

int bashfest_set_parameters(t_bashfest *x, float *params, float transpose_factor)
{
    int pcount = 0;
    float *odds = x->odds;
    int maxproc = x->max_process_per_note;
    int minproc = x->min_process_per_note;
    t_cycle tcycle = x->tcycle;

    if (transpose_factor) {
        params[pcount++] = (float)TRANSPOSE;
        params[pcount++] = transpose_factor;
    } else if (tcycle.len > 0) {
        params[pcount++] = (float)TRANSPOSE;
        params[pcount++] = tcycle.data[tcycle.p++];
        if (tcycle.p >= tcycle.len) {
            tcycle.p = 0;
        }
        x->tcycle.p = tcycle.p;
    }

    if (maxproc <= 0) return pcount;

    int events = minproc + rand() % (1 + (maxproc - minproc));

    for (int i = 0; i < events; i++) {
        float rval = boundrand(0.0f, 1.0f);
        int j = 0;
        while (j < PROCESS_COUNT && rval > odds[j]) {
            j++;
        }
        if (j >= PROCESS_COUNT) j = PROCESS_COUNT - 1;

        if (j == RETRO) {
            params[pcount++] = (float)RETRO;
        } else if (j == COMB) {
            params[pcount++] = (float)COMB;
            params[pcount++] = boundrand(0.001f, 0.035f);
            params[pcount++] = boundrand(0.25f, 0.98f);
            params[pcount++] = boundrand(0.05f, 0.5f);
        } else if (j == RINGMOD) {
            params[pcount++] = (float)RINGMOD;
            params[pcount++] = boundrand(100.0f, 2000.0f);
        } else if (j == TRANSPOSE) {
            params[pcount++] = (float)TRANSPOSE;
            params[pcount++] = boundrand(0.25f, 3.0f);
        } else if (j == FLANGE) {
            params[pcount++] = (float)FLANGE;
            params[pcount++] = boundrand(100.0f, 400.0f);
            params[pcount++] = boundrand(600.0f, 4000.0f);
            params[pcount++] = boundrand(0.1f, 2.0f);
            params[pcount++] = boundrand(0.1f, 0.95f);
            params[pcount++] = boundrand(0.0f, 0.9f);
        } else if (j == BUTTER) {
            params[pcount++] = (float)BUTTER;
            int type = rand() % 3;
            params[pcount++] = (float)type;
            float cf = boundrand(70.0f, 3000.0f);
            params[pcount++] = cf;
            if (type == BANDPASS) {
                params[pcount++] = cf * boundrand(0.05f, 0.6f);
            }
        } else if (j == TRUNCATE) {
            params[pcount++] = (float)TRUNCATE;
            params[pcount++] = boundrand(0.05f, 0.15f);
            params[pcount++] = boundrand(0.01f, 0.05f);
        } else if (j == SWEEPRESON) {
            params[pcount++] = (float)SWEEPRESON;
            params[pcount++] = boundrand(100.0f, 300.0f);
            params[pcount++] = boundrand(600.0f, 6000.0f);
            params[pcount++] = boundrand(0.01f, 0.2f);
            params[pcount++] = boundrand(0.05f, 2.0f);
            params[pcount++] = boundrand(0.0f, 1.0f);
        } else if (j == SLIDECOMB) {
            params[pcount++] = (float)SLIDECOMB;
            params[pcount++] = boundrand(0.001f, 0.03f);
            params[pcount++] = boundrand(0.001f, 0.03f);
            params[pcount++] = boundrand(0.05f, 0.95f);
            params[pcount++] = boundrand(0.05f, 0.5f);
        } else if (j == REVERB1) {
            params[pcount++] = (float)REVERB1;
            params[pcount++] = boundrand(0.25f, 0.99f);
            params[pcount++] = boundrand(0.1f, 1.0f);
            params[pcount++] = boundrand(0.2f, 0.8f);
        } else if (j == ELLIPSE) {
            params[pcount++] = (float)ELLIPSE;
            params[pcount++] = (float)(rand() % ELLIPSE_FILTER_COUNT);
        } else if (j == FEED1) {
            params[pcount++] = (float)FEED1;
            float tval = boundrand(0.001f, 0.1f);
            params[pcount++] = tval;
            params[pcount++] = boundrand(tval, 0.1f);
            tval = boundrand(0.01f, 0.5f);
            params[pcount++] = tval;
            params[pcount++] = boundrand(tval, 0.5f);
            params[pcount++] = boundrand(0.05f, 1.0f);
        } else if (j == FLAM1) {
            params[pcount++] = (float)FLAM1;
            params[pcount++] = (float)(4 + (rand() % 20));
            params[pcount++] = boundrand(0.3f, 0.8f);
            params[pcount++] = boundrand(0.5f, 1.2f);
            params[pcount++] = boundrand(0.025f, 0.15f);
        } else if (j == FLAM2) {
            params[pcount++] = (float)FLAM2;
            params[pcount++] = (float)(4 + (rand() % 20));
            params[pcount++] = boundrand(0.1f, 0.9f);
            params[pcount++] = boundrand(0.2f, 1.2f);
            params[pcount++] = boundrand(0.025f, 0.15f);
            params[pcount++] = boundrand(0.025f, 0.15f);
        } else if (j == EXPFLAM) {
            params[pcount++] = (float)EXPFLAM;
            params[pcount++] = (float)(4 + (rand() % 20));
            params[pcount++] = boundrand(0.1f, 0.9f);
            params[pcount++] = boundrand(0.2f, 1.2f);
            params[pcount++] = boundrand(0.025f, 0.15f);
            params[pcount++] = boundrand(0.025f, 0.15f);
            params[pcount++] = boundrand(-5.0f, 5.0f);
        } else if (j == COMB4) {
            params[pcount++] = (float)COMB4;
            params[pcount++] = boundrand(100.0f, 900.0f);
            params[pcount++] = boundrand(100.0f, 900.0f);
            params[pcount++] = boundrand(100.0f, 900.0f);
            params[pcount++] = boundrand(100.0f, 900.0f);
            float tval = boundrand(0.5f, 0.99f);
            params[pcount++] = tval;
            params[pcount++] = tval;
        } else if (j == COMPDIST) {
            params[pcount++] = (float)COMPDIST;
            float tval = boundrand(0.01f, 0.25f);
            params[pcount++] = tval;
            params[pcount++] = boundrand(tval, 0.9f);
            params[pcount++] = 1.0f;
        } else if (j == RINGFEED) {
            params[pcount++] = (float)RINGFEED;
            params[pcount++] = boundrand(90.0f, 1500.0f);
            params[pcount++] = boundrand(90.0f, 1500.0f);
            params[pcount++] = boundrand(0.2f, 0.95f);
            params[pcount++] = boundrand(90.0f, 1500.0f);
            params[pcount++] = boundrand(0.01f, 0.4f);
            params[pcount++] = boundrand(0.05f, 1.0f);
        } else if (j == RESONADSR) {
            params[pcount++] = (float)RESONADSR;
            params[pcount++] = boundrand(0.01f, 0.1f);
            params[pcount++] = boundrand(0.01f, 0.05f);
            params[pcount++] = boundrand(0.05f, 0.5f);
            params[pcount++] = boundrand(150.0f, 4000.0f);
            params[pcount++] = boundrand(150.0f, 4000.0f);
            params[pcount++] = boundrand(150.0f, 4000.0f);
            params[pcount++] = boundrand(150.0f, 4000.0f);
            params[pcount++] = boundrand(0.03f, 0.7f);
        } else if (j == STV) {
            params[pcount++] = (float)STV;
            params[pcount++] = boundrand(0.025f, 0.5f);
            params[pcount++] = boundrand(0.025f, 0.5f);
            params[pcount++] = boundrand(0.001f, 0.01f);
        }

        if (pcount >= MAX_PARAMETERS - 32) {
            break;
        }
    }
    return pcount;
}

void bashfest_dsp64(t_bashfest *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;

    attach_buffer(x);

    if (x->sr != (float)samplerate) {
        x->sr = (float)samplerate;
    }
    if (maxvectorsize > 0) {
        x->vs = (int)maxvectorsize;
    }

    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)bashfest_perform64, 0, NULL);
}

void bashfest_assist(t_bashfest *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Click Trigger", 256); break;
            case 1: strncpy_zero(dst, "(signal) Click Increment", 256); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Channel 1 Output (L)", 256); break;
            case 1: strncpy_zero(dst, "(signal) Channel 2 Output (R)", 256); break;
        }
    }
}
