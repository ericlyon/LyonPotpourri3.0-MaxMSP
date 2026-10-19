/**
 * @file chameleon_dsp.c
 * @brief Real-time DSP transform routines for chameleon~.
 * Modernized for Max 8/9 with bounds checking and denormal protection.
 */

#include "chameleon.h"

void ringmod(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *sinewave = x->sinewave;
    int sinelen = x->sinelen;
    double *params = x->params;
    float srate = x->sr;
    long vs = x->vs;

    ++(*pcount);
    double rmodFreq = params[(*pcount)++];
    int ringmod_count = (int)params[(*pcount)++];
    if (ringmod_count < 0 || ringmod_count >= MAX_DSP_UNITS) return;

    double phase = x->ringmod_phases[ringmod_count];
    double si = ((double)sinelen / srate) * rmodFreq;

    for (int i = 0; i < vs; i++) {
        while (phase >= (double)sinelen) phase -= (double)sinelen;
        while (phase < 0.0)             phase += (double)sinelen;

        long iphase = (long)phase;
        if (iphase < 0) iphase = 0;
        else if (iphase >= sinelen) iphase = sinelen - 1;

        buf1[i] = buf1[i] * sinewave[iphase];
        buf2[i] = buf2[i] * sinewave[iphase];
        phase += si;
    }
    x->ringmod_phases[ringmod_count] = phase;
}

void ringmod4(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *sinewave = x->sinewave;
    int sinelen = x->sinelen;
    double *params = x->params;
    float srate = x->sr;
    long vs = x->vs;

    ++(*pcount);
    double rmodFreq = params[(*pcount)++];
    int ringmod_count = (int)params[(*pcount)++];
    if (ringmod_count < 0 || ringmod_count >= MAX_DSP_UNITS) return;

    double phase = x->ringmod4_phases[ringmod_count];
    double si = ((double)sinelen / srate) * rmodFreq;

    for (int i = 0; i < vs; i++) {
        while (phase >= (double)sinelen) phase -= (double)sinelen;
        while (phase < 0.0)             phase += (double)sinelen;

        long iphase = (long)phase;
        if (iphase < 0) iphase = 0;
        else if (iphase >= sinelen) iphase = sinelen - 1;

        double s = sinewave[iphase];
        buf1[i] = (buf1[i] * buf1[i] * s) - (buf1[i] * s * s);
        buf2[i] = (buf2[i] * buf2[i] * s) - (buf2[i] * s * s);
        phase += si;
    }
    x->ringmod4_phases[ringmod_count] = phase;
}

void comber(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    long vs = x->vs;
    double *params = x->params;

    ++(*pcount);
    params[(*pcount)++]; // delay
    params[(*pcount)++]; // revtime
    int comb_dl_count = (int)params[(*pcount)++];
    if (comb_dl_count < 0 || comb_dl_count >= MAX_DSP_UNITS) return;

    double *delayline1 = x->comb_delay_pool1[comb_dl_count];
    double *delayline2 = x->comb_delay_pool2[comb_dl_count];

    for (int i = 0; i < vs; i++) {
        double c1 = mycomb(buf1[i], delayline1);
        double c2 = mycomb(buf2[i], delayline2);
        FLUSH_DENORM(c1);
        FLUSH_DENORM(c2);
        buf1[i] += c1;
        buf2[i] += c2;
    }
}

void flange(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    double srate = x->sr;
    long vs = x->vs;
    double max_delay = x->max_flangedelay;
    double *sinewave = x->sinewave;
    int sinelen = x->sinelen;

    ++(*pcount);
    double minres = params[(*pcount)++];
    double maxres = params[(*pcount)++];
    double speed = params[(*pcount)++];
    double feedback = params[(*pcount)++];
    int flange_count = (int)params[(*pcount)++];

    if (flange_count < 0 || flange_count >= MAX_DSP_UNITS) return;
    if (minres <= 0.0 || maxres <= 0.0) return;

    double *flange_dl1 = x->flange_units[flange_count].flange_dl1;
    double *flange_dl2 = x->flange_units[flange_count].flange_dl2;
    int *dv1 = x->flange_units[flange_count].dv1;
    int *dv2 = x->flange_units[flange_count].dv2;
    double phase = x->flange_units[flange_count].phase;

    double mindel = 1.0 / maxres;
    double maxdel = 1.0 / minres;
    if (maxdel > max_delay * 0.99) maxdel = max_delay * 0.99;

    double si = ((double)sinelen / srate) * speed;
    double delsamp1 = 0.0, delsamp2 = 0.0;
    double fac2 = 0.5 * (maxdel - mindel);
    double fac1 = mindel + fac2;

    for (int i = 0; i < vs; i++) {
        while (phase >= (double)sinelen) phase -= (double)sinelen;
        while (phase < 0.0)             phase += (double)sinelen;

        long iph = (long)phase;
        if (iph >= sinelen) iph = sinelen - 1;

        double delay_time = fac1 + fac2 * sinewave[iph];
        if (delay_time < 0.00001) delay_time = 0.00001;
        else if (delay_time >= maxdel) delay_time = maxdel;

        phase += si;

        delput2(buf1[i] + delsamp1 * feedback, flange_dl1, dv1);
        delsamp1 = dliget2(flange_dl1, delay_time, dv1, srate);
        FLUSH_DENORM(delsamp1);
        buf1[i] += delsamp1;

        delput2(buf2[i] + delsamp2 * feedback, flange_dl2, dv2);
        delsamp2 = dliget2(flange_dl2, delay_time, dv2, srate);
        FLUSH_DENORM(delsamp2);
        buf2[i] += delsamp2;
    }
    x->flange_units[flange_count].phase = phase;
}

void butterme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    long vs = x->vs;

    ++(*pcount);
    params[(*pcount)++]; // ftype
    params[(*pcount)++]; // cf
    params[(*pcount)++]; // bw
    int butterworth_count = (int)params[(*pcount)++];
    if (butterworth_count < 0 || butterworth_count >= MAX_DSP_UNITS) return;

    double *data1 = x->butterworth_units[butterworth_count].data1;
    double *data2 = x->butterworth_units[butterworth_count].data2;

    butter_filter(buf1, data1, vs);
    butter_filter(buf2, data2, vs);
}

void bitcrush(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    long vs = x->vs;

    ++(*pcount);
    int bitcrush_count = (int)params[(*pcount)++];
    params[(*pcount)++]; // bitcrush_factor from params

    if (bitcrush_count < 0 || bitcrush_count >= MAX_DSP_UNITS) return;

    double factor = x->bitcrush_factors[bitcrush_count];
    if (factor <= 0.0) factor = 1.0;

    double inv_factor = 1.0 / factor;
    for (int i = 0; i < vs; i++) {
        buf1[i] = floor(buf1[i] * factor) * inv_factor;
        buf2[i] = floor(buf2[i] * factor) * inv_factor;
    }
}

void truncateme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    long vs = x->vs;

    ++(*pcount);
    int truncate_count = (int)params[(*pcount)++];
    if (truncate_count < 0 || truncate_count >= MAX_DSP_UNITS) return;

    long counter = x->truncate_units[truncate_count].counter;
    long state = x->truncate_units[truncate_count].state;
    long segsamples = x->truncate_units[truncate_count].segsamples;
    if (segsamples <= 0) segsamples = 1;

    for (int i = 0; i < vs; i++) {
        double samp1 = buf1[i];
        double samp2 = buf2[i];
        counter++;

        if (state >= 0 && state <= 2) {
            buf1[i] = 0.0;
            buf2[i] = 0.0;
        } else if (state == 3) {
            double env_gain = (double)counter / (double)segsamples;
            buf1[i] = samp1 * env_gain;
            buf2[i] = samp2 * env_gain;
        } else if (state == 7) {
            double env_gain = 1.0 - ((double)counter / (double)segsamples);
            buf1[i] = samp1 * env_gain;
            buf2[i] = samp2 * env_gain;
        }

        if (counter >= segsamples) {
            if (state == 0)      segsamples = (long)(x->sr * boundrand(0.1f, 2.0f));
            else if (state == 2 || state == 6) segsamples = (long)(x->sr * 0.05f);
            else if (state == 4) segsamples = (long)(x->sr * boundrand(1.0f, 7.0f));
            else                 segsamples = 1;

            if (segsamples <= 0) segsamples = 1;
            counter = 0;
            state = (state + 1) % 8;
        }
    }

    x->truncate_units[truncate_count].state = state;
    x->truncate_units[truncate_count].segsamples = segsamples;
    x->truncate_units[truncate_count].counter = counter;
}

void sweepreson(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    double srate = x->sr;
    long vs = x->vs;
    double *sinewave = x->sinewave;
    int sinelen = x->sinelen;

    ++(*pcount);
    long sweepreson_count = (long)params[(*pcount)++];
    params[(*pcount)++]; // minfreq
    params[(*pcount)++]; // maxfreq
    params[(*pcount)++]; // bwfac
    params[(*pcount)++]; // speed
    params[(*pcount)++]; // phase

    if (sweepreson_count < 0 || sweepreson_count >= MAX_DSP_UNITS) return;

    double minfreq = x->sweepreson_units[sweepreson_count].minfreq;
    double maxfreq = x->sweepreson_units[sweepreson_count].maxfreq;
    double bwfac = x->sweepreson_units[sweepreson_count].bwfac;
    double speed = x->sweepreson_units[sweepreson_count].speed;
    double phase = x->sweepreson_units[sweepreson_count].phase;
    double *q1 = x->sweepreson_units[sweepreson_count].q1;
    double *q2 = x->sweepreson_units[sweepreson_count].q2;

    double si = ((double)sinelen / srate) * speed;
    double fac2 = 0.5 * (maxfreq - minfreq);
    double fac1 = minfreq + fac2;

    for (int i = 0; i < vs; i++) {
        phase += si;
        while (phase >= (double)sinelen) phase -= (double)sinelen;
        while (phase < 0.0)             phase += (double)sinelen;

        long iph = (long)phase;
        if (iph >= sinelen) iph = sinelen - 1;

        double cf = fac1 + fac2 * sinewave[iph];
        if (cf < 10.0) cf = 10.0;
        else if (cf > srate * 0.45) cf = srate * 0.45;

        double bw = bwfac * cf;
        if (bw < 1.0) bw = 1.0;

        rsnset2(cf, bw, 2.0, 1.0, q1, srate);
        rsnset2(cf, bw, 2.0, 1.0, q2, srate);

        double s1 = reson(buf1[i], q1);
        double s2 = reson(buf2[i], q2);
        FLUSH_DENORM(s1);
        FLUSH_DENORM(s2);
        buf1[i] = s1;
        buf2[i] = s2;
    }
    x->sweepreson_units[sweepreson_count].phase = phase;
}

void slidecomb(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    double srate = x->sr;
    long vs = x->vs;

    ++(*pcount);
    int slidecomb_count = (int)params[(*pcount)++];
    params[(*pcount)++]; // delay1
    params[(*pcount)++]; // delay2
    params[(*pcount)++]; // feedback
    params[(*pcount)++]; // sample_length

    if (slidecomb_count < 0 || slidecomb_count >= MAX_DSP_UNITS) return;

    double delay1 = x->slidecomb_units[slidecomb_count].start_delay;
    double delay2 = x->slidecomb_units[slidecomb_count].end_delay;
    long sample_length = x->slidecomb_units[slidecomb_count].sample_length;
    long counter = x->slidecomb_units[slidecomb_count].counter;
    double feedback = x->slidecomb_units[slidecomb_count].feedback;
    int *dv1 = x->slidecomb_units[slidecomb_count].dv1;
    int *dv2 = x->slidecomb_units[slidecomb_count].dv2;
    double *delayline1 = x->slidecomb_units[slidecomb_count].delayline1;
    double *delayline2 = x->slidecomb_units[slidecomb_count].delayline2;
    if (sample_length <= 0) sample_length = 1;

    double delsamp1 = 0.0, delsamp2 = 0.0;

    for (int i = 0; i < vs; i++) {
        double delay_time;
        if (counter < sample_length) {
            double m2 = (double)counter / (double)sample_length;
            delay_time = delay1 * (1.0 - m2) + delay2 * m2;
            counter++;
        } else {
            counter = 0;
            double tmp = x->slidecomb_units[slidecomb_count].start_delay;
            delay1 = x->slidecomb_units[slidecomb_count].start_delay = x->slidecomb_units[slidecomb_count].end_delay;
            delay2 = x->slidecomb_units[slidecomb_count].end_delay = tmp;
            delay_time = delay1;
        }

        if (delay_time > MAX_SLIDECOMB_DELAY) delay_time = MAX_SLIDECOMB_DELAY;
        else if (delay_time < 0.0) delay_time = 0.0;

        delput2(buf1[i] + (delsamp1 * feedback), delayline1, dv1);
        delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
        FLUSH_DENORM(delsamp1);
        buf1[i] = delsamp1;

        delput2(buf2[i] + (delsamp2 * feedback), delayline2, dv2);
        delsamp2 = dliget2(delayline2, delay_time, dv2, srate);
        FLUSH_DENORM(delsamp2);
        buf2[i] = delsamp2;
    }
    x->slidecomb_units[slidecomb_count].counter = counter;
}

void slideflam(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    double srate = x->sr;
    long vs = x->vs;

    ++(*pcount);
    int slideflam_count = (int)params[(*pcount)++];
    params[(*pcount)++]; // delay1
    params[(*pcount)++]; // delay2
    params[(*pcount)++]; // feedback
    params[(*pcount)++]; // sample_length

    if (slideflam_count < 0 || slideflam_count >= MAX_DSP_UNITS) return;

    double delay1 = x->slideflam_units[slideflam_count].dt1;
    double delay2 = x->slideflam_units[slideflam_count].dt2;
    long sample_length = x->slideflam_units[slideflam_count].sample_length;
    long counter = x->slideflam_units[slideflam_count].counter;
    double feedback = x->slideflam_units[slideflam_count].feedback;
    int *dv1 = x->slideflam_units[slideflam_count].dv1;
    int *dv2 = x->slideflam_units[slideflam_count].dv2;
    double *delayline1 = x->slideflam_units[slideflam_count].delayline1;
    double *delayline2 = x->slideflam_units[slideflam_count].delayline2;
    if (sample_length <= 0) sample_length = 1;

    double delsamp1 = 0.0, delsamp2 = 0.0;

    for (int i = 0; i < vs; i++) {
        double delay_time;
        if (counter < sample_length) {
            double m2 = (double)counter / (double)sample_length;
            delay_time = delay1 * (1.0 - m2) + delay2 * m2;
            counter++;
        } else {
            counter = 0;
            double tmp = x->slideflam_units[slideflam_count].dt1;
            delay1 = x->slideflam_units[slideflam_count].dt1 = x->slideflam_units[slideflam_count].dt2;
            delay2 = x->slideflam_units[slideflam_count].dt2 = tmp;
            delay_time = delay1;
        }

        if (delay_time < 0.0) delay_time = 0.0;
        else if (delay_time >= MAX_SLIDEFLAM_DELAY) delay_time = MAX_SLIDEFLAM_DELAY;

        delput2(buf1[i] + (delsamp1 * feedback), delayline1, dv1);
        delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
        FLUSH_DENORM(delsamp1);
        buf1[i] = delsamp1;

        delput2(buf2[i] + (delsamp2 * feedback), delayline2, dv2);
        delsamp2 = dliget2(delayline2, delay_time, dv2, srate);
        FLUSH_DENORM(delsamp2);
        buf2[i] = delsamp2;
    }
    x->slideflam_units[slideflam_count].counter = counter;
}

void bendy(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    double srate = x->sr;
    long vs = x->vs;

    ++(*pcount);
    int bendy_count = (int)params[(*pcount)++];
    params[(*pcount)++]; // val1
    params[(*pcount)++]; // val2

    if (bendy_count < 0 || bendy_count >= MAX_DSP_UNITS) return;

    long counter = x->bendy_units[bendy_count].counter;
    double val1 = x->bendy_units[bendy_count].val1;
    double val2 = x->bendy_units[bendy_count].val2;
    int *dv1 = x->bendy_units[bendy_count].dv1;
    int *dv2 = x->bendy_units[bendy_count].dv2;
    double *delayline1 = x->bendy_units[bendy_count].delayline1;
    double *delayline2 = x->bendy_units[bendy_count].delayline2;
    long segment_samples = x->bendy_units[bendy_count].segment_samples;
    if (segment_samples <= 0) segment_samples = 1;

    for (int i = 0; i < vs; i++) {
        if (counter <= 0) {
            val1 = val2;
            val2 = boundrand(0.01f, (float)BENDY_MAXDEL);
            double segdur = boundrand((float)BENDY_MINSEG, (float)BENDY_MAXSEG);
            counter = (long)(segdur * srate);
            if (counter <= 0) counter = 1;
            segment_samples = counter;
        }

        double frak = 1.0 - ((double)counter / (double)segment_samples);
        double delay_time = val2 + (frak * (val1 - val2));
        if (delay_time > BENDY_MAXDEL) delay_time = BENDY_MAXDEL;
        else if (delay_time < 0.0) delay_time = 0.0;

        delput2(buf1[i], delayline1, dv1);
        double s1 = dliget2(delayline1, delay_time, dv1, srate);
        FLUSH_DENORM(s1);
        buf1[i] = s1;

        delput2(buf2[i], delayline2, dv2);
        double s2 = dliget2(delayline2, delay_time, dv2, srate);
        FLUSH_DENORM(s2);
        buf2[i] = s2;
        counter--;
    }
    x->bendy_units[bendy_count].counter = counter;
    x->bendy_units[bendy_count].val1 = val1;
    x->bendy_units[bendy_count].val2 = val2;
    x->bendy_units[bendy_count].segment_samples = segment_samples;
}

void reverb1(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    long vs = x->vs;

    ++(*pcount);
    int reverb1_count = (int)params[(*pcount)++];
    double revtime = params[(*pcount)++];
    params[(*pcount)++]; // raw_wet
    for (int i = 0; i < 4; i++) {
        params[(*pcount)++];
    }

    if (reverb1_count < 0 || reverb1_count >= MAX_DSP_UNITS) return;

    double wet = x->reverb1_units[reverb1_count].wet;
    double dry = x->reverb1_units[reverb1_count].dry;
    double **alpo1 = x->reverb1_units[reverb1_count].alpo1;
    double **alpo2 = x->reverb1_units[reverb1_count].alpo2;
    int nsects = x->reverb1_units[reverb1_count].nsects;
    double xnorm = x->reverb1_units[reverb1_count].xnorm;
    LSTRUCT *eel1 = x->reverb1_units[reverb1_count].eel1;
    LSTRUCT *eel2 = x->reverb1_units[reverb1_count].eel2;

    if (revtime >= 0.99) revtime = 0.98;
    else if (revtime < 0.0) revtime = 0.0;

    for (int i = 0; i < vs; i++) {
        double a1 = allpass(buf1[i], alpo1[0]);
        double a2 = allpass(buf1[i], alpo1[1]);
        double a3 = allpass(buf1[i], alpo1[2]);
        double a4 = allpass(buf1[i], alpo1[3]);
        double outL = (buf1[i] * dry) + (ellipse((a1 + a2 + a3 + a4), eel1, nsects, xnorm) * wet);
        FLUSH_DENORM(outL);
        buf1[i] = outL;

        a1 = allpass(buf2[i], alpo2[0]);
        a2 = allpass(buf2[i], alpo2[1]);
        a3 = allpass(buf2[i], alpo2[2]);
        a4 = allpass(buf2[i], alpo2[3]);
        double outR = (buf2[i] * dry) + (ellipse((a1 + a2 + a3 + a4), eel2, nsects, xnorm) * wet);
        FLUSH_DENORM(outR);
        buf2[i] = outR;
    }
}

void ellipseme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    long vs = x->vs;

    ++(*pcount);
    int ellipseme_count = (int)params[(*pcount)++];
    params[(*pcount)++]; // filtercode

    if (ellipseme_count < 0 || ellipseme_count >= MAX_DSP_UNITS) return;

    LSTRUCT *eel1 = x->ellipseme_units[ellipseme_count].eel1;
    LSTRUCT *eel2 = x->ellipseme_units[ellipseme_count].eel2;
    int nsects = x->ellipseme_units[ellipseme_count].nsects;
    double xnorm = x->ellipseme_units[ellipseme_count].xnorm;

    for (int i = 0; i < vs; i++) {
        double s1 = ellipse(buf1[i], eel1, nsects, xnorm);
        double s2 = ellipse(buf2[i], eel2, nsects, xnorm);
        FLUSH_DENORM(s1);
        FLUSH_DENORM(s2);
        buf1[i] = s1;
        buf2[i] = s2;
    }
}

void feed1me(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    long vs = x->vs;
    double srate = x->sr;

    ++(*pcount);
    int feed1_count = (int)params[(*pcount)++];
    params[(*pcount)++]; // mindelay
    params[(*pcount)++]; // maxdelay
    params[(*pcount)++]; // speed1
    params[(*pcount)++]; // speed2
    double duration = params[(*pcount)++];
    if (duration <= 0.0) duration = 0.1;

    if (feed1_count < 0 || feed1_count >= MAX_DSP_UNITS) return;

    double *func1 = x->feed1_units[feed1_count].func1;
    double *func2 = x->feed1_units[feed1_count].func2;
    double *func3 = x->feed1_units[feed1_count].func3;
    double *func4 = x->feed1_units[feed1_count].func4;
    double *delayLine1a = x->feed1_units[feed1_count].delayLine1a;
    double *delayLine2a = x->feed1_units[feed1_count].delayLine2a;
    double *delayLine1b = x->feed1_units[feed1_count].delayLine1b;
    double *delayLine2b = x->feed1_units[feed1_count].delayLine2b;
    int *dv1a = x->feed1_units[feed1_count].dv1a;
    int *dv2a = x->feed1_units[feed1_count].dv2a;
    int *dv1b = x->feed1_units[feed1_count].dv1b;
    int *dv2b = x->feed1_units[feed1_count].dv2b;

    long ddl_func_len = (long)(x->sr * MAX_MINI_DELAY);
    double funcSi = ((double)FEEDFUNCLEN / srate) / duration;
    double funcPhs = x->feed1_units[feed1_count].funcPhs;

    double delsamp1a = 0.0, delsamp2a = 0.0;
    double delsamp1b = 0.0, delsamp2b = 0.0;

    for (int i = 0; i < vs; i++) {
        int fidx = (int)funcPhs;
        if (fidx >= FEEDFUNCLEN) fidx = FEEDFUNCLEN - 1;
        else if (fidx < 0) fidx = 0;

        double delay1 = func1[fidx];
        double delay2 = func2[fidx];
        if (delay1 < 0.0) delay1 = 0.0;
        else if (delay1 > ddl_func_len) delay1 = ddl_func_len - 1;

        if (delay2 < 0.0) delay2 = 0.0;
        else if (delay2 > ddl_func_len) delay2 = ddl_func_len - 1;

        double feedback1 = func3[fidx];
        double feedback2 = func4[fidx];

        funcPhs += funcSi;
        while (funcPhs >= (double)FEEDFUNCLEN) funcPhs -= (double)FEEDFUNCLEN;

        double putsamp = buf1[i] + delsamp1a * feedback1;
        delput2(putsamp, delayLine1a, dv1a);
        delsamp1a = dliget2(delayLine1a, delay1, dv1a, srate);

        putsamp = delsamp1a + (delsamp2a * feedback2);
        delput2(putsamp, delayLine2a, dv2a);
        delsamp2a = dliget2(delayLine2a, delay2, dv2a, srate);
        FLUSH_DENORM(delsamp2a);
        buf1[i] += delsamp2a;

        putsamp = buf2[i] + delsamp1b * feedback1;
        delput2(putsamp, delayLine1b, dv1b);
        delsamp1b = dliget2(delayLine1b, delay1, dv1b, srate);

        putsamp = delsamp1b + (delsamp2b * feedback2);
        delput2(putsamp, delayLine2b, dv2b);
        delsamp2b = dliget2(delayLine2b, delay2, dv2b, srate);
        FLUSH_DENORM(delsamp2b);
        buf2[i] += delsamp2b;
    }
    x->feed1_units[feed1_count].funcPhs = funcPhs;
}

void flam1(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    double srate = x->sr;
    long vs = x->vs;

    ++(*pcount);
    int flam1_count = (int)params[(*pcount)++];
    params[(*pcount)++]; // sample_length
    params[(*pcount)++]; // delaytime

    if (flam1_count < 0 || flam1_count >= MAX_DSP_UNITS) return;

    double delaytime = x->flam1_units[flam1_count].dt;
    long sample_length = x->flam1_units[flam1_count].sample_length;
    long counter = x->flam1_units[flam1_count].counter;
    int *dv1 = x->flam1_units[flam1_count].dv1;
    int *dv2 = x->flam1_units[flam1_count].dv2;
    double *delayline1 = x->flam1_units[flam1_count].delayline1;
    double *delayline2 = x->flam1_units[flam1_count].delayline2;
    if (sample_length <= 0) sample_length = 1;

    if (delaytime >= FLAM1_MAX_DELAY) delaytime = FLAM1_MAX_DELAY * 0.99;
    else if (delaytime < 0.0) delaytime = 0.0;

    double delsamp1 = 0.0, delsamp2 = 0.0;

    for (int i = 0; i < vs; i++) {
        if (counter < sample_length) {
            double feedback = 1.0 - ((double)counter / (double)sample_length);
            delput2(buf1[i] + (delsamp1 * feedback), delayline1, dv1);
            delsamp1 = dliget2(delayline1, delaytime, dv1, srate);
            FLUSH_DENORM(delsamp1);
            buf1[i] += delsamp1;

            delput2(buf2[i] + (delsamp2 * feedback), delayline2, dv2);
            delsamp2 = dliget2(delayline2, delaytime, dv2, srate);
            FLUSH_DENORM(delsamp2);
            buf2[i] += delsamp2;
        }
        counter++;
        if (counter >= sample_length) counter = 0;
    }
    x->flam1_units[flam1_count].counter = counter;
}

void comb4(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    long vs = x->vs;

    ++(*pcount);
    int comb4_count = (int)params[(*pcount)++];
    params[(*pcount)++]; // basefreq
    for (int i = 0; i < 4; i++) {
        params[(*pcount)++]; // dex
    }

    if (comb4_count < 0 || comb4_count >= MAX_DSP_UNITS) return;

    for (int i = 0; i < vs; i++) {
        double insamp = buf1[i];
        buf1[i] = 0.0;
        for (int j = 0; j < 4; j++) {
            double s = mycomb(insamp, x->comb4_units[comb4_count].combs1[j]) * 0.25;
            FLUSH_DENORM(s);
            buf1[i] += s;
        }
        insamp = buf2[i];
        buf2[i] = 0.0;
        for (int j = 0; j < 4; j++) {
            double s = mycomb(insamp, x->comb4_units[comb4_count].combs2[j]) * 0.25;
            FLUSH_DENORM(s);
            buf2[i] += s;
        }
    }
}

void compdist(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    long vs = x->vs;
    double *dist_fn = x->distortion_function;
    int dist_len = x->distortion_length;

    ++(*pcount);

    for (int i = 0; i < vs; i++) {
        buf1[i] = dlookup(buf1[i], dist_fn, dist_len);
        buf2[i] = dlookup(buf2[i], dist_fn, dist_len);
    }
}

void ringfeed(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    long vs = x->vs;

    ++(*pcount);
    int ringfeed_count = (int)params[(*pcount)++];
    params[(*pcount)++]; // cf
    params[(*pcount)++]; // bw
    params[(*pcount)++]; // si1
    params[(*pcount)++]; // si2
    params[(*pcount)++]; // lpt
    params[(*pcount)++]; // rvt

    if (ringfeed_count < 0 || ringfeed_count >= MAX_DSP_UNITS) return;

    double si1 = x->resonfeed_units[ringfeed_count].osc1si;
    double si2 = x->resonfeed_units[ringfeed_count].osc2si;
    double osc1phs = x->resonfeed_units[ringfeed_count].osc1phs;
    double osc2phs = x->resonfeed_units[ringfeed_count].osc2phs;
    double rescale = 1.5;

    for (int i = 0; i < vs; i++) {
        buf1[i] *= oscil(1.0, si1, x->sinewave, x->sinelen, &osc1phs);
        buf1[i] += mycomb(buf1[i], x->resonfeed_units[ringfeed_count].comb1arr);
        buf1[i] = reson(buf1[i], x->resonfeed_units[ringfeed_count].res1q) * rescale;
        FLUSH_DENORM(buf1[i]);

        buf2[i] *= oscil(1.0, si2, x->sinewave, x->sinelen, &osc2phs);
        buf2[i] += mycomb(buf2[i], x->resonfeed_units[ringfeed_count].comb2arr);
        buf2[i] = reson(buf2[i], x->resonfeed_units[ringfeed_count].res2q) * rescale;
        FLUSH_DENORM(buf2[i]);
    }
    x->resonfeed_units[ringfeed_count].osc1phs = osc1phs;
    x->resonfeed_units[ringfeed_count].osc2phs = osc2phs;
}

void resonadsr(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    float srate = x->sr;
    long vs = x->vs;
    double *params = x->params;

    ++(*pcount);
    int resonadsr_count = (int)params[(*pcount)++];
    params[(*pcount)++]; // a
    params[(*pcount)++]; // d
    params[(*pcount)++]; // r
    params[(*pcount)++]; // v1
    params[(*pcount)++]; // v2
    params[(*pcount)++]; // v3
    params[(*pcount)++]; // v4
    params[(*pcount)++]; // bwfac
    params[(*pcount)++]; // notedur

    if (resonadsr_count < 0 || resonadsr_count >= MAX_DSP_UNITS) return;

    double *q1 = x->resonadsr_units[resonadsr_count].q1;
    double *q2 = x->resonadsr_units[resonadsr_count].q2;
    double phase = x->resonadsr_units[resonadsr_count].phs;
    double *adsrfunc = x->resonadsr_units[resonadsr_count].adsr->func;
    int funclen = x->resonadsr_units[resonadsr_count].adsr->len;
    float si = (float)x->resonadsr_units[resonadsr_count].si;
    float bwfac = (float)x->resonadsr_units[resonadsr_count].bwfac;

    for (int i = 0; i < vs; i++) {
        long idx = (long)phase;
        if (idx < 0) idx = 0;
        else if (idx >= funclen) idx = funclen - 1;

        double cf = adsrfunc[idx];
        double bw = bwfac * cf;
        phase += si;
        if (phase >= (double)funclen) phase = (double)(funclen - 1);

        rsnset2(cf, bw, 2.0, 1.0, q1, srate);
        rsnset2(cf, bw, 2.0, 1.0, q2, srate);

        double s1 = reson(buf1[i], q1);
        double s2 = reson(buf2[i], q2);
        FLUSH_DENORM(s1);
        FLUSH_DENORM(s2);
        buf1[i] = s1;
        buf2[i] = s2;
    }
    x->resonadsr_units[resonadsr_count].phs = phase;
}

void stv(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    float srate = x->sr;
    long vs = x->vs;
    double *params = x->params;

    ++(*pcount);
    int stv_count = (int)params[(*pcount)++];
    params[(*pcount)++]; // speed1
    params[(*pcount)++]; // speed2
    params[(*pcount)++]; // maxdelay

    if (stv_count < 0 || stv_count >= MAX_DSP_UNITS) return;

    double *sinewave = x->sinewave;
    int sinelen = x->sinelen;
    double *delayline1 = x->stv_units[stv_count].delayline1;
    double *delayline2 = x->stv_units[stv_count].delayline2;
    int *dv1 = x->stv_units[stv_count].dv1;
    int *dv2 = x->stv_units[stv_count].dv2;
    double si1 = x->stv_units[stv_count].osc1si;
    double si2 = x->stv_units[stv_count].osc2si;
    double osc1phs = x->stv_units[stv_count].osc1phs;
    double osc2phs = x->stv_units[stv_count].osc2phs;
    double fac1 = x->stv_units[stv_count].fac1;
    double fac2 = x->stv_units[stv_count].fac2;

    for (int i = 0; i < vs; i++) {
        double delay_time = fac1 + oscil(fac2, si1, sinewave, sinelen, &osc1phs);
        if (delay_time < 0.0) delay_time = 0.0;
        else if (delay_time > STV_MAX_DELAY) delay_time = STV_MAX_DELAY;

        delput2(buf1[i], delayline1, dv1);
        double s1 = dliget2(delayline1, delay_time, dv1, srate);
        FLUSH_DENORM(s1);
        buf1[i] = s1;

        delay_time = fac1 + oscil(fac2, si2, sinewave, sinelen, &osc2phs);
        if (delay_time < 0.0) delay_time = 0.0;
        else if (delay_time > STV_MAX_DELAY) delay_time = STV_MAX_DELAY;

        delput2(buf2[i], delayline2, dv2);
        double s2 = dliget2(delayline2, delay_time, dv2, srate);
        FLUSH_DENORM(s2);
        buf2[i] = s2;
    }

    x->stv_units[stv_count].osc1phs = osc1phs;
    x->stv_units[stv_count].osc2phs = osc2phs;
}