#include "MSPd.h"

static t_class *channel_class;


typedef struct _channel
{
	t_pxobject x_obj;
	void *float_outlet;
	int channel;
} t_channel;

#define OBJECT_NAME "channel~"

void *channel_new(t_symbol *s, int argc, t_atom *argv);

t_int *channel_perform(t_int *w);
void channel_assist(t_channel *x, void *b, long m, long a, char *s);
void channel_channel(t_channel *x, t_floatarg chan) ;
void channel_int(t_channel *x, long chan) ;
void channel_perform64(t_channel *x, t_object *dsp64, double **ins, 
    long numins, double **outs,long numouts, long vectorsize,
    long flags, void *userparam);
void channel_dsp64(t_channel *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.channel~", (method)channel_new, (method)dsp_free, sizeof(t_channel), 0,A_GIMME, 0);

    class_addmethod(c,(method)channel_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c,(method)channel_assist,"assist",A_CANT,0);
    class_addmethod(c,(method)channel_channel,"channel",A_FLOAT,0);
	class_addmethod(c,(method)channel_int,"int",A_LONG,0);
	class_addmethod(c,(method)channel_int,"in1",A_LONG,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	channel_class = c;
	potpourri_announce(OBJECT_NAME);   
	return 0;
}


void channel_channel(t_channel *x, t_floatarg chan) 
{
	if(chan >= 0)
		x->channel = (int) chan;
}


void channel_int(t_channel *x, long chan) 
{
	if(chan >= 0)
		x->channel = (int) chan;
}

void channel_assist (t_channel *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0:sprintf(dst,"(signal) Input");break;
			case 1:sprintf(dst,"(int) Channel Number");break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Channel Value");
	}
}

void *channel_new(t_symbol *s, int argc, t_atom *argv)
{
	t_channel *x = (t_channel *)object_alloc(channel_class);
	intin(x,1);
	dsp_setup((t_pxobject *)x,1);
	outlet_new((t_object *)x, "signal");
	x->channel = (int)atom_getfloatarg(0,argc,argv);
	return (x);
}

t_int *channel_perform(t_int *w)
{
	t_channel *x = (t_channel *) (w[1]);
	t_float *in_vec = (t_float *)(w[2]);
	t_float *out_vec = (t_float *)(w[3]);
	t_int n = w[4];
	int channel = x->channel;
	float value;
	
	if(channel < 0 || channel > n){
		return w + 5;
	}
	value = in_vec[channel];
	
	while( n-- ) {
		*out_vec++ = value;
	}
	return w + 5;
}	

void channel_perform64(t_channel *x, t_object *dsp64, double **ins, 
    long numins, double **outs,long numouts, long vectorsize,
    long flags, void *userparam)
{
	t_double *in_vec = ins[0];
	t_double *out_vec = outs[0];
	t_int n = vectorsize;
	int channel = x->channel;
	t_double value;
	
	if(channel < 0 || channel > n){
        return;
	}
	value = in_vec[channel];
	
	while( n-- ) {
		*out_vec++ = value;
	}
}	
/*
void channel_dsp(t_channel *x, t_signal **sp, short *count)
{
    dsp_add(channel_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
}
*/
void channel_dsp64(t_channel *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if(!samplerate)
        return;
    object_method(dsp64, gensym("dsp_add64"),x,channel_perform64,0,NULL);
}
