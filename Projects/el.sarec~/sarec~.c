#include "MSPd.h"

static t_class *sarec_class;

#define MAXBEATS (256)
#define OBJECT_NAME "sarec~"
#define SAREC_RECORD 1
#define SAREC_OVERDUB 2
#define SAREC_PUNCH 3
// update time in samples
#define WAVEFORM_UPDATE (2205)

typedef struct _sarec
{
    
	t_pxobject x_obj;
    
	long display_counter;
	int valid; // status of recording buffer (not yet implemented)
	int status; // idle? 0, recording? 1
	int mode; // record, overdub or punch
	int overdub; // 0 ? write over track, 1: overdub into track
	int *armed_chans; // 1, armed, 0, protected
	long counter; // sample counter
	float sync; // position in recording
	long start_frame; // start time in samples
	long end_frame; // end time in samples
	long fadesamps; // number of samples for fades on PUNCH mode
	long regionsamps; // use for fade
	int channel_count; // number of channels (hopefully!) in buffer
	float sr;
	t_symbol *bufname; // name of recording buffer
	t_buffer_ref *recbuf; // name of recording buffer
} t_sarec;

void *sarec_new(t_symbol *msg, short argc, t_atom *argv);
void sarec_region(t_sarec *x, t_symbol *msg, short argc, t_atom *argv);
void sarec_regionsamps(t_sarec *x, t_symbol *msg, short argc, t_atom *argv);
void sarec_assist(t_sarec *x, void *b, long m, long a, char *s);
void sarec_arm(t_sarec *x, t_floatarg chan);
void sarec_disarm(t_sarec *x, t_floatarg chan);
void sarec_attach_buffer(t_sarec *x);
void sarec_overdub(t_sarec *x);
void sarec_record(t_sarec *x);
void sarec_punch(t_sarec *x);
void sarec_punchfade(t_sarec *x, t_floatarg fadetime);
void sarec_perform64(t_sarec *x, t_object *dsp64, double **ins,
                     long numins, double **outs,long numouts, long n,
                     long flags, void *userparam);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.sarec~", (method)sarec_new, (method)dsp_free, sizeof(t_sarec), 0,A_GIMME,0);
	class_addmethod(c,(method)sarec_assist,"assist",A_CANT,0);
	class_addmethod(c,(method)sarec_arm,"arm",A_FLOAT, 0);
	class_addmethod(c,(method)sarec_disarm,"disarm",A_FLOAT, 0);
	class_addmethod(c,(method)sarec_overdub,"overdub", 0);
	class_addmethod(c,(method)sarec_punch,"punch", 0);
	class_addmethod(c,(method)sarec_punchfade,"punchfade", A_FLOAT, 0);
	class_addmethod(c,(method)sarec_record,"record", 0);
	class_addmethod(c,(method)sarec_region,"region",A_GIMME, 0);
	class_addmethod(c,(method)sarec_regionsamps,"regionsamps",A_GIMME, 0);
	potpourri_announce(OBJECT_NAME);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	sarec_class = c;
	return 0;
}

void sarec_assist (t_sarec *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		if(arg == 0){
			sprintf(dst,"(signal) Record Click");
		} else {
			sprintf(dst,"(signal) Input Signal %ld",arg);
		}
        
	} else if (msg==2) {
		sprintf(dst,"(signal) Sync");
	}
}

void *sarec_new(t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	int chans;
	
	if(argc < 2){
		error("%s: must specify buffer and channel count",OBJECT_NAME);
		return (void *)NULL;
	}
    
    
    chans = (int)atom_getfloatarg(1,argc,argv);
    
	t_sarec *x = (t_sarec *)object_alloc(sarec_class);
	
	dsp_setup((t_pxobject *)x,(1 + chans));
	outlet_new((t_pxobject *)x, "signal");
	x->x_obj.z_misc |= Z_NO_INPLACE;
    x->status = 0;
	x->counter = 0;
	atom_arg_getsym(&x->bufname,0,argc,argv);
	x->channel_count = chans;
	x->armed_chans = (int *) malloc(x->channel_count * sizeof(int));
	x->display_counter = 0;
	x->start_frame = 0;
	x->end_frame = 0;
	x->overdub = 0;// soon will kill
	x->mode = SAREC_RECORD;
	x->sr = sys_getsr();
	x->fadesamps = x->sr * 0.05; // 50 ms fade time
	// post("fadesamps are %d", x->fadesamps);
	x->start_frame = -1; // initialize to a bad value
	x->end_frame = -1;
	x->regionsamps = 0;
	for(i = 0; i < x->channel_count; i++){
		x->armed_chans[i] = 1; // all channels armed by default
	}
	x->recbuf = buffer_ref_new((t_object*)x, x->bufname);
	
	return x;
}

void sarec_perform64(t_sarec *x, t_object *dsp64, double **ins,
                     long numins, double **outs,long numouts, long n,
                     long flags, void *userparam)
{
    int i, j;
	t_double *click_inlet = ins[0];
	t_double *record_inlet;
	t_double *sync;
	t_int channel_count = x->channel_count;
	int status = x->status;
	int counter = x->counter;
	int *armed_chans = x->armed_chans;
    t_buffer_obj *recbuf_b;
	t_float *samples;
	long b_frames;
	long start_frame = x->start_frame;
	long end_frame = x->end_frame;
	//int overdub = x->overdub;
	int mode = x->mode;
	long fadesamps = x->fadesamps;
	long regionsamps = x->regionsamps;
	int clickval;
	float frak;
	float goin_up, goin_down;
	long counter_msf;
	sync = (t_double *)outs[numouts - 1];// last outlet
	if(! regionsamps ){
		x->regionsamps = regionsamps = end_frame - start_frame;
	}
    
	buffer_ref_set(x->recbuf, x->bufname);
    recbuf_b = buffer_ref_getobject(x->recbuf);
    if(recbuf_b == NULL){
        return;
    }
//    b_nchans = buffer_getchannelcount(recbuf_b);
    b_frames= buffer_getframecount(recbuf_b);
    samples = buffer_locksamples(recbuf_b);
	for(i = 0; i < n; i++){
		// could be record (1) or overdub (2)
		if( click_inlet[i] ){
			clickval = (int) click_inlet[i];
			// post("click value is %d", clickval);
			// signal to stop recording
			if(clickval == -2) {
				status = 0;
				counter = 0;
			}
			else {
				// arm all channels
				if(clickval == -1) {
					// just use all armed channels
				}
				// arm only one channel - protect against fatal bad index
				else if(clickval <= channel_count && clickval > 0) {
					for(j=0; j < channel_count; j++){
						armed_chans[j] = 0;
					};
					armed_chans[clickval - 1] = 1;
					// post("arming channel %d", clickval);
				}
				
				counter = start_frame;
				if(!end_frame){
					end_frame = b_frames;
				}
				status = 1;
			}
			
		}
		
		if(counter >= end_frame) {
			counter = 0;
			status = 0;
		}
		// write over track
		if(status && (mode == SAREC_RECORD) ){
			for(j=0; j < channel_count; j++){
				if( armed_chans[j] ){
					record_inlet = (t_double *) ins[j + 1];
					samples[ (counter * channel_count) + j] = record_inlet[i];
				}
			}
			counter++;
		}
		// overdub
		else if(status && (mode == SAREC_OVERDUB) ){
			for(j=0; j < channel_count; j++){
				if( armed_chans[j] ){
					record_inlet = (t_double *) ins[j + 1];
					samples[ (counter * channel_count) + j] += record_inlet[i];
				}
			}
			counter++;
		}
		// punch
		
		else if(status && (mode == SAREC_PUNCH)) {
			counter_msf = counter - start_frame;
			for(j=0; j < channel_count; j++){
				if( armed_chans[j] ){
					record_inlet = (t_double *) ins[j + 1];
					// frak is multiplier for NEW STUFF, (1-frak) is MULTIPLIER for stuff to fade out
					// do power fade though
					
					if( counter_msf < fadesamps ){
						
						// fade in
						frak = (float)counter_msf / (float)fadesamps;
						goin_up = sin(PIOVERTWO * frak);
						goin_down = cos(PIOVERTWO * frak);
						//post("fadein: %d, up: %f, down: %f", counter_msf, goin_up, goin_down);
						samples[ (counter * channel_count) + j] =
						(samples[ (counter * channel_count) + j] * goin_down)
						+ (record_inlet[i] * goin_up);
					} else if ( counter_msf >= (regionsamps - fadesamps) ){
						frak = (float) (regionsamps - counter_msf) / (float) fadesamps;
						// fade out
                        
						// frak = (float)counter_msf / (float)fadesamps;
						goin_up = cos(PIOVERTWO * frak);
						goin_down = sin(PIOVERTWO * frak);
						//post("fadeout: %d, up: %f, down: %f", counter_msf, goin_up, goin_down);
						samples[ (counter * channel_count) + j] =
						(samples[ (counter * channel_count) + j] * goin_up)
						+ (record_inlet[i] * goin_down);
					} else {
						// straight replace
						samples[ (counter * channel_count) + j] = record_inlet[i];
					}
					
				}
			}
			counter++;
		}
		
		sync[i] = (float) counter / (float) b_frames;
	}
    buffer_unlocksamples(recbuf_b);
	if(status){
		x->display_counter += n;
		if(x->display_counter > WAVEFORM_UPDATE){
			object_method( (t_object *)x->recbuf, gensym("dirty") );
			x->display_counter = 0;
		}
	}
	x->end_frame = end_frame;
	x->status = status;
	x->counter = counter;
}

void sarec_overdub(t_sarec *x)
{
	x->mode = SAREC_OVERDUB;
}


void sarec_record(t_sarec *x)
{
	x->mode = SAREC_RECORD;
}

void sarec_punchfade(t_sarec *x, t_floatarg fadetime)
{
	x->fadesamps = x->sr * fadetime * 0.001; // read fade in ms.
	//post("punch mode");
}

void sarec_punch(t_sarec *x)
{
	x->mode = SAREC_PUNCH;
	post("punch mode");
}

void sarec_disarm(t_sarec *x, t_floatarg chan)
{
    //	int i;
	int ichan = (int) chan;
	if(chan <= x->channel_count && chan > 0) {
		x->armed_chans[ichan - 1] = 0;
	}
}

void sarec_arm(t_sarec *x, t_floatarg chan)
{
	int i;
	int ichan = (int) chan;
	if(ichan == -1){
		for(i = 0; i < x->channel_count; i++){
			x->armed_chans[i] = 1;
		}
	} else if(ichan == 0)
	{
		for(i = 0; i < x->channel_count; i++){
			x->armed_chans[i] = 0;
		}
	} else if(chan <= x->channel_count && chan > 0) {
		x->armed_chans[ichan - 1] = 1;
	}
}

void sarec_region(t_sarec *x, t_symbol *msg, short argc, t_atom *argv)
{
	long b_frames;
	long start_frame, end_frame;
	float sr = x->sr;
    t_buffer_obj *recbuf_b;
    
	buffer_ref_set(x->recbuf, x->bufname);
    recbuf_b = buffer_ref_getobject(x->recbuf);
    if(recbuf_b == NULL){
        error("invalid buffer");
        return;
    }
    b_frames = buffer_getframecount(recbuf_b);
	// convert milliseconds to samples:
	start_frame = (long) (sr * 0.001 * atom_getfloatarg(0, argc, argv) );
	end_frame = (long) (sr * 0.001 * atom_getfloatarg(1, argc, argv) );
	start_frame = start_frame < 0 || start_frame > b_frames - 1 ? 0 : start_frame;
	end_frame = end_frame > b_frames ? b_frames : end_frame;
	x->end_frame = end_frame;
	x->start_frame = start_frame;
	x->regionsamps = end_frame - start_frame;
}

void sarec_regionsamps(t_sarec *x, t_symbol *msg, short argc, t_atom *argv)
{
	long b_frames;
	long start_frame, end_frame;
    t_buffer_obj *recbuf_b;
    
	buffer_ref_set(x->recbuf, x->bufname);
    recbuf_b = buffer_ref_getobject(x->recbuf);
    if(recbuf_b == NULL){
        error("invalid buffer");
        return;
    }
    b_frames = buffer_getframecount(recbuf_b);

	start_frame = (long) atom_getfloatarg(0, argc, argv);
	end_frame = (long) atom_getfloatarg(1, argc, argv);
	start_frame = start_frame < 0 || start_frame > b_frames - 1 ? 0 : start_frame;
	end_frame = end_frame > b_frames ? b_frames : end_frame;
	x->end_frame = end_frame;
	x->start_frame = start_frame;
	x->regionsamps = end_frame - start_frame;
}


void sarec_attach_buffer(t_sarec *x)
{
	t_buffer_obj *b;
	t_symbol *bufname = x->bufname;

	buffer_ref_set(x->recbuf, bufname);
    b = buffer_ref_getobject(x->recbuf);
    x->end_frame = buffer_getframecount(b) - 1;
    if(b == NULL){
        post("invalid sound buffer %s", bufname->s_name);
        return;
    }

}


void sarec_dsp64(t_sarec *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    t_buffer_obj *b;
	t_symbol *bufname = x->bufname;
    
	buffer_ref_set(x->recbuf, bufname);
    b = buffer_ref_getobject(x->recbuf);
    if(!sr)
        return;
	sarec_attach_buffer(x);
	if( x->start_frame < 0 && x->end_frame < 0){
		x->start_frame = 0;
		x->end_frame = buffer_getframecount(b) - 1;
		post("new frame extrema: %ld %ld",x->start_frame,x->end_frame);
	}
    object_method(dsp64, gensym("dsp_add64"),x,sarec_perform64,0,NULL);
}
