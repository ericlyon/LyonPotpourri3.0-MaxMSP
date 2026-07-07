
#include "bashfest.h"
/// #import "MSPd.h"
// update July 2026 - many vibe-debugged fixes

#define DEFAULT_MAX_OVERLAP (8) // number of overlapping instances allowed
#define ACTIVE 0
#define INACTIVE 1
#define WAITING 2
#define MAX_VEC 4096
#define DEFAULT_BUFFER_SIZE 4000.0 // 4 second default buffer size * 2
#define DEFAULT_LATENCY 8192 //latency in samples after a trigger for note to start
#define MAX_PARAMETERS 2048
#define PROCESS_COUNT 20
#define CYCLE_MAX 1024
// add safety samples to each allocated block of memory
#define BUF_PAD (8192)
#define OBJECT_NAME "bashfest~"


static t_class *bashfest_class;


void *bashfest_new(t_symbol *msg, short argc, t_atom *argv);
t_int *bashfest_perform_hosed(t_int *w);
void bashfest_dsp(t_bashfest *x, t_signal **sp, short *count);
void bashfest_assist (t_bashfest *x, void *b, long msg, long arg, char *dst);
void bashfest_dsp_free(t_bashfest *x);
int bashfest_set_parameters(t_bashfest *x,float *params, float transpose_factor);
t_int *bashfest_perform(t_int *w);
void bashfest_deploy_dsp(t_bashfest *x);
// void bashfest_copy_to_MSP_buffer(t_bashfest *x, int slot);
void bashfest_copy_to_MSP_buffer(t_bashfest *x);
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
void bashfest_gozero(t_bashfest *x);
void bashfest_grab(t_bashfest *x);
void bashfest_printmem(t_bashfest *x);
void bashfest_setodds(t_bashfest *x,t_symbol *msg, short argc, t_atom *argv);
void bashfest_tcycle(t_bashfest *x,t_symbol *msg, short argc, t_atom *argv);
void bashfest_version(t_bashfest *x);
void bashfest_dsp64(t_bashfest *x, t_object *dsp64, short *count, 
                    double samplerate, long maxvectorsize, long flags);
t_max_err bashfest_notify(t_bashfest *x, t_symbol *s, t_symbol *msg, void *sender, void *data);

/* function code */

void killdc( float *inbuf, int in_frames, int channels, LSTRUCT *eel, t_bashfest *x );
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
    class_addmethod(c, (method)bashfest_dsp64, "dsp64", A_CANT,0);
    class_addmethod(c,(method)bashfest_notify,"notify", A_CANT, 0);
    class_addmethod(c,(method)bashfest_printmem,"printmem", 0);
	
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
/*
void bashfest_block_dsp(t_bashfest *x, t_floatarg t)
{
	
	x->block_dsp = (short)t;
	
}
*/
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

void bashfest_printmem(t_bashfest *x)
{
    post("Memory (MB) for this el.bashfest~ unit: %.2f", x->memcnt);
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
    
	int i,j;
	long membytes = 0;
	float tmpfloat;
	srand(time(0));
	
	x->sr = sys_getsr();
	x->vs = sys_getblksize();
    if(!x->sr){
        x->sr = 48000;
    }
	
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
	x->grab = 0;
	x->buf_frames = x->work_buffer_size * .001 * x->sr;
	x->buf_samps = x->buf_frames * 2 * 2; // two channels, double the size of the maximum workspace
	x->halfbuffer = x->buf_samps / 2;
	x->maxdelay = 1.0; // in seconds
	// memory allocation
	x->events = (t_event *) sysmem_newptrclear(x->overlap_max * sizeof(t_event));
	x->sinewave = (float *) sysmem_newptrclear((x->sinelen + BUF_PAD) * sizeof(float));
	x->params = (float *) sysmem_newptrclear(MAX_PARAMETERS * sizeof(float));
	x->odds = (float *) sysmem_newptrclear(64 * sizeof(float));
	
    for(i=0;i<64;i++){
        x->odds[i] = 0;
    }
	putsine(x->sinewave, x->sinelen);
	for(i=0; i < x->overlap_max; i++){
		x->events[i].workbuffer = (float *) sysmem_newptrclear((x->buf_samps + BUF_PAD) * sizeof(float));
	}
    for(i=0; i < x->overlap_max; i++){
        x->events[i].delayline1 = (float *) sysmem_newptrclear(((x->maxdelay * x->sr) + 2 + BUF_PAD) * sizeof(float));
        x->events[i].delayline2 = (float *) sysmem_newptrclear(((x->maxdelay * x->sr) + 2 + BUF_PAD) * sizeof(float));
    }
	x->max_mini_delay = .25;
    for(i=0; i < x->overlap_max; i++){
        x->events[i].eel = (LSTRUCT *) sysmem_newptrclear(MAXSECTS * sizeof(LSTRUCT));
    }
    for(i = 0; i < x->overlap_max; i++){
        x->events[i].mini_delay = (float **) sysmem_newptrclear(4 * sizeof(float *));
        for(j = 0; j < 4; j++){
            x->events[i].mini_delay[j] = (float *) sysmem_newptrclear(((int)(x->sr * x->max_mini_delay) + BUF_PAD)  * sizeof(float));
        }
    }
	x->reverb_ellipse_data = (float *) sysmem_newptrclear(16 * sizeof(float));
	x->ellipse_data = (float **) sysmem_newptrclear(MAXFILTER * sizeof(float *));
	for(i=0;i<MAXFILTER;i++){
		x->ellipse_data[i] = (float *) sysmem_newptrclear(MAX_COEF * sizeof(float));
	}
	x->tf_len = 1;
	x->tf_len <<= 16;
    for(i = 0; i < x->overlap_max; i++){
        x->events[i].transfer_function = (float *) sysmem_newptrclear(x->tf_len * sizeof(float));
    }
	x->feedfunclen = 8192;
    for(i=0; i < x->overlap_max; i++){
        x->events[i].feedfunc1 = (float *) sysmem_newptrclear((x->feedfunclen + BUF_PAD) * sizeof(float));
        x->events[i].feedfunc2 = (float *) sysmem_newptrclear((x->feedfunclen + BUF_PAD) * sizeof(float));
        x->events[i].feedfunc3 = (float *) sysmem_newptrclear((x->feedfunclen + BUF_PAD) * sizeof(float));
        x->events[i].feedfunc4 = (float *) sysmem_newptrclear((x->feedfunclen + BUF_PAD) * sizeof(float));
    }
	x->flamfunc1len = 8192;
	x->flamfunc1 = (float *) sysmem_newptrclear((x->flamfunc1len) * sizeof(float));
	setflamfunc1(x->flamfunc1,x->flamfunc1len);
	x->max_comb_lpt = 0.15 ;// watch out here
    for(i = 0; i < x->overlap_max; i++){
        x->events[i].combies = (CMIXCOMB **) sysmem_newptrclear(4 * sizeof(CMIXCOMB *));
        for(j = 0; j < 4; j++ ){
            x->events[i].combies[j] = (CMIXCOMB *) sysmem_newptrclear(4 * sizeof(CMIXCOMB));
            x->events[i].combies[j]->len = x->sr * x->max_comb_lpt + 2;
            x->events[i].combies[j]->arr = (float *) sysmem_newptrclear(x->events[i].combies[j]->len * sizeof(float));
        }
    }
    for(i = 0; i < x->overlap_max; i++){
        x->events[i].resies = (CMIXRESON **)sysmem_newptrclear(4 * sizeof(CMIXRESON *));
        for(j = 0; j < 4; j++ ){
            x->events[i].resies[j] = (CMIXRESON *)sysmem_newptrclear(4 * sizeof(CMIXRESON));
        }
    }
    for(i = 0; i < x->overlap_max; i++){
        x->events[i].adsr = (CMIXADSR *)sysmem_newptrclear(sizeof(CMIXADSR));
        x->events[i].adsr->len = 32768;
        x->events[i].adsr->func = (float *)sysmem_newptrclear(x->events[i].adsr->len * sizeof(float));
    }
	x->dcflt = (float *) sysmem_newptrclear(16 * sizeof(float));
	x->tcycle.data = (float *) sysmem_newptrclear(CYCLE_MAX * sizeof(float));
	x->tcycle.len = 0;
	for(i=0; i<x->overlap_max; i++){
		x->events[i].phasef = x->events[i].phase = 0.0;
	}
    x->qelem = qelem_new(x,(method)bashfest_deploy_dsp);
    x->qelem_grab = qelem_new(x,(method)bashfest_copy_to_MSP_buffer);
	membytes = x->overlap_max * sizeof(t_event);
	membytes += x->sinelen * sizeof(float);
	membytes += MAX_PARAMETERS * sizeof(float);
	membytes += 64 * sizeof(float);
	membytes += x->buf_samps * sizeof(float) * x->overlap_max;
	membytes += ((x->maxdelay * x->sr * 2) + BUF_PAD) * x->overlap_max * sizeof(float);
	membytes += MAXSECTS * sizeof(LSTRUCT) * x->overlap_max;
	membytes += ((int)(x->sr * x->max_mini_delay) + BUF_PAD) * 4 * x->overlap_max * sizeof(float);
	membytes += 16 * sizeof(float);
	membytes += MAXFILTER * sizeof(float *);
	membytes += MAX_COEF * sizeof(float) * MAXFILTER;
	membytes += x->tf_len * sizeof(float);
	membytes += x->feedfunclen * sizeof(float) * 4 * x->overlap_max;
	membytes += x->flamfunc1len * sizeof(float);
	membytes += 4 * sizeof(CMIXCOMB) * x->overlap_max;
	membytes += x->events[0].combies[0]->len * x->overlap_max * sizeof(float) * 4;
	membytes += sizeof(CMIXADSR);
	membytes += x->events[0].adsr->len * sizeof(float);
	membytes += 16 * sizeof(float);
	membytes += CYCLE_MAX * sizeof(float);
    membytes += x->tf_len * sizeof(float) * x->overlap_max;
    x->memcnt = (float)membytes/1000000.0;
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
    int i, j;
    if (!x) return;
    x->hosed = 1;
    
    if (x->qelem) {
        qelem_free(x->qelem);
    }
    if (x->qelem_grab) {
        qelem_free(x->qelem_grab);
    }
    dsp_free((t_pxobject *)x);

    // 1. FREE ALL CHILDREN FIRST
    if (x->events) {
        
        for (i = 0; i < x->overlap_max; i++) {
            if (x->events[i].workbuffer) sysmem_freeptr(x->events[i].workbuffer);
            if (x->events[i].eel) sysmem_freeptr(x->events[i].eel);
            if (x->events[i].delayline1) sysmem_freeptr(x->events[i].delayline1);
            if (x->events[i].delayline2) sysmem_freeptr(x->events[i].delayline2);
            if (x->events[i].transfer_function) sysmem_freeptr(x->events[i].transfer_function);
            if (x->events[i].feedfunc1) sysmem_freeptr(x->events[i].feedfunc1);
            if (x->events[i].feedfunc2) sysmem_freeptr(x->events[i].feedfunc2);
            if (x->events[i].feedfunc3) sysmem_freeptr(x->events[i].feedfunc3);
            if (x->events[i].feedfunc4) sysmem_freeptr(x->events[i].feedfunc4);
            
            if (x->events[i].mini_delay) {
                for (j = 0; j < 4; j++) {
                    if (x->events[i].mini_delay[j]) sysmem_freeptr(x->events[i].mini_delay[j]);
                }
                sysmem_freeptr(x->events[i].mini_delay);
            }
            
            if (x->events[i].combies) {
                for (j = 0; j < 4; j++) {
                    if (x->events[i].combies[j]) {
                        if (x->events[i].combies[j]->arr) sysmem_freeptr(x->events[i].combies[j]->arr);
                        sysmem_freeptr(x->events[i].combies[j]);
                    }
                }
                sysmem_freeptr(x->events[i].combies);
            }
            
            if (x->events[i].resies) {
                for (j = 0; j < 4; j++) {
                    if (x->events[i].resies[j]) sysmem_freeptr(x->events[i].resies[j]);
                }
                sysmem_freeptr(x->events[i].resies);
            }
            
            if (x->events[i].adsr) {
                if (x->events[i].adsr->func) sysmem_freeptr(x->events[i].adsr->func);
                sysmem_freeptr(x->events[i].adsr);
            }
        }
        // 2. NOW FREE THE PARENT
        sysmem_freeptr(x->events);
    }

    // 3. Free global read-only tables
    if (x->sinewave) sysmem_freeptr(x->sinewave);
    if (x->params) sysmem_freeptr(x->params);
    if (x->odds) sysmem_freeptr(x->odds);
    if (x->flamfunc1) sysmem_freeptr(x->flamfunc1);
    if (x->reverb_ellipse_data) sysmem_freeptr(x->reverb_ellipse_data);
    if (x->dcflt) sysmem_freeptr(x->dcflt);
    if (x->tcycle.data) sysmem_freeptr(x->tcycle.data);
    
    if (x->ellipse_data) {
        for (i = 0; i < MAXFILTER; i++) sysmem_freeptr(x->ellipse_data[i]);
        sysmem_freeptr(x->ellipse_data);
    }
    
    if (x->buffer_ref) object_free(x->buffer_ref);

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
	long i,j,k, slot_i;
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
    short deferred_argc = 2; // number of arguments
    t_atom deferred_argv[2]; // atom arguments
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


	
	/* main body of bashfest processing */	
	
	
	for(i=0; i<n; i++){ /* pre-clean buffers*/
		outchanL[i] = outchanR[i] = 0.0;
	}
	
	/* add output from all active buffers into global outlet buffers */
	
	for(slot_i = 0; slot_i < overlap_max; slot_i++){
		if( events[slot_i].status == ACTIVE){
			out_channels = events[slot_i].out_channels;
			/* assign the output part of work buffer to the local float buffer */
			
			processed_drum = events[slot_i].workbuffer + events[slot_i].in_start;
			
			for(j = 0; j < n; j++){
                /*
				if(x->grab){
					x->grab = 0;
                    x->grab_slot = slot_i;
                    qelem_set(x->qelem_grab); // this is where we copy from new_slot to MSP buffer
					// this needs to go into a qelem()
					// bashfest_copy_to_MSP_buffer(x,slot_i);
				}
                */
                if(x->grab){
                    x->grab = 0;
                    x->grab_slot = slot_i;
                    x->grab_start = events[slot_i].in_start;
                    qelem_set(x->qelem_grab); // this is where we copy from new_slot to MSP buffer
                }
				if(events[slot_i].countdown > 0){
					--events[slot_i].countdown;
				} else {
					if(out_channels == 1){
						outchanL[j] += processed_drum[events[slot_i].phase] * events[slot_i].gainL;
						outchanR[j] += processed_drum[events[slot_i].phase] * events[slot_i].gainR;
					} else if(out_channels == 2){
						iphase = events[slot_i].phase * 2;
						outchanL[j] += processed_drum[iphase] * events[slot_i].gainL;
						outchanR[j] += processed_drum[iphase+1] * events[slot_i].gainR;
					}
					
					events[slot_i].phase++;
					
					if(events[slot_i].phase >= events[slot_i].sample_frames){
						events[slot_i].status = INACTIVE;
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
			for(slot_i = 0; slot_i < overlap_max; slot_i++){
				if(events[slot_i].status == INACTIVE){
					events[slot_i].status = WAITING;// will activate slot in deferred function
					events[slot_i].gain = gain;
					events[slot_i].transpose = transpose_factor;
					insert_success = 1;
					new_insert = slot_i;
					break;
				}
			}
			if(!insert_success){ /* steal a note if necessary*/
				maxphase = 0;
				theft_candidate = 0;
				for(slot_i = 0; slot_i < overlap_max; slot_i++){
					if(events[slot_i].phase > maxphase){
						maxphase = events[slot_i].phase;
						theft_candidate = slot_i;
					}
				}
				if(x->verbose){
				//	post("stealing note at slot %d", theft_candidate);
				}
				//post("stealing a note at %d for buffer %s", theft_candidate, sound_name);
				new_insert = theft_candidate;
                events[new_insert].status = WAITING;
				events[new_insert].gain = gain;
                events[new_insert].transpose = transpose_factor;
				insert_success = 1;
			}
			
			events[new_insert].countdown = x->latency_samples;
			x->new_slot = new_insert;
			x->new_gain = gain;
            qelem_set(x->qelem); // this is where we deploy the new DSP chain
            /*
            if(x->grab){
                x->grab = 0;
                qelem_set(x->qelem_grab); // this is where we copy current processed sound to MSP buffer
            }
*/
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
                        x->grab_slot = new_insert;
                        x->grab_start = events[new_insert].in_start;
                        qelem_set(x->qelem_grab); // this is where we copy from new_slot to MSP buffer
					}
                    
                    // possibly zero out buffers until we move from WAITING to ACTIVE
					if(out_channels == 1){
						outchanL[j] += processed_drum[iphase] * events[new_insert].gainL;
						outchanR[j] += processed_drum[iphase] * events[new_insert].gainR;
					} else if(out_channels == 2){
                        iphase = events[new_insert].phase * 2;
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
        if(the_buffer){
            buffer_unlocksamples(the_buffer);
        }
}

void bashfest_copy_to_MSP_buffer(t_bashfest *x)
{
	int i; //,j;
	t_event *events = x->events;
    int grab_slot = x->grab_slot;
    int grab_start = x->grab_start;
	long b_nchans;
	long b_frames;
	float *b_samples;
	float *processed_drum;

    //  post("grabbing buffer at slot %d and start %d\n", grab_slot, grab_start);
    t_buffer_obj *the_buffer= NULL;
	attach_buffer(x);
    the_buffer = buffer_ref_getobject(x->buffer_ref);
    b_samples = buffer_locksamples(the_buffer);
	b_frames = buffer_getframecount(the_buffer);
	b_nchans = buffer_getchannelcount(the_buffer);
    
	// processed_drum = events[slot].workbuffer + events[slot].in_start;
    processed_drum = events[grab_slot].workbuffer + grab_start;

	if(events[grab_slot].out_channels == b_nchans){
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
    long b_nchans, b_frames;
    t_event *events = x->events;
    float *params = x->params;
    t_buffer_obj *the_buffer;
    int i, slot;

    // 1. Bail if the object is being freed
    if (x->hosed) {
        return;
    }

    // 2. Attach and Lock the MSP Source Buffer
    attach_buffer(x);
    the_buffer = buffer_ref_getobject(x->buffer_ref);
    if (!the_buffer) {
        return;
    }

    b_samples = buffer_locksamples(the_buffer);
    b_frames = buffer_getframecount(the_buffer);
    b_nchans = buffer_getchannelcount(the_buffer);
    
    
    // Initial check for buffer validity
    if (!b_samples || b_nchans < 1 || b_nchans > 2) {
        if (the_buffer) buffer_unlocksamples(the_buffer);
        return;
    }

    // 3. Scan all slots for notes marked as WAITING by the audio thread
    for (slot = 0; slot < x->overlap_max; slot++) {
        if (events[slot].status == WAITING) {
            
            // Note: gain and transpose_factor were stored in the slot by perform64
            float gain = events[slot].gain;
            float transpose_factor = events[slot].transpose;
            
            // Randomize spatial position
            float pan = boundrand(0.1, 0.9);
            events[slot].gainL = cos(PIOVERTWO * pan) * gain;
            events[slot].gainR = sin(PIOVERTWO * pan) * gain;
            
            events[slot].phase = 0;
            events[slot].out_channels = b_nchans;
            // events[slot].sample_frames = b_frames;

            // 4. Initial Copy: Transfer audio from MSP buffer to the slot's private workbuffer
            int copy_count = b_frames * b_nchans;
            if (copy_count > x->halfbuffer) {
                copy_count = x->halfbuffer;
            }
            events[slot].sample_frames = copy_count / b_nchans;

            
            // Copy audio data
            for (i = 0; i < copy_count; i++) {
                events[slot].workbuffer[i] = b_samples[i];
            }
            
            // SAFETY: Zero out the rest of the workbuffer to prevent "ghost" audio
            for (i = copy_count; i < (x->buf_samps); i++) {
                events[slot].workbuffer[i] = 0.0f;
            }

            // Set initial Ping-Pong state
            events[slot].in_start = 0;
            events[slot].out_start = x->halfbuffer;

            // 5. Generate random effect parameters for this note
            int pcount = bashfest_set_parameters(x, params, transpose_factor);
            int curarg = 0;

            // 6. Execute the DSP Chain
            // Each routine reads from in_start and writes to out_start (ping-pong)
            while (curarg < pcount) {
                int type = (int)params[curarg];
                
                if      (type == TRANSPOSE)  transpose(x, slot, &curarg);
                else if (type == RINGMOD)    ringmod(x, slot, &curarg);
                else if (type == RETRO)      retrograde(x, slot, &curarg);
                else if (type == COMB)       comber(x, slot, &curarg);
                else if (type == FLANGE)     flange(x, slot, &curarg);
                else if (type == BUTTER)     butterme(x, slot, &curarg);
                else if (type == TRUNCATE)   truncateme(x, slot, &curarg);
                else if (type == SWEEPRESON) sweepreson(x, slot, &curarg);
                else if (type == SLIDECOMB)  slidecomb(x, slot, &curarg);
                else if (type == REVERB1)    reverb1(x, slot, &curarg);
                else if (type == ELLIPSE)    ellipseme(x, slot, &curarg);
                else if (type == FEED1)      feed1me(x, slot, &curarg);
                else if (type == FLAM1)      flam1(x, slot, &curarg);
                else if (type == FLAM2)      flam2(x, slot, &curarg);
                else if (type == EXPFLAM)    expflam(x, slot, &curarg);
                else if (type == COMB4)      comb4(x, slot, &curarg);
                else if (type == COMPDIST)   compdist(x, slot, &curarg);
                else if (type == RINGFEED)   ringfeed(x, slot, &curarg);
                else if (type == RESONADSR)  resonadsr(x, slot, &curarg);
                else if (type == STV)        stv(x, slot, &curarg);
                else {
                    // Safety: if an unknown parameter is found, skip it to avoid infinite loop
                    curarg++;
                }
            }

            // 7. Normalization
            // Process the final buffer to ensure peak amplitude is 1.0
            float maxamp = 0.0f;
            float *processed_ptr = events[slot].workbuffer + events[slot].in_start;
            int total_samples = events[slot].sample_frames * events[slot].out_channels;
            
            // Ensure we don't scan past the half-buffer wall
            if (total_samples > x->halfbuffer) {
                total_samples = x->halfbuffer;
            }

            for (i = 0; i < total_samples; i++) {
                float abs_val = fabs(processed_ptr[i]);
                if (abs_val > maxamp) maxamp = abs_val;
            }
            
            if (maxamp > 0.00001f) {
                float rescale = 1.0f / maxamp;
                for (i = 0; i < total_samples; i++) {
                    processed_ptr[i] *= rescale;
                }
            }

            // 8. Final Hand-off
            // The note is fully rendered and normalized. Mark it ACTIVE for the audio thread.
            events[slot].status = ACTIVE;
        }
    }
    x->grab_slot = slot;
    x->grab_start = events[slot].out_start;
    // 9. Clean up
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
        if (pcount >= MAX_PARAMETERS - 32) {
            error("bashfest~: too many parameters, truncation applied");
            return pcount;
        }
	}
    // post("Pcount: %d\n", pcount);
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
