#include "MSPd.h"

/**/


static t_class *epluribus_class;


#define MAXBEATS (256)
#define OBJECT_NAME "epluribus~"
#define COMPILE_DATE "5.3.08"
#define OBJECT_VERSION "2.0"

typedef struct _epluribus
{
	t_pxobject x_obj; 
	int incount; // how many inlets (must be at least 2)
	short inverse; // flag to look for minimum instead
} t_epluribus;

void *epluribus_new(t_symbol *msg, short argc, t_atom *argv);
void epluribus_assist(t_epluribus *x, void *b, long m, long a, char *s);
void epluribus_inverse(t_epluribus *x, t_floatarg tog);
void version(void);
void epluribus_dsp64(t_epluribus *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void epluribus_perform64(t_epluribus *x, t_object *dsp64, double **ins, 
                         long numins, double **outs,long numouts, long n,
                         long flags, void *userparam);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.epluribus~", (method)epluribus_new, (method)dsp_free, sizeof(t_epluribus), 0,A_GIMME, 0);
    
	class_addmethod(c,(method)epluribus_assist,"assist",A_CANT,0);
    class_addmethod(c,(method)epluribus_inverse,"inverse",A_FLOAT,0);
    class_addmethod(c, (method)epluribus_dsp64, "dsp64", A_CANT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	epluribus_class = c;	
    potpourri_announce(OBJECT_NAME);
	return 0;
}


void epluribus_inverse(t_epluribus *x, t_floatarg tog)
{
	x->inverse = (short) tog;
}

void epluribus_assist (t_epluribus *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		sprintf(dst,"(signal) Input %ld", arg + 1); 
	} else if (msg==2) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Selected Samples"); break;
			case 1: sprintf(dst,"(signal) Selected Inlet Number"); break;
		}
		
	}
}

void *epluribus_new(t_symbol *msg, short argc, t_atom *argv)
{

	t_epluribus *x;
	x = (t_epluribus *)object_alloc(epluribus_class);
	x->x_obj.z_misc |= Z_NO_INPLACE;
	x->incount = (int) atom_getfloatarg(0,argc,argv);
	dsp_setup((t_pxobject *)x, x->incount);
	outlet_new((t_pxobject *)x, "signal");
  	outlet_new((t_pxobject *)x, "signal"); 
	if(x->incount < 2 || x->incount > 256 ){
		error("%s: there must be between 2 and 256 input vectors", OBJECT_NAME);
		return (NULL);
	}
	x->inverse = 0; // by default don't do inverse behaviour
	return (x);
}

void epluribus_perform64(t_epluribus *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
    int i,j,k;
	t_double *outlet;
	t_double *selection;
	t_double maxamp = 0.0;
	t_double maxout = 0.0;
	int maxloc;
	
	outlet = outs[0];
	selection = outs[1];
	
	if( x->inverse ){
		for(k = 0; k < n; k ++ ){
			maxamp = 99999999.0;
			maxloc = 0;
			for(i = 0, j=2; i < numins ; i++, j++){
				if( maxamp > fabs( ins[i][k] ) ){
					maxamp = fabs( ins[i][k] ); 
					maxout = ins[i][k]; // don't actually change signal
					maxloc = i + 1; // record location of max amp
				}
			}
			outlet[k] = maxout;
			selection[k] = maxloc;
		}
	} 
	else {
		for(k = 0; k < n; k ++ ){
			maxamp = 0.0;
			maxloc = 0;
			for(i = 0, j=2; i < numins ; i++, j++){
				if( maxamp < fabs( ins[i][k] ) ){
					maxamp = fabs( ins[i][k] );
					maxout = ins[i][k]; // don't actually change signal
					maxloc = i + 1; // record location of max amp
				}
			}
			outlet[k] = maxout;
			selection[k] = maxloc;
		}
	}    
}


void epluribus_dsp64(t_epluribus *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if(!samplerate)
        return;
    
	if( x->incount < 2 || x->incount > 8192 ){
		post("bad vector count");
		return;
	}
    object_method(dsp64, gensym("dsp_add64"),x,epluribus_perform64,0,NULL);
	
}

