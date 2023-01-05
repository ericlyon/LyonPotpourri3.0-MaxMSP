#include "chameleon.h"
#include "stdlib.h"

void putsine (double *arr, long len)
{
	long i;
	double twopi;
	twopi = 8.0 * atan2(1.,1.);
	for ( i = 0; i < len ; i++) {
		*(arr + i) = sin(twopi * i / len);
	}
}


float boundrand(float min, float max)
{
	return min + (max-min) * ((float)rand()/MY_MAX);
}


void mycombset(double loopt,double rvt,int init,double *a,double srate)
{
	int j;
	
	a[0] =  (3.0 + (loopt * srate + .5));
	a[1] = rvt;
	if(!init) {
        for(j=3; j<(int)*a; j++) {
			a[j] = 0;
        }
		a[2] = 3;
	}
}

double mycomb(double samp,double *a)
{
	double temp,*aptr;
	if ( a[2] >= (int) a[0]) 
		a[2] = 3;
	aptr = a + (int)a[2];
	a[2]++; 
	temp = *aptr;
	*aptr = *aptr * a[1] + samp;
	return(temp);
}

void setweights(float *a, int len)
{
	float sum = 0.0;
	int i;
	for(i=0;i<len;i++)
		sum += a[i];
	if(sum == 0.0){
		error("zero odds sum");
	}
	for(i=0;i<len;i++)
		a[i] /= sum;
	for(i=1;i<len;i++)
		a[i] += a[i-1];
}

// delset2(x->slidecomb_units[i].delayline1, x->slidecomb_units[i].dv1, MAX_SLIDECOMB_DELAY, x->sr);

void  delset2(double *a,int *l,double xmax, double srate)
{
	/* delay initialization.  a is address of float array, l is size-2 int 
	 * array for bookkeeping variables, xmax, is maximum expected delay */
	
	int i;
    /*
    if( ((int)(xmax * srate + .5) - 1) >  (srate * BENDY_MAXDEL) ) {
        post("chameleon~ delset2 error: %d is too big, compared to %d. Resetting delay length.", (int)(xmax * srate + .5) - 1, (int)(srate * BENDY_MAXDEL) );
        xmax = BENDY_MAXDEL;
    }
    */
	*l = 0;
	// *(l+1) = (int)(xmax * srate + .5);
	// attempted protection:
	*(l+1) = (int)(xmax * srate + .5) - 1;
	for(i = 0; i < *(l+1); i++) *(a+i) = 0;
}

void  delset3(double *a,int *l,double xmax,double srate, double alloc_max)
{
    /* delay initialization.  a is address of float array, l is size-2 int
     * array for bookkeeping variables, xmax, is maximum expected delay */
    
    int i;
    
    if( xmax > alloc_max ) {
        post("chameleon~ delset3 error: %d is too big, compared to %d. Resetting delay length.", xmax, alloc_max );
        xmax = alloc_max;
    }
    l[0] = 0;
    // *(l+1) = (int)(xmax * srate + .5);
    // attempted protection:
    l[1] = (int)(xmax * srate + .5) - 1;
    for(i = 0; i < l[1]; i++) *(a+i) = 0;
}


void delput2(double x,double *a,int *l)
{
	
	/* put value in delay line. See delset. x is float */
	
    if( ((*l) >= 0)  && ((*l) < *(l+1)) ){
        *(a + (*l)++) = x;
    }
    while(*(l) >= *(l+1)){
        *l -= *(l+1);
    }
}                                                            

float dliget2(double *a,double wait,int *l,double srate)
{
	/* get interpolated value from delay line, wait seconds old */
	int im1;
	float x = wait * srate;
	int i = x;
	float frac = x - i; // this could be the bug
	i = *l - i;
	im1 = i - 1;
	if(i <= 0) { 
		if(i < 0) i += *(l+1);
		if(i < 0) return(0.);
		if(im1 < 0) im1 += *(l+1);
	}
	return(*(a+i) + frac * (*(a+im1) - *(a+i)));
}

void butset(float *a)		
{
	a[6] = a[7] = 0.0;
}

void lobut(double *a, double cutoff, double SR)
{
	register float	 c;
	
	c = 1.0 / tan( PI * cutoff / SR);
	a[1] = 1.0 / ( 1.0 + ROOT2 * c + c * c);
	a[2] = a[1] + a[1];
	a[3] = a[1];
	a[4] = 2.0 * ( 1.0 - c*c) * a[1];
	a[5] = ( 1.0 - ROOT2 * c + c * c) * a[1];
    a[6] = a[7] = 0.0;
}

void hibut(double *a, double cutoff, double SR)
{
	register float	c;
	
	c = tan( PI * cutoff / SR);
	a[1] = 1.0 / ( 1.0 + ROOT2 * c + c * c);
	a[2] = -2.0 * a[1];
	a[3] = a[1];
	a[4] = 2.0 * ( c*c - 1.0) * a[1];
	a[5] = ( 1.0 - ROOT2 * c + c * c) * a[1];
    a[6] = a[7] = 0.0;
	
}

void bpbut(double *a, double formant, double bandwidth, double SR)
{
	register float  c, d;
	
	c = 1.0 / tan( PI * bandwidth / SR);
	d = 2.0 * cos( 2.0 * PI * formant / SR);
	a[1] = 1.0 / ( 1.0 + c);
	a[2] = 0.0;
	a[3] = -a[1];
	a[4] = - c * d * a[1];
	a[5] = ( c - 1.0) * a[1];
    a[6] = a[7] = 0.0;
}
/* in array can == out array */

void butter_filter(double *buf, double *a, long frames)
{
	
	int i;
	double t,y;
	for(i = 0 ; i < frames; i++) {
		t = *(buf + i) - a[4] * a[6] - a[5] * a[7];
		y = t * a[1] + a[2] * a[6] + a[3] * a[7];
		a[7] = a[6];
		a[6] = t;
		*(buf + i) = y;
    }
}

void rsnset2(double cf,double bw,double scl,double xinit,double *a,double srate)
{
	double c,temp;
	if(!xinit) {
		a[4] = 0;
		a[3] = 0;
	}
	a[2] = exp(-PI2 * bw/srate);
	temp = 1. - a[2];
	c = a[2] + 1;
	a[1] = 4. * a[2]/c * cos(PI2 * cf/srate);
	if(scl < 0) a[0] = 1;
	if(scl) a[0] = sqrt(temp/c*(c*c-a[1]*a[1]));
	if(!scl) a[0] = temp*sqrt(1.-a[1]*a[1]/(4.*a[2]));
}

double reson(double x,double *a)
{
	double temp;
	temp = *a * x + *(a+1) * *(a+3) - *(a+2) * *(a+4);
	*(a+4) = *(a+3);
	*(a+3) = temp;
	return(temp);
}

double allpass(double samp,double *a)
{
	double temp,*aptr;
	if ( a[STARTM1] >= (int) a[0]) a[STARTM1] = START;
	aptr = a + (int)a[STARTM1];
	a[STARTM1] ++; 
	temp = *aptr;
	*aptr = *aptr * a[1] + samp;
	return(temp - a[1] * *aptr);
}

void init_reverb_data(double *a)
{
	a[0] = 2;
	a[1] = -0.61043329;
	a[2] = -1.4582246;
	a[3] = 1;
	a[4] = 0.75887003;
	a[5] = 1;
	a[6] = -0.6922953;
	a[7] = 0;
	a[8] = 0;
	a[9] = 0.035888535;
}
/*
void reverb1me(double *buf1, double *buf2, double revtime, double dry, t_chameleon *x)
{
	float dels[4];// stick into main structure
	float **alpo = x->mini_delay;
	float a1,a2,a3,a4;
	int i;
	//  int alsmp ;
	float *fltdata = x->reverb_ellipse_data;
	
	int nsects;
	float xnorm;
	LSTRUCT *eel = x->eel;
	
	float wet;
	//  float max;
	float srate = x->sr;
	//  float max_del = x->max_mini_delay ;
	
	wet = cos(1.570796 * dry);
	dry = sin(1.570796 * dry);
	
	// combset uses reverb time , mycombset uses feedback
	for( i = 0; i < 4; i++ ){
		dels[i] = boundrand(.005, .1 );
		if(dels[i] < .005 || dels[i] > 0.1) {
			post("reverb1: bad random delay time: %f",dels[i]);
			dels[i] = .05;
		}
		mycombset(dels[i], revtime, 0, alpo[i], srate);
	}
	
	ellipset(fltdata,eel,&nsects,&xnorm); 
	
	for( i = channel ; i < inFrames * nchans; i += nchans ){
		
		a1 = allpass(in[i], alpo[0]);
		a2 = allpass(in[i], alpo[1]);
		a3 = allpass(in[i], alpo[2]);
		a4 = allpass(in[i], alpo[3]); 
		
		
		out[i] = in[i] * dry + ellipse((a1+a2+a3+a4), eel, nsects,xnorm) * wet;
	}
	
	for( i = channel + inFrames * nchans; i < out_frames * nchans; i += nchans ){
		
		a1 = allpass(0.0, alpo[0]);
		a2 = allpass(0.0, alpo[1]);
		a3 = allpass(0.0, alpo[2]);
		a4 = allpass(0.0, alpo[3]); 
		
		out[i] =  ellipse((a1+a2+a3+a4), eel, nsects,xnorm) * wet;
		
	}
	
}

void feed1(double *inbuf, double *outbuf, int in_frames, int out_frames,int channels, double *functab1,
		   double *functab2,double *functab3,double *functab4,int funclen,
		   double duration, double maxDelay, t_chameleon *x)
{
	int i;
	double srate = x->sr;

	double *delayLine1a = x->mini_delay[0];
	double *delayLine2a = x->mini_delay[1];
	double *delayLine1b = x->mini_delay[2];
	double *delayLine2b = x->mini_delay[3];
	int dv1a[2], dv2a[2];
	int dv1b[2], dv2b[2];
	double delsamp1a=0, delsamp2a=0 ;
	double delsamp1b=0, delsamp2b=0 ;
	double delay1, delay2, feedback1, feedback2;
	double funcSi, funcPhs;
	double putsamp;

	
	funcPhs = 0.;
	
	// read once during note
	
	
	funcSi = ((float) funclen / srate) / duration ;
	
	
	delset2(delayLine1a, dv1a, maxDelay,srate);
	delset2(delayLine2a, dv2a, maxDelay,srate);
	
	if( channels == 2 ){
		delset2(delayLine1b, dv1b, maxDelay,srate);
		delset2(delayLine2b, dv2b, maxDelay,srate);
	}
	
	
	for(i = 0; i < out_frames*channels; i += channels ){
		// buffer loop 
		
		delay1 = functab1[ (int) funcPhs ];
		delay2 = functab2[ (int) funcPhs ];
		feedback1 = functab3[ (int) funcPhs ];
		feedback2 = functab4[ (int) funcPhs ];
		
		funcPhs += funcSi;
		if( funcPhs >= (float) funclen )
			funcPhs = 0;
		
		putsamp = i < in_frames * channels ? inbuf[i] + delsamp1a*feedback1 : 0.0;
		outbuf[i] = putsamp; // zero instead ??
		
		delput2( putsamp, delayLine1a, dv1a);
		delsamp1a = dliget2(delayLine1a, delay1, dv1a,srate);
		
		putsamp = delsamp1a+delsamp2a*feedback2 ;
		
		delput2( putsamp, delayLine2a, dv2a);
		delsamp2a = dliget2(delayLine2a, delay2, dv2a, srate);
		outbuf[i] += delsamp2a;
		
		
		if( channels == 2 ){
			putsamp = i < in_frames * channels ? inbuf[i+1] + delsamp1a*feedback1 : 0.0;
			outbuf[i+1] = putsamp;
			delput2( putsamp, delayLine1b, dv1b);
			delsamp1b = dliget2(delayLine1b, delay1, dv1b, srate);
			putsamp = delsamp1b+delsamp2b*feedback2;
			delput2( putsamp, delayLine2b, dv2b);
			delsamp2b = dliget2(delayLine2b, delay2, dv2b, srate);
			outbuf[i+1] += delsamp2b;
		}
		
	}
	
}
*/
void setflamfunc1(float *arr, int flen)
{
	int i;
	float x;
	for ( i = 0; i < flen; i++){
		x = (float)i / (float) flen ;
		*(arr + i) = ((x - 1) / (x + 1)) * -1.  ;
		
	}
}


void setExpFlamFunc(float *arr, int flen, float v1,float v2,float alpha)
{
	int i;
//	double exp();
	
	if( alpha == 0 )
		alpha = .00000001 ;
	
	for ( i = 0; i < flen; i++){
		*(arr + i) = v1 + (v2-v1) * ((1-exp((float)i*alpha/((float)flen-1.)))/(1-exp(alpha)));
	}
}

void funcgen1(double *outArray, int outlen, double duration, double outMin, double outMax,
			  double speed1, double speed2, double gain1, double gain2, double *phs1, double *phs2,
			  double *sine, int sinelen)
{
	double si1, si2;
	double localSR;
	int i;
	
    // distrust all of these parameters!
	localSR = duration * (double) outlen ;
	*phs1 *= (double) sinelen;
	*phs2 *= (double) sinelen;
	si1 = ((double)sinelen/localSR)  * speed1;
	si2 = ((double)sinelen/localSR)  * speed2;
	
    // crash situation  - maybe outArray is too short...
	for( i = 0; i < outlen; i++ ){
		*(outArray + i) = oscil(gain1, si1, sine, sinelen, phs1) ; // this is a crasher
		*(outArray + i) += oscil(gain2, si2, sine, sinelen, phs2) ;
	}
	normtab( outArray, outArray, outMin, outMax, outlen);
}

void normtab(double *inarr,double *outarr, double min, double max, int len)
{
	int i;
	
	float imin=9999999999., imax=-9999999999.;
	
	for(i = 0; i < len ; i++){
		if( imin > inarr[i] ) 
			imin = inarr[i];
		if( imax < inarr[i] ) 
			imax = inarr[i];
	}
	for(i = 0; i < len; i++ )
		outarr[i] = mapp(inarr[i], imin, imax, min, max);
	
}

double mapp(double in,double imin,double imax,double omin,double omax)
{
	if( imax == 0.0 )
    {
		return 0.0 ;
    } else if(imax == imin){
        return imin;
    }
	return( omin+((omax-omin)*((in-imin)/(imax-imin))) );
}

double oscil(double amp,double si,double *farray,int len,double *phs)
{
	register int i =  *phs;   
	*phs += si;            
    while(*phs >= len){
		*phs -= len;
    }
    while(*phs < 0.){
        *phs += len;
    }
    if(i < 0){
        i = 0;
    } else if(i >= len){
        i = len - 1;
    }
	return(*(farray+i) * amp);
}

void set_dcflt(double *a)
{
	a[0] = 3;
	a[1] = -1.9999924;
	a[2] = -1.9992482;
	a[3] = 1;
	a[4] = 0.99928019;
	a[5] = -1.9999956;
	a[6] = -1.996408;
	a[7] = 1;
	a[8] = 0.99645999;
	a[9] = -1.9999994;
	a[10] = -1.9805074;
	a[11] = 1;
	a[12] = 0.98069401;
	a[13] = 0.98817413;
}

void set_distortion_table(double *arr, double cut, double max, int len)
{
	int i, j, len2;
	double samp;
	
    len2 = len / 2; // len>>1 ;
    // clean array;
    
    for(i = 0; i < len; i++){
        arr[i] = 0.0;
    }
    // first half
	for( i = len2; i < len; i++ ){
		samp = (double)(i - len2) / (double) len2 ;
        if( samp > cut ){
			samp = mapp( samp, cut, 1.0,  cut, max );
        }
		arr[i] = samp;
	}
	for( i = len2 - 1, j = len2; i > 0; i--, j++ )
		arr[i] = arr[j] * -1.0;
}

double dlookup(double samp,double *arr,int len)
{
    double raw = (samp + 1.0) / 2.0;
    double scaled = raw * (double) len;
    int index = floor(scaled);
    if( index > len - 1) {
        index = len - 1;
    } else if( index < 0 ){
        index = 0;
    }
	return arr[index];
}

void do_compdist(double *in,double *out,int sampFrames,int nchans,int channel,
				 double cutoff,double maxmult,int lookupflag,double *table,int range,double bufMaxamp)
{
	
	int i;
	
	float rectsamp;
	
	for( i = channel ; i < sampFrames * nchans; i+= nchans )
    {
		
		if( lookupflag){
			*(out + i) = dlookup( *(in + i)/bufMaxamp, table, range );
		} else {
			rectsamp = fabs( *(in + i) ) / bufMaxamp;
			if( rectsamp > cutoff ){
				*(in + i) = *(out + i) * 
				mapp( rectsamp, cutoff, 1.0, cutoff, maxmult);
			}
		}
    }
}

double getmaxamp(double *arr, int len)
{
	int i;
	double max = 0;
	
	for(i = 0; i < len; i++ ){
		if( fabs(arr[i]) > max )
			max = fabs(arr[i]);
	}
	return max;
}

void buildadsr(CMIXADSR *a)
{
	double A = a->a;
	double D = a->d;
	double S = a->s;
	double R = a->r;
	double f1 = a->v1;
	double f2 = a->v2;
	double f3 = a->v3;
	double f4 = a->v4;
	
	int funclen = a->len;
	double *func = a->func;
	double total;
	int ipoint = 0;
	int i;
	int segs[4];
	double m1,m2;
	total = A + D + S + R ;
	
	segs[0] = (A/total) * funclen;
	segs[1] = (D/total) * funclen;
	segs[2] = (S/total) * funclen;
	segs[3] = funclen - (segs[0]+segs[1]+segs[2]);
	
	if( f1 > 20000. || f1 < -20000. ){
		f1 = 250.0;
	}
	if( f2 > 20000. || f2 < -20000. ){
		f2 = 1250.0;
	}
	if( f3 > 20000. || f3 < -20000. ){
		f3 = 950.0;
	}
	if( f4 > 20000. || f4 < -20000. ){
		f4 = f1;
	}
	
	if( segs[0] <= 0 || segs[1] <= 0 || segs[2] <= 0 || segs[3] <= 0 ){
		
		for( i = 0; i < 4; i++ ){
			segs[i] = funclen / 4;
		}
	}
	
	for( i = 0 ; i < segs[0]; i++ ){
		m1 = 1.-(float)i/(float)(segs[0]);
		m2 = 1. - m1;
		*(func +i ) = f1 * m1 + f2 * m2;
	}
	ipoint = i;
	
	for( i = 0 ; i < segs[1]; i++ ){
		m1 = 1.-(float)i/(float)(segs[1]);
		m2 = 1. - m1;
		*(func + i + ipoint) = f2 * m1 + f3 * m2;
	}
	ipoint += i;
	
	for( i = 0 ; i < segs[2]; i++ ){
		m1 = 1.-(float)i/(float)(segs[2]);
		m2 = 1. - m1;
		*(func + i + ipoint) = f3;
	}
	ipoint += i;
	
	for( i = 0 ; i < segs[3]; i++ ){
		m1 = 1.-(float)i/(float)(segs[3]);
		m2 = 1. - m1;
		*(func + ipoint + i) = f3 * m1 + f4 * m2;
	}
	ipoint += i;
	
}

