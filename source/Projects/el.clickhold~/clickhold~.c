#include "MSPd.h"
#define OBJECT_NAME "clickhold~"
static t_class *clickhold_class;

typedef struct _clickhold
{
	t_pxobject x_obj;
	float hold_value;
} t_clickhold;

void *clickhold_new(t_symbol *s, int argc, t_atom *argv);
t_int *clickhold_perform(t_int *w);
void clickhold_assist(t_clickhold *x, void *b, long m, long a, char *s);
void clickhold_dsp64(t_clickhold *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void clickhold_perform64(t_clickhold *x, t_object *dsp64, double **ins, 
                         long numins, double **outs,long numouts, long n,
                         long flags, void *userparam);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.clickhold~", (method)clickhold_new, (method)dsp_free, sizeof(t_clickhold), 0,A_GIMME, 0);
	

    class_addmethod(c, (method)clickhold_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)clickhold_assist,"assist",A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	clickhold_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void clickhold_assist (t_clickhold *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0:sprintf(dst,"(signal) Non-Zero Trigger Value");break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Sample and Hold Output");
	}
}

void *clickhold_new(t_symbol *s, int argc, t_atom *argv)
{
	t_clickhold *x = (t_clickhold *)object_alloc(clickhold_class);
	dsp_setup((t_pxobject *)x,1); 
	outlet_new((t_pxobject *)x, "signal");
	x->hold_value = 0;
	return x;
}

void clickhold_perform64(t_clickhold *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
	t_double *in_vec = ins[0];
	t_double *out_vec = outs[0];
    
	float hold_value = x->hold_value;
	
	while( n-- ) {
		if(*in_vec){
			hold_value = *in_vec;
		}
		in_vec++;
		*out_vec++ = hold_value;
        
	}
	x->hold_value = hold_value;
}


void clickhold_dsp64(t_clickhold *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if(!samplerate)
        return;
    object_method(dsp64, gensym("dsp_add64"),x,clickhold_perform64,0,NULL);
}
