#include "MSPd.h"

static t_class *rotapan_class;

#define OBJECT_NAME "rotapan~"

#define MAXFLAMS (16)
#define MAXATTACKS (128)
#define STOPGAIN (.001)

#define RAND_MAX2 (0x7fffffff)

typedef struct _rotapan
	{

		t_pxobject x_obj;
		t_double sr;
        long rchans;
        double pio2;
        double *inarr;
	} t_rotapan;


void *rotapan_new(t_symbol *s, int argc, t_atom *argv);


void rotapan_free(t_rotapan *x);
void rotapan_assist(t_rotapan *x, void *b, long msg, long arg, char *dst);
void rotapan_version(t_rotapan *x);
void rotapan_dsp64(t_rotapan *x, t_object *dsp64, short *count, double sr, long n, long flags);
void rotapan_perform64(t_rotapan *x, t_object *dsp64, double **ins, 
                     long numins, double **outs,long numouts, long n,
                     long flags, void *userparam);


int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.rotapan~", (method)rotapan_new, (method)dsp_free, sizeof(t_rotapan), 0,A_GIMME, 0);
	
    class_addmethod(c, (method)rotapan_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)rotapan_assist,"assist",A_CANT,0);
    class_dspinit(c);
	class_register(CLASS_BOX, c);
	rotapan_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}



void rotapan_assist(t_rotapan *x, void *b, long msg, long arg, char *dst)
{
    int i;
	if (msg==1) {
        for(i = 0; i < x->rchans; i++){
            if( i == arg ) sprintf(dst,"(signal) Audio Input %d", i+1);
        }
        if( x->rchans == arg ) sprintf(dst,"(signal) Pan Controller (0-1)");

	} else if (msg==2) {
        for(i = 0; i < x->rchans; i++){
            if( i == arg ) sprintf(dst,"(signal) Audio Output %d", i+1);
        }
	}
}

void *rotapan_new(t_symbol *s, int argc, t_atom *argv)
{
	int i;
    
    t_rotapan *x = (t_rotapan *)object_alloc(rotapan_class);
    x->rchans = (long) atom_getfloatarg(0,argc,argv);
    dsp_setup((t_pxobject *)x, x->rchans + 1);
    for(i=0; i < x->rchans; i++){
        outlet_new((t_pxobject *)x, "signal");
    }
    x->x_obj.z_misc |= Z_NO_INPLACE;
    x->pio2 = PI / 2.0;
    x->inarr = (double *) malloc((x->rchans + 1) * sizeof(double));
    return x;
}



void rotapan_free(t_rotapan *x)
{
	dsp_free((t_pxobject *) x);	
}

void rotapan_perform64(t_rotapan *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
    
	long rchans = x->rchans;
    t_double pio2 = x->pio2;
    double *inarr = x->inarr;
    double amp1, amp2;
    double panloc;
    double scaledIndex;
	int chan,j;
    int offset;
    
	for( j = 0; j < n; j++){
        for(chan = 0; chan < rchans; chan++){
            inarr[chan] = ins[chan][j];
            outs[chan][j] = 0;
        }
        scaledIndex = ins[rchans][j] * rchans;
        if(scaledIndex < 0.0 || scaledIndex > rchans)
            scaledIndex = 0.0;
        
        offset = (int) floor(scaledIndex) % rchans;
        panloc = (scaledIndex - offset) * pio2;
        
        amp1 = cos( panloc );
        amp2 = sin( panloc );
       
        for(chan = 0; chan < rchans; chan++){
            outs[(chan+offset)%rchans][j] += amp1 * inarr[chan];
            outs[(chan+offset+1)%rchans][j] += amp2 * inarr[chan];
        }
	}
}

void rotapan_dsp64(t_rotapan *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if(!sr)
        return;
    object_method(dsp64, gensym("dsp_add64"),x,rotapan_perform64,0,NULL);
}
