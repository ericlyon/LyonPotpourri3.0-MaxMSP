#include "MSPd.h"

#define OBJECT_NAME "click2float~"

static t_class *click2float_class;

typedef struct _click2float
{
	t_pxobject x_obj;
	void *float_outlet;
	void *clock;
	double float_value;	
} t_click2float;

void *click2float_new(t_symbol *s, int argc, t_atom *argv);
t_int *click2float_perform(t_int *w);
void click2float_assist(t_click2float *x, void *b, long m, long a, char *s);
void click2float_tick(t_click2float *x) ;
void click2float_perform64(t_click2float *x, t_object *dsp64, double **ins, 
                           long numins, double **outs,long numouts, long n,
                           long flags, void *userparam);
void click2float_dsp64(t_click2float *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);


int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.click2float~", (method)click2float_new, (method)dsp_free, sizeof(t_click2float), 0,A_GIMME, 0);
    
	class_addmethod(c,(method)click2float_assist,"assist",A_CANT,0);
    class_addmethod(c, (method)click2float_dsp64, "dsp64", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	click2float_class = c;	
    potpourri_announce(OBJECT_NAME);
	return 0;
}


void click2float_tick(t_click2float *x) 
{
  outlet_float(x->float_outlet,x->float_value);
}

void click2float_assist (t_click2float *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0:sprintf(dst,"(signal) Click Trigger");break;
		}
	} else if (msg==2) {
		sprintf(dst,"(float) Click Value");
	}
}

void *click2float_new(t_symbol *s, int argc, t_atom *argv)
{
    t_click2float *x;
    x = (t_click2float *)object_alloc(click2float_class);
	x->float_outlet = floatout((t_pxobject *)x);
	dsp_setup((t_pxobject *)x,1); // No Signal outlets
	x->clock = clock_new(x,(void *)click2float_tick);
	return x;
}

void click2float_perform64(t_click2float *x, t_object *dsp64, double **ins, 
                           long numins, double **outs,long numouts, long n,
                           long flags, void *userparam)
{
	t_double *in_vec = ins[0];
    
	while( n-- ) {
		if(*in_vec){
			x->float_value = *in_vec;
			clock_delay(x->clock, 0);
		}
		in_vec++;
	}
}

void click2float_dsp64(t_click2float *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if(!samplerate)
        return;
    object_method(dsp64, gensym("dsp_add64"),x,click2float_perform64,0,NULL);
}

