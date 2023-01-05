
#include "MSPd.h"

#define F_LEN 16384
#define MAX_DELAY_TIME 60000.0 // in seconds

#define OBJECT_NAME "vdel~"

void *vdel_class;


typedef struct _vdel
{
    t_pxobject l_obj;
    //
    t_double R;
    t_double *sinetab;
    t_double si_factor;
    t_double osc1_phs;
    t_double osc1_si;
    t_double si1;
    
    t_double speed;
    t_double feedback;
    t_double depth;
    t_double maxdel;
    t_double *ddl1 ;
    int ddl1_len;
    int ddl1_phs;
    
    t_double tap1;
    
    short feedback_connected;
    short speed_connected;
    short depth_connected;
    short feedback_protect;
    short delay_connected;
    short mute_me;
    short init;
    
} t_vdel;


void vdel_protect(t_vdel *x, double state);
void *vdel_new(t_symbol *s, int argc, t_atom *argv);
void vdel_assist(t_vdel *x, void *b, long m, long a, char *s);
void vdel_float(t_vdel *x, double f);
void vdel_init(t_vdel *x, short initialized);
void vdel_free(t_vdel *x);
void vdel_perform64(t_vdel *x, t_object *dsp64, double **ins,
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam);
void vdel_dsp64(t_vdel *x, t_object *dsp64, short *count, double sr, long n, long flags);
int C74_EXPORT main(void)
{
    t_class *c;
	c = class_new("el.vdel~", (method)vdel_new, (method)vdel_free, sizeof(t_vdel), 0,A_GIMME, 0);
    class_addmethod(c, (method)vdel_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)vdel_float, "float", A_FLOAT, 0);
    class_addmethod(c, (method)vdel_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)vdel_protect, "protect", A_CANT, 0);
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    vdel_class = c;
	potpourri_announce(OBJECT_NAME);
	return 0;
}



void vdel_perform64(t_vdel *x, t_object *dsp64, double **ins,
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
	// DSP config
	t_double *in1 = ins[0];
	t_double *speed_vec = ins[1];
	t_double *depth_vec = ins[2];
	t_double *feedback_vec = ins[3];
	t_double *delay_vec = ins[4];
	t_double *out1 = outs[0];
    
	t_double fdelay;
	int idelay1;
	t_double insamp1; //, insamp2;
	t_double feedsamp;
	t_double frac;
	int index1, index2;
	t_double m1, m2;
    
	t_double *ddl1 = x->ddl1;
	t_double *sinetab = x->sinetab;
	int ddl1_phs = x->ddl1_phs;
	int ddl1_len = x->ddl1_len;
	t_double osc1_phs = x->osc1_phs;
	t_double osc1_si = x->osc1_si;
	t_double tap1 = x->tap1;
	t_double feedback = x->feedback;
	short feedback_connected = x->feedback_connected;
	short speed_connected = x->speed_connected;
	short depth_connected = x->depth_connected;
	t_double si_factor = x->si_factor;
	short feedback_protect = x->feedback_protect;
	short delay_connected = x->delay_connected;
	t_double depth = x->depth;
    
    
    while( n-- ){
        
        // Pull Data off Signal buffers
        insamp1 = *in1++;
        if( feedback_connected ){
            feedback = *feedback_vec++;
            if( feedback_protect ) {
                if( feedback > 0.99)
                    feedback = 0.99;
                if( feedback < -0.99 )
                    feedback = -0.99;
            }
        }
        
        if ( delay_connected) {
            fdelay = *delay_vec++ * (float) ddl1_len;
            if (fdelay < 1. )
                fdelay = 1.;
            if( fdelay > ddl1_len - 1 )
                fdelay = ddl1_len - 1;
        }
        else {
            if( speed_connected ){
                osc1_si = *speed_vec++ * si_factor;
            }
            
            if( depth_connected ){
                depth = *depth_vec++;
                if( depth < .0001 ){
                    depth = .0001;
                }
                if( depth > 1. ){
                    depth = 1.;
                }
                
            }
            fdelay = sinetab[ (int) osc1_phs ] * (float) ddl1_len * depth;
            
            osc1_phs += osc1_si;
            while( osc1_phs >= F_LEN )
                osc1_phs -= F_LEN;
            while( osc1_phs < 0 )
                osc1_phs += F_LEN;
            
        }
        feedsamp = tap1 * feedback;
        if(feedsamp < 0.000001  && feedsamp > -0.000001 )
            feedsamp = 0.0;
        ddl1[ ddl1_phs++ ] = insamp1 + feedsamp;
        ddl1_phs = ddl1_phs % ddl1_len;
        // linear interpolated lookup
        idelay1 = fdelay;
        index1 = (ddl1_phs + idelay1) % ddl1_len;
        index2 = (index1 + 1) % ddl1_len ;
        frac = fdelay - idelay1 ;
        m1 = 1. - frac;
        m2 = frac;
        tap1 = m1 * ddl1[ index1 ] + m2 * ddl1[ index2 ];
        
        *out1++ = tap1;
    }
    x->ddl1_phs = ddl1_phs;
    x->osc1_phs = osc1_phs;
    x->depth = depth;
    x->feedback = feedback;
    x->osc1_si = osc1_si;
    x->tap1 = tap1;
	
}



void *vdel_new(t_symbol *s, int argc, t_atom *argv)
{
    t_vdel *x = (t_vdel *)object_alloc(vdel_class);
    
    x->R = 0; // force init on first DSP call
    x->init = 0; // first time only
    // DSP CONFIG
    dsp_setup((t_pxobject *)x,5);
    outlet_new((t_object *)x, "signal");
    
    // SET DEFAULTS
    x->maxdel = 50.0; // milliseconds
    x->feedback = 0.0;
    x->speed = 0.1;
    x->depth = .25;
    
    if( argc > 0 )
        x->maxdel = atom_getfloatarg(0,argc,argv)/1000.0;
    if( argc > 1 )
        x->speed = atom_getfloatarg(1,argc,argv);
    if( argc > 2 )
        x->depth = atom_getfloatarg(2,argc,argv);
    if( argc > 3 )
        x->feedback = atom_getfloatarg(3,argc,argv);
    
    return x;
}

void vdel_init(t_vdel *x, short initialized)
{
    int i;
    x->si_factor = (t_double)F_LEN/x->R ;
    x->ddl1_phs = 0;
    x->osc1_si = x->si_factor * x->speed ;
    x->osc1_phs = 0;
    x->ddl1_len = x->maxdel * x->R;
    x->tap1 = 0;
    
    if(!initialized){
        x->feedback_protect = 1;
        if( x->maxdel < .00001 ){
            x->maxdel = .00001;
        }
        if( x->maxdel > MAX_DELAY_TIME ){
            x->maxdel = MAX_DELAY_TIME;
        }
        if( x->depth < 0)
            x->depth = 0;
        if( x->depth > 1)
            x->depth = 1;
        if( x->feedback < -.999)
            x->feedback = -.999;
        if( x->feedback > 0.999)
            x->feedback = 0.999;
        x->ddl1 = (t_double *) sysmem_newptrclear((x->ddl1_len + 20)*sizeof(t_double));
        x->sinetab = (t_double *) sysmem_newptrclear(F_LEN * sizeof(t_double));
        
        for(i = 0; i < F_LEN ; i++){
            x->sinetab[i] = 0.5 - 0.49 * cos(TWOPI * (t_double) i / (t_double) F_LEN);
        }
    } else {
        x->ddl1 = (t_double *) sysmem_resizeptr(x->ddl1,(x->ddl1_len + 20)*sizeof(t_double));
    }
}
void vdel_free(t_vdel *x)
{
	dsp_free((t_pxobject *)x);
	sysmem_freeptr(x->sinetab);
	sysmem_freeptr(x->ddl1);
}

void vdel_dsp64(t_vdel *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    // DSP CONFIG
    x->speed_connected = count[1];
    x->depth_connected = count[2];
    x->feedback_connected = count[3];
    x->delay_connected = count[4];
    
    if(x->R != sr){
        x->R = sr;
        vdel_init(x,x->init);
        x->init = 1; // flag that we are initialized
    }
    if(!x->R){
        error("zero sampling rate!");
        return;
    }
    object_method(dsp64, gensym("dsp_add64"),x,vdel_perform64,0,NULL);
}

void vdel_float(t_vdel *x, double f) // Look at floats at inlets
{
    int inlet = ((t_pxobject*)x)->z_in;
	// post("float: inlet %d val %f", inlet, f);
    if (inlet == 1){
        x->speed = f;
        x->osc1_si = f * x->si_factor;
    }
    else if (inlet == 2){
        x->depth = f;
        if( x->depth < 0 )
            x->depth = 0;
        if( x->depth > 1 )
            x->depth = 1;
    }
    else if (inlet == 3){
        x->feedback = f;
        if( x->feedback_protect ) {
            if( x->feedback > 0.99)
                x->feedback = 0.99;
            if( x->feedback < -0.99 )
                x->feedback = -0.99;
        }
    } 
}

void vdel_protect(t_vdel *x, double state)
{
    x->feedback_protect = state;
}

void vdel_assist(t_vdel *x, void *b, long msg, long arg, char *dst)
{
    if (msg==1) {
        switch (arg) {
            case 0:
                sprintf(dst,"(signal) Input ");
                break;
            case 1:
                sprintf(dst,"(signal/float) Speed ");
                break;
            case 2:
                sprintf(dst,"(signal/float) Depth ");
                break;
            case 3:
                sprintf(dst,"(signal/float) Feedback ");
                break;
            case 4:
                sprintf(dst,"(signal) Delay Time Override ");
                break;
        }
    } else if (msg==2) {
        switch (arg) {
            case 0:
                sprintf(dst,"(signal) Output ");
                break;
                
        }
    }
}
