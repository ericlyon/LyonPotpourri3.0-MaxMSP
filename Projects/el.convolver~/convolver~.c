#include "MSPd.h"
#include "fftease_oldskool.h"

#define OBJECT_NAME "convolver~"
#define DENORM_WANT_FIX		1	

#define COMPILE_DATE "1.7.08"
#define OBJECT_VERSION "2.02"

static t_class *convolver_class;

#define CBUF_SIZE 32768
#define NCMAX 52428800


/* Rewrite with Buffer protections */

typedef struct _buffy {
	float *b_samples;
	long b_frames;
	long b_nchans;
	long b_valid;
	t_buffer_obj *b;
    t_buffer_ref *bref;
	t_symbol *myname;
} t_buffy;


typedef struct _convolver
{
	t_pxobject x_obj;
	t_buffy *impulse; // impulse buffer
	t_buffy *source; // source buffer
	t_buffy *dest; // output buffer
	void *bang; // completion bang
	float sr;
	// convolution stuff
	float *tbuf;
	float *sbuf;
	float *filt;
	long N;
	long N2;
	long last_N;
	// for fast fft
	float mult; 
	float *trigland;
	int *bitshuffle;
	short static_memory; // flag to avoid dynamic memory manipulation
} t_convolver;

// maybe no dsp method
float boundrand(float min, float max);
void convolver_setbuf(t_convolver *x, t_buffy *trybuf);
void *convolver_new(t_symbol *msg, short argc, t_atom *argv);
void convolver_mute(t_convolver *x, t_floatarg toggle);
void convolver_assist (t_convolver *x, void *b, long msg, long arg, char *dst);
void convolver_dsp_free(t_convolver *x);
void convolver_seed(t_convolver *x, t_floatarg seed);
void convolver_attach_buffers(t_convolver *x) ;
void convolver_spikeimp(t_convolver *x, t_floatarg density);
void convolver_convolve(t_convolver *x);
void convolver_convolvechans(t_convolver *x, t_symbol *msg, short argc, t_atom *argv);
void convolver_version(t_convolver *x);
void convolver_noiseimp(t_convolver *x, t_floatarg curve);

void rfft( float *x, int N, int forward );
void cfft( float *x, int NC, int forward );
void bitreverse( float *x, int N );

void convolver_static_memory(t_convolver *x, t_floatarg toggle);

int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("el.convolver~", (method)convolver_new, (method)dsp_free, sizeof(t_convolver), 0,A_GIMME, 0);
	class_addmethod(c, (method)convolver_assist,"attach_buffers",A_CANT,0);
    class_addmethod(c, (method)convolver_spikeimp,"spikeimp",A_FLOAT,0);
    class_addmethod(c, (method)convolver_noiseimp,"noiseimp",A_FLOAT,0);
    class_addmethod(c, (method)convolver_convolve,"convolve",0);
    class_addmethod(c, (method)convolver_convolvechans,"convolvechans",A_GIMME,0);
    class_addmethod(c, (method)convolver_static_memory,"static_memory",A_FLOAT,0);
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	convolver_class = c;
	
	potpourri_announce(OBJECT_NAME);
	return 0;
}

void convolver_version(t_convolver *x)
{
	post("%s version %s, compiled %s",OBJECT_NAME, OBJECT_VERSION, COMPILE_DATE);
}


void convolver_static_memory(t_convolver *x, t_floatarg toggle)
{
	
	long memcount = 0;
	float *tbuf = x->tbuf;
	float *sbuf = x->sbuf;
	float *filt = x->filt;
	int *bitshuffle = x->bitshuffle;
	float *trigland = x->trigland;	
	t_buffy *impulse = x->impulse;
	long N, N2;
	
	x->static_memory = (short) toggle;
	
	if( x->static_memory ){
		convolver_attach_buffers( x );
		
		for( N2 = 2; N2 < NCMAX; N2 *= 2){
			if( N2 >= impulse->b_frames ){
				// post("%s: Exceeded Impulse Maximum: %d",OBJECT_NAME, NCMAX);
				break;
			}
		}
		N = 2 * N2;
		
		post("%s: memory is now static - do not reload your impulse buffer",OBJECT_NAME);
		
		if ((sbuf = (float *) sysmem_newptrclear((N+2) * sizeof(float))) == NULL)
			error("%s: insufficient memory", OBJECT_NAME);
		memcount += (N+2) * sizeof(float);
		if ((tbuf = (float *) sysmem_newptrclear(N2 * sizeof(float))) == NULL)
			error("%s: insufficient memory",OBJECT_NAME);
		memcount += (N2) * sizeof(float);
		if ((filt = (float *) sysmem_newptrclear((N+2) * sizeof(float))) == NULL)
			error("%s: insufficient memory",OBJECT_NAME);
		memcount += (N+2) * sizeof(float);
		if( (bitshuffle = (int *) sysmem_newptrclear(N * 2 * sizeof(int))) == NULL)
			error("%s: insufficient memory",OBJECT_NAME);
		memcount += (N2) * sizeof(float);
		if( (trigland = (float *) sysmem_newptrclear(N * 2 * sizeof(float))) == NULL)
			error("%s: insufficient memory",OBJECT_NAME);	
		memcount += (N2) * sizeof(float);
		post("%s: allocated %f Megabytes for %s", OBJECT_NAME, (float)memcount / 1000000.0, impulse->myname->s_name);
	}
}



void convolver_seed(t_convolver *x, t_floatarg seed)
{
	srand((long)seed);
}

void convolver_convolve(t_convolver *x)
{
	int i;
	t_symbol *mymsg;
	short myargc = 3;
	t_atom data[3];
	mymsg = (t_symbol *) calloc(1, sizeof(t_symbol));
	convolver_attach_buffers( x );
	if(x->source->b_nchans == x->impulse->b_nchans && x->impulse->b_nchans == x->dest->b_nchans){
		// post("case 1");
		for(i = 0; i < x->source->b_nchans; i++){
			atom_setfloat(data, i+1); // source
			atom_setfloat(data+1, i+1); // impulse
			atom_setfloat(data+2, i+1); // destination
			convolver_convolvechans(x, mymsg, myargc, data);
		}
	} 
	else if(x->source->b_nchans == 1 && x->impulse->b_nchans == x->dest->b_nchans){
		//post("case 2");
		for(i = 0; i < x->impulse->b_nchans; i++){
			atom_setfloat(data, 1); // source
			atom_setfloat(data+1, i+1); // impulse
			atom_setfloat(data+2, i+1); // destination
			convolver_convolvechans(x, mymsg, myargc, data);
		}
	}
	else if(x->impulse->b_nchans == 1 && x->source->b_nchans == x->dest->b_nchans){
		//post("case 3");
		for(i = 0; i < x->impulse->b_nchans; i++){
			atom_setfloat(data, i+1); // source
			atom_setfloat(data+1, 1); // impulse
			atom_setfloat(data+2, i+1); // destination
			convolver_convolvechans(x, mymsg, myargc, data);
		}
	} else {
		post("%s: \"convolve\" is not smart enough to figure out what you want to do. Try \"convolvechans\"",OBJECT_NAME);
		post("source chans: %d, impulse chans: %d, dest chans: %d",x->source->b_nchans, x->impulse->b_nchans, x->dest->b_nchans );
	}
	outlet_bang(x->bang);
}

// this one is for multi channel convolutions

void convolver_convolvechans(t_convolver *x, t_symbol *msg, short argc, t_atom *argv)
{
	float *tbuf = x->tbuf;
	float *sbuf = x->sbuf;
	float *filt = x->filt;
	long N = x->N;
	long N2 = x->N2;
	long i, j, ip, ip1;
	long ifr_cnt = 0, ofr_cnt = 0;
	long inframes, outframes;
	int target_frames = 2;
	short copacetic; // loop enabler
	float a,b,temp,max=0.0,gain=1.0; //,thresh=.0000000001,fmag;
	int readframes, writeframes;
	t_buffy *impulse = x->impulse;
	t_buffy *source = x->source;
	t_buffy *dest = x->dest;
	int *bitshuffle = x->bitshuffle;
	float *trigland = x->trigland;	
	long source_chan, impulse_chan, dest_chan;
	float rescale = 0.5 / (float) N;
	t_atom newsize;
	
	convolver_attach_buffers( x );
	
	source_chan = atom_getfloatarg(0,argc,argv);
	impulse_chan = atom_getfloatarg(1,argc,argv);
	dest_chan = atom_getfloatarg(2,argc,argv);
    
    impulse->b_samples = buffer_locksamples(impulse->b);
    source->b_samples = buffer_locksamples(source->b);
    dest->b_samples = buffer_locksamples(dest->b);
    
//	post("chans source: %d imp: %d dest: %d", source_chan, impulse_chan, dest_chan);

	if( source_chan <= 0 || impulse_chan <= 0 || dest_chan <= 0){
		error("%s: channels are counted starting from 1",OBJECT_NAME);
		return;
	}
	if( source_chan > source->b_nchans ){
		error("%s: source channel %d out of range", OBJECT_NAME, source_chan);
		return;
	}
	if( impulse_chan > impulse->b_nchans ){
		error("%s: impulse channel %d out of range", OBJECT_NAME, impulse_chan);
		return;
	}
	if( dest_chan > dest->b_nchans ){
		error("%s: dest channel %d out of range", OBJECT_NAME, dest_chan);
		return;
	}
	--source_chan;
	--impulse_chan;
	--dest_chan;
	inframes = source->b_frames;
	outframes = dest->b_frames;
	// initialization routine (move out and only do once)
	for( N2 = 2; N2 < NCMAX; N2 *= 2){
		if( N2 >= impulse->b_frames )
			break;
	}
	N = 2 * N2;
	// be more careful with memory
	// also be sure to clear destination buffer
	
	
	if(! x->static_memory ){
		if ((sbuf = (float *) sysmem_newptrclear((N+2) * sizeof(float))) == NULL)
			error("%s: insufficient memory", OBJECT_NAME);
		if ((tbuf = (float *) sysmem_newptrclear(N2 * sizeof(float))) == NULL)
			error("%s: insufficient memory",OBJECT_NAME);
		if ((filt = (float *) sysmem_newptrclear((N+2) * sizeof(float))) == NULL)
			error("%s: insufficient memory",OBJECT_NAME);
		if( (bitshuffle = (int *) sysmem_newptrclear(N * 2 * sizeof(int))) == NULL)
			error("%s: insufficient memory",OBJECT_NAME);
		if( (trigland = (float *) sysmem_newptrclear(N * 2 * sizeof(float))) == NULL)
			error("%s: insufficient memory",OBJECT_NAME);
	}

	// crashes, but why ???
	x->mult = 1. / (float) N;
	x->last_N = N;
	init_rdft( N, bitshuffle, trigland);
	

	
	for(i = 0, j = 0; i < impulse->b_frames; i+= impulse->b_nchans, j++){
		filt[j] = impulse->b_samples[i + impulse_chan];
	}
		
	rdft( N, 1, filt, bitshuffle, trigland );

	for (i=0; i <= N; i += 2){
		
		a = filt[i];
		b = filt[i + 1];
		temp = a*a + b*b;
		if (temp > max)
			max = temp;
	}
	
	if (max != 0.) {
		max = gain/(sqrt(max));
	} 
	else {
		error("%s: impulse response is all zeros",OBJECT_NAME);
		return;
	}
	// make normalization optional
	for (i=0; i< N+2; i++)
		filt[i] *= max;
	
	ifr_cnt = ofr_cnt = 0;


    
	if(source->b_frames - ifr_cnt >= N2)
		readframes = N2;
	else readframes = source->b_frames - ifr_cnt;
	// read desired channel from multichannel source buffer into sbuf
	for(i = 0, j = ifr_cnt * dest->b_nchans; i < readframes; i++, ifr_cnt++, j += source->b_nchans)
		sbuf[i] = source->b_samples[j + source_chan];
	// zero pad source buffer
	for (i = readframes; i<N+2; i++)
		sbuf[i] = 0.;
	copacetic = 1;

	while( target_frames < source->b_frames + impulse->b_frames ){
		target_frames *= 2;
	}
	//post("src frames + imp frames %d dest frames %d",source->b_frames + impulse->b_frames, dest->b_frames);


    
	if( dest->b_frames < target_frames){
        buffer_unlocksamples(dest->b);
		atom_setfloat(&newsize, (float) target_frames);
		typedmess((void *) x->dest->b, gensym("sizeinsamps"),1, &newsize);
		post("%s: destination buffer was too small and has been resized",OBJECT_NAME);
		convolver_attach_buffers( x );
        dest->b_samples = buffer_locksamples(dest->b);
	}
// 	    return;
	while(copacetic && ofr_cnt < ifr_cnt + N2){
		// post("ofr %d ifr %d, N %d",ofr_cnt, ifr_cnt, N);
		
		// convolve source buffer with filter buffer
		rdft( N, 1, sbuf, bitshuffle, trigland );
		for (i=0; i<=N2; i++) {
			ip = 2*i;
			ip1 = ip + 1;
			a = sbuf[ip] * filt[ip] - sbuf[ip1] * filt[ip1];
			b = sbuf[ip] * filt[ip1] + sbuf[ip1] * filt[ip];
			sbuf[ip] = a;
			sbuf[ip1] = b;
		}
		// inverse fft
		
		rdft( N, -1, sbuf, bitshuffle, trigland );
		
		//accumulate to output buffer
		// denormals fix is in
		for (i=0; i<N2; i++){
			FIX_DENORM_FLOAT(sbuf[i]);
			tbuf[i] += sbuf[i];
		}
		
		// write to msp buffer
		if(dest->b_frames - ofr_cnt >= N2)
			writeframes = N2;
		else {
			writeframes = source->b_frames - ofr_cnt;
			// post("cutting off with N2 %d dest frames - ofr %d", N2, dest->b_frames - ofr_cnt );
			copacetic = 0; // reached end of dest buffer
		}
		//shift samples to desired channel of multichannel output buffer
		for(i = 0, j = ofr_cnt * dest->b_nchans; i < writeframes; i++, ofr_cnt++, j += dest->b_nchans)
			dest->b_samples[j + dest_chan] = tbuf[i];
		
		// shift over remaining convolved samples
		for (i=0; i<N2; i++){
			FIX_DENORM_FLOAT(sbuf[N2 + i]);
			tbuf[i] = sbuf[N2+i];
		}
		// read in next batch
		if(source->b_frames - ifr_cnt >= N2)
			readframes = N2;
		else {
			readframes = source->b_frames - ifr_cnt;
		}
		// arbitrary input channel read
		for(i = 0, j = ifr_cnt * source->b_nchans; i < readframes; i++, ifr_cnt++, j += source->b_nchans)
			sbuf[i] = source->b_samples[j + source_chan];
		// zero pad
		for (i = readframes; i<N+2; i++)
			sbuf[i] = 0.;
	}
	// now normalize output buffer
  	

	// OK 
//	post("first rescale: %f", rescale);
	max = 0.0;
	for(i = 0, j = 0; i < dest->b_frames; i++, j += dest->b_nchans){
		if(max < fabs(dest->b_samples[j + dest_chan]) )
			max = fabs(dest->b_samples[j + dest_chan]);
	}	
	if(max <= 0.0){
		post("convolvesf: zero output");
		return;
	}
	rescale = 1.0 / max;
	// post("max: %f, second rescale: %f", max, rescale);
	
	for(i = 0, j = 0; i < dest->b_frames; i++, j+= dest->b_nchans){
		dest->b_samples[j + dest_chan] *= rescale;
	}
	// FAILED BY HERE
	// post("rescale done");
//	return;

    buffer_unlocksamples(impulse->b);
    buffer_unlocksamples(source->b);
    buffer_unlocksamples(dest->b);
    
	if(! x->static_memory ){
		sysmem_freeptr(sbuf);
		sysmem_freeptr(tbuf);
		sysmem_freeptr(filt);
		sysmem_freeptr(bitshuffle);
		sysmem_freeptr(trigland);
	}
	outlet_bang(x->bang);
	object_method( (t_object *)x->dest->b, gensym("dirty") );
}

void convolver_noiseimp(t_convolver *x, t_floatarg curve)
{
	long b_nchans;
	long b_frames;
	float *b_samples;
	float sr = x->sr;
	int i, j;
	int count;
//	int position;
	float gain, guess;
	float dur;
	float level = 1.0, endLevel = 0.001;
	float grow, a1, a2, b1;

	if(fabs(curve) < 0.001){
		curve = 0.001;
	}
	// let's be current
	convolver_attach_buffers(x);
	b_nchans = x->impulse->b_nchans;
	b_frames = x->impulse->b_frames;
	b_samples = buffer_locksamples(x->impulse->b);
    
	// chan test
	if( sr == 0. ){
		error("zero sample rate");
		return;
	}
	// zero out buffer
	dur = (float) b_frames / sr;
	count = b_frames;
	if(b_frames < 20){
		post("impulse buffer too small!");
		return;
	}

	
//	memset((char *)b_samples, 0, b_nchans * b_frames * sizeof(float));
	// return;
	for( j = 0; j < b_nchans; j++ ){
		level = 1.0;
		endLevel = 0.001;
		grow = exp(curve / (count - 1) );
		a1 = (endLevel - level) / (1.0 - exp(curve));
		a2 = level + a1;
		b1 = a1;	
		for( i = 0; i < b_frames; i++ ){
			guess = boundrand(-1.0, 1.0);
			gain = 1. - guess;

			b1 = b1 * grow;
			level = a2 - b1;
			b_samples[ (i * b_nchans) + j ] = level * guess;
		}
	}
	object_method( (t_object *)x->impulse->b, gensym("dirty") );
    buffer_unlocksamples(x->impulse->b);
	outlet_bang(x->bang);
	
}

void convolver_spikeimp(t_convolver *x, t_floatarg density)
{
	long b_nchans;
	long b_frames;
	float *b_samples;
	float sr = x->sr;
	int i, j;
	int count;
	int position;
	float gain, guess;
	float dur;
	
	// let's be current
    // post("Spiking with density %f", density);
	convolver_attach_buffers(x);
	b_nchans = x->impulse->b_nchans;
	b_frames = x->impulse->b_frames;
	b_samples = buffer_locksamples(x->impulse->b);
	// chan test
	if( sr == 0. ){
		error("zero sample rate");
		return;
	}
	// zero out buffer
	dur = (float) b_frames / sr;
	count = density * dur;
	memset((char *)b_samples, 0, b_nchans * b_frames * sizeof(float));
	// return;
	for( j = 0; j < b_nchans; j++ ){
		for( i = 0; i < count; i++ ){
			guess = boundrand(0., 1.);
			gain = 1. - guess;
			gain = gain * gain;
			if( boundrand(0.0,1.0) > 0.5 ){
				gain = gain * -1.0; // randomly invert signs to remove DC
			}
			position = (int) (dur * guess * guess * sr) * b_nchans + j;
			if( position >= b_frames * b_nchans ){
				error("%d exceeds %d",position, b_frames * b_nchans);
			} else{
				b_samples[ position ] = gain;
			}
		}
	}
	object_method( (t_object *)x->impulse->b, gensym("dirty") );
    buffer_unlocksamples(x->impulse->b);
	outlet_bang(x->bang);
}

float boundrand(float min, float max)
{
	return min + (max-min) * ((float) (rand() % RAND_MAX)/ (float) RAND_MAX);
}


void *convolver_new(t_symbol *msg, short argc, t_atom *argv)
{

    t_convolver *x = (t_convolver *)object_alloc(convolver_class);
	x->bang = bangout((t_pxobject *)x); 
    dsp_setup((t_pxobject *)x,1);	

	srand(time(0)); //need "seed" message
	x->impulse = (t_buffy *)sysmem_newptrclear(sizeof(t_buffy));
	x->source = (t_buffy *)sysmem_newptrclear(sizeof(t_buffy));
	x->dest = (t_buffy *)sysmem_newptrclear(sizeof(t_buffy));
	x->static_memory = 0;
	
	
	// default names
	x->impulse->myname = gensym("impulse_buf");
	x->source->myname = gensym("source_buf");
	x->dest->myname = gensym("dest_buf");
	
	x->last_N = -1;

	atom_arg_getsym(&x->source->myname,0,argc,argv);
	atom_arg_getsym(&x->impulse->myname,1,argc,argv);
	atom_arg_getsym(&x->dest->myname,2,argc,argv);
	convolver_attach_buffers(x); // try
	x->sr = sys_getsr();
    return (x);
}


void convolver_attach_buffers(t_convolver *x) 
{
	convolver_setbuf(x,x->source);
	convolver_setbuf(x,x->impulse);
	convolver_setbuf(x,x->dest);
}


void convolver_setbuf(t_convolver *x, t_buffy *trybuf)
{
	t_buffer_obj *b;

	if (!trybuf->bref)
		trybuf->bref = buffer_ref_new((t_object *)x, trybuf->myname);
	else
		buffer_ref_set(trybuf->bref, trybuf->myname);
    
    b = buffer_ref_getobject(trybuf->bref);
    trybuf->b_valid = 0;
    if(b){
        trybuf->b_frames = buffer_getframecount(b);
        trybuf->b_nchans = buffer_getchannelcount(b);
        trybuf->b_valid = 1;
        trybuf->b = b;
       // post("successfully attached %s", trybuf->myname->s_name);
    } else {
      //  post("could not attach %s", trybuf->myname->s_name);
    }
    
}

void convolver_dsp_free(t_convolver *x)
{
	dsp_free((t_pxobject *)x);
	if( x->static_memory ){
		sysmem_freeptr(x->sbuf);
		sysmem_freeptr(x->tbuf);
		sysmem_freeptr(x->filt);
		sysmem_freeptr(x->bitshuffle);
		sysmem_freeptr(x->trigland);
		outlet_bang(x->bang);
	}
    sysmem_freeptr(x->impulse);
    sysmem_freeptr(x->source);
    sysmem_freeptr(x->dest);
}

void convolver_assist (t_convolver *x, void *b, long msg, long arg, char *dst)
{
	if (msg==1) {
		switch (arg) {
			case 0: sprintf(dst,"(mesages) No Signal Input"); break;
    	}
	} else if (msg==2) {
		switch (arg){
			case 0: sprintf(dst,"(bang) Event Completion Message"); break;
		}  
	}
}


// old FFT stuff

void cfft( float *x, int NC, int forward )

{
	float 	wr,wi,
	wpr,wpi,
	theta,
	scale;
	int 		mmax,
		ND,
		m,
		i,j,
		delta;
	
	// void bitreverse();
	
    ND = NC<<1;
    bitreverse( x, ND );
    for ( mmax = 2; mmax < ND; mmax = delta ) {
		delta = mmax<<1;
		theta = TWOPI/( forward? mmax : -mmax );
		wpr = -2.*pow( sin( 0.5*theta ), 2. );
		wpi = sin( theta );
		wr = 1.;
		wi = 0.;
		for ( m = 0; m < mmax; m += 2 ) {
			register float rtemp, itemp;
			for ( i = m; i < ND; i += delta ) {
				j = i + mmax;
				rtemp = wr*x[j] - wi*x[j+1];
				itemp = wr*x[j+1] + wi*x[j];
				x[j] = x[i] - rtemp;
				x[j+1] = x[i+1] - itemp;
				x[i] += rtemp;
				x[i+1] += itemp;
			}
			wr = (rtemp = wr)*wpr - wi*wpi + wr;
			wi = wi*wpr + rtemp*wpi + wi;
		}
    }
	
	/* scale output */
	
    scale = forward ? 1./ND : 2.;
    { register float *xi=x, *xe=x+ND;
		while ( xi < xe )
			*xi++ *= scale;
    }
}

/* bitreverse places float array x containing N/2 complex values
into bit-reversed order */

void bitreverse( float *x, int N )

{
	float 	rtemp,itemp;
	int 		i,j,
		m;
	
    for ( i = j = 0; i < N; i += 2, j += m ) {
		if ( j > i ) {
			rtemp = x[j]; itemp = x[j+1]; /* complex exchange */
			x[j] = x[i]; x[j+1] = x[i+1];
			x[i] = rtemp; x[i+1] = itemp;
		}
		for ( m = N>>1; m >= 2 && j >= m; m >>= 1 )
			j -= m;
    }
}

