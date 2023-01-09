#include "MSPd.h"

static t_class *rtrig_class;

#define OBJECT_NAME "rtrig~"

#define MAXFLAMS (16)
#define MAXATTACKS (128)
#define STOPGAIN (.001)

#define RAND_MAX2 (0x7fffffff)

typedef struct _rtrig
	{

		t_pxobject x_obj;
		short mute;
		t_double min;
		t_double max;
		t_double odds;
		t_double sr;
	} t_rtrig;


void *rtrig_new(t_symbol *s, int argc, t_atom *argv);
void rtrig_free(t_rtrig *x);
void rtrig_assist(t_rtrig *x, void *b, long msg, long arg, char *dst);
void rtrig_mute(t_rtrig *x, t_floatarg tog);
void rtrig_min(t_rtrig *x, t_floatarg v);
void rtrig_max(t_rtrig *x, t_floatarg v);
void rtrig_odds(t_rtrig *x, t_floatarg v);
void rtrig_density(t_rtrig *x, t_floatarg v);
void rtrig_dsp64(t_rtrig *x, t_object *dsp64, short *count, double sr, long n, long flags);
void rtrig_perform64(t_rtrig *x, t_object *dsp64, double **ins, 
                     long numins, double **outs,long numouts, long n,
                     long flags, void *userparam);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.rtrig~", (method)rtrig_new, (method)dsp_free, sizeof(t_rtrig), 0,A_GIMME, 0);	
    class_addmethod(c, (method)rtrig_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)rtrig_assist,"assist",A_CANT,0);
    class_addmethod(c,(method)rtrig_min, "min", A_FLOAT, 0);
    class_addmethod(c,(method)rtrig_max, "max", A_FLOAT, 0);
    class_addmethod(c,(method)rtrig_odds, "odds", A_FLOAT, 0);
	class_addmethod(c,(method)rtrig_density, "density", A_FLOAT, 0);
    class_addmethod(c,(method)rtrig_mute, "mute", A_FLOAT, 0);
    class_dspinit(c);
	class_register(CLASS_BOX, c);
	rtrig_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void rtrig_mute(t_rtrig *x, t_floatarg tog)
{
	x->mute = (short) tog;
}

void rtrig_min(t_rtrig *x, t_floatarg v)
{
	x->min = (float) v;
}

void rtrig_max(t_rtrig *x, t_floatarg v)
{
	x->max = (float) v;
}

void rtrig_odds(t_rtrig *x, t_floatarg v)
{
	x->odds = (float) v;
}

void rtrig_density(t_rtrig *x, t_floatarg v)
{
	if(x->sr > 0.0){
		x->odds = (float) v / x->sr;
	}
}

void rtrig_assist(t_rtrig *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Unused"); break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Triggers");
	}
	
}

void *rtrig_new(t_symbol *s, int argc, t_atom *argv)
{
	
    t_rtrig *x = (t_rtrig *)object_alloc(rtrig_class);
    dsp_setup((t_pxobject *)x,1);	
    outlet_new((t_pxobject *)x, "signal");
    
    x->mute = 0;
	x->min = atom_getfloatarg(0,argc,argv);
	x->max = atom_getfloatarg(1,argc,argv);
	x->odds = atom_getfloatarg(2,argc,argv);
	x->sr = sys_getsr();
	srand(time(0));
    return (x);
}



void rtrig_free(t_rtrig *x)
{
	dsp_free((t_pxobject *) x);	
}

void rtrig_perform64(t_rtrig *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
	t_double *out_vec = outs[0];
	
	t_double rval;
	t_double min = x->min;
	t_double max = x->max;
	t_double odds = x->odds;
	
	
	while( n-- ){
		rval = (t_double) rand() / (t_double) RAND_MAX2;
		if(rval < odds){
			rval = min + (max-min) * ((t_double) rand() / (t_double) RAND_MAX2);
			*out_vec++ = rval;
		} else {
			*out_vec++ = 0.0;
		}
	}
}

void rtrig_dsp64(t_rtrig *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if(!sr)
        return;
    x->sr = sr;
    object_method(dsp64, gensym("dsp_add64"),x,rtrig_perform64,0,NULL);
}
