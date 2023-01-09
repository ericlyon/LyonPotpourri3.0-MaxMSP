#include "MSPd.h"

static t_class *mask_class;

#define MAXLEN 512
#define MAXMASKS 1024
#define MAXSEQ 1024
#define OBJECT_NAME "mask~"

typedef struct 
	{
		t_double *pat; // mask pattern
		int length;// length of pattern
	} t_maskpat;

typedef struct 
	{
		int *seq; // mask pattern
		int length;// length of pattern
		int phase; // keep track of where we are in sequence
	} t_sequence;

typedef struct _mask
	{

		t_pxobject x_obj;

		short mute;// stops all computation (try z-disable)
		short gate; // continues masking but inhibits all output
		short phaselock; // indicates all patterns are the same size and use the same phase count
		long indexmode;//special mode where input clicks are also mask indicies (+ 1)
		int phase;//phase of current pattern
		int current_mask;// currently selected pattern
		t_maskpat *masks;// contains the mask patterns
		t_sequence sequence;// contains an optional mask sequence	
		int *stored_masks;// a list of patterns stored
		int pattern_count;//how many patterns are stored
		short noloop;// flag to play pattern only once
		t_double *in_vec;//copy space for input to avoid dreaded vector sharing override
	} t_mask;

void *mask_new(t_symbol *msg, short argc, t_atom *argv);
t_int *mask_perform(t_int *w);
void mask_assist(t_mask *x, void *b, long m, long a, char *s);
void mask_mute(t_mask *x, t_floatarg f);
void mask_phaselock(t_mask *x, t_floatarg f);
void mask_gate(t_mask *x, t_floatarg f);
void mask_addmask(t_mask *x, t_symbol *msg, short argc, t_atom *argv);
void mask_recall(t_mask *x, t_floatarg p);
void mask_showmask(t_mask *x, t_floatarg p);
void mask_gozero(t_mask *x);
void mask_free(t_mask *x);
void mask_sequence(t_mask *x, t_symbol *msg, short argc, t_atom *argv);
void mask_noloop(t_mask *x, t_floatarg f);
void mask_playonce(t_mask *x, t_floatarg pnum);
void mask_dsp64(t_mask *x, t_object *dsp64, short *count, double sr, long n, long flags);
void mask_perform64(t_mask *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.mask~", (method)mask_new, (method)mask_free, sizeof(t_mask), 0,A_GIMME, 0);

	class_addmethod(c,(method)mask_mute,"mute",A_FLOAT,0);
	class_addmethod(c,(method)mask_phaselock,"phaselock",A_FLOAT,0);
	class_addmethod(c,(method)mask_gate,"gate",A_FLOAT,0);
	class_addmethod(c,(method)mask_addmask,"addmask",A_GIMME,0);
	class_addmethod(c,(method)mask_sequence,"sequence",A_GIMME,0);
	class_addmethod(c,(method)mask_recall,"recall",A_FLOAT,0);
	class_addmethod(c,(method)mask_showmask,"showmask",A_FLOAT,0);
	class_addmethod(c,(method)mask_noloop,"noloop",A_FLOAT,0);
	class_addmethod(c,(method)mask_playonce,"playonce",A_FLOAT,0);
	class_addmethod(c,(method)mask_gozero,"gozero",0);
	class_addmethod(c,(method)mask_assist,"assist",A_CANT,0);
    class_addmethod(c,(method)mask_dsp64,"dsp64",A_CANT,0);
	CLASS_ATTR_LONG(c, "indexmode", 0, t_mask, indexmode);
	CLASS_ATTR_DEFAULT_SAVE(c, "indexmode", 0, "0");
	CLASS_ATTR_ENUMINDEX(c,"indexmode", 0, "Off On");	
	CLASS_ATTR_LABEL(c, "indexmode", 0, "Index Mode");
	
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	mask_class = c;
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void mask_playonce(t_mask *x, t_floatarg pnum)
{
	x->noloop = 1;
	x->mute = 0;
	mask_recall(x,pnum);
}

void mask_gozero(t_mask *x)
{
	x->phase = 0;
}

void mask_mute(t_mask *x, t_floatarg f)
{
	x->mute = (short)f;
}

void mask_noloop(t_mask *x, t_floatarg f)
{
	x->noloop = (short)f;
}

void mask_phaselock(t_mask *x, t_floatarg f)
{
	x->phaselock = (short)f;
}

void mask_gate(t_mask *x, t_floatarg f)
{
	x->gate = (short)f;
}


void mask_showmask(t_mask *x, t_floatarg p){
	int location = p;
	short found = 0;
	int i;
	int len;
	
	
	for(i = 0; i<x->pattern_count; i++){
		if(location == x->stored_masks[i]){
			found = 1;
			break;
		}
	}
	if(found){
		len = x->masks[location].length;
		post("pattern length is %d",len);
		for(i = 0; i < len; i++){
			post("%d: %f",i,x->masks[location].pat[i]);
		}
		
	} else {
		error("no pattern stored at location %d",location);
	}
}

void mask_recall(t_mask *x, t_floatarg p)
{
	int i;
	int location = p;
	short found = 0;
	
	
	for(i = 0; i < x->pattern_count; i++){
		if(location == x->stored_masks[i]){
			found = 1;
			break;
		}
	}
	if(found){
		x->current_mask = location;
		if(! x->phaselock){
			x->phase = 0;
		}
	} else {
		error("no pattern stored at location %d",location);
	}
}

//initiate mask recall sequence
void mask_sequence(t_mask *x, t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	
	if(argc > MAXSEQ){
		error("%d exceeds possible length for a sequence",argc);
		return;
	}
	if(argc < 1){
		error("you must sequence at least 1 mask");
		return;
	}
	for(i = 0; i < argc; i++){
		x->sequence.seq[i] = atom_getfloatarg(i,argc,argv);
	}
	if(x->sequence.seq[0] < 0){
		post("sequencing turned off");
		x->sequence.length = 0;
		return;
	}
	x->sequence.phase = 0;
	x->sequence.length = argc;
	// now load in first mask of sequence
	mask_recall(x, (t_floatarg)x->sequence.seq[x->sequence.phase++]);
	
	// ideally would check that each sequence number is a valid stored location
}

void mask_addmask(t_mask *x, t_symbol *msg, short argc, t_atom *argv)
{
	int location;
	int i;
	
	if(argc < 2){
		error("must specify location and mask");
		return;
	}
	if(argc > MAXLEN){
		error("mask is limited to length %d",MAXLEN);
		return;
	}
	location = atom_getintarg(0,argc,argv);
	if(location < 0 || location > MAXMASKS - 1){
		error("illegal location");
		return;
	}
	if(x->masks[location].pat == NULL){
		x->masks[location].pat = (t_double *) sysmem_newptrclear(MAXLEN * sizeof(t_double));
		x->stored_masks[x->pattern_count++] = location;
	} else {
		//    post("replacing pattern stored at location %d", location);
	}
	//  post("reading new mask from argument list, with %d members",argc-1);
	x->masks[location].length = argc-1;
	for(i=1; i<argc; i++){
		x->masks[location].pat[i-1] = atom_getfloatarg(i,argc,argv);
	}
	//  post("there are currently %d patterns stored",x->pattern_count);
}

void mask_free(t_mask *x)
{
	int i;
	dsp_free((t_pxobject *) x);
    for(i=0;i<x->pattern_count;i++)
        sysmem_freeptr(x->masks[i].pat);
    sysmem_freeptr(x->masks);
    sysmem_freeptr(x->stored_masks);
    sysmem_freeptr(x->sequence.seq);
    sysmem_freeptr(x->in_vec);
}

void *mask_new(t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	

	int argoffset = attr_args_offset(argc,argv);
	t_mask *x = (t_mask *)object_alloc(mask_class);
	dsp_setup((t_pxobject *)x,1);
	outlet_new((t_pxobject *)x, "signal");

	
	x->masks = (t_maskpat *) sysmem_newptrclear(MAXMASKS * sizeof(t_maskpat));
	x->stored_masks = (int *) sysmem_newptrclear(MAXMASKS * sizeof(int));
	
	x->sequence.seq = (int *) sysmem_newptrclear(MAXSEQ * sizeof(int));
	
	
	/* this should be vector size, and possibly realloced in dsp routine if size changes */
	x->in_vec = (t_double *) sysmem_newptrclear(8192 * sizeof(t_double));
	
	x->sequence.length = 0; // no sequence by default
	x->sequence.phase = 0; // 
	
	//	post("allocated %d bytes for basic mask holder",MAXMASKS * sizeof(t_maskpat));
	
	x->current_mask = -1; // by default no mask is selected
	for(i=0; i<MAXMASKS; i++){
		x->stored_masks[i] = -1; // indicates no pattern stored
		x->masks[i].pat = NULL;
	}
	if(argoffset > 0){
		//	post("reading initial mask from argument list, with %d members",argc);
		x->masks[0].pat = (t_double *) sysmem_newptrclear(MAXLEN * sizeof(t_double));
		//		post("allocated %d bytes for this pattern", MAXLEN * sizeof(float));
		x->masks[0].length = argoffset;
		for(i=0; i< argoffset; i++){
			x->masks[0].pat[i] = atom_getfloatarg(i,argc,argv);
		}
		x->current_mask = 0; // now we use the mask we read from the arguments
		x->stored_masks[0] = 0;
		x->pattern_count = 1;
	}
	x->indexmode = 0;
	x->mute = 0;
	x->gate = 1;//by default gate is on, and the pattern goes out (zero gate turns it off)
	x->phaselock = 0;// by default do NOT use a common phase for all patterns
	x->phase = 0;
	x->noloop = 0;
	//post("indexmode status before processing: %d", x->indexmode);
	attr_args_process(x, argc, argv);
	//post("indexmode status after processing: %d", x->indexmode);
	
	return (x);
}

void mask_perform64(t_mask *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
	int i;
	t_double *inlet = ins[0];
	t_double *outlet = outs[0];
	
	int phase = x->phase;
	short gate = x->gate;
	long indexmode = x->indexmode;
	short noloop = x->noloop;
	int current_mask = x->current_mask;
	t_maskpat *masks = x->masks;
	t_sequence sequence = x->sequence;
	t_double *in_vec = x->in_vec;
	
	if( x->mute || current_mask < 0){
		while(n--) *outlet++ = 0;
		return;	
	}
	
	// should use memcpy() here
	for(i = 0; i < n; i++){
		in_vec[i] = inlet[i];
	}
	// clean outlet - should use memset()
	for( i = 0; i < n; i++){
		outlet[i] = 0.0;
	}
	
	for(i = 0; i<n; i++){
		if(in_vec[i]){ // got a click
			if(indexmode){ // indexmode means the click itself controls the phase of the mask
				phase = in_vec[i] - 1;
				/*    	post("current mask: %d, length: %d, inphase %d", current_mask, masks[current_mask].length, phase); */
				if(phase < 0 || phase >= masks[current_mask].length){
					/*	post("phase %d out of range", phase); */
					phase %= masks[current_mask].length;
					/*	post("phase reset to %d", phase); */
				}
			}
			if(gate){
				outlet[i] = masks[current_mask].pat[phase];
				//				post("mask value: %f",outlet[i]);
			}
			++phase; //advance phase in all cases (so pattern advances when gated)
			if(phase >= masks[current_mask].length){
				phase = 0;
				if(noloop){
					x->mute = 1;
					//				post("halted by noloop");
					goto out;
				}
				// if a sequence is active, reset the current mask too
				if(sequence.length){
					mask_recall(x, (t_floatarg)sequence.seq[sequence.phase++]);
					current_mask = x->current_mask; // this was reset internally!
					if(sequence.phase >= sequence.length)
						sequence.phase = 0;
				}
			}
		} 
	}
out:
	x->phase = phase;
	x->sequence.phase = sequence.phase;    
}


void mask_dsp64(t_mask *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    if(!sr)
        return;
    object_method(dsp64, gensym("dsp_add64"),x,mask_perform64,0,NULL);
}

void mask_assist (t_mask *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Trigger Impulses"); break;
		}
	} else if (msg==2) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Masked Impulses"); break;
		}
	}
}
