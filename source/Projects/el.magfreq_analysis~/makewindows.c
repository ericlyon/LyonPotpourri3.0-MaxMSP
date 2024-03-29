#include "fftease_oldskool.h"

void makewindows( float *H, float *A, float *S, int Nw, int N, int I )

{
 int i ;
 float sum ;

    for ( i = 0 ; i < Nw ; i++ )
	H[i] = A[i] = S[i] = 0.54 - 0.46*cos( TWOPI*i/(Nw - 1) ) ;

    if ( Nw > N ) {
     float x ;

	x = -(Nw - 1)/2. ;
	for ( i = 0 ; i < Nw ; i++, x += 1. )
	    if ( x != 0. ) {
		A[i] *= N*sin( PI*x/N )/(PI*x) ;
		if ( I )
		    S[i] *= I*sin( PI*x/I )/(PI*x) ;
	    }
    }

    for ( sum = i = 0 ; i < Nw ; i++ )
	sum += A[i] ;

    for ( i = 0 ; i < Nw ; i++ ) {
     float afac = 2./sum ;
     float sfac = Nw > N ? 1./afac : afac ;
	A[i] *= afac ;
	S[i] *= sfac ;
    }

    if ( Nw <= N && I ) {
	for ( sum = i = 0 ; i < Nw ; i += I )
	    sum += S[i]*S[i] ;
	for ( sum = 1./sum, i = 0 ; i < Nw ; i++ )
	    S[i] *= sum ;
    }
}

void makehamming( float *H, float *A, float *S, int Nw, int N, int I, int odd )

{
 int i;
 float sum ;


 
 if (odd) {
    for ( i = 0 ; i < Nw ; i++ )
	  H[i] = A[i] = S[i] = sqrt(0.54 - 0.46*cos( TWOPI*i/(Nw - 1) ));
 }
	
 else {

   for ( i = 0 ; i < Nw ; i++ )
	  H[i] = A[i] = S[i] = 0.54 - 0.46*cos( TWOPI*i/(Nw - 1) );

 }
 	
    if ( Nw > N ) {
     float x ;

	x = -(Nw - 1)/2. ;
	for ( i = 0 ; i < Nw ; i++, x += 1. )
	    if ( x != 0. ) {
		A[i] *= N*sin( PI*x/N )/(PI*x) ;
		if ( I )
		    S[i] *= I*sin( PI*x/I )/(PI*x) ;
	    }
    }
    for ( sum = i = 0 ; i < Nw ; i++ )
	sum += A[i] ;

    for ( i = 0 ; i < Nw ; i++ ) {
     float afac = 2./sum ;
     float sfac = Nw > N ? 1./afac : afac ;
	A[i] *= afac ;
	S[i] *= sfac ;
    }

    if ( Nw <= N && I ) {
	for ( sum = i = 0 ; i < Nw ; i += I )
	    sum += S[i]*S[i] ;
	for ( sum = 1./sum, i = 0 ; i < Nw ; i++ )
	    S[i] *= sum ;
    }
}

  
void makehanning( float *H, float *A, float *S, int Nw, int N, int I, int odd )
{
 int i;
 float sum ;
 
 
 if (odd) {
    for ( i = 0 ; i < Nw ; i++ )
	  H[i] = A[i] = S[i] = sqrt(0.5 * (1. + cos(PI + TWOPI * i / (Nw - 1))));
 }
	
 else {

   for ( i = 0 ; i < Nw ; i++ )
	  H[i] = A[i] = S[i] = 0.5 * (1. + cos(PI + TWOPI * i / (Nw - 1)));

 }
 	
    if ( Nw > N ) {
     float x ;

	x = -(Nw - 1)/2. ;
	for ( i = 0 ; i < Nw ; i++, x += 1. )
	    if ( x != 0. ) {
		A[i] *= N*sin( PI*x/N )/(PI*x) ;
		if ( I )
		    S[i] *= I*sin( PI*x/I )/(PI*x) ;
	    }
    }
    for ( sum = i = 0 ; i < Nw ; i++ )
	sum += A[i] ;

    for ( i = 0 ; i < Nw ; i++ ) {
     float afac = 2./sum ;
     float sfac = Nw > N ? 1./afac : afac ;
	A[i] *= afac ;
	S[i] *= sfac ;
    }

    if ( Nw <= N && I ) {
	for ( sum = i = 0 ; i < Nw ; i += I )
	    sum += S[i]*S[i] ;
	for ( sum = 1./sum, i = 0 ; i < Nw ; i++ )
	    S[i] *= sum ;
    }
}
