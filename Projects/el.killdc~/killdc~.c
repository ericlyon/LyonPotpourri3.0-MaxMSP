#include "MSPd.h"

#define OBJECT_NAME "killdc~"

static t_class *killdc_class;

//#define FUNC_LEN (512)
#define VERSION "2.5"

#define MAXSECTS 4

typedef struct {
  float ps[4][MAXSECTS];
  float c[4][MAXSECTS];
  int nsects ;
  float xnorm;
} COEFS;

typedef struct {
    double ps[4][MAXSECTS];
    double c[4][MAXSECTS];
    int nsects ;
    double xnorm;
} COEFS64;

typedef struct _killdc
{
    t_pxobject x_obj;
    COEFS fdata;
    COEFS64 fdata64;
	short mute;
} t_killdc;

void *killdc_new(void);
t_int *offset_perform(t_int *w);
void killdc_mute(t_killdc *x, t_floatarg state);
void killdc_version(t_killdc *x);
void killdc_assist(t_killdc *x, void *b, long m, long a, char *s);
void killdc_dsp64(t_killdc *x, t_object *dsp64, short *count, double samplerate, long n, long flags);
void killdc_perform64(t_killdc *x, t_object *dsp64, double **ins, 
                      long numins, double **outs,long numouts, long n,
                      long flags, void *userparam);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.killdc~", (method)killdc_new, (method)dsp_free, sizeof(t_killdc), 0, 0);
	
    class_addmethod(c, (method)killdc_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)killdc_assist,"assist",A_CANT,0);
    class_dspinit(c);
	class_register(CLASS_BOX, c);
	killdc_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}


void killdc_mute(t_killdc *x, t_floatarg state)
{
	x->mute = (short)state;	
}

void killdc_assist (t_killdc *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0:
				sprintf(dst,"(signal) Input");
				break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Output");
	}
}

void *killdc_new(void)
{
    t_killdc *x = (t_killdc *)object_alloc(killdc_class);
	int i, j;
/*
	x->fdata.nsects = 3;
	x->fdata.c[0][0] = -1.9999996;
	x->fdata.c[1][0] = -1.9997695;
	x->fdata.c[2][0] =  1.0000000;
	x->fdata.c[3][0] =  0.99977354;
    
	x->fdata.c[0][1] = -1.9999998;
	x->fdata.c[1][1] = -1.9985010;
	x->fdata.c[2][1] =  1.0000000;
	x->fdata.c[3][1] =  0.99851080;
    
	x->fdata.c[0][2] = -1.0000000;
	x->fdata.c[1][2] = -0.98978878;
	x->fdata.c[2][2] =  0.0;
	x->fdata.c[3][2] =  0.0;
	
    for(i=0; i< x->fdata.nsects; i++) {
    	for(j=0;j<4;j++)  {
      		x->fdata.ps[j][i] = 0.0;
    	}
  	}
    
	x->fdata.xnorm = .13669191E+01;
 */
// same for 64 bit
	x->fdata64.nsects = 3;
	x->fdata64.c[0][0] = -1.9999996;
	x->fdata64.c[1][0] = -1.9997695;
	x->fdata64.c[2][0] =  1.0000000;
	x->fdata64.c[3][0] =  0.99977354;
    
	x->fdata64.c[0][1] = -1.9999998;
	x->fdata64.c[1][1] = -1.9985010;
	x->fdata64.c[2][1] =  1.0000000;
	x->fdata64.c[3][1] =  0.99851080;
    
	x->fdata64.c[0][2] = -1.0000000;
	x->fdata64.c[1][2] = -0.98978878;
	x->fdata64.c[2][2] =  0.0;
	x->fdata64.c[3][2] =  0.0;
	
    for(i=0; i< x->fdata64.nsects; i++) {
    	for(j=0;j<4;j++)  {
      		x->fdata64.ps[j][i] = 0.0;
    	}
  	}
    
	x->fdata64.xnorm = .13669191E+01;
    
    dsp_setup((t_pxobject *)x,1);
    outlet_new((t_pxobject *)x, "signal");
    
    return x;
}

// method from Paul Lansky's cmix implementation

void killdc_perform64(t_killdc *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
    t_double sample ;
	int m;
	t_double op;
    /********/
	t_double *in1 = ins[0];
	t_double *out = outs[0];
	
	while (n--) { 
		sample = *in1++;
		
        for(m=0; m< x->fdata64.nsects;m++) {
            op = sample + x->fdata64.c[0][m] * x->fdata64.ps[0][m] +
            x->fdata64.c[2][m] * x->fdata64.ps[1][m]
            - x->fdata64.c[1][m] * x->fdata64.ps[2][m]
            - x->fdata64.c[3][m] * x->fdata64.ps[3][m];
            
            x->fdata64.ps[1][m] = x->fdata64.ps[0][m];
            x->fdata64.ps[0][m] = sample;
            x->fdata64.ps[3][m] = x->fdata64.ps[2][m];
            x->fdata64.ps[2][m] = op;
            sample = op;
        }
		
		if(fabs(sample) < 0.0000001)
			sample = 0.0;
 		*out++ = sample * x->fdata64.xnorm;
	}
}

void killdc_dsp64(t_killdc *x, t_object *dsp64, short *count, double samplerate, long n, long flags)
{
    if(!samplerate)
        return;
    object_method(dsp64, gensym("dsp_add64"),x,killdc_perform64,0,NULL);
}

