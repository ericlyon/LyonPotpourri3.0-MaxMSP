#include "MSPd.h"
#include "chameleon.h"

#define MAX_PARAMETERS 1024
#define MAX_SLOTS 512
#define PROCESS_COUNT 20
#define MAX_VEC 4096
#define OBJECT_NAME "chameleon~"

static t_class *chameleon_class;

/* Method Prototypes */
void *chameleon_new(t_symbol *msg, long argc, t_atom *argv);
void chameleon_assist(t_chameleon *x, void *b, long msg, long arg, char *dst);
void chameleon_dsp_free(t_chameleon *x);
void chameleon_set_parameters(t_chameleon *x);
void chameleon_info(t_chameleon *x);
void chameleon_mute(t_chameleon *x, double t);
void chameleon_maximum_process(t_chameleon *x, double n);
void chameleon_minimum_process(t_chameleon *x, double n);
void chameleon_flatodds(t_chameleon *x);
void chameleon_randodds(t_chameleon *x);
void chameleon_killproc(t_chameleon *x, double p);
void chameleon_soloproc(t_chameleon *x, double p);
void chameleon_setodds(t_chameleon *x, t_symbol *msg, long argc, t_atom *argv);
void chameleon_dsp64(t_chameleon *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void chameleon_store(t_chameleon *x, double t);
void chameleon_recall(t_chameleon *x, double t);
void chameleon_recall_parameters_exec(t_chameleon *x);
void chameleon_loadslot(t_chameleon *x, t_symbol *msg, long argc, t_atom *argv);
void chameleon_set_parameters_exec(t_chameleon *x);
void chameleon_print_parameters(t_chameleon *x);
void chameleon_report(t_chameleon *x);
void chameleon_perform64(t_chameleon *x, t_object *dsp64, double **ins,
                         long numins, double **outs, long numouts, long vectorsize,
                         long flags, void *userparam);
void chameleon_tweak_parameters(t_chameleon *x, double t);
void clip(double *x, double min, double max);

/* DSP Function Prototypes */
void ringmod(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void ringmod4(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void comber(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void flange(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void butterme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void sweepreson(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void truncateme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void slidecomb(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void reverb1(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void ellipseme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void feed1me(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void flam1(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void comb4(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void ringfeed(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void stv(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void compdist(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void bendy(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void slideflam(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void bitcrush(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.chameleon~",
                           (method)chameleon_new,
                           (method)chameleon_dsp_free,
                           (long)sizeof(t_chameleon),
                           0L,
                           A_GIMME,
                           0);

    class_addmethod(c, (method)chameleon_assist,          "assist",           A_CANT, 0);
    class_addmethod(c, (method)chameleon_dsp64,           "dsp64",            A_CANT, 0);
    class_addmethod(c, (method)chameleon_flatodds,        "flatodds",         0);
    class_addmethod(c, (method)chameleon_randodds,        "randodds",         0);
    class_addmethod(c, (method)chameleon_soloproc,        "soloproc",         A_FLOAT, 0);
    class_addmethod(c, (method)chameleon_killproc,        "killproc",         A_FLOAT, 0);
    class_addmethod(c, (method)chameleon_store,           "store",            A_FLOAT, 0);
    class_addmethod(c, (method)chameleon_recall,          "recall",           A_FLOAT, 0);
    class_addmethod(c, (method)chameleon_setodds,         "setodds",          A_GIMME, 0);
    class_addmethod(c, (method)chameleon_loadslot,        "loadslot",         A_GIMME, 0);
    class_addmethod(c, (method)chameleon_set_parameters,  "set_parameters",   0);
    class_addmethod(c, (method)chameleon_tweak_parameters,"tweak_parameters", A_FLOAT, 0);
    class_addmethod(c, (method)chameleon_maximum_process, "maximum_process",  A_FLOAT, 0);
    class_addmethod(c, (method)chameleon_minimum_process, "minimum_process",  A_FLOAT, 0);
    class_addmethod(c, (method)chameleon_print_parameters,"print_parameters", 0);
    class_addmethod(c, (method)chameleon_report,          "report",           0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    chameleon_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void clip(double *x, double min, double max)
{
    if (*x < min) *x = min;
    else if (*x > max) *x = max;
}

void chameleon_print_parameters(t_chameleon *x)
{
    post("loadslot 9999 %ld", x->pcount);
    for (int i = 0; i < x->pcount; i++) {
        post("%f", x->params[i]);
    }
}

void chameleon_report(t_chameleon *x)
{
    t_atom *data = x->data;
    t_symbol *loadslot = gensym("loadslot");
    t_symbol *comma = gensym(",");
    t_slot *slots = x->slots;
    int datacount = 0;

    for (int i = 0; i < MAX_SLOTS; i++) {
        if (slots[i].pcount > 0) {
            if (datacount + slots[i].pcount + 4 >= 1024) break;

            atom_setsym(&data[datacount++], loadslot);
            atom_setfloat(&data[datacount++], (float)i);
            atom_setfloat(&data[datacount++], (float)slots[i].pcount);
            for (int j = 0; j < slots[i].pcount; j++) {
                atom_setfloat(&data[datacount++], (float)slots[i].params[j]);
            }
            atom_setsym(&data[datacount++], comma);
        }
    }
    outlet_list(x->listo, NULL, (short)datacount, data);
}

void chameleon_maximum_process(t_chameleon *x, double n)
{
    if (n < 0.0) return;
    x->max_process_per_note = (int)n;
}

void chameleon_minimum_process(t_chameleon *x, double n)
{
    if (n < 0.0) return;
    x->min_process_per_note = (int)n;
}

void chameleon_setodds(t_chameleon *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc > PROCESS_COUNT) {
        object_error((t_object *)x, "%s: maximum %d processes supported", OBJECT_NAME, PROCESS_COUNT);
        return;
    }
    for (int i = 0; i < PROCESS_COUNT; i++) x->odds[i] = 0.0f;
    for (int i = 0; i < argc; i++) x->odds[i] = (float)atom_getfloat(argv + i);
    setweights(x->odds, PROCESS_COUNT);
}

void chameleon_soloproc(t_chameleon *x, double fp)
{
    int p = (int)fp;
    if (p < 0 || p >= PROCESS_COUNT) return;
    for (int i = 0; i < PROCESS_COUNT; i++) x->odds[i] = 0.0f;
    x->odds[p] = 1.0f;
    setweights(x->odds, PROCESS_COUNT);
}

void chameleon_store(t_chameleon *x, double fp)
{
    long slotnum = (long)fp;
    if (slotnum < 0 || slotnum >= MAX_SLOTS) {
        object_error((t_object *)x, "%s: %ld is not a valid slot number", OBJECT_NAME, slotnum);
        return;
    }
    x->slots[slotnum].pcount = x->pcount;
    for (int i = 0; i < x->pcount; i++) {
        x->slots[slotnum].params[i] = x->params[i];
    }
}

void chameleon_loadslot(t_chameleon *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 2) return;

    t_atom_long slot = atom_getlong(argv);
    t_atom_long pcount = atom_getlong(argv + 1);

    if (slot < 0 || slot >= MAX_SLOTS) {
        object_error((t_object *)x, "%s: invalid slot %ld", OBJECT_NAME, (long)slot);
        return;
    }
    if (pcount <= 0 || pcount > MAX_PARAMETERS) {
        object_error((t_object *)x, "%s: invalid pcount %ld", OBJECT_NAME, (long)pcount);
        return;
    }
    if (argc < pcount + 2) {
        object_error((t_object *)x, "%s: insufficient parameters in loadslot", OBJECT_NAME);
        return;
    }

    for (int i = 0; i < pcount; i++) {
        x->slots[slot].params[i] = atom_getfloat(argv + 2 + i);
    }
    x->slots[slot].pcount = (long)pcount;
}

void chameleon_recall(t_chameleon *x, double fp)
{
    long slotnum = (long)fp;
    if (slotnum < 0 || slotnum >= MAX_SLOTS) {
        object_error((t_object *)x, "%s: %ld is not a valid slot", OBJECT_NAME, slotnum);
        return;
    }
    x->recall_slot = slotnum;
    x->recall_parameters_flag = 1;
}

void chameleon_killproc(t_chameleon *x, double fp)
{
    int p = (int)fp;
    if (p < 0 || p >= PROCESS_COUNT) return;
    for (int i = 0; i < PROCESS_COUNT; i++) x->odds[i] = 1.0f;
    x->odds[p] = 0.0f;
    setweights(x->odds, PROCESS_COUNT);
}

void chameleon_flatodds(t_chameleon *x)
{
    for (int i = 0; i < PROCESS_COUNT; i++) x->odds[i] = 1.0f;
    setweights(x->odds, PROCESS_COUNT);
}

void chameleon_randodds(t_chameleon *x)
{
    for (int i = 0; i < PROCESS_COUNT; i++) x->odds[i] = boundrand(0.0f, 1.0f);
    setweights(x->odds, PROCESS_COUNT);
}

void *chameleon_new(t_symbol *msg, long argc, t_atom *argv)
{
    t_chameleon *x = (t_chameleon *)object_alloc(chameleon_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 2);
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->listo = listout((t_object *)x);
    outlet_new((t_object *)x, "signal");
    outlet_new((t_object *)x, "signal");

    x->sr = sys_getsr();
    x->vs = sys_getblksize();
    if (x->sr <= 0.0f) x->sr = 48000.0f;
    if (x->vs <= 0)    x->vs = 64;

    x->sinelen = 65536;
    x->verbose = 0;
    x->maxdelay = 1.0f;
    x->max_flangedelay = 0.1;

    x->chan1buf = (t_double *)sysmem_newptrclear(MAX_VEC * sizeof(t_double));
    x->chan2buf = (t_double *)sysmem_newptrclear(MAX_VEC * sizeof(t_double));

    x->data = (t_atom *)sysmem_newptrclear(1024 * sizeof(t_atom));
    x->sinewave = (double *)sysmem_newptrclear((x->sinelen + 1) * sizeof(double));
    x->params = (double *)sysmem_newptrclear(MAX_PARAMETERS * sizeof(double));
    x->odds = (float *)sysmem_newptrclear(64 * sizeof(float));
    x->distortion_length = 32768;
    x->distortion_function = (double *)sysmem_newptrclear(x->distortion_length * sizeof(double));
    set_distortion_table(x->distortion_function, 0.1, 0.5, x->distortion_length);

    putsine(x->sinewave, x->sinelen);

    x->comb_delay_pool1 = (double **)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(double *));
    x->comb_delay_pool2 = (double **)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(double *));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->comb_delay_pool1[i] = (double *)sysmem_newptrclear(((int)(x->maxdelay * x->sr) + 2) * sizeof(double));
        x->comb_delay_pool2[i] = (double *)sysmem_newptrclear(((int)(x->maxdelay * x->sr) + 2) * sizeof(double));
    }

    x->ringmod_phases = (double *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(double));
    x->ringmod4_phases = (double *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(double));

    x->flange_units = (t_flange_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_flange_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->flange_units[i].flange_dl1 = (double *)sysmem_newptrclear(((int)(x->maxdelay * x->sr) + 2) * sizeof(double));
        x->flange_units[i].flange_dl2 = (double *)sysmem_newptrclear(((int)(x->maxdelay * x->sr) + 2) * sizeof(double));
        x->flange_units[i].dv1 = (int *)sysmem_newptrclear(2 * sizeof(int));
        x->flange_units[i].dv2 = (int *)sysmem_newptrclear(2 * sizeof(int));
        delset2(x->flange_units[i].flange_dl1, x->flange_units[i].dv1, x->max_flangedelay, x->sr);
        delset2(x->flange_units[i].flange_dl2, x->flange_units[i].dv2, x->max_flangedelay, x->sr);
    }

    x->butterworth_units = (t_butterworth_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_butterworth_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->butterworth_units[i].data1 = (double *)sysmem_newptrclear(8 * sizeof(double));
        x->butterworth_units[i].data2 = (double *)sysmem_newptrclear(8 * sizeof(double));
    }

    x->truncate_units = (t_truncate_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_truncate_unit));

    x->sweepreson_units = (t_sweepreson_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_sweepreson_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->sweepreson_units[i].q1 = (double *)sysmem_newptrclear(5 * sizeof(double));
        x->sweepreson_units[i].q2 = (double *)sysmem_newptrclear(5 * sizeof(double));
    }

    x->slidecomb_units = (t_slidecomb_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_slidecomb_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->slidecomb_units[i].dv1 = (int *)sysmem_newptrclear(2 * sizeof(int));
        x->slidecomb_units[i].dv2 = (int *)sysmem_newptrclear(2 * sizeof(int));
        x->slidecomb_units[i].delayline1 = (double *)sysmem_newptrclear((2 + (int)(MAX_SLIDECOMB_DELAY * x->sr)) * sizeof(double));
        x->slidecomb_units[i].delayline2 = (double *)sysmem_newptrclear((2 + (int)(MAX_SLIDECOMB_DELAY * x->sr)) * sizeof(double));
        delset2(x->slidecomb_units[i].delayline1, x->slidecomb_units[i].dv1, MAX_SLIDECOMB_DELAY, x->sr);
        delset2(x->slidecomb_units[i].delayline2, x->slidecomb_units[i].dv2, MAX_SLIDECOMB_DELAY, x->sr);
    }

    x->reverb1_units = (t_reverb1_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_reverb1_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->reverb1_units[i].dels = (double *)sysmem_newptrclear(4 * sizeof(double));
        x->reverb1_units[i].eel1 = (LSTRUCT *)sysmem_newptrclear(MAXSECTS * sizeof(LSTRUCT));
        x->reverb1_units[i].eel2 = (LSTRUCT *)sysmem_newptrclear(MAXSECTS * sizeof(LSTRUCT));
        x->reverb1_units[i].alpo1 = (double **)sysmem_newptrclear(4 * sizeof(double *));
        x->reverb1_units[i].alpo2 = (double **)sysmem_newptrclear(4 * sizeof(double *));
        for (int j = 0; j < 4; j++) {
            x->reverb1_units[i].alpo1[j] = (double *)sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 1) * sizeof(double));
            x->reverb1_units[i].alpo2[j] = (double *)sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 1) * sizeof(double));
        }
    }
    x->reverb_ellipse_data = (double *)sysmem_newptrclear(16 * sizeof(double));

    x->ellipseme_units = (t_ellipseme_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_ellipseme_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->ellipseme_units[i].eel1 = (LSTRUCT *)sysmem_newptrclear(MAXSECTS * sizeof(LSTRUCT));
        x->ellipseme_units[i].eel2 = (LSTRUCT *)sysmem_newptrclear(MAXSECTS * sizeof(LSTRUCT));
    }

    x->ellipse_data = (double **)sysmem_newptr(MAXFILTER * sizeof(double *));
    for (int i = 0; i < MAXFILTER; i++) {
        x->ellipse_data[i] = (double *)sysmem_newptrclear(MAX_COEF * sizeof(double));
    }

    x->feed1_units = (t_feed1_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_feed1_unit));
    x->feedfunclen = 8192;
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->feed1_units[i].func1 = (double *)sysmem_newptrclear(2 * FEEDFUNCLEN * sizeof(double));
        x->feed1_units[i].func2 = (double *)sysmem_newptrclear(2 * FEEDFUNCLEN * sizeof(double));
        x->feed1_units[i].func3 = (double *)sysmem_newptrclear(2 * FEEDFUNCLEN * sizeof(double));
        x->feed1_units[i].func4 = (double *)sysmem_newptrclear(2 * FEEDFUNCLEN * sizeof(double));
        x->feed1_units[i].delayLine1a = (double *)sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 5) * sizeof(double));
        x->feed1_units[i].delayLine2a = (double *)sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 5) * sizeof(double));
        x->feed1_units[i].delayLine1b = (double *)sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 5) * sizeof(double));
        x->feed1_units[i].delayLine2b = (double *)sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 5) * sizeof(double));
        x->feed1_units[i].dv1a = (int *)sysmem_newptrclear(2 * sizeof(int));
        x->feed1_units[i].dv2a = (int *)sysmem_newptrclear(2 * sizeof(int));
        x->feed1_units[i].dv1b = (int *)sysmem_newptrclear(2 * sizeof(int));
        x->feed1_units[i].dv2b = (int *)sysmem_newptrclear(2 * sizeof(int));
    }

    x->bitcrush_factors = (double *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(double));

    x->flam1_units = (t_flam1_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_flam1_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->flam1_units[i].delayline1 = (double *)sysmem_newptrclear(((int)(x->sr * FLAM1_MAX_DELAY) + 5) * sizeof(double));
        x->flam1_units[i].delayline2 = (double *)sysmem_newptrclear(((int)(x->sr * FLAM1_MAX_DELAY) + 5) * sizeof(double));
        x->flam1_units[i].dv1 = (int *)sysmem_newptrclear(2 * sizeof(int));
        x->flam1_units[i].dv2 = (int *)sysmem_newptrclear(2 * sizeof(int));
    }

    x->comb4_units = (t_comb4_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_comb4_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->comb4_units[i].combs1 = (double **)sysmem_newptrclear(4 * sizeof(double *));
        x->comb4_units[i].combs2 = (double **)sysmem_newptrclear(4 * sizeof(double *));
        for (int j = 0; j < 4; j++) {
            x->comb4_units[i].combs1[j] = (double *)sysmem_newptrclear((5 + (int)(TONECOMB_MAX_DELAY * x->sr)) * sizeof(double));
            x->comb4_units[i].combs2[j] = (double *)sysmem_newptrclear((5 + (int)(TONECOMB_MAX_DELAY * x->sr)) * sizeof(double));
        }
    }

    x->resonfeed_units = (t_resonfeed_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_resonfeed_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->resonfeed_units[i].res1q = (double *)sysmem_newptrclear(5 * sizeof(double));
        x->resonfeed_units[i].res2q = (double *)sysmem_newptrclear(5 * sizeof(double));
        x->resonfeed_units[i].comb1arr = (double *)sysmem_newptrclear((int)(TONECOMB_MAX_DELAY * x->sr) * sizeof(double));
        x->resonfeed_units[i].comb2arr = (double *)sysmem_newptrclear((int)(TONECOMB_MAX_DELAY * x->sr) * sizeof(double));
    }

    x->resonadsr_units = (t_resonadsr_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_resonadsr_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->resonadsr_units[i].q1 = (double *)sysmem_newptrclear(5 * sizeof(double));
        x->resonadsr_units[i].q2 = (double *)sysmem_newptrclear(5 * sizeof(double));
        x->resonadsr_units[i].adsr = (CMIXADSR *)sysmem_newptrclear(sizeof(CMIXADSR));
        x->resonadsr_units[i].adsr->len = 8192;
        x->resonadsr_units[i].adsr->func = (double *)sysmem_newptrclear(8192 * sizeof(double));
    }

    x->stv_units = (t_stv_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_stv_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->stv_units[i].delayline1 = (double *)sysmem_newptrclear(((int)(x->sr * STV_MAX_DELAY) + 5) * sizeof(double));
        x->stv_units[i].delayline2 = (double *)sysmem_newptrclear(((int)(x->sr * STV_MAX_DELAY) + 5) * sizeof(double));
        x->stv_units[i].dv1 = (int *)sysmem_newptrclear(2 * sizeof(int));
        x->stv_units[i].dv2 = (int *)sysmem_newptrclear(2 * sizeof(int));
    }

    x->bendy_units = (t_bendy_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_bendy_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->bendy_units[i].delayline1 = (double *)sysmem_newptrclear(((int)(x->sr * BENDY_MAXDEL) + 5) * sizeof(double));
        x->bendy_units[i].delayline2 = (double *)sysmem_newptrclear(((int)(x->sr * BENDY_MAXDEL) + 5) * sizeof(double));
        x->bendy_units[i].dv1 = (int *)sysmem_newptrclear(8 * sizeof(int));
        x->bendy_units[i].dv2 = (int *)sysmem_newptrclear(8 * sizeof(int));
        x->bendy_units[i].val1 = boundrand(0.01f, (float)BENDY_MAXDEL);
        x->bendy_units[i].val2 = boundrand(0.01f, (float)BENDY_MAXDEL);
        x->bendy_units[i].counter = 0;
    }

    x->slideflam_units = (t_slideflam_unit *)sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_slideflam_unit));
    for (int i = 0; i < MAX_DSP_UNITS; i++) {
        x->slideflam_units[i].dv1 = (int *)sysmem_newptrclear(2 * sizeof(int));
        x->slideflam_units[i].dv2 = (int *)sysmem_newptrclear(2 * sizeof(int));
        x->slideflam_units[i].delayline1 = (double *)sysmem_newptrclear((10 + (int)(MAX_SLIDEFLAM_DELAY * x->sr)) * sizeof(double));
        x->slideflam_units[i].delayline2 = (double *)sysmem_newptrclear((10 + (int)(MAX_SLIDEFLAM_DELAY * x->sr)) * sizeof(double));
        delset2(x->slideflam_units[i].delayline1, x->slideflam_units[i].dv1, MAX_SLIDEFLAM_DELAY, x->sr);
        delset2(x->slideflam_units[i].delayline2, x->slideflam_units[i].dv2, MAX_SLIDEFLAM_DELAY, x->sr);
    }

    x->dcflt = (double *)sysmem_newptrclear(16 * sizeof(double));

    x->slots = (t_slot *)sysmem_newptrclear(MAX_SLOTS * sizeof(t_slot));
    for (int i = 0; i < MAX_SLOTS; i++) {
        x->slots[i].params = (double *)sysmem_newptrclear(MAX_PARAMETERS * sizeof(double));
    }

    set_dcflt(x->dcflt);
    init_reverb_data(x->reverb_ellipse_data);
    init_ellipse_data(x->ellipse_data);

    for (int i = 0; i < PROCESS_COUNT; i++) x->odds[i] = 1.0f;
    x->min_process_per_note = 1;
    x->max_process_per_note = 1;
    setweights(x->odds, PROCESS_COUNT);

    x->ratios = (double *)sysmem_newptrclear(5 * sizeof(double));
    x->ratios[0] = 9.0 / 8.0;
    x->ratios[1] = 4.0 / 3.0;
    x->ratios[2] = 5.0 / 4.0;
    x->ratios[3] = 6.0 / 5.0;
    x->ratios[4] = 3.0 / 2.0;

    chameleon_set_parameters(x);
    return x;
}

void chameleon_dsp_free(t_chameleon *x)
{
    if (!x) return;
    dsp_free((t_pxobject *)x);

    if (x->chan1buf) sysmem_freeptr(x->chan1buf);
    if (x->chan2buf) sysmem_freeptr(x->chan2buf);

    if (x->sinewave) sysmem_freeptr(x->sinewave);
    if (x->params) sysmem_freeptr(x->params);
    if (x->odds) sysmem_freeptr(x->odds);
    if (x->data) sysmem_freeptr(x->data);
    if (x->distortion_function) sysmem_freeptr(x->distortion_function);
    if (x->ratios) sysmem_freeptr(x->ratios);
    if (x->reverb_ellipse_data) sysmem_freeptr(x->reverb_ellipse_data);
    if (x->dcflt) sysmem_freeptr(x->dcflt);
    if (x->ringmod_phases) sysmem_freeptr(x->ringmod_phases);
    if (x->ringmod4_phases) sysmem_freeptr(x->ringmod4_phases);
    if (x->bitcrush_factors) sysmem_freeptr(x->bitcrush_factors);

    if (x->comb_delay_pool1) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) sysmem_freeptr(x->comb_delay_pool1[i]);
        sysmem_freeptr(x->comb_delay_pool1);
    }
    if (x->comb_delay_pool2) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) sysmem_freeptr(x->comb_delay_pool2[i]);
        sysmem_freeptr(x->comb_delay_pool2);
    }

    if (x->flange_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->flange_units[i].flange_dl1);
            sysmem_freeptr(x->flange_units[i].flange_dl2);
            sysmem_freeptr(x->flange_units[i].dv1);
            sysmem_freeptr(x->flange_units[i].dv2);
        }
        sysmem_freeptr(x->flange_units);
    }

    if (x->butterworth_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->butterworth_units[i].data1);
            sysmem_freeptr(x->butterworth_units[i].data2);
        }
        sysmem_freeptr(x->butterworth_units);
    }

    if (x->truncate_units) sysmem_freeptr(x->truncate_units);

    if (x->sweepreson_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->sweepreson_units[i].q1);
            sysmem_freeptr(x->sweepreson_units[i].q2);
        }
        sysmem_freeptr(x->sweepreson_units);
    }

    if (x->slidecomb_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->slidecomb_units[i].delayline1);
            sysmem_freeptr(x->slidecomb_units[i].delayline2);
            sysmem_freeptr(x->slidecomb_units[i].dv1);
            sysmem_freeptr(x->slidecomb_units[i].dv2);
        }
        sysmem_freeptr(x->slidecomb_units);
    }

    if (x->slideflam_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->slideflam_units[i].delayline1);
            sysmem_freeptr(x->slideflam_units[i].delayline2);
            sysmem_freeptr(x->slideflam_units[i].dv1);
            sysmem_freeptr(x->slideflam_units[i].dv2);
        }
        sysmem_freeptr(x->slideflam_units);
    }

    if (x->bendy_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->bendy_units[i].delayline1);
            sysmem_freeptr(x->bendy_units[i].delayline2);
            sysmem_freeptr(x->bendy_units[i].dv1);
            sysmem_freeptr(x->bendy_units[i].dv2);
        }
        sysmem_freeptr(x->bendy_units);
    }

    if (x->flam1_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->flam1_units[i].delayline1);
            sysmem_freeptr(x->flam1_units[i].delayline2);
            sysmem_freeptr(x->flam1_units[i].dv1);
            sysmem_freeptr(x->flam1_units[i].dv2);
        }
        sysmem_freeptr(x->flam1_units);
    }

    if (x->reverb1_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->reverb1_units[i].dels);
            sysmem_freeptr(x->reverb1_units[i].eel1);
            sysmem_freeptr(x->reverb1_units[i].eel2);
            for (int j = 0; j < 4; j++) {
                sysmem_freeptr(x->reverb1_units[i].alpo1[j]);
                sysmem_freeptr(x->reverb1_units[i].alpo2[j]);
            }
            sysmem_freeptr(x->reverb1_units[i].alpo1);
            sysmem_freeptr(x->reverb1_units[i].alpo2);
        }
        sysmem_freeptr(x->reverb1_units);
    }

    if (x->ellipseme_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->ellipseme_units[i].eel1);
            sysmem_freeptr(x->ellipseme_units[i].eel2);
        }
        sysmem_freeptr(x->ellipseme_units);
    }

    if (x->ellipse_data) {
        for (int i = 0; i < MAXFILTER; i++) sysmem_freeptr(x->ellipse_data[i]);
        sysmem_freeptr(x->ellipse_data);
    }

    if (x->feed1_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->feed1_units[i].func1);
            sysmem_freeptr(x->feed1_units[i].func2);
            sysmem_freeptr(x->feed1_units[i].func3);
            sysmem_freeptr(x->feed1_units[i].func4);
            sysmem_freeptr(x->feed1_units[i].delayLine1a);
            sysmem_freeptr(x->feed1_units[i].delayLine2a);
            sysmem_freeptr(x->feed1_units[i].delayLine1b);
            sysmem_freeptr(x->feed1_units[i].delayLine2b);
            sysmem_freeptr(x->feed1_units[i].dv1a);
            sysmem_freeptr(x->feed1_units[i].dv2a);
            sysmem_freeptr(x->feed1_units[i].dv1b);
            sysmem_freeptr(x->feed1_units[i].dv2b);
        }
        sysmem_freeptr(x->feed1_units);
    }

    if (x->comb4_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            for (int j = 0; j < 4; j++) {
                sysmem_freeptr(x->comb4_units[i].combs1[j]);
                sysmem_freeptr(x->comb4_units[i].combs2[j]);
            }
            sysmem_freeptr(x->comb4_units[i].combs1);
            sysmem_freeptr(x->comb4_units[i].combs2);
        }
        sysmem_freeptr(x->comb4_units);
    }

    if (x->resonfeed_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->resonfeed_units[i].res1q);
            sysmem_freeptr(x->resonfeed_units[i].res2q);
            sysmem_freeptr(x->resonfeed_units[i].comb1arr);
            sysmem_freeptr(x->resonfeed_units[i].comb2arr);
        }
        sysmem_freeptr(x->resonfeed_units);
    }

    if (x->resonadsr_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->resonadsr_units[i].q1);
            sysmem_freeptr(x->resonadsr_units[i].q2);
            if (x->resonadsr_units[i].adsr) {
                sysmem_freeptr(x->resonadsr_units[i].adsr->func);
                sysmem_freeptr(x->resonadsr_units[i].adsr);
            }
        }
        sysmem_freeptr(x->resonadsr_units);
    }

    if (x->stv_units) {
        for (int i = 0; i < MAX_DSP_UNITS; i++) {
            sysmem_freeptr(x->stv_units[i].delayline1);
            sysmem_freeptr(x->stv_units[i].delayline2);
            sysmem_freeptr(x->stv_units[i].dv1);
            sysmem_freeptr(x->stv_units[i].dv2);
        }
        sysmem_freeptr(x->stv_units);
    }

    if (x->slots) {
        for (int i = 0; i < MAX_SLOTS; i++) {
            sysmem_freeptr(x->slots[i].params);
        }
        sysmem_freeptr(x->slots);
    }
}

void chameleon_perform64(t_chameleon *x, t_object *dsp64, double **ins,
                         long numins, double **outs, long numouts, long n,
                         long flags, void *userparam)
{
    t_double *OGchan1 = ins[0];
    t_double *OGchan2 = ins[1];
    t_double *outchanL = outs[0];
    t_double *outchanR = outs[1];

    if (x->set_parameters_flag == 1) {
        chameleon_set_parameters_exec(x);
    } else if (x->recall_parameters_flag == 1) {
        chameleon_recall_parameters_exec(x);
    }

    if (n > MAX_VEC) n = MAX_VEC;

    t_double *chan1 = x->chan1buf;
    t_double *chan2 = x->chan2buf;

    for (long i = 0; i < n; i++) {
        chan1[i] = OGchan1[i];
        chan2[i] = OGchan2[i];
    }

    long pcount = x->pcount;
    double *params = x->params;
    long curarg = 0;

    while (curarg < pcount) {
        int type = (int)params[curarg];
        if      (type == BENDY)     bendy(x, &curarg, chan1, chan2);
        else if (type == RINGMOD)   ringmod(x, &curarg, chan1, chan2);
        else if (type == BITCRUSH)  bitcrush(x, &curarg, chan1, chan2);
        else if (type == COMB)      comber(x, &curarg, chan1, chan2);
        else if (type == FLANGE)    flange(x, &curarg, chan1, chan2);
        else if (type == BUTTER)    butterme(x, &curarg, chan1, chan2);
        else if (type == TRUNCATE)  truncateme(x, &curarg, chan1, chan2);
        else if (type == SWEEPRESON)sweepreson(x, &curarg, chan1, chan2);
        else if (type == SLIDECOMB) slidecomb(x, &curarg, chan1, chan2);
        else if (type == REVERB1)   reverb1(x, &curarg, chan1, chan2);
        else if (type == ELLIPSE)   ellipseme(x, &curarg, chan1, chan2);
        else if (type == FEED1)     feed1me(x, &curarg, chan1, chan2);
        else if (type == FLAM1)     flam1(x, &curarg, chan1, chan2);
        else if (type == SLIDEFLAM) slideflam(x, &curarg, chan1, chan2);
        else if (type == RINGMOD4)  ringmod4(x, &curarg, chan1, chan2);
        else if (type == COMB4)     comb4(x, &curarg, chan1, chan2);
        else if (type == COMPDIST)  compdist(x, &curarg, chan1, chan2);
        else if (type == RINGFEED)  ringfeed(x, &curarg, chan1, chan2);
        else if (type == RESONADSR) resonadsr(x, &curarg, chan1, chan2);
        else if (type == STV)       stv(x, &curarg, chan1, chan2);
        else {
            curarg++;
        }
    }

    for (long i = 0; i < n; i++) {
        outchanL[i] = chan1[i];
        outchanR[i] = chan2[i];
    }
}

void chameleon_set_parameters(t_chameleon *x)
{
    x->set_parameters_flag = 1;
}

void chameleon_tweak_parameters(t_chameleon *x, double tdev)
{
    clip(&tdev, 0.001, 0.5);
    double multmin = 1.0 - tdev;
    double multmax = 1.0 + tdev;
    long slotnum = x->recall_slot;

    if (slotnum < 0 || slotnum >= MAX_SLOTS) return;
    long slot_pcount = x->slots[slotnum].pcount;
    if (slot_pcount <= 0) return;

    double *rparams = x->slots[slotnum].params;
    double *params = x->params;
    long pcount = 0;
    long rpcount = 0;

    while (rpcount < slot_pcount && pcount < MAX_PARAMETERS - 16) {
        int j = (int)rparams[rpcount++];
        params[pcount++] = (double)j;

        if (j == COMB) {
            double delay = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&delay, 0.01, 0.35);
            params[pcount++] = delay;
            double revtime = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&revtime, 0.5, 0.98);
            params[pcount++] = revtime;
            int comb_dl_count = (int)rparams[rpcount++];
            params[pcount++] = (double)comb_dl_count;
            mycombset(delay, revtime, 0, x->comb_delay_pool1[comb_dl_count], x->sr);
            mycombset(delay, revtime, 0, x->comb_delay_pool2[comb_dl_count], x->sr);
        } else if (j == RINGMOD || j == RINGMOD4) {
            double tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 40.0, 3000.0);
            params[pcount++] = tmp;
            params[pcount++] = rparams[rpcount++];
        } else if (j == BENDY) {
            int bendy_count = (int)rparams[rpcount++];
            params[pcount++] = (double)bendy_count;
            double tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.01, BENDY_MAXDEL);
            params[pcount++] = x->bendy_units[bendy_count].val1 = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.01, BENDY_MAXDEL);
            params[pcount++] = x->bendy_units[bendy_count].val2 = tmp;
            x->bendy_units[bendy_count].counter = 0;
            delset2(x->bendy_units[bendy_count].delayline1, x->bendy_units[bendy_count].dv1, x->bendy_units[bendy_count].val1, x->sr);
            delset2(x->bendy_units[bendy_count].delayline2, x->bendy_units[bendy_count].dv2, x->bendy_units[bendy_count].val2, x->sr);
        } else if (j == FLANGE) {
            double tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 80.0, 400.0);
            params[pcount++] = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 600.0, 4000.0);
            params[pcount++] = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.1, 2.0);
            params[pcount++] = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.1, 0.95);
            params[pcount++] = tmp;
            params[pcount++] = rparams[rpcount++];
        } else if (j == BUTTER) {
            int ftype = (int)rparams[rpcount++];
            params[pcount++] = (double)ftype;
            double cf = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&cf, 70.0, 3000.0);
            params[pcount++] = cf;
            double bw = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&bw, 0.05, 0.6);
            params[pcount++] = bw;
            int butterworth_count = (int)rparams[rpcount++];
            params[pcount++] = (double)butterworth_count;
            if (ftype == LOPASS) {
                lobut(x->butterworth_units[butterworth_count].data1, cf, x->sr);
                lobut(x->butterworth_units[butterworth_count].data2, cf, x->sr);
            } else if (ftype == HIPASS) {
                hibut(x->butterworth_units[butterworth_count].data1, cf, x->sr);
                hibut(x->butterworth_units[butterworth_count].data2, cf, x->sr);
            } else if (ftype == BANDPASS) {
                bpbut(x->butterworth_units[butterworth_count].data1, cf, bw, x->sr);
                bpbut(x->butterworth_units[butterworth_count].data2, cf, bw, x->sr);
            }
        } else if (j == TRUNCATE) {
            params[pcount++] = rparams[rpcount++];
        } else if (j == SWEEPRESON) {
            int sweepreson_count = (int)rparams[rpcount++];
            params[pcount++] = (double)sweepreson_count;
            double tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 100.0, 300.0);
            params[pcount++] = x->sweepreson_units[sweepreson_count].minfreq = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 600.0, 6000.0);
            params[pcount++] = x->sweepreson_units[sweepreson_count].maxfreq = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.01, 0.2);
            params[pcount++] = x->sweepreson_units[sweepreson_count].bwfac = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.05, 2.0);
            params[pcount++] = x->sweepreson_units[sweepreson_count].speed = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.0, 0.5);
            params[pcount++] = x->sweepreson_units[sweepreson_count].phase = tmp;
        } else if (j == SLIDECOMB) {
            int slidecomb_count = (int)rparams[rpcount++];
            params[pcount++] = (double)slidecomb_count;
            double tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.001, MAX_SLIDECOMB_DELAY * 0.95);
            params[pcount++] = x->slidecomb_units[slidecomb_count].start_delay = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.001, MAX_SLIDECOMB_DELAY * 0.95);
            params[pcount++] = x->slidecomb_units[slidecomb_count].end_delay = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.7, 0.99);
            params[pcount++] = x->slidecomb_units[slidecomb_count].feedback = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.1, 2.0);
            params[pcount++] = tmp;
            x->slidecomb_units[slidecomb_count].sample_length = (long)(tmp * x->sr);
            x->slidecomb_units[slidecomb_count].counter = 0;
            delset2(x->slidecomb_units[slidecomb_count].delayline1, x->slidecomb_units[slidecomb_count].dv1, MAX_SLIDECOMB_DELAY, x->sr);
            delset2(x->slidecomb_units[slidecomb_count].delayline2, x->slidecomb_units[slidecomb_count].dv2, MAX_SLIDECOMB_DELAY, x->sr);
        } else if (j == REVERB1) {
            int reverb1_count = (int)rparams[rpcount++];
            params[pcount++] = (double)reverb1_count;
            double tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.25, 0.99);
            params[pcount++] = x->reverb1_units[reverb1_count].revtime = tmp;
            tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 0.2, 0.8);
            params[pcount++] = tmp;
            x->reverb1_units[reverb1_count].wet = sin(1.570796 * tmp);
            x->reverb1_units[reverb1_count].dry = cos(1.570796 * tmp);
            for (int k = 0; k < 4; k++) {
                tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
                clip(&tmp, 0.005, 0.1);
                params[pcount++] = x->reverb1_units[reverb1_count].dels[k] = tmp;
                mycombset(tmp, x->reverb1_units[reverb1_count].revtime, 0, x->reverb1_units[reverb1_count].alpo1[k], x->sr);
                mycombset(tmp, x->reverb1_units[reverb1_count].revtime, 0, x->reverb1_units[reverb1_count].alpo2[k], x->sr);
            }
        } else if (j == ELLIPSE) {
            params[pcount++] = rparams[rpcount++];
            params[pcount++] = rparams[rpcount++];
        } else if (j == BITCRUSH) {
            int bitcrush_count = (int)rparams[rpcount++];
            params[pcount++] = (double)bitcrush_count;
            double tmp = rparams[rpcount++] * boundrand((float)multmin, (float)multmax);
            clip(&tmp, 2.0, 8.0);
            params[pcount++] = x->bitcrush_factors[bitcrush_count] = tmp;
        } else if (j == COMPDIST) {
            // No parameters
        } else {
            break;
        }
    }
    x->pcount = pcount;
}

void chameleon_recall_parameters_exec(t_chameleon *x)
{
    if (x->recall_parameters_flag != 1) return;
    x->recall_parameters_flag = 0;

    long slotnum = x->recall_slot;
    if (slotnum < 0 || slotnum >= MAX_SLOTS) return;

    long slot_pcount = x->slots[slotnum].pcount;
    if (slot_pcount <= 0 || slot_pcount > MAX_PARAMETERS) return;

    for (int i = 0; i < slot_pcount; i++) {
        x->params[i] = x->slots[slotnum].params[i];
    }
    x->pcount = slot_pcount;
}

void chameleon_set_parameters_exec(t_chameleon *x)
{
    if (x->set_parameters_flag == 0) return;
    x->set_parameters_flag = 0;

    int maxproc = x->max_process_per_note;
    int minproc = x->min_process_per_note;
    if (maxproc <= 0) return;

    int events = (int)floor(boundrand((float)minproc, (float)maxproc));
    long pcount = 0;
    float *odds = x->odds;
    double *params = x->params;

    int comb_dl_count = 0;
    int flange_count = 0;
    int truncate_count = 0;
    int butterworth_count = 0;
    int sweepreson_count = 0;
    int slidecomb_count = 0;
    int reverb1_count = 0;
    int ellipseme_count = 0;
    int feed1_count = 0;
    int flam1_count = 0;
    int comb4_count = 0;
    int ringfeed_count = 0;
    int bendy_count = 0;
    int ringmod_count = 0;
    int ringmod4_count = 0;
    int slideflam_count = 0;
    int bitcrush_count = 0;
    int resonadsr_count = 0;
    int stv_count = 0;

    for (int i = 0; i < events && pcount < MAX_PARAMETERS - 32; i++) {
        float rval = boundrand(0.0f, 1.0f);
        int j = 0;
        while (j < PROCESS_COUNT && rval > odds[j]) j++;
        if (j >= PROCESS_COUNT) j = PROCESS_COUNT - 1;

        if (j == COMB) {
            params[pcount++] = COMB;
            double delay = boundrand(0.01f, 0.35f);
            params[pcount++] = delay;
            double revtime = boundrand(0.5f, 0.98f);
            params[pcount++] = revtime;
            params[pcount++] = (double)comb_dl_count;
            mycombset(delay, revtime, 0, x->comb_delay_pool1[comb_dl_count], x->sr);
            mycombset(delay, revtime, 0, x->comb_delay_pool2[comb_dl_count], x->sr);
            comb_dl_count = (comb_dl_count + 1) % MAX_DSP_UNITS;
        } else if (j == RINGMOD) {
            params[pcount++] = RINGMOD;
            params[pcount++] = boundrand(100.0f, 2000.0f);
            params[pcount++] = (double)ringmod_count;
            x->ringmod_phases[ringmod_count] = 0.0;
            ringmod_count = (ringmod_count + 1) % MAX_DSP_UNITS;
        } else if (j == RINGMOD4) {
            params[pcount++] = RINGMOD4;
            params[pcount++] = boundrand(100.0f, 2000.0f);
            params[pcount++] = (double)ringmod4_count;
            x->ringmod4_phases[ringmod4_count] = 0.0;
            ringmod4_count = (ringmod4_count + 1) % MAX_DSP_UNITS;
        } else if (j == BENDY) {
            params[pcount++] = BENDY;
            params[pcount++] = (double)bendy_count;
            params[pcount++] = x->bendy_units[bendy_count].val1 = boundrand(0.01f, (float)BENDY_MAXDEL);
            params[pcount++] = x->bendy_units[bendy_count].val2 = boundrand(0.01f, (float)BENDY_MAXDEL);
            x->bendy_units[bendy_count].counter = 0;
            delset3(x->bendy_units[bendy_count].delayline1, x->bendy_units[bendy_count].dv1, x->bendy_units[bendy_count].val1, x->sr, BENDY_MAXDEL);
            delset3(x->bendy_units[bendy_count].delayline2, x->bendy_units[bendy_count].dv2, x->bendy_units[bendy_count].val2, x->sr, BENDY_MAXDEL);
            bendy_count = (bendy_count + 1) % MAX_DSP_UNITS;
        } else if (j == FLANGE) {
            params[pcount++] = FLANGE;
            params[pcount++] = boundrand(80.0f, 400.0f);
            params[pcount++] = boundrand(600.0f, 4000.0f);
            params[pcount++] = boundrand(0.1f, 2.0f);
            params[pcount++] = boundrand(0.1f, 0.95f);
            params[pcount++] = (double)flange_count;
            x->flange_units[flange_count].phase = boundrand(0.0f, 0.5f) * (double)x->sinelen / x->sr;
            flange_count = (flange_count + 1) % MAX_DSP_UNITS;
        } else if (j == BUTTER) {
            params[pcount++] = BUTTER;
            int ftype = rand() % 3;
            params[pcount++] = (double)ftype;
            double cf = boundrand(70.0f, 3000.0f);
            params[pcount++] = cf;
            double bw = cf * boundrand(0.05f, 0.6f);
            params[pcount++] = bw;
            params[pcount++] = (double)butterworth_count;
            if (ftype == LOPASS) {
                lobut(x->butterworth_units[butterworth_count].data1, cf, x->sr);
                lobut(x->butterworth_units[butterworth_count].data2, cf, x->sr);
            } else if (ftype == HIPASS) {
                hibut(x->butterworth_units[butterworth_count].data1, cf, x->sr);
                hibut(x->butterworth_units[butterworth_count].data2, cf, x->sr);
            } else if (ftype == BANDPASS) {
                bpbut(x->butterworth_units[butterworth_count].data1, cf, bw, x->sr);
                bpbut(x->butterworth_units[butterworth_count].data2, cf, bw, x->sr);
            }
            x->butterworth_units[butterworth_count].cf = cf;
            x->butterworth_units[butterworth_count].ftype = ftype;
            butterworth_count = (butterworth_count + 1) % MAX_DSP_UNITS;
        } else if (j == TRUNCATE) {
            params[pcount++] = TRUNCATE;
            params[pcount++] = (double)truncate_count;
            x->truncate_units[truncate_count].counter = 0;
            x->truncate_units[truncate_count].state = 0;
            x->truncate_units[truncate_count].segsamples = 1;
            truncate_count = (truncate_count + 1) % MAX_DSP_UNITS;
        } else if (j == SWEEPRESON) {
            params[pcount++] = SWEEPRESON;
            params[pcount++] = (double)sweepreson_count;
            params[pcount++] = x->sweepreson_units[sweepreson_count].minfreq = boundrand(100.0f, 300.0f);
            params[pcount++] = x->sweepreson_units[sweepreson_count].maxfreq = boundrand(600.0f, 6000.0f);
            params[pcount++] = x->sweepreson_units[sweepreson_count].bwfac = boundrand(0.01f, 0.2f);
            params[pcount++] = x->sweepreson_units[sweepreson_count].speed = boundrand(0.05f, 2.0f);
            params[pcount++] = x->sweepreson_units[sweepreson_count].phase = boundrand(0.0f, 0.5f);
            x->sweepreson_units[sweepreson_count].phase *= ((double)x->sinelen / x->sr);
            sweepreson_count = (sweepreson_count + 1) % MAX_DSP_UNITS;
        } else if (j == SLIDECOMB) {
            params[pcount++] = SLIDECOMB;
            params[pcount++] = (double)slidecomb_count;
            params[pcount++] = x->slidecomb_units[slidecomb_count].start_delay = boundrand(0.001f, (float)(MAX_SLIDECOMB_DELAY * 0.95));
            params[pcount++] = x->slidecomb_units[slidecomb_count].end_delay = boundrand(0.001f, (float)(MAX_SLIDECOMB_DELAY * 0.95));
            params[pcount++] = x->slidecomb_units[slidecomb_count].feedback = boundrand(0.7f, 0.99f);
            double slen = boundrand(0.1f, 2.0f);
            params[pcount++] = slen;
            x->slidecomb_units[slidecomb_count].sample_length = (long)(slen * x->sr);
            x->slidecomb_units[slidecomb_count].counter = 0;
            delset2(x->slidecomb_units[slidecomb_count].delayline1, x->slidecomb_units[slidecomb_count].dv1, MAX_SLIDECOMB_DELAY, x->sr);
            delset2(x->slidecomb_units[slidecomb_count].delayline2, x->slidecomb_units[slidecomb_count].dv2, MAX_SLIDECOMB_DELAY, x->sr);
            slidecomb_count = (slidecomb_count + 1) % MAX_DSP_UNITS;
        } else if (j == REVERB1) {
            params[pcount++] = REVERB1;
            params[pcount++] = (double)reverb1_count;
            double revtime = boundrand(0.25f, 0.99f);
            params[pcount++] = x->reverb1_units[reverb1_count].revtime = revtime;
            double raw_wet = boundrand(0.2f, 0.8f);
            params[pcount++] = raw_wet;
            x->reverb1_units[reverb1_count].wet = sin(1.570796 * raw_wet);
            x->reverb1_units[reverb1_count].dry = cos(1.570796 * raw_wet);
            for (int k = 0; k < 4; k++) {
                double d = boundrand(0.005f, 0.1f);
                params[pcount++] = x->reverb1_units[reverb1_count].dels[k] = d;
                mycombset(d, revtime, 0, x->reverb1_units[reverb1_count].alpo1[k], x->sr);
                mycombset(d, revtime, 0, x->reverb1_units[reverb1_count].alpo2[k], x->sr);
            }
            ellipset(x->reverb_ellipse_data, x->reverb1_units[reverb1_count].eel1, &x->reverb1_units[reverb1_count].nsects, &x->reverb1_units[reverb1_count].xnorm);
            ellipset(x->reverb_ellipse_data, x->reverb1_units[reverb1_count].eel2, &x->reverb1_units[reverb1_count].nsects, &x->reverb1_units[reverb1_count].xnorm);
            reverb1_count = (reverb1_count + 1) % MAX_DSP_UNITS;
        } else if (j == ELLIPSE) {
            params[pcount++] = ELLIPSE;
            params[pcount++] = (double)ellipseme_count;
            int fcode = rand() % ELLIPSE_FILTER_COUNT;
            params[pcount++] = (double)fcode;
            x->ellipseme_units[ellipseme_count].filtercode = fcode;
            ellipset(x->ellipse_data[fcode], x->ellipseme_units[ellipseme_count].eel1, &x->ellipseme_units[ellipseme_count].nsects, &x->ellipseme_units[ellipseme_count].xnorm);
            ellipset(x->ellipse_data[fcode], x->ellipseme_units[ellipseme_count].eel2, &x->ellipseme_units[ellipseme_count].nsects, &x->ellipseme_units[ellipseme_count].xnorm);
            ellipseme_count = (ellipseme_count + 1) % MAX_DSP_UNITS;
        } else if (j == FEED1) {
            params[pcount++] = FEED1;
            params[pcount++] = (double)feed1_count;
            double mindelay = boundrand(0.001f, 0.1f);
            params[pcount++] = x->feed1_units[feed1_count].mindelay = mindelay;
            double maxdelay = boundrand((float)mindelay, 0.1f);
            params[pcount++] = x->feed1_units[feed1_count].maxdelay = maxdelay;
            double speed1 = boundrand(0.01f, 0.5f);
            params[pcount++] = x->feed1_units[feed1_count].speed1 = speed1;
            double speed2 = boundrand((float)speed1, 0.5f);
            params[pcount++] = x->feed1_units[feed1_count].speed2 = speed2;
            double dur = boundrand(0.05f, 1.0f);
            params[pcount++] = x->feed1_units[feed1_count].duration = dur;

            double phz1 = 0.0, phz2 = 0.0;
            funcgen1(x->feed1_units[feed1_count].func1, FEEDFUNCLEN, dur, mindelay, maxdelay, speed1, speed2, 1.0, 1.0, &phz1, &phz2, x->sinewave, x->sinelen);
            phz1 = phz2 = 0.0;
            funcgen1(x->feed1_units[feed1_count].func2, FEEDFUNCLEN, dur, mindelay * 0.5, maxdelay * 2.0, speed1 * 1.25, speed2 * 0.75, 1.0, 1.0, &phz1, &phz2, x->sinewave, x->sinelen);
            phz1 = phz2 = 0.0;
            funcgen1(x->feed1_units[feed1_count].func3, FEEDFUNCLEN, dur, 0.1, 0.7, speed1 * 0.35, speed2 * 1.25, 1.0, 1.0, &phz1, &phz2, x->sinewave, x->sinelen);
            phz1 = phz2 = 0.0;
            funcgen1(x->feed1_units[feed1_count].func4, FEEDFUNCLEN, dur, 0.1, 0.7, speed1 * 0.55, speed2 * 2.25, 1.0, 1.0, &phz1, &phz2, x->sinewave, x->sinelen);

            delset2(x->feed1_units[feed1_count].delayLine1a, x->feed1_units[feed1_count].dv1a, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine2a, x->feed1_units[feed1_count].dv2a, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine1b, x->feed1_units[feed1_count].dv1b, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine2b, x->feed1_units[feed1_count].dv2b, MAX_MINI_DELAY, x->sr);
            feed1_count = (feed1_count + 1) % MAX_DSP_UNITS;
        } else if (j == BITCRUSH) {
            params[pcount++] = BITCRUSH;
            params[pcount++] = (double)bitcrush_count;
            params[pcount++] = x->bitcrush_factors[bitcrush_count] = boundrand(2.0f, 8.0f);
            bitcrush_count = (bitcrush_count + 1) % MAX_DSP_UNITS;
        } else if (j == FLAM1) {
            params[pcount++] = FLAM1;
            params[pcount++] = (double)flam1_count;
            double slen = boundrand(1.0f, 4.0f);
            params[pcount++] = x->flam1_units[flam1_count].sample_length = (long)(slen * x->sr);
            double dt = boundrand(0.08f, 0.2f);
            params[pcount++] = x->flam1_units[flam1_count].dt = dt;
            x->flam1_units[flam1_count].counter = 0;
            delset2(x->flam1_units[flam1_count].delayline1, x->flam1_units[flam1_count].dv1, MAX_MINI_DELAY, x->sr);
            delset2(x->flam1_units[flam1_count].delayline2, x->flam1_units[flam1_count].dv2, MAX_MINI_DELAY, x->sr);
            flam1_count = (flam1_count + 1) % MAX_DSP_UNITS;
        } else if (j == SLIDEFLAM) {
            params[pcount++] = SLIDEFLAM;
            params[pcount++] = (double)slideflam_count;
            double dt1, dt2;
            if (boundrand(0.0f, 1.0f) > 0.1f) {
                dt1 = boundrand(0.01f, 0.05f);
                dt2 = boundrand(0.1f, (float)(MAX_SLIDEFLAM_DELAY * 0.95));
            } else {
                dt1 = boundrand(0.1f, (float)(MAX_SLIDEFLAM_DELAY * 0.95));
                dt2 = boundrand(0.01f, 0.05f);
            }
            params[pcount++] = x->slideflam_units[slideflam_count].dt1 = dt1;
            params[pcount++] = x->slideflam_units[slideflam_count].dt2 = dt2;
            params[pcount++] = x->slideflam_units[slideflam_count].feedback = boundrand(0.7f, 0.99f);
            double slen = boundrand(0.25f, 4.0f);
            params[pcount++] = slen;
            x->slideflam_units[slideflam_count].sample_length = (long)(slen * x->sr);
            x->slideflam_units[slideflam_count].counter = 0;
            delset2(x->slideflam_units[slideflam_count].delayline1, x->slideflam_units[slideflam_count].dv1, MAX_SLIDEFLAM_DELAY, x->sr);
            delset2(x->slideflam_units[slideflam_count].delayline2, x->slideflam_units[slideflam_count].dv2, MAX_SLIDEFLAM_DELAY, x->sr);
            slideflam_count = (slideflam_count + 1) % MAX_DSP_UNITS;
        } else if (j == COMB4) {
            params[pcount++] = COMB4;
            params[pcount++] = (double)comb4_count;
            double rvt = 0.99;
            double basefreq = boundrand(100.0f, 400.0f);
            params[pcount++] = basefreq;
            for (int k = 0; k < 4; k++) {
                int dex = (int)floor(boundrand(0.0f, 4.0f));
                if (dex > 4) dex = 4;
                double lpt = 1.0 / basefreq;
                basefreq *= x->ratios[dex];
                params[pcount++] = lpt;
                mycombset(lpt, rvt, 0, x->comb4_units[comb4_count].combs1[k], x->sr);
                mycombset(lpt, rvt, 0, x->comb4_units[comb4_count].combs2[k], x->sr);
            }
            comb4_count = (comb4_count + 1) % MAX_DSP_UNITS;
        } else if (j == COMPDIST) {
            params[pcount++] = COMPDIST;
        } else if (j == RINGFEED) {
            params[pcount++] = RINGFEED;
            params[pcount++] = (double)ringfeed_count;
            double cf = boundrand(90.0f, 1500.0f);
            params[pcount++] = cf;
            double bw = boundrand(0.02f, 0.4f) * cf;
            params[pcount++] = bw;
            rsnset2(cf, bw, RESON_NO_SCL, 0.0, x->resonfeed_units[ringfeed_count].res1q, x->sr);
            rsnset2(cf, bw, RESON_NO_SCL, 0.0, x->resonfeed_units[ringfeed_count].res2q, x->sr);
            x->resonfeed_units[ringfeed_count].osc1phs = 0.0;
            x->resonfeed_units[ringfeed_count].osc2phs = 0.0;
            double si1 = boundrand(90.0f, 1500.0f) * ((double)x->sinelen / x->sr);
            double si2 = boundrand(90.0f, 1500.0f) * ((double)x->sinelen / x->sr);
            params[pcount++] = x->resonfeed_units[ringfeed_count].osc1si = si1;
            params[pcount++] = x->resonfeed_units[ringfeed_count].osc2si = si2;
            double lpt = 1.0 / boundrand(90.0f, 1500.0f);
            double rvt = boundrand(0.01f, 0.8f);
            params[pcount++] = lpt;
            params[pcount++] = rvt;
            mycombset(lpt, rvt, 0, x->resonfeed_units[ringfeed_count].comb1arr, x->sr);
            mycombset(lpt, rvt, 0, x->resonfeed_units[ringfeed_count].comb2arr, x->sr);
            ringfeed_count = (ringfeed_count + 1) % MAX_DSP_UNITS;
        } else if (j == RESONADSR) {
            params[pcount++] = RESONADSR;
            params[pcount++] = (double)resonadsr_count;
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->a = boundrand(0.01f, 0.1f);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->d = boundrand(0.01f, 0.05f);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->r = boundrand(0.05f, 0.5f);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v1 = boundrand(150.0f, 4000.0f);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v2 = boundrand(150.0f, 4000.0f);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v3 = boundrand(150.0f, 4000.0f);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v4 = boundrand(150.0f, 4000.0f);
            params[pcount++] = x->resonadsr_units[resonadsr_count].bwfac = boundrand(0.03f, 0.7f);
            double notedur = boundrand(0.7f, 1.2f);
            params[pcount++] = notedur;
            x->resonadsr_units[resonadsr_count].phs = 0.0;
            double sust = notedur - (x->resonadsr_units[resonadsr_count].adsr->a + x->resonadsr_units[resonadsr_count].adsr->d + x->resonadsr_units[resonadsr_count].adsr->r);
            x->resonadsr_units[resonadsr_count].adsr->s = (sust > 0.0) ? sust : 0.01;
            buildadsr(x->resonadsr_units[resonadsr_count].adsr);
            x->resonadsr_units[resonadsr_count].si = ((double)x->resonadsr_units[resonadsr_count].adsr->len / x->sr) / notedur;
            resonadsr_count = (resonadsr_count + 1) % MAX_DSP_UNITS;
        } else if (j == STV) {
            params[pcount++] = STV;
            params[pcount++] = (double)stv_count;
            double speed1 = boundrand(0.025f, 0.5f);
            double speed2 = boundrand(0.025f, 0.5f);
            double maxdel = boundrand(0.001f, 0.01f);
            params[pcount++] = speed1;
            params[pcount++] = speed2;
            params[pcount++] = maxdel;
            x->stv_units[stv_count].osc1phs = 0.0;
            x->stv_units[stv_count].osc2phs = 0.0;
            x->stv_units[stv_count].fac2 = 0.5 * (maxdel - 0.001);
            x->stv_units[stv_count].fac1 = 0.001 + x->stv_units[stv_count].fac2;
            x->stv_units[stv_count].osc1si = ((double)x->sinelen / x->sr) * speed1;
            x->stv_units[stv_count].osc2si = ((double)x->sinelen / x->sr) * speed2;
            delset2(x->stv_units[stv_count].delayline1, x->stv_units[stv_count].dv1, maxdel, x->sr);
            delset2(x->stv_units[stv_count].delayline2, x->stv_units[stv_count].dv2, maxdel, x->sr);
            stv_count = (stv_count + 1) % MAX_DSP_UNITS;
        }
    }
    x->pcount = pcount;
}

void chameleon_dsp64(t_chameleon *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;
    x->vs = (maxvectorsize > 0 && maxvectorsize <= MAX_VEC) ? maxvectorsize : MAX_VEC;

    if (x->sr != (float)samplerate) {
        x->sr = (float)samplerate;
    }

    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)chameleon_perform64, 0, NULL);
}

void chameleon_assist(t_chameleon *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Channel 1 Input", 256); break;
            case 1: strncpy_zero(dst, "(signal) Channel 2 Input", 256); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Channel 1 Output", 256); break;
            case 1: strncpy_zero(dst, "(signal) Channel 2 Output", 256); break;
            case 2: strncpy_zero(dst, "(list) Data Bank of Stored Settings", 256); break;
        }
    }
}
