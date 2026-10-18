/**
 * @file bashfest_helper.c
 * @brief Math, filter, delay, and wavetable utilities for bashfest~.
 * Modernized for Max 8/9: bounds safety, denormal protection, and division guards.
 */

#include "bashfest.h"
#include <stdlib.h>
#include <math.h>

#define FLUSH_DENORM(v) if (fabsf(v) < 1.0e-15f) (v) = 0.0f

void putsine(float *arr, int len) 
{
    if (!arr || len <= 0) return;
    double twopi = 8.0 * atan2(1.0, 1.0);

    for (int i = 0; i < len; i++) {
        arr[i] = (float)sin(twopi * (double)i / (double)len);
    }
}

float boundrand(float min, float max)
{
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}

void mycombset(float loopt, float rvt, int init, float *a, float srate)
{
    if (!a || srate <= 0.0f) return;
    if (loopt < 1.0f / srate) loopt = 1.0f / srate;

    a[0] = (float)(3.0 + (loopt * srate + 0.5));
    a[1] = rvt;

    if (!init) {
        int end = (int)a[0];
        for (int j = 3; j < end; j++) {
            a[j] = 0.0f;
        }
        a[2] = 3.0f;
    }
}

float mycomb(float samp, float *a)
{
    int limit = (int)a[0];
    int idx = (int)a[2];

    if (idx >= limit || idx < 3) {
        idx = 3;
    }

    float *aptr = a + idx;
    a[2] = (float)(idx + 1);

    float temp = *aptr;
    float next_val = temp * a[1] + samp;
    FLUSH_DENORM(next_val);
    *aptr = next_val;

    return temp;
}

void setweights(float *a, int len)
{
    if (!a || len <= 0) return;

    float sum = 0.0f;
    for (int i = 0; i < len; i++) {
        sum += a[i];
    }

    if (sum <= 0.0f) {
        error("zero odds sum");
        return; // Guard prevents division by zero
    }

    float inv_sum = 1.0f / sum;
    for (int i = 0; i < len; i++) {
        a[i] *= inv_sum;
    }
    for (int i = 1; i < len; i++) {
        a[i] += a[i - 1];
    }
}

void delset2(float *a, int *l, float xmax, float srate)
{
    if (!a || !l || srate <= 0.0f) return;

    *l = 0;
    int size = (int)(xmax * srate + 0.5f) - 1;
    if (size < 2) size = 2; // Must hold at least 2 samples for interpolation
    *(l + 1) = size;

    for (int i = 0; i < size; i++) {
        a[i] = 0.0f;
    }
}

void delput2(float x, float *a, int *l)
{
    int size = *(l + 1);
    if (size <= 0) return;

    int write_idx = *l;
    a[write_idx++] = x;

    if (write_idx >= size) {
        write_idx = 0;
    }
    *l = write_idx;
}

float dliget2(float *a, float wait, int *l, float srate)
{
    int size = *(l + 1);
    if (size <= 0) return 0.0f;
    if (wait < 0.0f) wait = 0.0f;

    float x = wait * srate;
    int i = (int)x;
    float frac = x - (float)i;

    i = *l - i;
    while (i < 0)       i += size;
    while (i >= size)   i -= size;

    int im1 = i - 1;
    if (im1 < 0) im1 += size;

    return a[i] + frac * (a[im1] - a[i]);
}

void butterLopass(float *in, float *out, float cutoff, int frames, int channels, float srate)
{
    float data[8];
    for (int ch = 0; ch < channels; ch++) {
        butset(data);
        lobut(data, cutoff, srate);
        butter_filter(in, out, data, frames, channels, ch);
    }
}

void butterBandpass(float *in, float *out, float center, float bandwidth, int frames, int channels, float srate)
{
    float data[8];
    for (int ch = 0; ch < channels; ch++) {
        butset(data);
        bpbut(data, center, bandwidth, srate);
        butter_filter(in, out, data, frames, channels, ch);
    }
}

void butterHipass(float *in, float *out, float cutoff, int frames, int channels, float srate)
{
    float data[8];
    for (int ch = 0; ch < channels; ch++) {
        butset(data);
        hibut(data, cutoff, srate);
        butter_filter(in, out, data, frames, channels, ch);
    }
}

void butset(float *a)
{
    a[6] = 0.0f;
    a[7] = 0.0f;
}

void lobut(float *a, float cutoff, float SR)
{
    if (SR <= 0.0f) return;
    if (cutoff < 1.0f) cutoff = 1.0f;
    if (cutoff > SR * 0.49f) cutoff = SR * 0.49f;

    float c = 1.0f / (float)tan(PI * cutoff / SR);
    a[1] = 1.0f / (1.0f + (float)ROOT2 * c + c * c);
    a[2] = a[1] + a[1];
    a[3] = a[1];
    a[4] = 2.0f * (1.0f - c * c) * a[1];
    a[5] = (1.0f - (float)ROOT2 * c + c * c) * a[1];
}

void hibut(float *a, float cutoff, float SR)
{
    if (SR <= 0.0f) return;
    if (cutoff < 1.0f) cutoff = 1.0f;
    if (cutoff > SR * 0.49f) cutoff = SR * 0.49f;

    float c = (float)tan(PI * cutoff / SR);
    a[1] = 1.0f / (1.0f + (float)ROOT2 * c + c * c);
    a[2] = -2.0f * a[1];
    a[3] = a[1];
    a[4] = 2.0f * (c * c - 1.0f) * a[1];
    a[5] = (1.0f - (float)ROOT2 * c + c * c) * a[1];
}

void bpbut(float *a, float formant, float bandwidth, float SR)
{
    if (SR <= 0.0f) return;
    if (bandwidth < 1.0f) bandwidth = 1.0f;
    if (formant < 1.0f) formant = 1.0f;
    if (formant > SR * 0.49f) formant = SR * 0.49f;

    float c = 1.0f / (float)tan(PI * bandwidth / SR);
    float d = 2.0f * (float)cos(2.0 * PI * formant / SR);
    a[1] = 1.0f / (1.0f + c);
    a[2] = 0.0f;
    a[3] = -a[1];
    a[4] = -c * d * a[1];
    a[5] = (c - 1.0f) * a[1];
}

void butter_filter(float *in, float *out, float *a, int frames, int channels, int channel)
{
    for (int i = channel; i < frames * channels; i += channels) {
        float t = in[i] - a[4] * a[6] - a[5] * a[7];
        float y = t * a[1] + a[2] * a[6] + a[3] * a[7];

        FLUSH_DENORM(t);
        FLUSH_DENORM(y);

        a[7] = a[6];
        a[6] = t;
        out[i] = y;
    }
}

void rsnset2(float cf, float bw, float scl, float xinit, float *a, float srate)
{
    if (srate <= 0.0f) return;
    if (!xinit) {
        a[3] = 0.0f;
        a[4] = 0.0f;
    }

    a[2] = (float)exp(-PI2 * bw / srate);
    float temp = 1.0f - a[2];
    float c = a[2] + 1.0f;
    a[1] = 4.0f * a[2] / c * (float)cos(PI2 * cf / srate);

    if (scl < 0.0f) {
        a[0] = 1.0f;
    } else if (scl > 0.0f) {
        float arg = temp / c * (c * c - a[1] * a[1]);
        a[0] = (arg > 0.0f) ? sqrtf(arg) : 0.0f;
    } else {
        float arg = 1.0f - a[1] * a[1] / (4.0f * a[2]);
        a[0] = (arg > 0.0f) ? temp * sqrtf(arg) : 0.0f;
    }
}

float reson(float x, float *a)
{
    float temp = a[0] * x + a[1] * a[3] - a[2] * a[4];
    FLUSH_DENORM(temp);
    a[4] = a[3];
    a[3] = temp;
    return temp;
}

float allpass(float samp, float *a)
{
    int limit = (int)a[0];
    int idx = (int)a[STARTM1];

    if (idx >= limit || idx < START) {
        idx = START;
    }

    float *aptr = a + idx;
    a[STARTM1] = (float)(idx + 1);

    float temp = *aptr;
    float next_val = temp * a[1] + samp;
    FLUSH_DENORM(next_val);
    *aptr = next_val;

    return temp - a[1] * next_val;
}

void init_reverb_data(float *a)
{
    a[0] = 2.0f;
    a[1] = -0.61043329f;
    a[2] = -1.4582246f;
    a[3] = 1.0f;
    a[4] = 0.75887003f;
    a[5] = 1.0f;
    a[6] = -0.6922953f;
    a[7] = 0.0f;
    a[8] = 0.0f;
    a[9] = 0.035888535f;
}

void reverb1me(float *in, float *out, int inFrames, int out_frames, int nchans, 
               int channel, float revtime, float dry, LSTRUCT *eel, float **alpo, t_bashfest *x)
{
    float dels[4];
    float a1, a2, a3, a4;
    float *fltdata = x->reverb_ellipse_data;
    int nsects;
    float xnorm;
    float srate = x->sr;

    float wet = cosf(1.570796f * dry);
    dry = sinf(1.570796f * dry);

    for (int i = 0; i < 4; i++) {
        dels[i] = boundrand(0.005f, 0.1f);
        if (dels[i] > x->max_mini_delay * 0.98f) {
            dels[i] = x->max_mini_delay * 0.98f;
        }
        mycombset(dels[i], revtime, 0, alpo[i], srate);
    }
    ellipset(fltdata, eel, &nsects, &xnorm);

    for (int i = channel; i < inFrames * nchans; i += nchans) {
        a1 = allpass(in[i], alpo[0]);
        a2 = allpass(in[i], alpo[1]);
        a3 = allpass(in[i], alpo[2]);
        a4 = allpass(in[i], alpo[3]);

        out[i] = in[i] * dry + ellipse(a1 + a2 + a3 + a4, eel, nsects, xnorm) * wet;
    }

    // Reverb tail
    for (int i = channel + inFrames * nchans; i < out_frames * nchans; i += nchans) {
        a1 = allpass(0.0f, alpo[0]);
        a2 = allpass(0.0f, alpo[1]);
        a3 = allpass(0.0f, alpo[2]);
        a4 = allpass(0.0f, alpo[3]);

        out[i] = ellipse(a1 + a2 + a3 + a4, eel, nsects, xnorm) * wet;
    }
}

void feed1(float *inbuf, float *outbuf, int in_frames, int out_frames, int channels, float *functab1,
           float *functab2, float *functab3, float *functab4, int funclen,
           float duration, float maxDelay, float **mini_delay, t_bashfest *x)
{
    float srate = x->sr;
    float *delayLine1L = mini_delay[0];
    float *delayLine2L = mini_delay[1];
    float *delayLine1R = mini_delay[2];
    float *delayLine2R = mini_delay[3];
    int dv1L[2], dv2L[2], dv1R[2], dv2R[2];
    float delsamp1L = 0.0f, delsamp2L = 0.0f, delsamp1R = 0.0f, delsamp2R = 0.0f;
    float delay1, delay2, feedback1, feedback2;
    float funcSi, funcPhs = 0.0f;
    float putsamp;

    if (duration <= 0.0f || srate <= 0.0f) return;
    funcSi = ((float)funclen / srate) / duration;

    delset2(delayLine1L, dv1L, maxDelay, srate);
    delset2(delayLine2L, dv2L, maxDelay, srate);
    if (channels == 2) {
        delset2(delayLine1R, dv1R, maxDelay, srate);
        delset2(delayLine2R, dv2R, maxDelay, srate);
    }

    for (int i = 0; i < out_frames; i++) {
        int fidx = (int)funcPhs;
        if (fidx >= funclen) fidx = funclen - 1;
        if (fidx < 0) fidx = 0;

        delay1    = functab1[fidx];
        delay2    = functab2[fidx];
        feedback1 = functab3[fidx];
        feedback2 = functab4[fidx];

        // Left Channel
        float insampL = (i < in_frames) ? inbuf[i * channels] : 0.0f;
        FLUSH_DENORM(delsamp1L);
        putsamp = insampL + delsamp1L * feedback1;
        delput2(putsamp, delayLine1L, dv1L);
        delsamp1L = dliget2(delayLine1L, delay1, dv1L, srate);

        FLUSH_DENORM(delsamp2L);
        putsamp = delsamp1L + delsamp2L * feedback2;
        delput2(putsamp, delayLine2L, dv2L);
        delsamp2L = dliget2(delayLine2L, delay2, dv2L, srate);
        outbuf[i * channels] = insampL + delsamp2L;

        // Right Channel
        if (channels == 2) {
            float insampR = (i < in_frames) ? inbuf[i * 2 + 1] : 0.0f;
            FLUSH_DENORM(delsamp1R);
            putsamp = insampR + delsamp1R * feedback1;
            delput2(putsamp, delayLine1R, dv1R);
            delsamp1R = dliget2(delayLine1R, delay1, dv1R, srate);

            FLUSH_DENORM(delsamp2R);
            putsamp = delsamp1R + delsamp2R * feedback2;
            delput2(putsamp, delayLine2R, dv2R);
            delsamp2R = dliget2(delayLine2R, delay2, dv2R, srate);
            outbuf[i * 2 + 1] = insampR + delsamp2R;
        }

        funcPhs += funcSi;
        if (funcPhs >= (float)funclen) funcPhs -= (float)funclen;
    }
}

void setflamfunc1(float *arr, int flen)
{
    if (!arr || flen <= 0) return;
    for (int i = 0; i < flen; i++) {
        float x = (float)i / (float)flen;
        arr[i] = ((x - 1.0f) / (x + 1.0f)) * -1.0f;
    }
}

void setExpFlamFunc(float *arr, int flen, float v1, float v2, float alpha)
{
    if (!arr || flen <= 1) return;
    if (fabsf(alpha) < 1.0e-7f) alpha = 1.0e-7f;

    float denom = 1.0f - expf(alpha);
    for (int i = 0; i < flen; i++) {
        float numer = 1.0f - expf((float)i * alpha / (float)(flen - 1));
        arr[i] = v1 + (v2 - v1) * (numer / denom);
    }
}

void funcgen1(float *outArray, int outlen, float duration, float outMin, float outMax,
              float speed1, float speed2, float gain1, float gain2, float *phs1, float *phs2, 
              float *sine, int sinelen)
{
    if (!outArray || outlen <= 0 || duration <= 0.0f) return;

    float localSR = duration * (float)outlen;
    if (localSR <= 0.0f) return;

    *phs1 *= (float)sinelen;
    *phs2 *= (float)sinelen;
    float si1 = ((float)sinelen / localSR) * speed1;
    float si2 = ((float)sinelen / localSR) * speed2;

    for (int i = 0; i < outlen; i++) {
        outArray[i] = oscil(gain1, si1, sine, sinelen, phs1);
        outArray[i] += oscil(gain2, si2, sine, sinelen, phs2);
    }
    normtab(outArray, outArray, outMin, outMax, outlen);
}

void normtab(float *inarr, float *outarr, float min, float max, int len)
{
    if (!inarr || !outarr || len <= 0) return;

    float imin = inarr[0];
    float imax = inarr[0];

    for (int i = 1; i < len; i++) {
        if (inarr[i] < imin) imin = inarr[i];
        if (inarr[i] > imax) imax = inarr[i];
    }
    for (int i = 0; i < len; i++) {
        outarr[i] = mapp(inarr[i], imin, imax, min, max);
    }
}

float mapp(float in, float imin, float imax, float omin, float omax)
{
    float range = imax - imin;
    if (fabsf(range) < 1.0e-9f) {
        return omin; // Prevents division by zero when min == max
    }
    return omin + ((omax - omin) * ((in - imin) / range));
}

float oscil(float amp, float si, float *farray, int len, float *phs)
{
    if (!farray || len <= 0) return 0.0f;

    while (*phs >= (float)len) *phs -= (float)len;
    while (*phs < 0.0f)        *phs += (float)len;

    int i = (int)(*phs);
    if (i < 0) i = 0;
    else if (i >= len) i = len - 1;

    *phs += si;
    return farray[i] * amp;
}

void killdc(float *inbuf, int in_frames, int channels, LSTRUCT *eel, t_bashfest *x)
{
    if (!inbuf || in_frames <= 0 || channels <= 0) return;
    int nsects;
    float xnorm;
    float *dcflt = x->dcflt;

    for (int j = 0; j < channels; j++) {
        ellipset(dcflt, eel, &nsects, &xnorm);

        for (int i = j; i < in_frames * channels; i += channels) {
            float s = ellipse(inbuf[i], eel, nsects, xnorm);
            FLUSH_DENORM(s);
            inbuf[i] = s;
        }
    }
}

void set_dcflt(float *a)
{
    if (!a) return;
    a[0]  = 3.0f;
    a[1]  = -1.9999924f;
    a[2]  = -1.9992482f;
    a[3]  = 1.0f;
    a[4]  = 0.99928019f;
    a[5]  = -1.9999956f;
    a[6]  = -1.996408f;
    a[7]  = 1.0f;
    a[8]  = 0.99645999f;
    a[9]  = -1.9999994f;
    a[10] = -1.9805074f;
    a[11] = 1.0f;
    a[12] = 0.98069401f;
    a[13] = 0.98817413f;
}

void set_distortion_table(float *arr, float cut, float max, int len)
{
    if (!arr || len <= 1) return;
    int len2 = len >> 1;

    for (int i = len2; i < len; i++) {
        float samp = (float)(i - len2) / (float)len2;
        if (samp > cut) {
            samp = mapp(samp, cut, 1.0f, cut, max);
        }
        arr[i] = samp;
    }
    for (int i = 0; i < len2; i++) {
        arr[i] = -arr[len - (i + 1)];
    }
}

float dlookup(float samp, float *arr, int len)
{
    if (!arr || len <= 0) return 0.0f;

    int idx = (int)(((samp + 1.0f) * 0.5f) * (float)len);
    if (idx < 0) idx = 0;
    if (idx >= len) idx = len - 1;
    return arr[idx];
}

void do_compdist(float *in, float *out, int sampFrames, int nchans, int channel, 
                 float cutoff, float maxmult, int lookupflag, float *table, int range, float bufMaxamp)
{
    if (!in || !out || sampFrames <= 0) return;

    if (bufMaxamp > 0.0f) {
        float inv_maxamp = 1.0f / bufMaxamp;
        for (int i = channel; i < sampFrames * nchans; i += nchans) {
            out[i] = dlookup(in[i] * inv_maxamp, table, range);
        }
    } else {
        for (int i = channel; i < sampFrames * nchans; i += nchans) {
            out[i] = 0.0f;
        }
    }
}

float getmaxamp(float *arr, int len) 
{
    if (!arr || len <= 0) return 0.0f;

    float max = 0.0f;
    for (int i = 0; i < len; i++) {
        float val = fabsf(arr[i]);
        if (val > max) max = val;
    }
    return max;
}

void buildadsr(CMIXADSR *a)
{
    if (!a || !a->func || a->len <= 0) return;

    float A = a->a;
    float D = a->d;
    float S = a->s;
    float R = a->r;
    float f1 = a->v1;
    float f2 = a->v2;
    float f3 = a->v3;
    float f4 = a->v4;

    int funclen = a->len;
    float *func = a->func;
    int segs[4];

    float total = A + D + S + R;
    if (total <= 0.0f) {
        for (int i = 0; i < 4; i++) {
            segs[i] = funclen / 4;
        }
        segs[3] = funclen - (segs[0] + segs[1] + segs[2]);
    } else {
        float inv_total = (float)funclen / total;
        segs[0] = (int)(A * inv_total);
        segs[1] = (int)(D * inv_total);
        segs[2] = (int)(S * inv_total);
        segs[3] = funclen - (segs[0] + segs[1] + segs[2]);
    }

    if (f1 > 20000.0f || f1 < -20000.0f) f1 = 250.0f;
    if (f2 > 20000.0f || f2 < -20000.0f) f2 = 1250.0f;
    if (f3 > 20000.0f || f3 < -20000.0f) f3 = 950.0f;
    if (f4 > 20000.0f || f4 < -20000.0f) f4 = f1;

    for (int i = 0; i < 4; i++) {
        if (segs[i] <= 0) segs[i] = 1;
    }

    int ipoint = 0;

    // Segment 1 (Attack)
    for (int i = 0; i < segs[0] && ipoint < funclen; i++, ipoint++) {
        float m1 = 1.0f - (float)i / (float)segs[0];
        func[ipoint] = f1 * m1 + f2 * (1.0f - m1);
    }

    // Segment 2 (Decay)
    for (int i = 0; i < segs[1] && ipoint < funclen; i++, ipoint++) {
        float m1 = 1.0f - (float)i / (float)segs[1];
        func[ipoint] = f2 * m1 + f3 * (1.0f - m1);
    }

    // Segment 3 (Sustain)
    for (int i = 0; i < segs[2] && ipoint < funclen; i++, ipoint++) {
        func[ipoint] = f3;
    }

    // Segment 4 (Release)
    for (int i = 0; i < segs[3] && ipoint < funclen; i++, ipoint++) {
        float m1 = 1.0f - (float)i / (float)segs[3];
        func[ipoint] = f3 * m1 + f4 * (1.0f - m1);
    }

    // Fill any remainder
    while (ipoint < funclen) {
        func[ipoint++] = f4;
    }
}