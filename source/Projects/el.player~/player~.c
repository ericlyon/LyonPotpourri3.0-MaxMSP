#include "../include/MSPd.h"

static t_class *player_class;

#include "time.h"
#include "stdlib.h"

#define DEFAULT_MAX_OVERLAP (8) // number of overlapping instances allowed
#define FORWARD 1
#define BACKWARD 2
#define ACTIVE 0
#define INACTIVE 1

#define MAXIMUM_VECTOR (8192)

#define OBJECT_NAME "player~"

// updated with notify function enabled (Nov. 2022)
#define MAX_OUTLETS (4)

typedef struct
{
    float phase; // current phase in frames
    float gain; // gain for this note
    short status;// status of this event slot
    float increment;// first increment noted (only if using static increments)
    
} t_event;

typedef struct _player
{
    
    t_pxobject x_obj;
    t_buffer_ref *wavebuf; // holds waveform samples
    t_symbol *wavename; // name of waveform buffer
    int already_failed; // flag to send bad buffer message just one time
    long last_modtime; // last time the buffer was loaded (for diagnostics)
    
    
    float sr; // sampling rate
    short hosed; // buffers are bad
    float fadeout; // fadeout time in sample frames (if truncation)
    float sync; // input from groove sync signal
    float increment; // read increment
    short direction; // forwards or backwards
    int most_recent_event; // position in array where last note was initiated
    long b_nchans; // channels of buffer (SOON OBSOLETE?)
    long outlet_count; // the defined (inflexible) number of outlets on this object
    int overlap_max; // max number of simultaneous plays
    t_event *events; //note attacks
    int active_events; // how many currently activated notes?
    short connections[4];// state of signal connections
    short interpolation_tog;// select for interpolation or not
    short mute;
    char static_increment; // flag to use static increment (off by default)
    /* variables only for Pd */
    int vs;//signal vector size
    float *trigger_vec;//copy of input vector (Pd only)
    float *increment_vec;//copy of input vector (Pd only)
    float *b_samples;//pointer to array data
    long b_frames;//number of frames (in Pd frames are mono)
    short paranoia; // flag to get older version performance
    t_float **msp_outlets; // link msp outlets just once in perform routine
} t_player;

void player_setbuf(t_player *x, t_symbol *wavename);
void *player_new(t_symbol *msg, short argc, t_atom *argv);
t_int *player_perform_mono_interpol(t_int *w);
t_int *player_perform_stereo_interpol(t_int *w);
t_int *pd_player(t_int *w);
// try to make this work for everybody (except maybe Pd)

void player_dblclick(t_player *x);
float player_boundrand(float min, float max);
void player_assist (t_player *x, void *b, long msg, long arg, char *dst);
void player_dsp_free(t_player *x);
void player_float(t_player *x, double f);
void player_interpolation(t_player *x, t_float f);
void player_mute(t_player *x, t_floatarg f);
void player_static_increment(t_player *x, t_floatarg f);
void player_stop(t_player *x);
void player_info(t_player *x);
void player_init(t_player *x,short initialized);
void attach_buffer(t_player *x, t_symbol *wavename);
void player_version(t_player *x);
void player_paranoia(t_player *x, t_floatarg f);
void player_dsp64(t_player *x, t_object *dsp64, short *count, double sr, long n, long flags);
void player_perform64(t_player *x, t_object *dsp64, double **ins,
                      long numins, double **outs,long numouts, long n,
                      long flags, void *userparam);
t_max_err player_notify(t_player *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
int player_attach_buffer(t_player*x);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.player~", (method)player_new, (method)player_dsp_free, sizeof(t_player), 0,A_GIMME, 0);
    class_addmethod(c,(method)player_dsp64,"dsp64", A_CANT, 0);
	class_addmethod(c,(method)player_assist,"assist", A_CANT, 0);
	class_addmethod(c,(method)player_dblclick,"dblclick", A_CANT, 0);
	class_addmethod(c,(method)player_setbuf,"setbuf", A_SYM, 0);
	class_addmethod(c,(method)player_stop,"stop", 0);
	class_addmethod(c,(method)player_mute,"mute", A_FLOAT, 0);
	class_addmethod(c,(method)player_paranoia,"paranoia", A_FLOAT, 0);
	class_addmethod(c,(method)player_float,"float", A_FLOAT, 0);
    class_addmethod(c,(method)player_notify,"notify", A_CANT, 0);
	CLASS_ATTR_CHAR(c, "static_increment", 0, t_player, static_increment);
	CLASS_ATTR_DEFAULT_SAVE(c, "static_increment", 0, "20");
	CLASS_ATTR_ENUMINDEX(c,"static_increment", 0, "Off On");
	CLASS_ATTR_LABEL(c, "static_increment", 0, "Static Increment");
	
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	player_class = c;
	potpourri_announce(OBJECT_NAME);
	return 0;
	
}

t_max_err player_notify(t_player *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    return buffer_ref_notify(x->wavebuf, s, msg, sender, data);
}

void player_stop(t_player *x)
{
	int i;
	
	for(i = 0; i < x->overlap_max; i++){
		x->events[i].status = INACTIVE;
		x->events[i].phase = 0.0;
		x->events[i].phase = 0.0;
		x->events[i].gain = 0.0;
	}
}

void player_mute(t_player *x, t_floatarg f)
{
	x->mute = (short)f;
}

void player_paranoia(t_player *x, t_floatarg f)
{
	x->paranoia = (short)f;
	post("%s: restart DACs for paranoia mode change",OBJECT_NAME);
}


void *player_new(t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	int argoffset = attr_args_offset(argc,argv);
    t_atom_long c;	
    
	t_player *x = (t_player *)object_alloc(player_class);
	x->outlet_count = 2;
	x->static_increment = 0;
	x->overlap_max = 2;
	
	atom_arg_getsym(&x->wavename,0,argc,argv);
	if( argoffset >= 2 ){
		atom_arg_getlong(&c,1,argc,argv);
        x->outlet_count = (long) c;
        //		x->overlap_max = atom_getfloatarg(2,argc,argv);
	}
	if( argoffset >= 3 ){
		x->overlap_max = (int) atom_getfloatarg(2,argc,argv);
	}
    //		post("getting attributes");
    attr_args_process(x, argc, argv);
    //		post("fixed status %d", x->static_increment);
	
	
	if(x->outlet_count < 1 || x->outlet_count > MAX_OUTLETS) {
		error("player~: %d is out of range - auto setting channels to 1",x->outlet_count);
		x->outlet_count = 1;
	}
	//post("player~ creating %d outlets",x->outlet_count);
	x->b_nchans = x->outlet_count;
	
	dsp_setup((t_pxobject *)x,2);
	x->x_obj.z_misc |= Z_NO_INPLACE;
	for(i = 0; i < x->outlet_count; i++){
		outlet_new((t_pxobject *)x, "signal");
	}
    
	if(argc < 1){
		error("%s: must specify buffer name",OBJECT_NAME);
		return NIL;
	}
    
	if(x->overlap_max <= 0 || x->overlap_max > 128){
		x->overlap_max = DEFAULT_MAX_OVERLAP;
	}
    //	post("%d overlaps for %s",x->overlap_max,x->wavename->s_name);
	x->sr = sys_getsr();
	x->vs = sys_getblksize();
    /*
	if(!x->sr)
		x->sr = 44100;
	if(!x->vs)
		x->vs = 256;
    */
//    x->wavebuf = buffer_ref_new((t_object*)x, x->wavename);
    
	player_init(x,0);
    
    //	post("post-init fixed status %d", x->static_increment);
    
	return x;
}

void player_init(t_player *x,short initialized)
{
	int i;
	
	if(!initialized){
		x->msp_outlets = (t_float **) calloc(x->outlet_count, sizeof(t_float *));
		
		x->most_recent_event = 0;
		x->active_events = 0;
		x->increment = 1.0;
		x->direction = FORWARD;
		
		x->events = (t_event *) calloc(x->overlap_max, sizeof(t_event));
		x->mute = 0;
		x->interpolation_tog = 1; // interpolation by default
		x->paranoia = 1; // use more expensive but safer version by default
		for(i = 0; i < x->overlap_max; i++){
			x->events[i].status = INACTIVE;
			x->events[i].increment = 0.0;
			x->events[i].phase = 0.0;
			x->events[i].gain = 0.0;
		}
	}
}


void player_dblclick(t_player *x)
{
    attach_buffer(x, x->wavename);
    buffer_view(buffer_ref_getobject(x->wavebuf));
}


void attach_buffer(t_player *x, t_symbol *wavename)
{
	
//	t_buffer_obj *b;
	
	x->hosed = 0;
	if (!x->wavebuf)
		x->wavebuf = buffer_ref_new((t_object*)x, x->wavename);
	else
		buffer_ref_set(x->wavebuf, x->wavename);
    
	// buffer_ref_set(x->wavebuf, wavename);
    /*
    b = buffer_ref_getobject(x->wavebuf);
    if(b == NULL){
        post("invalid sound buffer %s", wavename->s_name);
        return;
    }
    
    x->b_frames = buffer_getframecount(b);
    x->wavename = wavename;
    
	if (buffer_getchannelcount(b) != x->outlet_count) {
		post("player~: channel mismatch between buffer [%d] and outlet count [%s]",
             buffer_getchannelcount(b), x->outlet_count);
		x->hosed = 1;
	}
    */
}


void player_setbuf(t_player *x, t_symbol *wavename)
{
	player_stop(x);
	attach_buffer(x, wavename);
}

void player_perform64(t_player *x, t_object *dsp64, double **ins,
                      long numins, double **outs,long numouts, long n,
                      long flags, void *userparam)
{
	long outlet_count = x->outlet_count;
	
	t_double *trigger_vec = ins[0];
	t_double *increment_vec = ins[1];

	float *b_samples = x->b_samples;
	long b_nchans = x->b_nchans;
	long b_frames = x->b_frames;
	t_event *events = x->events;
	
	float increment = x->increment;
	int overlap_max = x->overlap_max;
	int iphase;
	float gain;
	short insert_success;
	int new_insert = 0;
	int i,j,k, chan;
	short *connections = x->connections;
	short bail;
	char static_increment = x->static_increment;
	float maxphase;
	float frac, fphase;
	int theft_candidate;
	float samp1, samp2;
	float vincrement;
	long active_outlets;
	// t_double **msp_outlets = x->msp_outlets;
	t_buffer_obj *wavebuf_b;
	

    
    // Now clean all outlets to prevent buildup of noise
    
    for(j = 0; j < n; j++){
        for(i = 0; i < numouts; i++){
            outs[i][j] = 0.0;
        }
    }
	if(x->mute || x->hosed ){
		return;
        post("stopped in perform: hosed %d mute %d",x->hosed, x->mute);
	}
	if (!x->wavebuf)
		x->wavebuf = buffer_ref_new((t_object*)x, x->wavename);
	else
		buffer_ref_set(x->wavebuf, x->wavename);
    
    wavebuf_b = buffer_ref_getobject(x->wavebuf);
    if(wavebuf_b == NULL){
        post("perform got NULL object");
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames = buffer_getframecount(wavebuf_b);

    
	if(x->outlet_count != b_nchans){
        if(!x->already_failed){
            post("ocount %d bchns %d", x->outlet_count, b_nchans);
            x->already_failed = 1;
            object_post((t_object *)x, "channel mismatch between buffer \"%s\" which has %d channels and the player~ which requires %d channels",
                x->wavename->s_name, b_nchans, numouts);
        }
        return;
    }
    if(b_frames == 0){
        if(!x->already_failed){
            x->already_failed = 1;
            object_post((t_object *)x, "\"%s\" is an empty buffer", x->wavename->s_name);
        }
        return;
    }
	b_samples = buffer_locksamples(wavebuf_b);
	active_outlets = b_nchans;
    
    
	// limit to number of declared outlets on the object
	if( active_outlets > outlet_count )
		active_outlets = outlet_count;
    
	// test if there are any active notes
	bail = 1;
	for(i = 0; i < overlap_max; i++){
		if(events[i].status == ACTIVE){
			bail = 0;
			break;
		}
	}
	// test if there are any triggers in this vector
	if(bail){
		for(i = 0; i < n; i++){
			if(trigger_vec[i]){
				bail = 0;
			}
		}
	}
	// if no to both, we're done
	if(bail){
        // post("aborting");
		goto abort;
	}
	
	// ok we do have business here
	
	
	if(connections[1]){// increment connected
		vincrement = increment_vec[0]; // startup
	} else {
		vincrement = 1.0; // default increment (improve by reading from float inlet)
	}
	// here is where we loop through each channel
	
	
	
	// play through currently active notes
	
	
	for(i = 0; i < overlap_max; i++){
		if( events[i].status == ACTIVE){
			gain = events[i].gain;
			
			for(j = 0; j < n; j++){
				
				fphase = events[i].phase;
				iphase = (int)floorf(fphase);
				frac = fphase - iphase;
				
				
				if(static_increment){ // overwrite input increment
					increment = events[i].increment;
				} else {
					increment = vincrement;
				}
				
				// SAFETY TESTS

				// anomaly - weird non-local behaviour if msp_outlets assignment is omitted
				
				if(iphase < b_frames && iphase >= 0){
					for(chan = 0; chan < active_outlets; chan++){
						if(increment >= 0){ // positive increment (DC is possible!!!)
							if(iphase == b_frames - 1 ){
								outs[chan][j] += b_samples[iphase * b_nchans + chan] * gain;
							} else {
								samp1 = b_samples[iphase * b_nchans + chan];
								samp2 = b_samples[(iphase + 1) * b_nchans + chan];
								outs[chan][j] += gain * (samp1 + frac * (samp2-samp1));
							}
						}
						else {
							if(iphase == 0.0 ){
								outs[chan][j] += b_samples[iphase * b_nchans + chan] * gain;
							} else {
								samp2 = b_samples[iphase * b_nchans + chan];
								samp1 = b_samples[(iphase - 1) * b_nchans + chan];
								outs[chan][j] += gain * (samp1 + frac * (samp2-samp1));
							}
						}
					}
				}
				
				if(static_increment){
					events[i].phase += events[i].increment;
				}
				else {
					if(connections[1]){
						events[i].phase += increment_vec[i];
					} else {
						events[i].phase += 1.0; // default increment
					}
				}
				// change to look at actual wavebuf ???
				if(events[i].phase < 0.0 || events[i].phase >= b_frames || ((events[i].increment == 0) && (static_increment)) ){
					events[i].status = INACTIVE;
					break;
				}
			}
		}
	}
	// trigger responder and initial playback code
	for(i=0; i<n; i++){
		
		
		if(trigger_vec[i]){
			gain = trigger_vec[i];
			
			// read instantaneous increment, or use 1.0 if not connected
			
			if(connections[1]){
				increment = increment_vec[i];// grab instantaneous increment
			}
			else {
				increment = 1.0; // default
			}
			insert_success = 0;
			for(j=0; j<overlap_max; j++){
				if(events[j].status == INACTIVE){
					events[j].status = ACTIVE;
					events[j].gain = gain;
					events[j].increment = increment;
					if(increment > 0){
						events[j].phase = 0.0;
					} else {
						events[j].phase = b_frames - 1;
					}
					insert_success = 1;
					new_insert = j;
					break;
				}
			}
			
			if(!insert_success){ // steal a note
				
				maxphase = 0;
				theft_candidate = 0;
				for(k = 0; k < overlap_max; k++){
					if(events[k].phase > maxphase){
						maxphase = events[k].phase;
						theft_candidate = k;
					}
				}
				// post("stealing note at slot %d", theft_candidate);
				new_insert = theft_candidate;
				events[new_insert].gain = gain;
				events[new_insert].increment = increment;
				if(increment > 0){
					events[new_insert].phase = 0.0;
				} else {
					events[new_insert].phase = b_frames - 1;
				}
				insert_success = 1;
			}
			for(k=i; k<n; k++){
				//roll out for remaining portion of vector
				
				fphase = events[new_insert].phase;
				iphase = (int)floorf(fphase);
				frac = fphase - iphase;
				
				if(static_increment){
					increment = events[new_insert].increment;
				}
				else if(connections[1]){
					increment = increment_vec[k];
				} else {
					increment = 1.0;
				}

				
				if(iphase < b_frames && iphase > 0){
					for(chan = 0; chan < active_outlets; chan++){
						if(increment >= 0){
							if(iphase == b_frames - 1 ){
								outs[chan][k] += b_samples[iphase * b_nchans + chan] * gain;
							} else {
								samp1 = b_samples[iphase * b_nchans + chan];
								samp2 = b_samples[(iphase + 1) * b_nchans + chan];
								outs[chan][k] += gain * (samp1 + frac * (samp2-samp1));
							}
						}
						else {
							if(iphase == 0.0 ){
								outs[chan][k] += b_samples[iphase * b_nchans + chan] * gain;
							}
							else {
								samp2 = b_samples[iphase * b_nchans + chan];
								samp1 = b_samples[(iphase - 1) * b_nchans + chan];
								outs[chan][k] += gain * (samp1 + frac * (samp2-samp1));
							}
						}
					}
				}
				if(static_increment){
					increment = events[new_insert].increment;
				} else {
					increment = vincrement;
				}
				events[new_insert].phase += increment;
				// termination conditions
				if( events[new_insert].phase < 0.0 || events[new_insert].phase >= b_frames){
					events[new_insert].status = INACTIVE;
					break;
				}
			}
		}
	}
	
abort:
	buffer_unlocksamples(wavebuf_b);
}

float player_boundrand(float min, float max)
{
	return min + (max-min) * ((float) (rand() % RAND_MAX)/ (float) RAND_MAX);
}


void player_dsp_free(t_player *x)
{
    
	dsp_free((t_pxobject *)x);
	free(x->events);
}

void player_dsp64(t_player *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    /*
    //   post("64 bit version of player~");
    if(!sr)
        return;
    
    // player_stop(x);
	// attach_buffer(x, x->wavename);
	
	if(x->sr != sr){
		x->sr = sr;
	}
	
	if(x->vs != n){
		x->vs = n;
		// player_init(x,1);
	}
    
	if(! x->hosed) {
		x->connections[1] = count[1];
	}
    
	
	if(x->b_frames <= 0 && ! x->hosed){
		// post("empty buffer, external disabled until it a sound is loaded");
		x->hosed = 1;
	}
	
//	player_stop(x); // turn off all players to start
 */
    object_method(dsp64, gensym("dsp_add64"),x,player_perform64,0,NULL);
}


void player_float(t_player *x, double f)
{
	int inlet = ((t_pxobject*)x)->z_in;
	float fval = (float) f;
	
	// post("float value is %f",fval);
	if(inlet == 1){
		if(fval == 0.0){
			error("%s: zero increment rejected", OBJECT_NAME);
			return;
		}
		x->increment = fval;
	}
}

void player_assist (t_player *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Click Trigger"); break;
			case 1: sprintf(dst,"(signal) Increment"); break;
		}
	} else if (msg==2) {
		if(x->b_nchans == 1){
			switch (arg){
				case 0: sprintf(dst,"(signal) Channel 1 Output"); break;
			}
		} else if(x->b_nchans == 2) {
			switch (arg){
				case 0: sprintf(dst,"(signal) Channel 1 Output"); break;
				case 1: sprintf(dst,"(signal) Channel 2 Output"); break;
			} 
		}
	}
}
