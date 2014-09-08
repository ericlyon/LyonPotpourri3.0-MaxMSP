#include "MSPd.h"

#define OBJECT_NAME "waveshape~"

#define ws_MAXHARMS (256)


static t_class *waveshape_class;

typedef struct _waveshape
{
	t_pxobject x_obj;
    int flen;
    t_double *wavetab;
    t_double *tempeh; // work function
    int hcount;
    t_double *harms;
    short mute;
} t_waveshape;

void *waveshape_new(t_symbol *s, int argc, t_atom *argv);

t_int *waveshape_perform(t_int *w);
void waveshape_assist(t_waveshape *x, void *b, long m, long a, char *s);
void waveshape_list (t_waveshape *x, t_symbol *msg, short argc, t_atom *argv);
void update_waveshape_function( t_waveshape *x );
void waveshape_mute(t_waveshape *x, t_floatarg tog);
void waveshape_free(t_waveshape *x);
void waveshape_perform64(t_waveshape *x, t_object *dsp64, double **ins, 
                         long numins, double **outs,long numouts, long n,
                         long flags, void *userparam);
void waveshape_dsp64(t_waveshape *x, t_object *dsp64, short *count, double sr, long n, long flags);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.waveshape~", (method)waveshape_new, (method)dsp_free, sizeof(t_waveshape), 0,A_GIMME, 0);
	
    class_addmethod(c, (method)waveshape_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)waveshape_assist,"assist",A_CANT,0);
    class_addmethod (c,(method)waveshape_list, "list", A_GIMME, 0);
    class_dspinit(c);
	class_register(CLASS_BOX, c);
	waveshape_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void waveshape_free(t_waveshape *x)
{
	dsp_free((t_pxobject *)x);
	free(x->wavetab);
	free(x->tempeh);
	free(x->harms);
}

void waveshape_assist (t_waveshape *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Input"); break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Output");
	}
}

void waveshape_list (t_waveshape *x, t_symbol *msg, short argc, t_atom *argv)
{
	short i;
	
	x->hcount = 0;
	for (i=0; i < argc; i++) {
		switch (argv[i].a_type) {
			case A_LONG:
				x->harms[ x->hcount ] = argv[i].a_w.w_long;
				++(x->hcount);
				break;
				
			case A_SYM:
				break;
			case A_FLOAT:
				x->harms[ x->hcount ] = argv[i].a_w.w_float;
				++(x->hcount);
				break;
		}
	}
	update_waveshape_function( x );	
	
}

void waveshape_mute(t_waveshape *x, t_floatarg tog)
{
	x->mute = tog;
}

void *waveshape_new(t_symbol *s, int argc, t_atom *argv)
{

    t_waveshape *x = (t_waveshape *)object_alloc(waveshape_class);
    dsp_setup((t_pxobject *)x,1);
    outlet_new((t_pxobject *)x, "signal");

	x->flen = 1<<16 ;
	x->wavetab = (t_double *) calloc( x->flen, sizeof(t_double) );
	x->tempeh = (t_double *) calloc( x->flen, sizeof(t_double) );
	x->harms = (t_double *) calloc( ws_MAXHARMS, sizeof(t_double) );
	x->hcount = 4;
	x->harms[0] = 0;
	x->harms[1] = .33;
	x->harms[2] = .33;
	x->harms[3] = .33;
	x->mute = 0;
	update_waveshape_function( x );	
    return (x);
}

void update_waveshape_function( t_waveshape *x ) {
	float point;
	int i, j;
	float min, max;
	// zero out function;
	for( i = 0; i < x->flen; i++ ){
		x->tempeh[i] = 0;
	}	
	for( i = 0 ; i < x->hcount; i++ ){
		if( x->harms[i] > 0.0 ) {
			for( j = 0; j < x->flen; j++ ){
				point = -1.0 + 2.0 * ( (float) j / (float) x->flen) ;
				x->tempeh[j] += x->harms[i] * cos( (float) i * acos( point ) );
			}
		}
	}
	min = 1; max = -1;
	for( j = 0; j < x->flen; j++ ){	
		if( min > x->tempeh[j] )
			min = x->tempeh[j];
		if( max < x->tempeh[j] )
			max = x->tempeh[j];
		
	}
	//	post("min:%f, max:%f",min,max);
	// normalize from -1 to +1
	if( (max - min) == 0 ){
		post("all zero function - watch out!");
		return;
	}
	for( j = 0; j < x->flen; j++ ){	
		x->tempeh[j] = -1.0 + ( (x->tempeh[j] - min) / (max - min) ) * 2.0 ;
	}
	// put tempeh into waveshape function
	for( j = 0; j < x->flen; j++ ){	
		x->wavetab[j] = x->tempeh[j];
	}
}




void waveshape_perform64(t_waveshape *x, t_object *dsp64, double **ins, 
                         long numins, double **outs,long numouts, long n,
                         long flags, void *userparam)
{
	t_double insamp; // , waveshape, ingain ;
	int windex ;
	
	t_double *in = ins[0];
	t_double *out = outs[0];
	int flenm1 = x->flen - 1;
	t_double *wavetab = x->wavetab;
	
	while (n--) { 
		insamp = *in++;
		if(insamp > 1.0){
			insamp = 1.0;
		}
		else if(insamp < -1.0){
			insamp = -1.0;
		}
		windex = ((insamp + 1.0)/2.0) * (float)flenm1 ;
		*out++ = wavetab[windex] ;
	}
}

void waveshape_dsp64(t_waveshape *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if(!sr)
        return;
    object_method(dsp64, gensym("dsp_add64"),x,waveshape_perform64,0,NULL);
}