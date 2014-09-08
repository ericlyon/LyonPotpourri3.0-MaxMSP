#include "MSPd.h"

static t_class *counter_class;

#define OBJECT_NAME "counter~"

#define COUNTER_UP (1)
#define COUNTER_DOWN (-1)

typedef struct _counter
{
	t_pxobject x_obj;
	t_atom_long current;
	t_atom_long min;
	t_atom_long max;
	short direction;	
} t_counter;

void *counter_new(t_symbol *s, int argc, t_atom *argv);
void counter_assist(t_counter *x, void *b, long m, long a, char *s);
void counter_setnext(t_counter *x, t_floatarg val);
void counter_direction(t_counter *x, t_floatarg d);
void counter_minmax(t_counter *x, t_floatarg min, t_floatarg max);
void counter_dsp64(t_counter *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void counter_perform64(t_counter *x, t_object *dsp64, double **ins, 
                       long numins, double **outs,long numouts, long n,
                       long flags, void *userparam);
int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.counter~", (method)counter_new, (method)dsp_free, sizeof(t_counter), 0,A_GIMME, 0);
	
    class_addmethod(c, (method)counter_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)counter_assist,"assist",A_CANT,0);
    class_addmethod(c, (method)counter_minmax,"minmax",A_FLOAT,A_FLOAT,0);
    class_addmethod(c, (method)counter_direction,"direction",A_FLOAT,0);
    class_addmethod(c, (method)counter_setnext,"setnext",A_FLOAT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	counter_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}


void counter_setnext(t_counter *x, t_floatarg val)
{
	if( val < x->min || val > x->max)
		return;
	x->current = (long) val;
}

void counter_direction(t_counter *x, t_floatarg d)
{
	if( (d != COUNTER_UP) && (d != COUNTER_DOWN) )
		return;
	x->direction = (short) d;
}

void counter_minmax(t_counter *x, t_floatarg min, t_floatarg max)
{
	if(min < 1){
		return;
	}
	if(min >= max){
		return;
	}
	x->min = min;
	x->max = max;
}

void counter_assist (t_counter *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0:sprintf(dst,"(signal) Non-Zero Triggers");break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Counter Clicks");
	}
}


void *counter_new(t_symbol *s, int argc, t_atom *argv)
{

	t_counter *x = (t_counter *)object_alloc(counter_class);
	dsp_setup((t_pxobject *)x,1); 
	outlet_new((t_pxobject *)x, "signal");
	x->min = 1;
	x->max = 10;
	x->direction = COUNTER_UP;
	atom_arg_getlong(&x->min,0,argc,argv);
	atom_arg_getlong(&x->max,1,argc,argv);
	if(x->min <= 1)
		x->min = 1;
	if(x->max <= x->min)
		x->max = 10;

	return x;
}

void counter_perform64(t_counter *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
	t_double *in_vec = ins[0];
	t_double *out_vec = outs[0];
	
	int i;
	long min = x->min;
	long max = x->max;
	long current = x->current;
	short direction = x->direction;
	
	for(i = 0; i < n; i++){
		if(in_vec[i]){
			out_vec[i] = current;
			current = current + direction;
			if( direction == COUNTER_UP ){
				if( current > max ){
					current = min; 
				}
			} else if( direction == COUNTER_DOWN ){
				if( current < min ){
					current = max;
				}
			}
		} else {
			out_vec[i] = 0.0;
		}
	}
	
	x->current = current;
}


t_int *counter_perform(t_int *w)
{
	t_counter *x = (t_counter *) (w[1]);
	t_float *in_vec = (t_float *)(w[2]);
	t_float *out_vec = (t_float *)(w[3]);
	t_int n = w[4];
	
	int i;
	long min = x->min;
	long max = x->max;
	long current = x->current;
	short direction = x->direction;
	
	for(i = 0; i < n; i++){
		if(in_vec[i]){
			out_vec[i] = current;
			current = current + direction;
			if( direction == COUNTER_UP ){
				if( current > max ){
					current = min; 
				}
			} else if( direction == COUNTER_DOWN ){
				if( current < min ){
					current = max;
				}
			}
		} else {
			out_vec[i] = 0.0;
		}
	}
	
	x->current = current;
	return w + 5;
}		

void counter_dsp64(t_counter *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if(!samplerate)
        return;
    object_method(dsp64, gensym("dsp_add64"),x,counter_perform64,0,NULL);
}

