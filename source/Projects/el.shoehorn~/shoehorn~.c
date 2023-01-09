#include "MSPd.h"

static t_class *shoehorn_class;

#define OBJECT_NAME "shoehorn~"

typedef struct _shoehorn
{
    
    t_pxobject x_obj;
    long inChans;
    long outChans;
    double pio2;
    double *inarr;
    double advFrac;
    double *pangains1;
    double *pangains2;
    long *indexList;
} t_shoehorn;


void *shoehorn_new(t_symbol *s, int argc, t_atom *argv);
void shoehorn_free(t_shoehorn *x);
void shoehorn_assist(t_shoehorn *x, void *b, long msg, long arg, char *dst);
void shoehorn_dsp64(t_shoehorn *x, t_object *dsp64, short *count, double sr, long n, long flags);
void shoehorn_perform64(t_shoehorn *x, t_object *dsp64, double **ins,
                        long numins, double **outs,long numouts, long n,
                        long flags, void *userparam);


int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.shoehorn~", (method)shoehorn_new, (method)dsp_free, sizeof(t_shoehorn), 0,A_GIMME, 0);
	
    class_addmethod(c, (method)shoehorn_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)shoehorn_assist,"assist",A_CANT,0);
    class_dspinit(c);
	class_register(CLASS_BOX, c);
	shoehorn_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void shoehorn_assist(t_shoehorn *x, void *b, long msg, long arg, char *dst)
{
    int i;
	if (msg==1) {
        for(i = 0; i < x->inChans; i++){
            if( i == arg ) sprintf(dst,"(signal) Audio Input %d", i+1);
        }
	} else if (msg==2) {
        for(i = 0; i < x->outChans; i++){
            if( i == arg ) sprintf(dst,"(signal) Audio Output %d", i+1);
        }
	}
}

void *shoehorn_new(t_symbol *s, int argc, t_atom *argv)
{
	int i;
    double fullFrac, thisFrac, panloc;
    long outIndex;
    t_shoehorn *x = (t_shoehorn *)object_alloc(shoehorn_class);
    x->inChans = (long) atom_getfloatarg(0,argc,argv);
    x->outChans = (long) atom_getfloatarg(1,argc,argv);
    if( x->outChans < 2 || x->inChans < 2){
        object_error((t_object *)x, "%s: illegal channel count: [in = %d] [out = %d]",OBJECT_NAME,x->inChans,x->outChans);
        return NULL;
    }
    dsp_setup((t_pxobject *)x, x->inChans);
    for(i=0; i < x->outChans; i++){
        outlet_new((t_pxobject *)x, "signal");
    }
    x->pio2 = PI / 2.0;
    x->inarr = (double *) malloc(x->inChans * sizeof(double));
    x->pangains1 = (double *) malloc(x->inChans * sizeof(double));
    x->pangains2 = (double *) malloc(x->inChans * sizeof(double));
    x->indexList = (long *) malloc(x->inChans * sizeof(long));
    x->advFrac = (double)(x->outChans - 1)/(double)(x->inChans - 1);
    
    for(i = 1; i < x->inChans - 1; i++){
        fullFrac = i * x->advFrac;
        outIndex = floor(fullFrac);
        thisFrac = fullFrac - outIndex;
        panloc = thisFrac * x->pio2;
        x->indexList[i] = outIndex;
        x->pangains1[i] = cos(panloc);
        x->pangains2[i] = sin(panloc);
    }
    return x;
}



void shoehorn_free(t_shoehorn *x)
{
	dsp_free((t_pxobject *) x);
    free(x->inarr);
    free(x->pangains1);
    free(x->pangains2);
    free(x->indexList);
}

void shoehorn_perform64(t_shoehorn *x, t_object *dsp64, double **ins,
                        long numins, double **outs,long numouts, long n,
                        long flags, void *userparam)
{
    
	long inChans = x->inChans;
    long outChans = x->outChans;
    double *inarr = x->inarr;
    double *pangains1 = x->pangains1;
    double *pangains2 = x->pangains2;
	int chan,j;
    long outIndex;
    long *indexList = x->indexList;
    
	for( j = 0; j < n; j++){
        // copy input channel frame
        for(chan = 0; chan < inChans; chan++){
            inarr[chan] = ins[chan][j];
        }
        // zero out output channels
        for(chan = 1; chan < outChans - 1; chan++){
            outs[chan][j] = 0.0;
        }
        // copy outer channel samples directly
        outs[0][j] = inarr[0];
        outs[outChans - 1][j] = inarr[inChans - 1];
        
        // spread internal input channels to respective output channels
        
        for(chan = 1; chan < inChans - 1; chan++){
            outIndex = indexList[chan];
            outs[outIndex][j] += pangains1[chan] * inarr[chan];
            outs[outIndex+1][j] += pangains2[chan] * inarr[chan];
        }
        
	}
}

void shoehorn_dsp64(t_shoehorn *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if(!sr)
        return;
    object_method(dsp64, gensym("dsp_add64"),x,shoehorn_perform64,0,NULL);
}
