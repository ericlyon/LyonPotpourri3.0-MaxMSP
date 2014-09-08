#include "MSPd.h"

static t_class *chopper_class;

#define MAXSTORE 1024
#define OBJECT_NAME "chopper~"

typedef struct _chopper
{
    t_pxobject x_obj;
    // t_buffer_obj *the_buffer; // holds the actual buffer
    t_buffer_ref *buffer_ref; // MSP reference to the buffer
    t_symbol *wavename; // name of waveform buffer
    long l_chan; // may be expendable
    float increment;
    double fbindex;
    float buffer_duration; // watch this one
    int already_failed; // flag to send bad buffer message just one time
    long last_modtime; // last time the buffer was loaded (for diagnostics)
    float minseg;
    float maxseg;
    float segdur;
    float minincr;
    float maxincr;
    int loop_samps;
    int samps_to_go ;
    int loop_start;
    int bindex ;
    int taper_samps;
    int loop_min_samps;
    int loop_max_samps;
    float R;
    float ldev;
    float st_dev ;
    int lock_loop;
    int force_new_loop;
    int framesize;
    short mute;
    short disabled;
    int outlet_count;
    int *stored_starts;
    int *stored_samps;
    float *stored_increments;
    short preempt;
    short loop_engaged;
    short data_recalled;
    short initialize_loop;
    short fixed_increment_on;
    float fixed_increment;
    float retro_odds;
    float fade_level;
    int transp_loop_samps;
    float taper_duration;
    short lock_terminated;
    int preempt_samps;
    int preempt_count;
    short recalling_loop;
    float jitter_factor;
    float rdur_factor;
    float rinc_factor;
    short increment_adjusts_loop ;
    short loop_adjust_inverse;
    float saved_increment; // keep old increment when going to fixed increment
    long b_frames;
    long b_nchans;
    float *b_samples;
    void *sync_outlet; // outlet for sync w/ message sync_out
    float dropout_odds; // odds of a loop bit dropping out (0 .. 1)
    short dropout_state; // on or off?
    float amplitude_deviation; // variable gain per iteration (0 .. 1)
    float loop_gain; // actual gain per iteration
    t_float **msp_outlets; // link msp outlets just once in perform routine
    float minseg_attr; // attribute storage
    float maxseg_attr; // attribute storage
} t_chopper;

t_int *chopper_perform_stereo(t_int *w);
t_int *choppermono_perform(t_int *w);
t_int *chopper_perform_stereo_nointerpol(t_int *w);
t_int *chopper_perform_mono(t_int *w);
t_int *chopper_pd_perform(t_int *w);
void chopper_dsp(t_chopper *x, t_signal **sp);
void chopper_setbuf(t_chopper *x, t_symbol *s);
void chopper_mute(t_chopper *x, t_floatarg toggle);
void chopper_increment_adjust(t_chopper *x, t_floatarg toggle);
void chopper_adjust_inverse(t_chopper *x, t_floatarg toggle);
void *chopper_new(t_symbol *msg, short argc, t_atom *argv);
void chopper_in1(t_chopper *x, long n);
void chopper_set_minincr(t_chopper *x, double n);
void chopper_set_maxincr(t_chopper *x, double n);
void chopper_set_minseg(t_chopper *x, double n);
void chopper_set_maxseg(t_chopper *x, double n);
void chopper_taper(t_chopper *x, t_floatarg f);
void chopper_fixed_increment(t_chopper *x, t_floatarg f);
void chopper_lockme(t_chopper *x, t_floatarg n);
void chopper_force_new(t_chopper *x);
float chopper_boundrand(float min, float max);
void chopper_assist(t_chopper *x, void *b, long m, long a, char *s);
void chopper_dblclick(t_chopper *x);
void chopper_show_loop(t_chopper *x);
void chopper_set_loop(t_chopper *x, t_symbol *msg, short argc, t_atom *argv);
void chopper_randloop( t_chopper *x);
void chopper_store_loop(t_chopper *x, t_floatarg loop_bindex);
void chopper_recall_loop(t_chopper *x,  t_floatarg loop_bindex);
void chopper_free(t_chopper *x) ;
void chopper_retro_odds(t_chopper *x, t_floatarg f);
void chopper_jitter(t_chopper *x, t_floatarg f);
void chopper_jitterme(t_chopper *x);
void chopper_rdur(t_chopper *x, t_floatarg f);
void chopper_rdurme(t_chopper *x);
void chopper_rinc(t_chopper *x, t_floatarg f);
void chopper_rincme(t_chopper *x);
void chopper_adjust_inverse(t_chopper *x, t_floatarg toggle);
t_int *chopper_performtest(t_int *w);
void chopper_init(t_chopper *x, short initialized);
void chopper_seed(t_chopper *x, t_floatarg seed);
void attach_buffer(t_chopper *x, t_symbol *wavename);
void chopper_version(t_chopper *x);
t_int *chopper_perform_universal(t_int *w);
void chopper_sync_out(t_chopper *x);
void chopper_sync_in(t_chopper *x, t_symbol *msg, short argc, t_atom *argv);
void chopper_chopspec(t_chopper *x, t_symbol *msg, short argc, t_atom *argv);
t_max_err get_minseg(t_chopper *x, void *attr, long *ac, t_atom **av);
t_max_err set_minseg(t_chopper *x, void *attr, long *ac, t_atom *av);
t_max_err get_maxseg(t_chopper *x, void *attr, long *ac, t_atom **av);
t_max_err set_maxseg(t_chopper *x, void *attr, long *ac, t_atom *av);
void chopper_perform_universal64(t_chopper *x, t_object *dsp64, double **ins,
                                 long numins, double **outs,long numouts, long n,
                                 long flags, void *userparam);
void chopper_attach_buffer(t_chopper *x);
void chopper_dsp64(t_chopper *x, t_object *dsp64, short *count, double sr, long n, long flags);

t_symbol *ps_buffer;


int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.chopper~", (method)chopper_new, (method)dsp_free, sizeof(t_chopper), 0,A_GIMME, 0);
	
    class_addmethod(c,(method)chopper_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c,(method)chopper_set_minincr, "ft1", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_set_maxincr,"ft2", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_set_minseg, "ft3",A_FLOAT, 0);
	class_addmethod(c,(method)chopper_set_maxseg, "ft4",A_FLOAT, 0);
	class_addmethod(c,(method)chopper_lockme, "ft5", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_force_new, "bang", 0);
	class_addmethod(c,(method)chopper_force_new, "newloop", 0); // synonym for bang
	class_addmethod(c,(method)chopper_assist, "assist", A_CANT, 0);
	class_addmethod(c,(method)chopper_dblclick, "dblclick", A_CANT, 0);
	class_addmethod(c,(method)chopper_taper, "taper", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_fixed_increment, "fixed_increment", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_retro_odds, "retro_odds", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_show_loop, "show_loop", 0);
	class_addmethod(c,(method)chopper_set_loop, "set_loop", A_GIMME, 0);
	class_addmethod(c,(method)chopper_store_loop, "store_loop", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_recall_loop, "recall_loop", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_increment_adjust, "increment_adjust", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_adjust_inverse, "adjust_inverse", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_jitter, "jitter", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_rdur, "rdur", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_rinc, "rinc", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_mute, "mute", A_FLOAT, 0);
	class_addmethod(c,(method)chopper_setbuf, "setbuf", A_SYM, 0);
	class_addmethod(c,(method)chopper_sync_out, "sync_out", 0);
	class_addmethod(c,(method)chopper_sync_in, "sync_in", A_GIMME, 0);
	class_addmethod(c,(method)chopper_chopspec, "chopspec", A_GIMME, 0);
	
	ps_buffer = gensym("buffer~");
	class_dspinit(c);
	CLASS_ATTR_FLOAT(c, "dropout_odds", 0, t_chopper, dropout_odds);
	CLASS_ATTR_DEFAULT_SAVE(c, "dropout_odds", 0, "0.0");
	CLASS_ATTR_LABEL(c, "dropout_odds", 0, "Dropout Odds");
	
	CLASS_ATTR_FLOAT(c, "amplitude_deviation", 0, t_chopper, amplitude_deviation);
	CLASS_ATTR_DEFAULT_SAVE(c, "amplitude_deviation", 0, "0.0");
	CLASS_ATTR_LABEL(c, "amplitude_deviation", 0, "Amplitude Deviation");
	
	CLASS_ATTR_FLOAT(c, "minimum_increment", 0, t_chopper, minincr);
	CLASS_ATTR_DEFAULT_SAVE(c, "minimum_increment", 0, "0.8");
	CLASS_ATTR_LABEL(c, "minimum_increment", 0, "Minimum Increment");
    
	CLASS_ATTR_FLOAT(c, "maximum_increment", 0, t_chopper, maxincr);
	CLASS_ATTR_DEFAULT_SAVE(c, "maximum_increment", 0, "1.2");
	CLASS_ATTR_LABEL(c, "maximum_increment", 0, "Maximum Increment");
    
	CLASS_ATTR_FLOAT(c, "minimum_segdur", 0, t_chopper, minseg);
	CLASS_ATTR_DEFAULT_SAVE(c, "minimum_segdur", 0, "250.0");
	CLASS_ATTR_ACCESSORS(c, "minimum_segdur", (method)get_minseg, (method)set_minseg);
	CLASS_ATTR_LABEL(c, "minimum_segdur", 0, "Minimum Segment Duration");
	
	CLASS_ATTR_FLOAT(c, "maximum_segdur", 0, t_chopper, maxseg);
	CLASS_ATTR_DEFAULT_SAVE(c, "maximum_segdur", 0, "2000.0");
	CLASS_ATTR_ACCESSORS(c, "maximum_segdur", (method)get_maxseg, (method)set_maxseg);
	CLASS_ATTR_LABEL(c, "maximum_segdur", 0, "Maximum Segment Duration");
	
	CLASS_ATTR_ORDER(c, "minimum_increment",    0, "1");
	CLASS_ATTR_ORDER(c, "maximum_increment",    0, "2");
	CLASS_ATTR_ORDER(c, "minimum_segdur",    0, "3");
	CLASS_ATTR_ORDER(c, "maximum_segdur",    0, "4");
	CLASS_ATTR_ORDER(c, "dropout_odds",    0, "5");
	CLASS_ATTR_ORDER(c, "amplitude_deviation",    0, "6");
	
	class_register(CLASS_BOX, c);
	
	chopper_class = c;
	potpourri_announce(OBJECT_NAME);
    
	return 0;
}




void chopper_dblclick(t_chopper *x)
{
    chopper_attach_buffer(x);
    buffer_view(buffer_ref_getobject(x->buffer_ref));
}

void chopper_sync_out(t_chopper *x)
{
    
	
	t_atom synclist[8];
    t_buffer_obj	*buffer = buffer_ref_getobject(x->buffer_ref);
    long frames = buffer_getframecount(buffer);
	// chopper_attach_buffer(x);
	if( x->x_obj.z_disabled ){
		error("%s: no valid buffer", OBJECT_NAME);
		return;
	}
	post("%d %d %d %d %d %f %f %f", frames, x->loop_start, x->loop_samps, x->transp_loop_samps, x->samps_to_go,
		 x->increment, x->segdur, x->fbindex);
    
	atom_setlong(synclist, frames);
	atom_setlong(synclist+1, x->loop_start);
	atom_setlong(synclist+2, x->loop_samps);
	atom_setlong(synclist+3, x->transp_loop_samps);
	atom_setlong(synclist+4, x->samps_to_go);
	atom_setfloat(synclist+5, x->increment);
	atom_setfloat(synclist+6,  x->segdur);
	atom_setfloat(synclist+7,  x->fbindex);
	outlet_anything(x->sync_outlet, gensym("sync_in"), 8, synclist);
    
	
}

void chopper_chopspec(t_chopper *x, t_symbol *msg, short argc, t_atom *argv)
{
	if(argc < 4){
		post("chopspec: mininc maxinc minseg maxseg");
		return;
	}
	atom_arg_getfloat(&x->minincr, 0, argc, argv);
	atom_arg_getfloat(&x->maxincr, 1, argc, argv);
	atom_arg_getfloat(&x->minseg, 2, argc, argv);
	atom_arg_getfloat(&x->maxseg, 3, argc, argv);
    // convert to seconds
	x->minseg *= 0.001;
	x->maxseg *= 0.001;
}

void chopper_sync_in(t_chopper *x, t_symbol *msg, short argc, t_atom *argv)
{
	t_atom_long b_frames, loop_start, loop_samps, transp_loop_samps, samps_to_go;
	float increment,segdur,fbindex;
    t_buffer_obj	*buffer = buffer_ref_getobject(x->buffer_ref);
	// could we compensate for MSP vector delay ?
    // t_max_err atom_arg_getlong(t_atom_long *c, long idx, long ac, const t_atom *av);

	atom_arg_getlong(&b_frames, 0, argc, argv);
	atom_arg_getlong(&loop_start, 1, argc, argv);
	atom_arg_getlong(&loop_samps, 2, argc, argv);
	atom_arg_getlong(&transp_loop_samps, 3, argc, argv);
	atom_arg_getlong(&samps_to_go, 4, argc, argv);
	atom_arg_getfloat(&increment, 5, argc, argv);
	atom_arg_getfloat(&segdur, 6, argc, argv);
	atom_arg_getfloat(&fbindex, 7, argc, argv);
	post("%d %d %d %d %d %f %f %f", b_frames, loop_start, loop_samps, transp_loop_samps, samps_to_go, increment, segdur, fbindex);
	if(b_frames != buffer_getframecount(buffer)){
		error("%s: buffer mismatch: you can only sync to the same buffer",OBJECT_NAME);
		return;
	}
	x->loop_start = loop_start;
	x->loop_samps = loop_samps;
	x->transp_loop_samps = transp_loop_samps;
	x->samps_to_go = samps_to_go;
	x->increment = increment;
	x->segdur = segdur;
	x->fbindex = fbindex;
	
}

void chopper_assist(t_chopper *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(bang) Force New Loop ");
				break;
			case 1: sprintf(dst,"(float) Minimum Increment ");
				break;
			case 2: sprintf(dst,"(float) Maximum Increment ");
				break;
			case 3: sprintf(dst,"(float) Minimum Segdur ");
				break;
			case 4: sprintf(dst,"(float) Maximum Segdur ");
				break;
			case 5: sprintf(dst,"(int) Non-Zero Locks Loop ");
				break;
				
		}
	} else if (msg==2) {
		if(arg < x->outlet_count){
			sprintf(dst,"(signal) Output");
		} else {
			sprintf(dst,"(list) Sync Data");
		}
	}
	
}


void chopper_mute(t_chopper *x, t_floatarg toggle)
{
	x->mute = (int) toggle;
}

void chopper_seed(t_chopper *x, t_floatarg seed)
{
    
	srand((long)seed);
}


void chopper_increment_adjust(t_chopper *x, t_floatarg toggle)
{
	x->increment_adjusts_loop = (short)toggle;
	
}

void chopper_adjust_inverse(t_chopper *x, t_floatarg toggle)
{
	x->loop_adjust_inverse = (short)toggle;
}

void chopper_fixed_increment(t_chopper *x, t_floatarg f)
{
	float new_samps = 0;
	float rectf;
	
	x->fixed_increment = f;
	if( f ){
		x->fixed_increment_on = 1;
		x->saved_increment = x->increment;
	} else {
		x->fixed_increment_on = 0;
		x->increment = x->saved_increment;
	}
	
	rectf = fabs(f);
	
	if( x->lock_loop && rectf > 0.0 ){
		
		if( x->loop_adjust_inverse ){
			new_samps = (float) x->loop_samps * rectf ;
		} else {
			new_samps = (float) x->loop_samps / rectf ;
		}
		if( f > 0 ){
			if( x->loop_start + new_samps >= x->framesize ){
				return;
			} else {
				x->increment = x->fixed_increment;
			}
		} else {
			if( x->loop_start - new_samps < 0) {
				return;
			} else {
				x->increment = x->fixed_increment;
			}
		}
		
	}
    
	
	
}

void chopper_jitter(t_chopper *x, t_floatarg f)
{
	f *= 0.1; // scale down a bit
	if( f >= 0. && f <= 1.0 )
		x->jitter_factor = f;
}

void chopper_rdur(t_chopper *x, t_floatarg f)
{
	
	if( f >= 0. && f <= 1.0 )
		x->rdur_factor = f;
}

void chopper_rinc(t_chopper *x, t_floatarg f)
{
	// f *= 0.1; // scale down a bit
	
	if( f >= 0. && f <= 1.0 )
		x->rinc_factor = f;
}


void chopper_retro_odds(t_chopper *x, t_floatarg f)
{
	
	if( f < 0 )
		f = 0;
	if( f > 1 )
		f = 1;
	
	x->retro_odds = f;
	
}

void chopper_show_loop(t_chopper *x)
{
	post("start: %d, samps: %d, increment: %f", x->loop_start, x->transp_loop_samps, x->increment);
	post("set_loop %d %d %f", x->loop_start, x->transp_loop_samps, x->increment);
}

void chopper_store_loop(t_chopper *x, t_floatarg f)
{
	int loop_bindex = (int) f;
	
	if( loop_bindex < 0 || loop_bindex >= MAXSTORE ){
		error("bindex %d out of range", loop_bindex);
		return;
	}
	
	x->stored_starts[ loop_bindex ] = x->loop_start;
	x->stored_samps[ loop_bindex ] = x->transp_loop_samps;
	x->stored_increments[ loop_bindex ] = x->increment;
	
	post("storing loop %d: %d %d %f",loop_bindex,
		 x->stored_starts[ loop_bindex ],x->stored_samps[ loop_bindex ],  x->stored_increments[ loop_bindex ] );
	
	// post("loop stored at position %d", loop_bindex );
}

void chopper_recall_loop(t_chopper *x, t_floatarg f)
{
	// bug warning: recall preceding store will crash program
	// need to add warning
	int loop_bindex = (int) f;
	
	if( loop_bindex < 0 || loop_bindex >= MAXSTORE ){
		error("bindex %d out of range", loop_bindex);
		return;
	}
	
	if( ! x->stored_samps[ loop_bindex ] ){
		error("no loop stored at position %d!", loop_bindex);
		return;
	}
	
	
	x->loop_start = x->stored_starts[ loop_bindex ];
	x->samps_to_go = x->transp_loop_samps = x->stored_samps[ loop_bindex ];
	
	if( x->loop_min_samps > x->transp_loop_samps )
		x->loop_min_samps = x->transp_loop_samps ;
	if( x->loop_max_samps < x->transp_loop_samps )
		x->loop_max_samps = x->transp_loop_samps ;
	x->increment = x->stored_increments[ loop_bindex ];
	/*
	 post("restoring loop %d: %d %d %f",loop_bindex,
	 x->loop_start,x->transp_loop_samps,  x->increment );
	 post("min %d max %d",x->loop_min_samps, x->loop_max_samps );
	 */
	x->preempt_count = x->preempt_samps;
	// post("preempt samps:%d", x->preempt_count);
	x->recalling_loop = 1;
	//  x->data_recalled = 1;
}

void chopper_set_loop(t_chopper *x, t_symbol *msg, short argc, t_atom *argv)
{
	if( argc < 3 ){
		error("format: start samples increment");
		return;
	}
	x->loop_start = atom_getintarg(0,argc,argv);
	x->transp_loop_samps = atom_getintarg(1,argc,argv);
	x->increment = atom_getfloatarg(2,argc,argv);
	x->data_recalled = 1;
	
	x->samps_to_go = x->transp_loop_samps;
	x->fbindex = x->bindex = x->loop_start;
	//  post("loop set to: st %d samps %d incr %f", x->loop_start, x->loop_samps,x->increment);
}

void chopper_taper(t_chopper *x, t_floatarg f)
{
	f /= 1000.0;
	
	if( f > 0 ){
		x->taper_samps = (float) x->R * f ;
	}
	if( x->taper_samps < 2 )
		x->taper_samps = 2;
}


void *chopper_new(t_symbol *msg, short argc, t_atom *argv)
{
	t_atom_long tint, i;
	t_chopper *x = (t_chopper *)object_alloc(chopper_class);
	
	x->outlet_count = 1;
	x->already_failed = 0;
	atom_arg_getsym(&x->wavename,0,argc,argv);
	//	post("chopper wave is %s", x->wavename);
	tint = 1;
	x->taper_duration = 20.0;
	atom_arg_getlong(&tint,1,argc,argv);
	x->outlet_count = tint;
	//	post("chopper chan count is %d or %d", x->outlet_count, tint);
	atom_arg_getfloat(&x->taper_duration,2,argc,argv);

	
	dsp_setup((t_pxobject *)x,0);
	x->x_obj.z_misc |= Z_NO_INPLACE;
	floatin((t_object *)x,5);
	floatin((t_object *)x,4);
	floatin((t_object *)x,3);
	floatin((t_object *)x,2);
	floatin((t_object *)x,1);
    // create list outlet (generic type)
    //	x->sync_outlet = listout((t_pxobject *)x);
	x->sync_outlet = outlet_new((t_pxobject *)x, NULL);
    
	for(i = 0; i < x->outlet_count; i++){
		outlet_new((t_object *)x, "signal");
	}
	x->R = (float) sys_getsr();
	chopper_init(x,0);
	return (x);
}

void chopper_init(t_chopper *x, short initialized)
{
	if(!initialized){
		srand(time(0));
		x->msp_outlets = (t_float **) calloc(x->outlet_count, sizeof(t_float *));
		if(!x->R) {
			error("zero sampling rate - set to 44100");
			x->R = 44100;
		}
		x->minseg = 0.1;
		x->maxseg = 0.8 ;
		x->minincr = 0.5 ;
		x->maxincr = 2.0 ;
		x->increment = 1.0;
		x->data_recalled = 0;
		x->segdur = 0;
		x->bindex = 0 ;
		x->taper_duration /= 1000.0;
		if( x->taper_duration < .0001 || x->taper_duration > 10.0 )
			x->taper_duration = .0001;
		x->increment_adjusts_loop = 0;
		x->taper_samps = x->R * x->taper_duration;
		if(x->taper_samps < 2)
			x->taper_samps = 2;
		
		x->preempt_samps = 5;
		x->loop_adjust_inverse = 0;
		x->preempt = 1;
		x->recalling_loop = 0;
		x->ldev = 0;
		x->lock_loop = 0;
		x->buffer_duration = 0.0 ;
		x->st_dev = 0.0;
		x->framesize = 0;
		x->force_new_loop = 0;
		x->mute = 0;
		x->disabled = 0;
		x->initialize_loop = 1;
		x->loop_engaged = 0;
		x->fixed_increment_on = 0;
		x->retro_odds = 0.5;
		x->fade_level = 1.0;
		x->lock_terminated = 0;
		x->dropout_odds = 0.0;
		x->dropout_state = 0;
		x->amplitude_deviation = 0.0;
		x->loop_gain = 1.0;
		x->stored_starts = calloc(MAXSTORE, sizeof(int));
		x->stored_samps = calloc(MAXSTORE, sizeof(int));
		x->stored_increments = calloc(MAXSTORE, sizeof(int));
		
		
	} else {
		x->taper_samps = x->R * x->taper_duration;
		if(x->taper_samps < 2)
			x->taper_samps = 2;
	}
}

void chopper_free(t_chopper *x)
{
	dsp_free((t_pxobject *) x);
    
	free(x->stored_increments);
	free(x->stored_samps);
	free(x->stored_starts);
}

void chopper_jitterme(t_chopper *x)
{
	float new_start;
	float jitter_factor = x->jitter_factor;
	new_start = (1.0 + chopper_boundrand(-jitter_factor, jitter_factor) ) * (float) x->loop_start ;
	
	if( new_start < 0 ){
		//    error("jitter loop %d out of range", new_start);
		new_start = 0;
		
	}
	else if( new_start + x->transp_loop_samps >= x->framesize ){
		//    error("jitter loop %d out of range", new_start);
		new_start = x->framesize - x->transp_loop_samps ;
	}
	if( new_start >= 0 )
		x->loop_start = new_start;
}

void chopper_rdurme(t_chopper *x)
{
	float new_dur;
	float rdur_factor = x->rdur_factor;
	float durmult;
	if(rdur_factor < 0 || rdur_factor > 1){
		return;
	}
	durmult = 1.0 + chopper_boundrand( -rdur_factor, rdur_factor);
	
	new_dur = durmult * (float) x->transp_loop_samps;
	x->loop_min_samps = x->minseg * x->R;
	x->loop_max_samps = x->maxseg * x->R;
	if( new_dur > x->loop_max_samps )
		new_dur = x->loop_max_samps;
	if( new_dur < x->loop_min_samps )
		new_dur = x->loop_min_samps;
	// post("min %d max %d current %d durmult %f", x->loop_min_samps, x->loop_max_samps, x->transp_loop_samps, durmult );
	// post("new loop size would be %d", (int) new_dur);
	x->transp_loop_samps = new_dur;
	// chopper_show_loop(x);
}

void chopper_rincme(t_chopper *x )
{
	float new_inc = 0;
	//  int count = 0;
	int new_samps;
	float rinc_factor = x->rinc_factor;
	
	/* test generate a new increment */
	new_inc = (1.0 + chopper_boundrand( 0.0, rinc_factor)) ;
	if( chopper_boundrand(0.0,1.0) > 0.5 ){
		new_inc = 1.0 / new_inc;
	}
	
	// test for transgression
	
	//	post("increment adjust:%d",x->increment_adjusts_loop);
	
	if( fabs(new_inc * x->increment) < x->minincr ) {
		new_inc = x->minincr / fabs(x->increment) ; // now when we multiply - increment is set to minincr
	}
	else if ( fabs(new_inc * x->increment) > x->maxincr ){
		new_inc = x->maxincr / fabs(x->increment) ; // now when we multiply - increment is set to maxincr
	}
	
	if(x->increment_adjusts_loop){
		new_samps = (float) x->transp_loop_samps / new_inc ;
	} else {
		new_samps = x->transp_loop_samps;
	}
    
	new_inc *= x->increment ;
	if( x->increment > 0 ){
		if( x->loop_start + new_samps >= x->framesize ){
			new_samps = (x->framesize - 1) - x->loop_start ;
		}
	} else {
		if( x->loop_start - new_samps < 0) {
			new_samps = x->loop_start + 1;
		}
	}
	x->transp_loop_samps = new_samps;
	x->increment = new_inc;
}

void chopper_randloop( t_chopper *x )
{
    t_buffer_obj	*buffer = buffer_ref_getobject(x->buffer_ref);
    long framesize = buffer_getframecount(buffer);
	// int framesize = x->the_buffer->b_frames;
	float segdur = x->segdur;
	int loop_start = x->loop_start;
	int loop_samps = x->loop_samps;
	int transp_loop_samps = x->transp_loop_samps;
	int samps_to_go = x->samps_to_go;
	float increment = x->increment;
	float minincr = x->minincr;
	float maxincr = x->maxincr;
	float minseg = x->minseg;
	float maxseg = x->maxseg;
	float buffer_duration = x->buffer_duration;
	float R = x->R;
	float fixed_increment = x->fixed_increment;
	short fixed_increment_on = x->fixed_increment_on;
	float retro_odds = x->retro_odds;
	
	if(fixed_increment_on){
		increment = fixed_increment;
	} else {
		increment = chopper_boundrand(minincr,maxincr);
	}
	// special protection
	if( fabs(increment) < 0.00001 ){
		increment = 0.01;
	}
	segdur = chopper_boundrand( minseg, maxseg );
	loop_samps = segdur * R * increment; // total samples in segment
	//  post("rand: segdur %f R %f increment %f lsamps %d",segdur,R,increment,loop_samps);
	transp_loop_samps = segdur * R ; // actual count of samples to play back
	samps_to_go = transp_loop_samps;
	if( loop_samps >= framesize ){
		loop_samps = framesize - 1;
		loop_start = 0;
	} else {
		//    post("rand: bufdur %f segdur %f",buffer_duration, segdur);
		loop_start = R * chopper_boundrand( 0.0, buffer_duration - segdur );
		if( loop_start + loop_samps >= framesize ){
			loop_start = framesize - loop_samps;
			if( loop_start < 0 ){
				loop_start = 0;
				error("%s: negative starttime",OBJECT_NAME);
			}
		}
	}
	if( chopper_boundrand(0.0,1.0) < retro_odds ){
		increment *= -1.0 ;
		loop_start += (loop_samps - 1);
	}
	
	// post("randset: lstart %d lsamps %d incr %f segdur %f",loop_start,loop_samps,increment,segdur);
	x->samps_to_go = samps_to_go;
	x->fbindex = x->bindex = loop_start;
	x->loop_start = loop_start;
	x->loop_samps = loop_samps;
	x->transp_loop_samps = transp_loop_samps;
	x->increment = increment;
	x->segdur = segdur;
}


void chopper_perform_universal64(t_chopper *x, t_object *dsp64, double **ins,
                                 long numins, double **outs,long numouts, long n,
                                 long flags, void *userparam)
{
	int bindex, bindex2;
	int i,k;
	float sample, frac;
	float tmp1, tmp2;
	
	/*********************************************/
	
	float *tab;
	long frames;
	long nc;
	
	float segdur = x->segdur;
	int taper_samps = x->taper_samps ;
	float taper_duration = x->taper_duration;
	float minseg = x->minseg;
	float maxseg = x->maxseg;
	int lock_loop = x->lock_loop;
	int force_new_loop = x->force_new_loop;
	short initialize_loop = x->initialize_loop;
	float fade_level = x->fade_level;
	short preempt = x->preempt;
	
	int preempt_count = x->preempt_count;
	int preempt_samps = x->preempt_samps;
	short recalling_loop = x->recalling_loop;
	float preempt_gain = 1.0;
	
	float jitter_factor = x->jitter_factor;
	float rdur_factor = x->rdur_factor;
	float rinc_factor = x->rinc_factor;
	float fbindex = x->fbindex;
	float increment = x->increment;
	int transp_loop_samps = x->transp_loop_samps;
	int loop_start = x->loop_start;
	int loop_samps = x->loop_samps;
	int samps_to_go = x->samps_to_go;
	float loop_gain = x->loop_gain;
	long active_outlets;
	t_buffer_obj *the_buffer;
    
	// quick bail on bad buffer!
    chopper_attach_buffer(x);
    the_buffer = buffer_ref_getobject(x->buffer_ref);
    if ( the_buffer == NULL){
        if(!x->already_failed){
            x->already_failed = 1;
            object_post((t_object *)x, "\"%s\" is an invalid buffer", x->wavename->s_name);
        }
        return;
    }
	tab = buffer_locksamples(the_buffer);
	if (!tab){
		return;
    }
    frames = buffer_getframecount(the_buffer);
    nc = buffer_getchannelcount(the_buffer);
    // now OWN the buffer, freeing it at the end of this job

    
    
    
    // is buffer zero sized or do its channels not match output channels? DIE!!!!
	if(x->outlet_count != nc){
        if(!x->already_failed){
            x->already_failed = 1;
            object_post((t_object *)x, "mismatch between buffer \"%s\" which has %d channels and the chopper~ which requires %d channels",
                        x->wavename->s_name, nc, x->outlet_count);
        }
        buffer_perform_end(the_buffer);
        return;
    }
    if(frames == 0){
        if(!x->already_failed){
            x->already_failed = 1;
            object_post((t_object *)x, "\"%s\" is an empty buffer", x->wavename->s_name);
        }
        buffer_perform_end(the_buffer);
        return;
    }
    
    
	//	post("frames %d chans %d valid %d",frames, nc, b_valid);
	active_outlets = nc;
    
	
	/* ATTACH OUTLETS */
	// unnecessary clear here??
	if(x->mute || frames <= 0 || x->x_obj.z_disabled){
		for(i = 0; i < numouts; i++){
			memset((void *)outs[i],0,sizeof(t_double) * n);
		}
		return;
	}
	//	post("chopper IS active");
	/* ONLY CALL WHEN THERE IS A NEW BUFFER ATTACHED */
	if(initialize_loop){
		x->buffer_duration = (double) frames / x->R;
		x->framesize = frames;
		chopper_randloop(x);
		bindex = x->fbindex;
		initialize_loop = 0;
	}
	
	if(maxseg > x->buffer_duration){
		maxseg = x->buffer_duration;
	}
	
	if(minseg < 2. * taper_duration)
		minseg = taper_duration;
	
	/* SET INITIAL SEGMENT */
	
	bindex = fbindex = x->fbindex;
	
	
	for(k = 0; k < n; k++) {
		if(lock_loop)  {
			
			if (recalling_loop) {
				
				bindex = floor((double)fbindex);
				frac = fbindex - bindex ;
				fbindex += increment;
				--preempt_count;
				preempt_gain = fade_level  * ( (float) preempt_count / (float) preempt_samps );
				
				for(i = 0; i < numouts; i++){
					if( x->dropout_state ) {
						outs[i][k] = 0.0;
					} else {
						if(bindex < frames - 1){
							tmp1 = tab[bindex * nc + i];
							tmp2 = tab[(bindex + 1) * nc + i] ;
							sample = (tmp1 + frac * (tmp2-tmp1)) * preempt_gain;
						} else {
							sample = tab[bindex * nc + i] * preempt_gain;
						}
						outs[i][k] = sample * loop_gain;
					}
				}
				
				if(preempt_count <= 0) {
					bindex = fbindex = loop_start;
					recalling_loop = 0;
				}
			}
			
			else if(force_new_loop){
				if( bindex < 0 || bindex >= frames ){
					fbindex = bindex = frames / 2;
				}
				if( preempt && preempt_samps > x->samps_to_go ){ /* PREEMPT FADE */
					
					--preempt_count;
					preempt_gain = fade_level  * ( (float) preempt_count / (float) preempt_samps );
					bindex = fbindex ;
					frac = fbindex - bindex ;
					fbindex += increment;
					for(i = 0; i < active_outlets; i++){
						if( x->dropout_state ) {
							outs[i][k] = 0.0;
						} else {
							if(bindex < frames - 1){
								tmp1 = tab[bindex * nc + i];
								tmp2 = tab[(bindex + 1) * nc + i] ;
								sample = (tmp1 + frac * (tmp2-tmp1)) * preempt_gain;
							} else {
								sample = tab[bindex * nc + i] * preempt_gain;
							}
							outs[i][k]= sample * loop_gain;
						}
					}
					
					if(! preempt_count) {
						chopper_randloop(x);
						bindex = fbindex = x->fbindex;
						samps_to_go = x->samps_to_go;
						loop_start = x->loop_start;
						loop_samps = x->loop_samps;
						transp_loop_samps = x->transp_loop_samps;
						increment = x->increment;
						segdur = x->segdur;
						force_new_loop = 0;
					}
				}
				/* IMMEDIATE FORCE NEW LOOP AFTER PREEMPT FADE */
				else {
					
					chopper_randloop(x);
					bindex = fbindex = x->fbindex;
					samps_to_go = x->samps_to_go;
					loop_start = x->loop_start;
					loop_samps = x->loop_samps;
					transp_loop_samps = x->transp_loop_samps;
					increment = x->increment;
					segdur = x->segdur;
					force_new_loop = 0;
					bindex2 = bindex << 1;
					fbindex += increment;
					
					--samps_to_go;
					if(samps_to_go <= 0 ){
						bindex = fbindex = loop_start;
						samps_to_go = transp_loop_samps;
					}
					bindex = fbindex ;
					frac = fbindex - bindex ;
					fbindex += increment;
					
					for(i = 0; i < active_outlets; i++){
						if(x->dropout_state){
							outs[i][k] = 0.0;
						}
						else {
							if(bindex < frames - 1){
								tmp1 = tab[bindex * nc + i];
								tmp2 = tab[(bindex + 1) * nc + i] ;
								sample = (tmp1 + frac * (tmp2-tmp1)) * preempt_gain;
							} else {
								sample = tab[bindex * nc + i] * preempt_gain;
							}
							sample *= loop_gain;
							if(samps_to_go > transp_loop_samps - taper_samps ){
								fade_level =  (float)(transp_loop_samps - samps_to_go)/(float)taper_samps ;
								outs[i][k] = sample * fade_level;
							} else if(samps_to_go < taper_samps ) {
								fade_level = (float)(samps_to_go)/(float)taper_samps;
								outs[i][k] = sample * fade_level;
							} else {
								fade_level = 1.0;
								outs[i][k] = sample;
							}
						}
					}
				}
			}
			/* REGULAR PLAYBACK */
			else {
				
				if( bindex < 0 || bindex >= frames ){
					// error("lock_loop: bindex %d is out of range", bindex);
					chopper_randloop(x);
					bindex = fbindex = x->fbindex;
					samps_to_go = x->samps_to_go;
					loop_start = x->loop_start;
					loop_samps = x->loop_samps;
					transp_loop_samps = x->transp_loop_samps;
					increment = x->increment;
					segdur = x->segdur;
				}
				bindex = floor( (double) fbindex );
				frac = fbindex - bindex ;
				
				fbindex += increment;
				
				--samps_to_go;
				if(samps_to_go <= 0){
					// set the dropout here
					if( chopper_boundrand(0.0, 1.0) < x->dropout_odds ){
						x->dropout_state = 1;
					} else {
						x->dropout_state = 0;
					}
					if( x->amplitude_deviation <= 0.0 ){
						loop_gain = 1.0;
					} else {
						loop_gain = chopper_boundrand( (1.0 - x->amplitude_deviation), 1.0) ;
					}
					if( rdur_factor ){
						chopper_rdurme(x);
					}
					if( jitter_factor ){
						chopper_jitterme(x);
					}
					if( rinc_factor ) {
						chopper_rincme(x);
					}
					bindex = fbindex = loop_start = x->loop_start;
					samps_to_go = transp_loop_samps = x->transp_loop_samps;
					increment = x->increment;
					
				}
				
				for(i = 0; i < active_outlets; i++){
					if( x->dropout_state ) {
						outs[i][k] = 0.0;
					} else {
						if(bindex < frames - 1){
							tmp1 = tab[bindex * nc + i];
							tmp2 = tab[(bindex + 1) * nc + i] ;
							sample = (tmp1 + frac * (tmp2-tmp1)) * preempt_gain;
						} else {
							sample = tab[bindex * nc + i] * preempt_gain;
						}
						sample *= loop_gain;
						if(samps_to_go > transp_loop_samps - taper_samps ){
							fade_level =  (float)(transp_loop_samps - samps_to_go)/(float)taper_samps ;
							outs[i][k] = sample * fade_level;
						} else if(samps_to_go < taper_samps) {
							fade_level = (float)(samps_to_go)/(float)taper_samps;
							outs[i][k] = sample * fade_level;
						} else {
							fade_level = 1.0;
							outs[i][k] = sample;
						}
					}
				}
			}
		} /* END OF LOCK LOOP */
		/* RECALL STORED LOOP */
		
		else if (recalling_loop) {
			--preempt_count;
			if( preempt_samps <= 0 ){
				preempt_gain = fade_level;
			} else {
				preempt_gain = fade_level  * ( (float) preempt_count / (float) preempt_samps );
			}
			
			bindex = fbindex;
			frac = fbindex - bindex;
			fbindex += increment;
			
			for(i = 0; i < active_outlets; i++){
				if( x->dropout_state ) {
					outs[i][k] = 0.0;
				} else {
					if(bindex < frames - 1){
						tmp1 = tab[bindex * nc + i];
						tmp2 = tab[(bindex + 1) * nc + i] ;
						sample = (tmp1 + frac * (tmp2-tmp1)) * preempt_gain;
					} else {
						sample = tab[bindex * nc + i] * preempt_gain;
					}
					sample *= loop_gain;
					outs[i][k] = sample * preempt_gain;
				}
			}
			
			
			if( preempt_count <= 0) {
				fbindex = loop_start;
				bindex = fbindex;
				recalling_loop = 0;
			}
		}
		
		else {
			if( force_new_loop ){
				/* FORCE LOOP CODE : MUST PREEMPT */
				force_new_loop = 0;
				/* NEED CODE HERE*/
			}
			/* NORMAL OPERATION */
			else {
				/* default level */
				fade_level = 1.0;
				
				if( bindex < 0 || bindex >= frames ){
					chopper_randloop(x);
					bindex = fbindex = x->fbindex;
					samps_to_go = x->samps_to_go;
					loop_start = x->loop_start;
					loop_samps = x->loop_samps;
					transp_loop_samps = x->transp_loop_samps;
					increment = x->increment;
					segdur = x->segdur;
				}
				bindex = fbindex;
				frac = fbindex - bindex;
				fbindex += increment;
				
				for(i = 0; i < active_outlets; i++){
					if( x->dropout_state ) {
						outs[i][k] = 0.0;
					} else {
						if(bindex < frames - 1){
							tmp1 = tab[bindex * nc + i];
							tmp2 = tab[(bindex + 1) * nc + i] ;
							sample = (tmp1 + frac * (tmp2-tmp1)) * preempt_gain;
						} else {
							sample = tab[bindex * nc + i] * preempt_gain;
						}
						sample *= loop_gain;
						if(samps_to_go > transp_loop_samps - taper_samps ){
							fade_level =  (float)(transp_loop_samps - samps_to_go)/(float)taper_samps ;
							outs[i][k] = sample * fade_level;
						} else if(samps_to_go < taper_samps) {
							fade_level = (float)(samps_to_go)/(float)taper_samps;
							outs[i][k] = sample * fade_level;
						} else {
							fade_level = 1.0;
							outs[i][k] = sample;
						}
					}
				}
				
				--samps_to_go;
				if(samps_to_go <= 0){
					// set the dropout here
					if( chopper_boundrand(0.0, 1.0) < x->dropout_odds ){
						x->dropout_state = 1;
					} else {
						x->dropout_state = 0;
					}
					if( x->amplitude_deviation <= 0.0 ){
						loop_gain = 1.0;
					} else {
						loop_gain = chopper_boundrand( (1.0 - x->amplitude_deviation), 1.0) ;
					}
					chopper_randloop(x);
					bindex = fbindex = x->fbindex;
					samps_to_go = x->samps_to_go;
					loop_start = x->loop_start;
					loop_samps = x->loop_samps;
					transp_loop_samps = x->transp_loop_samps;
					increment = x->increment;
					segdur = x->segdur;
				}
			}
		}
	}
	x->loop_gain = loop_gain;
	x->recalling_loop = recalling_loop;
	x->fade_level = fade_level;
	x->initialize_loop = initialize_loop;
	x->maxseg = maxseg;
	x->minseg = minseg;
	x->segdur = segdur;
	x->force_new_loop = force_new_loop;
	x->fbindex = fbindex;
	x->samps_to_go = samps_to_go;
	x->fbindex = fbindex;
	x->loop_start = loop_start;
	x->loop_samps = loop_samps;
	x->increment = increment;
    // release the buffer
    buffer_unlocksamples(the_buffer);
}



void chopper_force_new(t_chopper *x)
{
	x->preempt_count = x->preempt_samps;
	x->force_new_loop = 1;
	
}

void chopper_lockme(t_chopper *x, t_floatarg n)
{
	x->lock_loop = (short) n;
}

//set min time for loop
void chopper_set_minincr(t_chopper *x, double n)
{
	
	if( n < .005 ){
		n = .005;
	}
	x->minincr = n ;
}

// set max incr for loop
void chopper_set_maxincr(t_chopper *x, double n)
{
	if( n > 20.0 ){
		n = 20.0;
	}
	//post("set maxincr to %f", n);
	x->maxincr =  n;
}

// set deviation factor
void chopper_set_maxseg(t_chopper *x, double n)
{
	
	n /= 1000.0 ; // convert to seconds
	if( n > 120. )
		n = 120.;
	//post("set maxseg to %f", n);
	x->maxseg = n;
	x->loop_max_samps = x->maxseg * x->R;
}

void chopper_set_minseg(t_chopper *x, double n)
{
	
	n /= 1000.0 ; // convert to seconds
	
	if( n < 0.03 )
		n = 0.03;
	//post("set minseg to %f", n);
	x->minseg = n;
	x->loop_min_samps = x->minseg * x->R;
}

t_max_err get_maxseg(t_chopper *x, void *attr, long *ac, t_atom **av)
{
	if (ac && av) {
		char alloc;
		
		if (atom_alloc(ac, av, &alloc)) {
			return MAX_ERR_GENERIC;
		}
		x->maxseg_attr = x->maxseg * 1000.0; // converted to ms
		atom_setfloat(*av, x->maxseg_attr);
	}
	return MAX_ERR_NONE;
}


t_max_err set_maxseg(t_chopper *x, void *attr, long *ac, t_atom *av)
{
	if (ac && av) {
		float val = atom_getfloat(av);
		x->maxseg = val * 0.001; // convert to seconds
	}
	return MAX_ERR_NONE;
}

t_max_err get_minseg(t_chopper *x, void *attr, long *ac, t_atom **av)
{
	if (ac && av) {
		char alloc;
		
		if (atom_alloc(ac, av, &alloc)) {
			return MAX_ERR_GENERIC;
		}
		x->minseg_attr = x->minseg * 1000.0; // converted to ms
		atom_setfloat(*av, x->minseg_attr);
	}
	return MAX_ERR_NONE;
}


t_max_err set_minseg(t_chopper *x, void *attr, long *ac, t_atom *av)
{
	if (ac && av) {
		float val = atom_getfloat(av);
		x->minseg = val * 0.001; // convert to seconds
	}
	return MAX_ERR_NONE;
}

void chopper_setbuf(t_chopper *x, t_symbol *s)
{
	if( s == x->wavename ){
		// post("that is already the working buffer!");
	} else {
		x->wavename = s;
		x->initialize_loop = 1;
        x->already_failed = 0;
	}
}

// new improved version
void chopper_attach_buffer(t_chopper *x)
{
    
	if (!x->buffer_ref)
		x->buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
	else
		buffer_ref_set(x->buffer_ref, x->wavename);
}


void chopper_dsp64(t_chopper *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if(!sr)
        return;
	if(x->R != sr){
		x->R = sr;
		chopper_init(x,1);
	}
    object_method(dsp64, gensym("dsp_add64"),x,chopper_perform_universal64,0,NULL);
}



#define LONG_RAND_MAX 2147483648.0
float chopper_boundrand(float min, float max)
{
    
	return min + (max-min) * ((float) (rand() % RAND_MAX)/(float)RAND_MAX);
}
