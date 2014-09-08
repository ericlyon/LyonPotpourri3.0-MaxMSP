
#include "MSPd.h"

// #define F_LEN 16384
#define MAX_DELAY_TIME 36000000.0 // in seconds
#define OBJECT_NAME "vdp~"


///
// added inf_hold 11.6.06
// pointer version of vd~
// 5.13.06 added error checking on input data, memset for mute
// 5.20.07 added protection from denormals
// 6.16.07 tested alternative denormals method and found it lacking
// 6.16.07 fixed initial delay time so it can be stated in milliseconds
// 12.21.09 adding a soft infinite hold with specified fade time

static t_class *vdp_class;

typedef struct
{
	t_double coef;
	t_double cutoff;
	t_double x1;
} t_lpf;

typedef struct _vdp
{
    t_pxobject x_obj;
	t_double sr;
	t_lpf lpf;
	short filter;
	t_double speed;
	t_double feedback;
	t_double delay_time;
	t_double delay_samps;
	t_double maxdel;
	t_double *delay_line;
	t_double *write_ptr; // location to write current input
	t_double *startmem; // first address in delay line
	t_double *endmem; // last address to read in delay line
	int len;
	int phs;
	
	
	t_double tap;
	short connections[4];
	
	short feedback_protect;
	short mute;
	short interpolate;
	short inf_hold;
	/* copy to buffer */
	t_buffer_ref *destbuf; /* for copying to another buffer */
    t_symbol *destbuf_name;
	/* tapering */
	long taper_count;
	t_double taper_feedback;
} t_vdp;

t_int *vdp_perform(t_int *w);

void vdp_protect(t_vdp *x, double state);
void *vdp_new(t_symbol *s, int argc, t_atom *argv);
void vdp_assist(t_vdp *x, void *b, long m, long a, char *s);
void vdp_float(t_vdp *x, double f);
void vdp_mute(t_vdp *x, double t);
void vdp_interpolate(t_vdp *x, long t);
void vdp_show(t_vdp *x);
void vdp_coef(t_vdp *x, t_double f);
void vdp_filter(t_vdp *x, long t);
void vdp_init(t_vdp *x,short initialized);
void vdp_clear(t_vdp *x);
void vdp_inf_hold(t_vdp *x,double state);
void vdp_copy_to_buffer(t_vdp *x, t_symbol *msg, short argc, t_atom *argv);
void vdp_setdestbuf(t_vdp *x, t_symbol *wavename);
void vdp_free(t_vdp *x);
void vdp_perform64(t_vdp *x, t_object *dsp64, double **ins, 
                   long numins, double **outs,long numouts, long n,
                   long flags, void *userparam);
void vdp_dsp64(t_vdp *x, t_object *dsp64, short *count, double sr, long n, long flags);

//MSP only
void vdp_int(t_vdp *x, long f);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.vdp~", (method)vdp_new, (method)dsp_free, sizeof(t_vdp), 0,A_GIMME,0);
	
    class_addmethod(c,(method)vdp_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c,(method)vdp_assist,"assist",A_CANT,0);
    class_addmethod(c,(method)vdp_protect,"protect",A_DEFFLOAT,0);
	class_addmethod(c,(method)vdp_filter, "filter", A_LONG, 0);
	class_addmethod(c,(method)vdp_coef, "coef", A_FLOAT, 0);
    class_addmethod(c,(method)vdp_float, "float", A_FLOAT, 0);
	class_addmethod(c,(method)vdp_show, "show", 0);
	class_addmethod(c,(method)vdp_clear, "clear", 0);
	class_addmethod(c,(method)vdp_inf_hold, "inf_hold", A_FLOAT, 0);
	class_addmethod(c,(method)vdp_interpolate, "interpolate", A_LONG, 0);
	class_addmethod(c,(method)vdp_copy_to_buffer, "copy_to_buffer", A_GIMME, 0);

    class_dspinit(c);
	class_register(CLASS_BOX, c);
	vdp_class = c;
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void vdp_mute(t_vdp *x, double t)
{
	x->mute = (short)t;
}

void vdp_inf_hold(t_vdp *x, double t)
{
	x->inf_hold = (short)t;
	x->taper_feedback = 1.0;
	x->taper_count = 0;
}


void vdp_filter(t_vdp *x, long t)
{
	x->filter = t;
}

void vdp_coef(t_vdp *x, t_double f)
{
	x->lpf.coef = f;
}

void vdp_show(t_vdp *x)
{
	post("feedback %.4f delay %.4f",x->feedback, x->delay_time * 1000.0); // convert to ms
}

void vdp_interpolate(t_vdp *x, long t)
{
	x->interpolate = (short)t;
}

void vdp_perform64(t_vdp *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
    t_double *input = ins[0];
    t_double *delay_vec = ins[1];
    t_double *feedback_vec = ins[2];
    t_double *output = outs[0];
    
    t_double fdelay;
    t_double insamp;
    t_double outsamp = 0.0;
	t_double feedsamp;
    t_double frac;
	t_double *write_ptr = x->write_ptr;
	t_double *read_ptr;
	t_double *startmem = x->startmem;
	t_double *endmem = x->endmem;
	int len = x->len;
	t_double tap = x->tap;
	t_double feedback = x->feedback;
	t_double delay_samps = x->delay_samps;
	short *connections = x->connections;
	t_double sr = x->sr;
	t_double msr = sr * 0.001;
	short feedback_protect = x->feedback_protect;
	short interpolate = x->interpolate;
	t_lpf lpf = x->lpf;
	short filter = x->filter;
	t_double x1,x2;
	int idelay;
	short inf_hold = x->inf_hold;
	
	
	/**********************/
	
	
	fdelay = delay_samps;
	idelay = floor(fdelay);
	
	/* loop only for infinite hold */
	
	if(inf_hold){
		while( n-- ){		
			read_ptr = write_ptr;
			outsamp = *read_ptr;
			// no interpolation is why infinite hold is cheaper
			
			// *write_ptr++;
            write_ptr++;
			
			if( write_ptr >= endmem ){
				write_ptr = startmem;
			}
			*output++ = outsamp;
		}
		x->write_ptr = write_ptr;
		x->delay_samps = fdelay;
		return;
	}
	
	
	/* normal main loop*/
	while( n-- ){
		
		// Pull Data off Signal buffers
		insamp = *input++;
		
		
		if ( connections[1]) {
			fdelay = *delay_vec++ * msr; // convert delay from milliseconds to samples
			if (fdelay < 0.0 )
				fdelay = 0.0;
			if( fdelay >= len )
				fdelay = len - 1;
			idelay = floor(fdelay);
		} 
		
		if(connections[2]){
			feedback = *feedback_vec++;
			if( feedback_protect ) {
				if( feedback > 0.99)
					feedback = 0.99;
				if( feedback < -0.99 )
					feedback = -0.99;
			}
			
		} 
		
		
		/* make fdelay behave */
		if(fdelay < 0.0){
			fdelay = 0.0;
		}
		else if(fdelay >= len){
			fdelay = len - 1;
		}	
		idelay = floor(fdelay);
		
		if(interpolate){
			frac = (fdelay - idelay);
			read_ptr = write_ptr - idelay;
			if( read_ptr < startmem ){
				read_ptr += len;
			}
			x1 = *read_ptr--;
			if( read_ptr < startmem ){
				read_ptr += endmem - startmem;
			}		
			x2 = *read_ptr;
			outsamp = x1 + frac * (x2 - x1);
			
		} 
		else { // no interpolation case 
			read_ptr = write_ptr - idelay;
			if( read_ptr < startmem ){
				read_ptr += len;
			}	
			outsamp = *read_ptr;
			
		}
		if(filter){
			outsamp += lpf.x1 * lpf.coef;
			outsamp /= (1.0+lpf.coef);
			lpf.x1 = outsamp;
		}
		
		
		feedsamp = outsamp * feedback;
		
		if(feedsamp < 0.000001  && feedsamp > -0.000001 ){
			feedsamp = 0.0;
		}
		
		*write_ptr++ = insamp + feedsamp;
		
		if( write_ptr >= endmem ){
			write_ptr = startmem;
		}
		
		*output++ = outsamp;
	}
	
	x->tap = tap;
	x->feedback = feedback;
	x->delay_time = fdelay;
	x->delay_samps = fdelay;
	x->write_ptr = write_ptr;
}



void *vdp_new(t_symbol *s, int argc, t_atom *argv)
{
	t_vdp *x = (t_vdp *)object_alloc(vdp_class);
	
	x->sr = sys_getsr();
	
	if(!x->sr){
		// error("zero sampling rate - set to 44100");
		x->sr = 44100;
	}
	// DSP CONFIG
	
	
	
	dsp_setup((t_pxobject *)x,3);
	outlet_new((t_object *)x, "signal");
    x->x_obj.z_misc |= Z_NO_INPLACE;
	
	// SET DEFAULTS
	x->maxdel = 50.0; // milliseconds
	x->feedback = 0.5;
	x->delay_time  = 0.0;
	x->interpolate = 1; // force this by default
	x->maxdel = atom_getfloatarg(0,argc,argv);
	x->delay_time = atom_getfloatarg(1,argc,argv) * 0.001; // convert to seconds
	x->feedback = atom_getfloatarg(2,argc,argv);
	if(!x->maxdel)
		x->maxdel = 50.0;
    // x->destbuf_name->s_name = "default_catch_buffer";
    // x->destbuf = buffer_ref_new((t_object*)x, x->destbuf_name);
	vdp_init(x,0);


	return x;
}

void vdp_free(t_vdp *x)
{
	dsp_free((t_pxobject *)x);
	free(x->delay_line);
}

void vdp_clear(t_vdp *x)
{
	memset((char*)x->delay_line,0,(x->len + 2) * sizeof(float));
}


void vdp_init(t_vdp *x,short initialized)
{
	//int i;
	
	if(!initialized){
		x->feedback_protect = 0;
		x->interpolate = 1;
		x->filter = 0;
		x->inf_hold = 0;
		if( x->maxdel < 1.0){
			x->maxdel = 1.0;
		}
		if( x->delay_time >= x->maxdel || x->delay_time < 0 )
			x->delay_time = 0.0;
		x->delay_samps = x->delay_time * x->sr; // input is in seconds, converted to samples here
		if( x->feedback > 0.999 || x->feedback < -0.999 ){
			error("%s: feedback initialization value %f is out of range",OBJECT_NAME, x->feedback);
			x->feedback = 0.0;
		}
		if( x->maxdel > MAX_DELAY_TIME ){
			error("%s: %f is too long, delay time set to max of %f",OBJECT_NAME,x->maxdel, MAX_DELAY_TIME);
			x->maxdel = MAX_DELAY_TIME;
		}
		x->len = x->maxdel * .001 * x->sr;
		x->lpf.coef = 0.5;
		x->lpf.x1 = 0.0;
		x->delay_line = (t_double *) calloc((x->len + 2), sizeof(t_double));
		
		x->phs = 0;
		x->mute = 0;
		x->tap = 0;
	} else {
		x->len = x->maxdel * .001 * x->sr;
		x->delay_line = (t_double *) realloc(x->delay_line, (x->len + 2) * sizeof(t_double));
		memset((char*)x->delay_line,0,(x->len + 2) * sizeof(float));
	}
	x->startmem = x->delay_line;
	x->endmem = x->startmem + x->len;
	// x->endmem = x->startmem + (x->len - 1);
	
	x->write_ptr = x->startmem;
	/*
	 post("startmem %d endmem %d len %d diff %d diffback %d", x->startmem, x->endmem, x->len, x->endmem - x->startmem, ( x->endmem - x->startmem) /sizeof(float));
	 */
}

void vdp_dsp64(t_vdp *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if(!sr)
        return;
	x->connections[1] = count[1];
	x->connections[2] = count[2];
	
	if(x->sr != sr){
		x->sr = sr;
		vdp_init(x,1);
	}
    object_method(dsp64, gensym("dsp_add64"),x,vdp_perform64,0,NULL);
}



void vdp_int(t_vdp *x, long tog)
{
	vdp_float(x,(float)tog);	
}

void vdp_float(t_vdp *x, double df) // Look at floats at inlets
{
	float f = (float)df;
	
	int inlet = x->x_obj.z_in;
	
	if (inlet == 1)
	{
		f *= 0.001; // convert to seconds
		if(f > 0.0 && f < x->maxdel){
			x->delay_time = f;
			x->delay_samps = f * x->sr;
			x->tap = 0;
		} else {
			error("%s: delaytime %.4f out of range [0.0 - %.4f]",OBJECT_NAME, f * 1000.0 ,x->maxdel);
			return;
		}
		
	}
	else if (inlet == 2)
	{
		// post("feedback is now %f",x->feedback);
		x->feedback = f;
		if( x->feedback_protect ) {
			if( x->feedback > 1.0)
				x->feedback = 1.0;
			if( x->feedback < -1.0 )
				x->feedback = -1.0;
		}
	}
	
}

void vdp_protect(t_vdp *x, double state)
{
	x->feedback_protect = state;
}

void vdp_copy_to_buffer(t_vdp *x, t_symbol *msg, short argc, t_atom *argv)
{
	t_symbol *destname;
	
	t_double *b_samples = x->delay_line;
    t_buffer_obj *b;
//	long b_nchans = 1;
	long b_frames = x->len + 2;
	
	float *b_dest_samples;
	long b_dest_nchans;
	long b_dest_frames;
	int i;
	
	destname = atom_getsymarg(0,argc,argv);
	if(! x->destbuf ){
        x->destbuf = buffer_ref_new((t_object*)x, destname);
    }
	buffer_ref_set(x->destbuf, destname);
    b = buffer_ref_getobject(x->destbuf);
    
    if(b == NULL){
        post("invalid sound buffer %s", destname->s_name);
        return;
    }
    b_dest_frames = buffer_getframecount(b);
    b_dest_nchans = buffer_getchannelcount(b);
    if(b_dest_nchans != 1 || b_dest_frames < b_frames ){
        post("destination buffer smaller than %d", b_frames);
        return;
    }
	b_dest_samples = buffer_locksamples(b);
// clear dest
    for(i = 0; i < b_dest_frames; i++){
        b_dest_samples[i] = 0;
    }
// copy samples
    for(i = 0; i < b_frames; i++){
        b_dest_samples[i] = b_samples[i];
    }
	
	object_method( (t_object *)x->destbuf, gensym("dirty") );
	
}

void vdp_setdestbuf(t_vdp *x, t_symbol *wavename)
{
 	t_buffer_obj *b;
	long b_frames;
    
	buffer_ref_set(x->destbuf, wavename);
    b = buffer_ref_getobject(x->destbuf);
    
    if(b == NULL){
        post("invalid sound buffer %s", wavename->s_name);
        return;
    }
    b_frames = buffer_getframecount(b);
    x->destbuf_name = wavename;
    
	if (buffer_getchannelcount(b) != 1) {
		post("vdp~: channel mismatch between buffer [%d] and outlet count [1]",
             buffer_getchannelcount(b));
        return;
	}
 }

void vdp_assist(t_vdp *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Input");break;
			case 1: sprintf(dst,"(signal/float) Delay Time");break;
			case 2: sprintf(dst,"(signal/float) Feedback");break;
				
		}
	} else if (msg==2) {
		switch (arg) {
			case 0:
				sprintf(dst,"(signal) Output ");
				break;
				
		}
		
	}
}