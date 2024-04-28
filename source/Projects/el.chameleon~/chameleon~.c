#include "MSPd.h"
#include "chameleon.h"


/* maximum number of parameters for each pattern */
#define MAX_PARAMETERS 1024
/* maximum number of patterns that a chameleon~ can store */
#define MAX_SLOTS 512
/* number of currently coded DSP processes */
#define PROCESS_COUNT 20
/* turn on for debug print messages*/
#define DEBUG_CHAMELEON (0)

#define OBJECT_NAME "chameleon~"

static t_class *chameleon_class;


void *chameleon_new(t_symbol *msg, short argc, t_atom *argv);
t_int *chameleon_perform_hosed(t_int *w);
void chameleon_dsp(t_chameleon *x, t_signal **sp, short *count);
void chameleon_assist (t_chameleon *x, void *b, long msg, long arg, char *dst);
void chameleon_dsp_free(t_chameleon *x);
void chameleon_set_parameters(t_chameleon *x);
t_int *chameleon_perform(t_int *w);
void chameleon_copy_to_MSP_buffer(t_chameleon *x, int slot);

/*user messages*/
void chameleon_info(t_chameleon *x);
void chameleon_dblclick(t_chameleon *x);
void chameleon_mute(t_chameleon *x, t_floatarg t);
void chameleon_maximum_process(t_chameleon *x, t_floatarg n);
void chameleon_minimum_process(t_chameleon *x, t_floatarg n);
void chameleon_setbuf(t_chameleon *x, t_symbol *wavename);
void attach_buffer(t_chameleon *x);
void chameleon_flatodds(t_chameleon *x);
void chameleon_randodds(t_chameleon *x);
void chameleon_killproc(t_chameleon *x, t_floatarg p);
void chameleon_soloproc(t_chameleon *x, t_floatarg p);
void chameleon_verbose(t_chameleon *x, t_floatarg t);
void chameleon_block_dsp(t_chameleon *x, t_floatarg t);
void chameleon_gozero(t_chameleon *x);
void chameleon_grab(t_chameleon *x);
void chameleon_setodds(t_chameleon *x,t_symbol *msg, short argc, t_atom *argv);
void chameleon_tcycle(t_chameleon *x,t_symbol *msg, short argc, t_atom *argv);
void chameleon_version(t_chameleon *x);
void chameleon_dsp64(t_chameleon *x, t_object *dsp64, short *count,
                     double samplerate, long maxvectorsize, long flags);
void chameleon_store(t_chameleon *x, t_floatarg t);
void chameleon_recall(t_chameleon *x, t_floatarg t);
void chameleon_recall_parameters_exec(t_chameleon *x);
void chameleon_loadslot(t_chameleon *x,t_symbol *msg, short argc, t_atom *argv);
/* CMIX function code */

void killdc( double *inbuf, int in_frames, int channels, t_chameleon *x);
void ringmod(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void ringmod4(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void retrograde(t_chameleon *x, int slot, int *pcount);
void comber(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
// void transpose(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void flange(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void butterme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void sweepreson(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void truncateme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void slidecomb(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void reverb1(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void ellipseme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void feed1me(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void flam1(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void flam2(t_chameleon *x, int slot, int *pcount);
// void expflam(t_chameleon *x, int slot, int *pcount);
void comb4(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void ringfeed(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void resonchameleon(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void stv(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void compdist(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void bendy(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void slideflam(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void bitcrush(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2);
void chameleon_set_parameters_exec(t_chameleon *x);
void chameleon_print_parameters(t_chameleon *x);
void chameleon_report(t_chameleon *x);
void chameleon_perform64(t_chameleon *x, t_object *dsp64, double **ins,
                         long numins, double **outs,long numouts, long vectorsize,
                         long flags, void *userparam);
void chameleon_tweak_parameters(t_chameleon *x, t_floatarg t);
void clip(double *x, double min, double max);

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.chameleon~", (method)chameleon_new, (method)chameleon_dsp_free, sizeof(t_chameleon), 0,A_GIMME, 0);
    
    class_addmethod(c,(method)chameleon_assist,"assist", A_CANT, 0);
    class_addmethod(c,(method)chameleon_flatodds,"flatodds", 0);
    class_addmethod(c,(method)chameleon_randodds,"randodds", 0);
    class_addmethod(c,(method)chameleon_soloproc,"soloproc", A_FLOAT, 0);
    class_addmethod(c,(method)chameleon_killproc,"killproc", A_FLOAT, 0);
    class_addmethod(c,(method)chameleon_store,"store", A_FLOAT, 0);
    class_addmethod(c,(method)chameleon_recall,"recall", A_FLOAT, 0);
    class_addmethod(c,(method)chameleon_setodds,"setodds", A_GIMME, 0);
    class_addmethod(c,(method)chameleon_loadslot,"loadslot", A_GIMME, 0);
    class_addmethod(c,(method)chameleon_set_parameters,"set_parameters", 0);
    class_addmethod(c,(method)chameleon_tweak_parameters,"tweak_parameters", A_FLOAT, 0);
    class_addmethod(c,(method)chameleon_maximum_process,"maximum_process", A_FLOAT, 0);
    class_addmethod(c,(method)chameleon_minimum_process,"minimum_process", A_FLOAT, 0);
    class_addmethod(c,(method)chameleon_print_parameters,"print_parameters", 0);
    class_addmethod(c,(method)chameleon_report,"report", 0);
    class_addmethod(c,(method)chameleon_dsp64, "dsp64", A_CANT,0);
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    chameleon_class = c;
    potpourri_announce(OBJECT_NAME);
    return 0;
}

void clip(double *x, double min, double max){
    if(*x < min){
        *x = min;
    } else if (*x > max){
        *x = max;
    }
}

void chameleon_print_parameters(t_chameleon *x){
    int i;
    post("loadslot 9999 %d",x->pcount);
    for(i = 0; i < x->pcount; i++){
        post("%f",x->params[i]);
    }
}

void chameleon_report(t_chameleon *x){
    void *listo = x->listo;
    t_atom *data = x->data;
    t_symbol *loadslot = gensym("loadslot");
    t_symbol *comma = gensym(",");
    t_slot *slots = x->slots;
    int datacount = 0;
    // int pattern_count = 0;
    int i,j;
    
    for(i = 0; i < MAX_SLOTS; i++){
        if( slots[i].pcount > 0 ){
            atom_setsym(&data[datacount++], loadslot);
            atom_setfloat(&data[datacount++], (float)i);
            atom_setfloat(&data[datacount++], slots[i].pcount);
            for(j = 0; j < slots[i].pcount; j++){
                atom_setfloat(&data[datacount++], slots[i].params[j]);
            }
            atom_setsym(&data[datacount++], comma);
            // pattern_count++;
        }
    }
    outlet_list(listo, 0L, datacount, data);
    // post("reported %d chameleon patterns", pattern_count);
}

void chameleon_maximum_process(t_chameleon *x, t_floatarg n)
{
    if(n < 0){
        error("illegal val to maximum_process");
        return;
    }
    x->max_process_per_note = (int)n;
}

void chameleon_minimum_process(t_chameleon *x, t_floatarg n)
{
    if(n < 0){
        error("illegal val to minimum_process");
        return;
    }
    x->min_process_per_note = (int)n;
}

void chameleon_setodds(t_chameleon *x,t_symbol *msg, short argc, t_atom *argv)
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

void chameleon_soloproc(t_chameleon *x, t_floatarg fp)
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


void chameleon_store(t_chameleon *x, t_floatarg fp)
{
    long slotnum = (long) fp;
    int i;
    if( (slotnum < 0) || (slotnum >= MAX_SLOTS)){
        object_error((t_object *)x, "%d is not a valid slot number", slotnum);
        return;
    }
    // we're good, store the data
    // post("storing %d pieces of data at slot %d", x->pcount, slotnum);
    x->slots[slotnum].pcount = x->pcount;
    for(i = 0; i < x->pcount; i++){
        x->slots[slotnum].params[i] = x->params[i];
    }
}

void chameleon_loadslot(t_chameleon *x,t_symbol *msg, short argc, t_atom *argv)
{
    int i;
    t_atom_long slot, pcount;
    
    atom_arg_getlong(&slot,0,argc, argv);
    atom_arg_getlong(&pcount,1,argc, argv);
    
    post("args are pcount: %d and slot: %d", pcount, slot);
   
    if(argc < pcount + 2){
        object_error((t_object *)x, "wrong number of arguments to loadslot. Should be %d, got %d", pcount, argc);
    }
    for(i = 0; i < pcount; i++){
        atom_arg_getdouble( &x->slots[slot].params[i], 2 + i, argc, argv);
    }
    x->slots[slot].pcount = pcount;
}


void chameleon_recall(t_chameleon *x, t_floatarg fp)
{
    long slotnum = (long) fp;
    if( (slotnum < 0) || (slotnum >= MAX_SLOTS)){
        object_error((t_object *)x, "%d is not a valid slot number", slotnum);
        return;
    }
    // post("preparing to recall slot %d", slotnum);
    x->recall_slot = slotnum;
    x->recall_parameters_flag = 1;
}

void chameleon_killproc(t_chameleon *x, t_floatarg fp)
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

void chameleon_flatodds(t_chameleon *x)
{
    int i;
    for(i=0;i<PROCESS_COUNT;i++){
        x->odds[i] = 1.0;
    }
    setweights(x->odds,PROCESS_COUNT);
}

void chameleon_randodds(t_chameleon *x)
{
    int i;
    for(i=0;i<PROCESS_COUNT;i++){
        x->odds[i] = boundrand(0.0,1.0);
    }
    setweights(x->odds,PROCESS_COUNT);
}

void *chameleon_new(t_symbol *msg, short argc, t_atom *argv)
{
    t_chameleon *x = (t_chameleon *)object_alloc(chameleon_class);
    
    int i,j;
    long membytes = 0;
    srand((unsigned int)time(0));
    
    x->sr = sys_getsr();
    x->vs = sys_getblksize();
    if(! x->sr){
        x->sr = 48000;
        
    }
 //   x->work_buffer_size = DEFAULT_BUFFER_SIZE;
    
    dsp_setup((t_pxobject *)x,2); // added inlet
    x->x_obj.z_misc |= Z_NO_INPLACE; // maybe not needed, or even useful turned off
    x->listo = listout( (void *) x);
    outlet_new((t_pxobject *)x, "signal");
    outlet_new((t_pxobject *)x, "signal");
    
    
    
    x->sinelen = 65536;
    x->verbose = 0;

    
    // KILL THESE
    
//    x->halfbuffer = x->buf_samps / 2;
    
    x->chan1buf = NULL;
    x->chan2buf = NULL;
    
    // OK HERE
    x->maxdelay = 1.0; // in seconds
    x->max_flangedelay = 0.1; // in seconds
    
    // memory allocation
    
    x->data = (t_atom *) sysmem_newptrclear( 1024 * sizeof(t_atom)); // NEW ALLOCATION
    membytes += 1024 * sizeof(t_atom);
    x->sinewave = (double *) sysmem_newptrclear( (x->sinelen + 1) * sizeof(double));
    membytes += (x->sinelen + 1) * sizeof(double);
    x->params = (double *) sysmem_newptrclear(MAX_PARAMETERS * sizeof(double));
    membytes += MAX_PARAMETERS * sizeof(double);
    x->odds = (float *) sysmem_newptrclear(64 * sizeof(float));
    x->distortion_length = 32768;
    x->distortion_function = (double *) sysmem_newptrclear(x->distortion_length * sizeof(double));
    membytes += x->distortion_length * sizeof(double);
    set_distortion_table(x->distortion_function, 0.1, 0.5, x->distortion_length);
    
    
    putsine(x->sinewave, x->sinelen);
    
    x->comb_delay_pool1 = (double **) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(double *));
    x->comb_delay_pool2 = (double **) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(double *));
    membytes += 2 * MAX_DSP_UNITS * sizeof(double *);
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->comb_delay_pool1[i] = (double *) sysmem_newptrclear( ((x->maxdelay * x->sr) + 2) * sizeof(double));
        x->comb_delay_pool2[i] = (double *) sysmem_newptrclear( ((x->maxdelay * x->sr) + 2) * sizeof(double));
        membytes += 2 * ((x->maxdelay * x->sr) + 2) * sizeof(double);
    }
    
    // RINGMOD
    x->ringmod_phases = (double *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(double));
    membytes += MAX_DSP_UNITS * sizeof(double);
    // RINGMOD4
    x->ringmod4_phases = (double *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(double));
    membytes += MAX_DSP_UNITS * sizeof(double);
    
    // FLANGE
    
    x->flange_units = (t_flange_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_flange_unit));
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->flange_units[i].flange_dl1 = (double *) sysmem_newptrclear( ((x->maxdelay * x->sr) + 2) * sizeof(double));
        membytes += ((x->maxdelay * x->sr) + 2) * sizeof(double);
        x->flange_units[i].flange_dl2 = (double *) sysmem_newptrclear( ((x->maxdelay * x->sr) + 2) * sizeof(double));
        membytes += ((x->maxdelay * x->sr) + 2) * sizeof(double);
        x->flange_units[i].dv1 = (int *) sysmem_newptrclear(2 * sizeof(int));
        x->flange_units[i].dv2 = (int *) sysmem_newptrclear(2 * sizeof(int));
        membytes += 4 * sizeof(int);
        x->flange_units[i].phase = 0.0;
        delset2(x->flange_units[i].flange_dl1, x->flange_units[i].dv1, x->max_flangedelay,x->sr);
        delset2(x->flange_units[i].flange_dl2, x->flange_units[i].dv2, x->max_flangedelay,x->sr);
    }
    
    // BUTTERWORTH
    
    x->butterworth_units = (t_butterworth_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_butterworth_unit));
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->butterworth_units[i].data1 = (double *) sysmem_newptrclear(8 * sizeof(double));
        x->butterworth_units[i].data2 = (double *) sysmem_newptrclear(8 * sizeof(double));
        membytes += 16 * sizeof(double);
    }
    
    // TRUNCATE
    
    x->truncate_units = (t_truncate_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_truncate_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_truncate_unit);
    
    // SWEEPRESON
    
    x->sweepreson_units = (t_sweepreson_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_sweepreson_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_sweepreson_unit);
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->sweepreson_units[i].q1 = (double *) sysmem_newptrclear(5 * sizeof(double));
        x->sweepreson_units[i].q2 = (double *) sysmem_newptrclear(5 * sizeof(double));
        membytes += 10 * sizeof(double);
    }
    
    // SLIDECOMB
    
    x->slidecomb_units = (t_slidecomb_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_slidecomb_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_slidecomb_unit);
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->slidecomb_units[i].dv1 = (int *) sysmem_newptrclear(2 * sizeof(int));
        x->slidecomb_units[i].dv2 = (int *) sysmem_newptrclear(2 * sizeof(int));
        membytes += 4 * sizeof(int);
        x->slidecomb_units[i].delayline1 = (double *) sysmem_newptrclear( (2 + (MAX_SLIDECOMB_DELAY * x->sr)) * sizeof(double));
        x->slidecomb_units[i].delayline2 = (double *) sysmem_newptrclear( (2 + (MAX_SLIDECOMB_DELAY * x->sr)) * sizeof(double));
        membytes += 2 * (2 + (MAX_SLIDECOMB_DELAY * x->sr)) * sizeof(double);
        delset2(x->slidecomb_units[i].delayline1, x->slidecomb_units[i].dv1, MAX_SLIDECOMB_DELAY, x->sr);
        delset2(x->slidecomb_units[i].delayline2, x->slidecomb_units[i].dv2, MAX_SLIDECOMB_DELAY, x->sr);
    }
    
    // REVERB1
    
    x->reverb1_units = (t_reverb1_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_reverb1_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_reverb1_unit);
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->reverb1_units[i].dels = (double *) sysmem_newptrclear(4 * sizeof(double));
        x->reverb1_units[i].eel1 = (LSTRUCT *) sysmem_newptrclear(MAXSECTS * sizeof(LSTRUCT));
        x->reverb1_units[i].eel2 = (LSTRUCT *) sysmem_newptrclear(MAXSECTS * sizeof(LSTRUCT));
        x->reverb1_units[i].alpo1 = (double **) sysmem_newptrclear(4 * sizeof(double *));
        membytes += 2 * MAXSECTS * sizeof(LSTRUCT);
        membytes += 8 * sizeof(double *);
        for(j = 0; j < 4 ; j++ ){
            x->reverb1_units[i].alpo1[j] = (double *) sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 1)  * sizeof(double));
            membytes += ((int)(x->sr * MAX_MINI_DELAY) + 1)  * sizeof(double);
        }
        x->reverb1_units[i].alpo2 = (double **) sysmem_newptrclear(4 * sizeof(double *));
        for(j = 0; j < 4 ; j++ ){
            x->reverb1_units[i].alpo2[j] = (double *) sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 1)  * sizeof(double));
            membytes += ((int)(x->sr * MAX_MINI_DELAY) + 1)  * sizeof(double);
        }
    }
    x->reverb_ellipse_data = (double *) sysmem_newptrclear(16 * sizeof(double));
    membytes += 16 * sizeof(double);
    
    // ELLIPSEME
    
    x->ellipseme_units = (t_ellipseme_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_ellipseme_unit));
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->ellipseme_units[i].eel1 = (LSTRUCT *) sysmem_newptrclear(MAXSECTS * sizeof(LSTRUCT));
        x->ellipseme_units[i].eel2 = (LSTRUCT *) sysmem_newptrclear(MAXSECTS * sizeof(LSTRUCT));
        membytes += 2 * MAXSECTS * sizeof(LSTRUCT);
    }
    
    x->ellipse_data = (double **) sysmem_newptr(MAXFILTER * sizeof(double *));
    membytes += MAXFILTER * sizeof(double *);
    for(i=0;i<MAXFILTER;i++){
        x->ellipse_data[i] = (double *) sysmem_newptrclear(MAX_COEF * sizeof(double));
        membytes += MAX_COEF * sizeof(double);
    }
    
    // FEED1
    
    x->feed1_units = (t_feed1_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_feed1_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_feed1_unit);
    x->feedfunclen = 8192;
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->feed1_units[i].func1 = (double *)sysmem_newptrclear(2 * FEEDFUNCLEN * sizeof(double)); // doubling size, just in case
        x->feed1_units[i].func2 = (double *)sysmem_newptrclear(2 * FEEDFUNCLEN * sizeof(double));
        x->feed1_units[i].func3 = (double *)sysmem_newptrclear(2 * FEEDFUNCLEN * sizeof(double));
        x->feed1_units[i].func4 = (double *)sysmem_newptrclear(2 * FEEDFUNCLEN * sizeof(double));
        membytes += 8 * FEEDFUNCLEN * sizeof(double);
        x->feed1_units[i].delayLine1a = (double *) sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 5)  * sizeof(double));
        x->feed1_units[i].delayLine2a = (double *) sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 5)  * sizeof(double));
        x->feed1_units[i].delayLine1b = (double *) sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 5)  * sizeof(double));
        x->feed1_units[i].delayLine2b = (double *) sysmem_newptrclear(((int)(x->sr * MAX_MINI_DELAY) + 5)  * sizeof(double));
        membytes += 4 * ((int)(x->sr * MAX_MINI_DELAY) + 5)  * sizeof(double);
        x->feed1_units[i].dv1a = (int *) sysmem_newptrclear(2 * sizeof(int));
        x->feed1_units[i].dv2a = (int *) sysmem_newptrclear(2 * sizeof(int));
        x->feed1_units[i].dv1b = (int *) sysmem_newptrclear(2 * sizeof(int));
        x->feed1_units[i].dv2b = (int *) sysmem_newptrclear(2 * sizeof(int));
        membytes += 8 * sizeof(int);
    }
    
    // BITCRUSH
    
    x->bitcrush_factors = (double *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(double));
    membytes += MAX_DSP_UNITS * sizeof(double);
    
    // FLAM1
    
    x->flam1_units = (t_flam1_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_flam1_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_flam1_unit);
    
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->flam1_units[i].delayline1 = (double *) sysmem_newptrclear(((int)(x->sr * FLAM1_MAX_DELAY) + 5)  * sizeof(double));
        x->flam1_units[i].delayline2 = (double *) sysmem_newptrclear(((int)(x->sr * FLAM1_MAX_DELAY) + 5)  * sizeof(double));
        membytes += 2 * ((int)(x->sr * FLAM1_MAX_DELAY) + 5)  * sizeof(double);
        x->flam1_units[i].dv1 = (int *) sysmem_newptrclear(2 * sizeof(int));
        x->flam1_units[i].dv2 = (int *) sysmem_newptrclear(2 * sizeof(int));
        membytes += 4 * sizeof(int);
    }
    
    // COMB4
    
    x->comb4_units = (t_comb4_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_comb4_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_comb4_unit);
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->comb4_units[i].combs1 = (double **)sysmem_newptrclear(4 * sizeof(double *));
        x->comb4_units[i].combs2 = (double **)sysmem_newptrclear(4 * sizeof(double *));
        membytes += 8 * sizeof(double *);
        for(j = 0; j < 4; j++){
            x->comb4_units[i].combs1[j] = (double *)sysmem_newptrclear( (5 + TONECOMB_MAX_DELAY * x->sr) * sizeof(double));
            x->comb4_units[i].combs2[j] = (double *)sysmem_newptrclear( (5 + TONECOMB_MAX_DELAY * x->sr) * sizeof(double));
            membytes += 2 * (5 + TONECOMB_MAX_DELAY * x->sr) * sizeof(double);
        }
    }
    
    // RESONFEED
    
    x->resonfeed_units = (t_resonfeed_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_resonfeed_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_resonfeed_unit);
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->resonfeed_units[i].res1q = (double *)sysmem_newptrclear(5 * sizeof(double));
        x->resonfeed_units[i].res2q = (double *)sysmem_newptrclear(5 * sizeof(double));
        membytes += 10 * sizeof(double);
        x->resonfeed_units[i].comb1arr = (double *)sysmem_newptrclear(TONECOMB_MAX_DELAY * x->sr * sizeof(double));
        x->resonfeed_units[i].comb2arr = (double *)sysmem_newptrclear(TONECOMB_MAX_DELAY * x->sr * sizeof(double));
        membytes += 2 * TONECOMB_MAX_DELAY * x->sr * sizeof(double);
    }
    
    // RESONADSR
    
    x->resonadsr_units = (t_resonadsr_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_resonadsr_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_resonadsr_unit);
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->resonadsr_units[i].q1 = (double *)sysmem_newptrclear(5 * sizeof(double));
        x->resonadsr_units[i].q2 = (double *)sysmem_newptrclear(5 * sizeof(double));
        membytes += 10 * sizeof(double);
        x->resonadsr_units[i].adsr = (CMIXADSR *)sysmem_newptrclear(sizeof(CMIXADSR));
        membytes += sizeof(CMIXADSR);
        x->resonadsr_units[i].adsr->func = (double *)sysmem_newptrclear(8192 * sizeof(double));
        membytes += 8192 * sizeof(double);
        x->resonadsr_units[i].adsr->len = 8192;
    }
    
    // STV
    
    x->stv_units = (t_stv_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_stv_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_stv_unit);
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->stv_units[i].delayline1 = (double *) sysmem_newptrclear(((int)(x->sr * STV_MAX_DELAY) + 5)  * sizeof(double));
        x->stv_units[i].delayline2 = (double *) sysmem_newptrclear(((int)(x->sr * STV_MAX_DELAY) + 5)  * sizeof(double));
        membytes += 2 * ((int)(x->sr * STV_MAX_DELAY) + 5)  * sizeof(double);
        x->stv_units[i].dv1 = (int *) sysmem_newptrclear(2 * sizeof(int));
        x->stv_units[i].dv2 = (int *) sysmem_newptrclear(2 * sizeof(int));
        membytes += 4 * sizeof(int);
    }
    
    // BENDY
    
    x->bendy_units = (t_bendy_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_bendy_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_bendy_unit);
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->bendy_units[i].delayline1 = (double *) sysmem_newptrclear(((int)(x->sr * BENDY_MAXDEL) + 5)  * sizeof(double));
        x->bendy_units[i].delayline2 = (double *) sysmem_newptrclear(((int)(x->sr * BENDY_MAXDEL) + 5)  * sizeof(double));
        membytes += 2 * ((int)(x->sr * BENDY_MAXDEL) + 5)  * sizeof(double);
        x->bendy_units[i].dv1 = (int *) sysmem_newptrclear(8 * sizeof(int));
        x->bendy_units[i].dv2 = (int *) sysmem_newptrclear(8 * sizeof(int));
        membytes += 4 * sizeof(int);
        x->bendy_units[i].val1 = boundrand(0.01, BENDY_MAXDEL);
        x->bendy_units[i].val2 = boundrand(0.01, BENDY_MAXDEL);
        x->bendy_units[i].counter = 0;
    }
    
    // SLIDEFLAM
    
    x->slideflam_units = (t_slideflam_unit *) sysmem_newptrclear(MAX_DSP_UNITS * sizeof(t_slideflam_unit));
    membytes += MAX_DSP_UNITS * sizeof(t_slideflam_unit);
    for(i = 0; i < MAX_DSP_UNITS; i++){
        x->slideflam_units[i].dv1 = (int *) sysmem_newptrclear(2 * sizeof(int));
        x->slideflam_units[i].dv2 = (int *) sysmem_newptrclear(2 * sizeof(int));
        membytes += 4 * sizeof(int);
        x->slideflam_units[i].delayline1 = (double *) sysmem_newptrclear( (10 + (MAX_SLIDEFLAM_DELAY * x->sr)) * sizeof(double));
        x->slideflam_units[i].delayline2 = (double *) sysmem_newptrclear( (10 + (MAX_SLIDEFLAM_DELAY * x->sr)) * sizeof(double));
        membytes += 2 * (10 + (MAX_SLIDEFLAM_DELAY * x->sr)) * sizeof(double);
        delset2(x->slideflam_units[i].delayline1, x->slideflam_units[i].dv1, MAX_SLIDEFLAM_DELAY, x->sr);
        delset2(x->slideflam_units[i].delayline2, x->slideflam_units[i].dv2, MAX_SLIDEFLAM_DELAY, x->sr);
    }
    
    x->tf_len = 1;
    x->tf_len <<= 16;
    
    setflamfunc1(x->flamfunc1,x->flamfunc1len);
    x->max_comb_lpt = 0.15 ;// watch out here
    
    x->adsr = (CMIXADSR *) sysmem_newptr(1 * sizeof(CMIXADSR));
    membytes += sizeof(CMIXADSR);
    x->adsr->len = 32768;
    x->adsr->func = (double *) sysmem_newptr(x->adsr->len * sizeof(double) );
    membytes += x->adsr->len * sizeof(double);
    x->dcflt = (double *) sysmem_newptrclear(16 * sizeof(double));
    membytes += 16 * sizeof(double);
    
    
    // allocate memory to store random patterns
    x->slots = (t_slot *) sysmem_newptrclear( MAX_SLOTS * sizeof(t_slot) );
    membytes += MAX_SLOTS * sizeof(t_slot);
    for(i = 0; i < MAX_SLOTS; i++){
        x->slots[i].params = (double *) sysmem_newptrclear( MAX_PARAMETERS * sizeof(double));
        membytes += MAX_PARAMETERS * sizeof(double);
    }
        
    /* be sure to finish clearing memory */
    set_dcflt(x->dcflt); // WE NEED THIS FILTER!
    init_reverb_data(x->reverb_ellipse_data);
    init_ellipse_data(x->ellipse_data);
    
    for(i=0;i<PROCESS_COUNT;i++){
        x->odds[i] = 1;
    }
    x->min_process_per_note = 1;
    x->max_process_per_note = 1;
    setweights(x->odds,PROCESS_COUNT);
    
    x->ratios = (double *) sysmem_newptrclear(5 * sizeof(double));
    membytes += 5 * sizeof(double);
    x->ratios[0] = 9.0/8.0;
    x->ratios[1] = 4.0/3.0;
    x->ratios[2] = 5.0/4.0;
    x->ratios[3] = 6.0/5.0;
    x->ratios[4] = 3.0/2.0;
    
    post("total memory for this chameleon %.2f MBytes",(float)membytes/1000000.);
    
    chameleon_set_parameters(x);
    return x;
}

void chameleon_dsp_free(t_chameleon *x)
{
    int i;
    dsp_free((t_pxobject *)x);
    sysmem_freeptr(x->sinewave);
    sysmem_freeptr(x->params);
    sysmem_freeptr(x->odds);
    for(i= 0; i < MAX_DSP_UNITS; i++){
        sysmem_freeptr( x->comb_delay_pool1[i] );
        sysmem_freeptr( x->comb_delay_pool2[i] );
    }
    sysmem_freeptr( x->comb_delay_pool1);
    sysmem_freeptr( x->comb_delay_pool2);
    sysmem_freeptr(x->eel);
    for( i = 0; i < 4 ; i++ ){
        sysmem_freeptr(x->mini_delay[i]);
    }
    sysmem_freeptr(x->reverb_ellipse_data);
    for(i=0;i<MAXFILTER;i++){
        sysmem_freeptr(x->ellipse_data[i] );
    }
    
    // free bendy
    for(i = 0; i < MAX_DSP_UNITS; i++){
        sysmem_freeptr(x->bendy_units[i].delayline1);
        sysmem_freeptr(x->bendy_units[i].delayline2);
        sysmem_freeptr(x->bendy_units[i].dv1);
        sysmem_freeptr(x->bendy_units[i].dv2);
    }
    sysmem_freeptr(x->bendy_units);
    
    // free slidecomb
    
    for(i = 0; i < MAX_DSP_UNITS; i++){
        sysmem_freeptr(x->slideflam_units[i].delayline1);
        sysmem_freeptr(x->slideflam_units[i].delayline2);
        sysmem_freeptr(x->slideflam_units[i].dv1);
        sysmem_freeptr(x->slideflam_units[i].dv2);
    }
    sysmem_freeptr(x->slideflam_units);
    
    // remaining memory to clear
    sysmem_freeptr(x->transfer_function);
    sysmem_freeptr(x->feedfunc1);
    sysmem_freeptr(x->feedfunc2);
    sysmem_freeptr(x->feedfunc3);
    sysmem_freeptr(x->feedfunc4);
    sysmem_freeptr(x->flamfunc1);
    sysmem_freeptr(x->adsr->func);
    sysmem_freeptr(x->adsr);
    sysmem_freeptr(x->dcflt);
    sysmem_freeptr(x->ringmod_phases);
    sysmem_freeptr(x->ringmod4_phases);
    sysmem_freeptr(x->bitcrush_factors);
    
    // free slot memory
    for(i = 0; i < MAX_SLOTS; i++){
        sysmem_freeptr(x->slots[i].params);
    }
    sysmem_freeptr(x->slots);
}	



void chameleon_perform64(t_chameleon *x, t_object *dsp64, double **ins,
                         long numins, double **outs,long numouts, long n,
                         long flags, void *userparam)
{
    t_double *OGchan1 = ins[0];
    t_double *OGchan2 = ins[1];
    t_double *outchanL = outs[0];
    t_double *outchanR = outs[1];
    long i;
    long pcount;
    double *params;
    long curarg;
    t_double *chan1, *chan2;
    if( x->set_parameters_flag == 1){
        chameleon_set_parameters_exec(x);
    }
    else if(x->recall_parameters_flag == 1)  {
        chameleon_recall_parameters_exec(x);
    }
    
    /* NOTE: operating directly on the input vectors resulted in those vectors (dry signal) being replaced by
     processed signal. We need to copy to local arrays to avoid signal contamination. */
    
    if( x->chan1buf == NULL){
        // post("allocated vector inside perform routine");
        x->chan1buf = (t_double *) sysmem_newptr(sizeof(t_double) * n);
        x->chan2buf = (t_double *) sysmem_newptr(sizeof(t_double) * n);
    }
    chan1 = x->chan1buf;
    chan2 = x->chan2buf;
    
    
    
    /* copy input buffers to local copies (look for sysmem alternative for this copy operation */
    for(i = 0; i < n; i++){
        chan1[i] = OGchan1[i];
        chan2[i] = OGchan2[i];
    }
    
    pcount = x->pcount;
    params = x->params;
    curarg = 0;
    while(curarg < pcount){
        if(params[curarg] == BENDY){
            bendy(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == RINGMOD){
            ringmod(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == BITCRUSH){
            bitcrush(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == COMB){
            comber(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == FLANGE){
            flange(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == BUTTER){
            butterme(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == TRUNCATE){
            truncateme(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == SWEEPRESON){
            sweepreson(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == SLIDECOMB){
            slidecomb(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == REVERB1){
            reverb1(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == ELLIPSE){
            ellipseme(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == FEED1){
            feed1me(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == FLAM1){
            flam1(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == SLIDEFLAM){
            slideflam(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == RINGMOD4){
            ringmod4(x,&curarg,chan1,chan2);
        }
        else if(params[curarg] == COMB4){
            comb4(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == COMPDIST){
            compdist(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == RINGFEED){
            ringfeed(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == RESONADSR){
            resonadsr(x, &curarg, chan1, chan2);
        }
        else if(params[curarg] == STV){
            stv(x, &curarg, chan1, chan2);
        }
        else {
            error("deploy missing branch for %d", curarg);
            goto panic;
        }
    }
    // HERE GOES THE OUTPUT:
    for(i = 0; i < n; i++){
        outchanL[i] = chan1[i];
        outchanR[i] = chan2[i];
    }
panic:
    ;
}

void chameleon_set_parameters(t_chameleon *x){
    x->set_parameters_flag = 1;
}

void chameleon_tweak_parameters(t_chameleon *x, t_floatarg tdev){
    int i, j;
    int ftype;
    double cf, bw;//, bw;
    double delay, revtime;
    double *params = x->params;
    long pcount = x->pcount;
    int comb_dl_count = 0; // where in the comb pool to grab memory
    int flange_count = 0;
    int truncate_count = 0;
    int butterworth_count = 0;
    int sweepreson_count = 0;
    int slidecomb_count = 0;
    int reverb1_count = 0;
    int ellipseme_count = 0;
    int feed1_count = 0;
    int flam1_count = 0;
    int comb4_count = 0;
    int ringfeed_count = 0;
    int bendy_count = 0;
    int ringmod_count = 0;
    int ringmod4_count = 0;
    int slideflam_count = 0;
    int bitcrush_count = 0;
    int resonadsr_count = 0;
    int stv_count = 0;
    double raw_wet;
    double sr = x->sr;
    double *dels;
    double **alpo1, **alpo2;
    double speed1, speed2, mindelay, maxdelay, duration;
    double basefreq;
    double rvt = boundrand(0.1,0.98);
    double lpt;
    double notedur, sust;
    double phz1, phz2, minfeedback = 0.1, maxfeedback = 0.7;
    long slotnum = x->recall_slot;
    double *rparams; // parameter set to recall
    long rpcount; // number of parameters to read
    long slot_pcount;
    double multmin, multmax, tmp;
    
    clip(&tdev, 0.001, 0.5);
    multmin = 1.0 - tdev;
    multmax = 1.0 + tdev;
    /* Now iterate through the recall, but with no setting of memory */
    slot_pcount = x->slots[slotnum].pcount;
    
    if( slot_pcount <= 0){
        //    post("Aborting reload of slot %d", slotnum);
        return;
    }
    rparams = x->slots[slotnum].params;
    pcount = 0;
    rpcount = 0;

    while( rpcount < slot_pcount ){
        j = rparams[rpcount++];
        if(j == COMB){
            params[pcount++] = COMB;
            delay = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&delay, 0.01, 0.35);
            params[pcount++] = delay;
            revtime = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&revtime,0.5,0.98);
            params[pcount++] = revtime;
            params[pcount++] = comb_dl_count = rparams[rpcount++]; // Possible Bug Here???
            mycombset(delay,revtime,0,x->comb_delay_pool1[comb_dl_count],x->sr);
            mycombset(delay,revtime,0,x->comb_delay_pool2[comb_dl_count],x->sr);
        }
        else if(j == RINGMOD) {
            // post("Added RINGMOD unit");
            params[pcount++] = RINGMOD;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 40.0, 3000.0 );
            params[pcount++] = tmp;
            params[pcount++] = ringmod_count = rparams[rpcount++];
        }
        else if(j == RINGMOD4) {
            // post("Added RINGMOD4 unit");
            params[pcount++] = RINGMOD4;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 40.0, 3000.0 );
            params[pcount++] = tmp;
            params[pcount++] = ringmod4_count = rparams[rpcount++];
        }
        else if(j == BENDY){
            params[pcount++] = BENDY;
            params[pcount++] = bendy_count = rparams[rpcount++];
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.01, BENDY_MAXDEL);
            params[pcount++] = x->bendy_units[bendy_count].val1 = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.01, BENDY_MAXDEL);
            params[pcount++] = x->bendy_units[bendy_count].val2 = tmp;
            // danger here
            x->bendy_units[bendy_count].counter = 0;
            delset2(x->bendy_units[bendy_count].delayline1, x->bendy_units[bendy_count].dv1, x->bendy_units[bendy_count].val1,x->sr);
            delset2(x->bendy_units[bendy_count].delayline2, x->bendy_units[bendy_count].dv2, x->bendy_units[bendy_count].val2,x->sr);
        }
        else if(j == FLANGE){
            params[pcount++] = FLANGE;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 80.0, 400.0);
            params[pcount++] = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 600.0, 4000.0);
            params[pcount++] = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.1, 2.0);
            params[pcount++] = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.1, 0.95);
            params[pcount++] = tmp;
            params[pcount++] = flange_count = rparams[rpcount++];
        }
        else if(j == BUTTER){
            /*
             params[pcount++] = cf = boundrand(70.0,3000.0);
             params[pcount++] = bw = cf * boundrand(0.05,0.6);
             */
            params[pcount++] = BUTTER;
            params[pcount++] = ftype = rparams[rpcount++];
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 70.0, 3000.0);
            params[pcount++] = cf = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.05, 0.6);
            params[pcount++] = bw = tmp;
            params[pcount++] = butterworth_count = rparams[rpcount++];
            if( ftype == LOPASS) {
                lobut(x->butterworth_units[butterworth_count].data1, cf, sr);
                lobut(x->butterworth_units[butterworth_count].data2, cf, sr);
            } else if (ftype == HIPASS){
                hibut(x->butterworth_units[butterworth_count].data1, cf, sr);
                hibut(x->butterworth_units[butterworth_count].data2, cf, sr);
            }
            else if(ftype == BANDPASS){
                bpbut(x->butterworth_units[butterworth_count].data1, cf, bw, sr);
                bpbut(x->butterworth_units[butterworth_count].data2, cf, bw, sr);
                x->butterworth_units[butterworth_count].bw = bw;
            }
            x->butterworth_units[butterworth_count].cf = cf;
            x->butterworth_units[butterworth_count].ftype = ftype;
        }
        else if(j == TRUNCATE){
            // nothing to change here
            params[pcount++] = TRUNCATE;
            params[pcount++] = truncate_count = rparams[rpcount++];
            /*
            x->truncate_units[truncate_count].counter = 0;
            x->truncate_units[truncate_count].state = 0;
            x->truncate_units[truncate_count].segsamples = 1;
            */
        }
        else if(j == SWEEPRESON){
            params[pcount++] = SWEEPRESON;
            params[pcount++] = sweepreson_count = rparams[rpcount++];
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 100.0, 300.0);
            params[pcount++] = x->sweepreson_units[sweepreson_count].minfreq = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 600.0, 6000.0);
            params[pcount++] = x->sweepreson_units[sweepreson_count].maxfreq = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.01, 0.2);
            params[pcount++] = x->sweepreson_units[sweepreson_count].bwfac = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.05, 2.0);
            params[pcount++] = x->sweepreson_units[sweepreson_count].speed = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.0, 0.5);
            params[pcount++] = x->sweepreson_units[sweepreson_count].phase = tmp;
            x->sweepreson_units[sweepreson_count].q1[3] = 0;
            x->sweepreson_units[sweepreson_count].q1[4] = 0;
            x->sweepreson_units[sweepreson_count].q2[3] = 0;
            x->sweepreson_units[sweepreson_count].q2[4] = 0;
        }
        else if(j == SLIDECOMB){
            params[pcount++] = SLIDECOMB;
            params[pcount++] = slidecomb_count = rparams[rpcount++];
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.001,MAX_SLIDECOMB_DELAY * 0.95);
            params[pcount++] = x->slidecomb_units[slidecomb_count].start_delay = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.001,MAX_SLIDECOMB_DELAY * 0.95);
            params[pcount++] = x->slidecomb_units[slidecomb_count].end_delay = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.7,0.99);
            params[pcount++] = x->slidecomb_units[slidecomb_count].feedback = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.1,2.0);
            params[pcount++] = x->slidecomb_units[slidecomb_count].sample_length = tmp;
            // scale length to SR
            x->slidecomb_units[slidecomb_count].sample_length *= x->sr;
            x->slidecomb_units[slidecomb_count].counter = 0;
            delset2(x->slidecomb_units[slidecomb_count].delayline1,x->slidecomb_units[slidecomb_count].dv1,MAX_SLIDECOMB_DELAY, x->sr);
            delset2(x->slidecomb_units[slidecomb_count].delayline2,x->slidecomb_units[slidecomb_count].dv2,MAX_SLIDECOMB_DELAY, x->sr);
        }
        else if(j == REVERB1){
            params[pcount++] = REVERB1;
            params[pcount++] = reverb1_count = rparams[rpcount++];
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.25,0.99);
            params[pcount++] = revtime = x->reverb1_units[reverb1_count].revtime = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.2,0.8);
            params[pcount++] = raw_wet = tmp;
            x->reverb1_units[reverb1_count].wet = sin(1.570796 * raw_wet);
            x->reverb1_units[reverb1_count].dry = cos(1.570796 * raw_wet);
            dels = x->reverb1_units[reverb1_count].dels;
            for(i = 0; i < 4; i++){
                tmp = rparams[rpcount++] * boundrand(multmin, multmax);
                clip(&tmp, 0.005, 0.1);
                params[pcount++] = dels[i] = tmp;
            }
            alpo1 = x->reverb1_units[reverb1_count].alpo1;
            alpo2 = x->reverb1_units[reverb1_count].alpo2;
            for( i = 0; i < 4; i++ ){
                if(dels[i] < .005 || dels[i] > 0.1) {
                    post("reverb1: bad random delay time: %f",dels[i]);
                    dels[i] = .05;
                }
                // could be dangerous!
                mycombset(dels[i], revtime, 0, alpo1[i], x->sr);
                mycombset(dels[i], revtime, 0, alpo2[i], x->sr);
            }
            /*
            ellipset(x->reverb_ellipse_data,x->reverb1_units[reverb1_count].eel1,&x->reverb1_units[reverb1_count].nsects,&x->reverb1_units[reverb1_count].xnorm);
            ellipset(x->reverb_ellipse_data,x->reverb1_units[reverb1_count].eel2,&x->reverb1_units[reverb1_count].nsects,&x->reverb1_units[reverb1_count].xnorm);
            */
            //  reverb1_count = (reverb1_count + 1) % max_dsp_units;
        }
        else if(j == ELLIPSE){
           // nothing to change here
            params[pcount++] = ELLIPSE;
            params[pcount++] = ellipseme_count = rparams[rpcount++];
            params[pcount++] = x->ellipseme_units[ellipseme_count].filtercode = rparams[rpcount++];
            
            if( x->ellipseme_units[ellipseme_count].filtercode >= ELLIPSE_FILTER_COUNT ){
                error("there is no %d ellipse data",x->ellipseme_units[ellipseme_count].filtercode);
                return;
            };
            /*
            fltdata = x->ellipse_data [x->ellipseme_units[ellipseme_count].filtercode];
            eel1 = x->ellipseme_units[ellipseme_count].eel1;
            eel2 = x->ellipseme_units[ellipseme_count].eel2;
            ellipset(fltdata,eel1,&nsects,&xnorm);
            ellipset(fltdata,eel2,&nsects,&xnorm);
            x->ellipseme_units[ellipseme_count].nsects = nsects;
            x->ellipseme_units[ellipseme_count].xnorm = xnorm;
            */
            // ellipseme_count = (ellipseme_count + 1) % max_dsp_units;
        }
        else if(j == FEED1){
            params[pcount++] = FEED1;
            params[pcount++] = feed1_count = rparams[rpcount++];
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.001, 0.1);
            params[pcount++] = mindelay = x->feed1_units[feed1_count].mindelay = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, mindelay, 0.1);
            params[pcount++] = maxdelay = x->feed1_units[feed1_count].maxdelay = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.01, 0.5);
            params[pcount++] = speed1 = x->feed1_units[feed1_count].speed1 = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, speed1, 0.5);
            params[pcount++] = speed2 = x->feed1_units[feed1_count].speed2 = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.05, 1.0);
            params[pcount++] = duration = x->feed1_units[feed1_count].duration = tmp;
            
            funcgen1(x->feed1_units[feed1_count].func1, FEEDFUNCLEN,duration,
                     mindelay,maxdelay, speed1, speed2, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            phz1 /= (double) FEEDFUNCLEN; phz2 /= (double) FEEDFUNCLEN;
            funcgen1(x->feed1_units[feed1_count].func2, FEEDFUNCLEN,duration,
                     mindelay * 0.5,maxdelay * 2.0, speed1 * 1.25, speed2 * 0.75, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            phz1 /= (double) FEEDFUNCLEN; phz2 /= (double) FEEDFUNCLEN;
            funcgen1(x->feed1_units[feed1_count].func3, FEEDFUNCLEN,duration,
                     minfeedback, maxfeedback, speed1*.35, speed2*1.25, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            phz1 /= (double) FEEDFUNCLEN; phz2 /= (double) FEEDFUNCLEN;
            funcgen1(x->feed1_units[feed1_count].func4, FEEDFUNCLEN,duration,
                     minfeedback, maxfeedback, speed1*.55, speed2*2.25, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            /*
            delset2(x->feed1_units[feed1_count].delayLine1a, x->feed1_units[feed1_count].dv1a, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine2a, x->feed1_units[feed1_count].dv2a, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine1b, x->feed1_units[feed1_count].dv1b, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine2b, x->feed1_units[feed1_count].dv2b, MAX_MINI_DELAY, x->sr);
            */
            // feed1_count = (feed1_count + 1) % max_dsp_units;
            
        }
        else if(j == BITCRUSH){
            params[pcount++] = BITCRUSH;
            params[pcount++] = bitcrush_count = rparams[rpcount++];
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 2.0, 8.0);
            params[pcount++] = x->bitcrush_factors[bitcrush_count] = tmp;
            // bitcrush_count = (bitcrush_count + 1) % max_dsp_units;
        }
        else if(j == FLAM1){
            params[pcount++] = FLAM1;
            params[pcount++] = flam1_count = rparams[rpcount++];
            tmp = rparams[rpcount++] * boundrand(multmin, multmax); // not yet scaled to SR
            clip(&tmp, 1.0, 4.0);
            params[pcount++] = x->flam1_units[flam1_count].sample_length = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.08, 0.2);
            params[pcount++] = x->flam1_units[flam1_count].dt = tmp;
            
            x->flam1_units[flam1_count].counter = 0;
            // scale to SR
            x->flam1_units[flam1_count].sample_length *= x->sr;
            delset2(x->flam1_units[flam1_count].delayline1, x->flam1_units[flam1_count].dv1, MAX_MINI_DELAY, x->sr);
            delset2(x->flam1_units[flam1_count].delayline2, x->flam1_units[flam1_count].dv2, MAX_MINI_DELAY, x->sr);
        }
        else if(j == SLIDEFLAM){
            double sf_del1, sf_del2;
            params[pcount++] = SLIDEFLAM;
            params[pcount++] = slideflam_count = rparams[rpcount++];
            sf_del1 = rparams[rpcount++] * boundrand(multmin, multmax);
            sf_del2 = rparams[rpcount++] * boundrand(multmin, multmax);
            if( sf_del2 > sf_del1 ){
                clip(&sf_del1, 0.01, 0.05);
                clip(&sf_del2, 0.1,MAX_SLIDEFLAM_DELAY * 0.95);
            } else {
                clip(&sf_del2, 0.01, 0.05);
                clip(&sf_del1, 0.1,MAX_SLIDEFLAM_DELAY * 0.95);
            }
            params[pcount++] = x->slideflam_units[slideflam_count].dt1 = sf_del1;
            params[pcount++] = x->slideflam_units[slideflam_count].dt2 = sf_del2;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.7, 0.99);
            params[pcount++] = x->slideflam_units[slideflam_count].feedback = tmp;
            params[pcount++] = x->slideflam_units[slideflam_count].sample_length = rparams[rpcount++] * boundrand(multmin, multmax); // not yet scaled to SR
            
            x->slideflam_units[slideflam_count].counter = 0;
            // scale to SR
            x->slideflam_units[slideflam_count].sample_length *= x->sr;
            delset2(x->slideflam_units[slideflam_count].delayline1,x->slideflam_units[slideflam_count].dv1,MAX_SLIDEFLAM_DELAY, x->sr);
            delset2(x->slideflam_units[slideflam_count].delayline2,x->slideflam_units[slideflam_count].dv2,MAX_SLIDEFLAM_DELAY, x->sr);
        }
        else if(j == COMB4){
            params[pcount++] = COMB4;
            params[pcount++] = comb4_count = rparams[rpcount++];
            rvt = 0.99;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 100.0,400.0);
            params[pcount++] = basefreq = tmp;
            // post("comb4: count: %d, basefreq: %f",comb4_count, basefreq);
            for( i = 0; i < 4; i++ ){
                tmp = rparams[rpcount++] * boundrand(multmin, multmax);
                clip(&tmp, 0.0001,0.05);
                params[pcount++] = lpt = tmp;
                // dangerous ?
                mycombset(lpt, rvt, 0, x->comb4_units[comb4_count].combs1[i], x->sr);
                mycombset(lpt, rvt, 0, x->comb4_units[comb4_count].combs2[i], x->sr);
            }
        }
        else if(j == COMPDIST){
            /* nothing to change */
            params[pcount++] = COMPDIST;
        }
        else if(j == RINGFEED){
            params[pcount++] = RINGFEED;
            params[pcount++] = ringfeed_count = rparams[rpcount++];
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 90.0, 1500.0);
            params[pcount++] = cf = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.02 * cf, 0.4 * cf);
            params[pcount++] = bw = tmp;
            
            rsnset2(cf, bw, RESON_NO_SCL, 0., x->resonfeed_units[ringfeed_count].res1q, x->sr);
            rsnset2(cf, bw, RESON_NO_SCL, 0., x->resonfeed_units[ringfeed_count].res2q, x->sr);
            x->resonfeed_units[ringfeed_count].osc1phs = 0.0;
            x->resonfeed_units[ringfeed_count].osc2phs = 0.0;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 90.0, 1500.0);
            params[pcount++] = x->resonfeed_units[ringfeed_count].osc1si = tmp; // not yet scaled to SR
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 90.0, 1500.0);
            params[pcount++] = x->resonfeed_units[ringfeed_count].osc2si = tmp; // not yet scaled to SR
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 1.0/1500.0, 1.0/90.0);
            params[pcount++] = lpt = tmp;
            tmp = rparams[rpcount++];
            clip(&tmp, 0.01, 0.8);
            params[pcount++] = rvt = tmp;
            // scale to SR
            x->resonfeed_units[ringfeed_count].osc1si *= ((double)x->sinelen / x->sr);
            x->resonfeed_units[ringfeed_count].osc2si *= ((double)x->sinelen / x->sr);
            // dangerous ?
            mycombset(lpt, rvt, 0, x->resonfeed_units[ringfeed_count].comb1arr, x->sr);
            mycombset(lpt, rvt, 0, x->resonfeed_units[ringfeed_count].comb2arr, x->sr);

        }
        else if(j == RESONADSR){
            params[pcount++] = RESONADSR;
            params[pcount++] = resonadsr_count = rparams[rpcount++];
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.01, 0.1);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->a = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.01, 0.05);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->d = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.05, 0.5);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->r = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 150.0, 4000.0);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v1 = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 150.0, 4000.0);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v2 = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 150.0, 4000.0);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v3 = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 150.0, 4000.0);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v4 = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.03,0.7);
            params[pcount++] = x->resonadsr_units[resonadsr_count].bwfac = tmp;
            tmp =  rparams[rpcount++] * boundrand(multmin, multmax);
            params[pcount++] =  notedur = tmp;
            clip(&tmp, 0.7,1.2);
            x->resonadsr_units[resonadsr_count].phs = 0.0;
            sust = notedur - (x->resonadsr_units[resonadsr_count].adsr->a + x->resonadsr_units[resonadsr_count].adsr->d +  x->resonadsr_units[resonadsr_count].adsr->r);
            x->resonadsr_units[resonadsr_count].adsr->s = sust;
            buildadsr(x->resonadsr_units[resonadsr_count].adsr);
            x->resonadsr_units[resonadsr_count].si = ((double)x->resonadsr_units[resonadsr_count].adsr->len / x->sr) / notedur;
            // resonadsr_count = (resonadsr_count + 1) % max_dsp_units;
        }
        else if(j == STV){

            params[pcount++] = STV;
            params[pcount++] = stv_count = rparams[rpcount++];
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.025,0.5);
            params[pcount++] = speed1 = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.025,0.5);
            params[pcount++] = speed2 = tmp;
            tmp = rparams[rpcount++] * boundrand(multmin, multmax);
            clip(&tmp, 0.001,0.01);
            params[pcount++] = maxdelay = tmp;
            
            x->stv_units[stv_count].osc1phs = 0.0;
            x->stv_units[stv_count].osc2phs = 0.0;
            x->stv_units[stv_count].fac2 = 0.5 * (maxdelay - 0.001);
            x->stv_units[stv_count].fac1 = 0.001 + x->stv_units[stv_count].fac2;
            x->stv_units[stv_count].osc1si = ((double)x->sinelen / x->sr) * speed1;
            x->stv_units[stv_count].osc2si = ((double)x->sinelen / x->sr) * speed2;
            delset2(x->stv_units[stv_count].delayline1, x->stv_units[stv_count].dv1, maxdelay, x->sr);
            delset2(x->stv_units[stv_count].delayline2, x->stv_units[stv_count].dv2, maxdelay, x->sr);
            // stv_count = (stv_count + 1) % max_dsp_units;
        }
        else {
            error("el.chameleon~: could not find a process for %d",j);
        }
    }
    
    //    events = floor( boundrand( (float)minproc, (float) maxproc) );
    
    x->pcount = pcount;
}

void chameleon_recall_parameters_exec(t_chameleon *x)
{
    int i, j;
    int ftype;
    double cf, bw;//, bw;
    double delay, revtime;
    double *params = x->params;
    long pcount = x->pcount;
    int comb_dl_count = 0; // where in the comb pool to grab memory
    int flange_count = 0;
    int truncate_count = 0;
    int butterworth_count = 0;
    int sweepreson_count = 0;
    int slidecomb_count = 0;
    int reverb1_count = 0;
    int ellipseme_count = 0;
    int feed1_count = 0;
    int flam1_count = 0;
    int comb4_count = 0;
    int ringfeed_count = 0;
    int bendy_count = 0;
    int ringmod_count = 0;
    int ringmod4_count = 0;
    int slideflam_count = 0;
    int bitcrush_count = 0;
    int resonadsr_count = 0;
    int stv_count = 0;
    double raw_wet;
    double sr = x->sr;
    double *dels;
    double **alpo1, **alpo2;
    double *fltdata;
    double xnorm;
    int nsects;
    LSTRUCT *eel1, *eel2;
    double speed1, speed2, mindelay, maxdelay, duration;
    double basefreq;
    double rvt = boundrand(0.1,0.98);
    double lpt;
    double notedur, sust;
    double phz1, phz2, minfeedback = 0.1, maxfeedback = 0.7;
    long slotnum = x->recall_slot;
    double *rparams; // parameter set to recall
    long rpcount; // number of parameters to read
    long slot_pcount;
        
    if(x->recall_parameters_flag == 1 ){
        x->recall_parameters_flag = 0;
    } else {
        return;
    }

    slot_pcount = x->slots[slotnum].pcount;
    
    if( slot_pcount <= 0){
    //    post("Aborting reload of slot %d", slotnum);
        return;
    }
    rparams = x->slots[slotnum].params;
    pcount = 0;
    rpcount = 0;
    // read the pattern here
   // post("loading %d parameters", slot_pcount);
    while( rpcount < slot_pcount ){
        j = rparams[rpcount++];
        if(j == COMB){
            post("Added COMB unit");
            params[pcount++] = COMB;
            params[pcount++] = delay = rparams[rpcount++];
            params[pcount++] = revtime = rparams[rpcount++];
            params[pcount++] = comb_dl_count = rparams[rpcount++]; // Possible Bug Here???
            mycombset(delay,revtime,0,x->comb_delay_pool1[comb_dl_count],x->sr);
            mycombset(delay,revtime,0,x->comb_delay_pool2[comb_dl_count],x->sr);
        }
        else if(j == RINGMOD) {
            post("Added RINGMOD unit");
            params[pcount++] = RINGMOD;
            params[pcount++] = rparams[rpcount++]; //need a log version
            params[pcount++] = ringmod_count = rparams[rpcount++];
            x->ringmod_phases[ringmod_count] = 0.0;
        }
        else if(j == RINGMOD4) {
            post("Added RINGMOD4 unit");
            params[pcount++] = RINGMOD4;
            params[pcount++] = rparams[rpcount++];
            params[pcount++] = ringmod4_count = rparams[rpcount++];
            x->ringmod4_phases[ringmod4_count] = 0.0;
        }
        else if(j == BENDY){
            post("Added BENDY unit");
            params[pcount++] = BENDY;
            params[pcount++] = bendy_count = rparams[rpcount++];
            params[pcount++] = x->bendy_units[bendy_count].val1 = rparams[rpcount++];
            params[pcount++] = x->bendy_units[bendy_count].val2 = rparams[rpcount++];
            x->bendy_units[bendy_count].counter = 0;
            //delset3(x->bendy_units[bendy_count].delayline1, x->bendy_units[bendy_count].dv1, x->bendy_units[bendy_count].val1,x->sr,BENDY_MAXDEL);
            //delset3(x->bendy_units[bendy_count].delayline2, x->bendy_units[bendy_count].dv2, x->bendy_units[bendy_count].val2,x->sr,BENDY_MAXDEL);
            delset2(x->bendy_units[bendy_count].delayline1, x->bendy_units[bendy_count].dv1, x->bendy_units[bendy_count].val1,x->sr);
            delset2(x->bendy_units[bendy_count].delayline2, x->bendy_units[bendy_count].dv2, x->bendy_units[bendy_count].val2,x->sr);
        }
        else if(j == FLANGE){
            post("Added FLANGE unit");
            params[pcount++] = FLANGE;
            params[pcount++] = rparams[rpcount++];
            params[pcount++] = rparams[rpcount++];
            params[pcount++] = rparams[rpcount++];
            params[pcount++] = rparams[rpcount++];
            params[pcount++] = flange_count = rparams[rpcount++];
            x->flange_units[flange_count].phase = boundrand(0.0,0.5) * (double)x->sinelen / x->sr; // maybe should have stored this too
        }
        else if(j == BUTTER){
            post("Added BUTTER unit");
            params[pcount++] = BUTTER;
            params[pcount++] = ftype = rparams[rpcount++];
            params[pcount++] = cf = rparams[rpcount++];
            params[pcount++] = bw = rparams[rpcount++];
            params[pcount++] = butterworth_count = rparams[rpcount++];
            if( ftype == LOPASS) {
                lobut(x->butterworth_units[butterworth_count].data1, cf, sr);
                lobut(x->butterworth_units[butterworth_count].data2, cf, sr);
            } else if (ftype == HIPASS){
                hibut(x->butterworth_units[butterworth_count].data1, cf, sr);
                hibut(x->butterworth_units[butterworth_count].data2, cf, sr);
            }
            else if(ftype == BANDPASS){
                bpbut(x->butterworth_units[butterworth_count].data1, cf, bw, sr);
                bpbut(x->butterworth_units[butterworth_count].data2, cf, bw, sr);
                x->butterworth_units[butterworth_count].bw = bw;
            }
            x->butterworth_units[butterworth_count].cf = cf;
            x->butterworth_units[butterworth_count].ftype = ftype;
            
        }
        else if(j == TRUNCATE){
            post("Added TRUNCATE unit");
            params[pcount++] = TRUNCATE;
            params[pcount++] = truncate_count = rparams[rpcount++];
            x->truncate_units[truncate_count].counter = 0;
            x->truncate_units[truncate_count].state = 0;
            x->truncate_units[truncate_count].segsamples = 1;

        }
        else if(j == SWEEPRESON){
            post("Added SWEEPRESON unit");
            params[pcount++] = SWEEPRESON;
            params[pcount++] = sweepreson_count = rparams[rpcount++];
            params[pcount++] = x->sweepreson_units[sweepreson_count].minfreq = rparams[rpcount++];
            params[pcount++] = x->sweepreson_units[sweepreson_count].maxfreq = rparams[rpcount++];
            params[pcount++] = x->sweepreson_units[sweepreson_count].bwfac = rparams[rpcount++];
            params[pcount++] = x->sweepreson_units[sweepreson_count].speed = rparams[rpcount++];
            params[pcount++] = x->sweepreson_units[sweepreson_count].phase = rparams[rpcount++];
            // scale phase to SR
            // x->sweepreson_units[sweepreson_count].phase *= ((double)x->sinelen / x->sr);
            // x->sweepreson_units[sweepreson_count].phase = 0.0;
            x->sweepreson_units[sweepreson_count].q1[3] = 0;
            x->sweepreson_units[sweepreson_count].q1[4] = 0;
            x->sweepreson_units[sweepreson_count].q2[3] = 0;
            x->sweepreson_units[sweepreson_count].q2[4] = 0;
        }
        else if(j == SLIDECOMB){
            post("Added SLIDECOMB unit");
            params[pcount++] = SLIDECOMB;
            params[pcount++] = slidecomb_count = rparams[rpcount++];
            params[pcount++] = x->slidecomb_units[slidecomb_count].start_delay = rparams[rpcount++];
            params[pcount++] = x->slidecomb_units[slidecomb_count].end_delay = rparams[rpcount++];
            params[pcount++] = x->slidecomb_units[slidecomb_count].feedback = rparams[rpcount++];
            params[pcount++] = x->slidecomb_units[slidecomb_count].sample_length = rparams[rpcount++];
            
            // scale length to SR
            x->slidecomb_units[slidecomb_count].sample_length *= x->sr;
            x->slidecomb_units[slidecomb_count].counter = 0;
            delset2(x->slidecomb_units[slidecomb_count].delayline1,x->slidecomb_units[slidecomb_count].dv1,MAX_SLIDECOMB_DELAY, x->sr);
            delset2(x->slidecomb_units[slidecomb_count].delayline2,x->slidecomb_units[slidecomb_count].dv2,MAX_SLIDECOMB_DELAY, x->sr);
        }
        else if(j == REVERB1){
            post("Added REVERB1 unit");
            params[pcount++] = REVERB1;
            params[pcount++] = reverb1_count = rparams[rpcount++];
            params[pcount++] = revtime = x->reverb1_units[reverb1_count].revtime = rparams[rpcount++];
            params[pcount++] = raw_wet = rparams[rpcount++];
            
            x->reverb1_units[reverb1_count].wet = sin(1.570796 * raw_wet);
            x->reverb1_units[reverb1_count].dry = cos(1.570796 * raw_wet);
            dels = x->reverb1_units[reverb1_count].dels;
            
            for(i = 0; i < 4; i++){
                 params[pcount++] = dels[i] = rparams[rpcount++];
            }
            
            alpo1 = x->reverb1_units[reverb1_count].alpo1;
            alpo2 = x->reverb1_units[reverb1_count].alpo2;
            for( i = 0; i < 4; i++ ){
                // dels[i] = boundrand(.005, .1 );
                if(dels[i] < .005 || dels[i] > 0.1) {
                    post("reverb1: bad random delay time: %f",dels[i]);
                    dels[i] = .05;
                }
                mycombset(dels[i], revtime, 0, alpo1[i], x->sr);
                mycombset(dels[i], revtime, 0, alpo2[i], x->sr);
            }
            ellipset(x->reverb_ellipse_data,x->reverb1_units[reverb1_count].eel1,&x->reverb1_units[reverb1_count].nsects,&x->reverb1_units[reverb1_count].xnorm);
            ellipset(x->reverb_ellipse_data,x->reverb1_units[reverb1_count].eel2,&x->reverb1_units[reverb1_count].nsects,&x->reverb1_units[reverb1_count].xnorm);
           //  reverb1_count = (reverb1_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == ELLIPSE){
            post("Added ELLIPSE unit");
            params[pcount++] = ELLIPSE;
            params[pcount++] = ellipseme_count = rparams[rpcount++];
            params[pcount++] = x->ellipseme_units[ellipseme_count].filtercode = rparams[rpcount++];
            
            if( x->ellipseme_units[ellipseme_count].filtercode >= ELLIPSE_FILTER_COUNT ){
                error("there is no %d ellipse data",x->ellipseme_units[ellipseme_count].filtercode);
                return;
            };
            fltdata = x->ellipse_data [x->ellipseme_units[ellipseme_count].filtercode];
            eel1 = x->ellipseme_units[ellipseme_count].eel1;
            eel2 = x->ellipseme_units[ellipseme_count].eel2;
            ellipset(fltdata,eel1,&nsects,&xnorm);
            ellipset(fltdata,eel2,&nsects,&xnorm);
            x->ellipseme_units[ellipseme_count].nsects = nsects;
            x->ellipseme_units[ellipseme_count].xnorm = xnorm;
            // ellipseme_count = (ellipseme_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == FEED1){
            post("Added FEED1 unit");
            params[pcount++] = FEED1;
            params[pcount++] = feed1_count = rparams[rpcount++];
            params[pcount++] = mindelay = x->feed1_units[feed1_count].mindelay = rparams[rpcount++];
            params[pcount++] = maxdelay = x->feed1_units[feed1_count].maxdelay = rparams[rpcount++];
            params[pcount++] = speed1 = x->feed1_units[feed1_count].speed1 = rparams[rpcount++];
            params[pcount++] = speed2 = x->feed1_units[feed1_count].speed2 = rparams[rpcount++];
            params[pcount++] = duration = x->feed1_units[feed1_count].duration = rparams[rpcount++];
            
            funcgen1(x->feed1_units[feed1_count].func1, FEEDFUNCLEN,duration,
                     mindelay,maxdelay, speed1, speed2, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            phz1 /= (double) FEEDFUNCLEN; phz2 /= (double) FEEDFUNCLEN;
            funcgen1(x->feed1_units[feed1_count].func2, FEEDFUNCLEN,duration,
                     mindelay * 0.5,maxdelay * 2.0, speed1 * 1.25, speed2 * 0.75, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            phz1 /= (double) FEEDFUNCLEN; phz2 /= (double) FEEDFUNCLEN;
            funcgen1(x->feed1_units[feed1_count].func3, FEEDFUNCLEN,duration,
                     minfeedback, maxfeedback, speed1*.35, speed2*1.25, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            phz1 /= (double) FEEDFUNCLEN; phz2 /= (double) FEEDFUNCLEN;
            funcgen1(x->feed1_units[feed1_count].func4, FEEDFUNCLEN,duration,
                     minfeedback, maxfeedback, speed1*.55, speed2*2.25, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            delset2(x->feed1_units[feed1_count].delayLine1a, x->feed1_units[feed1_count].dv1a, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine2a, x->feed1_units[feed1_count].dv2a, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine1b, x->feed1_units[feed1_count].dv1b, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine2b, x->feed1_units[feed1_count].dv2b, MAX_MINI_DELAY, x->sr);
            // feed1_count = (feed1_count + 1) % MAX_DSP_UNITS;
            
        }
        else if(j == BITCRUSH){
            post("Added BITCRUSH unit");
            params[pcount++] = BITCRUSH;
            params[pcount++] = bitcrush_count = rparams[rpcount++];
            params[pcount++] = x->bitcrush_factors[bitcrush_count] = rparams[rpcount++];
            // bitcrush_count = (bitcrush_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == FLAM1){
            params[pcount++] = FLAM1;
            params[pcount++] = flam1_count = rparams[rpcount++];
            params[pcount++] = x->flam1_units[flam1_count].sample_length = rparams[rpcount++]; // not yet scaled to SR
            params[pcount++] = x->flam1_units[flam1_count].dt = rparams[rpcount++];
            
            x->flam1_units[flam1_count].counter = 0;
            // scale to SR
            x->flam1_units[flam1_count].sample_length *= x->sr;
            
            delset2(x->flam1_units[flam1_count].delayline1, x->flam1_units[flam1_count].dv1, MAX_MINI_DELAY, x->sr);
            delset2(x->flam1_units[flam1_count].delayline2, x->flam1_units[flam1_count].dv2, MAX_MINI_DELAY, x->sr);
           //  flam1_count = (flam1_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == SLIDEFLAM){
            post("Added SLIDEFLAM unit");
            params[pcount++] = SLIDEFLAM;
            params[pcount++] = slideflam_count = rparams[rpcount++];
            params[pcount++] = x->slideflam_units[slideflam_count].dt1 = rparams[rpcount++];
            params[pcount++] = x->slideflam_units[slideflam_count].dt2 = rparams[rpcount++];
            params[pcount++] = x->slideflam_units[slideflam_count].feedback = rparams[rpcount++];
            params[pcount++] = x->slideflam_units[slideflam_count].sample_length = rparams[rpcount++]; // not yet scaled to SR
            
            x->slideflam_units[slideflam_count].counter = 0;
            // scale to SR
            x->slideflam_units[slideflam_count].sample_length *= x->sr;
            
            delset2(x->slideflam_units[slideflam_count].delayline1,x->slideflam_units[slideflam_count].dv1,MAX_SLIDEFLAM_DELAY, x->sr);
            delset2(x->slideflam_units[slideflam_count].delayline2,x->slideflam_units[slideflam_count].dv2,MAX_SLIDEFLAM_DELAY, x->sr);
            // slideflam_count = (slideflam_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == COMB4){
            post("Added COMB4 unit");
            params[pcount++] = COMB4;
            params[pcount++] = comb4_count = rparams[rpcount++];
            rvt = 0.99;
            params[pcount++] = basefreq = rparams[rpcount++];
            // post("comb4: count: %d, basefreq: %f",comb4_count, basefreq);
            for( i = 0; i < 4; i++ ){
                params[pcount++] = lpt = rparams[rpcount++];
                // post("comb4: lpt: %f",lpt);
                mycombset(lpt, rvt, 0, x->comb4_units[comb4_count].combs1[i], x->sr);
                mycombset(lpt, rvt, 0, x->comb4_units[comb4_count].combs2[i], x->sr);
            }
        }
        else if(j == COMPDIST){
            post("Added COMPDIST unit");
            params[pcount++] = COMPDIST;
        }
        else if(j == RINGFEED){
            post("Added RINGFEED unit");
            params[pcount++] = RINGFEED;
            params[pcount++] = ringfeed_count = rparams[rpcount++];
            params[pcount++] = cf = rparams[rpcount++];
            params[pcount++] = bw = rparams[rpcount++];
            
            rsnset2(cf, bw, RESON_NO_SCL, 0., x->resonfeed_units[ringfeed_count].res1q, x->sr);
            rsnset2(cf, bw, RESON_NO_SCL, 0., x->resonfeed_units[ringfeed_count].res2q, x->sr);
            x->resonfeed_units[ringfeed_count].osc1phs = 0.0;
            x->resonfeed_units[ringfeed_count].osc2phs = 0.0;
            params[pcount++] = x->resonfeed_units[ringfeed_count].osc1si = rparams[rpcount++]; // not yet scaled to SR
            params[pcount++] = x->resonfeed_units[ringfeed_count].osc2si = rparams[rpcount++]; // not yet scaled to SR
            params[pcount++] = lpt = rparams[rpcount++];
            params[pcount++] = rvt = rparams[rpcount++];
            
            // scale to SR
            x->resonfeed_units[ringfeed_count].osc1si *= ((double)x->sinelen / x->sr);
            x->resonfeed_units[ringfeed_count].osc2si *= ((double)x->sinelen / x->sr);
            
            mycombset(lpt, rvt, 0, x->resonfeed_units[ringfeed_count].comb1arr, x->sr);
            mycombset(lpt, rvt, 0, x->resonfeed_units[ringfeed_count].comb2arr, x->sr);
            ringfeed_count = (ringfeed_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == RESONADSR){
            post("Added RESONADSR unit");
            params[pcount++] = RESONADSR;
            params[pcount++] = resonadsr_count = rparams[rpcount++];
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->a = rparams[rpcount++];
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->d = rparams[rpcount++];
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->r = rparams[rpcount++];
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v1 = rparams[rpcount++];
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v2 = rparams[rpcount++];
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v3 = rparams[rpcount++];
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v4 = rparams[rpcount++];
            params[pcount++] = x->resonadsr_units[resonadsr_count].bwfac = rparams[rpcount++];
            params[pcount++] =  notedur = rparams[rpcount++];
            x->resonadsr_units[resonadsr_count].phs = 0.0;
            sust = notedur - (x->resonadsr_units[resonadsr_count].adsr->a + x->resonadsr_units[resonadsr_count].adsr->d +  x->resonadsr_units[resonadsr_count].adsr->r);
            x->resonadsr_units[resonadsr_count].adsr->s = sust;
            buildadsr(x->resonadsr_units[resonadsr_count].adsr);
            x->resonadsr_units[resonadsr_count].si = ((double)x->resonadsr_units[resonadsr_count].adsr->len / x->sr) / notedur;
            resonadsr_count = (resonadsr_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == STV){
            post("Added STV unit");
            params[pcount++] = STV;
            params[pcount++] = stv_count = rparams[rpcount++];
            params[pcount++] = speed1 = rparams[rpcount++];
            params[pcount++] = speed2 = rparams[rpcount++];
            params[pcount++] = maxdelay = rparams[rpcount++];

            x->stv_units[stv_count].osc1phs = 0.0;
            x->stv_units[stv_count].osc2phs = 0.0;
            x->stv_units[stv_count].fac2 = 0.5 * (maxdelay - 0.001);
            x->stv_units[stv_count].fac1 = 0.001 + x->stv_units[stv_count].fac2;
            x->stv_units[stv_count].osc1si = ((double)x->sinelen / x->sr) * speed1;
            x->stv_units[stv_count].osc2si = ((double)x->sinelen / x->sr) * speed2;
            delset2(x->stv_units[stv_count].delayline1, x->stv_units[stv_count].dv1, maxdelay, x->sr);
            delset2(x->stv_units[stv_count].delayline2, x->stv_units[stv_count].dv2, maxdelay, x->sr);
            stv_count = (stv_count + 1) % MAX_DSP_UNITS;
        }
        else {
            error("could not find a process for %d",j);
        }
    }
    
//    events = floor( boundrand( (float)minproc, (float) maxproc) );

    x->pcount = pcount;
}

void chameleon_set_parameters_exec(t_chameleon *x)
{
    float rval;
    int events;
    int i, j;
    int ftype;
    double cf, bw;//, bw;
    float *odds = x->odds;
    int maxproc  = x->max_process_per_note;
    int minproc = x->min_process_per_note;
    double delay, revtime;
    double *params = x->params;
    long pcount = x->pcount;
    int comb_dl_count = 0; // where in the comb pool to grab memory
    int flange_count = 0;
    int truncate_count = 0;
    int butterworth_count = 0;
    int sweepreson_count = 0;
    int slidecomb_count = 0;
    int reverb1_count = 0;
    int ellipseme_count = 0;
    int feed1_count = 0;
    int flam1_count = 0;
    int comb4_count = 0;
    int ringfeed_count = 0;
    int bendy_count = 0;
    int ringmod_count = 0;
    int ringmod4_count = 0;
    int slideflam_count = 0;
    int bitcrush_count = 0;
    int resonadsr_count = 0;
    int stv_count = 0;
    double raw_wet;
    double sr = x->sr;
    double *dels;
    double **alpo1, **alpo2;
    double *fltdata;
    double xnorm;
    int nsects;
    LSTRUCT *eel1, *eel2;
    double speed1, speed2, mindelay, maxdelay, duration;
    double basefreq;
    double *ratios = x->ratios;
    double rvt = boundrand(0.1,0.98);
    double lpt;
    int dex;
    double notedur, sust;
    double phz1, phz2, minfeedback = 0.1, maxfeedback = 0.7;
    
    if(x->set_parameters_flag == 0){
        return;
    } else {
        x->set_parameters_flag = 0;
    }
    if(maxproc <= 0){
        return;
    }
    
    events = floor( boundrand( (float)minproc, (float) maxproc) );

    pcount = 0;
    if( DEBUG_CHAMELEON ){
        post("*** EVENT LIST ***");
    }
    for(i = 0; i < events; i++){
        rval = boundrand(0.0,1.0);
        j = 0;
        while(rval > odds[j]){
            j++;
        }
        if( DEBUG_CHAMELEON ){
            post("event: %d", j);
        }
        if(j == COMB){
            params[pcount++] = COMB;
            params[pcount++] = delay = boundrand(0.01,0.35);
            params[pcount++] = revtime = boundrand(0.5,0.98);
            params[pcount++] = comb_dl_count;
            mycombset(delay,revtime,0,x->comb_delay_pool1[comb_dl_count],x->sr);
            mycombset(delay,revtime,0,x->comb_delay_pool2[comb_dl_count],x->sr);
            ++comb_dl_count;
            if( comb_dl_count >= MAX_DSP_UNITS){
                comb_dl_count = 0;
            }
        }
        else if(j == RINGMOD) {
            params[pcount++] = RINGMOD;
            params[pcount++] = boundrand(100.0,2000.0); //need a log version
            params[pcount++] = ringmod_count;
            x->ringmod_phases[ringmod_count] = 0.0;
            ringmod4_count = (ringmod_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == RINGMOD4) {
            params[pcount++] = RINGMOD4;
            params[pcount++] = boundrand(100.0,2000.0); //need a log version
            params[pcount++] = ringmod4_count;
            x->ringmod4_phases[ringmod4_count] = 0.0;
            ringmod4_count = (ringmod4_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == BENDY){
            params[pcount++] = BENDY;
            params[pcount++] = bendy_count;
            params[pcount++] = x->bendy_units[bendy_count].val1 = boundrand(0.01, BENDY_MAXDEL);
            params[pcount++] = x->bendy_units[bendy_count].val2 = boundrand(0.01, BENDY_MAXDEL);
            x->bendy_units[bendy_count].counter = 0;
            delset3(x->bendy_units[bendy_count].delayline1, x->bendy_units[bendy_count].dv1, x->bendy_units[bendy_count].val1,x->sr, BENDY_MAXDEL);
            delset3(x->bendy_units[bendy_count].delayline2, x->bendy_units[bendy_count].dv2, x->bendy_units[bendy_count].val2,x->sr, BENDY_MAXDEL);
            bendy_count = (bendy_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == FLANGE){
            params[pcount++] = FLANGE;
            params[pcount++] = boundrand(80.0,400.0);
            
            params[pcount++] = boundrand(600.0,4000.0);
            params[pcount++] = boundrand(0.1,2.0);
            params[pcount++] = boundrand(0.1,0.95);
            params[pcount++] = flange_count;
            x->flange_units[flange_count].phase = boundrand(0.0,0.5) * (double)x->sinelen / x->sr;
            ++flange_count;
            if( flange_count >= MAX_DSP_UNITS){
                flange_count = 0;
            }
        }
        else if(j == BUTTER){
            params[pcount++] = BUTTER;
            params[pcount++] = ftype = rand() % 3;
            params[pcount++] = cf = boundrand(70.0,3000.0);
            params[pcount++] = bw = cf * boundrand(0.05,0.6);
            params[pcount++] = butterworth_count;
            if( ftype == LOPASS) {
                lobut(x->butterworth_units[butterworth_count].data1, cf, sr);
                lobut(x->butterworth_units[butterworth_count].data2, cf, sr);
            } else if (ftype == HIPASS){
                hibut(x->butterworth_units[butterworth_count].data1, cf, sr);
                hibut(x->butterworth_units[butterworth_count].data2, cf, sr);
            }
            else if(ftype == BANDPASS){
                bpbut(x->butterworth_units[butterworth_count].data1, cf, bw, sr);
                bpbut(x->butterworth_units[butterworth_count].data2, cf, bw, sr);
                x->butterworth_units[butterworth_count].bw = bw;
            }
            x->butterworth_units[butterworth_count].cf = cf;
            x->butterworth_units[butterworth_count].ftype = ftype;
            
            butterworth_count = (butterworth_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == TRUNCATE){
            params[pcount++] = TRUNCATE;
            params[pcount++] = truncate_count;
            x->truncate_units[truncate_count].counter = 0;
            x->truncate_units[truncate_count].state = 0;
            x->truncate_units[truncate_count].segsamples = 1;

            truncate_count = (truncate_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == SWEEPRESON){
            params[pcount++] = SWEEPRESON;
            params[pcount++] = sweepreson_count;
            params[pcount++] = x->sweepreson_units[sweepreson_count].minfreq = boundrand(100.0,300.0);
            params[pcount++] = x->sweepreson_units[sweepreson_count].maxfreq = boundrand(600.0,6000.0);
            params[pcount++] = x->sweepreson_units[sweepreson_count].bwfac = boundrand(0.01,0.2);
            params[pcount++] = x->sweepreson_units[sweepreson_count].speed = boundrand(0.05,2.0);
            params[pcount++] = x->sweepreson_units[sweepreson_count].phase = boundrand(0.0,0.5);
            
            // scale phase to SR
            x->sweepreson_units[sweepreson_count].phase *= ((double)x->sinelen / x->sr);
            
            sweepreson_count = (sweepreson_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == SLIDECOMB){
            params[pcount++] = SLIDECOMB;
            params[pcount++] = slidecomb_count;
            params[pcount++] = x->slidecomb_units[slidecomb_count].start_delay = boundrand(0.001,MAX_SLIDECOMB_DELAY * 0.95);
            params[pcount++] = x->slidecomb_units[slidecomb_count].end_delay = boundrand(0.001,MAX_SLIDECOMB_DELAY * 0.95);
            params[pcount++] = x->slidecomb_units[slidecomb_count].feedback = boundrand(0.7,0.99);
            params[pcount++] = x->slidecomb_units[slidecomb_count].sample_length = boundrand(0.1,2.0);
            
            // scale length to SR
            x->slidecomb_units[slidecomb_count].sample_length *= x->sr;
            
            x->slidecomb_units[slidecomb_count].counter = 0;
            delset2(x->slidecomb_units[slidecomb_count].delayline1,x->slidecomb_units[slidecomb_count].dv1,MAX_SLIDECOMB_DELAY, x->sr);
            delset2(x->slidecomb_units[slidecomb_count].delayline2,x->slidecomb_units[slidecomb_count].dv2,MAX_SLIDECOMB_DELAY, x->sr);
            slidecomb_count = (slidecomb_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == REVERB1){
            params[pcount++] = REVERB1;
            params[pcount++] = reverb1_count;
            params[pcount++] = revtime = x->reverb1_units[reverb1_count].revtime = boundrand(0.25,0.99);
            params[pcount++] = raw_wet = boundrand(0.2,0.8);
            
            x->reverb1_units[reverb1_count].wet = sin(1.570796 * raw_wet);
            x->reverb1_units[reverb1_count].dry = cos(1.570796 * raw_wet);
            dels = x->reverb1_units[reverb1_count].dels;
            
            for(i = 0; i < 4; i++){
                 params[pcount++] = dels[i] = boundrand(.005, .1 );
            }
            
            alpo1 = x->reverb1_units[reverb1_count].alpo1;
            alpo2 = x->reverb1_units[reverb1_count].alpo2;
            for( i = 0; i < 4; i++ ){
                // dels[i] = boundrand(.005, .1 );
                if(dels[i] < .005 || dels[i] > 0.1) {
                    post("reverb1: bad random delay time: %f",dels[i]);
                    dels[i] = .05;
                }
                mycombset(dels[i], revtime, 0, alpo1[i], x->sr);
                mycombset(dels[i], revtime, 0, alpo2[i], x->sr);
            }
            ellipset(x->reverb_ellipse_data,x->reverb1_units[reverb1_count].eel1,&x->reverb1_units[reverb1_count].nsects,&x->reverb1_units[reverb1_count].xnorm);
            ellipset(x->reverb_ellipse_data,x->reverb1_units[reverb1_count].eel2,&x->reverb1_units[reverb1_count].nsects,&x->reverb1_units[reverb1_count].xnorm);
            reverb1_count = (reverb1_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == ELLIPSE){
            params[pcount++] = ELLIPSE;
            params[pcount++] = ellipseme_count;
            params[pcount++] = x->ellipseme_units[ellipseme_count].filtercode = rand() % ELLIPSE_FILTER_COUNT;
            
            if( x->ellipseme_units[ellipseme_count].filtercode >= ELLIPSE_FILTER_COUNT ){
                error("there is no %d ellipse data",x->ellipseme_units[ellipseme_count].filtercode);
                return;
            };
            fltdata = x->ellipse_data [x->ellipseme_units[ellipseme_count].filtercode];
            eel1 = x->ellipseme_units[ellipseme_count].eel1;
            eel2 = x->ellipseme_units[ellipseme_count].eel2;
            ellipset(fltdata,eel1,&nsects,&xnorm);
            ellipset(fltdata,eel2,&nsects,&xnorm);
            x->ellipseme_units[ellipseme_count].nsects = nsects;
            x->ellipseme_units[ellipseme_count].xnorm = xnorm;
            ellipseme_count = (ellipseme_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == FEED1){
            params[pcount++] = FEED1;
            params[pcount++] = feed1_count;
            params[pcount++] = mindelay = x->feed1_units[feed1_count].mindelay = boundrand(.001,0.1);
            params[pcount++] = maxdelay = x->feed1_units[feed1_count].maxdelay = boundrand(mindelay,0.1);
            params[pcount++] = speed1 = x->feed1_units[feed1_count].speed1 = boundrand(.01,0.5);
            params[pcount++] = speed2 = x->feed1_units[feed1_count].speed2 = boundrand(speed1,0.5);
            params[pcount++] = duration = x->feed1_units[feed1_count].duration = boundrand(.05,1.0);
            
            funcgen1(x->feed1_units[feed1_count].func1, FEEDFUNCLEN,duration,
                     mindelay,maxdelay, speed1, speed2, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            phz1 /= (double) FEEDFUNCLEN; phz2 /= (double) FEEDFUNCLEN;
            funcgen1(x->feed1_units[feed1_count].func2, FEEDFUNCLEN,duration,
                     mindelay * 0.5,maxdelay * 2.0, speed1 * 1.25, speed2 * 0.75, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            phz1 /= (double) FEEDFUNCLEN; phz2 /= (double) FEEDFUNCLEN;
            funcgen1(x->feed1_units[feed1_count].func3, FEEDFUNCLEN,duration,
                     minfeedback, maxfeedback, speed1*.35, speed2*1.25, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            phz1 /= (double) FEEDFUNCLEN; phz2 /= (double) FEEDFUNCLEN;
            funcgen1(x->feed1_units[feed1_count].func4, FEEDFUNCLEN,duration,
                     minfeedback, maxfeedback, speed1*.55, speed2*2.25, 1.0, 1.0,&phz1, &phz2, x->sinewave, x->sinelen);
            delset2(x->feed1_units[feed1_count].delayLine1a, x->feed1_units[feed1_count].dv1a, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine2a, x->feed1_units[feed1_count].dv2a, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine1b, x->feed1_units[feed1_count].dv1b, MAX_MINI_DELAY, x->sr);
            delset2(x->feed1_units[feed1_count].delayLine2b, x->feed1_units[feed1_count].dv2b, MAX_MINI_DELAY, x->sr);
            feed1_count = (feed1_count + 1) % MAX_DSP_UNITS;
            
        }
        else if(j == BITCRUSH){
            params[pcount++] = BITCRUSH;
            params[pcount++] = bitcrush_count;
            params[pcount++] = x->bitcrush_factors[bitcrush_count] = boundrand(2.0,8.0);
            bitcrush_count = (bitcrush_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == FLAM1){
            params[pcount++] = FLAM1;
            params[pcount++] = flam1_count;
            params[pcount++] = x->flam1_units[flam1_count].sample_length = boundrand(1.0,4.0); // not yet scaled to SR
            params[pcount++] = x->flam1_units[flam1_count].dt = boundrand(0.08, 0.2);
            
            x->flam1_units[flam1_count].counter = 0;
            // scale to SR
            x->flam1_units[flam1_count].sample_length *= x->sr;
            
            delset2(x->flam1_units[flam1_count].delayline1, x->flam1_units[flam1_count].dv1, MAX_MINI_DELAY, x->sr);
            delset2(x->flam1_units[flam1_count].delayline2, x->flam1_units[flam1_count].dv2, MAX_MINI_DELAY, x->sr);
            flam1_count = (flam1_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == SLIDEFLAM){
            params[pcount++] = SLIDEFLAM;
            params[pcount++] = slideflam_count;
            if( boundrand(0.0,1.0) > 0.1 ){
                params[pcount++] = x->slideflam_units[slideflam_count].dt1 = boundrand(0.01,0.05);
                params[pcount++] = x->slideflam_units[slideflam_count].dt2 = boundrand(0.1,MAX_SLIDEFLAM_DELAY * 0.95);
            } else {
                params[pcount++] = x->slideflam_units[slideflam_count].dt1 = boundrand(0.1,MAX_SLIDEFLAM_DELAY * 0.95);
                params[pcount++] = x->slideflam_units[slideflam_count].dt2 = boundrand(0.01,0.05);
            }
            params[pcount++] = x->slideflam_units[slideflam_count].feedback = boundrand(0.7,0.99);
            params[pcount++] = x->slideflam_units[slideflam_count].sample_length = boundrand(0.25,4.0); // not yet scaled to SR
            
            x->slideflam_units[slideflam_count].counter = 0;
            // scale to SR
            x->slideflam_units[slideflam_count].sample_length *= x->sr;
            
            delset2(x->slideflam_units[slideflam_count].delayline1,x->slideflam_units[slideflam_count].dv1,MAX_SLIDEFLAM_DELAY, x->sr);
            delset2(x->slideflam_units[slideflam_count].delayline2,x->slideflam_units[slideflam_count].dv2,MAX_SLIDEFLAM_DELAY, x->sr);
            slideflam_count = (slideflam_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == COMB4){
            params[pcount++] = COMB4;
            params[pcount++] = comb4_count;
            rvt = 0.99;
            params[pcount++] = basefreq = boundrand(100.0,400.0);
            for( i = 0; i < 4; i++ ){
                dex = (int)floor( boundrand(0.0, 4.0));
                lpt = 1. / basefreq;
                if( dex > 4) { dex = 4; };
                basefreq *= ratios[dex];
                params[pcount++] = lpt;
                mycombset(lpt, rvt, 0, x->comb4_units[comb4_count].combs1[i], x->sr);
                mycombset(lpt, rvt, 0, x->comb4_units[comb4_count].combs2[i], x->sr);
            }
            comb4_count = (comb4_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == COMPDIST){
            params[pcount++] = COMPDIST;
        }
        else if(j == RINGFEED){
            params[pcount++] = RINGFEED;
            params[pcount++] = ringfeed_count;
            params[pcount++] = cf = boundrand(90.0,1500.0);
            params[pcount++] = bw = boundrand(0.02,0.4) * cf;
            rsnset2(cf, bw, RESON_NO_SCL, 0., x->resonfeed_units[ringfeed_count].res1q, x->sr);
            rsnset2(cf, bw, RESON_NO_SCL, 0., x->resonfeed_units[ringfeed_count].res2q, x->sr);
            x->resonfeed_units[ringfeed_count].osc1phs = 0.0;
            x->resonfeed_units[ringfeed_count].osc2phs = 0.0;
            params[pcount++] = x->resonfeed_units[ringfeed_count].osc1si = boundrand(90.0,1500.0); // not yet scaled to SR
            params[pcount++] = x->resonfeed_units[ringfeed_count].osc2si = boundrand(90.0,1500.0); // not yet scaled to SR
            params[pcount++] = lpt = 1.0 / boundrand(90.0,1500.0);
            params[pcount++] = rvt = boundrand(.01,.8);
            
            // scale to SR
            x->resonfeed_units[ringfeed_count].osc1si *= ((double)x->sinelen / x->sr);
            x->resonfeed_units[ringfeed_count].osc2si *= ((double)x->sinelen / x->sr);
            
            mycombset(lpt, rvt, 0, x->resonfeed_units[ringfeed_count].comb1arr, x->sr);
            mycombset(lpt, rvt, 0, x->resonfeed_units[ringfeed_count].comb2arr, x->sr);
            ringfeed_count = (ringfeed_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == RESONADSR){
            params[pcount++] = RESONADSR;
            params[pcount++] = resonadsr_count;
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->a = boundrand(0.01,0.1);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->d = boundrand(0.01,0.05);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->r = boundrand(0.05,0.5);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v1 = boundrand(150.0,4000.0);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v2 = boundrand(150.0,4000.0);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v3 = boundrand(150.0,4000.0);
            params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v4 = boundrand(150.0,4000.0);
            params[pcount++] = x->resonadsr_units[resonadsr_count].bwfac = boundrand(0.03,0.7);
            params[pcount++] =  notedur = boundrand(0.7, 1.2);
            x->resonadsr_units[resonadsr_count].phs = 0.0;
            sust = notedur - (x->resonadsr_units[resonadsr_count].adsr->a + x->resonadsr_units[resonadsr_count].adsr->d +  x->resonadsr_units[resonadsr_count].adsr->r);
            x->resonadsr_units[resonadsr_count].adsr->s = sust;
            buildadsr(x->resonadsr_units[resonadsr_count].adsr);
            x->resonadsr_units[resonadsr_count].si = ((double)x->resonadsr_units[resonadsr_count].adsr->len / x->sr) / notedur;
            resonadsr_count = (resonadsr_count + 1) % MAX_DSP_UNITS;
        }
        else if(j == STV){
            params[pcount++] = STV;
            params[pcount++] = stv_count;
            params[pcount++] = speed1 = boundrand(0.025,0.5);
            params[pcount++] = speed2 = boundrand(0.025,0.5);
            params[pcount++] = maxdelay = boundrand(0.001,0.01);
            x->stv_units[stv_count].osc1phs = 0.0;
            x->stv_units[stv_count].osc2phs = 0.0;
            
            x->stv_units[stv_count].fac2 = 0.5 * (maxdelay - 0.001);
            x->stv_units[stv_count].fac1 = 0.001 + x->stv_units[stv_count].fac2;
            x->stv_units[stv_count].osc1si = ((double)x->sinelen / x->sr) * speed1;
            x->stv_units[stv_count].osc2si = ((double)x->sinelen / x->sr) * speed2;
            delset2(x->stv_units[stv_count].delayline1, x->stv_units[stv_count].dv1, maxdelay, x->sr);
            delset2(x->stv_units[stv_count].delayline2, x->stv_units[stv_count].dv2, maxdelay, x->sr);
            stv_count = (stv_count + 1) % MAX_DSP_UNITS;
        }
        else {
            error("could not find a process for %d",j);
        }
    }
    x->pcount = pcount;
}

void chameleon_dsp64(t_chameleon *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    x->vs = maxvectorsize;
    
    // need more comprehensive reinitialization if sr changes
    if(x->sr != samplerate){
        x->sr = samplerate;
        x->delayline1 = (double *) sysmem_newptr( ((x->maxdelay * x->sr) + 2) * sizeof(double));
        x->delayline2 = (double *) sysmem_newptr( ((x->maxdelay * x->sr) + 2) * sizeof(double));
    }
    
    object_method(dsp64, gensym("dsp_add64"),x,chameleon_perform64,0,NULL);
}

void chameleon_assist (t_chameleon *x, void *b, long msg, long arg, char *dst)
{
    if (msg==1) {
        switch (arg) {
            case 0: sprintf(dst,"(signal) Channel 1 Input"); break;
            case 1: sprintf(dst,"(signal) Channel 2 Input"); break;
        }
    }
    else if (msg==2) {
        switch(arg){
            case 0: sprintf(dst,"(signal) Channel 1 Output"); break;
            case 1: sprintf(dst,"(signal) Channel 2 Output"); break;
            case 2: sprintf(dst,"(list) Data Bank of Stored Settings"); break;
        }
    }
}
