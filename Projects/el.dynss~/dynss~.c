#include "MSPd.h"

static t_class *dynss_class;

#define MAXPOINTS (64)
#define OBJECT_NAME "dynss~"
#define COMPILE_DATE "9.02.07"
#define OBJECT_VERSION "2.01"
// #define DATE "prerelease"

typedef struct _dynss
{

	t_pxobject x_obj;
 
	long point_count; // how many points in waveform (including beginning and end point)
	t_double freq; // frequency
	long counter; // count samples
	long period_samples; // how many samples in a period
	float srate; // sampling rate
	long current_point; // which point are we on
	//t_depoint *dpoints; // all the points
	t_double *values;
	long *point_breaks;
	t_double *norm_breaks;
	t_double *xdevs;
	t_double *ydevs;
	t_double devscale_x;
	t_double devscale_y;
} t_dynss;

void *dynss_new(t_symbol *msg, short argc, t_atom *argv);
void dynss_assist(t_dynss *x, void *b, long m, long a, char *s);
float boundrand(float min, float max);
void dynss_init(t_dynss *x,short initialized);
void dynss_free(t_dynss *x);
void dynss_devx(t_dynss *x, t_floatarg f);
void dynss_devy(t_dynss *x, t_floatarg f);
void dynss_freq(t_dynss *x, t_floatarg f);
void dynss_printwave(t_dynss *x);
void dynss_new_wave(t_dynss *x);
void dynss_pointcount(t_dynss *x, t_floatarg f);
void dynss_new_amps(t_dynss *x);
void dynss_perform64(t_dynss *x, t_object *dsp64, double **ins, 
                     long numins, double **outs,long numouts, long n,
                     long flags, void *userparam);
void dynss_dsp64(t_dynss *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);


int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.dynss~", (method)dynss_new, (method)dsp_free, sizeof(t_dynss), 0,A_GIMME, 0);
	class_addmethod(c,(method)dynss_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c,(method)dynss_assist,"assist",A_CANT,0);
	class_addmethod(c,(method)dynss_devx,"devx",A_FLOAT,0);
	class_addmethod(c,(method)dynss_devy,"devy",A_FLOAT,0);
	class_addmethod(c,(method)dynss_freq,"freq",A_FLOAT,0);
	class_addmethod(c,(method)dynss_printwave,"printwave",0);
	class_addmethod(c,(method)dynss_new_wave,"new_wave",0);
	class_addmethod(c,(method)dynss_new_amps,"new_amps",0);
	class_addmethod(c,(method)dynss_pointcount,"pointcount",A_FLOAT,0);
	potpourri_announce(OBJECT_NAME);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	dynss_class = c;	
	return 0;
}


void dynss_pointcount(t_dynss *x, t_floatarg f)
{
	if(f >= 2 && f <= MAXPOINTS ){
		x->point_count = (long) f;
		dynss_init(x,0);
	}
}

void dynss_freq(t_dynss *x, t_floatarg f)
{
	x->freq = fabs(f);
}

void dynss_devx(t_dynss *x, t_floatarg f)
{
	x->devscale_x = f;
}

void dynss_devy(t_dynss *x, t_floatarg f)
{
	x->devscale_y = f;
}

void version(void)
{
	post("%s version %s, compiled %s",OBJECT_NAME, OBJECT_VERSION, COMPILE_DATE);
}

void dynss_printwave(t_dynss *x)
{
	int i;
	for(i = 0; i < x->point_count; i++){
		post("point %d break %d norm break %f value %f",i, x->point_breaks[i], x->norm_breaks[i], x->values[i]);
	}
}

void dynss_new_wave(t_dynss *x)
{
	dynss_init(x,0);
}

void dynss_new_amps(t_dynss *x)
{
	int i;
	for(i = 1; i < x->point_count - 1; i++){
		x->values[i] = boundrand(-0.95, 0.95);
	}
}

void dynss_assist (t_dynss *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Trigger Impulse"); break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Beat Impulse %ld",arg + 1);
	}
}

void *dynss_new(t_symbol *msg, short argc, t_atom *argv)
{
	    
	t_dynss *x = (t_dynss *)object_alloc(dynss_class);
	dsp_setup((t_pxobject *)x,1);
	outlet_new((t_pxobject *)x, "signal");  
	x->x_obj.z_misc |= Z_NO_INPLACE;
	
	x->srate = sys_getsr();
	
	x->point_count = 6; // including fixed start and endpoint, so just 4 dynamic points
	
	x->values = (t_double *) calloc(MAXPOINTS + 2, sizeof(t_double));
	x->point_breaks = (long *) calloc(MAXPOINTS + 2, sizeof(long));
	x->norm_breaks = (t_double *) calloc(MAXPOINTS + 2, sizeof(t_double));
	x->xdevs = (t_double *) calloc(MAXPOINTS + 2, sizeof(t_double));
	x->ydevs = (t_double *) calloc(MAXPOINTS + 2, sizeof(t_double));
	
	if(! x->srate ){
		x->srate = 44100;
		post("sr autoset to 44100");
	}
	x->freq = 100.0;
	
	srandom(time(0));

	dynss_init(x,0); 
	
	return (x);
}

void dynss_free(t_dynss *x)
{
	dsp_free((t_pxobject *) x);
}

float boundrand(float min, float max)
{
	return min + (max-min) * ((float) (rand() % RAND_MAX)/ (float) RAND_MAX);
}

void dynss_init(t_dynss *x,short initialized)
{
	int i,j;
	float findex;
	long point_count = x->point_count;
	t_double *values = x->values;
	t_double *norm_breaks = x->norm_breaks;
	long *point_breaks = x->point_breaks;
	t_double *ydevs = x->ydevs;
	t_double *xdevs =x->xdevs;

	
    if(!initialized){
		x->period_samples = (long)(x->srate / x->freq);
		x->counter = 0;
		x->current_point = 0;
		x->devscale_x = 0.0; // no evolution by default
		x->devscale_y = 0.0;
		// post("there are %d samples per period, srate %f freq %f", x->period_samples, x->srate, x->freq);
		norm_breaks[0] = 0.0;
		norm_breaks[point_count - 1] = 1.0;
		values[0] = values[point_count - 1] = 0.0;
		for(i = 1; i < point_count - 1; i++){
			values[i] = boundrand(-1.0, 1.0);
			norm_breaks[i] = boundrand(0.05,0.95);
		}
		// now sort into order (insertion sort)

		for(i = 1; i < point_count; i++){
			findex = norm_breaks[i];
			j = i;
			while( j > 0 && norm_breaks[j-1] > findex){
				norm_breaks[j] = norm_breaks[j-1];
				j = j - 1;
			}
			norm_breaks[j] = findex;
				
		}
		// now generate sample break points;
		for(i = 0; i < point_count; i++){
			point_breaks[i] = (long) ( (float)x->period_samples * norm_breaks[i] );
			post("%i %f %f",point_breaks[i], norm_breaks[i], values[i]);
		}
		// set y deviation maxes
		for(i = 0; i < point_count; i++){
			ydevs[i] = boundrand(0.0,0.99);
			xdevs[i] = boundrand(0.0,0.99);
			// post("rands: %f %f",ydevs[i],xdevs[i]);
		}
    } else {
		
    }
}

void dynss_perform64(t_dynss *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
    int i,j;
	t_double findex1,findex2;
	t_double *outlet = outs[0];
	t_double *values = x->values;
	t_double *norm_breaks = x->norm_breaks;
	long *point_breaks = x->point_breaks;
	t_double *ydevs = x->ydevs;
	t_double *xdevs = x->xdevs;
	long counter = x->counter;
	long period_samples = x->period_samples;
	long current_point = x->current_point;
	long point_count = x->point_count;
	t_double sample;
	t_double frak;
	t_double dev, newval;
	long segsamps;
	
	while(n--){
		if( counter == point_breaks[current_point + 1]){
			sample = values[current_point + 1];
			++current_point;
			if(current_point > point_count - 1){
				current_point = 0;
				counter = 0;
			}
		} 
        
		else {
			segsamps = point_breaks[current_point + 1] - point_breaks[current_point];
			if( segsamps <= 1){
				frak = 0.0;
			} 
			else {
				frak = (float)(counter - point_breaks[current_point]) / (float)segsamps;
				if( frak < 0.0 || frak > 1.0 ){
					post("bad fraction: %f",frak);
					post("current point: %d", current_point);
					post("segsamps %d counter %d current break %d next break %d", segsamps, counter, point_breaks[current_point], point_breaks[current_point + 1]);
				}
			}
			if(current_point < 0 || current_point > point_count - 1){
				post("ERROR: dss had bad current point!");
				sample = 0;
			} else {
				sample = values[current_point] + (values[current_point+1] - values[current_point]) * frak;
				
			}
		}
		++counter;
		if(counter >= period_samples){
			counter = 0;
			current_point = 0;
			if( x->freq > 0.0 ){
				period_samples = x->srate / x->freq;
			}
			// nudge waveform
			for(i = 1; i < point_count - 1; i++){
				dev = boundrand(-1.0,1.0) * ydevs[i] * x->devscale_y;
				newval = values[i] + dev;
				// clip
				newval = newval > 0.95 ? 0.95 : newval;
				newval = newval < -0.95 ? -0.95 : newval;
				values[i] = newval;
			}
			for(i = 1; i < point_count - 1; i++){
				dev = boundrand(-1.0,1.0) * xdevs[i] * x->devscale_x;
				newval = norm_breaks[i] + dev;
				// clip
				newval = newval < 0.05 ? 0.05 : newval;
				newval = newval > 0.95 ? 0.95: newval;
				norm_breaks[i] = newval;
				/* if(norm_breaks[i] < norm_breaks[i-1]){
                 norm_breaks[i] = norm_breaks[i-1] + 0.03; // disallow point jumps for now
                 } */
			}
			// now sort them
			
			for(i = 1; i < point_count; i++){
				findex1 = norm_breaks[i];
				findex2 = values[i];
				j = i;
				while( j > 0 && norm_breaks[j-1] > findex1){
					norm_breaks[j] = norm_breaks[j-1];
					values[j] = values[j-1];
					j = j - 1;
				}
				norm_breaks[j] = findex1;
				values[j] = findex2;
			}
			
			// now generate sample breaks
			for(i = 0; i < point_count; i++){
				point_breaks[i] = (long) ( (float)period_samples * norm_breaks[i] );
			}
		}
		*outlet++ = sample;
	}
	x->counter = counter;
	x->current_point = current_point;	
	x->period_samples = period_samples;
}



void dynss_dsp64(t_dynss *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    // zero sample rate, go die
    if(!samplerate)
        return;
	if(x->srate != samplerate) {
		x->srate = samplerate;
	}
    object_method(dsp64, gensym("dsp_add64"),x,dynss_perform64,0,NULL);
}
