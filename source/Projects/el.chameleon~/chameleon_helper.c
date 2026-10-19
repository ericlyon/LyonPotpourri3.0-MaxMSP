/**
 * @file chameleon_helper.c
 * @brief Math, filter, delay, and wavetable utilities for chameleon~.
 * Modernized for Max 8/9 with denormal protection and division guards.
 */

#include "chameleon.h"
#include <stdlib.h>
#include <math.h>

void putsine(double *arr, long len)
{
    if (!arr || len <= 0) return;
    double twopi = 8.0 * atan2(1.0, 1.0);
    for (long i = 0; i < len; i++) {
        arr[i] = sin(twopi * (double)i / (double)len);
    }
}

float boundrand(float min, float max)
{
    return min + (max - min) * ((float)rand() / (float)RAND_MAX);
}

void mycombset(double loopt, double rvt, int init, double *a, double srate)
{
    if (!a || srate <= 0.0) return;
    if (loopt < 1.0 / srate) loopt = 1.0 / srate;

    a[0] = 3.0 + (loopt * srate + 0.5);
    a[1] = rvt;
    if (!init) {
        int end = (int)a[0];
        for (int j = 3; j < end; j++) {
            a[j] = 0.0;
        }
        a[2] = 3.0;
    }
}

double mycomb(double samp, double *a)
{
    int limit = (int)a[0];
    int idx = (int)a[2];
    if (idx >= limit || idx < 3) {
        idx = 3;
    }

    double *aptr = a + idx;
    a[2] = (double)(idx + 1);

    double temp = *aptr;
    double next_val = temp * a[1] + samp;
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
        return;
    }
    float inv = 1.0f / sum;
    for (int i = 0; i < len; i++) {
        a[i] *= inv;
    }
    for (int i = 1; i < len; i++) {
        a[i] += a[i - 1];
    }
}

void delset2(double *a, int *l, double xmax, double srate)
{
    if (!a || !l || srate <= 0.0) return;

    *l = 0;
    int size = (int)(xmax * srate + 0.5) - 1;
    if (size < 2) size = 2;
    *(l + 1) = size;

    for (int i = 0; i < size; i++) a[i] = 0.0;
}

void delset3(double *a, int *l, double xmax, double srate, double alloc_max)
{
    if (!a || !l || srate <= 0.0) return;

    if (xmax > alloc_max) xmax = alloc_max;
    l[0] = 0;
    int size = (int)(xmax * srate + 0.5) - 1;
    if (size < 2) size = 2;
    l[1] = size;

    for (int i = 0; i < size; i++) a[i] = 0.0;
}

void delput2(double x, double *a, int *l)
{
    int size = *(l + 1);
    if (size <= 0) return;

    int write_idx = *l;
    if (write_idx >= 0 && write_idx < size) {
        a[write_idx++] = x;
    }
    while (write_idx >= size) {
        write_idx -= size;
    }
    *l = write_idx;
}

float dliget2(double *a, double wait, int *l, double srate)
{
    int size = *(l + 1);
    if (size <= 0) return 0.0f;
    if (wait < 0.0) wait = 0.0;

    double x = wait * srate;
    int i = (int)x;
    float frac = (float)(x - (double)i);

    i = *l - i;
    while (i < 0)     i += size;
    while (i >= size) i -= size;

    int im1 = i - 1;
    if (im1 < 0) im1 += size;

    return (float)(a[i] + frac * (a[im1] - a[i]));
}

void butset(float *a)
{
    a[6] = a[7] = 0.0f;
}

void lobut(double *a, double cutoff, double SR)
{
    if (SR <= 0.0) return;
    if (cutoff < 1.0) cutoff = 1.0;
    if (cutoff > SR * 0.49) cutoff = SR * 0.49;

    double c = 1.0 / tan(PI * cutoff / SR);
    a[1] = 1.0 / (1.0 + ROOT2 * c + c * c);
    a[2] = a[1] + a[1];
    a[3] = a[1];
    a[4] = 2.0 * (1.0 - c * c) * a[1];
    a[5] = (1.0 - ROOT2 * c + c * c) * a[1];
    a[6] = a[7] = 0.0;
}

void hibut(double *a, double cutoff, double SR)
{
    if (SR <= 0.0) return;
    if (cutoff < 1.0) cutoff = 1.0;
    if (cutoff > SR * 0.49) cutoff = SR * 0.49;

    double c = tan(PI * cutoff / SR);
    a[1] = 1.0 / (1.0 + ROOT2 * c + c * c);
    a[2] = -2.0 * a[1];
    a[3] = a[1];
    a[4] = 2.0 * (c * c - 1.0) * a[1];
    a[5] = (1.0 - ROOT2 * c + c * c) * a[1];
    a[6] = a[7] = 0.0;
}

void bpbut(double *a, double formant, double bandwidth, double SR)
{
    if (SR <= 0.0) return;
    if (bandwidth < 1.0) bandwidth = 1.0;
    if (formant < 1.0) formant = 1.0;
    if (formant > SR * 0.49) formant = SR * 0.49;

    double c = 1.0 / tan(PI * bandwidth / SR);
    double d = 2.0 * cos(2.0 * PI * formant / SR);
    a[1] = 1.0 / (1.0 + c);
    a[2] = 0.0;
    a[3] = -a[1];
    a[4] = -c * d * a[1];
    a[5] = (c - 1.0) * a[1];
    a[6] = a[7] = 0.0;
}

void butter_filter(double *buf, double *a, long frames)
{
    for (long i = 0; i < frames; i++) {
        double t = buf[i] - a[4] * a[6] - a[5] * a[7];
        double y = t * a[1] + a[2] * a[6] + a[3] * a[7];

        FLUSH_DENORM(t);
        FLUSH_DENORM(y);

        a[7] = a[6];
        a[6] = t;
        buf[i] = y;
    }
}

void rsnset2(double cf, double bw, double scl, double xinit, double *a, double srate)
{
    if (srate <= 0.0) return;
    if (!xinit) {
        a[4] = 0.0;
        a[3] = 0.0;
    }
    a[2] = exp(-PI2 * bw / srate);
    double temp = 1.0 - a[2];
    double c = a[2] + 1.0;
    a[1] = 4.0 * a[2] / c * cos(PI2 * cf / srate);

    if (scl < 0.0) a[0] = 1.0;
    else if (scl > 0.0) {
        double arg = temp / c * (c * c - a[1] * a[1]);
        a[0] = (arg > 0.0) ? sqrt(arg) : 0.0;
    } else {
        double arg = 1.0 - a[1] * a[1] / (4.0 * a[2]);
        a[0] = (arg > 0.0) ? temp * sqrt(arg) : 0.0;
    }
}

double reson(double x, double *a)
{
    double temp = *a * x + *(a + 1) * *(a + 3) - *(a + 2) * *(a + 4);
    FLUSH_DENORM(temp);
    *(a + 4) = *(a + 3);
    *(a + 3) = temp;
    return temp;
}

double allpass(double samp, double *a)
{
    int limit = (int)a[0];
    int idx = (int)a[STARTM1];
    if (idx >= limit || idx < START) idx = START;

    double *aptr = a + idx;
    a[STARTM1] = (double)(idx + 1);

    double temp = *aptr;
    double next_val = temp * a[1] + samp;
    FLUSH_DENORM(next_val);
    *aptr = next_val;

    return temp - a[1] * next_val;
}

void init_reverb_data(double *a)
{
    a[0] = 2.0;
    a[1] = -0.61043329;
    a[2] = -1.4582246;
    a[3] = 1.0;
    a[4] = 0.75887003;
    a[5] = 1.0;
    a[6] = -0.6922953;
    a[7] = 0.0;
    a[8] = 0.0;
    a[9] = 0.035888535;
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

void funcgen1(double *outArray, int outlen, double duration, double outMin, double outMax,
              double speed1, double speed2, double gain1, double gain2, double *phs1, double *phs2,
              double *sine, int sinelen)
{
    if (!outArray || outlen <= 0 || duration <= 0.0) return;

    double localSR = duration * (double)outlen;
    if (localSR <= 0.0) return;

    *phs1 *= (double)sinelen;
    *phs2 *= (double)sinelen;
    double si1 = ((double)sinelen / localSR) * speed1;
    double si2 = ((double)sinelen / localSR) * speed2;

    for (int i = 0; i < outlen; i++) {
        outArray[i] = oscil(gain1, si1, sine, sinelen, phs1);
        outArray[i] += oscil(gain2, si2, sine, sinelen, phs2);
    }
    normtab(outArray, outArray, outMin, outMax, outlen);
}

void normtab(double *inarr, double *outarr, double min, double max, int len)
{
    if (!inarr || !outarr || len <= 0) return;

    double imin = inarr[0];
    double imax = inarr[0];

    for (int i = 1; i < len; i++) {
        if (inarr[i] < imin) imin = inarr[i];
        if (inarr[i] > imax) imax = inarr[i];
    }
    for (int i = 0; i < len; i++) {
        outarr[i] = mapp(inarr[i], imin, imax, min, max);
    }
}

double mapp(double in, double imin, double imax, double omin, double omax)
{
    double range = imax - imin;
    if (fabs(range) < 1.0e-9) {
        return omin;
    }
    return omin + ((omax - omin) * ((in - imin) / range));
}

double oscil(double amp, double si, double *farray, int len, double *phs)
{
    if (!farray || len <= 0) return 0.0;

    while (*phs >= (double)len) *phs -= (double)len;
    while (*phs < 0.0)          *phs += (double)len;

    int i = (int)(*phs);
    if (i < 0) i = 0;
    else if (i >= len) i = len - 1;

    *phs += si;
    return farray[i] * amp;
}

void set_dcflt(double *a)
{
    if (!a) return;
    a[0]  = 3.0;
    a[1]  = -1.9999924;
    a[2]  = -1.9992482;
    a[3]  = 1.0;
    a[4]  = 0.99928019;
    a[5]  = -1.9999956;
    a[6]  = -1.996408;
    a[7]  = 1.0;
    a[8]  = 0.99645999;
    a[9]  = -1.9999994;
    a[10] = -1.9805074;
    a[11] = 1.0;
    a[12] = 0.98069401;
    a[13] = 0.98817413;
}

void set_distortion_table(double *arr, double cut, double max, int len)
{
    if (!arr || len <= 1) return;
    int len2 = len / 2;

    for (int i = 0; i < len; i++) arr[i] = 0.0;

    for (int i = len2; i < len; i++) {
        double samp = (double)(i - len2) / (double)len2;
        if (samp > cut) {
            samp = mapp(samp, cut, 1.0, cut, max);
        }
        arr[i] = samp;
    }
    for (int i = len2 - 1, j = len2; i >= 0 && j < len; i--, j++) {
        arr[i] = arr[j] * -1.0;
    }
}

double dlookup(double samp, double *arr, int len)
{
    if (!arr || len <= 0) return 0.0;

    double raw = (samp + 1.0) * 0.5;
    int index = (int)(raw * (double)len);
    if (index >= len) index = len - 1;
    else if (index < 0) index = 0;

    return arr[index];
}

void do_compdist(double *in, double *out, int sampFrames, int nchans, int channel,
                 double cutoff, double maxmult, int lookupflag, double *table, int range, double bufMaxamp)
{
    if (!in || !out || sampFrames <= 0) return;

    if (lookupflag) {
        double inv = (bufMaxamp > 0.0) ? (1.0 / bufMaxamp) : 1.0;
        for (int i = channel; i < sampFrames * nchans; i += nchans) {
            out[i] = dlookup(in[i] * inv, table, range);
        }
    } else {
        for (int i = channel; i < sampFrames * nchans; i += nchans) {
            double rectsamp = (bufMaxamp > 0.0) ? (fabs(in[i]) / bufMaxamp) : 0.0;
            if (rectsamp > cutoff) {
                in[i] = out[i] * mapp(rectsamp, cutoff, 1.0, cutoff, maxmult);
            }
        }
    }
}

double getmaxamp(double *arr, int len)
{
    if (!arr || len <= 0) return 0.0;

    double max = 0.0;
    for (int i = 0; i < len; i++) {
        double val = fabs(arr[i]);
        if (val > max) max = val;
    }
    return max;
}

void buildadsr(CMIXADSR *a)
{
    if (!a || !a->func || a->len <= 0) return;

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
    int segs[4];

    double total = A + D + S + R;
    if (total <= 0.0) {
        for (int i = 0; i < 4; i++) segs[i] = funclen / 4;
        segs[3] = funclen - (segs[0] + segs[1] + segs[2]);
    } else {
        double inv_total = (double)funclen / total;
        segs[0] = (int)(A * inv_total);
        segs[1] = (int)(D * inv_total);
        segs[2] = (int)(S * inv_total);
        segs[3] = funclen - (segs[0] + segs[1] + segs[2]);
    }

    if (f1 > 20000.0 || f1 < -20000.0) f1 = 250.0;
    if (f2 > 20000.0 || f2 < -20000.0) f2 = 1250.0;
    if (f3 > 20000.0 || f3 < -20000.0) f3 = 950.0;
    if (f4 > 20000.0 || f4 < -20000.0) f4 = f1;

    for (int i = 0; i < 4; i++) {
        if (segs[i] <= 0) segs[i] = 1;
    }

    int ipoint = 0;
    for (int i = 0; i < segs[0] && ipoint < funclen; i++, ipoint++) {
        double m1 = 1.0 - (double)i / (double)segs[0];
        func[ipoint] = f1 * m1 + f2 * (1.0 - m1);
    }
    for (int i = 0; i < segs[1] && ipoint < funclen; i++, ipoint++) {
        double m1 = 1.0 - (double)i / (double)segs[1];
        func[ipoint] = f2 * m1 + f3 * (1.0 - m1);
    }
    for (int i = 0; i < segs[2] && ipoint < funclen; i++, ipoint++) {
        func[ipoint] = f3;
    }
    for (int i = 0; i < segs[3] && ipoint < funclen; i++, ipoint++) {
        double m1 = 1.0 - (double)i / (double)segs[3];
        func[ipoint] = f3 * m1 + f4 * (1.0 - m1);
    }
    while (ipoint < funclen) {
        func[ipoint++] = f4;
    }
}