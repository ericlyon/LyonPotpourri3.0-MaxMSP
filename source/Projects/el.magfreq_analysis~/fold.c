#include "fftease_oldskool.h"

/*
 * multiply current input I by window W (both of length Nw);
 * using modulus arithmetic, fold and rotate windowed input
 * into output array O of (FFT) length N according to current
 * input time n
 */
void fold( float *I, float *W, int Nw, float *O, int N, int n )

{
 
    int i;

    for ( i = 0; i < N; i++ )
	O[i] = 0.;

    while ( n < 0 )
      	n += N;
    n %= N;
    for ( i = 0; i < Nw; i++ ) {
	      O[n] += I[i]*W[i];
      	if ( ++n == N )
	      n = 0;
    }
}

