/**
 * @file adsr~.c
 * @brief Click-triggered ADSR envelope generator for Max 8/9.
 * Modernized for 64-bit dsp64, division-by-zero protection, and thread safety.
 */

#include "MSPd.h"

#define OBJECT_NAME "adsr~"

static t_class *adsr_class;

typedef struct _adsr {
    t_pxobject x_obj;

    // Stage times in seconds
    double a;
    double d;
    double s;
    double r;

    // Sample boundaries
    long asamps;
    long dsamps;
    long ssamps;
    long rsamps;
    long tsamps;

    long ebreak1;
    long ebreak2;
    long ebreak3;

    // Reciprocal step scalers (eliminates division in perform64)
    double inv_asamps;
    double inv_dsamps;
    double inv_ssamps;
    double inv_rsamps;

    // Target gains
    double egain1;
    double egain2;

    long counter;
    double srate;
    double click_gain;
    short mute;
} t_adsr;

/* Prototypes */
void *adsr_new(t_symbol *s, long argc, t_atom *argv);
void adsr_dsp64(t_adsr *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void adsr_perform64(t_adsr *x, t_object *dsp64, double **ins, 
                    long numins, double **outs, long numouts, long vectorsize,
                    long flags, void *userparam);
void adsr_assist(t_adsr *x, void *b, long m, long a, char *s);
void adsr_bang(t_adsr *x);
void adsr_list(t_adsr *x, t_symbol *msg, long argc, t_atom *argv);
void adsr_mute(t_adsr *x, double f);
void adsr_set_a(t_adsr *x, double f);
void adsr_set_d(t_adsr *x, double f);
void adsr_set_s(t_adsr *x, double f);
void adsr_set_r(t_adsr *x, double f);
void adsr_set_gain1(t_adsr *x, double f);
void adsr_set_gain2(t_adsr *x, double f);
void adsr_recalc(t_adsr *x);

/* Attribute Accessors */
t_max_err adsr_a_get(t_adsr *x, void *attr, long *ac, t_atom **av);
t_max_err adsr_a_set(t_adsr *x, void *attr, long ac, t_atom *av);
t_max_err adsr_d_get(t_adsr *x, void *attr, long *ac, t_atom **av);
t_max_err adsr_d_set(t_adsr *x, void *attr, long ac, t_atom *av);
t_max_err adsr_s_get(t_adsr *x, void *attr, long *ac, t_atom **av);
t_max_err adsr_s_set(t_adsr *x, void *attr, long ac, t_atom *av);
t_max_err adsr_r_get(t_adsr *x, void *attr, long *ac, t_atom **av);
t_max_err adsr_r_set(t_adsr *x, void *attr, long ac, t_atom *av);

int C74_EXPORT main(void)
{
    t_class *c = class_new("el.adsr~", 
                           (method)adsr_new, 
                           (method)dsp_free, 
                           (long)sizeof(t_adsr), 
                           0L, 
                           A_GIMME, 
                           0);

    class_addmethod(c, (method)adsr_dsp64,     "dsp64",  A_CANT, 0);
    class_addmethod(c, (method)adsr_assist,    "assist", A_CANT, 0);
    class_addmethod(c, (method)adsr_bang,      "bang",   0);
    class_addmethod(c, (method)adsr_list,      "list",   A_GIMME, 0);
    class_addmethod(c, (method)adsr_mute,      "mute",   A_FLOAT, 0);
    class_addmethod(c, (method)adsr_set_gain2, "ft1",    A_FLOAT, 0);
    class_addmethod(c, (method)adsr_set_gain1, "ft2",    A_FLOAT, 0);
    class_addmethod(c, (method)adsr_set_r,     "ft3",    A_FLOAT, 0);
    class_addmethod(c, (method)adsr_set_s,     "ft4",    A_FLOAT, 0);
    class_addmethod(c, (method)adsr_set_d,     "ft5",    A_FLOAT, 0);
    class_addmethod(c, (method)adsr_set_a,     "ft6",    A_FLOAT, 0);

    CLASS_ATTR_DOUBLE(c, "attack", 0, t_adsr, a);
    CLASS_ATTR_DEFAULT_SAVE(c, "attack", 0, "20");
    CLASS_ATTR_ACCESSORS(c, "attack", (method)adsr_a_get, (method)adsr_a_set);
    CLASS_ATTR_LABEL(c, "attack", 0, "Attack Time (ms)");

    CLASS_ATTR_DOUBLE(c, "decay", 0, t_adsr, d);
    CLASS_ATTR_DEFAULT_SAVE(c, "decay", 0, "50");
    CLASS_ATTR_ACCESSORS(c, "decay", (method)adsr_d_get, (method)adsr_d_set);
    CLASS_ATTR_LABEL(c, "decay", 0, "Decay Time (ms)");

    CLASS_ATTR_DOUBLE(c, "sustain", 0, t_adsr, s);
    CLASS_ATTR_DEFAULT_SAVE(c, "sustain", 0, "150");
    CLASS_ATTR_ACCESSORS(c, "sustain", (method)adsr_s_get, (method)adsr_s_set);
    CLASS_ATTR_LABEL(c, "sustain", 0, "Sustain Time (ms)");

    CLASS_ATTR_DOUBLE(c, "release", 0, t_adsr, r);
    CLASS_ATTR_DEFAULT_SAVE(c, "release", 0, "200");
    CLASS_ATTR_ACCESSORS(c, "release", (method)adsr_r_get, (method)adsr_r_set);
    CLASS_ATTR_LABEL(c, "release", 0, "Release Time (ms)");

    CLASS_ATTR_DOUBLE(c, "gain1", 0, t_adsr, egain1);
    CLASS_ATTR_DEFAULT_SAVE(c, "gain1", 0, "1.0");
    CLASS_ATTR_LABEL(c, "gain1", 0, "Gain 1 (Decay Level)");

    CLASS_ATTR_DOUBLE(c, "gain2", 0, t_adsr, egain2);
    CLASS_ATTR_DEFAULT_SAVE(c, "gain2", 0, "0.7");
    CLASS_ATTR_LABEL(c, "gain2", 0, "Gain 2 (Sustain Level)");

    CLASS_ATTR_ORDER(c, "attack",  0, "1");
    CLASS_ATTR_ORDER(c, "decay",   0, "2");
    CLASS_ATTR_ORDER(c, "sustain", 0, "3");
    CLASS_ATTR_ORDER(c, "release", 0, "4");
    CLASS_ATTR_ORDER(c, "gain1",   0, "5");
    CLASS_ATTR_ORDER(c, "gain2",   0, "6");

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    adsr_class = c;

    potpourri_announce(OBJECT_NAME);
    //post("updated version\n");
    return 0;
}

void adsr_recalc(t_adsr *x)
{
    if (x->srate <= 0.0) x->srate = 44100.0;

    x->asamps = (long)(x->a * x->srate);
    x->dsamps = (long)(x->d * x->srate);
    x->ssamps = (long)(x->s * x->srate);
    x->rsamps = (long)(x->r * x->srate);

    // Guard against zero-division in perform loop
    x->inv_asamps = (x->asamps > 0) ? (1.0 / (double)x->asamps) : 0.0;
    x->inv_dsamps = (x->dsamps > 0) ? (1.0 / (double)x->dsamps) : 0.0;
    x->inv_ssamps = (x->ssamps > 0) ? (1.0 / (double)x->ssamps) : 0.0;
    x->inv_rsamps = (x->rsamps > 0) ? (1.0 / (double)x->rsamps) : 0.0;

    x->ebreak1 = x->asamps;
    x->ebreak2 = x->asamps + x->dsamps;
    x->ebreak3 = x->asamps + x->dsamps + x->ssamps;
    x->tsamps  = x->ebreak3 + x->rsamps;
}

t_max_err adsr_a_get(t_adsr *x, void *attr, long *ac, t_atom **av)
{
    if (ac && av) {
        char alloc;
        if (atom_alloc(ac, av, &alloc)) return MAX_ERR_GENERIC;
        atom_setfloat(*av, x->a * 1000.0);
    }
    return MAX_ERR_NONE;
}

t_max_err adsr_a_set(t_adsr *x, void *attr, long ac, t_atom *av)
{
    if (ac && av) adsr_set_a(x, atom_getfloat(av));
    return MAX_ERR_NONE;
}

t_max_err adsr_d_get(t_adsr *x, void *attr, long *ac, t_atom **av)
{
    if (ac && av) {
        char alloc;
        if (atom_alloc(ac, av, &alloc)) return MAX_ERR_GENERIC;
        atom_setfloat(*av, x->d * 1000.0);
    }
    return MAX_ERR_NONE;
}

t_max_err adsr_d_set(t_adsr *x, void *attr, long ac, t_atom *av)
{
    if (ac && av) adsr_set_d(x, atom_getfloat(av));
    return MAX_ERR_NONE;
}

t_max_err adsr_s_get(t_adsr *x, void *attr, long *ac, t_atom **av)
{
    if (ac && av) {
        char alloc;
        if (atom_alloc(ac, av, &alloc)) return MAX_ERR_GENERIC;
        atom_setfloat(*av, x->s * 1000.0);
    }
    return MAX_ERR_NONE;
}

t_max_err adsr_s_set(t_adsr *x, void *attr, long ac, t_atom *av)
{
    if (ac && av) adsr_set_s(x, atom_getfloat(av));
    return MAX_ERR_NONE;
}

t_max_err adsr_r_get(t_adsr *x, void *attr, long *ac, t_atom **av)
{
    if (ac && av) {
        char alloc;
        if (atom_alloc(ac, av, &alloc)) return MAX_ERR_GENERIC;
        atom_setfloat(*av, x->r * 1000.0);
    }
    return MAX_ERR_NONE;
}

t_max_err adsr_r_set(t_adsr *x, void *attr, long ac, t_atom *av)
{
    if (ac && av) adsr_set_r(x, atom_getfloat(av));
    return MAX_ERR_NONE;
}

void adsr_assist(t_adsr *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        switch (arg) {
            case 0: strncpy_zero(dst, "(signal) Click Trigger / (bang) Trigger", 256); break;
            case 1: strncpy_zero(dst, "(float) Attack Time (ms)", 256); break;
            case 2: strncpy_zero(dst, "(float) Decay Time (ms)", 256); break;
            case 3: strncpy_zero(dst, "(float) Sustain Time (ms)", 256); break;
            case 4: strncpy_zero(dst, "(float) Release Time (ms)", 256); break;
            case 5: strncpy_zero(dst, "(float) Gain 1 (Decay Level)", 256); break;
            case 6: strncpy_zero(dst, "(float) Gain 2 (Sustain Level)", 256); break;
        }
    } else if (msg == ASSIST_OUTLET) {
        strncpy_zero(dst, "(signal) Envelope Output", 256);
    }
}

void adsr_mute(t_adsr *x, double f)        { x->mute = (short)f; }
void adsr_set_gain1(t_adsr *x, double f)   { x->egain1 = f; }
void adsr_set_gain2(t_adsr *x, double f)   { x->egain2 = f; }

void adsr_bang(t_adsr *x)
{
    x->counter = 0;
    x->click_gain = 1.0;
}

void adsr_set_a(t_adsr *x, double f)
{
    if (f < 0.0) f = 0.0;
    x->a = f * 0.001;
    adsr_recalc(x);
}

void adsr_set_d(t_adsr *x, double f)
{
    if (f < 0.0) f = 0.0;
    x->d = f * 0.001;
    adsr_recalc(x);
}

void adsr_set_s(t_adsr *x, double f)
{
    if (f < 0.0) f = 0.0;
    x->s = f * 0.001;
    adsr_recalc(x);
}

void adsr_set_r(t_adsr *x, double f)
{
    if (f < 0.0) f = 0.0;
    x->r = f * 0.001;
    adsr_recalc(x);
}

void adsr_list(t_adsr *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (argc < 4) {
        object_error((t_object *)x, "%s: list requires 4 values: attack decay sustain release (ms)", OBJECT_NAME);
        return;
    }

    double a = atom_getfloat(argv);
    double d = atom_getfloat(argv + 1);
    double s = atom_getfloat(argv + 2);
    double r = atom_getfloat(argv + 3);

    x->a = (a > 0.0 ? a : 0.0) * 0.001;
    x->d = (d > 0.0 ? d : 0.0) * 0.001;
    x->s = (s > 0.0 ? s : 0.0) * 0.001;
    x->r = (r > 0.0 ? r : 0.0) * 0.001;

    adsr_recalc(x);
}

void *adsr_new(t_symbol *s, long argc, t_atom *argv)
{
    t_adsr *x = (t_adsr *)object_alloc(adsr_class);
    if (!x) return NULL;

    dsp_setup((t_pxobject *)x, 1);       // 1 Signal inlet (trigger)
    outlet_new((t_object *)x, "signal"); // 1 Signal output

    // Create 6 message inlets right-to-left
    floatin((t_object *)x, 1); // Inlet 6: Gain 2
    floatin((t_object *)x, 2); // Inlet 5: Gain 1
    floatin((t_object *)x, 3); // Inlet 4: Release
    floatin((t_object *)x, 4); // Inlet 3: Sustain
    floatin((t_object *)x, 5); // Inlet 2: Decay
    floatin((t_object *)x, 6); // Inlet 1: Attack

    x->srate = sys_getsr();
    if (x->srate <= 0.0) x->srate = 44100.0;

    // Defaults in ms
    double a_ms = 10.0;
    double d_ms = 50.0;
    double s_ms = 100.0;
    double r_ms = 100.0;
    x->egain1 = 0.7;
    x->egain2 = 0.1;

    if (argc > 0) a_ms = atom_getfloat(argv);
    if (argc > 1) d_ms = atom_getfloat(argv + 1);
    if (argc > 2) s_ms = atom_getfloat(argv + 2);
    if (argc > 3) r_ms = atom_getfloat(argv + 3);
    if (argc > 4) x->egain1 = atom_getfloat(argv + 4);
    if (argc > 5) x->egain2 = atom_getfloat(argv + 5);

    x->a = (a_ms > 0.0 ? a_ms : 0.0) * 0.001;
    x->d = (d_ms > 0.0 ? d_ms : 0.0) * 0.001;
    x->s = (s_ms > 0.0 ? s_ms : 0.0) * 0.001;
    x->r = (r_ms > 0.0 ? r_ms : 0.0) * 0.001;

    x->counter = 0;
    x->click_gain = 0.0;
    x->mute = 0;

    adsr_recalc(x);
    // Initialize counter past duration so envelope is idle until triggered
    x->counter = x->tsamps + 1;

    return x;
}

void adsr_perform64(t_adsr *x, t_object *dsp64, double **ins, 
                    long numins, double **outs, long numouts, long vectorsize,
                    long flags, void *userparam)
{
    t_double *in = ins[0];
    t_double *out = outs[0];
    long n = vectorsize;

    if (x->mute) {
        while (n--) *out++ = 0.0;
        return; // Guard prevents falling through into second loop
    }

    long counter = x->counter;
    long tsamps = x->tsamps;
    long ebreak1 = x->ebreak1;
    long ebreak2 = x->ebreak2;
    long ebreak3 = x->ebreak3;

    double inv_a = x->inv_asamps;
    double inv_d = x->inv_dsamps;
    double inv_s = x->inv_ssamps;
    double inv_r = x->inv_rsamps;

    double egain1 = x->egain1;
    double egain2 = x->egain2;
    double click_gain = x->click_gain;

    while (n--) {
        double input_val = *in++;
        if (input_val != 0.0) {
            click_gain = input_val;
            counter = 0;
        }

        double env_val = 0.0;

        if (counter < ebreak1) {
            env_val = (inv_a > 0.0) ? ((double)counter * inv_a) : 1.0;
        } else if (counter < ebreak2) {
            double etmp = (inv_d > 0.0) ? ((double)(counter - ebreak1) * inv_d) : 1.0;
            env_val = (1.0 - etmp) + (egain1 * etmp);
        } else if (counter < ebreak3) {
            double etmp = (inv_s > 0.0) ? ((double)(counter - ebreak2) * inv_s) : 1.0;
            env_val = (egain1 * (1.0 - etmp)) + (egain2 * etmp);
        } else if (counter < tsamps) {
            double etmp = (inv_r > 0.0) ? ((double)(tsamps - counter) * inv_r) : 0.0;
            env_val = etmp * egain2;
        } else {
            env_val = 0.0;
        }

        if (click_gain != 0.0 && click_gain != 1.0) {
            env_val *= click_gain;
        }

        *out++ = env_val;

        if (counter <= tsamps) {
            counter++;
        }
    }

    x->counter = counter;
    x->click_gain = click_gain;
}

void adsr_dsp64(t_adsr *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if (samplerate <= 0.0) return;

    if (x->srate != samplerate) {
        x->srate = samplerate;
        adsr_recalc(x);
    }
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)adsr_perform64, 0, NULL);
}
