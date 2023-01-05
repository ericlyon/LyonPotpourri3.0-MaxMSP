#include "MSPd.h"

#define MAXGRAINS (512) // just for present to get lower overhead

#define MAXSCALE (8192)
#define OBJECT_NAME "multigrain~"


static t_class *multigrain_class;

typedef struct {
	double amplitude;
	long delay; // samples to wait until event starts
	long duration;// length in samples of event
	double phase; // phase for frequency oscillator
	double ephase; // phase for envelope
	double si; // sampling increment for frequency
	double esi; // sampling increment for envelope
	double endframe;//boundary frame (extremes are 0 or size-1); approach depends on sign of si
	short active;//status of this slot (inactives are available for new grains)
    long output_channel; // output channel for this grain
} t_grain;

typedef struct {
	float *b_samples;
	long b_frames;
	long b_nchans;//not needed here
} t_pdbuffer;


typedef struct _multigrain
{
    t_pxobject x_obj;
    t_buffer_ref *wavebuf; // holds waveform samples (by reference)
    t_buffer_ref *windowbuf; // holds window samples (by reference)
    t_symbol *wavename; // name of waveform buffer
    t_symbol *windowname; // name of window buffer
    
    float sr; // sampling rate
    short mute;
    short hosed; // buffers are bad
    /* Global grain data*/
    long events; // number of events in a block
    long horizon; // length of block for random events
    float min_incr; // minimum frequency for a grain
    float max_incr; // maximum frequency for a grain
    float minpan; // minimum pan for a grain
    float maxpan; // maxium pan for a grain
    float minamp; // minimum amplitude for a grain
    float maxamp; // maximum amplitude for a grain
    float mindur; // minumum duration for a grain
    float maxdur; // maximum duration for a grain
    t_grain *grains; // stores grain data
    float *pitchscale; // contains a frequency grid for pitch constraint
    int pitchsteps; // number of members in scale
    float transpose; // factor for scaling all pitches
    float pitch_deviation; // factor to adjust scaled pitches
    short steady; // toggles pulsed rhythmic activity
    float lowblock_increment; //lowest allowed frequency
    float highblock_increment;// highest allowed frequency
    float mindur_ms;//store duration in ms
    float maxdur_ms;//ditto
    float horizon_ms;//ditto
    short constrain_scale;//flag to only use bounded portion of scale rather than all of it
    short nopan;//stereo channels go straight out, mono goes to center
    long minskip;//minimum inskip in samples (default = zero)
    long maxskip;//maximum inskip in samples (default = maximum possible given dur/increment of note)
    long b_nchans;//channels in buffer (always 1 for Pd, at least today)
    long b_frames;//frames in waveform buffer
    float retro_odds;//odds to play sample backwards
    short interpolate;//flag to interpolate samples - on by default
    short interpolate_envelope;//flag to interpolate envelope
    long output_channels; // number of output channels
    long minchanr; // lowest channel to pick
    long maxchanr; // highest channel to pick
    long groupflag; // flag that we are constraining to a select group of channels
    long groupsize; // number of members in the group
    long *changroup; // hold the actual group
    long changroup_length; // how many members of group, currently
} t_multigrain;

void multigrain_setbuf(t_multigrain *x, t_symbol *wavename, t_symbol *windowname);
void *multigrain_new(t_symbol *msg, short argc, t_atom *argv);
void multigrain_dsp(t_multigrain *x, t_signal **sp, short *count);
void multigrain_reload(t_multigrain *x);
void multigrain_dblclick(t_multigrain *x);
void multigrain_spray(t_multigrain *x);
void multigrain_pitchspray(t_multigrain *x);
void multigrain_transpose(t_multigrain *x, t_floatarg t);
void multigrain_pitchdev(t_multigrain *x, t_floatarg d);
void multigrain_lowblock(t_multigrain *x, t_floatarg f);
void multigrain_highblock(t_multigrain *x, t_floatarg f);
void multigrain_events(t_multigrain *x, t_floatarg e);
float multigrain_boundrand(float min, float max);
void *multigrain_grist(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv);
void *multigrain_grain(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv);
void *multigrain_setscale(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv);
void *multigrain_chanrange(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv);
void *multigrain_setgroup(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv);
void multigrain_info(t_multigrain *x);
void multigrain_mute(t_multigrain *x, t_floatarg toggle);
void multigrain_steady(t_multigrain *x, t_floatarg toggle);
void multigrain_constrain_scale(t_multigrain *x, t_floatarg toggle);
void multigrain_assist (t_multigrain *x, void *b, long msg, long arg, char *dst);
void multigrain_dsp_free(t_multigrain *x);
void multigrain_init(t_multigrain *x,short initialized);
void multigrain_constrain(int *index_min, int *index_max, float min_incr, float max_incr, float *scale, int steps);
void multigrain_interpolate(t_multigrain *x, t_floatarg toggle);
void multigrain_nopan(t_multigrain *x, t_floatarg toggle);
void multigrain_retro_odds(t_multigrain *x, t_floatarg o);
void multigrain_seed(t_multigrain *x, t_floatarg seed);
void multigrain_interpolate_envelope(t_multigrain *x, t_floatarg toggle);
void multigrain_dsp64(t_multigrain *x, t_object *dsp64, short *count, double sr, long n, long flags);
void multigrain_perform64(t_multigrain *x, t_object *dsp64, double **ins,
                         long numins, double **outs,long numouts, long n,
                         long flags, void *userparam);

t_max_err multigrain_notify(t_multigrain *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.multigrain~", (method)multigrain_new, (method)dsp_free, sizeof(t_multigrain), 0,A_GIMME, 0);
    
    class_addmethod(c,(method)multigrain_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c,(method)multigrain_assist,"assist",A_CANT,0);
    class_addmethod(c,(method)multigrain_dblclick, "dblclick", A_CANT, 0);
    class_addmethod(c,(method)multigrain_setbuf, "setbuf", A_SYM, A_SYM, 0);
    class_addmethod(c,(method)multigrain_spray, "spray",  0);
    class_addmethod(c,(method)multigrain_pitchspray, "pitchspray",  0);
    class_addmethod(c,(method)multigrain_transpose, "transpose", A_DEFFLOAT, 0);
    class_addmethod(c,(method)multigrain_events, "events", A_DEFFLOAT, 0);
    class_addmethod(c,(method)multigrain_pitchdev, "pitchdev", A_DEFFLOAT, 0);
    class_addmethod(c,(method)multigrain_lowblock, "lowblock", A_DEFFLOAT, 0);
    class_addmethod(c,(method)multigrain_highblock, "highblock", A_DEFFLOAT, 0);
    class_addmethod(c,(method)multigrain_interpolate, "interpolate", A_FLOAT, 0);
    class_addmethod(c,(method)multigrain_retro_odds, "retro_odds", A_FLOAT, 0);
    class_addmethod(c,(method)multigrain_nopan, "nopan", A_FLOAT, 0);
    class_addmethod(c,(method)multigrain_seed, "seed", A_FLOAT, 0);
    class_addmethod(c,(method)multigrain_mute, "mute", A_DEFFLOAT, 0);
    class_addmethod(c,(method)multigrain_steady, "steady", A_DEFFLOAT, 0);
    class_addmethod(c,(method)multigrain_interpolate, "interpolate", A_FLOAT, 0);
    class_addmethod(c,(method)multigrain_interpolate_envelope, "interpolate_envelope", A_FLOAT, 0);
    class_addmethod(c,(method)multigrain_constrain_scale, "constrain_scale", A_DEFFLOAT, 0);
    class_addmethod(c,(method)multigrain_grist, "grist", A_GIMME, 0);
    class_addmethod(c,(method)multigrain_grain, "grain", A_GIMME, 0);
    class_addmethod(c,(method)multigrain_setscale, "setscale", A_GIMME, 0);
    class_addmethod(c,(method)multigrain_chanrange, "chanrange", A_GIMME, 0);
    class_addmethod(c,(method)multigrain_setgroup, "setgroup", A_GIMME, 0);
    class_addmethod(c,(method)multigrain_info, "info", 0);
    
    class_addmethod(c, (method)multigrain_notify, "notify", A_CANT, 0);
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    multigrain_class = c;
    
    potpourri_announce(OBJECT_NAME);
    return 0;
}


void multigrain_interpolate_envelope(t_multigrain *x, t_floatarg toggle)
{
    x->interpolate_envelope = toggle;
}

void multigrain_seed(t_multigrain *x, t_floatarg seed)
{
    srand((long)seed);
}

void multigrain_retro_odds(t_multigrain *x, t_floatarg o)
{
    if(o < 0 || o > 1){
        error("retro odds must be within [0.0 - 1.0]");
        return;
    }
    x->retro_odds = 0;
}

void multigrain_interpolate(t_multigrain *x, t_floatarg toggle)
{
	x->interpolate = toggle;
	post("toggle DACs to change interpolation status");
}

void multigrain_nopan(t_multigrain *x, t_floatarg toggle)
{
	x->nopan = toggle;
}

void multigrain_constrain_scale(t_multigrain *x, t_floatarg toggle)
{
	x->constrain_scale = toggle;
}
void multigrain_lowblock(t_multigrain *x, t_floatarg f)
{
	if(f > 0){
		x->lowblock_increment = f;
	}
}

void multigrain_highblock(t_multigrain *x, t_floatarg f)
{
	if(f > 0){
		x->highblock_increment = f;
	}
}

void multigrain_pitchdev(t_multigrain *x, t_floatarg d)
{
	if(d < 0 ){
		error("pitch deviation must be positive");
		return;
	}
	x->pitch_deviation = d;
}

void multigrain_mute(t_multigrain *x, t_floatarg toggle)
{
	x->mute = toggle;
}

void multigrain_steady(t_multigrain *x, t_floatarg toggle)
{
	x->steady = toggle;
}

void multigrain_events(t_multigrain *x, t_floatarg e)
{
	if( e <= 0 ){
		post("events must be positive!");
		return;
	}
	x->events = e;
	//	x->steady_dur = x->horizon / (float) x->events;
}

void multigrain_transpose(t_multigrain *x, t_floatarg t)
{
	if( t <= 0 ){
		error("transpose factor must be greater than zero!");
		return;
	}
	x->transpose = t;
}

void *multigrain_setscale(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	float *pitchscale = x->pitchscale;
	if( argc >= MAXSCALE ){
		error("%d is the maximum size scale", MAXSCALE);
		return 0;
	}
	if( argc < 2 ){
		error("there must be at least 2 members in scale");
		return 0;
	}
	for(i=0; i < argc; i++){
		pitchscale[i] = atom_getfloatarg(i,argc,argv);
	}
	x->pitchsteps = argc;
	// post("read %d values into scale", x->pitchsteps);
	return 0;
}

void *multigrain_chanrange(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv)
{
    long minchanr, maxchanr;
    if( argc < 2 ){
        error("chanrange: minchan maxchan");
        return 0;
    }
    minchanr = atom_getfloatarg(0,argc,argv);
    maxchanr = atom_getfloatarg(1,argc,argv);
    if(minchanr < 0){
        minchanr = 0;
    }
    if(maxchanr > x->output_channels - 1){
        maxchanr = x->output_channels - 1;
    }
    x->minchanr = minchanr;
    x->maxchanr = maxchanr;

    x->groupflag = 0;
    return 0;
}

void *multigrain_setgroup(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv)
{
    long i, chan;
    if( argc >= x->output_channels ){
        error("%d is the maximum group size", x->output_channels);
        return 0;
    }
    for(i=0; i < argc; i++){
        chan = (long)atom_getfloatarg(i,argc,argv);
        if(chan < 0){
            chan = 0;
        }
        if(chan > x->output_channels - 1){
            chan = x->output_channels - 1;
        }
        x->changroup[i] = chan;
    }
    x->changroup_length = argc;
    if(argc > 0){
        x->groupflag = 1;
    }
    return 0;
}

void multigrain_constrain(int *index_min, int *index_max, float min_incr, float max_incr, float *scale, int steps)
{
	int imax = steps - 1;
	int imin = 0;
	while(scale[imin] < min_incr && imin < imax){
		++imin;
	}
	if(imin == imax){
		post("could not constrain minimum index  - your grist parameters are out of range for this scale");
		*index_min = 0;
		*index_max = steps - 1;
		return;
	}
	while(scale[imax] > max_incr && imax > 0){
		--imax;
	}
	if(imax < 1 || imax <= imin){
		post("could not constrain maximum index - your grist parameters are out of range for this scale");
		*index_min = 0;
		*index_max = steps - 1;
		return;
	}
	*index_min = imin;
	*index_max = imax;
}

void multigrain_pitchspray(t_multigrain *x)
{
	int i,j;
	long grainframes;
    long eframes = buffer_getframecount(buffer_ref_getobject(x->windowbuf));
	long b_frames = buffer_getframecount(buffer_ref_getobject(x->wavebuf));
	long minskip = x->minskip;
	long maxskip = x->maxskip;
	float retro_odds = x->retro_odds;
	long horizon = x->horizon; // length of block for random events
	float mindur = x->mindur;
	float maxdur = x->maxdur;
	float min_incr = x->min_incr; // minimum frequency for a grain
	float max_incr = x->max_incr; // maximum frequency for a grain
	float minamp = x->minamp; // minimum amplitude for a grain
	float maxamp = x->maxamp; // maximum amplitude for a grain
	float transpose = x->transpose; // pitch scalar
	float lowblock_increment = x->lowblock_increment;
	float highblock_increment = x->highblock_increment;
	short steady = x->steady;
	float pitch_deviation = x->pitch_deviation;
	float pdev = 0;
	float pdev_invert = 0;
	int index_min, index_max;
	int steps = x->pitchsteps;
	float *scale = x->pitchscale;
	int windex;
	short inserted = 0;
	short constrain_scale = x->constrain_scale;
	t_grain *grains = x->grains;
	float tmp;
	
	if(! sys_getdspstate()){
		// error("scheduler halted until DSP is turned on.");
		return;
	}
	
	if( steps < 2 ){
		error("scale is undefined");
		return;
	}
	if( pitch_deviation ){
		pdev = 1.0 + pitch_deviation;
		pdev_invert = 1.0 / pdev;
	}
	for( i = 0; i < x->events; i++ ){
		inserted = 0;
		for(j = 0; j < MAXGRAINS; j++ ){
			if(!grains[j].active){
				if(steady){
					grains[j].delay = (float)(i * horizon) / (float) x->events ;
				} else {
					grains[j].delay = multigrain_boundrand(0.0,(float) horizon);
    			}
    			grains[j].duration = (long) multigrain_boundrand(mindur, maxdur);
    			grains[j].phase = 0.0;
    			grains[j].ephase = 0.0;
    			grains[j].amplitude = multigrain_boundrand(minamp, maxamp);
    			grains[j].amplitude *= .707;//used directly only for "nopan"
                grains[j].output_channel = random() % x->output_channels;
    			
				if(constrain_scale){
					multigrain_constrain(&index_min,&index_max,min_incr, max_incr, scale, steps);
					windex = (int) multigrain_boundrand((float)index_min, (float)index_max);
				} else {
					windex = (int) multigrain_boundrand(0.0, (float)(steps));
				}
    			grains[j].si = transpose * scale[windex];
				//	post("windex %d scale[w] %f transpose %f si %f",windex, scale[windex], transpose, grains[j].si );
    			grainframes = grains[j].duration * grains[j].si;
    			grains[j].esi =  (float) eframes / (float) grains[j].duration;
    			
    			if( pitch_deviation ){
    				grains[j].si *= multigrain_boundrand(pdev_invert,pdev);
    			}
    			// post("new si: %f", grains[j].si);
    			/* must add this code to spray, and also do for high frequencies
				 */
    			if(lowblock_increment > 0.0) {
    				if(grains[j].si < lowblock_increment){
    					post("lowblock: aborted grain with %f frequency",grains[j].si);
    					grains[j].active = 0; // abort grain
    					goto nextgrain;
    				}
    			}
    			if(highblock_increment > 0.0) {
    				if(grains[j].si > highblock_increment){
    					post("highblock: aborted grain with %f frequency, greater than %f",
							 grains[j].si, highblock_increment);
    					grains[j].active = 0; // abort grain
    					goto nextgrain;
    				}
    			}
				/* set skip time into sample */
				if(grainframes >= b_frames ){
				  	error("grain size %.0f is too long for buffer which is %d",grainframes, b_frames);
				  	grains[j].active = 0;
				  	goto nextgrain;
				}
				if(minskip > b_frames - grainframes){//bad minskip
				  	error("minskip time is illegal");
				  	grains[j].phase = 0.0;
				  	grains[j].endframe = grainframes - 1;
				} else {
					if(maxskip > b_frames - grainframes){
					  	grains[j].phase = multigrain_boundrand((float)minskip, (float) (b_frames - grainframes));
					  	//post("1. minskip %d maxskip %d",minskip,b_frames - grainframes);
					} else {
					  	grains[j].phase = multigrain_boundrand((float)minskip, (float)maxskip);
					  	//post("2. minskip %d maxskip %d",minskip,maxskip);
					}
					grains[j].endframe = grains[j].phase + grainframes - 1;
				}
				
				
				if( multigrain_boundrand(0.0,1.0) < retro_odds){//go backwards - make sure to test both boundaries
				  	grains[j].si *= -1.0;
				  	tmp = grains[j].phase;
				  	grains[j].phase = grains[j].endframe;
				  	grains[j].endframe = tmp;
				}
				/*post("grain: grainframes %d phase %f endframe %f amp %f",
				 grainframes, grains[j].phase, grains[j].endframe, grains[j].amplitude);*/
				grains[j].active = 1;
    			inserted = 1;
    			goto nextgrain;
    		}
		}
		if(!inserted){
			error("could not insert grain with increment %f",grains[j].si);
			return;
		}
	nextgrain: ;
	}
}

void multigrain_spray(t_multigrain *x)
{
	int i,j;
	long grainframes;
	long eframes = buffer_getframecount(buffer_ref_getobject(x->windowbuf));
	long b_frames = buffer_getframecount(buffer_ref_getobject(x->wavebuf));
	long horizon = x->horizon; // length of block for random events
	float mindur = x->mindur;
	float maxdur = x->maxdur;
	float min_incr = x->min_incr; // minimum incr for a grain (must be positive!)
	float max_incr = x->max_incr; // maximum incr for a grain (must be positive!)
	float minpan = x->minpan; // minimum pan for a grain
	float maxpan = x->maxpan; // maxium pan for a grain
	float minamp = x->minamp; // minimum amplitude for a grain
	float maxamp = x->maxamp; // maximum amplitude for a grain
	float transpose = x->transpose; // pitch scalar
	long minskip = x->minskip;
	long maxskip = x->maxskip;
	short steady = x->steady;
	float retro_odds = x->retro_odds;
	float pan;
	t_grain *grains = x->grains;
	short inserted;
	float tmp;
    long minchanr = x->minchanr;
    long maxchanr = x->maxchanr;
    long *changroup = x->changroup;
    long changroup_length = x->changroup_length;
    long dex;
    long groupflag = x->groupflag;
	if( !sys_getdspstate() ){
		return;
	}
	for( i = 0; i < x->events; i++ ){
		inserted = 0;
		for(j = 0; j < MAXGRAINS; j++ ){
			if(!grains[j].active){
				grains[j].active = 1;
				if(steady){
					grains[j].delay = (float)(i * horizon) / (float) x->events ;
				} else {
    				grains[j].delay = multigrain_boundrand(0.0,(float) horizon);
    			}
   				grains[j].duration = (long) multigrain_boundrand(mindur, maxdur);//frames for this grain
    			grains[j].ephase = 0.0;
    			pan = multigrain_boundrand(minpan, maxpan);
     			grains[j].amplitude = multigrain_boundrand(minamp, maxamp);
				grains[j].si = transpose * multigrain_boundrand(min_incr, max_incr);

                
                
                if(groupflag == 1){
                    dex = random() % changroup_length;
                    grains[j].output_channel = changroup[dex];
                } else {
                    grains[j].output_channel = minchanr + (random() % (maxchanr - minchanr));
                }
				grainframes = grains[j].duration * grains[j].si;//frames to be read from buffer
				grains[j].esi =  (float) eframes / (float) grains[j].duration;
				if(grainframes >= b_frames ){
				  	error("grain size %.0f is too long for buffer which is %d",grainframes, b_frames);
				  	grains[j].active = 0;
				  	goto nextgrain;
				}
				if(minskip > b_frames - grainframes){//bad minskip
				  	error("minskip time is illegal");
				  	grains[j].phase = 0.0;
				  	grains[j].endframe = grainframes - 1;
				} else {
					if(maxskip > b_frames - grainframes){
					  	grains[j].phase = multigrain_boundrand((float)minskip, (float) (b_frames - grainframes));
					  	//post("1. minskip %d maxskip %d",minskip,b_frames - grainframes);
					} else {
					  	grains[j].phase = multigrain_boundrand((float)minskip, (float)maxskip);
					  	//post("2. minskip %d maxskip %d",minskip,maxskip);
					}
					grains[j].endframe = grains[j].phase + grainframes - 1;
				}
				
				if( multigrain_boundrand(0.0,1.0) < retro_odds){//go backwards - make sure to test both boundaries
				  	grains[j].si *= -1.0;
				  	tmp = grains[j].phase;
				  	grains[j].phase = grains[j].endframe;
				  	grains[j].endframe = tmp;
				}
    			inserted = 1;
    			goto nextgrain;
    		}
		}
		if(! inserted){
			error("multigrain~: could not insert grain");
			return;
		}
	nextgrain: ;
	}
}

void *multigrain_grain(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv)
{
	short inserted;
	int j;
	float duration, incr, amplitude, pan;
	t_grain *grains;
	long eframes;
	long frames;
	float sr;
	float skip;
	if(!sys_getdspstate()){
		error("scheduler halted until DSP is turned on.");
		return NIL;
	}
	grains = x->grains;
     eframes = buffer_getframecount(buffer_ref_getobject(x->windowbuf));
	frames = buffer_getframecount(buffer_ref_getobject(x->wavebuf));

	sr = x->sr;
	
	if(argc < 5){
		error("grain takes 5 arguments, not %d",argc);
		post("duration increment amplitude pan skip(in ms)");
		return NIL;
	}
	duration = atom_getintarg(0,argc,argv);
	incr = atom_getfloatarg(1,argc,argv); // in ms
	amplitude = atom_getfloatarg(2,argc,argv);
	pan = atom_getfloatarg(3,argc,argv);
	skip = atom_getfloatarg(4,argc,argv) * .001 * sr;
	if(skip < 0){
		error("negative skip is illegal");
		return NIL;
	}
	if(skip >= frames){
		error("skip exceeds length of buffer");
		return NIL;
	}
	if(incr == 0.0){
		error("zero increment prohibited");
		return NIL;
	}
	if(duration <= 0.0){
		error("illegal duration:%f",duration);
		return NIL;
	}
	if(pan < 0.0 || pan > 1.0){
		error("illegal pan:%f",pan);
		return NIL;
	}
	inserted = 0;
	for(j = 0; j < MAXGRAINS; j++ ){
		if(!grains[j].active){
			grains[j].delay = 0.0;// immediate deployment
			grains[j].duration = (long) (.001 * x->sr * duration);
			grains[j].phase = skip;
			grains[j].ephase = 0.0;
			grains[j].amplitude = amplitude * .707;
//			grains[j].panL = amplitude * cos(pan * PIOVERTWO);
//			grains[j].panR = amplitude * sin(pan * PIOVERTWO);
			grains[j].esi =  (float)eframes / (float)grains[j].duration;
			grains[j].si = incr;
			grains[j].active = 1;
			return NIL;
		}
	}
	
	error("could not insert grain");
	return NIL;
	
}

float multigrain_boundrand(float min, float max)
{
	return min + (max-min) * ((float) (rand() % RAND_MAX)/ (float) RAND_MAX);
}


void *multigrain_new(t_symbol *msg, short argc, t_atom *argv)
{
    t_multigrain *x = (t_multigrain *)object_alloc(multigrain_class);
    float f = 2;
    int i;
    dsp_setup((t_pxobject *)x,1);

    
    srand(time(0)); //need "seed" message
    
    x->pitchscale = (float *) t_getbytes(MAXSCALE * sizeof(float));
    x->grains = (t_grain *) t_getbytes(MAXGRAINS * sizeof(t_grain));
    
    
    // default names
    x->wavename = gensym("waveform");
    x->windowname = gensym("window");
    
    if( argc < 2){
        error("Must enter wave buffer, window buffer, and output channel count");
        return NULL;
    }
    if(argc > 0)
        atom_arg_getsym(&x->wavename,0,argc,argv);
    if(argc > 1)
        atom_arg_getsym(&x->windowname,1,argc,argv);
    if(argc > 2)
        atom_arg_getfloat(&f,2,argc,argv);
    
    x->output_channels = (long) f;
    for(i = 0; i < x->output_channels; i++){
        outlet_new((t_pxobject *)x, "signal");
    }
    x->wavebuf = buffer_ref_new((t_object*)x, x->wavename);
    x->windowbuf = buffer_ref_new((t_object*)x, x->windowname);
    
    
    multigrain_init(x,0);
    return x;
}

void multigrain_init(t_multigrain *x,short initialized)
{
	int i;
	
	if(!initialized){
		x->pitchsteps = 0; // we could predefine a 12t scale
		x->mute = 0;
		x->steady = 0;
		x->events = 1; // set to 10 LATER
		x->horizon_ms = 1000;
		x->min_incr = 0.5;
		x->max_incr = 2.0;
		x->minamp = .1;
		x->maxamp = 1.0;
		x->mindur_ms = 150;
		x->maxdur_ms = 750;
		x->transpose = 1.0;
		x->pitch_deviation = 0.0;
		x->lowblock_increment = 0.0; // by default we do not block any increments
		x->highblock_increment = 0.0; // ditto
		x->constrain_scale = 0;
		x->retro_odds = 0.5;// after testing, set this to zero
		x->maxskip = -1;//flag to reset in setbuf SHOULD BE -1
		x->nopan = 0;//panning is on by default
		x->interpolate = 1;
		x->interpolate_envelope = 0;
        x->groupflag = 0;
        x->changroup = (long *)sysmem_newptrclear(x->output_channels);
        x->minchanr = 0;
        x->maxchanr = x->output_channels - 1;
	}
	x->horizon = x->horizon_ms * .001 * x->sr;
	x->mindur = x->mindur_ms * .001 * x->sr;
	x->maxdur = x->maxdur_ms * .001 * x->sr;
	for( i = 0; i < MAXGRAINS; i++ ){ // this is what we test for a legal place to insert grain
		x->grains[i].active = 0;
	}
}

void multigrain_info(t_multigrain *x)
{
	int tcount = 0;
    t_buffer_obj	*buffer = buffer_ref_getobject(x->wavebuf);
	t_grain *grains = x->grains;
	int i;
	
	for(i = 0; i < MAXGRAINS; i++ ){
		if(grains[i].active)
			++tcount;
	}
	post("%d active grains", tcount);
	post("wavename %s", x->wavename->s_name);
	post("windowname %s", x->windowname->s_name);
	post("sample size: %d",buffer_getframecount(buffer));
}

void multigrain_dblclick(t_multigrain *x)
{
    multigrain_reload(x);
    buffer_view(buffer_ref_getobject(x->wavebuf));
    buffer_view(buffer_ref_getobject(x->windowbuf));
}

void *multigrain_grist(t_multigrain *x, t_symbol *msg, short argc, t_atom *argv)
{
	if(argc < 10 ){
		error("grist takes 10 arguments:");
		post("events horizon min_incr max_incr minpan maxpan minamp maxamp mindur maxdur");
		return 0;
	}
	x->events = atom_getintarg(0,argc,argv);
	x->horizon_ms = atom_getfloatarg(1,argc,argv);
	x->min_incr = atom_getfloatarg(2,argc,argv);
	x->max_incr = atom_getfloatarg(3,argc,argv);
//	x->minpan = atom_getfloatarg(4,argc,argv);
//	x->maxpan = atom_getfloatarg(5,argc,argv);
	x->minamp = atom_getfloatarg(6,argc,argv);
	x->maxamp = atom_getfloatarg(7,argc,argv);
	x->mindur_ms = atom_getfloatarg(8,argc,argv);
	x->maxdur_ms = atom_getfloatarg(9,argc,argv);
	
	x->mindur = .001 * x->sr * x->mindur_ms ;
	x->maxdur = .001 * x->sr * x->maxdur_ms;
	x->horizon = .001 * x->sr * x->horizon_ms;
	
	if(x->min_incr < 0){
		x->min_incr *= -1.0;
	}
	if(x->max_incr < 0){
		x->max_incr *= -1.0;
	}
    /*
	if(x->minpan < 0.0) {
		x->minpan = 0.0;
	}
	if(x->maxpan > 1.0) {
		x->maxpan = 1.0;
	}
    */
	if(x->events < 0){
		x->events = 0;
	}
	return 0;
}


void multigrain_reload(t_multigrain *x)
{
	multigrain_setbuf(x, x->wavename, x->windowname);
}



void multigrain_setbuf(t_multigrain *x, t_symbol *wavename, t_symbol *windowname)
{
    t_buffer_obj *b;
    int i;
    

    buffer_ref_set(x->wavebuf, wavename);
    buffer_ref_set(x->windowbuf, windowname);


    b = buffer_ref_getobject(x->wavebuf);
    if(b == NULL){
        post("invalid sound buffer %s", wavename->s_name);
        return;
    } 
    x->b_frames = buffer_getframecount(b);

    x->hosed = 0;
    // kill all current grains
    for(i = 0; i < MAXGRAINS; i++){
        x->grains[i].active = 0;
    }
    if( buffer_getchannelcount(b) > 2 ){
        error("wavetable must be a mono or stereo buffer");
        x->hosed = 1;
    }
    
    b = buffer_ref_getobject(x->windowbuf);
    if(b == NULL){
        post("invalid window buffer %s", windowname->s_name);
        return;
    }
    if( buffer_getchannelcount(b) != 1 ){
        error("window must be a mono buffer");
        x->hosed = 1;
    }
    x->wavename = wavename;
    x->windowname = windowname;
    x->maxskip = x->b_frames - 1;

}

t_max_err multigrain_notify(t_multigrain *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    buffer_ref_notify(x->windowbuf, s, msg, sender, data);
    return buffer_ref_notify(x->wavebuf, s, msg, sender, data);
}

void multigrain_perform64(t_multigrain *x, t_object *dsp64, double **ins,
                         long numins, double **outs,long numouts, long n,
                         long flags, void *userparam)
{
    t_buffer_obj *wavebuf_b;
    t_buffer_obj *windowbuf_b;
    float *wavetable;
    float *window;
    t_grain *grains = x->grains;
    long b_nchans;
    long b_frames;
    short interpolate_envelope = x->interpolate_envelope;
    float sample1;
    float envelope;
    float amplitude;
    float si;
    float esi;
    float phase;
    float ephase;
    long delay;
    long eframes;
    long current_index;
    float tsmp1, tsmp2;
    float frac;
    int i,j,k;
    long output_channel; // randomly selected channel for output

 // we really need this clean operation:
    for(i = 0; i < x->output_channels; i++){
        for(j = 0; j < n; j++){
            outs[i][j] = 0.0;
        }
    }
    
    buffer_ref_set(x->wavebuf, x->wavename);
    buffer_ref_set(x->windowbuf, x->windowname);
    wavebuf_b = buffer_ref_getobject(x->wavebuf);
    windowbuf_b = buffer_ref_getobject(x->windowbuf);
    if(wavebuf_b == NULL || windowbuf_b == NULL){
        for(k = 0; k < MAXGRAINS; k++){
            grains[k].active = 0;
        }
        goto deliverance;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
    eframes = buffer_getframecount(windowbuf_b);
    if(b_nchans != 1){
        goto deliverance;
    }
    if(eframes == 0 || b_frames == 0){
        for(k = 0; k < MAXGRAINS; k++){
            grains[k].active = 0;
        }
        goto deliverance;
    }
    wavetable = buffer_locksamples(wavebuf_b);
    window = buffer_locksamples(windowbuf_b);
    if(!wavetable || !window ){
        goto deliverance;
    }

    
    for (j=0; j<MAXGRAINS; j++) {
        
        if(!grains[j].active){
            goto nextgrain;
        }
        amplitude = grains[j].amplitude;
        si =  grains[j].si;
        esi = grains[j].esi;
        phase =  grains[j].phase;
        ephase = grains[j].ephase;
        delay =  grains[j].delay;
        output_channel = grains[j].output_channel;
        
        for(i = 0; i < n; i++ ){
            
            if( delay > 0 ){
                --delay;
            }
            if( delay <= 0 && ephase < eframes){
                
                if(interpolate_envelope){
                    current_index = floor((double)ephase);
                    frac = ephase - current_index;
                    if(current_index == 0 || current_index == eframes - 1 || frac == 0.0){// boundary conditions
                        envelope = window[current_index];
                    } else {
                        tsmp1 = window[current_index];
                        tsmp2 = window[current_index + 1];
                        envelope = tsmp1 + frac * (tsmp2 - tsmp1);
                    }
                } else {
                    envelope = window[(int)ephase];
                }

                if(phase < 0 || phase >= b_frames){
                    error("phase %f is out of bounds",phase);
                    goto nextgrain;
                }
                current_index = floor((double)phase);
                frac = phase - current_index;
                if(current_index == 0 || current_index == b_frames - 1 || frac == 0.0){// boundary conditions
                    sample1 = wavetable[current_index] * amplitude; // amplitude * is new code
                } else {
                    tsmp1 = wavetable[current_index];
                    tsmp2 = wavetable[current_index + 1];
                    sample1 = tsmp1 + frac * (tsmp2 - tsmp1);
                }
                sample1 *= envelope;
                outs[output_channel][i] += sample1;


                
                phase += si;
                if(phase < 0 || phase >= b_frames){
                    // error("phase %f out of bounds",phase);
                    grains[j].active = 0;
                    goto nextgrain;
                }
                ephase += esi;
                
                
                if( ephase >= eframes ){
                    grains[j].active = 0;
                    goto nextgrain; // must escape loop now
                }
                
            }
        }
        grains[j].phase = phase;
        grains[j].ephase = ephase;
        grains[j].delay = delay;
        
    nextgrain: ;
    }
deliverance:
    buffer_unlocksamples(wavebuf_b);
    buffer_unlocksamples(windowbuf_b);
    ;
}



void multigrain_dsp_free(t_multigrain *x)
{
	dsp_free((t_pxobject *)x);
	t_freebytes(x->grains, MAXGRAINS * sizeof(t_grain));
	t_freebytes(x->pitchscale, MAXSCALE * sizeof(float));
    t_freebytes(x->changroup, x->output_channels * sizeof(long));
}


void multigrain_dsp64(t_multigrain *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    // zero sample rate, go die
    if(!sr)
        return;
    if( x->hosed ){
        return;
    }
    if( x->sr != sr){
        x->sr = sr;
        multigrain_init(x,1);
    }
    multigrain_reload(x);
    object_method(dsp64, gensym("dsp_add64"),x,multigrain_perform64,0,NULL);
}


void multigrain_assist (t_multigrain *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(mesages) No Signal Input"); break;
    	}
	} else if (msg==2) {
		switch (arg){
			case 0: sprintf(dst,"(signal) Output 1"); break;
			case 1: sprintf(dst,"(signal) Output 2"); break;
		}  
	}
}

