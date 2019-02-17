#import "MSPd.h"
#include "bashfest.h"

/// #import "MSPd.h"
// Updated for Pd 6 December 2006
// added protection for setbuf (renamed attach_buffer)

/* THIS IS PROBABLY THE LIMITING FACTOR FOR LARGE BUFFER SIZES
 SO, MAKE THIS A PARAMETER, -OR- BUFFER THE INPUT TRIGGER ITSELF!!!
 */
#define DEFAULT_MAX_OVERLAP (8) // number of overlapping instances allowed
#define ACTIVE 0
#define INACTIVE 1
#define MAX_VEC 4096
#define DEFAULT_BUFFER_SIZE 4000.0 // 4 second default buffer size * 2
#define DEFAULT_LATENCY 8192 //latency in samples after a trigger for note to start
#define MAX_PARAMETERS 2048
#define PROCESS_COUNT 20
#define CYCLE_MAX 1024

#define OBJECT_NAME "bashfest~"


static t_class *bashfest_class;


void *bashfest_new(t_symbol *msg, short argc, t_atom *argv);
t_int *bashfest_perform_hosed(t_int *w);
void bashfest_dsp(t_bashfest *x, t_signal **sp, short *count);
void bashfest_assist (t_bashfest *x, void *b, long msg, long arg, char *dst);
void bashfest_dsp_free(t_bashfest *x);
//int bashfest_set_parameters(t_bashfest *x,float *params);
int bashfest_set_parameters(t_bashfest *x,float *params, float transpose_factor);
t_int *bashfest_perform(t_int *w);
void bashfest_deploy_dsp(t_bashfest *x);
void bashfest_copy_to_MSP_buffer(t_bashfest *x, int slot);
/*user messages*/
void bashfest_stop(t_bashfest *x);
void bashfest_info(t_bashfest *x);
void bashfest_dblclick(t_bashfest *x);
void bashfest_mute(t_bashfest *x, t_floatarg t);
void bashfest_maximum_process(t_bashfest *x, t_floatarg n);
void bashfest_minimum_process(t_bashfest *x, t_floatarg n);
void bashfest_setbuf(t_bashfest *x, t_symbol *wavename);
void attach_buffer(t_bashfest *x);
void bashfest_flatodds(t_bashfest *x);
void bashfest_killproc(t_bashfest *x, t_floatarg p);
void bashfest_soloproc(t_bashfest *x, t_floatarg p);
void bashfest_latency(t_bashfest *x, t_floatarg n);
void bashfest_verbose(t_bashfest *x, t_floatarg t);
void bashfest_block_dsp(t_bashfest *x, t_floatarg t);
void bashfest_gozero(t_bashfest *x);
void bashfest_grab(t_bashfest *x);
void bashfest_setodds(t_bashfest *x,t_symbol *msg, short argc, t_atom *argv);
void bashfest_tcycle(t_bashfest *x,t_symbol *msg, short argc, t_atom *argv);
void bashfest_version(t_bashfest *x);
void bashfest_dsp64(t_bashfest *x, t_object *dsp64, short *count, 
                    double samplerate, long maxvectorsize, long flags);

/* function code */

void killdc( float *inbuf, int in_frames, int channels, t_bashfest *x );
void ringmod(t_bashfest *x, int slot, int *pcount);
void retrograde(t_bashfest *x, int slot, int *pcount);
void comber(t_bashfest *x, int slot, int *pcount);
void transpose(t_bashfest *x, int slot, int *pcount);
void flange(t_bashfest *x, int slot, int *pcount);
void butterme(t_bashfest *x, int slot, int *pcount);
void truncateme(t_bashfest *x, int slot, int *pcount);
void sweepreson(t_bashfest *x, int slot, int *pcount);
void slidecomb(t_bashfest *x, int slot, int *pcount);
void reverb1(t_bashfest *x, int slot, int *pcount);
void ellipseme(t_bashfest *x, int slot, int *pcount);
void feed1me(t_bashfest *x, int slot, int *pcount);
void flam1(t_bashfest *x, int slot, int *pcount);
void flam2(t_bashfest *x, int slot, int *pcount);
void expflam(t_bashfest *x, int slot, int *pcount);
void comb4(t_bashfest *x, int slot, int *pcount);
void ringfeed(t_bashfest *x, int slot, int *pcount);
void resonbashfest(t_bashfest *x, int slot, int *pcount);
void stv(t_bashfest *x, int slot, int *pcount);
void compdist(t_bashfest *x, int slot, int *pcount);
t_max_err bashfest_latency_get(t_bashfest *x, void *attr, long *ac, t_atom **av);
t_max_err bashfest_latency_set(t_bashfest *x, void *attr, long ac, t_atom *av);
void bashfest_perform64(t_bashfest *x, t_object *dsp64, double **ins, 
                        long numins, double **outs,long numouts, long vectorsize,
                        long flags, void *userparam);
t_max_err bashfest_notify(t_bashfest *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

int C74_EXPORT main(void)
{
	t_class *c;
	// SERIOUS MEMORY LEAK HERE - need to free it all up!
	c = class_new("el.bashfest~", (method)bashfest_new, (method)bashfest_dsp_free, sizeof(t_bashfest), 0,A_GIMME, 0);
	
	class_addmethod(c,(method)bashfest_assist,"assist", A_CANT, 0);
	class_addmethod(c,(method)bashfest_dblclick,"dblclick", A_CANT, 0);
	class_addmethod(c,(method)bashfest_setbuf,"setbuf", A_SYM, 0);
	class_addmethod(c,(method)bashfest_stop,"stop", 0);
	class_addmethod(c,(method)bashfest_flatodds,"flatodds", 0);
	class_addmethod(c,(method)bashfest_soloproc,"soloproc", A_FLOAT, 0);
	class_addmethod(c,(method)bashfest_killproc,"killproc", A_FLOAT, 0);
	class_addmethod(c,(method)bashfest_latency,"latency", A_FLOAT, 0);
	class_addmethod(c,(method)bashfest_mute,"mute", A_FLOAT, 0);
	class_addmethod(c,(method)bashfest_verbose,"verbose", A_FLOAT, 0);
	class_addmethod(c,(method)bashfest_setodds,"setodds", A_GIMME, 0);
	class_addmethod(c,(method)bashfest_tcycle,"tcycle", A_GIMME, 0);
	class_addmethod(c,(method)bashfest_gozero,"gozero", 0);
	class_addmethod(c,(method)bashfest_grab,"grab", 0);
	class_addmethod(c,(method)bashfest_maximum_process,"maximum_process", A_FLOAT, 0);
	class_addmethod(c,(method)bashfest_minimum_process,"minimum_process", A_FLOAT, 0);
	class_addmethod(c,(method)bashfest_block_dsp,"block_dsp", A_FLOAT, 0);
    class_addmethod(c, (method)bashfest_dsp64, "dsp64", A_CANT,0);
    class_addmethod(c, (method)bashfest_notify, "notify", A_CANT, 0);
	
	CLASS_ATTR_LONG(c, "latency", 0, t_bashfest, latency_samples);
	CLASS_ATTR_DEFAULT_SAVE(c, "latency", 0, "8192");
	CLASS_ATTR_ACCESSORS(c, "latency", (method)bashfest_latency_get, (method)bashfest_latency_set);
	CLASS_ATTR_LABEL(c, "latency", 0, "Latency");
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	bashfest_class = c;	
	potpourri_announce(OBJECT_NAME);
	return 0;
}

t_max_err bashfest_notify(t_bashfest *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    return buffer_ref_notify(x->buffer_ref, s, msg, sender, data);
}

void bashfest_block_dsp(t_bashfest *x, t_floatarg t)
{
	
	x->block_dsp = (short)t;
	
}
void bashfest_maximum_process(t_bashfest *x, t_floatarg n)
{
	if(n < 0){
		error("illegal val to maximum_process");
		return;
	}
	x->max_process_per_note = (int)n;
}

void bashfest_minimum_process(t_bashfest *x, t_floatarg n)
{
	if(n < 0){
		error("illegal val to minimum_process");
		return;
	}
	x->min_process_per_note = (int)n;
}

void bashfest_verbose(t_bashfest *x, t_floatarg t)
{
	x->verbose = (short)t;
}

t_max_err bashfest_latency_get(t_bashfest *x, void *attr, long *ac, t_atom **av)
{
	if (ac && av) {
		char alloc;
		
		if (atom_alloc(ac, av, &alloc)) {
			return MAX_ERR_GENERIC;
		}
		atom_setlong(*av, (long)x->latency_samples);
	}
	return MAX_ERR_NONE;
}

t_max_err bashfest_latency_set(t_bashfest *x, void *attr, long ac, t_atom *av)
{
	if (ac && av) {
		long a = atom_getlong(av);
		bashfest_latency(x,(int) a);
	}
	return MAX_ERR_NONE;
}


void bashfest_latency(t_bashfest *x, t_floatarg fp)
{
	int n = (int) fp;
	if(n < x->vs){
		error("%s: latency %d cannot be less than %d samples",OBJECT_NAME, n, x->vs);
		return;
	}
	x->latency_samples = n;
}

void bashfest_stop(t_bashfest *x)
{
	int i;
	
	for(i = 0; i < x->overlap_max; i++){
		x->events[i].status = INACTIVE;
	}
}


void bashfest_mute(t_bashfest *x, t_floatarg t)
{
	x->mute = (short)t;
}


void bashfest_grab(t_bashfest *x)
{
	x->grab = 1;
}

void bashfest_tcycle(t_bashfest *x,t_symbol *msg, short argc, t_atom *argv)
{
	t_cycle tcycle = x->tcycle;
	int i;
	float data=1.0;
	
	if(argc < 1){
		error("no data for tcycle!");
		return;
	} else if(argc > CYCLE_MAX){
		error("%d is the maximum size tcycle",CYCLE_MAX);
		return;
	}
	x->tcycle.len = argc;
	x->tcycle.p = 0;
	for(i=0;i<argc;i++){
		atom_arg_getfloat(&data,i,argc,argv);
		if(data <= 0.0){
			error("bad data for tcycle:%f",data);
		} else {
			tcycle.data[i] = data;
		}
	}
}

void bashfest_gozero(t_bashfest *x)
{
	x->tcycle.p = 0;
}

void bashfest_setodds(t_bashfest *x,t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	
	if(argc > PROCESS_COUNT){
		error("there are only %d processes",PROCESS_COUNT);
		return;
	}
	for(i=0;i<PROCESS_COUNT;i++){
		x->odds[i] = 0.0;
	}
	
	
	for(i=0;i<argc;i++){
		x->odds[i] = atom_getfloatarg(i,argc,argv);
	}
	
	setweights(x->odds,PROCESS_COUNT);
}

void bashfest_soloproc(t_bashfest *x, t_floatarg fp)
{
	int i;
	int p = (int) fp;
	if(p < 0 || p >= PROCESS_COUNT){
		error("bad %d",p);
	}
	for(i=0;i<PROCESS_COUNT;i++){
		x->odds[i] = 0.0;
	}
	x->odds[p] = 1.0;
	setweights(x->odds,PROCESS_COUNT);
}

void bashfest_killproc(t_bashfest *x, t_floatarg fp)
{
	int i;
	int p = (int) fp;
	if(p < 0 || p >= PROCESS_COUNT){
		error("bad %d",p);
	}
	for(i=0;i<PROCESS_COUNT;i++){
		x->odds[i] = 1.0;
	}
	x->odds[p] = 0.0;
	setweights(x->odds,PROCESS_COUNT);
}

void bashfest_flatodds(t_bashfest *x)
{
	int i;
	for(i=0;i<PROCESS_COUNT;i++){
		x->odds[i] = 1.0;
	}
	setweights(x->odds,PROCESS_COUNT);
}


void *bashfest_new(t_symbol *msg, short argc, t_atom *argv)
{
	t_bashfest *x = (t_bashfest *)object_alloc(bashfest_class);
    
	int i;
	long membytes = 0;
	float tmpfloat;
	srand(time(0));
	
	x->sr = sys_getsr();
	x->vs = sys_getblksize();
	if(! x->sr)
		x->sr = 44100;
	
	
	x->work_buffer_size = DEFAULT_BUFFER_SIZE;
	if(argc < 1 ){
		error("%s: must specify a buffer!",OBJECT_NAME);
		x->hosed = 1;
		return NIL;
	}
	
	/* argument list: buffer name, work buffer duration, latency in samples, number of overlaps */	
	atom_arg_getsym(&x->wavename,0,argc,argv);
	atom_arg_getfloat(&x->work_buffer_size,1,argc,argv);
	tmpfloat = DEFAULT_LATENCY;
	atom_arg_getfloat(&tmpfloat,2,argc,argv);
	x->latency_samples = tmpfloat;
	tmpfloat = DEFAULT_MAX_OVERLAP;
	atom_arg_getfloat(&tmpfloat,3,argc,argv);
	x->overlap_max = tmpfloat;
	
	
	dsp_setup((t_pxobject *)x,2); // added inlet
	outlet_new((t_pxobject *)x, "signal");
	outlet_new((t_pxobject *)x, "signal");
	x->x_obj.z_misc |= Z_NO_INPLACE;
	
	x->sinelen = 8192;
	
	x->verbose = 0;
	x->most_recent_event = 0;
	x->active_events = 0;
	x->increment = 1.0;
	x->block_dsp = 0;
	x->grab = 0;
	
	/* buffer contains space for both input and output, thus factor of 2 */
	x->buf_frames = 2 * x->work_buffer_size * .001 * x->sr;
	
	x->buf_samps = x->buf_frames * 2;
	x->halfbuffer = x->buf_samps / 2;
	
	x->maxdelay = 1.0; // in seconds
	/*memory allocation */
	x->events = (t_event *) sysmem_newptr(x->overlap_max * sizeof(t_event));
	x->sinewave = (float *) sysmem_newptr( (x->sinelen + 1) * sizeof(float));
	x->params = (float *) sysmem_newptr(MAX_PARAMETERS * sizeof(float));
	x->odds = (float *) sysmem_newptr(64 * sizeof(float));
	
	for(i=0;i<64;i++)
		x->odds[i] = 0;
	putsine(x->sinewave, x->sinelen);
	for(i=0;i<x->overlap_max;i++){
		x->events[i].workbuffer = (float *) sysmem_newptr(x->buf_samps * sizeof(float));
	}
	x->delayline1 = (float *) sysmem_newptr( ((x->maxdelay * x->sr) + 2) * sizeof(float));
	x->delayline2 = (float *) sysmem_newptr( ((x->maxdelay * x->sr) + 2) * sizeof(float));
	x->max_mini_delay = .25;
	x->eel = (LSTRUCT *) sysmem_newptr(MAXSECTS * sizeof(LSTRUCT));
	for( i = 0; i < 4 ; i++ ){
		x->mini_delay[i] = 
		(float *) sysmem_newptr(((int)(x->sr * x->max_mini_delay) + 1)  * sizeof(float));
	}
	x->reverb_ellipse_data = (float *) sysmem_newptr(16 * sizeof(float));
	
	x->ellipse_data = (float **) sysmem_newptr(MAXFILTER * sizeof(float *));
	for(i=0;i<MAXFILTER;i++){
		x->ellipse_data[i] = (float *) sysmem_newptr(MAX_COEF * sizeof(float));
	}
	x->tf_len = 1;
	x->tf_len <<= 16;
	x->transfer_function = (float *) sysmem_newptr(x->tf_len * sizeof(float) );
	x->feedfunclen = 8192 ;
	x->feedfunc1 = (float *) sysmem_newptr( x->feedfunclen * sizeof(float) );
	x->feedfunc2 = (float *) sysmem_newptr( x->feedfunclen * sizeof(float) );
	x->feedfunc3 = (float *) sysmem_newptr( x->feedfunclen * sizeof(float) );
	x->feedfunc4 = (float *) sysmem_newptr( x->feedfunclen * sizeof(float) );
	x->flamfunc1len = 8192 ;
	x->flamfunc1 = (float *) sysmem_newptr( x->flamfunc1len * sizeof(float));
	setflamfunc1(x->flamfunc1,x->flamfunc1len);
	x->max_comb_lpt = 0.15 ;// watch out here
	x->combies = (CMIXCOMB *) sysmem_newptr(4 * sizeof(CMIXCOMB));
	for( i = 0; i < 4; i++ ){
		x->combies[i].len = x->sr * x->max_comb_lpt + 2;
		x->combies[i].arr = (float *) sysmem_newptr(x->combies[i].len * sizeof(float));
	}
	x->adsr = (CMIXADSR *) sysmem_newptr(1 * sizeof(CMIXADSR));
	x->adsr->len = 32768 ;
	x->adsr->func = (float *) sysmem_newptr(x->adsr->len * sizeof(float) );
	x->dcflt = (float *) sysmem_newptr(16 * sizeof(float));
	x->tcycle.data = (float *) sysmem_newptr(CYCLE_MAX * sizeof(float));
	x->tcycle.len = 0;
	for(i=0;i<x->overlap_max;i++){
		x->events[i].phasef = x->events[i].phase = 0.0;
	}
	
	membytes = x->overlap_max * sizeof(t_event);
	membytes += x->sinelen * sizeof(float);
	membytes += MAX_PARAMETERS * sizeof(float);
	membytes += 64 * sizeof(float);
	membytes += x->buf_samps * sizeof(float) * x->overlap_max;
	membytes += x->maxdelay * x->sr * sizeof(float) * 2;
	membytes += MAXSECTS * sizeof(LSTRUCT);
	membytes += ((int)(x->sr * x->max_mini_delay) + 1)  * sizeof(float) * 4;
	membytes += 16 * sizeof(float);
	membytes += MAXFILTER * sizeof(float *);
	membytes += MAX_COEF * sizeof(float) * MAXFILTER;
	membytes += x->tf_len * sizeof(float);
	membytes += x->feedfunclen * sizeof(float) * 4;
	membytes += x->flamfunc1len * sizeof(float);
	membytes += 4 * sizeof(CMIXCOMB);
	membytes += x->combies[0].len * sizeof(float) * 4;
	membytes += sizeof(CMIXADSR);
	membytes += x->adsr->len * sizeof(float);
	membytes += 16 * sizeof(float);
	membytes += CYCLE_MAX * sizeof(float);
	
	post("total memory for this bashfest %.2f MBytes",(float)membytes/1000000.);
	
	/* be sure to finish clearing memory */
	set_dcflt(x->dcflt);
	init_reverb_data(x->reverb_ellipse_data);
	init_ellipse_data(x->ellipse_data);
	
	for(i=0;i<PROCESS_COUNT;i++){
		x->odds[i] = 1;
	}	
	
	x->min_process_per_note = 0;
	x->max_process_per_note = 2;
	setweights(x->odds,PROCESS_COUNT);
	
	
	x->mute = 0;
	
	for(i = 0; i < x->overlap_max; i++){
		x->events[i].status = INACTIVE;
	}
	
	
	return (x);
}

void bashfest_dsp_free(t_bashfest *x)
{
	int i;
	dsp_free((t_pxobject *)x);
	sysmem_freeptr(x->events);
	sysmem_freeptr(x->sinewave);
	sysmem_freeptr(x->params);
	sysmem_freeptr(x->odds);
	for(i=0;i<x->overlap_max;i++){
		sysmem_freeptr(x->events[i].workbuffer);
	}
	sysmem_freeptr(x->delayline1);
	sysmem_freeptr(x->delayline2);
	sysmem_freeptr(x->eel);
	for( i = 0; i < 4 ; i++ ){
		sysmem_freeptr(x->mini_delay[i]);
	}
	sysmem_freeptr(x->reverb_ellipse_data);
	for(i=0;i<MAXFILTER;i++){
		sysmem_freeptr(x->ellipse_data[i] );
	}
	sysmem_freeptr(x->transfer_function);
	sysmem_freeptr(x->feedfunc1);
	sysmem_freeptr(x->feedfunc2);
	sysmem_freeptr(x->feedfunc3);
	sysmem_freeptr(x->feedfunc4);
	sysmem_freeptr(x->flamfunc1);
	for( i = 0; i < 4; i++ ){
		sysmem_freeptr(x->combies[i].arr);
	}
	sysmem_freeptr(x->adsr->func);
	sysmem_freeptr(x->adsr);
	sysmem_freeptr(x->dcflt);
	sysmem_freeptr(x->tcycle.data);
}	

void bashfest_dblclick(t_bashfest *x)
{
    attach_buffer(x);
    buffer_view(buffer_ref_getobject(x->buffer_ref));
}


// set new buffer name and kill all active notes
void bashfest_setbuf(t_bashfest *x, t_symbol *wavename)
{
	x->wavename = wavename;
	bashfest_stop(x);
}

void attach_buffer(t_bashfest *x)
{
	if (!x->buffer_ref)
		x->buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
	else
		buffer_ref_set(x->buffer_ref, x->wavename);
	strcpy(x->sound_name, x->wavename->s_name);
	
}

/* modified for dsp turnoff*/


void bashfest_perform64(t_bashfest *x, t_object *dsp64, double **ins, 
                          long numins, double **outs,long numouts, long n,
                          long flags, void *userparam)
{
	t_double *t_vec = ins[0];
	t_double *i_vec = ins[1];
	t_double *outchanL = outs[0];
	t_double *outchanR = outs[1];
	// int n = vectorsize;
	float *b_samples;
	long b_nchans;
	long b_frames;
	
	t_event *events = x->events;
	float increment = x->increment;
	long overlap_max = x->overlap_max;
	long iphase;
	long flimit;
	short insert_success;
	long new_insert;
	long i,j,k;
	t_cycle tcycle = x->tcycle;
	t_double gain;
	t_double transpose_factor;
	t_double frac;
	t_double samp1, samp2;
	t_double maxphase;
	long theft_candidate;
	t_float *processed_drum;// cheat
	char *sound_name = x->sound_name;
	t_double *trigger_vec;
	t_double *transpose_vec;
	int out_channels;
	int latency_samples = x->latency_samples;
    t_buffer_obj *the_buffer= NULL;
 
    if(x->mute || x->hosed){
        for(i = 0; i < n; i++){
            outs[0][i] = 0;
            outs[1][i] = 0;
        }
        goto laterAlligator;
	}
	
    
	trigger_vec = t_vec;
	transpose_vec = i_vec;
    
    // t_buffer_obj *the_buffer= NULL;
	attach_buffer(x);
    the_buffer = buffer_ref_getobject(x->buffer_ref);
    if(the_buffer == NULL){
        goto laterAlligator;
    }
    b_samples = buffer_locksamples(the_buffer);
		
	
    if(! b_samples){
        goto laterAlligator;
        if(!x->already_failed){
            x->already_failed = 1;
            object_post((t_object *)x, "\"%s\" is an invalid buffer", x->wavename->s_name);
        }
    }
	b_frames = buffer_getframecount(the_buffer);
	b_nchans = buffer_getchannelcount(the_buffer);

    if(b_nchans > 2){
        goto laterAlligator;
    }

	// this whole section is only when NO dsp is being applied
	
	if(x->block_dsp){
		/* computation savings if processing is blocked */
		
		
		/* preliminary transposition will be set here */
		
		if(tcycle.len > 0){
			increment = tcycle.data[tcycle.p];
			// post("position %d, increment %f",tcycle.p,increment );
		} else {
			increment = 1.0;
			//error("increment default, len is zero");
		}
		// initial cleaning
		for(i=0; i<n; i++){
			outchanL[i] = outchanR[i] = 0.0;
		}
		flimit = (b_frames - 1) * 2;
		for(i = 0; i < overlap_max; i++){
			if(events[i].status == ACTIVE){
				gain = events[i].gain;
				if(events[i].transpose){ // override tcycle
					increment = events[i].transpose;
				}
				if(b_nchans == 1){ /* mono */
					
					flimit = (b_frames - 1);
					for(j = 0; j < n; j++){
						if(events[i].countdown > 0){
							--events[i].countdown;
						} else {
							
							iphase = events[i].phasef;
							frac = events[i].phasef - iphase;
							
							if(increment > 0){
								if(iphase == flimit || increment == 1.0){
									outchanL[j] += b_samples[iphase] * gain;
									outchanR[j] += b_samples[iphase] * gain;
								} else {
									samp1 = b_samples[iphase];
									samp2 = b_samples[iphase+1];
									samp1 = gain * (samp1 + frac * (samp2-samp1));
									outchanL[j] += samp1;
									outchanR[j] += samp1;
								}
							} else { /*negative increment case (currently unused but might be useful)*/
								if(iphase == 0.0 || increment == -1.0 ){
									outchanL[j] += b_samples[iphase] * gain;
									outchanR[j] += b_samples[iphase] * gain;
								} else {
									samp2 = b_samples[iphase];
									samp1 = b_samples[iphase-1];
									samp1 = gain * (samp1 + frac * (samp2-samp1));
									outchanL[j] += samp1;
									outchanR[j] += samp1;
								}
							}
							events[i].phasef += increment;
							
							if( events[i].phasef < 0.0 || events[i].phasef >= b_frames){
								events[i].status = INACTIVE;
								events[i].phasef = 0;
								// post("valid exit mono note");
								break;
							}
						}
					}
				} else if(b_nchans == 2){
					/* stereo */
					
					for(j = 0; j < n; j++){
						if(events[i].countdown > 0){
							--events[i].countdown;
						} else {
							iphase = events[i].phasef;
							frac = events[i].phasef - iphase;
							iphase *= 2;
							if(increment > 0){
								if(iphase == flimit || increment == 1.0){
									outchanL[j] += b_samples[iphase] * gain;
									outchanR[j] += b_samples[iphase+1] * gain;
								} else {
									samp1 = b_samples[iphase];
									samp2 = b_samples[iphase+2];
									outchanL[j] += gain * (samp1 + frac * (samp2-samp1));
									samp1 = b_samples[iphase+1];
									samp2 = b_samples[iphase+3];
									outchanR[j] += gain * (samp1 + frac * (samp2-samp1));
								}
							} else { /*negative increment case (currently unused but might be useful)*/
								if(iphase == 0.0 || increment == -1.0 ){
									outchanL[j] += b_samples[iphase] * gain;
									outchanR[j] += b_samples[iphase+1] * gain;
								} else {
									samp2 = b_samples[iphase];
									samp1 = b_samples[iphase-2];
									outchanL[j] += gain * (samp1 + frac * (samp2-samp1));
									samp2 = b_samples[iphase+1];
									samp1 = b_samples[iphase-1];
									outchanR[j] += gain * (samp1 + frac * (samp2-samp1));
								}
							}
							events[i].phasef += increment;
							
							if( events[i].phasef < 0.0 || events[i].phasef >= b_frames){
								events[i].status = INACTIVE;
								break;
							}
						}
					}
				}
			}
		}
		
		for(i=0; i<n; i++){
			if(trigger_vec[i]){
				gain = trigger_vec[i];
				
				insert_success = 0;
				for(j=0; j<overlap_max; j++){
					if(events[j].status == INACTIVE){
						events[j].status = ACTIVE;
						events[j].gain = gain;
						if(increment > 0){
							events[j].phasef = 0.0;
						} else {
							events[j].phasef = b_frames - 1;
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
						if(events[k].phasef > maxphase){
							maxphase = events[k].phasef;
							theft_candidate = k;
						}
					}
					new_insert = theft_candidate;
					events[new_insert].gain = gain;
					if(increment > 0){
						events[new_insert].phasef = 0.0;
					} else {
						events[new_insert].phasef = b_frames - 1;
					}
					insert_success = 1;
					post("stealing a note at %d for buffer %s", new_insert, sound_name);
				}
				events[new_insert].countdown = latency_samples;
				events[new_insert].status = ACTIVE;
				x->new_slot = new_insert;
				x->new_gain = gain;
				// post("new note at slot %d",new_insert);
				if(tcycle.len > 0){
					increment = tcycle.data[tcycle.p++];
					if(tcycle.p >= tcycle.len){
						tcycle.p = 0;
					}
					x->tcycle.p = tcycle.p;
				} else {
					increment = 1.0;
				}
				
				for(k=i; k<n; k++){
					//roll out for remaining portion of vector
					if(events[new_insert].countdown > 0){
						--events[new_insert].countdown;
					} else {
						if(b_nchans == 1){
							
							iphase = events[new_insert].phasef;
							frac = events[new_insert].phasef - iphase;
							if(iphase < 0 || iphase >= b_frames){
								error("aborting on phase %f",events[new_insert].phasef);
								break;
							}
							if(increment > 0){
								if(iphase == flimit || increment == 1.0){
									outchanL[k] += b_samples[iphase] * gain;
									outchanR[k] += b_samples[iphase] * gain;
								} else {
									samp1 = b_samples[iphase];
									samp2 = b_samples[iphase+1];
									samp1 = gain * (samp1 + frac * (samp2-samp1));
									outchanL[k] += samp1;
									outchanR[k] += samp1;
								}
							} else { /*negative increment case (currently unused but might be useful)*/
								if(iphase == 0.0 || increment == -1.0 ){
									outchanL[k] += b_samples[iphase] * gain;
									outchanR[k] += b_samples[iphase] * gain;
								} else {
									samp2 = b_samples[iphase];
									samp1 = b_samples[iphase-2];
									samp1 = gain * (samp1 + frac * (samp2-samp1));
									outchanL[k] += samp1;
									outchanR[k] += samp1;
								}
							}
							events[new_insert].phasef += increment;
							
							if( events[new_insert].phasef < 0.0 || events[new_insert].phasef >= b_frames){
								events[new_insert].status = INACTIVE;
								break;
							}
						} else if(b_nchans == 2)
						{
							iphase = events[new_insert].phasef;
							frac = events[new_insert].phasef - iphase;
							iphase *= 2;
							if(increment > 0){
								if(iphase == flimit || increment == 1.0){
									outchanL[k] += b_samples[iphase] * gain;
									outchanR[k] += b_samples[iphase+1] * gain;
								} else {
									samp1 = b_samples[iphase];
									samp2 = b_samples[iphase+2];
									outchanL[k] += gain * (samp1 + frac * (samp2-samp1));
									samp1 = b_samples[iphase+1];
									samp2 = b_samples[iphase+3];
									outchanR[k] += gain * (samp1 + frac * (samp2-samp1));
								}
							} else { /*negative increment case (currently unused but might be useful)*/
								if(iphase == 0.0 || increment == -1.0 ){
									outchanL[k] += b_samples[iphase] * gain;
									outchanR[k] += b_samples[iphase+1] * gain;
								} else {
									samp2 = b_samples[iphase];
									samp1 = b_samples[iphase-2];
									outchanL[k] += gain * (samp1 + frac * (samp2-samp1));
									samp2 = b_samples[iphase+1];
									samp1 = b_samples[iphase-1];
									outchanR[k] += gain * (samp1 + frac * (samp2-samp1));
								}
							}
							events[new_insert].phasef += increment;
							
							if( events[new_insert].phasef < 0.0 || events[new_insert].phasef >= b_frames){
								events[new_insert].status = INACTIVE;
								break;
							}
						}
					}					
				}
			}
		}
		x->increment = increment;
		/* end of block_dsp contingecy code */
	}
	
	/* main body of bashfest processing */	
	
	
	for(i=0; i<n; i++){ /* pre-clean buffers*/
		outchanL[i] = outchanR[i] = 0.0;
	}
	
	/* add output from all active buffers into global outlet buffers */
	
	for(i = 0; i < overlap_max; i++){
		if( events[i].status == ACTIVE){
			out_channels = events[i].out_channels;
			/* assign the output part of work buffer to the local float buffer */
			
			processed_drum = events[i].workbuffer + events[i].in_start;
			
			for(j = 0; j < n; j++){
				if(x->grab){
					x->grab = 0;
					// if too slow, defend with defer_low()
					bashfest_copy_to_MSP_buffer(x,i);
				}
				if(events[i].countdown > 0){
					--events[i].countdown;
				} else {
					if(out_channels == 1){
						outchanL[j] += processed_drum[events[i].phase] * events[i].gainL;
						outchanR[j] += processed_drum[events[i].phase] * events[i].gainR;
					} else if(out_channels == 2){
						iphase = events[i].phase * 2;
						outchanL[j] += processed_drum[iphase] * events[i].gainL;
						outchanR[j] += processed_drum[iphase+1] * events[i].gainR;
					}
					
					events[i].phase++;
					
					if(events[i].phase >= events[i].sample_frames){
						events[i].status = INACTIVE;
						break;
					}
				}
			}
		}
	}
	
	/* now check for initiation click. If found,
	 add to list. If necessary, steal a note 
	 */
	for(i=0; i<n; i++){
		if(trigger_vec[i]){
			gain = trigger_vec[i];
			transpose_factor =  transpose_vec[i];
			/*look for an open slot*/
			insert_success = 0;
			for(j=0; j<overlap_max; j++){
				if(events[j].status == INACTIVE){
					events[j].status = ACTIVE;
					events[j].gain = gain;
					events[j].transpose = transpose_factor;
					insert_success = 1;
					new_insert = j;
					break;
				}
			}	
			
			if(!insert_success){ /* steal a note if necessary*/
				maxphase = 0;
				theft_candidate = 0;
				for(k = 0; k < overlap_max; k++){
					if(events[k].phase > maxphase){
						maxphase = events[k].phase;
						theft_candidate = k;
					}
				}
				if(x->verbose){
				//	post("stealing note at slot %d", theft_candidate);
				}
				//post("stealing a note at %d for buffer %s", theft_candidate, sound_name);
				new_insert = theft_candidate;
				events[new_insert].gain = gain;
				insert_success = 1;
			}
			
			events[new_insert].countdown = x->latency_samples;
			x->new_slot = new_insert;
			x->new_gain = gain;
			defer_low(x,(void *)bashfest_deploy_dsp,0,0,0);
			
			/* now begin output from the new note */
			
			out_channels = events[new_insert].out_channels;
			processed_drum = events[new_insert].workbuffer + events[new_insert].in_start;
			
			/* processed_drum = events[new_insert].workbuffer;	*/		
			for(j = i; j < n; j++){ 
				if(events[new_insert].countdown > 0){
					--events[new_insert].countdown;
				} else{
					iphase = events[new_insert].phase;
					if(x->grab){
						x->grab = 0;
						// if too slow, defend with defer_low()
						bashfest_copy_to_MSP_buffer(x,i);
					}
					if(out_channels == 1){
						outchanL[j] += processed_drum[iphase] * events[new_insert].gainL;
						outchanR[j] += processed_drum[iphase] * events[new_insert].gainR;
					} else if(out_channels == 2){
						iphase = events[i].phase * 2;
						outchanL[j] += processed_drum[iphase] * events[new_insert].gainL;
						outchanR[j] += processed_drum[iphase+1] * events[new_insert].gainR;
					}
					
					events[new_insert].phase++;
					
					if(events[new_insert].phase >= events[new_insert].sample_frames){
						events[new_insert].status = INACTIVE;
						break;
					}
				}
			} 
		}
	}
    laterAlligator:
        buffer_unlocksamples(the_buffer);
}

void bashfest_copy_to_MSP_buffer(t_bashfest *x, int slot)
{
	int i; //,j;
	t_event *events = x->events;
	long b_nchans;
	long b_frames;
	float *b_samples;
	float *processed_drum;

    
    t_buffer_obj *the_buffer= NULL;
	attach_buffer(x);
    the_buffer = buffer_ref_getobject(x->buffer_ref);
    b_samples = buffer_locksamples(the_buffer);
	b_frames = buffer_getframecount(the_buffer);
	b_nchans = buffer_getchannelcount(the_buffer);
    
	processed_drum = events[slot].workbuffer + events[slot].in_start;
	
	if(events[slot].out_channels == b_nchans){
		if(b_nchans == 1){
			for(i=0;i<b_frames;i++){
				b_samples[i] = processed_drum[i];
			}
		} else if(b_nchans == 2){
			for(i=0;i<b_frames*2;i+=2){
				b_samples[i] = processed_drum[i];
				b_samples[i+1] = processed_drum[i+1];
			}
		}else{
			error("bashfest copy: channel mismatch");
			//fixable but first let's try these
		}
	}
    buffer_unlocksamples(the_buffer);
}
void bashfest_deploy_dsp(t_bashfest *x)
{
	float *b_samples;
	long b_nchans;
	long b_frames;
	t_event *events = x->events;
	float pan;
	int i; //,j;
	float *params = x->params;
	int pcount;
	int buf_samps = x->buf_samps;
	int curarg = 0;
	float maxamp;
	float rescale;
	float *inbuf;
	int slot = x->new_slot;
	float gain = x->new_gain;
	float transpose_factor = events[slot].transpose;
    t_buffer_obj *the_buffer;
	attach_buffer(x);
    the_buffer = buffer_ref_getobject(x->buffer_ref);
    b_samples = buffer_locksamples(the_buffer);
	b_frames = buffer_getframecount(the_buffer);
	b_nchans = buffer_getchannelcount(the_buffer);
    
	events[slot].completed = 1;// for testing only
	
	if(b_nchans <1 || b_nchans > 2){
		error("illegal channels in buffer:%d",b_nchans);
		goto outtahere;
		x->hosed = 1;
	}
	if(b_frames > x->buf_frames / 2){
		long sr;
		
		sr = 44100; // cheat for now
		x->buf_frames = (b_frames + (1 * sr)) * 2; // 1 second pad, 2 channels
		x->buf_samps = x->buf_frames * 2; // work buffer is twice size of MSP buffer
		x->halfbuffer = x->buf_samps / 2;
		post("sample in buffer %s is to large for work buffer, which will be resized",x->sound_name);
		for(i=0;i<x->overlap_max;i++){
			x->events[i].workbuffer = (float *) sysmem_resizeptr((void*)x->events[i].workbuffer,(x->buf_samps * sizeof(float)));
		}
		goto outtahere;// we do not deploy, just this once
	}
	
	pan = boundrand(0.1, 0.9);
	events[slot].gainL = cos(PIOVERTWO * pan) * gain;
	events[slot].gainR = sin(PIOVERTWO * pan) * gain;
	events[slot].phase = 0;
	events[slot].status = ACTIVE;
	/*  if(x->verbose)
	 post("initiating note at slot %d, gain %f, pan %f,inchans %d",slot,gain,pan,b_nchans);
	 */
	if(x->sound_lock){
		goto outtahere;// of course should finally copy good stuff to MSP buffer
	}
	events[slot].out_channels = b_nchans;
	events[slot].sample_frames = b_frames;
	for(i=0; i<b_frames*b_nchans; i++){
		events[slot].workbuffer[i] = b_samples[i];
	}
	
	// clean rest of work buffer
	for(i=b_frames*b_nchans; i<buf_samps; i++){
		events[slot].workbuffer[i] = 0.0;
	}
	events[slot].in_start = 0;
	events[slot].out_start = x->halfbuffer;
	pcount = bashfest_set_parameters(x, params, transpose_factor);
	
	while(curarg < pcount){
		if(params[curarg] == TRANSPOSE){
			transpose(x, slot, &curarg);
		}
		else if(params[curarg] == RINGMOD){
			ringmod(x, slot, &curarg);
		} 
		else if(params[curarg] == RETRO){
			retrograde(x, slot, &curarg);
		} 
		else if(params[curarg] == COMB){
			comber(x, slot, &curarg);
		}
		else if(params[curarg] == FLANGE){
			flange(x, slot, &curarg);
		}
		else if(params[curarg] == BUTTER){
			butterme(x, slot, &curarg);
		}
		else if(params[curarg] == TRUNCATE){
			truncateme(x, slot, &curarg);
		}
		else if(params[curarg] == SWEEPRESON){
			sweepreson(x, slot, &curarg);
		} 
		else if(params[curarg] == SLIDECOMB){
			slidecomb(x, slot, &curarg);
		} 
		else if(params[curarg] == REVERB1){
			reverb1(x, slot, &curarg);
		} 
		else if(params[curarg] == ELLIPSE){
			ellipseme(x, slot, &curarg);
		} 
		else if(params[curarg] == FEED1){
			feed1me(x, slot, &curarg);
		} 
		else if(params[curarg] == FLAM1){
			flam1(x, slot, &curarg);
		}     	
		else if(params[curarg] == FLAM2){
			flam2(x, slot, &curarg);
		} 
		else if(params[curarg] == EXPFLAM){
			expflam(x, slot, &curarg);
		}
		else if(params[curarg] == COMB4){
			comb4(x, slot, &curarg);
		}
		else if(params[curarg] == COMPDIST){
			compdist(x, slot, &curarg);
		}
		else if(params[curarg] == RINGFEED){
			ringfeed(x, slot, &curarg);
		}
		else if(params[curarg] == RESONADSR){
			resonadsr(x, slot, &curarg);
		}
		else if(params[curarg] == STV){
			stv(x, slot, &curarg);
		}
		else {
			error("deploy missing branch");
		}
	}
	
	maxamp = 0.0;
	inbuf = events[slot].workbuffer + events[slot].in_start;
	b_nchans = events[slot].out_channels;
	b_frames = events[slot].sample_frames;
	for(i=0; i< b_frames * b_nchans; i++){
		if(maxamp < fabs(inbuf[i])){
			maxamp = fabs(inbuf[i]);
		}
	}
	if(maxamp>0){
		rescale = 1.0/maxamp;
		for(i=0; i< b_frames * b_nchans; i++){
			inbuf[i] *= rescale;
		}
	}
	else{
		if(x->verbose)
			error("zero maxamp detected");
	}
	
	if(events[slot].countdown <= 0){
		error("%s: DSP failed to conclude for buffer %s. Increase latency",OBJECT_NAME,x->sound_name);
	}
outtahere:
    buffer_unlocksamples(the_buffer);
}

int bashfest_set_parameters(t_bashfest *x,float *params, float transpose_factor)
{
	float rval;
	int pcount = 0;
	int events;
	int i, j;
	int type;
	float cf;//, bw;
	float *odds = x->odds;
	int maxproc  = x->max_process_per_note;
	int minproc = x->min_process_per_note;
	float tval;
	t_cycle tcycle = x->tcycle;
	
	/* preliminary transposition will be set here */
	if(transpose_factor){ // overrides tcycle
		params[pcount++] = TRANSPOSE;
		params[pcount++] = transpose_factor;
	}
	else if(tcycle.len > 0){
		params[pcount++] = TRANSPOSE;
		params[pcount++] = tcycle.data[tcycle.p++];
		if(tcycle.p >= tcycle.len){
			tcycle.p = 0;
		}
		x->tcycle.p = tcycle.p;
	}
	
	
	if(maxproc <= 0){
		return pcount;
	}
	
	events = minproc + rand() % (1+(maxproc-minproc));
	
	for(i = 0; i < events; i++){
		rval = boundrand(0.0,1.0);
		j = 0;
		while(rval > odds[j]){
			j++;
		}
		
		
		if(j == RETRO){
			params[pcount++] = RETRO;
		} 
		else if(j == COMB){
			params[pcount++] = COMB;
			params[pcount++] = boundrand(.001,.035);// delaytime
			params[pcount++] = boundrand(.25,.98);//feedback
			params[pcount++] = boundrand(.05,.5);//hangtime
		} 
		else if(j == RINGMOD) {
			params[pcount++] = RINGMOD;
			params[pcount++] = boundrand(100.0,2000.0); //need a log version
		} 
		else if(j == TRANSPOSE){
			params[pcount++] = TRANSPOSE;
			params[pcount++] = boundrand(0.25,3.0);
		} 
		else if(j == FLANGE){
			params[pcount++] = FLANGE;
			params[pcount++] = boundrand(100.0,400.0);
			params[pcount++] = boundrand(600.0,4000.0);
			params[pcount++] = boundrand(0.1,2.0);
			params[pcount++] = boundrand(0.1,0.95);
			params[pcount++] = boundrand(0.0,0.9);
		} 	
		else if(j == BUTTER){
			params[pcount++] = BUTTER;
			type = rand() % 3;
			params[pcount++] = type;
			cf = boundrand(70.0,3000.0);
			params[pcount++] = cf;
			if(type == BANDPASS){
				params[pcount++] = cf * boundrand(0.05,0.6);
			}		
		}	
		else if(j == TRUNCATE){
			params[pcount++] = TRUNCATE;
			params[pcount++] = boundrand(.05,.15);
			params[pcount++] = boundrand(.01,.05);
		}
		else if(j == SWEEPRESON){
			params[pcount++] = SWEEPRESON;
			params[pcount++] = boundrand(100.0,300.0);
			params[pcount++] = boundrand(600.0,6000.0);
			params[pcount++] = boundrand(0.01,0.2);
			params[pcount++] = boundrand(0.05,2.0);
			params[pcount++] = boundrand(0.0,1.0);
		}
		else if(j == SLIDECOMB){
			params[pcount++] = SLIDECOMB;
			params[pcount++] = boundrand(.001,.03);
			params[pcount++] = boundrand(.001,.03);
			params[pcount++] = boundrand(0.05,0.95);
			params[pcount++] = boundrand(0.05,0.5);
		}
		else if(j == REVERB1){
			params[pcount++] = REVERB1;
			params[pcount++] = boundrand(0.25,0.99);
			params[pcount++] = boundrand(0.1,1.0);
			params[pcount++] = boundrand(0.2,0.8);
		}
		else if(j == ELLIPSE){
			params[pcount++] = ELLIPSE;
			params[pcount++] = rand() % ELLIPSE_FILTER_COUNT;
		}
		else if(j == FEED1){
			params[pcount++] = FEED1;
			tval = boundrand(.001,0.1);
			params[pcount++] = tval;
			params[pcount++] = boundrand(tval,0.1);
			tval = boundrand(.01,0.5);
			params[pcount++] = tval;
			params[pcount++] = boundrand(tval,0.5);
			params[pcount++] = boundrand(.05,1.0);
		}
		else if(j == FLAM1){
			params[pcount++] = FLAM1;
			params[pcount++] = 4 + (rand() % 20);
			params[pcount++] = boundrand(0.3,0.8);
			params[pcount++] = boundrand(0.5,1.2);
			params[pcount++] = boundrand(.025,0.15);
		}
		else if(j == FLAM2){
			params[pcount++] = FLAM2;
			params[pcount++] = 4 + (rand() % 20);
			params[pcount++] = boundrand(0.1,0.9);
			params[pcount++] = boundrand(0.2,1.2);
			params[pcount++] = boundrand(.025,0.15);
			params[pcount++] = boundrand(.025,0.15);
		}
		else if(j == EXPFLAM){
			params[pcount++] = EXPFLAM;
			params[pcount++] = 4 + (rand() % 20);
			params[pcount++] = boundrand(0.1,0.9);
			params[pcount++] = boundrand(0.2,1.2);
			params[pcount++] = boundrand(.025,0.15);
			params[pcount++] = boundrand(.025,0.15);
			params[pcount++] = boundrand(-5.0,5.0);
		}
		else if(j == COMB4){
			params[pcount++] = COMB4;
			params[pcount++] = boundrand(100.0,900.0);
			params[pcount++] = boundrand(100.0,900.0);
			params[pcount++] = boundrand(100.0,900.0);
			params[pcount++] = boundrand(100.0,900.0);
			tval = boundrand(.5,0.99);
			params[pcount++] = tval;
			params[pcount++] = tval;
		}
		else if(j == COMPDIST){
			params[pcount++] = COMPDIST;
			params[pcount++] = tval = boundrand(.01,.25);
			params[pcount++] = boundrand(tval,.9);
			params[pcount++] = 1;
		}
		else if(j == RINGFEED){
			params[pcount++] = RINGFEED;
			params[pcount++] = boundrand(90.0,1500.0);
			params[pcount++] = boundrand(90.0,1500.0);
			params[pcount++] = boundrand(0.2,0.95);
			params[pcount++] = boundrand(90.0,1500.0);
			params[pcount++] = boundrand(.01,.4);
			params[pcount++] = boundrand(.05,1.0);
		}
		else if(j == RESONADSR){
			params[pcount++] = RESONADSR;
			params[pcount++] = boundrand(.01,.1);
			params[pcount++] = boundrand(.01,.05);
			params[pcount++] = boundrand(.05,.5);
			params[pcount++] = boundrand(150.0,4000.0);
			params[pcount++] = boundrand(150.0,4000.0);
			params[pcount++] = boundrand(150.0,4000.0);
			params[pcount++] = boundrand(150.0,4000.0);
			params[pcount++] = boundrand(.03,.7);
		}
		else if(j == STV){
			params[pcount++] = STV;
			params[pcount++] = boundrand(.025,0.5);
			params[pcount++] = boundrand(.025,0.5);
			params[pcount++] = boundrand(.001,.01);
		}
		else {
			error("could not find a process for %d",j);
			return 0;
		}
	}
	return pcount;
}


void bashfest_dsp64(t_bashfest *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    if(!samplerate)
        return;
	attach_buffer(x);
	

	/* if vector size changes, we also need to deal, thanks to
	 the trigger buffer inter-delay
	 */
	if(x->sr != samplerate){
		x->sr = samplerate;
		if(!x->sr){
			post("%s: zero sampling rate!",OBJECT_NAME);
			x->sr = 44100;
            return;
		}
	} 

    object_method(dsp64, gensym("dsp_add64"),x,bashfest_perform64,0,NULL);
	
}

void bashfest_assist (t_bashfest *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Click Trigger"); break;
			case 1: sprintf(dst,"(signal) Click Increment"); break;
		}
	} 
	else if (msg==2) {
		switch(arg){
			case 0: sprintf(dst,"(signal) Channel 1 Output"); break;
			case 1: sprintf(dst,"(signal) Channel 2 Output"); break;
		}
	}
}
