#include "MSPd.h"
#define OBJECT_NAME "npan~"

#define SPEAKERMAX (1024)

void *npan_class;


typedef struct _npan
{
	t_pxobject x_obj;
	double pi_over_two;
	double twopi;
	int outcount;
} t_npan;

void *npan_new(t_symbol *s, int argc, t_atom *argv);
t_int *npan_perform(t_int *w);
void npan_assist(t_npan *x, void *b, long m, long a, char *s);


void npan_dsp64(t_npan *x, t_object *dsp64, short *count, double sr, long n, long flags);

/****/
int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.npan~", (method)npan_new, (method)dsp_free, sizeof(t_npan), 0,A_GIMME, 0);
	
    class_addmethod(c, (method)npan_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)npan_assist,"assist",A_CANT,0);
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    npan_class = c;
	
    potpourri_announce(OBJECT_NAME);
    return 0;
}


void npan_assist (t_npan *x, void *b, long msg, long arg, char *dst)
{
    int i;
	if (msg==1) {
        
        if( arg == 0 )
            sprintf(dst,"(signal) Audio Input");
        
        else if( arg == 1 )
            sprintf(dst,"(signal) Pan Controller (0-1)");
        
	}
    else if (msg==2) {
        for(i = 0; i < x->outcount; i++){
            if( i == arg ) sprintf(dst,"(signal) Audio Output %d", i+1);
        }
	}
}

void *npan_new(t_symbol *s, int argc, t_atom *argv)
{
	t_npan *x = (t_npan *)object_alloc(npan_class);
	int i;
	x->outcount = (int) atom_getfloatarg(0,argc,argv);
	if( x->outcount < 2 || x->outcount > SPEAKERMAX ){
		x->outcount = SPEAKERMAX;
		error("npan~: output count exceeded range limits of 2 to %d",SPEAKERMAX);
	}
	
	dsp_setup((t_pxobject *)x,2); // just input and panner
	for(i = 0; i < x->outcount; i++){
		outlet_new((t_pxobject *)x, "signal");
	}
    
	x->x_obj.z_misc |= Z_NO_INPLACE;
    
    x->pi_over_two = 1.5707963267948965;
	x->twopi = 6.283185307179586;
	return x;
}


void npan_perform64(t_npan *x, t_object *dsp64, double **ins,
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
	double gain1, gain2;
	double insamp;
	int chan1, chan2;
	double panloc, frak;
	int i;
	int outcount = x->outcount;
	double *input = ins[0];
	double *panner = ins[1];
	double *outlet1, *outlet2, *cleanoutlet;
	float pi_over_two = x->pi_over_two;
    
	
	// clean all outlets first
	for( i = 0; i < outcount; i++ ){
		cleanoutlet = outs[i];
		memset( (char *) cleanoutlet, 0, n * sizeof(double) );
	}
	for(i = 0; i < n; i++){
		panloc = panner[i];
		if( panloc < 0 )
			panloc = 0.0;
		if( panloc >= 1 ) // wrap around (otherwise crash on outlet out of range)
			panloc = 0.0;
		panloc *= (double) outcount;
		chan1 = floor( panloc );
		chan2 = (chan1 + 1) % outcount;
		frak = ( panloc - chan1 ) * pi_over_two;
		gain1 = cos( frak );
		gain2 = sin( frak );
		outlet1 = outs[chan1];
		outlet2 = outs[chan2];
		insamp = input[i];
		outlet1[i] = insamp * gain1;
		outlet2[i] = insamp * gain2;
	}
}

void npan_dsp64(t_npan *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if(!sr)
        return;
    object_method(dsp64, gensym("dsp_add64"),x,npan_perform64,0,NULL);
}
