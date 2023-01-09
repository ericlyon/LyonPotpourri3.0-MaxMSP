#include "MSPd.h"
#define FUNC_LEN (32768)
#define OBJECT_NAME "phasemod~"

static t_class *phasemod_class;

typedef struct _phasemod
	{
		t_pxobject x_obj;
		t_float x_val;
		t_double mygain;
		t_double *wavetab;
		t_double phs;
		t_double bendphs;
		t_double frequency;
		t_double alpha;
		short mute;
		short connections[4];
		t_double si_fac;
		t_double sr;
	} t_phasemod;

void *phasemod_new(t_symbol *s, int argc, t_atom *argv);
t_int *offset_perform(t_int *w);
void phasemod_float(t_phasemod *x, double f);
void phasemod_int(t_phasemod *x, long n);
void phasemod_mute(t_phasemod *x, t_floatarg toggle);
void phasemod_assist(t_phasemod *x, void *b, long m, long a, char *s);
void phasemod_dsp_free(t_phasemod *x);
void phasemod_version(t_phasemod *x);
void phasemod_dsp64(t_phasemod *x, t_object *dsp64, short *count, double sr, long n, long flags);
void phasemod_perform64(t_phasemod *x, t_object *dsp64, double **ins, 
                        long numins, double **outs,long numouts, long n,
                        long flags, void *userparam);


int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.phasemod~", (method)phasemod_new, (method)dsp_free, sizeof(t_phasemod), 0,A_GIMME, 0);
	
    class_addmethod(c, (method)phasemod_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)phasemod_assist,"assist",A_CANT,0);
    class_dspinit(c);
	class_register(CLASS_BOX, c);
	phasemod_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void phasemod_dsp_free( t_phasemod *x )
{
	dsp_free((t_pxobject *) x);
	free(x->wavetab);
}

void phasemod_mute(t_phasemod *x, t_floatarg toggle)
{
	x->mute = toggle;
}
void phasemod_assist (t_phasemod *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0:
				sprintf(dst,"(signal/float) Frequency ");
				break;
			case 1:
				sprintf(dst,"(signal/float) Slope Factor ");
				break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Output ");
	}
}

void *phasemod_new(t_symbol *s, int argc, t_atom *argv)
{
  	int i;

    t_phasemod *x = (t_phasemod *)object_alloc(phasemod_class);
    dsp_setup((t_pxobject *)x,2);
    outlet_new((t_pxobject *)x, "signal");
    x->phs = 0;
  	x->mute = 0;
	x->frequency = 440.0;
	atom_arg_getdouble(&x->frequency, 0, argc, argv);
	x->wavetab = (t_double *) calloc(FUNC_LEN, sizeof(t_double) );
    for( i = 0 ; i < FUNC_LEN; i++ ) {
    	x->wavetab[i] = sin( TWOPI * ((t_double)i/(t_double) FUNC_LEN)) ;
    }
	x->bendphs = 0;
	x->sr = sys_getsr();
	if(!x->sr)
		x->sr = 44100.0;
	x->si_fac = 1.0/x->sr;
    return (x);
}


void phasemod_float(t_phasemod *x, double f)
{
	int inlet = ((t_pxobject*)x)->z_in;
	
	if (inlet == 0)
	{
		x->frequency = f;
	} 
	else if (inlet == 1 )
	{
		x->alpha = f;
	}
	
}

void phasemod_int(t_phasemod *x, long n)
{
	x->x_val = (float)n;
}


void phasemod_perform64(t_phasemod *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
    t_double phs;
	
	t_double *frequency_vec = ins[0];
	t_double *alpha_vec = ins[1];
	t_double *out = outs[0];
	
	short *connections = x->connections;
	t_double bendphs = x->bendphs;
	t_double *wavetab = x->wavetab;
	t_double si_fac = x->si_fac;
	
	t_double incr = x->frequency * si_fac ;
	t_double alpha = x->alpha;	
	
	while (n--) { 
		if( connections[1] ){
			alpha = *alpha_vec++;
		}
		if( alpha == 0 ){
			alpha = .000001;
		}
		
		if( connections[0] ){
			incr = *frequency_vec++ * si_fac ;
		}
		// NO NEGATIVE FREQUENCIES
		if( incr < 0 )
			incr = -incr;
		
		bendphs += incr ;
		while( bendphs > 1.0 )
			bendphs -= 1.0 ;
		phs =   FUNC_LEN * ( (1 - exp(bendphs * alpha))/(1 - exp(alpha))  ); 
		
		while( phs < 0.0 ) {
			phs += FUNC_LEN;
		}
		while( phs >= FUNC_LEN ){
			phs -= FUNC_LEN;
		}
		*out++ =  wavetab[(int) phs] ; 
	}
	
	x->bendphs = bendphs;
}


void phasemod_dsp64(t_phasemod *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if(!sr)
        return;
	x->connections[0] = count[0];
	x->connections[1] = count[1];
	
	if(x->sr != sr){
		x->sr = sr;
		x->si_fac = 1.0/x->sr;
	}
    object_method(dsp64, gensym("dsp_add64"),x,phasemod_perform64,0,NULL);
}

