
#include "MSPd.h"

void *bvplay_class;

#define OBJECT_NAME "bvplay~"

typedef struct _bvplay
{
    t_pxobject l_obj;
    t_symbol *l_sym;
    t_buffer_ref *bufref;
    long l_chan;
   // float taper_dur;
    int R;
    int framesize;
    float *notedata;
    int active;
    float buffer_duration;
    int taper_frames;
    float amp;
    int start_frame;
    int note_frames;
    int end_frame;
    float increment;
    float findex;
    int index ;
	short verbose;
	short mute;
    float a_taper_dur; // attribute for taper duration
} t_bvplay;

t_int *bvplay_perform_mono(t_int *w);
t_int *bvplay_perform_stereo(t_int *w);
void bvplay_dsp(t_bvplay *x, t_signal **sp);
void bvplay_set(t_bvplay *x, t_symbol *s);
void *bvplay_new(t_symbol *s, short argc, t_atom *argv);
void bvplay_notelist(t_bvplay *x, t_symbol *msg, short argc, t_atom *argv );
void bvplay_assist(t_bvplay *x, void *b, long m, long a, char *s);
void bvplay_dblclick(t_bvplay *x);
void bvplay_perform_stereo64(t_bvplay *x, t_object *dsp64, double **ins, 
                             long numins, double **outs,long numouts, long vectorsize,
                             long flags, void *userparam);
void bvplay_dsp64(t_bvplay *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
t_max_err bvplay_taper_get(t_bvplay *x, void *attr, long *ac, t_atom **av);
t_max_err bvplay_taper_set(t_bvplay *x, void *attr, long ac, t_atom *av);

t_symbol *ps_buffer;


int C74_EXPORT main(void)
{
    t_class *c;
	c = class_new("el.bvplay~", (method)bvplay_new, (method)dsp_free, sizeof(t_bvplay), 0,A_GIMME, 0);
	

    class_addmethod(c, (method)bvplay_dsp64, "dsp64", A_CANT,0);
	class_addmethod(c, (method)bvplay_notelist, "list", A_GIMME, 0);
    class_addmethod(c, (method)bvplay_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)bvplay_dblclick, "dblclick", A_CANT, 0);

    CLASS_ATTR_FLOAT(c, "taper", 0, t_bvplay, a_taper_dur);
	CLASS_ATTR_LABEL(c, "taper", 0, "Taper");
	
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	bvplay_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}





void bvplay_notelist(t_bvplay *x, t_symbol *msg, short argc, t_atom *argv) 
{
	t_buffer_obj *b;

	if( ! sys_getdspstate() ){
	    if( x->verbose )
			error("cannot receive notes when the DSP is off!");
		return;
	}
	if( x->active ){
		if( x->verbose )
			error("object still playing - cannot add note!");
		return;
	}

	bvplay_set(x,x->l_sym);
    b = buffer_ref_getobject(x->bufref);
    

	if( x->framesize != buffer_getframecount(b) ) {
	    if( x->verbose ) {
			post("framesize reset");
		}
		x->framesize = buffer_getframecount(b) ;
		x->buffer_duration = (float)  buffer_getframecount(b)  / (float) x->R ;
		if( x->verbose ) {
			post("there are %d frames in this buffer. Duration = %.2f, Channels: %d ",
				buffer_getframecount(b) , x->buffer_duration, buffer_getchannelcount(b));
		}
	}	
	else if( x->buffer_duration <= 0.0 ) {
		x->framesize = buffer_getframecount(b)  ;
		x->buffer_duration = (float)  buffer_getframecount(b)  / (float) x->R ;
		if( x->verbose ){
			post("there are %d frames in this buffer. Duration = %.2f ",
			buffer_getframecount(b) , x->buffer_duration);
		}
	}
	if( x->buffer_duration <= 0.0 ) {
		post("buffer contains no data. Please fill it and try again");
		return;
	}
	
	// read note data
	if( argc != 4 ){
		if( x->verbose ){
			post("improper note data");
			post("notelist parameters: skiptime, duration, increment, amplitude");
		}
	}

x->notedata[0] = atom_getfloatarg(0,argc,argv) / 1000.0;
x->notedata[1] = atom_getfloatarg(1,argc,argv) / 1000.0;
x->notedata[2] = atom_getfloatarg(2,argc,argv);
x->notedata[3] = atom_getfloatarg(3,argc,argv);

	x->start_frame = x->notedata[0] * x->R;
	x->increment = x->notedata[2];
	x->index = x->findex = x->start_frame;

	if( x->increment == 0.0 ){
		if( x->verbose )
			post("zero increment!");
		return;
	}
	x->note_frames =  x->notedata[1] * x->increment  * x->R;
	x->end_frame = x->start_frame + x->note_frames;

    x->amp = x->notedata[3];
	if( x->start_frame < 0 || x->start_frame >= x->framesize){
		if( x->verbose )
			post("bad start time");
		return;
	}
	if( x->end_frame < 0 || x->end_frame >= x->framesize){
		if( x->verbose )
			post("bad end time");
		return;
	}

	x->active = 1;	
}






void bvplay_perform_mono64(t_bvplay *x, t_object *dsp64, double **ins, 
                           long numins, double **outs,long numouts, long vectorsize,
                           long flags, void *userparam)
{
    t_double *out = outs[0];
    t_int n = vectorsize;
	t_buffer_obj *b;
	float *tab; //  = b->b_samples;
	long index = x->index;
	t_double findex = x->findex;
	int end_frame = x->end_frame;
	t_double increment = x->increment;
	int start_frame = x->start_frame;
	int taper_frames = x->a_taper_dur * x->R * 0.001;
	t_double noteamp = x->amp;
	t_double frac, amp;
    /**********************/
    
	bvplay_set(x,x->l_sym);
    b = buffer_ref_getobject(x->bufref);
    tab = buffer_locksamples(b);

        if(!x->active || !tab){
            while(n--){
                *out++ = 0;
            }
        }
        else {
            while(n--){			
                if((increment > 0 && index < end_frame) || (increment < 0 && index > end_frame)) {
                    // envelope
                    if( increment > 0 ){
                        if( findex < start_frame + taper_frames ){
                            amp = noteamp * ((findex - (float) start_frame) / (float) taper_frames );
                        } else if ( findex > end_frame - taper_frames) {
                            amp = noteamp * (((float)end_frame - findex) / (float) taper_frames);
                        } else {
                            amp = noteamp;
                        }
                    } else { // negative increment case
                        if( findex > start_frame - taper_frames ){
                            amp =  noteamp * ( (start_frame - findex) / taper_frames );
                        } else if ( findex < end_frame + taper_frames) {
                            amp = noteamp * (( findex - end_frame ) / taper_frames) ;
                        } else {
                            amp = noteamp;
                        }
                    }
                    frac = findex - index ;
                    *out++ = amp * (tab[index] + frac * (tab[index + 1] - tab[index]));
                    findex += increment;
                    index = findex ;
                } 
                else {
                    *out++ = 0;
                    x->active = 0;
                }
            }
        }
    
    
	x->index = index;
	x->findex = findex;
    buffer_unlocksamples(b);
    
}

void bvplay_perform_stereo64(t_bvplay *x, t_object *dsp64, double **ins,
                           long numins, double **outs,long numouts, long vectorsize,
                           long flags, void *userparam)
{
    t_double *out1 = outs[0];
    t_double *out2 = outs[1];
    t_int n = vectorsize;
	t_buffer_obj *b = NULL;
	float *tab; //  = b->b_samples;
	long index = x->index;
	t_double findex = x->findex;
	int end_frame = x->end_frame;
	t_double increment = x->increment;
	int start_frame = x->start_frame;
	int taper_frames = x->a_taper_dur * x->R * 0.001; // grab direct from attribute
	t_double noteamp = x->amp;
	t_double frac, amp;
	long index2,index2a;
    /**********************/
	if(x->active){
        bvplay_set(x,x->l_sym);
        b = buffer_ref_getobject(x->bufref);
        tab = buffer_locksamples(b);

        
        if(!x->active || !tab){
            while(n--){
                *out1++ = *out2++ = 0;
            }
        }
    
        else {
            while(n--){			
                if((increment > 0 && index < end_frame) || (increment < 0 && index > end_frame)) {
                    // envelope
                    if( increment > 0 ){
                        if( findex < start_frame + taper_frames ){
                            amp = noteamp * ((findex - (float) start_frame) / (float) taper_frames );
                        } else if ( findex > end_frame - taper_frames) {
                            amp = noteamp * (((float)end_frame - findex) / (float) taper_frames);
                        } else {
                            amp = noteamp;
                        }
                    } else { // negative increment case
                        if( findex > start_frame - taper_frames ){
                            amp =  noteamp * ( (start_frame - findex) / taper_frames );
                        } else if ( findex < end_frame + taper_frames) {
                            amp = noteamp * (( findex - end_frame ) / taper_frames) ;
                        } else {
                            amp = noteamp;
                        }
                    }
                    frac = findex - index ;
                    index2 = index * 2 ;
                    index2a = index2 + 1;
                    *out1++ = amp * (tab[index2] + frac * (tab[index2 + 2] - tab[index2]));
                    *out2++ = amp * (tab[index2a] + frac * (tab[index2a + 2] - tab[index2a]));
                    
                    findex += increment;
                    index = findex ;
				}
                else {
                    *out1++ = *out2++ = 0;
                    x->active = 0;
                }
            } 
        }
    }
    buffer_unlocksamples(b);
	x->index = index;
	x->findex = findex;
}



void bvplay_set(t_bvplay *x, t_symbol *s)
{
	if (!x->bufref)
		x->bufref = buffer_ref_new((t_object*)x, s);
	else
		buffer_ref_set(x->bufref, s);
}



void bvplay_dblclick(t_bvplay *x)
{
    bvplay_set(x,x->l_sym);
    buffer_view(buffer_ref_getobject(x->bufref));
}

void bvplay_assist(t_bvplay *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(list) Note Data [st,dur,incr,amp]"); break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Output");
	}

}

void *bvplay_new(t_symbol *s, short argc, t_atom *argv)
{
    int chan;
	t_bvplay *x = object_alloc(bvplay_class);
	float erand();
    x->l_sym = atom_getsymarg(0,argc, argv);
    chan = atom_getfloatarg(1,argc, argv);
    x->a_taper_dur = atom_getfloatarg(2, argc, argv);
    
	dsp_setup((t_pxobject *)x,0);
	if( chan < 1 )
		chan = 1 ;
	if( chan == 1 ) {
		outlet_new((t_object *)x, "signal");
	} else if( chan == 2 ){
		outlet_new((t_object *)x, "signal");
		outlet_new((t_object *)x, "signal");
	} else {
		post("%s: ungood channels %d", OBJECT_NAME, chan);
		return NIL;
	}
    // minimum taper 1 millisecond
	if(x->a_taper_dur <= 1)
		x->a_taper_dur = 1;
	x->l_chan = chan ;

	
	// x->l_sym = s;
	x->R = sys_getsr();
	if(! x->R){
		error("zero sampling rate - set to 44100");
		x->R = 44100;
	}
	x->notedata = (float *) calloc(4, sizeof(float));
	// x->taper_dur = taperdur;
	x->taper_frames = x->R * x->a_taper_dur * 0.001;
	x->buffer_duration = 0.0 ;
	x->framesize = 0;
	x->active = 0;
	x->verbose = 0;
	x->mute = 0;
  //  x->a_taper_dur = 50.0;
    attr_args_process(x, argc, argv);
	return (x);
}




void bvplay_dsp64(t_bvplay *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    // zero sample rate, die
    if(!samplerate)
        return;
    
    bvplay_set(x,x->l_sym);
    
    if(x->R != samplerate){
    	x->R = samplerate;
    	x->taper_frames = x->R * x->a_taper_dur;
    }
    if( x->l_chan == 1 ) {
    	// post("initiating mono processor");
   	 	// dsp_add(bvplay_perform_mono, 3, x, sp[0]->s_vec, sp[0]->s_n);
        object_method(dsp64, gensym("dsp_add64"),x,bvplay_perform_mono64,0,NULL);
    } else if( x->l_chan == 2) {
    	// post("initiating stereo processor");
   	 	// dsp_add(bvplay_perform_stereo,4,x,sp[0]->s_vec,sp[1]->s_vec,sp[0]->s_n);
        object_method(dsp64, gensym("dsp_add64"),x,bvplay_perform_stereo64,0,NULL);
        
    } else {
     	post("el.bvplay: bad channel info: %d, cannot initiate dsp code", x->l_chan);
    }
    
}
