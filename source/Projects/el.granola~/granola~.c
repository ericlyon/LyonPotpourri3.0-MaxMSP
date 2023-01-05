#include "MSPd.h"

static t_class *granola_class;

#define OBJECT_NAME "granola~"

typedef struct _granola
{

	t_pxobject x_obj;

	float *gbuf;
    t_double *gbuf64;
	int grainsamps;
	int buflen;
	int maxgrainsamps; // set maximum delay in ms.
	float *grainenv; 
    t_double *grainenv64;
	float grain_duration; // user grain duration in seconds
	int gpt1;
	int gpt2;
	int gpt3;
	float phs1;
	float phs2;
	float phs3;
	float incr;
	int curdel;
	short mute_me;
	short iconnect;
	float sr;
} t_granola;

void *granola_new(t_floatarg val);
t_int *offset_perform(t_int *w);
void granola_assist(t_granola *x, void *b, long m, long a, char *s);
void granola_mute(t_granola *x, t_floatarg toggle);
void granola_size(t_granola *x, t_floatarg newsize);
void granola_clear(t_granola *x);
void granola_float(t_granola *x, double f) ;
void granola_int(t_granola *x, long f);
void granola_dsp_free(t_granola *x);
void granola_version(t_granola *x);
void granola_init(t_granola *x);
void granola_perform64(t_granola *x, t_object *dsp64, double **ins, 
                       long numins, double **outs,long numouts, long n,
                       long flags, void *userparam);
void granola_dsp64(t_granola *x, t_object *dsp64, short *count, double samplerate, long n, long flags);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.granola~", (method)granola_new,
                  (method)granola_dsp_free, sizeof(t_granola), 0,A_FLOAT,0);
	
    class_addmethod(c,(method)granola_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c,(method)granola_assist,"assist",A_CANT,0);
	class_addmethod(c,(method)granola_clear,"clear",0);
	class_addmethod(c,(method)granola_size,"size",A_FLOAT,0);
    class_addmethod(c,(method)granola_float,"float",A_FLOAT,0);
    class_dspinit(c);
	class_register(CLASS_BOX, c);
	granola_class = c;
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void granola_int(t_granola *x, long f) {
	granola_float( x, (double) f );
} 


void granola_float(t_granola *x, double f) {
	x->incr = f; 
} 

void granola_clear(t_granola *x) {
	memset((char *)x->gbuf, 0, x->buflen);
} 

void granola_size(t_granola *x, t_floatarg newsize) {
	int newsamps, i;
	newsamps = newsize * 0.001 * sys_getsr();
	if( newsamps >= x->maxgrainsamps ){
		error("granola~: specified size over preset maximum, no action taken");
		return;
	}
	if( newsamps < 8 ){
		error("granola~: grainsize too small");
		return;
	}
	x->grainsamps = newsamps; // will use for shrinkage
	x->buflen = x->grainsamps * 4;
	for(i = 0; i < x->grainsamps; i++ ){
		x->grainenv[i] = .5 + (-.5 * cos( TWOPI * ((float)i/(float)x->grainsamps) ) );
	}
	x->gpt1 = 0;
	x->gpt2 = x->grainsamps / 3.;
	x->gpt3 = 2. * x->grainsamps / 3.;
	x->phs1 = 0;
	x->phs2 = x->grainsamps / 3. ;
	x->phs3 = 2. * x->grainsamps / 3. ;
	x->curdel = 0;
} 


void granola_dsp_free(t_granola *x)
{
	dsp_free((t_pxobject *)x);
	free(x->gbuf);
	free(x->grainenv);
}


void granola_mute(t_granola *x, t_floatarg toggle)
{
	x->mute_me = (short) toggle;
}

void granola_assist (t_granola *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0:sprintf(dst,"(signal) Input");break;
			case 1:sprintf(dst,"(signal/float) Increment");break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Output");
	}
}

void *granola_new(t_floatarg val)
{

	t_granola *x = (t_granola *)object_alloc(granola_class);
	dsp_setup((t_pxobject *)x,2);
	outlet_new((t_pxobject *)x, "signal");
	
	// INITIALIZATIONS
	// input now in ms
	x->sr = sys_getsr();
	x->grain_duration = val * 0.001; // convert to seconds
	x->gbuf = NULL;
	x->grainenv = NULL;
	
	granola_init(x);
	return x;
}

void granola_init(t_granola *x)
{
	int i;
	if(x->sr == 0){
		// post("granola~: dodging zero sampling rate!");
		return;
	}
	x->grainsamps = x->grain_duration * x->sr;
  //  post("grain dur: %f sampling rate: %f grain samps: %d\n",x->grain_duration, x->sr, x->grainsamps);
	if(x->grainsamps <= 5 || x->grainsamps > 441000) {
		x->grainsamps = 2048;
	//	post( "granola~: grainsize autoset to %d samples, rather than user-specified length %.0f", x->grainsamps, x->grain_duration * x->sr);
	}
	x->maxgrainsamps = x->grainsamps; // will use for shrinkage
	x->buflen = x->grainsamps * 4;
	// first time only
	if(x->gbuf == NULL){
		x->gbuf = (float *) calloc(x->buflen, sizeof(float));
        x->gbuf64 = (t_double *) calloc(x->buflen, sizeof(t_double));
		x->grainenv = (float *) calloc(x->grainsamps, sizeof(float));
        x->grainenv64 = (t_double *) calloc(x->grainsamps, sizeof(t_double));
		x->incr = .5;
		x->mute_me = 0;
	} 
	// or realloc if necessary
	else {
		x->gbuf = (float *) realloc(x->gbuf, x->buflen * sizeof(float));
		x->grainenv = (float *) realloc(x->grainenv, x->grainsamps * sizeof(float));
		x->gbuf64 = (t_double *) realloc(x->gbuf, x->buflen * sizeof(t_double));
		x->grainenv64 = (t_double *) realloc(x->grainenv, x->grainsamps * sizeof(t_double));
	}
	for(i = 0; i < x->grainsamps; i++ ){
		x->grainenv[i] = .5 + (-.5 * cos(TWOPI * ((float)i/(float)x->grainsamps)));
        x->grainenv64[i] = .5 + (-.5 * cos(TWOPI * ((t_double)i/(t_double)x->grainsamps)));
	}
	x->gpt1 = 0;
	x->gpt2 = x->grainsamps / 3.;
	x->gpt3 = 2. * x->grainsamps / 3.;
	x->phs1 = 0;
	x->phs2 = x->grainsamps / 3. ;
	x->phs3 = 2. * x->grainsamps / 3. ;
	
	x->curdel = 0;
}

void granola_perform64(t_granola *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
    t_double  outsamp ;
	int iphs_a, iphs_b;
	t_double m1, m2;	
	/****/
	t_double *in = ins[0];
	t_double *increment = ins[1];
	t_double *out = outs[0];
	int iconnect = x->iconnect;
    
	while (n--) { 
        
		if(iconnect)
			x->incr = *increment++;
		
		if( x->incr <= 0. ) {
			x->incr = .5 ;
		}
		
		if( x->curdel >= x->buflen ){
			x->curdel = 0 ;
		}    
		x->gbuf64[ x->curdel ] = *in++;
    	
		// grain 1 
		iphs_a = floor( x->phs1 );
		iphs_b = iphs_a + 1;
		
		m2 = x->phs1 - iphs_a;
		m1 = 1. - m2 ;
		while( iphs_a >= x->buflen ) {
			iphs_a -= x->buflen;
		}
		while( iphs_b >= x->buflen ) {
			iphs_b -= x->buflen;
		}
		outsamp = ( m1 * x->gbuf64[ iphs_a ] + m2 * x->gbuf64[ iphs_b ] ) * x->grainenv64[ x->gpt1 ];
		++(x->gpt1);
		if( x->gpt1 >= x->grainsamps ) {
			
			x->gpt1 = 0;
			x->phs1 = x->curdel ;
		}
		x->phs1 += x->incr;
		while( x->phs1 >= x->buflen ) {
			x->phs1 -= x->buflen ;
		}
		
		// now add second grain 
		
		
		iphs_a = floor( x->phs2 );
		
		iphs_b = iphs_a + 1;
		
		m2 = x->phs2 - iphs_a;
		
		m1 = 1. - m2 ;
		
		while( iphs_a >= x->buflen ) {
			iphs_a -= x->buflen;
		}
		while( iphs_b >= x->buflen ) {
			iphs_b -= x->buflen;
		}
		
		outsamp += ( m1 * x->gbuf64[ iphs_a ] + m2 * x->gbuf64[ iphs_b ] ) * x->grainenv64[ x->gpt2 ];
		
		++(x->gpt2);
		if( x->gpt2 >= x->grainsamps ) {
			
			x->gpt2 = 0;
			x->phs2 = x->curdel ;
		}
		x->phs2 += x->incr ;    
		while( x->phs2 >= x->buflen ) {
			x->phs2 -= x->buflen ;
		}
		
		// now add third grain 
		
		iphs_a = floor( x->phs3 );
		iphs_b = iphs_a + 1;
		m2 = x->phs3 - iphs_a;
		m1 = 1. - m2 ;
		while( iphs_a >= x->buflen ) {
			iphs_a -= x->buflen;
		}
		while( iphs_b >= x->buflen ) {
			iphs_b -= x->buflen;
		}
		
		outsamp += ( m1 * x->gbuf64[ iphs_a ] + m2 * x->gbuf64[ iphs_b ] ) * x->grainenv64[ x->gpt3 ];
		
		++(x->gpt3);
		if( x->gpt3 >= x->grainsamps ) {
			x->gpt3 = 0;
			x->phs3 = x->curdel ;
		}
		x->phs3 += x->incr ;    
		while( x->phs3 >= x->buflen ) {
			x->phs3 -= x->buflen ;
		}
		
		
		++(x->curdel);
		
		
		*out++ = outsamp; 
		/* output may well need to be attenuated */
	}
}
		
void granola_dsp64(t_granola *x, t_object *dsp64, short *count, double samplerate, long n, long flags)
{
    if(!samplerate)
        return;
	if(x->sr != samplerate){
		granola_init(x);
	}
    object_method(dsp64, gensym("dsp_add64"),x,granola_perform64,0,NULL);
}
