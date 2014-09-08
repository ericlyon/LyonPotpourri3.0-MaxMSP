#include "MSPd.h"

static t_class *latch_class;

#define OBJECT_NAME "latch~"
#define LATCH_WAITING 0
#define LATCH_RUNNING 1

typedef struct _latch
{
	t_pxobject x_obj;

	double current;
	long latch;
	long dsamps;
	long sr;
	double duration;
	short status;
	short connected;
} t_latch;

void *latch_new(t_symbol *s, int argc, t_atom *argv);
t_int *latch_perform(t_int *w);
void latch_assist(t_latch *x, void *b, long m, long a, char *s);
void latch_float(t_latch *x, double f);
void latch_version(t_latch *x);
void latch_dsp64(t_latch *x, t_object *dsp64, short *count, double sr, long n, long flags);
void latch_perform64(t_latch *x, t_object *dsp64, double **ins, 
                     long numins, double **outs,long numouts, long n,
                     long flags, void *userparam);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.latch~", (method)latch_new, (method)dsp_free, sizeof(t_latch), 0,A_GIMME, 0);
	
    class_addmethod(c, (method)latch_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)latch_assist,"assist",A_CANT,0);
    class_dspinit(c);
	class_register(CLASS_BOX, c);
	latch_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void latch_assist (t_latch *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0:sprintf(dst,"(signal) Non-Zero Triggers");break;
			case 1:sprintf(dst,"(signal/float) Latch Duration");break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Latch");
	}
}


void *latch_new(t_symbol *s, int argc, t_atom *argv)
{

	t_latch *x = (t_latch *)object_alloc(latch_class);
	dsp_setup((t_pxobject *)x,2); 
	outlet_new((t_pxobject *)x, "signal");

	x->status = LATCH_WAITING;
	x->duration = 1000.0;
	atom_arg_getdouble(&x->duration,0,argc,argv);
	x->sr = sys_getsr();
	if(x->duration <= 1.0)
		x->duration = 1.0;
	x->duration *= 0.001;
	x->dsamps = x->sr ? x->sr * x->duration : 44100;

	return x;
}

void latch_perform64(t_latch *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
    t_double *trigger = ins[0];
	t_double *vec_duration = ins[1];
	t_double *output = outs[0];
	long latch = x->latch;
	long dsamps = x->dsamps;
	short status = x->status;
	double current = x->current;
	double duration = x->duration;
	long sr = x->sr;
	short connected = x->connected;
	int i;
	
	for(i = 0; i < n; i++){
		if(connected){
			duration = vec_duration[i];
			if( duration > 0.0 ){ // dummy proof
				dsamps = sr * duration * 0.001;
			}
		}
		if(trigger[i]){
			latch = 0;
			status = LATCH_RUNNING;
			current = trigger[i];
		} else {
			latch++;
			if(latch >= dsamps){
				status = LATCH_WAITING;
				current = 0.0;
			}
		}
		output[i] = current;
	}
	
	x->current = current;
	x->status = status;
	x->latch = latch;
	x->dsamps = dsamps;
}

void latch_float(t_latch *x, double f)
{
	long inletnum = proxy_getinlet((t_object *)x);
	if(inletnum == 1 && f > 0.0){
		x->duration = f * 0.001;
		x->dsamps = x->sr * x->duration;
	}

}

void latch_dsp64(t_latch *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if(!sr)
        return;
	if(x->sr != sr){
		x->sr = sr;
		x->dsamps = x->duration * x->sr;
	}    object_method(dsp64, gensym("dsp_add64"),x,latch_perform64,0,NULL);
}
