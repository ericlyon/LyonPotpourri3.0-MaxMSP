#include "MSPd.h"
#define MAX_CHANS (8192)
#define CS_LINEAR (0)
#define CS_POWER (1)

static t_class *clean_gate_class;

typedef struct _clean_gate
{
    t_pxobject x_obj;
    // Variables Here
    short output_chans;
    short active_chan;
    short last_chan;
    long gate;
    long current_gate;
    int samps_to_fade;
    int fadesamps;
    t_double fadetime;
    t_double pi_over_two;
    short fadetype;
 //   short *connected_list;
    //	t_double **bulk ; // array to point to all input audio channels
    t_double sr;
    t_double vs;
    int outlet_count;
    short linear; // flag to use linear fade
} t_clean_gate;w
#define OBJECT_NAME "clean_gate~"

void *clean_gate_new(t_symbol *s, int argc, t_atom *argv);

t_int *clean_gate_perform(t_int *w);
void clean_gate_dsp(t_clean_gate *x, t_signal **sp, short *count);
void clean_gate_assist(t_clean_gate *x, void *b, long m, long a, char *s);
void clean_gate_float(t_clean_gate *x, t_float f);
void clean_gate_fadetime(t_clean_gate *x, t_floatarg f);
void clean_gate_int(t_clean_gate *x, t_int i);
void clean_gate_channel(t_clean_gate *x, t_floatarg i);
void clean_gate_linear(t_clean_gate *x, t_floatarg tog);
void clean_gate_dsp_free(t_clean_gate *x);
void clean_gate_perform64(t_clean_gate *x, t_object *dsp64, double **ins,
                          long numins, double **outs,long numouts, long n,
                          long flags, void *userparam);
void clean_gate_dsp64(t_clean_gate *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.clean_gate~", (method)clean_gate_new, (method)dsp_free, sizeof(t_clean_gate), 0,A_GIMME, 0);
    class_addmethod(c,(method)clean_gate_assist,"assist",A_CANT,0);
    class_addmethod(c,(method)clean_gate_fadetime,"fadetime",A_FLOAT,0);
    class_addmethod(c,(method)clean_gate_channel,"channel",A_FLOAT,0);
    class_addmethod(c,(method)clean_gate_linear,"linear",A_FLOAT,0);
    class_addmethod(c,(method)clean_gate_float,"float",A_FLOAT,0);
    class_addmethod(c,(method)clean_gate_int,"int",A_LONG,0);
    class_addmethod(c, (method)clean_gate_dsp64, "dsp64", A_CANT,0);
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    clean_gate_class = c;
    potpourri_announce(OBJECT_NAME);
    return 0;
}


void clean_gate_assist (t_clean_gate *x, void *b, long msg, long arg, char *dst)
{
    if (msg==1) {
        if(arg == 0){
            sprintf(dst,"(signal/int) Gate");
        }
        else if(arg == 1){
            sprintf(dst,"(signal) Input");
        }
    } else if (msg==2) {
        sprintf(dst,"(signal) Output %ld", arg);
    }
}

void *clean_gate_new(t_symbol *s, int argc, t_atom *argv)
{
    int i;
    t_clean_gate *x;
    x = (t_clean_gate *)object_alloc(clean_gate_class);
    
    x->fadetime = 0.05;
    x->gate = 0;
    // x->outlet_count = 8;
    x->linear = 0; // by default use power cross fade
    if(argc >= 1){
        x->outlet_count = (int)atom_getfloatarg(0,argc,argv);
        if(x->outlet_count < 2 || x->outlet_count > MAX_CHANS){
            error("%s: %d is illegal number of inlets",OBJECT_NAME,x->outlet_count);
            return (void *) NULL;
        }
    }
    if(argc >= 2){
        x->fadetime = atom_getfloatarg(1,argc,argv) / 1000.0;
    }
    
    //		post("argc %d inlet count %d fadetime %f",argc, x->outlet_count, x->fadetime);
    dsp_setup((t_pxobject *)x, 2);
    for(i = 0; i < x->outlet_count; i++){
        outlet_new((t_pxobject *)x, "signal");
    }
    x->x_obj.z_misc |= Z_NO_INPLACE;
    x->sr = sys_getsr();
    if(!x->sr){
        x->sr = 44100.0;
        error("zero sampling rate - set to 44100");
    }
    x->fadetype = CS_POWER;
    x->pi_over_two = 1.57079632679;
    
    if(x->fadetime <= 0.0)
        x->fadetime = .05;
    x->fadesamps = x->fadetime * x->sr;
    
    // MAYBE DON'T NEED THIS:
    
    // x->connected_list = (short *) sysmem_newptrclear(MAX_CHANS * sizeof(short));
    /*
    for(i=0;i<16;i++){
        x->connected_list[i] = 0;
    }
    */
    x->active_chan = x->last_chan = 0;
    x->samps_to_fade = 0;
    return x;
}

void clean_gate_dsp_free(t_clean_gate *x)
{
    dsp_free((t_pxobject *)x);
}


void clean_gate_fadetime(t_clean_gate *x, t_floatarg f)
{
    t_double fades = (t_double)f / 1000.0;
    
    if( fades < .0001 || fades > 1000.0 ){
        error("fade time is constrained to 0.1 - 1000000, but you wanted %f",f );
        return;
    }
    x->fadetime = fades;
    x->fadesamps = x->sr * x->fadetime;
    x->samps_to_fade = 0;
}

void clean_gate_linear(t_clean_gate *x, t_floatarg flag)
{
    x->linear = (short) flag;
}

void clean_gate_perform64(t_clean_gate *x, t_object *dsp64, double **ins,
                          long numins, double **outs,long numouts, long n,
                          long flags, void *userparam)
{
    int fadesamps = x->fadesamps;
    short active_chan = x->active_chan;
    short last_chan = x->last_chan;
    int samps_to_fade = x->samps_to_fade;
    t_double m1, m2;
    int i, j;
    t_double pi_over_two = x->pi_over_two;
    t_double phase;
    
    /********************************************/
    for(i = 0; i < numouts; i++) {
        for(j = 0; j < n; j++) {
            outs[i][j] = 0.0;
        }
    }

    if ( (active_chan >= 0) && (active_chan < numouts) && (x->gate) )
    {
        for(i=0; i < n; i++) {
            if ( samps_to_fade >= 0 ) {
                phase = pi_over_two * (1.0 - ( (float) samps_to_fade / (float) fadesamps) ) ;
                m1 = sin( phase );
                m2 = cos( phase );
                outs[active_chan][i] = ins[1][i] * m1;
                outs[last_chan][i] = ins[1][i] * m2;
                --samps_to_fade;
            }
            else {
                outs[active_chan][i] = ins[1][i];
            }
        }
    }
    x->samps_to_fade = samps_to_fade;
    
}

void clean_gate_dsp64(t_clean_gate *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    // zero sample rate, go die
    if(!samplerate)
        return;
    if(x->sr != samplerate){
        x->sr = samplerate;
        x->fadesamps = x->fadetime * x->sr;
        x->samps_to_fade = 0;
    }
    object_method(dsp64, gensym("dsp_add64"),x,clean_gate_perform64,0,NULL);
}


void clean_gate_float(t_clean_gate *x, t_float f) // Look at floats at inlets
{
    clean_gate_int(x,(t_int)f);
}

void clean_gate_int(t_clean_gate *x, t_int i) // Look at int at inlets
{
    int inlet = ((t_pxobject*)x)->z_in;
    if (inlet == 0)
    {
        clean_gate_channel(x,(t_floatarg)i);
    }
    
}

void clean_gate_channel(t_clean_gate *x, t_floatarg i) // Look at int at inlets
{
    int chan = i;
    if(chan < -1 || chan > x->outlet_count - 1){
        post("%s: channel %d out of range",OBJECT_NAME, chan);
        return;
    }
    if( chan == -1){
        x->gate = 0;
        x->samps_to_fade = x->fadesamps;
    } else if(chan != x->active_chan) {
        x->gate = 1;
        x->last_chan = x->active_chan;
        x->active_chan = chan;
        x->samps_to_fade = x->fadesamps;
        if( x->active_chan < 0)
            x->active_chan = 0;
        if( x->active_chan > MAX_CHANS - 1) {
            x->active_chan = MAX_CHANS - 1;
        }
    }
}
