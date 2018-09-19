#include "MSPd.h"
// #include "fftease.h"

#define DENORM_WANT_FIX		1
#define CUSHION_FRAMES (128) // pad for grabbing
#define MAX_CHANNELS (1024)
#define MAX_RMS_BUFFER (0.250)
#define MIN_RMS_BUFFER (.001)
#define MAX_EVENTS (1024)
#define MAX_RMS_FRAMES (32768)

#define OBJECT_NAME "buffet~"

static t_class *buffet_class;

typedef struct _buffet
{
    t_pxobject x_obj;
    t_symbol *wavename; // name of waveform buffer
    t_buffer_ref *src_buffer_ref; // MSP reference to the buffer
    t_buffer_ref *dest_buffer_ref; // MSP reference to dest buffer for copying
    t_buffer_ref *cat_buffer_ref; // MSP reference to buffer to concatenate
    t_buffer_ref *dub_buffer_ref; // MSP reference to buffer to overdub
    float sr; // sampling rate
    short hosed; // buffers are bad
    float minframes; // minimum replacement block in sample frames
    float maxframes; // maximum replacement block in sample frames
    long storage_maxframes; // maxframe limit that current memory can handle
    float *storage; //temporary memory to store replacement block (set to maxframes * channels)
    long storage_bytes; // amount of currently allocated memory
    float fade; // fadein/fadeout time in sample frames
    float sync; // input from groove sync signal
    long swapframes; // number of frames in swap block
    long r1startframe; //start frame for block 1
    long r2startframe; // start frame for block 2
    float dc_coef; // filter coefficient
    float dc_gain; // normalization factor
    short initialized; // first time or not
    float *rmsbuf; // for onset analysis
    float rmschunk; // store lowest rms value in buffer
    void *list; // for start/end list
    void *bang; // completion bang
    void *floater; // outputs noise floor
    t_atom *listdata; // to report est. start/stop times of events in buffer
    float *analbuf; // contain overall envelope
    float *onset; // contain attack times for percussive evaluations
    
} t_buffet;

void buffet_setbuf(t_buffet *x, t_symbol *wavename);
void *buffet_new(t_symbol *msg, short argc, t_atom *argv);
t_int *buffet_perform(t_int *w);
void buffet_dsp(t_buffet *x, t_signal **sp, short *count);
void buffet_dblclick(t_buffet *x);
float buffet_boundrand(float min, float max);
void buffet_assist (t_buffet *x, void *b, long msg, long arg, char *dst);
void buffet_dsp_free(t_buffet *x);
void buffet_swap(t_buffet *x);
void buffet_specswap(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_retroblock(t_buffet *x);
void buffet_nakedswap(t_buffet *x);
void buffet_overlap(t_buffet *x, double f);
void buffet_minswap(t_buffet *x, double f);
void buffet_maxswap(t_buffet *x, double f);
void buffet_nosync_setswap(t_buffet *x);
void buffet_info(t_buffet *x);
void buffet_killdc(t_buffet *x);
void buffet_fixdenorm(t_buffet *x);
void buffet_rmschunk(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_fadein(t_buffet *x, double f);
void buffet_fadeout(t_buffet *x, double f);
void buffet_internal_fadeout(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_dc_gain(t_buffet *x, double f);
void buffet_dc_coef(t_buffet *x, double f);
void buffet_normalize(t_buffet *x, double f);
void buffet_rotatetozero(t_buffet *x, double f);
void buffet_erase(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_events(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_pevents(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_detect_onsets(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_detect_subband_onsets(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_copy_to_buffer(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
int buffet_setdestbuf(t_buffet *x, t_symbol *wavename);
void buffet_init(t_buffet *x, short initialized);
void buffet_reverse(t_buffet *x);
void buffet_version(t_buffet *x);
long ms2frames( long msdur, float sr );
void buffet_overdub(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_resize(t_buffet *x, t_floatarg newsize);
void buffet_normalize_selection(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_copy_to_mono_buffer(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_catbuf(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_trimzero(t_buffet *x, t_symbol *msg, short argc, t_atom *argv);
void buffet_update(t_buffet *x);
void buffet_spritz(t_buffet *x,  t_symbol *msg, short argc, t_atom *argv);
//void buffet_attach_any_buffer(t_buffet *x, t_buffer_ref *ref, t_symbol *wavename);
//void buffet_attach_buffer(t_buffet *x);
t_max_err buffet_notify(t_buffet *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void buffet_dsp64(t_buffet *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void buffet_perform64(t_buffet *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.buffet~", (method)buffet_new, (method)buffet_dsp_free, sizeof(t_buffet), 0,A_GIMME, 0);
	
    class_addmethod(c,(method)buffet_swap,"swap", 0);
    class_addmethod(c,(method)buffet_specswap,"specswap", A_GIMME, 0);
    class_addmethod(c,(method)buffet_retroblock,"retroblock", 0);
    class_addmethod(c,(method)buffet_minswap,"minswap", A_FLOAT, 0);
    class_addmethod(c,(method)buffet_maxswap,"maxswap", A_FLOAT, 0);
    class_addmethod(c,(method)buffet_fadein,"fadein", A_FLOAT, 0);
    class_addmethod(c,(method)buffet_fadeout,"fadeout", A_FLOAT, 0);
    class_addmethod(c,(method)buffet_overlap,"overlap", A_FLOAT, 0);
    class_addmethod(c,(method)buffet_dc_gain,"dc_gain", A_FLOAT, 0);
    class_addmethod(c,(method)buffet_dc_coef,"dc_coef", A_FLOAT, 0);
    class_addmethod(c,(method)buffet_normalize,"normalize", A_FLOAT, 0);
    class_addmethod(c,(method)buffet_rotatetozero,"rotatetozero", A_FLOAT, 0);
    class_addmethod(c,(method)buffet_killdc,"killdc", 0);
    class_addmethod(c,(method)buffet_nakedswap,"nakedswap", 0);
    class_addmethod(c,(method)buffet_rmschunk,"rmschunk", A_GIMME, 0);
    class_addmethod(c,(method)buffet_erase,"erase", A_GIMME, 0);
    class_addmethod(c,(method)buffet_copy_to_buffer,"copy_to_buffer", A_GIMME, 0);
    class_addmethod(c,(method)buffet_copy_to_mono_buffer,"copy_to_mono_buffer", A_GIMME, 0);
    class_addmethod(c,(method)buffet_internal_fadeout,"internal_fadeout", A_GIMME, 0);
    class_addmethod(c,(method)buffet_assist,"assist", A_CANT, 0);
    class_addmethod(c,(method)buffet_dblclick,"dblclick", A_CANT, 0);
    class_addmethod(c,(method)buffet_setbuf,"setbuf", A_SYM, 0);
	class_addmethod(c,(method)buffet_reverse,"reverse",  0);
	class_addmethod(c,(method)buffet_fixdenorm,"fixdenorm",  0);
	class_addmethod(c,(method)buffet_trimzero,"trimzero", A_GIMME, 0);
	class_addmethod(c,(method)buffet_overdub,"overdub", A_GIMME, 0);
    class_addmethod(c,(method)buffet_events,"events", A_GIMME, 0);
    class_addmethod(c,(method)buffet_pevents,"pevents", A_GIMME, 0);
	class_addmethod(c,(method)buffet_catbuf,"catbuf", A_GIMME, 0);
    class_addmethod(c,(method)buffet_info,"info", 0);
	class_addmethod(c,(method)buffet_resize,"resize", A_FLOAT, 0);
	class_addmethod(c,(method)buffet_spritz,"spritz", A_GIMME, 0);
    class_addmethod(c,(method)buffet_notify,"notify", A_CANT, 0);
    class_addmethod(c,(method)buffet_dsp64,"dsp64", A_CANT, 0);
	class_addmethod(c,(method)buffet_normalize_selection,"normalize_selection", A_GIMME, 0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	buffet_class = c;
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void buffet_spritz(t_buffet *x,  t_symbol *msg, short argc, t_atom *argv)
{
	int i, j;
	float val;
	long b_frames;
	long b_nchans;
	float *b_samples;
	double odds;
    t_buffer_obj *the_buffer;
	
	float density = atom_getfloatarg(0,argc,argv);
	
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    the_buffer = buffer_ref_getobject(x->src_buffer_ref);
	b_samples = buffer_locksamples(the_buffer);
	if (!b_samples){
		goto out;
    }
    b_frames = buffer_getframecount(the_buffer);
    b_nchans = buffer_getchannelcount(the_buffer);
	

	//	if( density > 1 ) { density = 1.0; }
	if( density <= 0 ) { odds = 1.0; } // fill the whole thing
	else{
		odds = density / x->sr;
		// post("odds %f",odds);
	}
    //	buffet_erase(x,msg,argc,argv);
	//	post("spritz: channels %d frames %d",b_nchans, b_frames);
	// memset((char *)b_samples, 0, b_nchans * b_frames * sizeof(float));
    
    for(i = 0; i < b_nchans * b_frames; i++){
        b_samples[i] = 0.0;
    }
	// do not repeating insert (NOT DONE YET!!!)
	for( j = 0; j < b_nchans; j++){
		for(i = 0; i < b_frames; i++){
			if(buffet_boundrand(0.0,1.0) < odds ){
                
				val = buffet_boundrand(-1.0,1.0);
				b_samples[ (b_nchans * i) + j ] = val;
			}
		}
        //		post("added %d sparks for channel %d",count, j+1);
	}
out:
    buffer_unlocksamples(the_buffer);
	buffet_update(x);
}

void buffet_trimzero(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
	short argcc = 1;
	long b_nchans, b_frames;
	float *b_samples;
	t_atom argvv;
	long oldframes, newframes, newsize,  skipmem, totalmem;
	long frontframe, backframe;
	float eppy = 0.000001;
	float *storage; // copy current contents here before resizing
	int i, j, k;
    t_buffer_obj *the_buffer;

    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    the_buffer = buffer_ref_getobject(x->src_buffer_ref);
	b_samples = buffer_locksamples(the_buffer);
	if (!b_samples){
		goto out;
    }
    b_frames = buffer_getframecount(the_buffer);
    b_nchans = buffer_getchannelcount(the_buffer);
	
	atom_arg_getfloat(&eppy,0,argc,argv);
	
	// do buffer tests here and return if not good
	
	if( eppy < 0 || eppy > 1.0 ){
		eppy = 0.000001;
		post("eppy set to %f", eppy);
	}
	frontframe = -1;
	backframe = -1;
	for( i = 0, j = 0; i < b_frames; i++, j += b_nchans){
		for(k = 0; k < b_nchans; k++){
			if( fabs(b_samples[j + k]) > eppy ){
				frontframe = i;
				goto escape1;
			}
		}
	}
escape1: ;
	
	for( i = b_frames - 1; i > 0; i--){
		j = i * b_nchans;
		for(k = 0; k < b_nchans; k++){
			if( fabs(b_samples[j + k]) > eppy ){
				backframe = i;
				goto escape2;
			}
		}
	}
escape2: ;
	oldframes = b_frames;
	newframes = 1 + (backframe - frontframe);
	
	if( frontframe == 0 && backframe == b_frames - 1){
		post("%s: no silence left to trim", OBJECT_NAME);
		return;
	}
    /*
	post("orig frames:%d new frames:%d",oldframes,newframes);
	post("frontframe: %d backframe: %d framedrop: %d\n", frontframe, backframe, oldframes - newframes);
    */
	if( newframes <= 0 ){
		post("%s: all zero buffer", OBJECT_NAME);
		return;
	}
	if( newframes + frontframe > oldframes ){
		post("frame error: skip %d seg %d total %d", frontframe, newframes, oldframes);
		goto out;
	}
	
	newsize = newframes * sizeof(float) * b_nchans;
	
	skipmem = frontframe * sizeof(float) * b_nchans;
	totalmem = b_frames * b_nchans * sizeof(float);
	
	storage = (float *) sysmem_newptr( newsize );
	if(storage == NULL){
		post("could not get memory");
		goto out;
	}
    /*
	post("frames left: %d",b_frames - (frontframe + newframes));
	post("totalmem: %d, skipmem: %d, sizemem %d, diff %d",
		 oldframes*b_nchans*sizeof(float), skipmem, newsize, totalmem - (skipmem+newsize));
	*/
	if(totalmem - (skipmem + newsize) <= 0 ){
		goto out;
		post("memory overflow");
	}
	// THIS WORKS:
	for( i = 0; i < newframes * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			storage[i + j] = b_samples[ frontframe * b_nchans + i + j ];
		}
	}
	// THIS DOESN'T - why???
	// memcpy(storage, b_samples + skipmem, newsize); // store current contents
	
	buffer_unlocksamples(the_buffer);
	
	atom_setfloat(&argvv,newframes);

    typedmess((void *)the_buffer, gensym("sizeinsamps"), argcc, &argvv);

    b_samples = buffer_locksamples(the_buffer);
	// memcpy(b_samples, storage, newsize); // dest, src, size
    //sysmem_copyptr(<#const void *src#>, <#void *dst#>, <#long bytes#>);
    sysmem_copyptr(storage, b_samples, newsize);
	sysmem_freeptr(storage);
	out :
    
    buffer_unlocksamples(the_buffer);
	buffet_update(x);
}


void buffet_overdub(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
	long skipin_frames;
	long skipout_frames;
	long seg_frames;
	long fadein_frames;
	long fadeout_frames;
	long sust_frames;
	float val;
	float gain;
	int i, j;
	float evpval;
	long b_nchans;
	t_symbol *dubname;
//    t_buffer_ref *dubref;
//    t_buffer_ref *srcref;
    t_buffer_obj *dubbuf;
    t_buffer_obj *srcbuf;
    
	float *thisbufsamps;
	float *dubsourcesamps;
	//t_buffer *thisbuf;
	
	
	// get args: dubsource_buf skipin skipout gain segduration fadein fadeout
	if( argc < 7 ){
		post("%s usage: overdub <dubsource skipin skipout gain segdur fadein fadeout>",OBJECT_NAME);
		return;
	}
	//	x->dubsource->myname = atom_getsymbolarg(0,argc,argv);
	atom_arg_getsym(&dubname,0,argc,argv);
	val = atom_getfloatarg(1,argc,argv);
	skipin_frames = ms2frames( val, x->sr );
	val = atom_getfloatarg(2,argc,argv);
	skipout_frames = ms2frames( val, x->sr );
	gain = atom_getfloatarg(3,argc,argv);
	val = atom_getfloatarg(4,argc,argv);
	seg_frames = ms2frames( val, x->sr );
	val = atom_getfloatarg(5,argc,argv);
	fadein_frames = ms2frames( val, x->sr );
	val = atom_getfloatarg(6,argc,argv);
	fadeout_frames = ms2frames( val, x->sr );
//	thisbuf->myname = x->wavename; // current name of this buffer
	
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
// dub_buffer_ref
    if(!x->dub_buffer_ref){
        x->dub_buffer_ref = buffer_ref_new((t_object*)x, dubname);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    
    // dubref = buffer_ref_new((t_object*)x, dubname);
    // srcref = buffer_ref_new((t_object*)x, x->wavename);
    dubbuf = buffer_ref_getobject(x->dub_buffer_ref);
    srcbuf = buffer_ref_getobject(x->src_buffer_ref);
  
    if(dubbuf == NULL){
        post("buffer %s is null", dubname->s_name);
        return;
    }
    if(srcbuf == NULL){
        post("buffer %s is null", x->wavename->s_name);
        return;
    }
	// attach buffers
//	setbuffy(dubsource);
//	setbuffy(thisbuf);
    

	
	/* sanity check: segduration + skipin not greater than thisbuf, channel counts match,
	 fadein and fadeout don't exceed segduration, skipin and skipout >= 0,
	 */
	if( buffer_getchannelcount(dubbuf) != buffer_getchannelcount(srcbuf)  ){
		error("%s: channel mismatch between %s (%d chans) and %s (%d chans)",
			  x->wavename->s_name, buffer_getchannelcount(srcbuf), dubname->s_name, buffer_getchannelcount(dubbuf) );
		return;
	}
	sust_frames = seg_frames - (fadein_frames + fadeout_frames);
	if(sust_frames < 0 ){
		error("%s: fadein + fadeout is greater than duration", OBJECT_NAME);
		return; // could fix it here
	}
	if(skipin_frames + seg_frames > buffer_getframecount(dubbuf)){
		// seg_frames = dubsource->b_frames - skipin_frames;
		error("%s: segment duration + skip exceeds duration of dub buffer; truncating",OBJECT_NAME);
		post("skip frames: %d, seg frames: %d, total frames: %d", skipin_frames, seg_frames, buffer_getframecount(dubbuf));
		return;
	}
	if(skipout_frames + seg_frames > buffer_getframecount(srcbuf)){
		// seg_frames = thisbuf->b_frames - skipin_frames;
		error("%s: segment duration + skip exceeds duration of destination buffer",OBJECT_NAME);
		return;
	}
	if(skipin_frames < 0 || skipout_frames < 0){
		error("%s: negative skip time detected",OBJECT_NAME);
		return;
	}
	if(seg_frames <= 0){
		error("%s: zero sized segment requested",OBJECT_NAME);
	}
	// do overdub
	
	b_nchans = buffer_getchannelcount(srcbuf);
	thisbufsamps = buffer_locksamples(srcbuf);
	dubsourcesamps = buffer_locksamples(dubbuf);
	for( i = 0 ; i < fadein_frames; i++, skipin_frames++, skipout_frames++){
		evpval = ( (float) i / (float) fadein_frames ) * gain;
		for( j = 0; j < b_nchans; j++){
			thisbufsamps[ skipout_frames * b_nchans + j ] += dubsourcesamps[ skipin_frames * b_nchans + j] * evpval;
		}
	}
	for( i = 0 ; i < sust_frames; i++, skipin_frames++, skipout_frames++){
		for( j = 0; j < b_nchans; j++){
			thisbufsamps[ skipout_frames * b_nchans + j ] += dubsourcesamps[ skipin_frames * b_nchans + j] * gain;
		}
	}
	for( i = 0 ; i < fadeout_frames; i++, skipin_frames++, skipout_frames++){
		evpval = ( (float) (fadeout_frames - i) / (float) fadeout_frames ) * gain;
		for( j = 0; j < b_nchans; j++){
			thisbufsamps[ skipout_frames * b_nchans + j ] += dubsourcesamps[ skipin_frames * b_nchans + j] * evpval;
		}
	}
    
    buffer_unlocksamples(srcbuf);
    buffer_unlocksamples(dubbuf);
    buffet_update(x);
	// send bang that overdub has completed
    buffet_update(x);
	
}

long ms2frames( long msdur, float sr )
{
	return (float) msdur * 0.001 * sr;
}

void buffet_info(t_buffet *x)
{
	long totalframes;
	t_buffer_obj *the_buffer;
    
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    the_buffer = buffer_ref_getobject(x->src_buffer_ref);
    
    
	totalframes = buffer_getframecount(the_buffer);
	post("minswap: %f, maxswap: %f", 1000. * x->minframes / x->sr, 1000. * x->maxframes / x->sr);
	post("buffer size: %f", 1000. * totalframes / x->sr);
}

void buffet_overlap(t_buffet *x, double f)
{
	if( f < 0.01 ){
		error("minimum fade time is 0.01 milliseconds");
		return;
	}
	x->fade = f * .001 * x->sr;
}

void buffet_events(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
	float *b_samples;
	long b_nchans;
	long b_frames;
    t_buffer_obj *wavebuf_b;
	t_atom *listdata = x->listdata;
	
	float bufsize;
	float onthresh;
	float offthresh;
	long bufsamps;
	long aframes; // frames to analyze
	float tadv;
	//long current_frame = 0;
	long i,j;
	float meansq;
	float rmsval;
	long bindex;
	float ipos;
	short activated = 0;
	float realtime = 0.0;
	int event_count = 0;
	float buffer_duration;

	
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        post("buffer %s is null", x->wavename->s_name);
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
    b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        post("invalid samples");
        goto exit;
    }
    
    
    
	// duration in ms.
	buffer_duration = 1000.0 * (float)b_frames / x->sr;
	
	bufsize = .001 * atom_getfloatarg(0,argc,argv);
	if(bufsize > MAX_RMS_BUFFER){
		bufsize = MAX_RMS_BUFFER;
		post("%s: setting analysis buffer to maximum: %f",OBJECT_NAME, MAX_RMS_BUFFER * 1000.0);
	} else if(bufsize < MIN_RMS_BUFFER){
		bufsize = MIN_RMS_BUFFER;
		post("%s: setting analysis buffer to maximum: %f",OBJECT_NAME, MIN_RMS_BUFFER * 1000.0);
	}
	
	onthresh = atom_getfloatarg(1,argc,argv);
	offthresh = atom_getfloatarg(2,argc,argv);
	bufsamps = x->sr * bufsize;
	
	bufsamps = bufsize * x->sr;
	tadv = (float)bufsamps / x->sr;
	//	post("actual window size: %f",tadv);
	aframes = (long) ( (float) b_frames / (float)bufsamps ) - 1;
	if(aframes < 2){
		error("%s: this buffer is too short to analyze",OBJECT_NAME);
		return;
	}
	//	post("analyzing %d frames",aframes);
	for(i = 0; i < aframes; i++){
		meansq = 0.0;
		ipos = b_nchans * i * bufsamps;
		// only analyze first channel
		for(j = 0; j < bufsamps; j+= b_nchans){
			bindex = ipos + j;
			meansq += b_samples[bindex] * b_samples[bindex];
		}
		meansq /= (float) bufsamps;
		rmsval = sqrt(meansq);
		realtime += tadv;
		if(rmsval > onthresh && ! activated) {
			activated = 1;
			
			//			post("event %d starts at %f",event_count+1, realtime);
			
			if(event_count >= MAX_EVENTS){
				error("%s: exceeded maximum of %d events",OBJECT_NAME, MAX_EVENTS);
				break;
			}
			// SETFLOAT(listdata+(event_count*2), realtime * 1000.0);
            atom_setfloat(listdata+(event_count*2), realtime * 1000.0);
			
		}
		else if( rmsval < offthresh && activated ){
			activated = 0;
			//			post("event %d ends at %f",event_count, realtime);
			atom_setfloat(listdata+((event_count*2) + 1), realtime * 1000.0);
			++event_count;
		}
	}
	if(activated){
		post("%s: missed the end of the last event; setting to end of buffer",OBJECT_NAME);
		atom_setfloat(listdata+((event_count*2) + 1), buffer_duration);
		++event_count;
	}
	outlet_list(x->list, 0, event_count * 2, listdata);
    exit: buffer_unlocksamples(wavebuf_b);
}



void buffet_pevents(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
	float *b_samples;
	long b_nchans;
	long b_frames;
    t_buffer_obj *wavebuf_b;
	t_atom *listdata = x->listdata;
	
	float bufsize;
	//float onthresh;
	//float offthresh;
	float diffthresh;
	long bufsamps;
	long aframes; // frames to analyze
	float tadv;
	//long current_frame = 0;
	long i,j;
	float meansq;
	float rmsval;
	long bindex;
	float ipos;
	//short activated = 0;
	float realtime = 0.0;
	int event_count = 0;
	float buffer_duration;
	float mindiff, maxdiff, absdiff,rmsdiff;
	float *analbuf = x->analbuf;
	float *onset = x->onset;
	//float endtime;
/*
	buffet_setbuf(x, x->wavename);
	b_samples = x->wavebuf->b_samples;
	b_nchans = x->wavebuf->b_nchans;
	b_frames = x->wavebuf->b_frames;
*/
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        post("buffer %s is null", x->wavename->s_name);
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
    b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        post("invalid samples");
        goto exit;
    }
    
	// duration in ms.
	buffer_duration = 1000.0 * (float)b_frames / x->sr;
	
	bufsize = .001 * atom_getfloatarg(0,argc,argv);
	if(bufsize > MAX_RMS_BUFFER){
		bufsize = MAX_RMS_BUFFER;
		post("%s: setting analysis buffer to maximum: %f",OBJECT_NAME, MAX_RMS_BUFFER * 1000.0);
	} else if(bufsize < MIN_RMS_BUFFER){
		bufsize = MIN_RMS_BUFFER;
		post("%s: setting analysis buffer to maximum: %f",OBJECT_NAME, MIN_RMS_BUFFER * 1000.0);
	}
	
	diffthresh = atom_getfloatarg(1,argc,argv);
	bufsamps = x->sr * bufsize;
	
	bufsamps = bufsize * x->sr;
	tadv = (float)bufsamps / x->sr;
	
	aframes = (long) ( (float) b_frames / (float)bufsamps );
	if(aframes < 2){
		error("%s: this buffer is too short to analyze",OBJECT_NAME);
		return;
	}
	if(aframes > MAX_RMS_FRAMES){
		post("too many frames: try a larger buffer size");
		return;
	}
	analbuf[0] = 0; // for first comparison
	for(i = 1; i < aframes; i++){
		meansq = 0.0;
		ipos = b_nchans * i * bufsamps;
		// only analyze first channel 
		for(j = 0; j < bufsamps; j+= b_nchans){
			bindex = ipos + j;
			meansq += b_samples[bindex] * b_samples[bindex];
		}
		meansq /= (float) bufsamps;
		analbuf[i] = rmsval = sqrt(meansq);
		realtime += tadv;
		
	}
	
	realtime = 0;
	mindiff = 9999.;
	maxdiff = 0.0;

	// look for big changes in direction
	for(i = 1; i < aframes; i++ ){
		rmsdiff = analbuf[i] - analbuf[i-1];
		absdiff = fabs(rmsdiff);
		if(absdiff > maxdiff)
			maxdiff = absdiff;
		if(absdiff < mindiff)
			mindiff = absdiff;
		
		if( rmsdiff > diffthresh ){
			// new
			
			onset[event_count] = (realtime + bufsize) * 1000.0;
			if(onset[event_count] < 0)
				onset[event_count] = 0;
			++event_count;
			//			post("rt %f diff %f",realtime * 1000.0,rmsdiff);
		}
		realtime += tadv;
	}
	//	post("mindiff %f maxdiff %f",mindiff,maxdiff);
	if(event_count == 0){
		post("%s: no events found",OBJECT_NAME);
	}
	

	
	for(i = 0; i < event_count; i++){
		atom_setfloat(listdata + i, onset[i]);
	}
	outlet_list(x->list, 0, event_count, listdata);
    exit: buffer_unlocksamples(wavebuf_b);
}

void buffet_internal_fadeout(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
	long fadeframes;
	long totalframes;
	int i,j,k;
	float env;
	float *b_samples;
	long b_nchans;
	long startframe;
	long endframe;
	t_buffer_obj *wavebuf_b;
    
	if(argc < 2){
		post("%s: internal_fadeout requires start and end times",OBJECT_NAME);
		return;
	}

	if( ! x->sr){
		error("zero sample rate!");
		return;
	}
	buffer_ref_set(x->src_buffer_ref, x->wavename);
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    totalframes= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples){
        buffer_unlocksamples(wavebuf_b);
        return;
    }

	startframe = .001 * x->sr * atom_getfloatarg(0,argc,argv);
	endframe = .001 * x->sr * atom_getfloatarg(1,argc,argv);
	
	if(startframe < 0 || endframe > totalframes || endframe <= startframe){
		error("%s: bad frame boundaries to internal_fadeout: %d and %d",OBJECT_NAME,startframe, endframe);
        buffer_unlocksamples(wavebuf_b);
		return;
	}
	fadeframes = endframe - startframe;
	
	for(i = (endframe-1) * b_nchans , k = 0; k < fadeframes ; i -= b_nchans, k++ ){
		env = (float) k / (float) fadeframes;
		for(j = 0; j < b_nchans; j++){
			b_samples[i + j] *= env;
		}
	}
    buffer_unlocksamples(wavebuf_b);
	buffet_update(x);
}

void buffet_update(t_buffet *x)
{
    t_buffer_obj *the_buffer;
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    the_buffer = buffer_ref_getobject(x->src_buffer_ref);
    object_method( the_buffer, gensym("dirty") );
	outlet_bang(x->bang);
}


void buffet_reverse(t_buffet *x)
{
	
	int i,j;
	float *b_samples;
	long b_nchans;
	long b_frames;
	float tmpsamp;
	long lenm1;
	t_buffer_obj *wavebuf_b;
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    // buffer_unlocksamples(wavebuf_b);
    if(! b_samples){
        buffer_unlocksamples(wavebuf_b);
        return;
    }

	lenm1 = (b_frames - 1) * b_nchans;
	for( i = 0; i < (b_frames * b_nchans) / 2; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			tmpsamp = b_samples[(lenm1 - i) + j];
			b_samples[(lenm1 - i) + j] = b_samples[i + j];
			b_samples[i + j] = tmpsamp;
		}
	}
    buffer_unlocksamples(wavebuf_b);
	buffet_update(x);
}

void buffet_copy_to_buffer(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
	t_symbol *destname;
	
	float *b_samples;
	long b_nchans;
	long b_frames;
	
    t_buffer_obj *destbuf_b;
    t_buffer_obj *srcbuf_b;
    
	float *b_dest_samples;
	long b_dest_nchans;
	long b_dest_frames;
	
	long startframe;
	long endframe;
	long chunkframes;
	
	float fadein,fadeout;
	int fadeframes;
	// totalframes;
	float env;
	int i,j,k;
	

    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    srcbuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(srcbuf_b == NULL){
        // warning message here...
        return;
    }
    b_nchans = buffer_getchannelcount(srcbuf_b);
    b_frames= buffer_getframecount(srcbuf_b);
	b_samples = buffer_locksamples(srcbuf_b);
    
	destname = atom_getsymarg(0,argc,argv);
	if(!x->dest_buffer_ref){
        x->dest_buffer_ref = buffer_ref_new((t_object*)x, destname);
    } else {
        buffer_ref_set(x->dest_buffer_ref, destname);
    }
    destbuf_b = buffer_ref_getobject(x->dest_buffer_ref);
    if(destbuf_b == NULL){
        // warning message here...
        buffer_unlocksamples(srcbuf_b);
        return;
    }

	
	b_dest_nchans = buffer_getchannelcount(destbuf_b);
	b_dest_frames = buffer_getframecount(destbuf_b);
	b_dest_samples = buffer_locksamples(destbuf_b);
    
	startframe = atom_getfloatarg(1,argc,argv) * .001 * x->sr;
	endframe = atom_getfloatarg(2,argc,argv) * .001 * x->sr;
	chunkframes = endframe - startframe;
	if(chunkframes <= 0){
		return;
	}
	if(b_nchans != b_dest_nchans){
		error("%s: channel mismatch with buffer %s",OBJECT_NAME, destname->s_name);
		return;
	}
	if(b_dest_frames < chunkframes ){
		//		post("%s: %s is too small, truncating copy region",OBJECT_NAME, destname->s_name);
		chunkframes = b_dest_frames;
	}
	if(startframe < 0 || endframe >= b_frames){
		error("%s: bad frame range for source buffer: %d %d",OBJECT_NAME, startframe,endframe);
		return;
	}
	/* first clean out destination */
	// memset((char *)b_dest_samples, 0, b_dest_frames * b_dest_nchans * sizeof(float));
    
    for(i = 0; i < b_dest_frames * b_dest_nchans; i++){
        b_dest_samples[i] = 0.0;
    }
	
	/* now copy segment */
    /*
	memcpy(b_dest_samples, b_samples + (startframe * b_nchans),
		   chunkframes * b_nchans * sizeof(float) );
	*/
    sysmem_copyptr(b_samples + (startframe * b_nchans), b_dest_samples, chunkframes * b_nchans * sizeof(float));
    
	if(argc == 5){
		//		post("enveloping");
		fadein = atom_getfloatarg(3,argc,argv);
		fadeout = atom_getfloatarg(4,argc,argv);
		if(fadein > 0){
			//			post("fading in");
			fadeframes = fadein * .001 * x->sr;
			
			if( fadeframes > b_dest_frames){
				error("%s: fadein is too long",OBJECT_NAME);
				return;
			}
			
			
			for(i = 0 , k = 0; k < fadeframes ; i += b_dest_nchans, k++ ){
				env = (float) k / (float) fadeframes;
				for(j = 0; j < b_dest_nchans; j++){
					b_dest_samples[i + j] *= env;
				}
			}
		}
		
		if(fadeout > 0){
			//			post("fading out");
			//			totalframes = chunkframes;
			fadeframes = fadeout * .001 * x->sr;
			startframe = chunkframes - fadeframes;
			endframe = chunkframes;
			
			//		endframe = .001 * x->sr * atom_getfloatarg(1,argc,argv); // stays the same
			
			if(startframe < 0){
				error("%s: bad frame boundaries to internal_fadeout: %d and %d",
					  OBJECT_NAME,startframe, endframe);
				return;
			}
			//	  fadeframes = endframe - startframe; // we already know this
			
			for(i = (chunkframes-1) * b_dest_nchans , k = 0; k < fadeframes ; i -= b_dest_nchans, k++ ){
				env = (float) k / (float) fadeframes;
				for(j = 0; j < b_dest_nchans; j++){
					b_dest_samples[i + j] *= env;
				}
			}
		}
	}
    buffer_unlocksamples(srcbuf_b);
    buffer_unlocksamples(destbuf_b);
	object_method( (t_object *)destbuf_b, gensym("dirty") );
	outlet_bang(x->bang);	
}



void buffet_copy_to_mono_buffer(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
	t_symbol *destname;
	
	float *b_samples;
	long b_nchans;
	long b_frames;
	
	float *b_dest_samples;
	long b_dest_nchans;
	long b_dest_frames;
	
	long startframe;
	long endframe;
	long chunkframes;
	
	float fadein,fadeout;
	int fadeframes;
	long grab_channel = 0;
	// totalframes;
	float env;
	int i,j,k;
	
	t_buffer_obj *wavebuf_b;
    t_buffer_obj *destbuf_b;
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
	destname = atom_getsymarg(0,argc,argv);
	
    if(!x->dest_buffer_ref){
        x->dest_buffer_ref = buffer_ref_new((t_object*)x, destname);
    } else{
        buffer_ref_set(x->dest_buffer_ref, destname);
    }
    destbuf_b = buffer_ref_getobject(x->dest_buffer_ref);
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL || destbuf_b == NULL){
        return;
    }
    
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples){
        goto exit;
    }
    
    b_dest_nchans = buffer_getchannelcount(destbuf_b);
    b_dest_frames= buffer_getframecount(destbuf_b);
	b_dest_samples = buffer_locksamples(destbuf_b);
    // buffer_unlocksamples(wavebuf_b);
    if(! b_samples){
        goto exit;
    }

	
	grab_channel = (long) atom_getfloatarg(1,argc,argv);
	startframe = atom_getfloatarg(2,argc,argv) * .001 * x->sr;
	endframe = atom_getfloatarg(3,argc,argv) * .001 * x->sr;
	
	chunkframes = endframe - startframe;
	if(chunkframes <= 0){
        goto exit;
	}
	/* make this more user-friendly with a "-1" option to copy the entire buffer */
	if(b_dest_nchans != 1){
		error("%s: buffer %s is not mono but has %d channels",
			  OBJECT_NAME, destname->s_name, b_dest_nchans);
		goto exit;
	}
	if(b_nchans <= grab_channel){
		error("%s: asked for channel %d but there are only %d channels in buffer %s",
			  OBJECT_NAME, b_nchans, grab_channel, destname->s_name);
		goto exit;
	}
	if(b_dest_frames < chunkframes ){
		//		post("%s: %s is too small, truncating copy region",OBJECT_NAME, destname->s_name);
		chunkframes = b_dest_frames;
	}
	if(startframe < 0 || endframe > b_frames){
		error("%s: bad frame range for source buffer: %d %d",OBJECT_NAME, startframe,endframe);
		startframe = 0;
		endframe = b_frames;
	}
	/* first clean out destination */
	// memset((char *)b_dest_samples, 0, b_dest_frames * sizeof(float));
	
    for(i = 0; i < b_dest_frames * b_dest_nchans; i++){
        b_dest_samples[i] = 0.0;
    }
    
	/* now copy segment */
	/*
	 memcpy(b_dest_samples, b_samples + (startframe * b_nchans),
	 chunkframes * b_nchans * sizeof(float) );
	 */
	
	for(i = 0; i < chunkframes; i++){
		b_dest_samples[i] = b_samples[ ( (startframe + i) * b_nchans) + grab_channel];
	}
	
	if(argc == 6){
		//		post("enveloping");
		fadein = atom_getfloatarg(4,argc,argv);
		fadeout = atom_getfloatarg(5,argc,argv);
		if(fadein > 0){
			//			post("fading in");
			fadeframes = fadein * .001 * x->sr;
			
			if( fadeframes > b_dest_frames){
				error("%s: fadein is too long",OBJECT_NAME);
				goto exit;
			}
			
			
			for(i = 0 , k = 0; k < fadeframes ; i += b_dest_nchans, k++ ){
				env = (float) k / (float) fadeframes;
				for(j = 0; j < b_dest_nchans; j++){
					b_dest_samples[i + j] *= env;
				}
			}
		}
		
		if(fadeout > 0){
			//			post("fading out");
			//			totalframes = chunkframes;
			fadeframes = fadeout * .001 * x->sr;
			startframe = chunkframes - fadeframes;
			endframe = chunkframes;
			
			//		endframe = .001 * x->sr * atom_getfloatarg(1,argc,argv); // stays the same
			
			if(startframe < 0){
				error("%s: bad frame boundaries to internal_fadeout: %d and %d",
					  OBJECT_NAME,startframe, endframe);
				goto exit;
			}
			//	  fadeframes = endframe - startframe; // we already know this
			
			for(i = (chunkframes-1) * b_dest_nchans , k = 0; k < fadeframes ; i -= b_dest_nchans, k++ ){
				env = (float) k / (float) fadeframes;
				for(j = 0; j < b_dest_nchans; j++){
					b_dest_samples[i + j] *= env;
				}
			}
		}
	}
	object_method( (t_object *)destbuf_b, gensym("dirty") );
exit:
    buffer_unlocksamples(wavebuf_b);
    buffer_unlocksamples(destbuf_b);
	outlet_bang(x->bang);
}

void buffet_rmschunk(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
	
	float *b_samples;
	long b_nchans;
	long b_frames;

	long bufsamps;

	long i;
	float meansq;
	float rmsval;
	long bindex;

	float buffer_duration;

	long startframe;
	long endframe;
    
	t_buffer_obj *wavebuf_b;

    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples){
        goto exit;
    }
    
	// duration in ms.
	buffer_duration = 1000.0 * (float)b_frames / x->sr;
	
	
	startframe = .001 * x->sr * atom_getfloatarg(0,argc,argv);
	endframe = .001 * x->sr * atom_getfloatarg(1,argc,argv);
	if(startframe < 0 || startframe >= b_frames){ // fixed 1 sample error here
		error("%s: naughty start frame: %d",OBJECT_NAME,startframe);
		goto exit;
	}
	if(endframe < 2 || endframe >= b_frames){
		error("%s: naughty start frame: %d",OBJECT_NAME,startframe);
		goto exit;
	}
	
	
	bufsamps = (endframe - startframe);
	//  post("analyzing %d samples",bufsamps);
	if(!bufsamps){
		// 	post("%s: start and end are identical!",OBJECT_NAME);
		// 	post("instantaneous sample value [ch 1 only]: %f", b_samples[startframe*b_nchans]);
		goto exit;
	}
	meansq = 0.0;
	for(i = startframe; i < endframe; i++){
		
		bindex = b_nchans * i; 		/* only analyze first channel */
		meansq += b_samples[bindex] * b_samples[bindex];
		
	}
	
	meansq /= (float) bufsamps;
	rmsval = sqrt(meansq);
	
	x->rmschunk = rmsval;
	outlet_float(x->floater, (double)x->rmschunk);
exit:
    buffer_unlocksamples(wavebuf_b);
}


void buffet_dc_gain(t_buffet *x, double f)
{
	x->dc_gain = f;
}

void buffet_dc_coef(t_buffet *x, double f)
{
	x->dc_coef = f;
}


void buffet_minswap(t_buffet *x, double f)
{
	if(f < 2.0 * 1000.0 * x->fade  / x->sr){
		error("minimum must be at least twice fade time which is %f", 1000. * x->fade / x->sr);
		return;
	}
	x->minframes = f * .001 * x->sr;
	//	post("min set to %f samples",x->minframes);
}

void buffet_maxswap(t_buffet *x, double f)
{
	long oldmem;
	//	long framelimit;
	long newframes;
	
	newframes = f * .001 * x->sr;
	if(newframes <= x->minframes){
		error("max blocksize must exceed minimum blocksize, which is %f", 1000. * x->minframes/ x->sr);
	}
	if(newframes > x->storage_maxframes ){
		//	post("extending memory");
		oldmem = x->storage_bytes;
		//	post("old memory %d", oldmem);
		x->storage_maxframes = newframes;
		x->storage_bytes = (x->storage_maxframes + 1) * 2 * sizeof(float);
		//	post("new memory %d", x->storage_bytes);
		
		x->storage = (float *) t_resizebytes((char *)x->storage, oldmem, x->storage_bytes);
	}
	x->maxframes = newframes;
}

void buffet_erase(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
	float *b_samples;
	long b_nchans;
	long b_frames;
	long startframe, endframe;
	int i;
	t_buffer_obj *wavebuf_b;
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);

    if(! b_samples){
        goto exit;
    }
    
	if(argc < 2){
		startframe = 0;
		endframe = b_frames;
	} else {
		startframe = .001 * x->sr * atom_getfloatarg(0,argc,argv);
		endframe = .001 * x->sr * atom_getfloatarg(1,argc,argv);
	}
	if(startframe < 0 || startframe >= b_frames - 1){
		error("%s: naughty start frame: %d",OBJECT_NAME,startframe);
		return;
	}
	if(endframe < 2 || endframe > b_frames){
		error("%s: naughty end frame: %d",OBJECT_NAME,endframe);
		endframe = b_frames;
		// return;
	}
	
	if(startframe == 0 ){
		// memset((char *)b_samples, 0, b_nchans * endframe * sizeof(float));
        
        for(i = 0; i < b_nchans * endframe; i++){
            b_samples[i] = 0.0;
        }
	} else {
		for(i = startframe * b_nchans; i < endframe * b_nchans; i++){
			b_samples[i] = 0.0;
		}
	}
	
	buffet_update(x);
    exit: buffer_unlocksamples(wavebuf_b);
}


void buffet_rotatetozero(t_buffet *x, double f)
{
	
	float target = (float) f;
	long shiftframes = (long) (target * 0.001 * x->sr);
	
	float *b_samples;
	long b_nchans;
	long b_frames;
	float *tmpmem;
	t_buffer_obj *wavebuf_b;
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);

	if(shiftframes <= 0 || shiftframes >= b_frames){
		error("%s: shift target %f is out of range",OBJECT_NAME,target);
		goto exit;
	}
	
	tmpmem = (float *) sysmem_newptr(shiftframes * b_nchans * sizeof(float));
	
	/* copy shift block to tmp */
	
	// memcpy(tmpmem, b_samples, shiftframes * b_nchans * sizeof(float));
	
    sysmem_copyptr(b_samples, tmpmem, shiftframes * b_nchans * sizeof(float));
	
	/* now shift the rest to the top */
	
	/* memmove(b_samples, b_samples + (shiftframes * b_nchans),
			(b_frames - shiftframes) * b_nchans * sizeof(float)); */
	
    // this could easily fail ??
    sysmem_copyptr(b_samples + (shiftframes * b_nchans), b_samples, (b_frames - shiftframes) * b_nchans * sizeof(float));
	
	/* finally copy tmp to the tail */
	
	
	/* memcpy(b_samples + (b_frames - shiftframes) * b_nchans,tmpmem,
		   shiftframes * b_nchans * sizeof(float )); */
	
    sysmem_copyptr(tmpmem, b_samples + (b_frames - shiftframes) * b_nchans, shiftframes * b_nchans * sizeof(float ));
    
	sysmem_freeptr(tmpmem);
	buffet_update(x);
    exit: buffer_unlocksamples(wavebuf_b);
}

void buffet_fixdenorm(t_buffet *x)
{
	float *b_samples;
	long b_nchans;
	long b_frames;
	long i;
	t_buffer_obj *wavebuf_b;
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        goto exit;
    }

	for(i = 0; i < b_frames * b_nchans; i++){
		
		if(b_samples[i] < .000001 && b_samples[i] > -.000001)
			b_samples[i] = 0.0;
	}
	// object_method( (t_object *)wavebuf_b, gensym("dirty") );
	buffet_update(x);
exit: buffer_unlocksamples(wavebuf_b);
}



void buffet_normalize(t_buffet *x, double f)
{
	
	float target = (float) f;
	float *b_samples;
	long b_nchans;
	long b_frames;
	long i;
	float maxamp = 0.0;
	float amptest;
	float rescale;
	t_buffer_obj *wavebuf_b;
    
    // post("trying normalize");
    
	if(target <= 0.0){
		error("%s: normalize target %f is too low",OBJECT_NAME,target);
		return;
	}
	
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        post("buffer %s is null", x->wavename->s_name);
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        post("invalid samples");
        goto exit;
    }
    // exit: buffer_unlocksamples(wavebuf_b);
    

	for(i = 0; i < b_frames * b_nchans; i++){
		amptest = fabs(b_samples[i]);
		if(maxamp < amptest)
			maxamp = amptest;
	}
	
	if(maxamp < .000000001){
		post("%s: amplitude zero or too low to normalize in \"%s\"",OBJECT_NAME,x->wavename->s_name);
		goto exit;
	}
	rescale = target / maxamp;
	if(rescale > .99 && rescale < 1.01){
		post("%s: \"%s\" already normalized to %f",OBJECT_NAME,x->wavename->s_name,target);
		outlet_bang(x->bang);
	}
	else {
        post("rescale factor: %f", rescale);
		for(i = 0; i < b_frames * b_nchans; i++){
			b_samples[i] *= rescale;
		}
	}
	object_method( (t_object *)wavebuf_b, gensym("dirty") );
	buffet_update(x);
    exit: buffer_unlocksamples(wavebuf_b);
}


void buffet_normalize_selection(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
	
	float target;
	float *b_samples;
	long b_nchans;
	long b_frames;
	long i;
	float maxamp = 0.0;
	float amptest;
	float rescale;
	long startframe, endframe;
	float startf, endf;
	t_buffer_obj *wavebuf_b;
    
	if(argc != 3){
		post("%s: normalize_section target_amp starttime endtime",OBJECT_NAME);
		return;
	}
	// we could put in a small ramp on norm if we feel like it
	
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        goto exit;
    }

	target = atom_getfloatarg(0,argc,argv);
	startf = atom_getfloatarg(1,argc,argv);
	endf = atom_getfloatarg(2,argc,argv);
	post("target %f start %f end %f",target, startf,endf);
	startframe = (long) (x->sr * 0.001 * startf);
	endframe = (long) (x->sr * 0.001 * endf);
	if(startframe < 0 || endframe > b_frames || startframe > endframe){
		error("%s: bad frame selection: %d %d",OBJECT_NAME,startframe,endframe);
		goto exit;
	}
	for(i = startframe * b_nchans; i < endframe * b_nchans; i++){
		amptest = fabs(b_samples[i]);
		if(maxamp < amptest)
			maxamp = amptest;
	}
	
	if(maxamp < .000000001){
		post("%s: amplitude zero or too low to normalize in \"%s\"",OBJECT_NAME,x->wavename->s_name);
		goto exit;
	}
	rescale = target / maxamp;
	if(rescale > .99 && rescale < 1.01){
		post("%s: \"%s\" already normalized to %f",OBJECT_NAME,x->wavename->s_name,target);
	}
	else {
		for(i = startframe * b_nchans; i < endframe * b_nchans; i++){
			b_samples[i] *= rescale;
		}
	}
	buffet_update(x);
    exit: buffer_unlocksamples(wavebuf_b);
}



void buffet_fadein(t_buffet *x, double f)
{
	long fadeframes;
	long b_frames;
	int i,j,k;
	float env;
	float *b_samples;
	long b_nchans;
	t_buffer_obj *wavebuf_b;
	
	if( ! x->sr){
		error("zero sample rate!");
		return;
	}

	// we could put in a small ramp on norm if we feel like it
	
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        goto exit;
    }
    
	fadeframes = f * .001 * x->sr;
	//post("fading in %d frames",fadeframes);

	if( fadeframes > b_frames){
		error("fadein is too long");
		goto exit;
	}
	for(i = 0, k = 0;i < fadeframes * b_nchans; i += b_nchans, k++ ){
		env = (float) k / (float) fadeframes;
		for(j = 0; j < b_nchans; j++){
			b_samples[i + j] *= env;
		}
	}
	buffet_update(x);
    exit: buffer_unlocksamples(wavebuf_b);
}

void buffet_fadeout(t_buffet *x, double f)
{
	long fadeframes;
	long b_frames;
	int i,j,k;
	float env;
	float *b_samples;
	long b_nchans;
	t_buffer_obj *wavebuf_b;
	
	if( ! x->sr){
		error("zero sample rate!");
		return;
	}
    
	// we could put in a small ramp on norm if we feel like it
	
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        goto exit;
    }

	fadeframes = f * .001 * x->sr;

	
	if( fadeframes > b_frames){
		error("%s: fadein is too long",OBJECT_NAME);
		goto exit;
	}
	for(i = (b_frames-1) * b_nchans , k = 0; k < fadeframes ; i -= b_nchans, k++ ){
		env = (float) k / (float) fadeframes;
		for(j = 0; j < b_nchans; j++){
			b_samples[i + j] *= env;
		}
	}
	buffet_update(x);
    exit: buffer_unlocksamples(wavebuf_b);
}



void buffet_killdc(t_buffet *x)
{
	long b_frames;
	int i,j;
	float *b_samples;
	long b_nchans;
	float dc_coef = x->dc_coef;
	float a0[MAX_CHANNELS];
	float a1[MAX_CHANNELS];
	float b0[MAX_CHANNELS];
	float b1[MAX_CHANNELS];
	t_buffer_obj *wavebuf_b;
		
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        goto exit;
    }
	
	if(b_nchans > MAX_CHANNELS) {
		error("buffer has too many channels");
		goto exit;
	}
	for(j = 0; j < b_nchans; j++){
		a0[j] = a1[j] = b0[j] = b1[j] = 0.0;
	}
	
	for(i = 0; i < b_frames * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			a0[j] = b_samples[i + j];
			b0[j] = a0[j] - a1[j] + dc_coef * b1[j];
			b_samples[i + j] = b0[j];
			b1[j] = b0[j];
			a1[j] = a0[j];
		}
	}
	buffet_update(x);
	exit: buffer_unlocksamples(wavebuf_b);
}


void *buffet_new(t_symbol *msg, short argc, t_atom *argv)
{
    t_buffet *x = (t_buffet *)object_alloc(buffet_class);
 	srand(clock());
	dsp_setup((t_pxobject *)x,0);
	x->floater = floatout((t_pxobject *)x); // right outlet
	x->list = listout((t_pxobject *)x); // middle outlet
	x->bang = bangout((t_pxobject *)x); // left outlet
	//
	
	x->sr = sys_getsr();
	if(! x->sr )
		x->sr = 44100;
	
	if(argc < 1){
		error("%s: you must provide a valid buffer name",OBJECT_NAME);
		return NIL;
	}
	x->minframes = 0;
	x->maxframes = 0;
	atom_arg_getsym(&x->wavename,0,argc,argv);
	if(argc >= 2)
		atom_arg_getfloat(&x->minframes,1,argc,argv);
	if(argc >= 3)
		atom_arg_getfloat(&x->maxframes,2,argc,argv);
	if(!x->minframes)
		x->minframes = 100;
	if(!x->maxframes)
		x->maxframes = x->minframes + 10;
	
	x->initialized = 0;
    
	buffet_init(x,0);
	return x;
}

void buffet_init(t_buffet *x, short initialized)
{
	if(x->minframes <= 0)
		x->minframes = 250;
	if(x->maxframes <= 0)
		x->maxframes = 1000;
	
	// rewrite memory initialization with callocs
	if(!initialized){
		x->minframes *= .001 * x->sr;
		x->storage_maxframes = x->maxframes *= .001 * x->sr;
		x->fade = .001 * 20 * x->sr; // 20 ms fadetime to start
		x->storage_bytes = (x->maxframes + 1) * 2 * sizeof(float); // stereo storage frames
		x->storage = (float *) sysmem_newptr(x->storage_bytes);
		x->dc_coef = .995; // for dc blocker
		x->dc_gain = 4.0;
		x->rmsbuf = (float *) sysmem_newptrclear(MAX_RMS_BUFFER * x->sr * sizeof(float));
		x->listdata = (t_atom *)sysmem_newptr(MAX_EVENTS * sizeof(t_atom)); // lots of events
		x->analbuf = (float *) sysmem_newptrclear(MAX_RMS_FRAMES * sizeof(float));
		x->onset = (float *) sysmem_newptr(MAX_EVENTS * sizeof(float));
		x->initialized = 1;
	} else {
		x->minframes *= .001 * x->sr;
		x->storage_maxframes = x->maxframes *= .001 * x->sr;
		x->fade = .001 * 20 * x->sr; // 20 ms fadetime to start
		x->storage_bytes = (x->maxframes + 1) * 2 * sizeof(float); // stereo storage frames
		x->storage = (float *) sysmem_resizeptr((void *)x->storage_bytes,x->storage_bytes);
		x->rmsbuf = (float *)sysmem_resizeptrclear((void *)x->rmsbuf,MAX_RMS_BUFFER * x->sr * sizeof(float));
	}
}


void buffet_nosync_setswap(t_buffet *x)
{	
	float minframes = x->minframes;
	float maxframes = x->maxframes;
	long swapframes = x->swapframes;
	long r1startframe = x->r1startframe;
	long r2startframe = x->r2startframe;
	long r1endframe;
	long r2endframe;
	long region1;
	long region2;
    long b_frames;
	t_buffer_obj *wavebuf_b;
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_frames= buffer_getframecount(wavebuf_b);
	swapframes = buffet_boundrand(minframes, maxframes);
	r1startframe = buffet_boundrand(0.0, (float)(b_frames-swapframes));
	r1endframe = r1startframe + swapframes;
	region1 = r1startframe;
	region2 = b_frames - r1endframe;
	if(swapframes > region1){
		r2startframe = buffet_boundrand((float)r1endframe,(float)(b_frames-swapframes));
	} else if(swapframes > region2) {
		r2startframe = buffet_boundrand(0.0,(float)(r1startframe-swapframes));
	} else { // either region ok
		if(buffet_boundrand(0.0,1.0) > 0.5){
			r2startframe = buffet_boundrand(0.0,(float)(r1startframe-swapframes));
		} else {
			r2startframe = buffet_boundrand((float)r1endframe,(float)(b_frames-swapframes));
		}
	}
	r2endframe = r2startframe + swapframes;
	
	if(r2startframe < 0 || r1startframe < 0){
		error("start frame less than zero!");
		return;
	}
	if(r2endframe >= b_frames || r1endframe >= b_frames){
		error("end frame reads beyond buffer!");
		return;
	}
	x->swapframes = swapframes;
	x->r1startframe = r1startframe;
	x->r2startframe = r2startframe;
}


void buffet_swap(t_buffet *x)
{
	float maxframes = x->maxframes;
	float fade = x->fade;
	long b_frames;
	long swapframes;

	long start_sample1;
	long start_sample2;
	float *storage = x->storage;
	float *b_samples;
	float mix_sample;
	float fadein_gain;
	float fadeout_gain;
	float fadephase;
	long b_nchans;
	int i,j,k;
	t_buffer_obj *wavebuf_b;
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        goto exit;
    }    
	
	if(b_frames < maxframes * 2 + 1){
		error("buffer must contain at least twice as many samples as the maximum swap size");
		goto exit;
	}
	if(b_nchans > 2){
		error("buffet~ only accepts mono or stereo buffers");
		goto exit;
	}
	
	buffet_nosync_setswap(x);
	start_sample1 = x->r1startframe * b_nchans;
	start_sample2 = x->r2startframe * b_nchans;
	swapframes = x->swapframes;
	
	// store block1 samples
	for(i = 0; i < swapframes * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			storage[i+j] = b_samples[start_sample1 + i + j];
		}
	}
	// swap block2 into block1 location, fadein
	
	for(i = 0, k = 0; i < fade * b_nchans; i += b_nchans, k++){
		fadephase = ((float)k / (float) fade) * PIOVERTWO;
		fadein_gain = sin(fadephase);
		fadeout_gain = cos(fadephase);
		for(j = 0; j < b_nchans; j++){
			mix_sample = fadein_gain * b_samples[start_sample2 + i + j] + fadeout_gain * b_samples[start_sample1 + i + j];
			b_samples[start_sample1 + i + j] = mix_sample;
		}
	}
	// middle part, pure swap
	
	for(i = fade * b_nchans; i < (swapframes-fade) * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			b_samples[start_sample1 + i + j] = b_samples[start_sample2 + i + j];
		}
	}
	// fade out
	
	for(i = (swapframes-fade) * b_nchans, k = 0; i < swapframes * b_nchans; i += b_nchans, k++){
		fadephase = ((float)k / (float) fade) * PIOVERTWO;
		fadein_gain = sin(fadephase);
		fadeout_gain = cos(fadephase);
		for(j = 0; j < b_nchans; j++){
			mix_sample = fadeout_gain * b_samples[start_sample2 + i + j] + fadein_gain * b_samples[start_sample1 + i + j];
			b_samples[start_sample1 + i + j] = mix_sample;
		}
	}
	// now mix stored block1 into block2 location
	// swap block2 into block1 location, fadein
	
	for(i = 0, k = 0; i < fade * b_nchans; i += b_nchans, k++){
		fadein_gain = (float)k / (float) fade;
		fadeout_gain = 1.0 - fadein_gain;
		for(j = 0; j < b_nchans; j++){
			mix_sample = fadein_gain * storage[i + j] + fadeout_gain * b_samples[start_sample2 + i + j];
			b_samples[start_sample2 + i + j] = mix_sample;
		}
	}
	// middle part, pure swap
	
	for(i = fade * b_nchans; i < (swapframes-fade) * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			b_samples[start_sample2 + i + j] = storage[i + j];
		}
	}
	// fade out
	
	for(i = (swapframes-fade) * b_nchans, k = 0; i < swapframes * b_nchans; i += b_nchans, k++){
		fadein_gain = (float)k / (float) fade;
		fadeout_gain = 1.0 - fadein_gain;
		for(j = 0; j < b_nchans; j++){
			mix_sample = fadeout_gain * storage[i + j] + fadein_gain * b_samples[start_sample2 + i + j];
			b_samples[start_sample2 + i + j] = mix_sample;
		}
	}
	buffet_update(x);
    exit: buffer_unlocksamples(wavebuf_b);
}


void buffet_specswap(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
	float maxframes = x->maxframes;
	float fade = x->fade;
	long b_frames;
	long swapframes;
	long r1startframe;
	long r1endframe;
	long r2startframe;
	long r2endframe;
	long region1;
	long region2;
	long start_sample1;
	long start_sample2;
	float *storage = x->storage;
	float *b_samples;
	float mix_sample;
	float fadein_gain;
	float fadeout_gain;
	float fadephase;
	long b_nchans;
	int i,j,k;
    
	t_buffer_obj *wavebuf_b;
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        goto exit;
    }
	
	if(b_frames < maxframes * 2 + 1){
		error("buffer must contain at least twice as many samples as the maximum swap size");
		return;
	}
	if(b_nchans > 2){
		error("buffet~ only accepts mono or stereo buffers");
		return;
	}
	
	r1startframe = x->sr * .001 * atom_getfloatarg(0,argc,argv);
	r2startframe = x->sr * .001 * atom_getfloatarg(1,argc,argv);
	swapframes = x->sr * .001 * atom_getfloatarg(2,argc,argv);
	if( r1startframe < 0 || r1startframe >= b_frames){
		error("bad first skip time");
		return;
	}
	if( r2startframe < 0 || r2startframe >= b_frames){
		error("bad second skip time");
		return;
	}
	
	r1endframe = r1startframe + swapframes;
	r2endframe = r2startframe + swapframes;
	region1 = r1startframe;
	region2 = b_frames - r1endframe;
	/*
	 post("min: %.0f, max: %.0f, total: %d, swap: %d",minframes, maxframes,totalframes, swapframes);
	 post("r1st %d, r1end %d, region1 %d, region2 %d",r1startframe, r1endframe, region1, region2);
	 */
	if(swapframes > x->storage_maxframes) {
		error("swapsize %d is larger than %d; reset maximum swap.", swapframes,x->storage_maxframes );
		return;
	}
	if(r1endframe >= b_frames){
		error("block 1 reads beyond buffer!");
		return;
	}
	if(r2endframe >= b_frames){
		error("block 2 reads beyond buffer!");
		return;
	}
	
	// store block1 samples
	start_sample1 = r1startframe * b_nchans;
	start_sample2 = r2startframe * b_nchans;
	for(i = 0; i < swapframes * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			storage[i+j] = b_samples[start_sample1 + i + j];
		}
	}
	// swap block2 into block1 location, fadein
	
	for(i = 0, k = 0; i < fade * b_nchans; i += b_nchans, k++){
		fadephase = ((float)k / (float) fade) * PIOVERTWO;
		fadein_gain = sin(fadephase);
		fadeout_gain = cos(fadephase);
		for(j = 0; j < b_nchans; j++){
			mix_sample = fadein_gain * b_samples[start_sample2 + i + j] + fadeout_gain * b_samples[start_sample1 + i + j];
			b_samples[start_sample1 + i + j] = mix_sample;
		}
	}
	// middle part, pure swap
	
	for(i = fade * b_nchans; i < (swapframes-fade) * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			b_samples[start_sample1 + i + j] = b_samples[start_sample2 + i + j];
		}
	}
	// fade out
	
	for(i = (swapframes-fade) * b_nchans, k = 0; i < swapframes * b_nchans; i += b_nchans, k++){
		fadephase = ((float)k / (float) fade) * PIOVERTWO;
		fadein_gain = sin(fadephase);
		fadeout_gain = cos(fadephase);
		for(j = 0; j < b_nchans; j++){
			mix_sample = fadeout_gain * b_samples[start_sample2 + i + j] + fadein_gain * b_samples[start_sample1 + i + j];
			b_samples[start_sample1 + i + j] = mix_sample;
		}
	}
	// now mix stored block1 into block2 location
	// swap block2 into block1 location, fadein
	
	for(i = 0, k = 0; i < fade * b_nchans; i += b_nchans, k++){
		fadein_gain = (float)k / (float) fade;
		fadeout_gain = 1.0 - fadein_gain;
		for(j = 0; j < b_nchans; j++){
			mix_sample = fadein_gain * storage[i + j] + fadeout_gain * b_samples[start_sample2 + i + j];
			b_samples[start_sample2 + i + j] = mix_sample;
		}
	}
	// middle part, pure swap
	
	for(i = fade * b_nchans; i < (swapframes-fade) * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			b_samples[start_sample2 + i + j] = storage[i + j];
		}
	}
	// fade out
	
	for(i = (swapframes-fade) * b_nchans, k = 0; i < swapframes * b_nchans; i += b_nchans, k++){
		fadein_gain = (float)k / (float) fade;
		fadeout_gain = 1.0 - fadein_gain;
		for(j = 0; j < b_nchans; j++){
			mix_sample = fadeout_gain * storage[i + j] + fadein_gain * b_samples[start_sample2 + i + j];
			b_samples[start_sample2 + i + j] = mix_sample;
		}
	}
	buffet_update(x);
    exit: buffer_unlocksamples(wavebuf_b);    
}


void buffet_retroblock(t_buffet *x)
{
	float minframes = x->minframes;
	float maxframes = x->maxframes;
	float fade = x->fade;
	long b_frames;
	long swapframes;
	long r1startframe;
	long r1endframe;
	long start_sample1;
	float *storage = x->storage;
	float *b_samples;
	float mix_sample;
	float fadein_gain;
	float fadeout_gain;
	//	float fadephase;
	long ub1, lb1;
	long ub2, lb2;
	long block1size,block2size;
	long b_nchans;
	long syncframe;
	int i,j,k;
	t_buffer_obj *wavebuf_b;
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        goto exit;
    }
	
	if(b_frames < maxframes * b_nchans + 1){
		error("buffer must contain at least twice as many samples as the maximum swap size");
		return;
	}
	if(b_nchans > 2){
		error("buffet~ only accepts mono or stereo buffers");
		return;
	}
	syncframe = x->sync * b_frames;
	swapframes = buffet_boundrand(minframes, maxframes);
	if(x->sync <= 0.0 ){ // either no sync signal or it is zero
		r1startframe = buffet_boundrand(0.0, (float)(b_frames-swapframes));
		r1endframe = r1startframe + swapframes;
	} else {
		// avoid swapping where we are playing from buffer
		lb1 = 0;
		ub1 = syncframe - CUSHION_FRAMES; // could be reading buffer backwards
		lb2 = ub1 + CUSHION_FRAMES;
		ub2 = b_frames - 1;
		block1size = ub1 - lb1;
		block2size = ub2 - lb2;
		
		if(block1size < maxframes && block2size < maxframes ) {
			error("could not reverse block");
			return;
		}
		if(block1size >= maxframes && block2size >= maxframes){
			if(buffet_boundrand(0.0,1.0) > 0.5){
				r1startframe = buffet_boundrand(0.0, (float)(ub1-swapframes));
			} else {
				r1startframe = buffet_boundrand((float)lb2, (float)(ub2-swapframes));
			}
		} else if(block1size < maxframes) {
			r1startframe = buffet_boundrand((float)lb2, (float)(ub2-swapframes));
		} else {
			r1startframe = buffet_boundrand(0.0, (float)(ub1-swapframes));
		}
		r1endframe = r1startframe + swapframes;
	}
	
	
	if(r1endframe >= b_frames){
		error("retro beyond bounds");
		return;
	}
	
	// store block1 samples
	start_sample1 = r1startframe * b_nchans;
	// start_sample2 = r2startframe * b_nchans;
	// store block in reversed order
	for(k = 0, i = (swapframes-1) * b_nchans; i > 0; i -= b_nchans, k += b_nchans){
		for(j = 0; j < b_nchans; j++){
			storage[i+j] = b_samples[start_sample1 + k + j];
		}
	}
	// swap block2 into block1 location, fadein
	
	for(i = 0, k = 0; i < fade * b_nchans; i += b_nchans, k++){
		fadein_gain = (float)k / (float) fade;
		fadeout_gain = 1.0 - fadein_gain;
		for(j = 0; j < b_nchans; j++){
			mix_sample = fadein_gain * storage[i + j] + fadeout_gain * b_samples[start_sample1 + i + j];
			b_samples[start_sample1 + i + j] = mix_sample;
		}
	}
	// middle part, pure swap
	
	
	for(i = fade * b_nchans; i < (swapframes-fade) * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			b_samples[start_sample1 + i + j] = storage[i + j];
		}
	}
	// fade out
	
	for(i = (swapframes-fade) * b_nchans, k = 0; i < swapframes * b_nchans; i += b_nchans, k++){
		fadein_gain = (float)k / (float) fade;
		fadeout_gain = 1.0 - fadein_gain;
		for(j = 0; j < b_nchans; j++){
			mix_sample = fadeout_gain * storage[i + j] + fadein_gain * b_samples[start_sample1 + i + j];
			b_samples[start_sample1 + i + j] = mix_sample;
		}
	}
	buffet_update(x);
    exit: buffer_unlocksamples(wavebuf_b);
}

// clicky version
void buffet_nakedswap(t_buffet *x)
{
	long minframes = x->minframes;
	long maxframes = x->maxframes;
	long b_frames;
	long swapframes;
	long r1startframe;
	long r1endframe;
	long r2startframe;
	long r2endframe;
	long region1;
	long region2;
	long start_sample1;
	long start_sample2;
	float *storage = x->storage;
	float *b_samples;
	long b_nchans;
	int i,j;
	t_buffer_obj *wavebuf_b;
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        return;
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    if(! b_samples ){
        goto exit;
    }
	
	if(b_frames < maxframes * 2 + 1){
		error("buffer must contain at least twice as many samples as the maximum swap size");
		return;
	}
	if(b_nchans != 2){
		error("buffet~ only accepts stereo buffers");
		return;
	}
	swapframes = buffet_boundrand((float)minframes, (float)maxframes);
	r1startframe = buffet_boundrand(0.0, (float)(b_frames-swapframes));
	r1endframe = r1startframe + swapframes;
	region1 = r1startframe;
	region2 = b_frames - r1endframe;
	
	if(swapframes > region1){
		r2startframe = buffet_boundrand((float)r1endframe,(float)(b_frames-swapframes));
	} else if(swapframes > region2) {
		r2startframe = buffet_boundrand(0.0,(float)(r1startframe-swapframes));
	} else {
		if(buffet_boundrand(0.0,1.0) > 0.5){
			r2startframe = buffet_boundrand(0.0,(float)(r1startframe-swapframes));
		} else {
			r2startframe = buffet_boundrand((float)r1endframe,(float)(b_frames-swapframes));
		}
	}
	r2endframe = r2startframe + swapframes;
	if(r2startframe < 0 || r1startframe < 0){
		error("start frame less than zero!");
		return;
	}
	if(r2endframe >= b_frames || r1endframe >= b_frames){
		error("end frame reads beyond buffer!");
		return;
	}
	// Now swap the samples. For now no cross fade
	start_sample1 = r1startframe * b_nchans;
	start_sample2 = r2startframe * b_nchans;
	for(i = 0; i < swapframes * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			storage[i+j] = b_samples[start_sample1 + i + j];
		}
	}
	// swap block1 into block2
	for(i = 0; i < swapframes * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			b_samples[start_sample1 + i + j] = b_samples[start_sample2 + i + j];
		}
	}
	// swap stored block into block1
	for(i = 0; i < swapframes * b_nchans; i += b_nchans){
		for(j = 0; j < b_nchans; j++){
			b_samples[start_sample2 + i + j] = storage[i + j];
		}
	}
	buffet_update(x);
    exit: buffer_unlocksamples(wavebuf_b);
}

void buffet_dblclick(t_buffet *x)
{
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    buffer_view(buffer_ref_getobject(x->src_buffer_ref));
}

void buffet_setbuf(t_buffet *x, t_symbol *wavename)
{
	x->wavename = wavename;	
}


void buffet_catbuf(t_buffet *x, t_symbol *msg, short argc, t_atom *argv)
{
    t_buffer_obj *srcbuf_b;
    t_buffer_obj *catbuf_b;
    t_symbol *dubsource;
    long b_nchans;
    long b_frames;
    long b_cat_nchans,b_cat_frames;
    float *b_samples, *b_cat_samples;
    float *storage = NULL; // copy current contents here before resizing
    

    short argcc = 1;
    t_atom argvv;
    long sizethis, sizecat, newsize, oldframes;
    
    /* Load source buffer */
    
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    srcbuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(srcbuf_b == NULL){
        // warning message here...
        return;
    }
    b_nchans = buffer_getchannelcount(srcbuf_b);
    b_frames= buffer_getframecount(srcbuf_b);
    b_samples = buffer_locksamples(srcbuf_b);
    
    /* Load buffer to concatenate */
    
    dubsource = atom_getsymarg(0,argc,argv);
    if(!x->cat_buffer_ref){
        x->cat_buffer_ref = buffer_ref_new((t_object*)x, dubsource);
    } else {
        buffer_ref_set(x->cat_buffer_ref, dubsource);
    }
    catbuf_b = buffer_ref_getobject(x->cat_buffer_ref);
    if(catbuf_b == NULL){
        // warning message here...
        buffer_unlocksamples(srcbuf_b);
        return;
    }
    
    b_cat_nchans = buffer_getchannelcount(catbuf_b);
    b_cat_frames = buffer_getframecount(catbuf_b);
    b_cat_samples = buffer_locksamples(catbuf_b);
    
    if( b_cat_nchans != b_nchans ){
        error("%s: channel mismatch between %s (%d chans) and %s (%d chans)",
              x->wavename->s_name, b_nchans, dubsource->s_name, b_cat_nchans);
        goto zero;
    }
    
    sizethis = sizeof(float) * b_nchans * b_frames;
    sizecat = sizeof(float) * b_cat_nchans * b_cat_frames;
    storage = (float *) sysmem_newptr( sizethis );
    newsize = (b_frames + b_cat_frames); // new frame size
    sysmem_copyptr(b_samples, storage, sizethis);
    
    oldframes = b_frames;
    atom_setfloat(&argvv,newsize);
    
    buffer_unlocksamples(srcbuf_b);
    typedmess((void *)srcbuf_b, gensym("sizeinsamps"), argcc, &argvv);
    b_samples = buffer_locksamples(srcbuf_b);
    sysmem_copyptr(storage, b_samples, sizethis);
    sysmem_copyptr(b_cat_samples, b_samples + (oldframes * b_nchans), b_cat_frames * b_cat_nchans * sizeof(float));
zero:
    buffer_unlocksamples(srcbuf_b);
    buffer_unlocksamples(catbuf_b);
    buffet_update(x);
    if(storage != NULL){
        sysmem_freeptr(storage);
    }
}




t_int *buffet_perform(t_int *w)
{
	t_buffet *x = (t_buffet *) (w[1]);
	float *sync = (t_float *)(w[2]);
	int n = w[3];
	
	while(n--){
		x->sync = *sync++;
	}
	
	return (w+4);
}


float buffet_boundrand(float min, float max)
{
	return min + (max-min) * ((float) (rand() % RAND_MAX)/ (float) RAND_MAX);
}

void buffet_dsp_free(t_buffet *x)
{
	dsp_free((t_pxobject *)x);
	sysmem_freeptr(x->storage);
	sysmem_freeptr(x->listdata);
	sysmem_freeptr(x->rmsbuf);
	sysmem_freeptr(x->analbuf);
	sysmem_freeptr(x->onset);
    // object_free(x->src_buffer_ref);
    // object_free(x->dest_buffer_ref);
}

void buffet_resize(t_buffet *x, t_floatarg newsize)
{
	short argc = 1;
	t_atom *argv;
	t_symbol *sizemsg;
	
	argv = (t_atom *) sysmem_newptr(sizeof(t_atom));
	sizemsg = gensym("size");
	atom_setfloat(argv,newsize);

	t_buffer_obj *b;
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    b =buffer_ref_getobject(x->src_buffer_ref);
	
	if (b != NULL) {
        typedmess((void *)b, sizemsg, argc, argv);
	} else {
    	error("%s: no such buffer~ %s", OBJECT_NAME, x->wavename->s_name);
	}
}

void buffet_assist (t_buffet *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(mesages) Groove Sync Signal Input"); break;
    	}
	} else if (msg==2) {
		switch (arg){
			case 0: sprintf(dst,"(bang) Operation Completed"); break;
			case 1: sprintf(dst,"(list) Buffer Event Times"); break;
			case 2: sprintf(dst,"(float) Buffer Segment RMS Value"); break;
		}  
	}
}

t_max_err buffet_notify(t_buffet *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
//    buffer_ref_notify(x->dest_buffer_ref, s, msg, sender, data);
    return buffer_ref_notify(x->src_buffer_ref, s, msg, sender, data);
}





void buffet_dsp64(t_buffet *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    // dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)buffet_perform64, 0, NULL);
}

void buffet_perform64(t_buffet *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
    t_double	*out = outs[0];
    int			n = sampleframes;
    while (n--){
        *out++ = 0.0;
    }
}

 
        
