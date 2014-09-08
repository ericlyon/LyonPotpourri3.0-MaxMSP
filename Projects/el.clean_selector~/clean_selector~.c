#include "MSPd.h"
#define MAX_CHANS (64)
#define CS_LINEAR (0)
#define CS_POWER (1)

static t_class *clean_selector_class;

typedef struct _clean_selector
{
	t_pxobject x_obj;
	// Variables Here
	short input_chans;
	short active_chan;
	short last_chan;
	int samps_to_fade;
	int fadesamps;
	t_double fadetime;
	t_double pi_over_two;
	short fadetype;
	short *connected_list;
//	t_double **bulk ; // array to point to all input audio channels
	t_double sr;
	t_double vs;
	int inlet_count;
	short linear; // flag to use linear fade
} t_clean_selector;

#define OBJECT_NAME "clean_selector~"

void *clean_selector_new(t_symbol *s, int argc, t_atom *argv);

t_int *clean_selector_perform(t_int *w);
void clean_selector_dsp(t_clean_selector *x, t_signal **sp, short *count);
void clean_selector_assist(t_clean_selector *x, void *b, long m, long a, char *s);
void clean_selector_float(t_clean_selector *x, t_float f);
void clean_selector_fadetime(t_clean_selector *x, t_floatarg f);
void clean_selector_int(t_clean_selector *x, t_int i);
void clean_selector_channel(t_clean_selector *x, t_floatarg i);
void clean_selector_linear(t_clean_selector *x, t_floatarg tog);
void clean_selector_dsp_free(t_clean_selector *x);
void clean_selector_perform64(t_clean_selector *x, t_object *dsp64, double **ins, 
                              long numins, double **outs,long numouts, long n,
                              long flags, void *userparam);
void clean_selector_dsp64(t_clean_selector *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);

int main(void)
{
	t_class *c;
	c = class_new("el.clean_selector~", (method)clean_selector_new, (method)dsp_free, sizeof(t_clean_selector), 0,A_GIMME, 0);

	class_addmethod(c,(method)clean_selector_assist,"assist",A_CANT,0);
	class_addmethod(c,(method)clean_selector_fadetime,"fadetime",A_FLOAT,0);
	class_addmethod(c,(method)clean_selector_channel,"channel",A_FLOAT,0);
	class_addmethod(c,(method)clean_selector_linear,"linear",A_FLOAT,0);
	class_addmethod(c,(method)clean_selector_float,"float",A_FLOAT,0);
	class_addmethod(c,(method)clean_selector_int,"int",A_LONG,0);
    class_addmethod(c, (method)clean_selector_dsp64, "dsp64", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	clean_selector_class = c;
    potpourri_announce(OBJECT_NAME);
	return 0;
}


void clean_selector_assist (t_clean_selector *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		if(arg == 0){
			sprintf(dst,"(signal/int) Input 0, Channel Number");
		} else {
			sprintf(dst,"(signal) Input %ld",arg);
		}
		
	} else if (msg==2) {
		sprintf(dst,"(signal) Output");
	}
}

void *clean_selector_new(t_symbol *s, int argc, t_atom *argv)
{
	int i;
	t_clean_selector *x;
	x = (t_clean_selector *)object_alloc(clean_selector_class);

		x->fadetime = 0.05;
		x->inlet_count = 8;
		x->linear = 0; // by default use power cross fade
		if(argc >= 1){
			x->inlet_count = (int)atom_getfloatarg(0,argc,argv);
			if(x->inlet_count < 2 || x->inlet_count > MAX_CHANS){
				error("%s: %d is illegal number of inlets",OBJECT_NAME,x->inlet_count);
				return (void *) NULL;
			}

		}  
		if(argc >= 2){
			x->fadetime = atom_getfloatarg(1,argc,argv) / 1000.0;
		}

//		post("argc %d inlet count %d fadetime %f",argc, x->inlet_count, x->fadetime);
	dsp_setup((t_pxobject *)x, x->inlet_count); 
	outlet_new((t_pxobject *)x, "signal");
	x->x_obj.z_misc |= Z_NO_INPLACE;

	
	
	x->sr = sys_getsr();
	if(!x->sr){
		x->sr = 44100.0;
		error("zero sampling rate - set to 44100");
	}
	x->fadetype = CS_POWER;
	x->pi_over_two = 1.57079632679;
	
	
    
    if(x->fadetime <= 0.0)
    	x->fadetime = .05;
    x->fadesamps = x->fadetime * x->sr;
    
    x->connected_list = (short *) sysmem_newptrclear(MAX_CHANS * sizeof(short));
    for(i=0;i<16;i++){
    	x->connected_list[i] = 0;
    }
    x->active_chan = x->last_chan = 0;
//    x->bulk = (t_double **) sysmem_newptrclear(16 * sizeof(t_double *));
    x->samps_to_fade = 0;
	return (x);
}

void clean_selector_dsp_free(t_clean_selector *x)
{
	dsp_free((t_pxobject *)x);
//	sysmem_newptrclear(x->bulk);
}


void clean_selector_fadetime(t_clean_selector *x, t_floatarg f)
{
	t_double fades = (t_double)f / 1000.0;
	
	if( fades < .0001 || fades > 1000.0 ){
		error("fade time is constrained to 0.1 - 1000000, but you wanted %f",f );
		return;
	}
	x->fadetime = fades;
	x->fadesamps = x->sr * x->fadetime;
	x->samps_to_fade = 0;
}

void clean_selector_linear(t_clean_selector *x, t_floatarg flag)
{
	x->linear = (short) flag;
}

void clean_selector_perform64(t_clean_selector *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
	t_double *out = outs[0];
	
	int fadesamps = x->fadesamps;
	short active_chan = x->active_chan;
	short last_chan = x->last_chan;
	int samps_to_fade = x->samps_to_fade;
	t_double m1, m2;
    int i;
	t_double pi_over_two = x->pi_over_two;
	short fadetype = x->fadetype;
	t_double phase;
	short linear = x->linear;
	
	/********************************************/
    
	if ( active_chan >= 0 && active_chan < numins) 
    {
		for(i=0; i < n; i++) {
			if ( samps_to_fade >= 0 ) {
				if( fadetype == CS_POWER ){
					if( linear ){
						phase = (float) samps_to_fade / (float) fadesamps;
						out[i] = ins[active_chan][i] + phase * ( ins[last_chan][i] - ins[active_chan][i] );
						
					} else {
						phase = pi_over_two * (1.0 - ( (float) samps_to_fade / (float) fadesamps) ) ;
						m1 = sin( phase );
						m2 = cos( phase );
						out[i] = (ins[active_chan][i] * m1) + (ins[last_chan][i] * m2);
					}
					--samps_to_fade;
				} 
			}
			else {
				out[i] =  ins[active_chan][i];
			}
		}
	} 
  	else  {
  		while( n-- ) {
			*out++ = 0.0;
		}
  	}
    
	x->samps_to_fade = samps_to_fade;
}

void clean_selector_dsp64(t_clean_selector *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    int i; 
    // zero sample rate, go die
    if(!samplerate)
        return;
	if(x->sr != samplerate){
		x->sr = samplerate;
		x->fadesamps = x->fadetime * x->sr;
		x->samps_to_fade = 0;
	}
    for (i = 0; i < MAX_CHANS; i++) {
        x->connected_list[i] = count[i];
    }
    object_method(dsp64, gensym("dsp_add64"),x,clean_selector_perform64,0,NULL);
}


void clean_selector_float(t_clean_selector *x, t_float f) // Look at floats at inlets
{
	clean_selector_int(x,(t_int)f);	
}

void clean_selector_int(t_clean_selector *x, t_int i) // Look at int at inlets
{
	int inlet = ((t_pxobject*)x)->z_in;
	
	if (inlet == 0)
	{		
			clean_selector_channel(x,(t_floatarg)i);
	}	

}

void clean_selector_channel(t_clean_selector *x, t_floatarg i) // Look at int at inlets
{
	int chan = i;
	if(chan < 0 || chan > x->inlet_count - 1){
		post("%s: channel %d out of range",OBJECT_NAME, chan);
		return;
	}	
	if(chan != x->active_chan) {
		
		x->last_chan = x->active_chan;
		x->active_chan = chan;
		x->samps_to_fade = x->fadesamps;
		if( x->active_chan < 0)
			x->active_chan = 0;
		if( x->active_chan > MAX_CHANS - 1) {
			x->active_chan = MAX_CHANS - 1;
		}
		if(! x->connected_list[chan]) {
		// do it anyway - it's user-stupidity
		/*
			post("warning: channel %d not connected",chan);
			x->active_chan = 1; */
		}
		// post("last: %d active %d", x->last_chan, x->active_chan);
	}	
}
