#include "MSPd.h"

#define MAXSTORE (128)
#define OBJECT_NAME "splitspec~"

static t_class *splitspec_class;

typedef struct _splitspec
{
    t_pxobject x_obj;
    int N;
    int N2;
    void *list_outlet;
    void *phase_outlet;
    t_atom *list_data;
    t_double frame_duration;
    int table_offset;
    int bin_offset;
    t_double *last_mag;
    t_double *current_mag;
    int *last_binsplit;
    int *current_binsplit;
    int **stored_binsplits;
    short *stored_slots;
    short new_distribution;
    short interpolation_completed;
    short bypass;
    short initialize;
    int manual_override;
    long countdown_samps; // samps for a given fadetime
    long counter;
    int overlap_factor; // compensate for overlap in fade
    t_double sr;
    t_double fl_phase; // show phase as float
    int hopsamps; // number of samples to hop
    int channel_count; // number of channels to split
    t_clock *phase_clock;
    t_double **magvecs;// connect to mag input vectors
    t_double **phasevecs;// point to phase input vectors
} t_splitspec;

void *splitspec_new(t_symbol *s, int argc, t_atom *argv);
void splitspec_perform64(t_splitspec *x, t_object *dsp64, double **ins,
                         long numins, double **outs,
                         long numouts, long n,
                         long flags, void *userparam);
t_int *splitspec_perform(t_int *w);
void splitspec_dsp64(t_splitspec *x, t_object *dsp64, short *count, double R,
                     long vector_size, long flags);
void splitspec_assist (t_splitspec *x, void *b, long msg, long arg, char *dst);
void splitspec_showstate( t_splitspec *x );
void splitspec_bypass( t_splitspec *x, t_floatarg toggle);
void splitspec_manual_override( t_splitspec *x, t_floatarg toggle );
void splitspec_setstate (t_splitspec *x, t_symbol *msg, int argc, t_atom *argv);
void splitspec_ramptime (t_splitspec *x, t_symbol *msg, int argc, t_atom *argv);
int rand_index( int max);
void splitspec_scramble (t_splitspec *x);
void splitspec_spiral(t_splitspec *x);
void splitspec_squantize(t_splitspec *x, t_floatarg blockbins);
void splitspec_overlap( t_splitspec *x, t_floatarg factor);

void splitspec_store( t_splitspec *x, t_floatarg floc);
void splitspec_recall( t_splitspec *x, t_floatarg floc);
void splitspeci( int *current_binsplit, int *last_binsplit, int bin_offset, int table_offset,
                t_double *current_mag, t_double *last_mag, t_double *inmag, t_double *dest_mag, int start, int end, int n,
                t_double oldfrac, t_double newfrac );
void splitspec( int *binsplit, int bin_offset, int table_offset,
               t_double *inmag, t_double *dest_mag, int start, int end, int n );

void splitspec_dsp_free( t_splitspec *x );

void splitspec_phaseout(t_splitspec *x);
int splitspec_closestPowerOfTwo(int p);
void splitspec_dsp64(t_splitspec *x, t_object *dsp64, short *count, double R,
                     long vector_size, long flags);


int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.splitspec~", (method)splitspec_new, (method)splitspec_dsp_free, sizeof(t_splitspec), 0, A_GIMME, 0);
	
	class_addmethod(c, (method)splitspec_assist,"assist",A_CANT,0);
    class_addmethod(c, (method)splitspec_dsp64, "dsp64", A_CANT,0);
    class_addmethod(c, (method)splitspec_showstate, "showstate",0);
    class_addmethod(c, (method)splitspec_manual_override, "manual_override",A_FLOAT,0);
    class_addmethod(c, (method)splitspec_bypass, "bypass",A_FLOAT,0);
    class_addmethod(c, (method)splitspec_store, "store",A_FLOAT,0);
    class_addmethod(c, (method)splitspec_recall, "recall",A_FLOAT,0);
    class_addmethod(c, (method)splitspec_setstate, "setstate",A_GIMME,0);
    class_addmethod(c, (method)splitspec_ramptime, "ramptime",A_GIMME,0);
    class_addmethod(c, (method)splitspec_overlap, "overlap",A_FLOAT,0);
    class_addmethod(c, (method)splitspec_scramble, "scramble",0);
    class_addmethod(c, (method)splitspec_spiral, "spiral",0);
    class_addmethod(c, (method)splitspec_squantize, "squantize",0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	splitspec_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void splitspec_phaseout(t_splitspec *x)
{
    outlet_float(x->phase_outlet, x->fl_phase);
}

void splitspec_overlap( t_splitspec *x, t_floatarg fol )
{
    int overlap = (int)fol;
	if( overlap < 2 ){
		post("splitspec~: illegal overlap %d",overlap);
	}
	x->overlap_factor = overlap;
}

void splitspec_spiral(t_splitspec *x)
{
    int i,j,k;
    int offset;

    int channel_count = x->channel_count;
    int *current_binsplit = x->current_binsplit;
    int *last_binsplit = x->last_binsplit;
    
    int N2 = x->N2;
    
    offset = N2 / channel_count;
    
    x->new_distribution = 1;
    x->interpolation_completed = 0;
    for( i = 0; i < N2; i++ ){
        last_binsplit[i] = current_binsplit[i];
    }
    k = 0;
    for( i = 0; i < N2 / channel_count; i++){
        for(j = 0; j < channel_count; j++){
            current_binsplit[i + (j * offset)] = k++;
        }
    }
    if(! x->counter) { // Ramp Off - Immediately set last to current
        for( i = 0; i < N2; i++ ){
            last_binsplit[ i ] = current_binsplit[ i ];
        }
    }
}

void splitspec_squantize(t_splitspec *x, t_floatarg bb)
{
    int i, j, k;
    
    int maxblock;
    int iterations;
    int bincount = 0;
    int *current_binsplit = x->current_binsplit;
    int *last_binsplit = x->last_binsplit;
    int blockbins = (int) bb;
    int N2 = x->N2;
    int channel_count = x->channel_count;
    blockbins = splitspec_closestPowerOfTwo( blockbins );
    maxblock = N2 / channel_count;
    if( blockbins < 1 || blockbins > maxblock ){
        error("%d is out of bounds - must be between 1 and %d", blockbins, maxblock);
        return;
    }
    
    iterations = N2 /  channel_count /  blockbins;
    x->new_distribution = 1;
    x->interpolation_completed = 0;
    
    
    for( i = 0; i < N2; i++ ){
        last_binsplit[i] = current_binsplit[i];
    }
    
    if( iterations == 1 ){
        for( i = 0; i < N2 ; i++ ){
            current_binsplit[i] = i;
        }
    }
    else {
        for( k = 0; k < iterations; k++ ) {
            for( i = 0; i < N2; i += maxblock  ){
                for( j = 0; j < blockbins; j++ ){
                    if( i + j + k * blockbins < N2 ){
                        current_binsplit[i + j + k * blockbins] = bincount++;
                        // post("assigning %d to position %d", bincount-1, i+j+k*blockbins);
                    } else {
                        // error("%d out of range", i + j + k * blockbins);
                    }
                }
            }
        }
    }
    
    
//    x->frames_left = x->ramp_frames;
    if(! x->counter) { // Ramp Off - Immediately set last to current
        for( i = 0; i < N2; i++ ){
            last_binsplit[ i ] = current_binsplit[ i ];
        }
    }
}

void splitspec_bypass( t_splitspec *x, t_floatarg toggle )
{
    x->bypass = (int)toggle;
}

void splitspec_manual_override( t_splitspec *x, t_floatarg toggle )
{
    x->manual_override = (int) toggle;
}

void splitspec_dsp_free( t_splitspec *x ){
    int i;
    if(x->initialize == 0){
        sysmem_freeptr(x->list_data);
        sysmem_freeptr(x->last_binsplit);
        sysmem_freeptr(x->current_binsplit);
        sysmem_freeptr(x->last_mag);
        sysmem_freeptr(x->current_mag);
        sysmem_freeptr(x->stored_slots);
        for(i = 0; i < MAXSTORE; i++){
            sysmem_freeptr(x->stored_binsplits[i]);
        }
        sysmem_freeptr(x->stored_binsplits);
       sysmem_freeptr(x->magvecs);
       sysmem_freeptr(x->phasevecs);
    }
}

void splitspeci( int *current_binsplit, int *last_binsplit, int bin_offset, int table_offset,
                t_double *current_mag, t_double *last_mag, t_double *inmag, t_double *dest_mag, int start, int end, int n,
                t_double oldfrac, t_double newfrac )
{
    int i;
    int bindex;
    
    for( i = 0; i < n; i++ ){
        last_mag[i] = current_mag[i] = 0.0;
    }
    for( i = start; i < end; i++ ){
        bindex = current_binsplit[ (i + table_offset) % n ];
        bindex = ( bindex + bin_offset ) % n;
        current_mag[ bindex ] = inmag[ bindex ];
        bindex = last_binsplit[ (i + table_offset) % n ];
        bindex = ( bindex + bin_offset ) % n;
        last_mag[ bindex ] = inmag[ bindex ];
    }
    for( i = 0; i < n; i++){
        if(! current_mag[i] && ! last_mag[i]){
            dest_mag[i] = 0.0;
        } else {
            dest_mag[i] = oldfrac * last_mag[i] + newfrac * current_mag[i];
        }
    }
    
}

void splitspec( int *binsplit, int bin_offset, int table_offset,
               t_double *inmag, t_double *dest_mag, int start, int end, int n )
{
    int i;
    int bindex;
    // n is actually N2
    for( i = start; i < end; i++){
        bindex = binsplit[ (i + table_offset) % n ];
        bindex = ( bindex + bin_offset ) % n;
        dest_mag[ bindex ] = inmag[ bindex ];
    }
}


void splitspec_store( t_splitspec *x, t_floatarg floc)
{
    int **stored_binsplits = x->stored_binsplits;
    int *current_binsplit = x->current_binsplit;
    short *stored_slots = x->stored_slots;
    int location = (int)floc;
    int i;
    
    if( location < 0 || location > MAXSTORE - 1 ){
        error("location must be between 0 and %d, but was %d", MAXSTORE, location);
        return;
    }
    for(i = 0; i < x->N2; i++ ){
        stored_binsplits[location][i] = current_binsplit[i];
    }
    stored_slots[location] = 1;
    
//    post("stored bin split at location %d", location);
}

void splitspec_recall( t_splitspec *x, t_floatarg floc)
{
    int **stored_binsplits = x->stored_binsplits;
    int *current_binsplit = x->current_binsplit;
    int *last_binsplit = x->last_binsplit;
    short *stored_slots = x->stored_slots;
    int i;
    int location = (int)floc;
    if( location < 0 || location > MAXSTORE - 1 ){
        error("location must be between 0 and %d, but was %d", MAXSTORE, location);
        return;
    }
    if( ! stored_slots[location] ){
        error("nothing stored at location %d", location);
        return;
    }
    
    for(i = 0; i < x->N2; i++ ){
        last_binsplit[i] = current_binsplit[i];
        current_binsplit[i] = stored_binsplits[location][i];
    }
    
    x->new_distribution = 1;
    x->interpolation_completed = 0;
 //   x->frames_left = x->ramp_frames;
    if(! x->counter) { // Ramp Off - Immediately set last to current
        for( i = 0; i < x->N2; i++ ){
            x->last_binsplit[ i ] = x->current_binsplit[ i ];
        }
    }
}

void *splitspec_new(t_symbol *s, int argc, t_atom *argv)
{
    int i;
    t_splitspec *x = (t_splitspec *)object_alloc(splitspec_class);
    
    // x->channel_count = 8; // hard wire just for now
    x->channel_count = (int) atom_getfloatarg(0, argc, argv);
    x->channel_count = splitspec_closestPowerOfTwo( x->channel_count );
    // post("Channel count is: %d", x->channel_count);
    srand( time( 0 ) );
    
    dsp_setup((t_pxobject *)x,5);
    
    /* for(i=0; i < 4; i++){
        inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("signal"),gensym("signal"));
    }
    for(i=0; i < x->channel_count * 2; i++){
        outlet_new(&x->x_obj, gensym("signal"));
    }
    */

    x->list_outlet = (void *) listout((void *)x);
    x->phase_outlet = (void *) floatout((void *)x);
    x->phase_clock = (void *) clock_new((void *)x, (method)splitspec_phaseout);
    for(i = 0; i < x->channel_count * 2; i++){
        outlet_new((t_object *)x, "signal");
    }
    x->x_obj.z_misc |= Z_NO_INPLACE;
    x->bypass = 0;
    x->table_offset = 0;
    x->bin_offset = 0;
    
    x->sr = sys_getsr();
 	x->counter = 0;
    x->overlap_factor = 8; // default
    
	x->countdown_samps = 1.0 * x->sr; // 1 second fade time by default
    
    x->initialize = 1;
    x->manual_override = 0;

    x->magvecs = (t_double **) sysmem_newptr(x->channel_count * sizeof(t_double *));
    x->phasevecs = (t_double **) sysmem_newptr(x->channel_count * sizeof(t_double *));
    return x;
}

void splitspec_assist (t_splitspec *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
        if( arg == 0){
            sprintf(dst,"(signal) Magnitude Input");
        } else if( arg > 0 && arg <= x->channel_count ){
            sprintf(dst,"(signal) Phase Input");
        } else if( arg == x->channel_count + 1) {
            sprintf(dst,"(signal) Table Offset");
        }
        else if( arg == x->channel_count + 2) {
            sprintf(dst,"(signal) Bin Offset");
        }
        else if( arg == x->channel_count + 3) {
            sprintf(dst,"(signal) Manual Control");
        }
        else if( arg == x->channel_count + 4) {
            sprintf(dst,"(signal) Manual Control");
        }
	} else if (msg==2) {
        if( arg < x->channel_count * 2){
            // needs to be fixed!!!!
            sprintf(dst,"(signal) Spectrum Segment %ld",arg / 2);
        } else if( arg == x->channel_count) {
            sprintf(dst,"(list) Current Bin State");
        } else {
            sprintf(dst,"(list) Ramp Sync");
        }
	}
}

int splitspec_closestPowerOfTwo(int p){
    int base = 2;
    while(base < p){
        base *= 2;
    }
    return base;
}

void splitspec_perform64(t_splitspec *x, t_object *dsp64, double **ins,
                         long numins, double **outs,
                         long numouts, long n,
                         long flags, void *userparam)
{
	
    int i, j;

//    t_splitspec *x = (t_splitspec *) (w[1]);
    int channel_count = x->channel_count;
    
    t_double *inmag = ins[0];
    t_double *inphase = ins[1];
    t_double *t_offset = ins[2];
    t_double *b_offset = ins[3];
    t_double *manual_control = ins[4];

    t_double **magvecs = x->magvecs;
    t_double **phasevecs = x->phasevecs;

    
    int table_offset = x->table_offset;
    int bin_offset = x->bin_offset;
    
    int *current_binsplit = x->current_binsplit;
    int *last_binsplit = x->last_binsplit;
    t_double *last_mag = x->last_mag;
    t_double *current_mag = x->current_mag;
    long counter = x->counter;
    long countdown_samps = x->countdown_samps;
    t_double frac, oldgain, newgain;

    int hopsamps = x->hopsamps;
    int N2 = x->N2;
    /****/
    

    // assign local vector pointers
    for(i = 0, j= 0; i < channel_count * 2; i+=2, j++){
        magvecs[j] = outs[ i ];
        phasevecs[j] = outs[ i + 1 ];
    }
    
    table_offset = *t_offset * n;
    bin_offset = *b_offset * n;
    
    if( table_offset  < 0 )
        table_offset *= -1;
    if( bin_offset  < 0 )
        bin_offset *= -1;
  	
    
    // n == fftsize / 2 (N2)
    // n is the number of "bins", and is also the number of values in each signal vector
    
    if( x->bypass ){
        for( i = 0; i < n; i++){
            for(j = 0; j < channel_count; j++){
                magvecs[j][i] = inmag[i] * 0.5;;
                phasevecs[j][i] = inphase[i];
            }
        }
        return;
    }
    
    // ZERO OUT MAGNITUDES AND COPY PHASES TO OUTPUT
    for( i = 0; i < n; i++ ){
        for(j = 0; j < channel_count; j++){
            magvecs[j][i] = 0.0;
            phasevecs[j][i] = inphase[i];
        }
    }
    
    // Special case of live control over interpolation
    if( x->manual_override ){
        
        // do interpolation
        frac = *manual_control;
        // sanity check here
        if( frac < 0 ) { frac = 0; }
        if( frac >1.0 ){ frac = 1.0; }
        oldgain = cos( frac * PIOVERTWO );
        newgain = sin( frac * PIOVERTWO );
        
        for(j = 0; j < channel_count; j++){
            splitspeci( current_binsplit, last_binsplit, bin_offset, table_offset,
                       current_mag, last_mag, inmag, magvecs[j],
                       N2*j/channel_count, N2*(j+1)/channel_count, N2, oldgain, newgain );
        }
        return;
    }
    
    // Normal operation
    if( x->new_distribution ){
        x->new_distribution = 0;
        // put out contents of last distribution
        for(j = 0; j < channel_count; j++){
            splitspec(last_binsplit, bin_offset, table_offset, inmag, magvecs[j],
                      N2*j/channel_count, N2*(j+1)/channel_count, N2);
        }
        frac = 0.0;
        
    } else if ( x->interpolation_completed ) {
        // put out contents of current distribution
        for(j = 0; j < channel_count; j++){
            splitspec(current_binsplit, bin_offset, table_offset, inmag, magvecs[j],
                      N2*j/channel_count, N2*(j+1)/channel_count, N2);
        }
        frac = 1.0;
    } else {
        // do interpolation
        frac = (t_double) counter / (t_double) countdown_samps;
        oldgain = cos( frac * PIOVERTWO );
        newgain = sin( frac * PIOVERTWO );
        for(j = 0; j < channel_count; j++){
            splitspeci( current_binsplit, last_binsplit, bin_offset, table_offset,
                       current_mag, last_mag, inmag, magvecs[j],
                       N2*j/channel_count, N2*(j+1)/channel_count, N2, oldgain, newgain );
        }
        // end of interpolation
        
        counter += hopsamps;
        if( counter >= countdown_samps )
        {
            counter = countdown_samps;
            x->interpolation_completed = 1;
        }
    }
    
    x->fl_phase = frac;
    clock_delay(x->phase_clock,0.0); // send current phase to float outlet
    x->counter = counter;
}


void splitspec_scramble (t_splitspec *x)
{
    int i;
    
    int max = x->N2;
    int swapi, tmp;
    int N2 = x->N2;
    int *current_binsplit = x->current_binsplit;
    int *last_binsplit = x->last_binsplit;
    
    x->new_distribution = 1;
    x->interpolation_completed = 0;

    // Copy current mapping to last mapping (first time this will be all zeros)
    
    for( i = 0; i < x->N2; i++ ){
        last_binsplit[i] = current_binsplit[i];
    }

    for( i = 0; i < N2; i++ ){
        current_binsplit[i] = i;
    }
    max = N2;
    for(i = 0; i < N2; i++){
        swapi = rand() % max;
        tmp = current_binsplit[swapi];
        current_binsplit[swapi] = current_binsplit[max - 1];
        current_binsplit[max - 1] = tmp;
        --max;
    }
    /*
    for(i = 0; i < N2; i++){
        post("i: %d, dex: %d", i, current_binsplit[i]);
    }
   */
    x->counter = 0;
    if(! x->countdown_samps ) { // Ramp Off - Immediately set last to current
        for( i = 0; i < x->N2; i++ ){
            last_binsplit[ i ] = current_binsplit[ i ];
        }
    }
}


void splitspec_setstate (t_splitspec *x, t_symbol *msg, int argc, t_atom *argv) {
    short i;
    
    if( argc != x->N2 ){
        error("list must be of length %d, but actually was %d", x->N2, argc);
        return;
    }
    for( i = 0; i < x->N2; i++ ){
        x->last_binsplit[ i ] = x->current_binsplit[ i ];
        x->current_binsplit[ i ] = 0;
    }
    for (i=0; i < argc; i++) {
        x->current_binsplit[i] = atom_getintarg(i, argc, argv );
		
    }
   // x->frames_left = x->ramp_frames;
    if(!x->counter) { // Ramp Off - Immediately set last to current
        for( i = 0; i < x->N2; i++ ){
            x->last_binsplit[ i ] = x->current_binsplit[ i ];
        }
    }
    
    return;
}

void splitspec_ramptime (t_splitspec *x, t_symbol *msg, int argc, t_atom *argv) {
    t_double rampdur;
    
 	rampdur = atom_getfloatarg(0,argc,argv) * 0.001; // convert from milliseconds
 	x->countdown_samps = rampdur * x->sr;
 	x->counter = 0;
    //  post("countdown samps :%d", x->countdown_samps  );
}

// REPORT CURRENT SHUFFLE STATUS
void splitspec_showstate (t_splitspec *x ) {
    
    t_atom *list_data = x->list_data;
    
    short i, count;
    
    count = 0;
    // post("showing %d data points", x->N2);
    
    if(! x->initialize){
        for( i = 0; i < x->N2; i++ ) {
            atom_setfloat(list_data+count,(t_double)x->current_binsplit[i]);
            ++count;
        }
        outlet_list(x->list_outlet,0,x->N2,list_data);
    }
    return;
}

//void splitspec_dsp(t_splitspec *x, t_signal **sp)
void splitspec_dsp64(t_splitspec *x, t_object *dsp64, short *count, double R,
                     long vector_size, long flags)
{
    int i;
    t_double funda;

    if( ! R ){
        // error("splitspec~: zero sample rate!");
        return;
    }
	
    
    if( x->initialize || x->sr != R || x->N != vector_size * 2){
        
        x->sr = R;
        x->N = vector_size * 2;
        x->N2 = vector_size;
        funda = R / (2. * (t_double) x->N) ;
        
        if(x->initialize){
            x->list_data = (t_atom *) sysmem_newptrclear((x->N + 2) * sizeof(t_atom));
            x->last_binsplit = (int *) sysmem_newptrclear( x->N2 * sizeof(int));
            x->current_binsplit = (int *) sysmem_newptrclear( x->N2 * sizeof(int));
            x->last_mag = (t_double *) sysmem_newptrclear(x->N2 * sizeof(t_double)) ;
            x->current_mag = (t_double *) sysmem_newptrclear(x->N2 * sizeof(t_double)) ;
            x->stored_slots = (short *) sysmem_newptrclear(x->N2 * sizeof(short));
            x->stored_binsplits = (int **) sysmem_newptrclear(MAXSTORE * sizeof(int *));
            for( i = 0; i < MAXSTORE; i++ ){
                x->stored_binsplits[i] = (int *)sysmem_newptrclear(x->N2 * sizeof(int));
            }
        } else {
            x->list_data = (t_atom *) sysmem_resizeptr((void *)x->list_data,(x->N + 2) * sizeof(t_atom));
            x->last_binsplit = (int *) sysmem_resizeptr((void *)x->last_binsplit,x->N2 * sizeof(int));
            x->current_binsplit = (int *) sysmem_resizeptr((void *)x->current_binsplit,x->N2 * sizeof(int));
            x->last_mag = (t_double *) sysmem_resizeptr((void *)x->last_mag,x->N2 * sizeof(t_double));
            x->current_mag = (t_double *) sysmem_resizeptr((void *)x->current_mag,x->N2 * sizeof(t_double));
            x->stored_slots = (short *) sysmem_resizeptr((void *)x->stored_slots,x->N2 * sizeof(short));
            for( i = 0; i < MAXSTORE; i++ ){
                x->stored_binsplits[i] = (int *) sysmem_resizeptr((void *)x->stored_binsplits[i],x->N2 * sizeof(int));
            }
            for(i = 0; i < x->N2; i++){
                x->last_mag[i] = 0.0;
                x->current_mag[i] = 0.0;
                x->current_binsplit[i] = i;
                x->last_binsplit[i] = i;
            }
        }
        
        x->frame_duration = (t_double) vector_size / R;
        
        splitspec_scramble( x );
        for( i = 0; i < x->N2; i++ ){
            x->last_binsplit[i] = x->current_binsplit[i];
        }
        
        x->initialize = 0;
        x->counter = 0;
    }
    
    if(vector_size == 0) {
        // post("zero vector size!");
        return;
    } else {
        x->hopsamps = x->N / x->overlap_factor;
        post("hop samps: %d, overlap: %d", x->hopsamps, x->overlap_factor);
        object_method(dsp64, gensym("dsp_add64"),x,splitspec_perform64,0,NULL);
    }
}


