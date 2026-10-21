/**
 * @file convert.c
 * @brief Phase vocoder cartesian-to-polar conversion and instantaneous frequency analysis.
 * Modernized for Max 8/9 with single-precision math intrinsics and standards conformance.
 */

#include "fftease_oldskool.h"
#include <math.h>

void convert(float *S, float *C, int N2, float *lastphase, float fundamental, float factor)
{
    float phase, phasediff;
    int real, imag, amp, freq;
    float a, b;

    for (int i = 0; i <= N2; i++) {
        imag = freq = (real = amp = i << 1) + 1;
        a = (i == N2 ? S[1] : S[real]);
        b = (i == 0 || i == N2 ? 0.0f : S[imag]);

        C[amp] = hypotf(a, b);
        if (C[amp] == 0.0f) {
            phasediff = 0.0f;
        } else {
            phase = -atan2f(b, a);
            phasediff = phase - lastphase[i];
            lastphase[i] = phase;

            while (phasediff > (float)PI)  phasediff -= (float)TWOPI;
            while (phasediff < -(float)PI) phasediff += (float)TWOPI;
        }
        C[freq] = phasediff * factor + (float)i * fundamental;
    }
}