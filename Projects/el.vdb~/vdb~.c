#include "MSPd.h"

#define OBJECT_NAME "vdb~"

static t_class *vdb_class;

typedef struct
{
	float coef;
	float cutoff;
	float x1;
} t_lpf;

typedef struct _vdb
{
    t_pxobject x_obj;
	// 
	t_double sr;
	t_lpf lpf;
	short filter;
	//
	t_double speed;
	t_double feedback;
	t_double delay_time;
	t_double delay_samps;
	// float maxdel;
	long len; // framelength of buffer
	long phs; // current phase

	t_double tap;
	short *connections;
	// 
	short feedback_protect;
	short mute;
	
	short inf_hold;
	t_buffer_ref *delay_buffer_ref;

	int inlet_count;
	int outlet_count;
	int delay_inlet;
	int feedback_inlet;
	short verbose;
    long inf_hold_phs; // keep a running phase when infinite hold is engaged
	void *phase_outlet; // float outlet for playback phase
    /* ATTRIBUTES */
	t_symbol *buffername_attr; //name of the buffer
    double maxdelay_attr; // maximum delay specified in milliseconds ( must be > 0 and <= bsize )
    long chans_attr; // expected number of channels in buffer
//    long interpolate_attr; // flag to interpolate or not
} t_vdb;

t_int *vdb_perform(t_int *w);

void vdb_protect(t_vdb *x, t_floatarg state);
void vdb_inf_hold(t_vdb *x, t_floatarg state);
void *vdb_new(t_symbol *s, int argc, t_atom *argv);
void vdb_assist(t_vdb *x, void *b, long m, long a, char *s);
void vdb_float(t_vdb *x, double f); // MaxMSP only
void vdb_mute(t_vdb *x, t_floatarg t);
void vdb_show(t_vdb *x);
void vdb_coef(t_vdb *x, t_floatarg f);
void vdb_filter(t_vdb *x, t_floatarg t);
void vdb_init(t_vdb *x,short initialized);
void vdb_free(t_vdb *x);
void vdb_perform64(t_vdb *x, t_object *dsp64, double **ins, 
                   long numins, double **outs,long numouts, long n,
                   long flags, void *userparam);
void vdb_dsp64(t_vdb *x, t_object *dsp64, short *count, double sr, long n, long flags);
void vdb_int(t_vdb *x, long f);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.vdb~", (method)vdb_new, (method)dsp_free, sizeof(t_vdb), 0,A_GIMME, 0);
	
    class_addmethod(c, (method)vdb_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)vdb_assist,"assist",A_CANT,0);
	class_addmethod(c,(method)vdb_protect,"protect",A_FLOAT,0);
	class_addmethod(c,(method)vdb_inf_hold,"inf_hold",A_FLOAT,0);
	class_addmethod(c,(method)vdb_mute, "mute", A_FLOAT, 0);
    class_addmethod(c,(method)vdb_float, "float", A_FLOAT, 0);
    class_addmethod(c,(method)vdb_int, "int", A_LONG, 0);
	class_addmethod(c,(method)vdb_show, "show", 0);
    
    CLASS_ATTR_SYM(c,"buffername",0, t_vdb, buffername_attr);
    CLASS_ATTR_DOUBLE(c,"maxdelay",0, t_vdb, maxdelay_attr);
    CLASS_ATTR_LONG(c,"chans",0, t_vdb, chans_attr);
//    CLASS_ATTR_LONG(c,"interpolate",0, t_vdb, interpolate_attr);
    class_dspinit(c);
	class_register(CLASS_BOX, c);
	vdb_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}


void vdb_mute(t_vdb *x, t_floatarg t)
{
	x->mute = (short)t;
}


void vdb_inf_hold(t_vdb *x, t_floatarg state)
{
	x->inf_hold = (short) state;
}

void vdb_filter(t_vdb *x, t_floatarg t)
{
	x->filter = (float)t;
}

void vdb_coef(t_vdb *x, t_floatarg f)
{
	x->lpf.coef = (float)f;
}

void vdb_show(t_vdb *x)
{
	post("feedback %f delay %f",x->feedback, x->delay_time);
}

void vdb_perform64(t_vdb *x, t_object *dsp64, double **ins,
                   long numins, double **outs,long numouts, long n,
                   long flags, void *userparam)
{
    t_double fdelay;
    t_double insamp;
    t_double outsamp;
	t_double feedsamp;
    t_double frac;
    // need to protect buffer here
    t_buffer_obj *dbuf;
    float *delay_line;
	
	int phs = x->phs;
    long inf_hold_phs = x->inf_hold_phs;
	
	t_double feedback = x->feedback;
	short *connections = x->connections;
	t_double sr = x->sr;
	short feedback_protect = x->feedback_protect;
    //	short interpolate_attr = x->interpolate_attr;
	
	short inf_hold = x->inf_hold;
	t_double x1,x2;
	int idelay;
	int dphs,dphs1,dphs2;
	long b_nchans;
    long b_frames;
    long maxdelay_samps;
    long ochans = x->chans_attr;
 	int delay_inlet = ochans;
 	int feedback_inlet = ochans + 1;
	t_int i,j;
	t_double *input;
	t_double *output;
	t_double *delay_vec;
	t_double *feedback_vec;
	
	/**********************/
	
	if( x->mute || x->x_obj.z_disabled) {
		for(i = 0; i < ochans; i++){
			output = outs[i];
			for(j = 0; j < n; j++){
				*output++ = 0.0;
			}
		}
		return;
	}
    
    if(!x->delay_buffer_ref){
        x->delay_buffer_ref = buffer_ref_new((t_object *)x, x->buffername_attr);
    } else {
        buffer_ref_set(x->delay_buffer_ref, x->buffername_attr);
    }
    dbuf = buffer_ref_getobject(x->delay_buffer_ref);
    // bad buffer
    if(dbuf == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(dbuf);
    
    if(b_nchans != ochans){
        post("channel mismatch");
        return;
    }
    
    b_frames = buffer_getframecount(dbuf);
    delay_line = buffer_locksamples(dbuf);
    maxdelay_samps = x->maxdelay_attr * x->sr * 0.001;
    if(maxdelay_samps <= 0 || maxdelay_samps > b_frames - 1 ){
        maxdelay_samps = b_frames - 1;
    }
    // x->maxdelay_len = b_frames - 1;
    // empty buffer
    if(! b_frames){
        return;
    }
	
	phs = x->phs;
	fdelay = x->delay_time * .001 * sr;
	feedback = x->feedback;
	delay_vec = ins[delay_inlet];
	feedback_vec = ins[feedback_inlet];
	/* calculate phase (in ms. actual time in buffer) once per vector */
    
    
	dphs = phs - x->delay_time;
	while(dphs >= maxdelay_samps){
		dphs -= maxdelay_samps;
	}
	while(dphs < 0){
		dphs += maxdelay_samps;
	}
	
	outlet_float(x->phase_outlet, (double)((dphs / sr) * 1000.0) );
	
	/* now do DSP loop */
    if(inf_hold){
        for(i = 0; i < b_nchans; i++){
            phs = x->phs; // reset for each channel
            inf_hold_phs = x->inf_hold_phs;
            output = outs[i]; // select appropriate signal outlet
            for(j = 0; j < n; j++){
                ++inf_hold_phs;
                while(inf_hold_phs >= maxdelay_samps){
                    inf_hold_phs -= maxdelay_samps;
                }
                while(inf_hold_phs < 0){
                    inf_hold_phs += maxdelay_samps;
                }
                output[j] = delay_line[inf_hold_phs * b_nchans + i];
                // might not need this bit
                ++phs;
                while(phs >= maxdelay_samps){
                    phs -= maxdelay_samps;
                }
                while(phs < 0){
                    phs += maxdelay_samps;
                }
            }
        }

    }
    else {
        for(i = 0; i < b_nchans; i++){
            input = ins[i];
            output = outs[i];
            phs = x->phs; // reset for each channel
            for(j = 0; j < n; j++){
                if ( connections[delay_inlet]) {
                    fdelay = delay_vec[j];
                    fdelay *= .001 * sr;
                    if (fdelay < 1. )
                        fdelay = 1.;
                    if( fdelay > maxdelay_samps - 1 )
                        fdelay = maxdelay_samps - 1;
                    x->delay_time = fdelay;
                }
                // will not need this if statement soon
                if(! inf_hold ){
                    if(connections[feedback_inlet]){
                        feedback = feedback_vec[j];
                        if( feedback_protect ) {
                            if( feedback > 0.99)
                                feedback = 0.99;
                            if( feedback < -0.99 )
                                feedback = -0.99;
                        }
                        x->feedback = feedback;
                    }
                }
                
                idelay = floor(fdelay);
                /* put sample of phase to float outlet - just once */
                if(phs < 0 || phs >= maxdelay_samps){
                    // error("%s: bad phase %d",OBJECT_NAME,phs);
                    phs = 0;
                }
                
                
                frac = (fdelay - idelay);
                dphs1 = phs - idelay;
                dphs2 = dphs1 - 1;
                
                while(dphs1 >= maxdelay_samps){
                    dphs1 -= maxdelay_samps;
                }
                while(dphs1 < 0){
                    dphs1 += maxdelay_samps;
                }
                while(dphs2 >= maxdelay_samps){
                    dphs2 -= maxdelay_samps;
                }
                while(dphs2 < 0){
                    dphs2 += maxdelay_samps;
                }
                inf_hold_phs = dphs1; // save this in case of infinite hold
                x1 = delay_line[dphs1 * b_nchans + i];
                x2 = delay_line[dphs2 * b_nchans + i];
                outsamp = x1 + frac * (x2 - x1);
                
                output[j] = outsamp;
                if(! inf_hold ){
                    insamp = input[j];
                    feedsamp = outsamp * feedback;
                    if(feedsamp < 0.000001  && feedsamp > -0.000001 )
                        feedsamp = 0.0;
                    delay_line[phs * b_nchans + i] = insamp + feedsamp;
                }
                ++phs;
                while(phs >= maxdelay_samps){
                    phs -= maxdelay_samps;
                }
                while(phs < 0){
                    phs += maxdelay_samps;
                }	
            }
        }
    }
	x->phs = phs;
    x->inf_hold_phs = inf_hold_phs;
    buffer_unlocksamples(dbuf);
    if(! inf_hold ){
        object_method( (t_object *)dbuf, gensym("dirty") );
    }
}

void *vdb_new(t_symbol *s, int argc, t_atom *argv)
{
	int i;
	t_vdb *x;

	x = (t_vdb *)object_alloc(vdb_class);


    attr_args_process(x, argc, argv);
    
	x->inlet_count = x->chans_attr + 2;
	x->outlet_count = x->chans_attr;
	x->delay_inlet = x->chans_attr;
	x->feedback_inlet = x->delay_inlet + 1;

	x->phase_outlet = floatout((t_pxobject *)x); // right outlet	
	dsp_setup((t_pxobject *)x,x->inlet_count);
	for(i = 0; i < x->outlet_count; i++){
		outlet_new((t_object *)x, "signal");
	}
    x->x_obj.z_misc |= Z_NO_INPLACE;
	vdb_init(x,0);
	return x;
}

void vdb_free(t_vdb *x)
{
	dsp_free((t_pxobject *)x);
    sysmem_freeptr(x->connections);
}


void vdb_init(t_vdb *x,short initialized)
{	
	
	if(!initialized){
//		x->maxdelay_len = x->maxdelay_attr * .001 * x->sr;
		x->feedback_protect = 0;
		x->inf_hold = 0;
		x->phs = 0;
		x->mute = 0;
//		x->always_update = 1;
		x->connections = (short *) sysmem_newptrclear((x->chans_attr + 2) * sizeof(short));// free me!
		x->verbose = 0; // not set anywhere for now
	} 
	
}



void vdb_int(t_vdb *x, long tog)
{
	vdb_float(x,(float)tog);	
}

void vdb_float(t_vdb *x, double df) // Look at floats at inlets
{
	float f = (float)df;
	
	int inlet = x->x_obj.z_in;
	
	if (inlet == x->delay_inlet)
	{
		
		if(f > 0.0 && f < x->maxdelay_attr){
			x->delay_time = f;
			x->delay_samps = f * .001 * x->sr;
			x->tap = 0;
		} else {
			error("%s: delaytime %f out of range [0.0 - %f]",OBJECT_NAME,f,x->maxdelay_attr);
		}
		//		post("delay is now %f",x->delay_time);
		
	}
	else if (inlet == x->feedback_inlet)
	{
		//		post("feedback is now %f",x->feedback);
		x->feedback = f;
		if( x->feedback_protect ) {
			if( x->feedback > 1.0)
				x->feedback = 1.0;
			if( x->feedback < -1.0 )
				x->feedback = -1.0;
		}
	}
	
}

void vdb_protect(t_vdb *x, double state)
{
	x->feedback_protect = state;
}

void vdb_assist(t_vdb *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		if(arg < x->chans_attr ){
			sprintf(dst,"(signal) Input %ld",arg + 1);
		} else  if (arg == x->chans_attr) {
			sprintf(dst,"(signal/float) Delay Time");
		} else if (arg == 1+x->chans_attr) {
			sprintf(dst,"(signal/float) Feedback");
		}
		
	} else if (msg==2) {
		if( arg < x->chans_attr )
			sprintf(dst,"(signal) Output %ld", arg + 1);
		else 
			sprintf(dst,"(float) Read Head Phase");		
	}
}

void vdb_dsp64(t_vdb *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    int i;
    if(!sr)
        return;
    x->sr = sr;
    for(i=0; i < x->chans_attr + 2; i++){
        x->connections[i] = count[i];
    }
    if(!x->delay_buffer_ref){
        x->delay_buffer_ref = buffer_ref_new((t_object *)x, x->buffername_attr);
    } else {
        buffer_ref_set(x->delay_buffer_ref, x->buffername_attr);
    }

    object_method(dsp64, gensym("dsp_add64"),x,vdb_perform64,0,NULL);
}

