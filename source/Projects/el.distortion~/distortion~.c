#include "MSPd.h"

static t_class *distortion_class;

#define OBJECT_NAME "distortion~"

typedef struct _distortion
{
    
    t_pxobject x_obj;
	t_double knee;
	t_double cut;
	t_double rescale;
	short mute;
	short case1;
    short *connections;
} t_distortion;

void *distortion_new(t_floatarg knee, t_floatarg cut);

void distortion_dsp(t_distortion *x, t_signal **sp, short *count);
void distortion_assist(t_distortion *x, void *b, long m, long a, char *s);
void distortion_float( t_distortion *x, double f );
void distortion_mute(t_distortion *x, t_floatarg f);
void distortion_dsp64(t_distortion *x, t_object *dsp64, short *count, double samplerate, long n, long flags);
void distortion1_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs,long numouts, long n,
                           long flags, void *userparam);
void distortion2_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs,long numouts, long n,
                           long flags, void *userparam);
void distortion3_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs,long numouts, long n,
                           long flags, void *userparam);
void distortion4_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs,long numouts, long n,
                           long flags, void *userparam);
int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.distortion~", (method)distortion_new, (method)dsp_free, sizeof(t_distortion), 0,
                  A_FLOAT,A_FLOAT,0);
	
    class_addmethod(c, (method)distortion_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)distortion_assist,"assist",A_CANT,0);
    class_addmethod(c, (method)distortion_mute,"mute",A_FLOAT,0);
    class_addmethod(c, (method)distortion_float,"float",A_FLOAT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	distortion_class = c;
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void distortion_assist (t_distortion *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Input"); break;
			case 1: sprintf(dst,"(signal/float) Knee"); break;
			case 2: sprintf(dst,"(signal/float) Cut"); break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Output");
	}
}

void *distortion_new(t_floatarg knee, t_floatarg cut)
{
    t_distortion *x = (t_distortion *)object_alloc(distortion_class);
    dsp_setup((t_pxobject *)x,3);
    outlet_new((t_pxobject *)x, "signal");
    x->x_obj.z_misc |= Z_NO_INPLACE;
	if( knee >= cut || knee <= 0 || cut <= 0 ) {
		x->knee = .1;
		x->cut = .3 ;
	} else {
		x->knee = knee;
		x->cut = cut;
	}
    // post("%f %f", x->knee, x->cut);
	x->rescale = 1.0 / x->cut ;
	x->mute = 0;
    return x;
}

// use when neither signal is connected



void distortion_perform64(t_distortion *x, t_object *dsp64, double **ins,
                          long numins, double **outs,long numouts, long n,
                          long flags, void *userparam)
{
    t_double *in = ins[0];
    t_double *data1 = ins[1];
    t_double *data2 =ins[2];
    t_double *out = outs[0];
    t_double in_sample;
    t_double rescale;
	t_double knee = x->knee;
	t_double cut = x->cut;
    t_double rectified_sample;

    while (n--) {
        in_sample = *in++;
        knee = *data1++;
        cut = *data2++;
        if( cut > 0.000001 )
            rescale = 1.0 / cut;
        else
            rescale = 1.0;
        
        rectified_sample = fabs( in_sample );
        if( rectified_sample < knee ){
            *out++ = in_sample;
        } else {
            if( in_sample > 0.0 ){
                *out++ = rescale * (knee + (rectified_sample - knee) * (cut - knee));
            } else {
                *out++ = rescale * (-(knee + (rectified_sample - knee) * (cut - knee)));
            }
        }
    }
}

void distortion1_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs,long numouts, long n,
                           long flags, void *userparam)
{
    t_double *in = ins[0];
    t_double *out = outs[0];
    t_double in_sample;
    t_double rescale;
	t_double knee = x->knee;
	t_double cut = x->cut;
    t_double rectified_sample;
    
    
    while (n--) {
        in_sample = *in++;
        if( cut > 0.000001 )
            rescale = 1.0 / cut;
        else
            rescale = 1.0;
        
        rectified_sample = fabs( in_sample );
        if( rectified_sample < knee ){
            *out++ = in_sample;
        } else {
            if( in_sample > 0.0 ){
                *out++ = rescale * (knee + (rectified_sample - knee) * (cut - knee));
            } else {
                *out++ = rescale * (-(knee + (rectified_sample - knee) * (cut - knee)));
            }
        }
    }
}

void distortion2_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs,long numouts, long n,
                           long flags, void *userparam)
{
    t_double *in = ins[0];
    t_double *data1 = ins[1];
    t_double *data2 =ins[2];
    t_double *out = outs[0];
    t_double in_sample;
    t_double rescale;
	t_double knee = x->knee;
	t_double cut = x->cut;
    t_double rectified_sample;
    
    while (n--) {
        in_sample = *in++;
        knee = *data1++;
        cut = *data2++;
        if( cut > 0.000001 )
            rescale = 1.0 / cut;
        else
            rescale = 1.0;
        
        rectified_sample = fabs( in_sample );
        if( rectified_sample < knee ){
            *out++ = in_sample;
        } else {
            if( in_sample > 0.0 ){
                *out++ = rescale * (knee + (rectified_sample - knee) * (cut - knee));
            } else {
                *out++ = rescale * (-(knee + (rectified_sample - knee) * (cut - knee)));
            }
        }
        
    }
}

void distortion3_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs,long numouts, long n,
                           long flags, void *userparam)
{
    t_double *in = ins[0];
    t_double *data1 = ins[1];
    t_double *out = outs[0];
    t_double in_sample;
    t_double rescale;
	t_double knee = x->knee;
	t_double cut = x->cut;
    t_double rectified_sample;
    
    
    while (n--) {
        in_sample = *in++;
        knee = *data1++;
        if( cut > 0.000001 )
            rescale = 1.0 / cut;
        else
            rescale = 1.0;
        
        rectified_sample = fabs( in_sample );
        if( rectified_sample < knee ){
            *out++ = in_sample;
        } else {
            if( in_sample > 0.0 ){
                *out++ = rescale * (knee + (rectified_sample - knee) * (cut - knee));
            } else {
                *out++ = rescale * (-(knee + (rectified_sample - knee) * (cut - knee)));
            }
        }
        
    }
}

void distortion4_perform64(t_distortion *x, t_object *dsp64, double **ins,
                           long numins, double **outs,long numouts, long n,
                           long flags, void *userparam)
{
    t_double *in = ins[0];
    t_double *data2 = ins[2];
    t_double *out = outs[0];
    t_double in_sample;
    t_double rescale;
	t_double knee = x->knee;
	t_double cut = x->cut;
    t_double rectified_sample;
    
    while (n--) {
        in_sample = *in++;
        cut = *data2++;
        if( cut > 0.000001 )
            rescale = 1.0 / cut;
        else
            rescale = 1.0;
        
        rectified_sample = fabs( in_sample );
        if( rectified_sample < knee ){
            *out++ = in_sample;
        } else {
            if( in_sample > 0.0 ){
                *out++ = rescale * (knee + (rectified_sample - knee) * (cut - knee));
            } else {
                *out++ = rescale * (-(knee + (rectified_sample - knee) * (cut - knee)));
            }
        }
        
    }
}

// use when both signals are connected



void distortion_mute(t_distortion *x, t_floatarg f) {
	x->mute = f;
}

void distortion_float( t_distortion *x, double f )
{
	int inlet = ((t_pxobject*)x)->z_in;
	switch( inlet ){
		case 1:
			x->knee = f;
			// post("knee: %f",x->knee);
			if( x->knee < .000001 )
				x->knee = .000001 ;
            
			break;
		case 2:
			x->cut = f;
			// post("cut: %f",x->cut);
			if( x->cut <= 0.0 ) {
				x->cut = .01 ;
			} else if( x->cut > 1.0 ) {
				x->cut = 1.0 ;
			}
			x->rescale = 1.0 / x->cut ;
            
            break;
	}
}


void distortion_dsp64(t_distortion *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    // int i;
    if(!samplerate)
        return;
    x->connections = count;
    if( count[0] && ! count[1] && ! count[2] ){
        object_method(dsp64, gensym("dsp_add64"),x,distortion1_perform64,0,NULL);
    } else if (count[0] && count[1] && count[2]) {
        object_method(dsp64, gensym("dsp_add64"),x,distortion2_perform64,0,NULL);
    } else if (count[0] && count[1] && ! count[2]) {
        // x->case1 = 1;
        object_method(dsp64, gensym("dsp_add64"),x,distortion3_perform64,0,NULL);
    }
    else if (count[0] && ! count[1] && count[2]) {
        // x->case1 = 0;
        object_method(dsp64, gensym("dsp_add64"),x,distortion4_perform64,0,NULL);
    }
    //object_method(dsp64, gensym("dsp_add64"),x,distortion_perform64,0,NULL);
}

