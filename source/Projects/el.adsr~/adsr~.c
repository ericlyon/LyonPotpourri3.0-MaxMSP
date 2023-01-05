#include "MSPd.h"
/* internal metronome now redundant so disabled */
// build for Max 6 - now removing all Pd references


static t_class *adsr_class;

#define OBJECT_NAME "adsr~"

typedef struct _adsr
{
	t_pxobject x_obj;
	// Variables Here
	double a;
	double d;
	double s;
	double r;
	int ebreak1;
	int ebreak2;
	int ebreak3;
	int asamps;
	int dsamps;
	int ssamps;
	int rsamps;
	int asamps_last;
	int dsamps_last;
	int ssamps_last;
	int rsamps_last;
	double tempo;
	double egain1;
	double egain2;
	int tempomode;
	int beat_subdiv;
	int tsamps;
	int counter;
	double srate;
	short manual_override;
	double click_gain; // input click sets volume too
	short mute;
} t_adsr;

void *adsr_new(t_symbol *s, int argc, t_atom *argv);
void adsr_perform64(t_adsr *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,
                    long numouts, long vectorsize,
                    long flags, void *userparam);

//t_int *adsr_perform(t_int *w);
void adsr_dsp(t_adsr *x, t_signal **sp, short *count);
void adsr_dsp64(t_adsr *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void adsr_assist(t_adsr *x, void *b, long m, long a, char *s);
void adsr_bang(t_adsr *x);
void adsr_manual_override(t_adsr *x, t_floatarg toggle);
void adsr_list (t_adsr *x, t_atom *msg, short argc, t_atom *argv);
void adsr_tempomode(t_adsr *x, t_atom *msg, short argc, t_atom *argv);
void adsr_set_a(t_adsr *x, t_floatarg f);
void adsr_set_d(t_adsr *x, t_floatarg f);
void adsr_set_s(t_adsr *x, t_floatarg f);
void adsr_set_r(t_adsr *x, t_floatarg f);
void adsr_set_gain1(t_adsr *x, t_floatarg f);
void adsr_set_gain2(t_adsr *x, t_floatarg f);
void set_tempo(t_adsr *x, t_floatarg f);
void adsr_mute(t_adsr *x, t_floatarg f);

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
	t_class *c;
	c = class_new("el.adsr~", (method)adsr_new, (method)dsp_free, sizeof(t_adsr), 0,A_GIMME, 0);
	
	class_addmethod(c, (method)adsr_assist,"assist",A_CANT,0);
	class_addmethod(c, (method)adsr_list, "list", A_GIMME, 0);
	class_addmethod(c, (method)adsr_mute, "mute", A_FLOAT, 0);
	class_addmethod(c, (method)adsr_set_gain2, "ft1", A_FLOAT, 0);
	class_addmethod(c, (method)adsr_set_gain1, "ft2", A_FLOAT, 0);
	class_addmethod(c, (method)adsr_set_r, "ft3", A_FLOAT, 0);
	class_addmethod(c, (method)adsr_set_s, "ft4", A_FLOAT, 0);
	class_addmethod(c, (method)adsr_set_d, "ft5", A_FLOAT, 0);
	class_addmethod(c, (method)adsr_set_a, "ft6", A_FLOAT, 0);
	class_addmethod(c, (method)adsr_bang, "bang", 0);
    class_addmethod(c, (method)adsr_dsp64, "dsp64", A_CANT,0);

		
	CLASS_ATTR_FLOAT(c, "attack", 0, t_adsr, a);
	CLASS_ATTR_DEFAULT_SAVE(c, "attack", 0, "20");
	CLASS_ATTR_ACCESSORS(c, "attack", (method)adsr_a_get, (method)adsr_a_set);
	CLASS_ATTR_LABEL(c, "attack", 0, "Attack Time");

	CLASS_ATTR_FLOAT(c, "decay", 0, t_adsr, d);
	CLASS_ATTR_DEFAULT_SAVE(c, "decay", 0, "50");
	CLASS_ATTR_ACCESSORS(c, "decay", (method)adsr_d_get, (method)adsr_d_set);
	CLASS_ATTR_LABEL(c, "decay", 0, "Decay Time");
	
	CLASS_ATTR_FLOAT(c, "sustain", 0, t_adsr, s); 
	CLASS_ATTR_DEFAULT_SAVE(c, "sustain", 0, "150");
	CLASS_ATTR_ACCESSORS(c, "sustain", (method)adsr_s_get, (method)adsr_s_set);
	CLASS_ATTR_LABEL(c, "sustain", 0, "Sustain Time");
	
	CLASS_ATTR_FLOAT(c, "release", 0, t_adsr, r);
	CLASS_ATTR_DEFAULT_SAVE(c, "release", 0, "200");
	CLASS_ATTR_ACCESSORS(c, "release", (method)adsr_r_get, (method)adsr_r_set);
	CLASS_ATTR_LABEL(c, "release", 0, "Release Time");

	CLASS_ATTR_FLOAT(c, "gain1", 0, t_adsr, egain1);
	CLASS_ATTR_DEFAULT_SAVE(c, "gain1", 0, "1.0");
	CLASS_ATTR_LABEL(c, "gain1", 0, "Gain 1");
	
	CLASS_ATTR_FLOAT(c, "gain2", 0, t_adsr, egain2);
	CLASS_ATTR_DEFAULT_SAVE(c, "gain2", 0, "0.7");
	CLASS_ATTR_LABEL(c, "gain2", 0, "Gain 2");

	CLASS_ATTR_ORDER(c, "attack",    0, "1");
	CLASS_ATTR_ORDER(c, "decay",    0, "2");
	CLASS_ATTR_ORDER(c, "sustain",    0, "3");
	CLASS_ATTR_ORDER(c, "release",    0, "4");
	CLASS_ATTR_ORDER(c, "gain1",    0, "5");
	CLASS_ATTR_ORDER(c, "gain2",    0, "6"); 
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	adsr_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}


t_max_err adsr_a_get(t_adsr *x, void *attr, long *ac, t_atom **av)
{
	if (ac && av) {
		char alloc;
		
		if (atom_alloc(ac, av, &alloc)) {
			return MAX_ERR_GENERIC;
		}
		atom_setfloat(*av, x->a * 1000.0);
	}
	return MAX_ERR_NONE;
}

t_max_err adsr_a_set(t_adsr *x, void *attr, long ac, t_atom *av)
{
	if (ac && av) {
		float a = atom_getfloat(av);
		adsr_set_a(x, a);
	}
	return MAX_ERR_NONE;
}


t_max_err adsr_d_get(t_adsr *x, void *attr, long *ac, t_atom **av)
{
	if (ac && av) {
		char alloc;
		
		if (atom_alloc(ac, av, &alloc)) {
			return MAX_ERR_GENERIC;
		}
		atom_setfloat(*av, x->d * 1000.0);
	}
	return MAX_ERR_NONE;
}

t_max_err adsr_d_set(t_adsr *x, void *attr, long ac, t_atom *av)
{
	if (ac && av) {
		float d = atom_getfloat(av);
		adsr_set_d(x, d);
	}
	return MAX_ERR_NONE;
}

t_max_err adsr_s_get(t_adsr *x, void *attr, long *ac, t_atom **av)
{
	if (ac && av) {
		char alloc;
		
		if (atom_alloc(ac, av, &alloc)) {
			return MAX_ERR_GENERIC;
		}
		atom_setfloat(*av, x->s * 1000.0);
	}
	return MAX_ERR_NONE;
}

t_max_err adsr_s_set(t_adsr *x, void *attr, long ac, t_atom *av)
{
	if (ac && av) {
		float s = atom_getfloat(av);
		adsr_set_s(x, s);
	}
	return MAX_ERR_NONE;
}

t_max_err adsr_r_get(t_adsr *x, void *attr, long *ac, t_atom **av)
{
	if (ac && av) {
		char alloc;
		
		if (atom_alloc(ac, av, &alloc)) {
			return MAX_ERR_GENERIC;
		}
		atom_setfloat(*av, x->r * 1000.0);
	}
	return MAX_ERR_NONE;
}

t_max_err adsr_r_set(t_adsr *x, void *attr, long ac, t_atom *av)
{
	if (ac && av) {
		float r = atom_getfloat(av);
		adsr_set_r(x, r);
	}
	return MAX_ERR_NONE;
}



void adsr_assist (t_adsr *x, void *b, long msg, long arg, char *dst)
{
	
	if (msg==1) {
		switch (arg) {
			case 0:
				sprintf(dst,"(float) Attack / (bang) Trigger");
				break;
			case 1:
				sprintf(dst,"(float) Decay");
				break;
			case 2:
				sprintf(dst,"(float) Sustain");
				break;
			case 3:
				sprintf(dst,"(float) Release");
				break;
			case 4:
				sprintf(dst,"(float) Gain1");
				break;
			case 5:
				sprintf(dst,"(float) Gain2");
				break;
			case 6:
				sprintf(dst,"(float) Tempo");
				break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) ADSR Output");
	}
}


void adsr_mute(t_adsr *x, t_floatarg f)
{
    x->mute = (short)f;
}

void adsr_set_gain1(t_adsr *x, t_floatarg f)
{
//	post("setting g1");

	x->egain1 = f;
	return;
}

void adsr_set_gain2(t_adsr *x, t_floatarg f)
{
//	post("setting g2");

	x->egain2 = f;
	return;
}
void adsr_bang(t_adsr *x) {
	x->counter = 0;
	return;
}
void adsr_set_a(t_adsr *x, t_floatarg f)
{
//	post("setting a");
	f /= 1000.0;
	
	x->a = f;
	x->asamps = x->a * x->srate;
	
	if( x->tempomode) {
		x->rsamps = x->tsamps - (x->asamps+x->dsamps+x->ssamps);	
		if( x->rsamps < 0 ) {
			x->rsamps = 0;
		}
	} else {	
		x->tsamps = x->asamps+x->dsamps+x->ssamps+x->rsamps;
	}
	x->ebreak1 = x->asamps;
	x->ebreak2 = x->asamps+x->dsamps;
	x->ebreak3 = x->asamps+x->dsamps+x->ssamps;
	return ;
}

void adsr_set_d(t_adsr *x, t_floatarg f)
{
//	post("setting d");

	f /= 1000.0 ;
	
	x->d = f;
	x->dsamps = x->d * x->srate;
	
	if( x->tempomode) {
		x->rsamps = x->tsamps - (x->asamps+x->dsamps+x->ssamps);	
		if( x->rsamps < 0 ) {
			x->rsamps = 0;
		}
	} else {	
		x->tsamps = x->asamps+x->dsamps+x->ssamps+x->rsamps;
	}
	x->ebreak2 = x->asamps+x->dsamps;
	x->ebreak3 = x->asamps+x->dsamps+x->ssamps;
	return ;
}

void adsr_set_s(t_adsr *x, t_floatarg f)
{
//	post("setting s");

	f /= 1000.0;
	
	x->s = f;
	x->ssamps = x->s * x->srate;
	
	if( x->tempomode) {
		x->rsamps = x->tsamps - (x->asamps+x->dsamps+x->ssamps);	
		if( x->rsamps < 0 ) {
			x->rsamps = 0;
		}
	} else {	
		x->tsamps = x->asamps+x->dsamps+x->ssamps+x->rsamps;
	}
	
	x->ebreak3 = x->asamps+x->dsamps+x->ssamps;
	return ;
}

void adsr_set_r(t_adsr *x, t_floatarg f)
{
//	post("setting r");
	
	f /= 1000.0;
	
	if( x->tempomode) {
		return;
	} else {	
		x->r = f;
		x->rsamps = x->r * x->srate;		
		x->tsamps = x->asamps+x->dsamps+x->ssamps+x->rsamps;
	}
	
	return ;
}

void adsr_list (t_adsr *x, t_atom *msg, short argc, t_atom *argv)
{

	
	x->rsamps = x->tsamps - (x->asamps+x->dsamps+x->ssamps);	
	if( x->rsamps < 0 ) 
		x->rsamps = 0;
	
	x->a = (atom_getfloatarg(0,argc,argv)) * .001;
	x->d = (atom_getfloatarg(1,argc,argv)) * .001;
	x->s = (atom_getfloatarg(2,argc,argv)) * .001;
	x->r = (atom_getfloatarg(3,argc,argv)) * .001;
	
	x->asamps = x->a * x->srate;
	x->dsamps = x->d * x->srate;
	x->ssamps = x->s * x->srate;
	x->rsamps = x->r * x->srate;	
	
    x->tsamps = x->asamps+x->dsamps+x->ssamps+x->rsamps;
    x->ebreak1 = x->asamps;
    x->ebreak2 = x->asamps+x->dsamps;
    x->ebreak3 = x->asamps+x->dsamps+x->ssamps;
	
}

void *adsr_new(t_symbol *s, int argc, t_atom *argv)
{
	t_adsr *x = (t_adsr *)object_alloc(adsr_class);
	dsp_setup((t_pxobject *)x,1); // no inputs
	outlet_new((t_object *)x, "signal");
	x->x_obj.z_misc |= Z_NO_INPLACE;
	floatin((t_object *)x, 1);
	floatin((t_object *)x, 2);
	floatin((t_object *)x, 3);
	floatin((t_object *)x, 4);
	floatin((t_object *)x, 5);
	floatin((t_object *)x, 6);
	
	x->srate = sys_getsr();
	if(!x->srate){
		error("zero sampling rate, setting to 44100");
		x->srate = 44100;
	}
	
	x->a = 10;
	x->d = 50;
	x->s = 100;
	x->r = 100;
	atom_arg_getdouble(&x->a,0,argc,argv);
	atom_arg_getdouble(&x->d,1,argc,argv);
	atom_arg_getdouble(&x->s,2,argc,argv);
	atom_arg_getdouble(&x->r,3,argc,argv);
	atom_arg_getdouble(&x->egain1,4,argc,argv);
	atom_arg_getdouble(&x->egain2,5,argc,argv);
	x->a *= .001;
	x->d *= .001;
	x->s *= .001;
	x->r *= .001;
	
	x->asamps = x->a * x->srate;
	x->dsamps = x->d * x->srate;
	x->ssamps = x->s * x->srate;
	x->rsamps = x->r * x->srate;
	x->tsamps = x->asamps+x->dsamps+x->ssamps+x->rsamps;
	x->ebreak1 = x->asamps;
	x->ebreak2 = x->asamps+x->dsamps;
	x->ebreak3 = x->asamps+x->dsamps+x->ssamps;
	x->egain1 = .7;
	x->egain2 = .1;
	x->counter = 0;
	x->click_gain = 0.0;
	x->mute = 0;
	return (x);
}


void adsr_perform64(t_adsr *x, t_object *dsp64, double **ins, 
    long numins, double **outs,long numouts, long vectorsize,
    long flags, void *userparam)
{
	t_double *in = ins[0];
	t_double *out = outs[0];
	int  n = vectorsize;
	int tsamps = x->tsamps;
	int counter = x->counter;
	int ebreak1 = x->ebreak1;
	int ebreak2 = x->ebreak2;
	int ebreak3 = x->ebreak3;
	t_double egain1 = x->egain1;
	t_double egain2 = x->egain2;
	int asamps = x->asamps;
	int dsamps = x->dsamps;
	int ssamps = x->ssamps;
	int rsamps = x->rsamps;
	t_double click_gain = x->click_gain;
	t_double etmp;
	t_double env_val;
	t_double input_val;
	/*********************************************/	
	if(x->mute){
		while(n--) *out++ = 0.0; 
	}
	while(n--) {
		input_val = *in++;
		if(input_val){
			click_gain = input_val;
			counter = 0;
		}
		if( counter < ebreak1 ){
			env_val = (t_double) counter / (t_double) asamps;
		} else if (counter < ebreak2) {
			etmp = (t_double) (counter - ebreak1) / (t_double) dsamps;
			env_val = (1.0 - etmp) + (egain1 * etmp);
		} else if (counter < ebreak3) {
			etmp = (t_double) (counter - ebreak2) / (t_double) ssamps;
			env_val = (egain1 * (1.0 - etmp)) + (egain2 * etmp);
		} else if( counter < tsamps ){
			env_val = ((t_double)(tsamps-counter)/(t_double)rsamps) * egain2 ;
		} else {
			env_val = 0.0;
		}
		if(click_gain && env_val && (click_gain != 1.0) ){
			env_val *= click_gain;
		}
		*out++ = env_val;
		if(counter < tsamps)
			counter++;
	}
	x->counter = counter;
	x->click_gain = click_gain;
}

void adsr_dsp64(t_adsr *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    // zero sample rate, die
    if(!samplerate)
        return;
    if(x->srate != samplerate ){
		x->srate = samplerate;
		x->asamps = x->a * x->srate;
		x->dsamps = x->d * x->srate;
		x->ssamps = x->s * x->srate;
		x->rsamps = x->r * x->srate;
		x->tsamps = x->asamps+x->dsamps+x->ssamps+x->rsamps;
		x->ebreak1 = x->asamps;
		x->ebreak2 = x->asamps+x->dsamps;
		x->ebreak3 = x->asamps+x->dsamps+x->ssamps;
		x->counter = 0;	
	}
    object_method(dsp64, gensym("dsp_add64"),x,adsr_perform64,0,NULL);
}

