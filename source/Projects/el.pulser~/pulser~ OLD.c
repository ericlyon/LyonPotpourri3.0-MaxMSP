#include "MSPd.h"

#define FUNC_LEN (16384)
#define FUNC_LEN_OVER2 (8192)
#define MAX_COMPONENTS (256)

#define OBJECT_NAME "pulser~"

static t_class *pulser_class;

typedef struct _pulser
{
    t_pxobject x_obj;
    int components;
    t_double global_gain;
    t_double *wavetab;
    t_double *phases;
    t_double frequency;
    t_double pulsewidth;
    t_double si_fac;
    short mute;
    short connected[4];
    t_double sr;
} t_pulser;

void *pulser_new(t_symbol *s, int argc, t_atom *argv);
void pulser_assist(t_pulser *x, void *b, long m, long a, char *s);
void pulser_mute(t_pulser *x, t_floatarg toggle);
void pulser_harmonics(t_pulser *x, t_floatarg c);
void pulser_float(t_pulser *x, double f);
void pulser_free(t_pulser *x);
void pulser_dsp64(t_pulser *x, t_object *dsp64, short *count, double sr, long n, long flags);
void pulser_perform64(t_pulser *x, t_object *dsp64, double **ins,
                      long numins, double **outs,long numouts, long n,
                      long flags, void *userparam);
int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.pulser~", (method)pulser_new, (method)dsp_free, sizeof(t_pulser), 0,A_GIMME, 0);
	
    class_addmethod(c, (method)pulser_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)pulser_assist,"assist",A_CANT,0);
	class_addmethod(c, (method)pulser_harmonics,"harmonics",A_FLOAT,0);
    class_dspinit(c);
	class_register(CLASS_BOX, c);
	pulser_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void pulser_mute(t_pulser *x, t_floatarg toggle)
{
	x->mute = toggle;
}

void pulser_assist (t_pulser *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0:
				sprintf(dst,"(signal/float) Frequency");
				break;
			case 1:
				sprintf(dst,"(signal/float) Pulse Width");
				break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Output");
	}
}

void pulser_harmonics(t_pulser *x, t_floatarg c)
{
	if(c < 2 || c > MAX_COMPONENTS){
		error("harmonic count out of bounds");
		return;
	}
	x->components = c;
	x->global_gain = 1.0 / (float) x->components ;
}

void pulser_float(t_pulser *x, double f)
{
	int inlet = ((t_pxobject*)x)->z_in;
	
	if (inlet == 0)
	{
		x->frequency = f;
	}
	else if (inlet == 1 )
	{
		x->pulsewidth = f;
	}
}

void pulser_free(t_pulser *x)
{
	dsp_free((t_pxobject *)x);
	free(x->phases);
	free(x->wavetab);
}

void *pulser_new(t_symbol *s, int argc, t_atom *argv)
{
	int i;
    
	t_pulser *x = (t_pulser *)object_alloc(pulser_class);
	dsp_setup((t_pxobject *)x,2);
	outlet_new((t_pxobject *)x, "signal");
	x->sr = sys_getsr();
	if(!x->sr){
		error("zero sampling rate, setting to 44100");
		x->sr = 44100;
	}
	
	x->mute = 0;
	x->components = 8;
	x->frequency = 440.0;
	x->pulsewidth = 0.5;
	
	if( argc > 0 )
		x->frequency = atom_getfloatarg(0,argc,argv);
	if( argc > 1 )
		x->components = atom_getfloatarg(1,argc,argv);
	
	x->si_fac = ((float)FUNC_LEN/x->sr) ;
	
	if(x->components <= 0 || x->components > MAX_COMPONENTS){
		error("%d is an illegal number of components, setting to 8",x->components );
		x->components = 8;
	}
	x->global_gain = 1.0 / (t_double) x->components ;
	x->phases = (t_double *) calloc(MAX_COMPONENTS, sizeof(t_double) );
	x->wavetab = (t_double *) calloc(FUNC_LEN, sizeof(t_double) );
	
	for(i = 0 ; i < FUNC_LEN; i++) {
		x->wavetab[i] = sin(TWOPI * ((t_double)i/(t_double) FUNC_LEN)) ;
	}
	return x;
}

void pulser_perform64(t_pulser *x, t_object *dsp64, double **ins,
                      long numins, double **outs,long numouts, long n,
                      long flags, void *userparam)
{
    int i,j;
    
	t_double gain;
	t_double incr;
	
	t_double outsamp;
	int lookdex;
	t_double *frequency_vec = ins[0];
	t_double *pulsewidth_vec = ins[1];
	t_double *out = outs[0];
	t_double *wavetab = x->wavetab;
	t_double si_fac = x->si_fac;
	t_double *phases = x->phases;
	int components = x->components;
	t_double global_gain = x->global_gain;
	t_double pulsewidth = x->pulsewidth;
	t_double frequency = x->frequency;
	short *connected = x->connected;
	
	incr = frequency * si_fac;
	
	while (n--) {
		
		if( connected[1] ){
			pulsewidth = *pulsewidth_vec++;
		}
		if( pulsewidth < 0 )
			pulsewidth = 0;
		if( pulsewidth > 1 )
			pulsewidth = 1;
		
		if( connected[0] ){
			incr = *frequency_vec++ * si_fac ;
		}
		
		outsamp = 0;
		
		for( i = 0, j = 1; i < components; i++, j++ ){
			
			lookdex = (float)FUNC_LEN_OVER2 * pulsewidth * (float)j;
			
			while( lookdex >= FUNC_LEN ){
				lookdex -= FUNC_LEN;
			}
			
			gain = wavetab[ lookdex ] ;
			
			phases[i] += incr * (float) j;
			while( phases[i] < 0.0 ) {
				phases[i] += FUNC_LEN;
			}
			while( phases[i] >= FUNC_LEN ){
				phases[i] -= FUNC_LEN;
			}
			outsamp += gain * wavetab[ (int) phases[i] ];
			
		}
		*out++ =  outsamp * global_gain;
	}
}

void pulser_dsp64(t_pulser *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    long i;
    if(!sr)
        return;
	
	if(x->sr != sr){
		x->sr = sr;
		x->si_fac = ((t_double)FUNC_LEN/x->sr);
		for(i=0;i<MAX_COMPONENTS;i++){
			x->phases[i] = 0.0;
		}
	}
	for( i = 0; i < 2; i++){
		x->connected[i] = count[i];
	}
    object_method(dsp64, gensym("dsp_add64"),x,pulser_perform64,0,NULL);
}
