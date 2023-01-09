#include "MSPd.h"

#define OBJECT_NAME "kbuffer~"

static t_class *kbuffer_class;

typedef struct _kbuffer
{
  t_pxobject x_obj;

	float ksrate;
	float srate;
	t_double si;
	t_double phase;
	t_double duration;
	int iphase;
	int lastphase;
	int length;
	t_double *data;
	t_double fval;
	t_double lastval;
	short record_flag;
	short play_flag;
	short dump_flag;
	short loop_flag;
	t_double sync ;
	t_double speed ;
	short in_connected;
	int memsize;
} t_kbuffer;

t_int *kbuffer_perform(t_int *w);

void kbuffer_dsp(t_kbuffer *x, t_signal **sp, short *count);
void *kbuffer_new(t_symbol *s, int argc, t_atom *argv);
void kbuffer_assist(t_kbuffer *x, void *b, long m, long a, char *s);
void kbuffer_dsp_free(t_kbuffer *x);
void kbuffer_record(t_kbuffer *x);
void kbuffer_play(t_kbuffer *x);
void kbuffer_loop(t_kbuffer *x);
void kbuffer_info(t_kbuffer *x);
void kbuffer_dump(t_kbuffer *x);
void kbuffer_stop(t_kbuffer *x);
void kbuffer_info(t_kbuffer *x);
void kbuffer_speed(t_kbuffer *x, t_floatarg speed);
void kbuffer_size(t_kbuffer *x, t_floatarg ms);
void kbuffer_ksrate(t_kbuffer *x, t_floatarg ksrate);
void kbuffer_float(t_kbuffer *x, double f);
void kbuffer_int(t_kbuffer *x, int i);
void kbuffer_init(t_kbuffer *x,short initialized);
void kbuffer_version(t_kbuffer *x);

void kbuffer_dsp64(t_kbuffer *x, t_object *dsp64, short *count, double samplerate, long n, long flags);
void kbuffer_perform64(t_kbuffer *x, t_object *dsp64, double **ins, 
                       long numins, double **outs,long numouts, long n,
                       long flags, void *userparam);


int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.kbuffer~", (method)kbuffer_new, (method)dsp_free, sizeof(t_kbuffer), 0,A_GIMME, 0);
    class_addmethod(c,(method)kbuffer_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c,(method)kbuffer_assist,"assist",A_CANT,0);
	class_addmethod(c,(method)kbuffer_record, "record", 0);
	class_addmethod(c,(method)kbuffer_play, "play", 0);
	class_addmethod(c,(method)kbuffer_loop, "loop", 0);
	class_addmethod(c,(method)kbuffer_stop, "stop", 0);
	class_addmethod(c,(method)kbuffer_dump, "dump", 0);
	class_addmethod(c,(method)kbuffer_info, "info", 0);
	class_addmethod(c,(method)kbuffer_speed, "speed", A_FLOAT, 0);
	class_addmethod(c,(method)kbuffer_size, "size", A_FLOAT, 0);
	class_addmethod(c,(method)kbuffer_ksrate, "ksrate", A_FLOAT, 0);
	class_register(CLASS_BOX, c);
	kbuffer_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void kbuffer_speed(t_kbuffer *x, t_floatarg speed) {
	x->speed = speed;
	// post("speed set to %f",speed);	
}

void kbuffer_size(t_kbuffer *x, t_floatarg ms) {
	int i;
	if(ms < 1)
		ms = 1;
	x->duration = ms / 1000.0 ;
	x->memsize = x->ksrate * x->duration * sizeof(t_double);
    x->length = x->duration * x->ksrate ;
	x->data = (t_double*) realloc(x->data,x->memsize*sizeof(t_double));
    for( i = 0; i < x->length; i++){
    	x->data[i] = 0.0;
    }
}

void kbuffer_ksrate(t_kbuffer *x, t_floatarg ksrate) {
	int i;
	if( ksrate < 1 )
		ksrate = 1 ;
	x->ksrate = ksrate ;
	x->memsize = x->ksrate * x->duration * sizeof(t_double);
    x->length = x->duration * x->ksrate ;
    x->si = x->ksrate / x->srate;
	x->data = (t_double*) realloc(x->data,x->memsize*sizeof(t_double));
    for( i = 0; i < x->length; i++){
    	x->data[i] = 0.0;
    }
}

void kbuffer_info(t_kbuffer *x) {
	post("function length is %d samples",x->length);
	post("function sampling rate is %.2f",x->ksrate);
	post("function byte size is %d",x->memsize);
	post("function duration is %.2f seconds",x->duration);
}

void kbuffer_record(t_kbuffer *x) {
	x->record_flag = 1;
	x->play_flag = 0;
	x->dump_flag = 0;
	x->loop_flag = 0;
	x->sync = 0.0;
	x->phase = x->iphase = 0 ;
	x->lastphase = -1 ;
	// post("starting to record");
}
void kbuffer_stop(t_kbuffer *x) {
	x->record_flag = 0;
	x->play_flag = 0;
	x->dump_flag = 0;
	x->loop_flag = 0;
	x->sync = 0.0;
	x->phase = x->iphase = 0 ;
	x->lastphase = -1 ;
}
void kbuffer_dump(t_kbuffer *x) {
	x->record_flag = 0;
	x->play_flag = 0;
	x->loop_flag = 0;
	x->dump_flag = 1;
	x->sync = 0.0;
	x->phase = x->iphase = 0 ;
	x->lastphase = -1 ;
}

void kbuffer_play(t_kbuffer *x) {
	x->record_flag = 0;
	x->play_flag = 1;
	x->dump_flag = 0;
	x->loop_flag = 0;
	x->sync = 0.0;
	x->phase = x->iphase = 0 ;
	x->lastphase = -1 ;
}

void kbuffer_loop(t_kbuffer *x) {
	x->record_flag = 0;
	x->play_flag = 0;
	x->dump_flag = 0;
	x->loop_flag = 1;
	x->sync = 0.0;
	x->phase = x->iphase = 0 ;
	x->lastphase = -1 ;
}

void kbuffer_dsp_free(t_kbuffer *x) {
	dsp_free((t_pxobject *) x);
	free(x->data);
}

void kbuffer_perform64(t_kbuffer *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
    t_double *in = ins[0];
    t_double *out = outs[0];
    t_double *sync_out = outs[1];
    short record_flag = x->record_flag;
    short play_flag = x->play_flag ;
    short dump_flag = x->dump_flag ;
    short loop_flag = x->loop_flag ;
    int length = x->length;
    int iphase = x->iphase;
    int lastphase = x->lastphase;
    t_double phase = x->phase;
    t_double *data = x->data;
    t_double si = x->si;
    t_double speed = x->speed;
    t_double sample;
    short in_connected = x->in_connected;
    t_double fval = x->fval;
    /*********************/
    
    while( n-- ){
        if( in_connected ){
            sample = *in++ ;
        } else {
            sample = fval;
        }
        if( record_flag ){
            iphase = phase;
            /*		phase += (si * speed); Bug!! */
            phase += si;
            if( iphase >= length ){
                record_flag = 0;
                // post("end of recording at %d samples",length);
            }
            else if( iphase > lastphase ){
                lastphase = iphase ;
                data[ iphase ] = sample ;
            }
            *sync_out++ = phase / (float) length ;
            *out++ = sample ; // mirror input to output
        } else if ( play_flag ){
            iphase = phase;
            phase += (si * speed);
            if( iphase >= length ){
                play_flag = 0;
                *out++ = data[ length - 1 ]; // lock at final value
            } else if (iphase < 0 ) {
                play_flag = 0;
                *out++ = data[ 0 ]; // lock at first value
            }
            else {
                *out++ = data[ iphase ] ;
            }
            *sync_out++ = phase / (float) length ;		
        } 
        else if ( loop_flag ){
            iphase = phase;
            phase += (si * speed);
            if( iphase >= length ){
                phase = iphase = 0;
            } else if (iphase < 0 ) {
                phase = iphase = length - 1;
            }
            *out++ = data[ iphase ] ;
            *sync_out++ = phase / (float) length ;		
            
        }
        else if ( dump_flag ) {
            iphase = phase ;
            phase += 1.0 ;
            if( iphase >= length ){
                dump_flag = 0;
            } else {
                *out++ = data[ iphase ];
            }
            
        }
        
        else {
            *sync_out++ = 0.0 ;
            *out++ = 0.0;
            
        }
        x->phase = phase;
        x->lastphase = lastphase;
        x->record_flag = record_flag;
        x->play_flag = play_flag;
        
    }
}

void *kbuffer_new(t_symbol *s, int argc, t_atom *argv)
{
	t_kbuffer *x = (t_kbuffer *)object_alloc(kbuffer_class);
	dsp_setup((t_pxobject *)x,1);
	outlet_new((t_object *)x, "signal");
	outlet_new((t_object *)x, "signal");

	x->srate = sys_getsr();
	if( x->srate == 0 ){
		error("zero sampling rate - set to 44100");
		x->srate = 44100;
	}
	x->ksrate = atom_getfloatarg(0,argc,argv);
    x->duration = atom_getfloatarg(1,argc,argv)/1000.0;
	if(x->ksrate <= 0)
		x->ksrate = 128;
	if(x->duration <= 0)
		x->duration = 10.;
	
	kbuffer_init(x,0);   
 	return (x);
}

void kbuffer_init(t_kbuffer *x,short initialized)
{
    if(!initialized){
        x->record_flag = 0;
        x->play_flag = 0;
        x->dump_flag = 0;
        x->loop_flag = 0;
        x->fval = 0;
        x->speed = 1.0 ;
        x->memsize = x->ksrate * x->duration * sizeof(t_double);
        x->length = x->duration * x->ksrate;
        x->data = (t_double *) calloc(x->memsize, sizeof(t_double));
    }
    x->si = x->ksrate / x->srate;
}

void kbuffer_dsp64(t_kbuffer *x, t_object *dsp64, short *count, double samplerate, long n, long flags)
{
    if(!samplerate)
        return;
	if(x->srate != samplerate){
        x->srate = samplerate;
        kbuffer_init(x,1);
	}
    object_method(dsp64, gensym("dsp_add64"),x,kbuffer_perform64,0,NULL);
}

void kbuffer_assist(t_kbuffer *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0:
				sprintf(dst,"(signal/float) Input ");
				break;
		}
	} else if (msg==2) {
		switch (arg) {
			case 0:
				sprintf(dst,"(signal) Output ");
				break;
			case 1:
				sprintf(dst,"(signal) Sync ");
				break;

		}

	}
}
void kbuffer_float(t_kbuffer *x, double f) // Look at floats at inlets
{
	int inlet = ((t_pxobject*)x)->z_in;
	
	if (inlet == 0)
	{
		x->fval = f;
		//post("fval set to %f", x->fval);
	}
}

void kbuffer_int(t_kbuffer *x, int i) // Look at floats at inlets
{
	int inlet = ((t_pxobject*)x)->z_in;
	
	if (inlet == 0)
	{
		x->fval = i;
		//post("ival set to %f", x->fval);
	}
}
