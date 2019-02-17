#include "MSPd.h"


/* comment */

static t_class *function_class;
#define OBJECT_NAME "function~"

typedef struct _function
{
    t_pxobject x_obj;
    t_symbol *wavename; // name of waveform buffer
    t_buffer_ref *src_buffer_ref; // MSP reference to the buffer
    t_buffer_ref *dest_buffer_ref; // MSP reference to source buffer
    
    long b_frames;
    long b_nchans;
    float *b_samples;
    short normalize;
} t_function;

void function_setbuf(t_function *x, t_symbol *wavename);
void *function_new(t_symbol *msg, short argc, t_atom *argv);
void function_dsp(t_function *x, t_signal **sp);
void function_redraw(t_function *x);
void function_clear(t_function *x);
void function_addsyn(t_function *x, t_symbol *msg, short argc, t_atom *argv);
void function_aenv(t_function *x, t_symbol *msg, short argc, t_atom *argv);
void function_adenv(t_function *x, t_symbol *msg, short argc, t_atom *argv);
void function_normalize(t_function *x, t_floatarg f);
void function_adrenv(t_function *x, t_symbol *msg, short argc, t_atom *argv);
void function_rcos(t_function *x);
void function_gaussian(t_function *x);

void buffet_update(t_function *x);
int function_openbuf(t_function *x);
t_max_err function_notify(t_function *x, t_symbol *s, t_symbol *msg, void *sender, void *data);


int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.function~", (method)function_new, (method)dsp_free, sizeof(t_function), 0,A_GIMME, 0);
	
    class_addmethod(c,(method)function_addsyn,"addsyn", A_GIMME, 0);
    class_addmethod(c,(method)function_aenv,"aenv", A_GIMME, 0);
    class_addmethod(c,(method)function_adenv,"adenv", A_GIMME, 0);
    class_addmethod(c,(method)function_adrenv,"adrenv", A_GIMME, 0);
    class_addmethod(c,(method)function_gaussian,"gaussian", A_GIMME, 0);
    class_addmethod(c,(method)function_normalize,"normalize", A_FLOAT, 0);
    class_addmethod(c,(method)function_rcos,"rcos", 0);
    class_addmethod(c, (method)function_notify, "notify", A_CANT, 0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	function_class = c;
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void *function_new(t_symbol *msg, short argc, t_atom *argv)
{
    t_function *x = (t_function *)object_alloc(function_class);
    x->wavename = atom_getsymarg(0,argc,argv);
    x->normalize = 1;
    return x;
}

t_max_err function_notify(t_function *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    return buffer_ref_notify(x->src_buffer_ref, s, msg, sender, data);
}

void function_addsyn(t_function *x, t_symbol *msg, short argc, t_atom *argv)
{
    int i,j;
    t_buffer_obj *wavebuf_b = NULL;
    long b_frames;
    float *b_samples;
    float amp;
    float maxamp, rescale;
    
    if( function_openbuf(x) ){
        wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
        b_frames= buffer_getframecount(wavebuf_b);
        b_samples = buffer_locksamples(wavebuf_b);
    } else {
        return;
    }
    
    amp = atom_getfloatarg(0,argc,argv);
    for(i=0;i<b_frames;i++){
        b_samples[i] = amp;
    }
    for(j=1;j<argc;j++){
        amp = atom_getfloatarg(j,argc,argv);
        if(amp){
            for(i=0;i<b_frames;i++){
                b_samples[i] += amp * sin(TWOPI * (float) j * (float)i/(float)b_frames);
            }
        }
    }
    if(x->normalize){
        maxamp = 0;
        for(i=0;i<b_frames;i++){
            if(maxamp < fabs(b_samples[i]))
                maxamp = fabs(b_samples[i]);
        }
        if(!maxamp){
            post("* zero maxamp!");
            return;
        }
        rescale = 1.0 / maxamp;
        for(i=0;i<b_frames;i++){
            b_samples[i] *= rescale;
        }
    }
	buffet_update(x);
    buffer_unlocksamples(wavebuf_b);
}

void function_rcos(t_function *x)
{
    int i;
    t_buffer_obj *wavebuf_b = NULL;
    long b_frames;
    float *b_samples;
    
    if( function_openbuf(x) ){
        wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
        b_frames= buffer_getframecount(wavebuf_b);
        b_samples = buffer_locksamples(wavebuf_b);
    } else {
        return;
    }
    for(i=0;i<b_frames;i++){
        b_samples[i] = 0.5 - 0.5 * cos(TWOPI * (float)i/(float)b_frames);
    }
	buffet_update(x);
    buffer_unlocksamples(wavebuf_b);
}


void function_gaussian(t_function *x)
{
    int i;
    t_buffer_obj *wavebuf_b = NULL;
    long b_frames;
    float *b_samples;
    float arg, xarg,in;
    
    if( function_openbuf(x) ){
        wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
        b_frames= buffer_getframecount(wavebuf_b);
        b_samples = buffer_locksamples(wavebuf_b);
    } else {
        return;
    }
    arg = 12.0 / (float)b_frames;
    xarg = 1.0;
    in = -6.0;
    
    for(i=0;i<b_frames;i++){
        b_samples[i] = xarg * pow(2.71828, -(in*in)/2.0);
        in += arg;
    }
	buffet_update(x);
    buffer_unlocksamples(wavebuf_b);
}
/*
void function_clear(t_function *x)
{
    int i;
    long b_frames = x->b_frames;
    float *b_samples = x->b_samples;
    
    function_setbuf(x, x->wavename);
    b_frames = x->b_frames;
    b_samples = x->b_samples;
    for(i=0;i<b_frames;i++)
        b_samples[i] = 0.0;
    function_redraw(x);
}
*/
void function_adrenv(t_function *x, t_symbol *msg, short argc, t_atom *argv)
{
    int i;
    t_buffer_obj *wavebuf_b = NULL;
    long b_frames;
    float *b_samples;
    
    int al, dl, sl, rl;
    int j;
    float downgain = 0.33;
    
    if( function_openbuf(x) ){
        wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
        b_frames= buffer_getframecount(wavebuf_b);
        b_samples = buffer_locksamples(wavebuf_b);
    } else {
        return;
    }
    al = (float) b_frames * atom_getfloatarg(0,argc,argv);
    dl = (float) b_frames * atom_getfloatarg(1,argc,argv);
    rl = (float) b_frames * atom_getfloatarg(2,argc,argv);
    downgain = atom_getfloatarg(3,argc,argv);
    if(downgain <= 0)
        downgain = 0.333;
    if(al+dl+rl >= b_frames){
        post("atk and dk and release are too long");
        return;
    }
    sl = b_frames - (al+dl+rl);
    
    for(i=0;i<al;i++){
        b_samples[i] = (float)i/(float)al;
    }
    for(i=al, j=dl;i<al+dl;i++,j--){
        b_samples[i] = downgain + (1.-downgain)*(float)j/(float)dl;
    }
    for(i=al+dl;i<al+dl+sl;i++){
        b_samples[i] = downgain;
    }
    for(i=al+dl+sl,j=rl;i<b_frames;i++,j--){
        b_samples[i] = downgain * (float)j/(float)rl;
    }
	buffet_update(x);
    buffer_unlocksamples(wavebuf_b);
}

void function_adenv(t_function *x, t_symbol *msg, short argc, t_atom *argv)
{
    int i;
    t_buffer_obj *wavebuf_b = NULL;
    long b_frames;
    float *b_samples;
    int j;
    int al, dl, rl;
    float downgain = 0.33;
    
    if( function_openbuf(x) ){
        wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
        b_frames= buffer_getframecount(wavebuf_b);
        b_samples = buffer_locksamples(wavebuf_b);
    } else {
        return;
    }
    al = (float) b_frames * atom_getfloatarg(0,argc,argv);
    dl = (float) b_frames * atom_getfloatarg(1,argc,argv);
    downgain = atom_getfloatarg(2,argc,argv);
    if(downgain <= 0)
        downgain = 0.333;
    if(al+dl >= b_frames){
        post("atk and dk are too long");
        return;
    }
    rl = b_frames - (al+dl);
    
    for(i=0;i<al;i++){
        b_samples[i] = (float)i/(float)al;
    }
    for(i=al, j=dl;i<al+dl;i++,j--){
        b_samples[i] = downgain + (1.-downgain)*(float)j/(float)dl;
    }
    for(i=al+dl,j=rl;i<b_frames;i++,j--){
        b_samples[i] = downgain * (float)j/(float)rl;
    }
	buffet_update(x);
    buffer_unlocksamples(wavebuf_b);
}

void function_aenv(t_function *x, t_symbol *msg, short argc, t_atom *argv)
{
    int i;
    t_buffer_obj *wavebuf_b = NULL;
    long b_frames;
    float *b_samples;
    int j;
    int al, dl;
    float frac;
    frac = atom_getfloatarg(0,argc,argv);
    
    if( function_openbuf(x) ){
        wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
        b_frames= buffer_getframecount(wavebuf_b);
        b_samples = buffer_locksamples(wavebuf_b);
    } else {
        return;
    }
    if(frac <= 0 || frac >= 1){
        post("* attack time must range from 0.0 - 1.0, rather than %f",frac);
    }
    
    al = b_frames * frac;
    
    dl = b_frames - al;
    for(i=0;i<al;i++){
        b_samples[i] = (float)i/(float)al;
    }
    for(i=al, j=dl;i<b_frames;i++,j--){
        b_samples[i] = (float)j/(float)dl;
    }
	buffet_update(x);
    buffer_unlocksamples(wavebuf_b);
}

void function_normalize(t_function *x, t_floatarg f)
{
    x->normalize = (short)f;
}

int function_openbuf(t_function *x)
{
    
    t_buffer_obj *wavebuf_b;
    long b_nchans;
    long b_frames;
    float *b_samples;
    
    // Acquire Buffer
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    wavebuf_b = buffer_ref_getobject(x->src_buffer_ref);
    if(wavebuf_b == NULL){
        // buffer_unlocksamples(wavebuf_b);
        return 0; // bad buf
    }
    b_nchans = buffer_getchannelcount(wavebuf_b);
    b_frames= buffer_getframecount(wavebuf_b);
	b_samples = buffer_locksamples(wavebuf_b);
    
    if(! b_samples){
        buffer_unlocksamples(wavebuf_b);
        return 0; // bad buf
    }
    if( b_nchans != 1){
        object_post((t_object *)x, "buffer must have only 1 channel, this one has %d", b_nchans);
        buffer_unlocksamples(wavebuf_b);
        return 0; // bad buf
    }
    buffer_unlocksamples(wavebuf_b);
    return 1;  // good buffer acquired
}

void buffet_update(t_function *x)
{
    t_buffer_obj *the_buffer;
    if(!x->src_buffer_ref){
        x->src_buffer_ref = buffer_ref_new((t_object*)x, x->wavename);
    } else{
        buffer_ref_set(x->src_buffer_ref, x->wavename);
    }
    the_buffer = buffer_ref_getobject(x->src_buffer_ref);
    object_method( the_buffer, gensym("dirty") );
}
