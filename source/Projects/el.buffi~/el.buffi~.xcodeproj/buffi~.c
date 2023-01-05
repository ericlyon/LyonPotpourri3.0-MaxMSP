#include "MSPd.h"

#define OBJECT_NAME "buffi~"

static t_class *buffi_class;

typedef struct _buffi
{
    t_pxobject x_obj;
    t_symbol *src1; // name of src1 buffer
    t_symbol *src2; // name of src2 buffer
    t_symbol *dest; // name of dest buffer
    t_buffer_ref *src1_buffer_ref; // MSP reference to the buffer
    t_buffer_ref *src2_buffer_ref; // MSP reference to the buffer
    t_buffer_ref *dest_buffer_ref; // MSP reference to source buffer
    float *warpfunc; // temporary holder for the warp functions
} t_buffi;

void *buffi_new(t_symbol *msg, short argc, t_atom *argv);
void buffi_assist (t_buffi *x, void *b, long msg, long arg, char *dst);
void buffi_dsp64(t_buffi *x, t_object *dsp64, short *count, double sr, long n, long flags);
void buffi_perform64(t_buffi *x, t_object *dsp64, double **ins,
                      long numins, double **outs,long numouts, long n,
                      long flags, void *userparam);
t_max_err buffi_notify(t_buffi *x, t_symbol *s, t_symbol *msg, void *sender, void *data);
void buffi_doset(t_buffi *x);
void buffi_set(t_buffi *x, t_symbol *s);
double buffi_randf( double min, double max );
void buffi_autofunc(t_buffi *x, t_floatarg minval, t_floatarg maxval, t_symbol *buffername);
void buffi_reset(t_buffi *x, t_symbol *src1, t_symbol *src2, t_symbol *dest);



int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.buffi~", (method)buffi_new, (method)dsp_free, sizeof(t_buffi), 0,A_GIMME, 0);
    class_addmethod(c,(method)buffi_assist,"assist", A_CANT, 0);
    class_addmethod(c, (method)buffi_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)buffi_notify, "notify", A_CANT, 0);
    class_addmethod(c, (method)buffi_autofunc, "autofunc", A_FLOAT, A_FLOAT, A_SYM, 0);
    class_addmethod(c, (method)buffi_reset, "reset", A_SYM, A_SYM, A_SYM, 0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	buffi_class = c;
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void *buffi_new(t_symbol *msg, short argc, t_atom *argv)
{
    t_buffi *x = (t_buffi *)object_alloc(buffi_class);

    dsp_setup((t_pxobject *)x,1);
    outlet_new((t_object *)x, "signal");

    
    if(argc < 3){
        error("%s: you must provide three buffer names: src1, src2, and dest",OBJECT_NAME);
        return NIL;
    }

    atom_arg_getsym(&x->src1,0,argc,argv);
    atom_arg_getsym(&x->src2,1,argc,argv);
    atom_arg_getsym(&x->dest,2,argc,argv);
    buffi_set(x, msg);
    srandom(clock());
    return x;
}


void buffi_reset(t_buffi *x, t_symbol *src1, t_symbol *src2, t_symbol *dest)
{
    x->src1 = src1;
    x->src2 = src2;
    x->dest = dest;
    buffi_doset(x);
    
}

t_max_err buffi_notify(t_buffi *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    buffer_ref_notify(x->src1_buffer_ref, s, msg, sender, data);
    buffer_ref_notify(x->src2_buffer_ref, s, msg, sender, data);
    return buffer_ref_notify(x->dest_buffer_ref, s, msg, sender, data);
    
}

void buffi_doset(t_buffi *x)
{
    if (!x->src1_buffer_ref)
        x->src1_buffer_ref = buffer_ref_new((t_object *)x, x->src1);
    else
        buffer_ref_set(x->src1_buffer_ref, x->src1);

    if (!x->src2_buffer_ref)
        x->src2_buffer_ref = buffer_ref_new((t_object *)x, x->src2);
    else
        buffer_ref_set(x->src2_buffer_ref, x->src2);
    
    if (!x->dest_buffer_ref)
        x->dest_buffer_ref = buffer_ref_new((t_object *)x, x->dest);
    else
        buffer_ref_set(x->dest_buffer_ref, x->dest);
}

void buffi_set(t_buffi *x, t_symbol *s)
{
    defer(x, (method)buffi_doset, s, 0, NULL);
}

void buffi_perform64(t_buffi *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
    t_double	*in = ins[0];
    t_double	*out = outs[0];
    t_float		*src1tab,*src2tab,*desttab;
    long src1nc, src2nc, destnc;
    long src1frames, src2frames, destframes;
    double		phase;
    int i,j;
//    int k;
    long dexter;
    float hybrid; // Max buffers are still 32-bit
    
    t_buffer_obj	*src1 = buffer_ref_getobject(x->src1_buffer_ref);
    t_buffer_obj	*src2 = buffer_ref_getobject(x->src2_buffer_ref);
    t_buffer_obj	*dest = buffer_ref_getobject(x->dest_buffer_ref);
    
    src1tab = buffer_locksamples(src1);
    src2tab = buffer_locksamples(src2);
    desttab = buffer_locksamples(dest);
    if (!src1tab || !src2tab || !desttab){
        goto zero;

    }
    src1frames = buffer_getframecount(src1);
    src1nc = buffer_getchannelcount(src1);
    src2frames = buffer_getframecount(src2);
    src2nc = buffer_getchannelcount(src2);
    destframes = buffer_getframecount(dest);
    destnc = buffer_getchannelcount(dest);
    
    // test for lengths match (otherwise jet)
    
    if( (src1frames != destframes) || (src2frames != destframes) ){
        goto zero;
    }
    
    // test for channels match

    if( (src1nc != src2nc) || (src2nc != destnc) ){
        goto zero;
    }
    
    phase = *in; // only read the first sample of the vector

  
    for(i = 0; i < src1frames; i++){
        for(j = 0; j < src1nc; j++){
            dexter = (i*src1nc) + j;
            hybrid = (src1tab[dexter] * phase) + (src2tab[dexter] * (1 - phase));
            desttab[dexter] = hybrid;
        }
    }

    
    buffer_unlocksamples(src1);
    buffer_unlocksamples(src2);
    buffer_unlocksamples(dest);
    return;
zero:
    for(i = 0; i < sampleframes; i++){
        *out++ = 0.0;
    }
}

void buffi_dsp64(t_buffi *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)buffi_perform64, 0, NULL);
}

void buffi_assist (t_buffi *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(mesages) Buffer Interpolation Value [0-1]"); break;
    	}
	}
    // nothing interesting at the output
}

void buffi_autofunc(t_buffi *x, t_floatarg minval, t_floatarg maxval, t_symbol *buffername)
{
    int minpoints, maxpoints, segpoints, i;
    int pointcount = 0;
    double target, lastval;
    double m1, m2;
    t_buffer_ref *tablebuf_ref;
    t_buffer_obj *dbuf;
    long b_nchans;
    long b_frames;
    float *b_samples;
    
    /////

    tablebuf_ref = buffer_ref_new((t_object*)x, buffername);

    dbuf = buffer_ref_getobject(tablebuf_ref);
    if(dbuf == NULL){
        object_post((t_object*)x,"%s: nonexistent buffer ( %s )",
                    OBJECT_NAME, buffername->s_name);
        return;
    }
    b_nchans = buffer_getchannelcount(dbuf);
    b_frames = buffer_getframecount(dbuf);
    if(b_nchans != 1){
        post("%s: table must be mono",OBJECT_NAME);
        return;
    }
    x->warpfunc = (float *)sysmem_newptr(b_frames * sizeof(float));
    
    minpoints = 0.05 * (float) b_frames;
    maxpoints = 0.25 * (float) b_frames;
    if( minval > 1000.0 || minval < .001 ){
        minval = 0.5;
    }
    if( maxval < 0.01 || maxval > 1000.0 ){
        minval = 2.0;
    }
    
    lastval = buffi_randf(minval, maxval);
    // post("automate: min %d max %d",minpoints, maxpoints);
    while( pointcount < b_frames ){
        target = buffi_randf(minval, maxval);
        segpoints = minpoints + (rand() % (maxpoints-minpoints));
        if( pointcount + segpoints > b_frames ){
            segpoints = b_frames - pointcount;
        }
        for( i = 0; i < segpoints; i++ ){
            m2 = (float)i / (float) segpoints ;
            m1 = 1.0 - m2;
            x->warpfunc[ pointcount + i ] = m1 * lastval + m2 * target;
        }
        lastval = target;
        pointcount += segpoints;
    }
    // buffer stuffer
    b_samples = buffer_locksamples(dbuf);
    for(i = 0; i < b_frames; i++){
        b_samples[i] = x->warpfunc[i];
    }
    buffer_unlocksamples(dbuf);
    sysmem_freeptr( (void *) x->warpfunc );
    object_method(dbuf, gensym("dirty"));
}



double buffi_randf( double min, double max ){
    return min + (max - min) * ((double) rand() / (double)RAND_MAX);
}

