#include "MSPd.h"

static t_class *samm_class;

#define MAXBEATS (256)
#define OBJECT_NAME "samm~"
#define COMPILE_DATE "9.02.07"
#define OBJECT_VERSION "2.01"
// #define DATE "prerelease"

typedef struct _samm
{

	t_pxobject x_obj;

	double tempo; /* current tempo */
	double onebeat_samps; /* number of samples for a single beat */
	double *beats; /* amount of beats for each active tempo outlet */
	double *metro_samps; /* number of samples to count down for each time interval */
	double *metro_beatdurs; /* number of beats for each metro time interval */
	double *metro; /* current countdown for each time interval */
	int metro_count; /* number of metronomes to keep track of */
	double sr; /* current sampling rate */
	short pause;
	short mute;
	short tap_listen; /* flag that we are listening for a tempo */
	long tap_samples; /* running count of samples  while listening for tempo */
	/* Pd only */
	t_double *trigger_vec;
	short vs;
} t_samm;

void *samm_new(t_symbol *msg, short argc, t_atom *argv);
void *samm_beatlist(t_samm *x, t_symbol *msg, short argc, t_atom *argv);
void samm_assist(t_samm *x, void *b, long m, long a, char *s);
void samm_tempo(t_samm *x, t_floatarg f);
void samm_divbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv);
void samm_msbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv);
void samm_sampbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv);
void samm_ratiobeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv);
void samm_free(t_samm *x);
void samm_beatinfo(t_samm *x);
void samm_init(t_samm *x,short initialized);
void samm_mute(t_samm *x, t_floatarg f);
void samm_pause(t_samm *x);
void samm_arm(t_samm *x);
void samm_resume(t_samm *x);
void samm_beats(t_samm *x, t_symbol *msg, short argc, t_atom *argv);
void version(void);
void samm_tap_tempo(t_samm *x);
void samm_tap_end(t_samm *x);
void samm_dsp64(t_samm *x, t_object *dsp64, short *count, double sr, long n, long flags);
void samm_perform64(t_samm *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.samm~", (method)samm_new, (method)dsp_free, sizeof(t_samm), 0,A_GIMME, 0);
	class_addmethod(c,(method)samm_mute,"mute",A_FLOAT,0);
	class_addmethod(c,(method)samm_tempo,"tempo",A_FLOAT,0);
	class_addmethod(c,(method)samm_assist,"assist",A_CANT,0);
    class_addmethod(c,(method)samm_dsp64,"dsp64",A_CANT,0);
	class_addmethod(c,(method)samm_beatinfo,"beatinfo",0);
	class_addmethod(c,(method)samm_beats,"beats",A_GIMME,0);
	class_addmethod(c,(method)samm_divbeats,"divbeats",A_GIMME,0);
	class_addmethod(c,(method)samm_msbeats,"msbeats",A_GIMME,0);
	class_addmethod(c,(method)samm_sampbeats,"sampbeats",A_GIMME,0);
	class_addmethod(c,(method)samm_ratiobeats,"ratiobeats",A_GIMME,0);
	class_addmethod(c,(method)samm_pause,"pause",0);
	class_addmethod(c,(method)samm_arm,"arm",0);
	class_addmethod(c,(method)samm_resume,"resume",0);
	class_addmethod(c,(method)samm_tap_tempo,"tap_tempo",0);
	class_addmethod(c,(method)samm_tap_end,"tap_end",0);
	potpourri_announce(OBJECT_NAME);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	samm_class = c;	return 0;
}



void samm_tap_end(t_samm *x)
{
double new_tempo;
	if( ! x->tap_samples ){
		error("%s: zero tap samples",OBJECT_NAME);
		return;
	}
	new_tempo = 60.0 * x->sr / x->tap_samples;
	x->tap_listen = 0;
	samm_tempo(x,new_tempo);
	samm_resume(x);
}

void samm_tap_tempo(t_samm *x)
{
	x->tap_listen = 1;
	x->tap_samples = 0;
	samm_arm(x);
}

void samm_beatinfo(t_samm *x)
{
	int i;
	post("tempo %.10f",x->tempo);
	post("samples in one beat: %.10f",x->onebeat_samps);
	for(i = 0; i < x->metro_count; i++){
		post("%d: relative duration %.10f, samples %.10f samples ratio to 1 beat: %.10f", i, 
			 x->metro_beatdurs[i], x->metro_samps[i], x->onebeat_samps / x->metro_samps[i]  );
	}
}

void samm_pause(t_samm *x)
{
	x->pause = 1;
}

void samm_mute(t_samm *x, t_floatarg f)
{
	x->mute = (short) f;
}

void samm_arm(t_samm *x)
{
	int i;
	
	x->pause = 1;
	for( i = 0; i < x->metro_count; i++){
		x->metro[i] = 1.0;
	}
	
}

void samm_resume(t_samm *x)
{
	x->pause = 0;
}

void samm_beats(t_samm *x, t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	double beatdur;
	
	if(argc != x->metro_count){
		error("%s: arguments did not match metro count %d",x->metro_count);
		return;
	}
	
	for(i = 0; i < argc; i++){
		beatdur = (double)atom_getfloatarg(i,argc,argv);
    	if(!beatdur){
			error("%s: zero divisor given for beat stream %d",OBJECT_NAME,i+1);
			beatdur = 1.0;
		}
		
		x->metro_beatdurs[i] = beatdur;
		x->metro_samps[i] = x->metro_beatdurs[i] * x->onebeat_samps;
		x->metro[i] = 1.0; // initialize for instantaneous beat
	}	
}

void samm_divbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	double divisor;
	
	if(argc != x->metro_count){
		error("%s: arguments did not match metro count %d",x->metro_count);
		return;
	}
	
	for(i = 0; i < argc; i++){
		divisor = (double)atom_getfloatarg(i,argc,argv);
    	if(!divisor){
			error("%s: zero divisor given for beat stream %d",OBJECT_NAME,i+1);
			divisor = 1.0;
		}
		
		x->metro_beatdurs[i] = 1.0 / divisor; // argument is now DIVISOR of beat 
		x->metro_samps[i] = x->metro_beatdurs[i] * x->onebeat_samps;
		x->metro[i] = 1.0; // initialize for instantaneous beat
	}	
}

void samm_msbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	double msecs;
	if(argc != x->metro_count){
		error("%s: arguments did not match metro count %d",x->metro_count);
		return;
	}
	for(i = 0; i < argc; i++){
		msecs = (double)atom_getfloatarg(i,argc,argv);
    	if(msecs <= 0){
			error("%s: illegal duration for beat stream %d",OBJECT_NAME,i+1);
			msecs = 1000.0;
		}
		x->metro_samps[i] = x->sr * .001 * msecs;
		x->metro_beatdurs[i] = x->metro_samps[i] / x->onebeat_samps; // just in case tempo changes 
		x->metro[i] = 1.0; // initialize for instantaneous beat
	}	
	
}

void samm_sampbeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv)
{
	int i;
	double samples;
	if(argc != x->metro_count){
		error("%s: arguments did not match metro count %d",x->metro_count);
		return;
	}
	for(i = 0; i < argc; i++){
		samples = (double)atom_getfloatarg(i,argc,argv);
    	if(samples <= 0){
			error("%s: illegal duration for beat stream %d",OBJECT_NAME,i+1);
			samples = x->sr;
		}
		x->metro_samps[i] = samples;
		x->metro_beatdurs[i] = x->metro_samps[i] / x->onebeat_samps; // just in case tempo changes 
		x->metro[i] = 1.0; // initialize for instantaneous beat
	}	
}

void samm_ratiobeats(t_samm *x, t_symbol *msg, short argc, t_atom *argv)
{
	int i,j;
	double num,denom;
	
	if(argc != x->metro_count * 2){
		error("%s: arguments did not match metro count %d",x->metro_count);
		return;
	}
	
	for(i = 0, j= 0; i < argc; i += 2, j++){
		num = (double)atom_getfloatarg(i,argc,argv);
		denom = (double)atom_getfloatarg(i+1,argc,argv);
    	if(!denom){
			error("%s: zero divisor given for beat stream %d",OBJECT_NAME,(i/2)+1);
			denom = 1.0;
		}
		
		x->metro_beatdurs[j] = 4.0 * (num / denom);
		//		post("beat duration %f",4.0 * (num/denom)); 
		x->metro_samps[j] = x->metro_beatdurs[j] * x->onebeat_samps;
		x->metro[j] = 1.0; // initialize for instantaneous beat
	}	
}

void samm_tempo(t_samm *x, t_floatarg f)
{
	int i;
	double last_tempo;
	double tempo_fac;
	
	if( f <= 0.0) {
		error("illegal tempo: %f", f);
		return;
	}
	last_tempo = x->tempo;
	x->tempo = f;
	tempo_fac = last_tempo / x->tempo; // shrink or stretch factor for beats
	x->onebeat_samps = (60.0/x->tempo) * x->sr;
	for(i = 0; i < x->metro_count; i++){
		x->metro_samps[i] = x->metro_beatdurs[i] * x->onebeat_samps;
		x->metro[i] *= tempo_fac;
	}	
}

void samm_assist (t_samm *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(signal) Trigger Impulse"); break;
		}
	} else if (msg==2) {
		sprintf(dst,"(signal) Beat Impulse %ld",arg + 1);
	}
}

void *samm_new(t_symbol *msg, short argc, t_atom *argv)
{
	int i,j;
	double divisor;
	
	
	if(argc < 2){
		error("%s: there must be at least 1 beat stream",OBJECT_NAME);
		return (void *)NULL;
	}
	if(argc > MAXBEATS + 1)
    {
		error("%s: exceeded maximum of %d beat values",OBJECT_NAME, MAXBEATS);
		return (void *)NULL;
    }

	t_samm *x = (t_samm *)object_alloc(samm_class);
	
	x->metro_count = argc - 1;
	dsp_setup((t_pxobject *)x,1);
	
	for(i=0;i< x->metro_count ;i++)
		outlet_new((t_pxobject *)x, "signal");  
	x->x_obj.z_misc |= Z_NO_INPLACE;

	x->sr = sys_getsr();
	x->vs = sys_getblksize();
	
	x->pause = 0;
	x->mute = 0;
	x->tap_listen = 0;
	
	x->beats = (double *) calloc(x->metro_count, sizeof(double));
	x->metro_samps = (double *) calloc(x->metro_count, sizeof(double));
	x->metro_beatdurs = (double *) calloc(x->metro_count, sizeof(double));
	x->metro = (double *) calloc(x->metro_count, sizeof(double));
	
	if(! x->sr ){
		x->sr = 44100;
		post("sr autoset to 44100");
	}
	
	if(argc > 0) {
		x->tempo = (double) atom_getfloatarg(0,argc,argv);
	}
	
	if( x->tempo <= 0.0 ){
		x->tempo = 120;
		post("tempo autoset to 120 BPM");
	}
	
	x->onebeat_samps = (60.0/x->tempo) * x->sr;
	
	
	
	
	for(i = 1,j = 0; i < argc; i++, j++){
		divisor = (double)atom_getfloatarg(i,argc,argv);
    	if(!divisor){
			error("%s: zero divisor given for beat stream %d",OBJECT_NAME,i);
			divisor = 1.0;
		}
		
		x->metro_beatdurs[j] = 1.0 / divisor; // argument is now DIVISOR of beat 
		x->metro_samps[j] = x->metro_beatdurs[j] * x->onebeat_samps;
		x->metro[j] = 1.0; // initialize for instantaneous beat
		
	}
	//    post("there are %d beat streams",x->metro_count);
	samm_init(x,0); 
	
	return (x);
}

void samm_free(t_samm *x)
{

	dsp_free((t_pxobject *) x);

    free(x->trigger_vec);
    free(x->beats);
    free(x->metro_samps);
    free(x->metro_beatdurs);
    free(x->metro);
	
}
void samm_init(t_samm *x,short initialized)
{
    if(!initialized){
        x->trigger_vec = (t_double *)calloc(x->vs, sizeof(t_double));
		
		
    } else {
        x->trigger_vec = (t_double *) realloc(x->trigger_vec,x->vs*sizeof(t_double));
    }
}

void samm_perform64(t_samm *x, t_object *dsp64, double **ins, 
                    long numins, double **outs,long numouts, long n,
                    long flags, void *userparam)
{
    int i, j, k;
	t_double *inlet = ins[0];
	t_double *beat_outlet;
		
	int metro_count = x->metro_count;
	double *metro = x->metro;
	t_double *trigger_vec = x->trigger_vec;
	short pause = x->pause;
    
	
	if(x->tap_listen){
		x->tap_samples += n;
		for(i = 0; i < metro_count; i++){
			memset((char *) outs[i], 0, n * sizeof(t_double));
		}
		return;
	}

	/* main loop */    
	for(i=0;i<n;i++)
		trigger_vec[i] = inlet[i];
	
	for(i = 0, j=3; i < metro_count; i++, j++){
		beat_outlet = outs[i];
		for(k = 0; k < n; k++){
			if(trigger_vec[k]) {
				metro[i] = 1.0; // instant reset from an impulse
				pause = 0;
			}
			
			beat_outlet[k] = 0.0;
			if(! pause ){
				metro[i] -= 1.0;
				if( metro[i] <= 0){
					beat_outlet[k] = 1.0;
					metro[i] += x->metro_samps[i];
				} 		
			}	
		}
	}
	x->pause = pause;
}

void samm_dsp64(t_samm *x, t_object *dsp64, short *count, double sr, long n, long flags)
{
    int i;
    if(!sr)
        return;
	if(x->vs != n){
		x->vs = n;
		samm_init(x,1);
	}
	if(x->sr != sr) {
		x->sr = sr;
		x->onebeat_samps = (60.0/x->tempo) * x->sr;
		for(i = 0; i < x->metro_count; i++){
			x->metro_samps[i] = x->metro_beatdurs[i] * x->onebeat_samps;
			x->metro[i] = 0;
		}		
	}
    object_method(dsp64, gensym("dsp_add64"),x,samm_perform64,0,NULL);
}
