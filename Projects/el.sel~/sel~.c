#include "MSPd.h"

static t_class *sel_class;

#define MAXBEATS (256)
#define OBJECT_NAME "sel~"
#define COMPILE_DATE "9.02.07"
#define OBJECT_VERSION "2.01"
// #define DATE "prerelease"

typedef struct _sel
{
	t_pxobject x_obj;
	t_double *matches; // store numbers to match against
//	float *trigger_vec; // copy of input vector
	t_double length; // number of matches to check
} t_sel;

void *sel_new(t_symbol *msg, short argc, t_atom *argv);
void *sel_beatlist(t_sel *x, t_symbol *msg, short argc, t_atom *argv);
void sel_assist(t_sel *x, void *b, long m, long a, char *s);
void sel_free(t_sel *x);
void sel_mute(t_sel *x, t_floatarg f);
void sel_perform64(t_sel *x, t_object *dsp64, double **ins, 
                   long numins, double **outs,long numouts, long n,
                   long flags, void *userparam);
void sel_dsp64(t_sel *x, t_object *dsp64, short *count, double sr, long n, long flags);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.sel~", (method)sel_new, (method)sel_free, sizeof(t_sel), 0,A_GIMME, 0);
	class_addmethod(c,(method)sel_assist,"assist",A_CANT,0);
    class_addmethod(c,(method)sel_dsp64,"dsp64",A_CANT,0);
	potpourri_announce(OBJECT_NAME);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	sel_class = c;	return 0;
}


void sel_assist (t_sel *x, void *b, long msg, long arg, char *dst)
{
	if (msg==ASSIST_INLET) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Test Value"); break;
		}
	} 
	else if (msg==ASSIST_OUTLET) {
		// run through each outlet algorithmically
		sprintf(dst,"(signal) Click if Matches %f",x->matches[arg]);
	}
}

void *sel_new(t_symbol *msg, short argc, t_atom *argv)
{
	int i;

	t_sel *x = (t_sel *)object_alloc(sel_class);
	
	x->length = argc;
	dsp_setup((t_pxobject *)x,1);
	
	for(i=0;i< x->length ;i++)
		outlet_new((t_pxobject *)x, "signal");  
	x->x_obj.z_misc |= Z_NO_INPLACE;


	x->matches = (t_double *) sysmem_newptr(x->length * sizeof(double));
	
	for(i = 0; i < argc; i++){
		x->matches[i] = (double)atom_getfloatarg(i,argc,argv);
	}

	return x;
}

void sel_free(t_sel *x)
{
	dsp_free((t_pxobject *) x);
	sysmem_freeptr(x->matches);	
}

void sel_perform64(t_sel *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
    int i, j;
	t_double *inlet = ins[0];
	t_double *match_outlet;
	t_double *matches = x->matches;
	int length = x->length;
	
    // clean each outlet
	for(j = 0; j < length; j++){
		match_outlet = (t_double *) outs[j];
		for(i = 0; i < n; i++){
			match_outlet[i] = 0.0;
		}
	}
    // now match and route any clicks in the input
	for(i = 0; i < n; i++){
		if(inlet[i]){
			for(j = 0; j < length; j++){
				if( inlet[i] == matches[j]){
					match_outlet = outs[j];
					match_outlet[i] = 1.0; // always send a unity click
				}
			}
		}
	}
}




void sel_dsp64(t_sel *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if(!sr)
        return;
    object_method(dsp64, gensym("dsp_add64"),x,sel_perform64,0,NULL);
}
