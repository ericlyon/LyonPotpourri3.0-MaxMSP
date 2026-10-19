#include "chameleon.h"

void ringmod(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *sinewave = x->sinewave;
    int sinelen = x->sinelen;
    double *params = x->params;
    float srate = x->sr;
    long vs = x->vs;
    int i;
    double phase = 0.0;
    long iphase;
    double si;
    double rmodFreq;
    int ringmod_count;
    
    ++(*pcount);
    rmodFreq = params[(*pcount)++];
    ringmod_count = params[(*pcount)++];
    phase = x->ringmod_phases[ringmod_count];
    
    si = ((double) sinelen / srate) * rmodFreq;
    
    for(i = 0; i < vs; i++ ){
        iphase = (long) phase;
        buf1[i] = buf1[i] * sinewave[iphase];
        buf2[i] = buf2[i] * sinewave[iphase];
        phase += si;
        while( phase > sinelen ){
            phase -= sinelen;
        }
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
    int i;
    double phase = 0.0;
    long iphase;
    double si;
    double rmodFreq;
    int ringmod_count;
    
    ++(*pcount);
    rmodFreq = params[(*pcount)++];
    ringmod_count =params[(*pcount)++];
    phase = x->ringmod4_phases[ringmod_count];
    
    si = ((double) sinelen / srate) * rmodFreq;
    
    // ((a*a *b) - (a*b*b))
    for(i = 0; i < vs; i++ ){
        iphase = (long) phase;
        buf1[i] = (buf1[i] * buf1[i] * sinewave[iphase]) - (buf1[i] * sinewave[iphase] * sinewave[iphase]);
        buf2[i] = (buf2[i] * buf2[i] * sinewave[iphase]) - (buf2[i] * sinewave[iphase] * sinewave[iphase]);
        phase += si;
        while( phase > sinelen ){
            phase -= sinelen;
        }
    }
    x->ringmod_phases[ringmod_count] = phase;
}

void comber(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *delayline1;
    double *delayline2;
    double delay, revtime;
    long vs = x->vs;
    int i;
    int comb_dl_count;
    double *params = x->params;
    /******************************/
    ++(*pcount);
    delay = params[(*pcount)++];
    revtime = params[(*pcount)++];
    comb_dl_count = params[(*pcount)++];
    
    delayline1 = x->comb_delay_pool1[comb_dl_count];
    delayline2 = x->comb_delay_pool2[comb_dl_count];
    // ADD IN ORIGINAL SIGNAL
    for( i = 0; i < vs; i++){
        buf1[i] += mycomb(buf1[i], delayline1);
        buf2[i] += mycomb(buf2[i], delayline2);
    }
}

void flange(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    int i;
    double si;
    double mindel, maxdel;
    double fac1, fac2;
    double delsamp1, delsamp2;
    double delay_time;
    double speed, feedback, minres, maxres;
    double *params = x->params;
    double srate = x->sr;
    long vs = x->vs;
    double *flange_dl1;
    double *flange_dl2;
    double max_delay = x->max_flangedelay;
    double *sinewave = x->sinewave;
    int sinelen = x->sinelen;
    int *dv1 = x->flange_dv1;
    int *dv2 = x->flange_dv2;
    double phase = x->flange_phase;
    int flange_count;
    
    ++(*pcount);
    minres = params[(*pcount)++];
    maxres = params[(*pcount)++];
    speed = params[(*pcount)++];
    feedback = params[(*pcount)++];
    flange_count = params[(*pcount)++];
    
    flange_dl1 = x->flange_units[flange_count].flange_dl1;
    flange_dl2 = x->flange_units[flange_count].flange_dl2;
    dv1 = x->flange_units[flange_count].dv1;
    dv2 = x->flange_units[flange_count].dv2;
    phase = x->flange_units[flange_count].phase;
    
    if( minres <= 0. || maxres <= 0. ){
        error("flange: got zero frequency resonances as input");
        return;
    }
    mindel = 1.0/maxres;
    maxdel = 1.0/minres;
    // added safety
    if( maxdel > max_delay * 0.99 ){
        maxdel = max_delay * 0.99;
        error("flange: excessive delay time shortened");
    }
    
    si = ((double)sinelen/srate) * speed;
    delsamp1 = delsamp2 = 0;
    fac2 = .5 * (maxdel - mindel);
    fac1 = mindel + fac2;
    
    for(i = 0; i < vs; i++ ){
        /* homemade oscillator */
        delay_time = fac1 + fac2 *  sinewave[(int) phase];
        if( delay_time < .00001 ){
            delay_time = .00001;
        } else if(delay_time >= maxdel) {
            delay_time = maxdel;
        }
        phase += si;
        while( phase > sinelen ){
            phase -= sinelen;
        }
        delput2(buf1[i] + delsamp1 * feedback, flange_dl1, dv1);
        delsamp1 = dliget2(flange_dl1, delay_time, dv1, srate);
        buf1[i] += delsamp1;
 
        delput2(buf2[i] + delsamp2 * feedback, flange_dl2, dv2);
        delsamp2 = dliget2(flange_dl2, delay_time, dv2, srate);
        buf2[i] += delsamp2;
    }
    x->flange_units[flange_count].phase = phase;
}

void butterme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    int butterworth_count;
    double *data1, *data2;
    long vs = x->vs;
    int ftype;
    double bw, cf;
    
    ++(*pcount);
    ftype = params[(*pcount)++];
    cf = params[(*pcount)++];
    bw = params[(*pcount)++];
    butterworth_count = params[(*pcount)++];
    
    data1 = x->butterworth_units[butterworth_count].data1;
    data2 = x->butterworth_units[butterworth_count].data2;

    butter_filter(buf1, data1, vs);
    butter_filter(buf2, data2, vs);
}



void bitcrush(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    int bitcrush_count;
    double bitcrush_factor;
    long vs = x->vs;
    int i;
    ++(*pcount);
    bitcrush_count = params[(*pcount)++];
    bitcrush_factor = params[(*pcount)++];

    bitcrush_factor = x->bitcrush_factors[bitcrush_count];
    for(i = 0; i < vs; i++){
        buf1[i] = floor(buf1[i] * bitcrush_factor) / bitcrush_factor;
        buf2[i] = floor(buf2[i] * bitcrush_factor) / bitcrush_factor;
    }
}


void truncateme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double *params = x->params;
    double env_gain;
    int truncate_count;
    long vs = x->vs;
    int i;
    long counter;
    long state;
    long segsamples;
    double samp1, samp2;
    ++(*pcount);
    truncate_count = params[(*pcount)++];

    counter = x->truncate_units[truncate_count].counter;
    state = x->truncate_units[truncate_count].state;
    segsamples = x->truncate_units[truncate_count].segsamples;

    for(i = 0; i < vs; i++){
        samp1 = buf1[i];
        samp2 = buf2[i];
        counter++;
        if( (state == 0) || (state == 1) || (state == 2) ){
            buf1[i] = 0.0;
            buf2[i] = 0.0;
        }
        else if( state == 3 ){
            env_gain = (double)counter / (double)segsamples;
            buf1[i] = samp1 * env_gain;
            buf2[i] = samp2 * env_gain;
        }
        /*
        else if( (state == 4) || (state == 5) || (state == 6) ) {
         just output unprocessed sound
        }
        */
        else if( state == 7 ){
            env_gain = 1.0 - ((double)counter / (double)segsamples);
            buf1[i] = samp1 * env_gain;
            buf2[i] = samp2 * env_gain;
        }
        
        if( counter >= segsamples ){
            if( state == 0){
                segsamples = x->sr * boundrand(0.1, 2.0); // silence
            }
            else if( (state == 2) || (state == 6) ){
                segsamples = x->sr * 0.05; // transition
            }
            else if(state == 4){
                segsamples = x->sr * boundrand(1.0, 7.0); // sustain
            }
            else if( (state == 1 || (state == 3) || (state == 5) || (state == 7)) ){
                segsamples = 1; // single sample transition to next state
            }
            counter = 0;
            state = (state + 1) % 8;
        }
    }
    
    x->truncate_units[truncate_count].state = state;
    x->truncate_units[truncate_count].segsamples = segsamples;
    x->truncate_units[truncate_count].counter = counter; // update count
}

void sweepreson(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    int i;
    double *params = x->params;
    double bwfac;
    double minfreq, maxfreq, speed, phase;
    double *q1, *q2;
    double cf, bw;
    double si;
    double fac1, fac2;
    double srate = x->sr;
    long vs = x->vs;
    double *sinewave = x->sinewave;
    int sinelen = x->sinelen ;
    long sweepreson_count;

    ++(*pcount);
    sweepreson_count = params[(*pcount)++]; // note swapped location
    minfreq = params[(*pcount)++];
    maxfreq = params[(*pcount)++];
    bwfac = params[(*pcount)++];
    speed = params[(*pcount)++];
    phase = params[(*pcount)++]; // cannot use this, phase is reentrant

    minfreq = x->sweepreson_units[sweepreson_count].minfreq;
    maxfreq = x->sweepreson_units[sweepreson_count].maxfreq;
    bwfac = x->sweepreson_units[sweepreson_count].bwfac;
    speed = x->sweepreson_units[sweepreson_count].speed;
    phase = x->sweepreson_units[sweepreson_count].phase;
    q1 = x->sweepreson_units[sweepreson_count].q1;
    q2 = x->sweepreson_units[sweepreson_count].q2;
    
    si = ((double) sinelen / srate) * speed;
    fac2 = .5 * (maxfreq - minfreq) ;
    fac1 = minfreq + fac2;
    cf = fac1 + fac2 * sinewave[(int) phase];
    bw = bwfac * cf;
    
    for(i = 0; i < vs; i++ ){
        phase += si;
        while( phase >= sinelen ){
            phase -= sinelen;
        }
        fac2 = .5 * (maxfreq - minfreq) ;
        fac1 = minfreq + fac2;
        if( phase < 0.0 || phase > sinelen - 1){
            phase = 0;
            // post("sweepreson - sine phase out of bounds");
        }
        cf = fac1 + fac2 * sinewave[(int) phase];
        bw = bwfac * cf;
        if(cf < 10 || cf > 8000 || bw < 1 || srate < 100){
            post("sweepreson - danger values, cf %f bw %f sr %f",cf, bw, srate);
        }
        
        // void rsnset2(double cf,double bw,double scl,double xinit,double *a,double srate)
        rsnset2( cf, bw, 2.0, 1.0, q1, srate );
        rsnset2( cf, bw, 2.0, 1.0, q2, srate );
        buf1[i] = reson(buf1[i], q1);
        buf2[i] = reson(buf2[i], q2);
    }
    x->sweepreson_units[sweepreson_count].phase = phase;
}

void slidecomb(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double feedback, delay1, delay2;
    int i;
    int *dv1, *dv2;		/* cmix bookkeeping */
    double delsamp1 = 0, delsamp2 = 0;
    double m1, m2;
    double delay_time;
    double *params = x->params;
    double srate = x->sr;
    double *delayline1;
    double *delayline2;
    int slidecomb_count;
    long vs = x->vs;
    long sample_length;
    long counter;
    
    ++(*pcount);
    slidecomb_count = params[(*pcount)++];
    delay1 = params[(*pcount)++];
    delay2 = params[(*pcount)++];
    feedback = params[(*pcount)++];
    sample_length = params[(*pcount)++];

    delay1 = x->slidecomb_units[slidecomb_count].start_delay;
    delay2 = x->slidecomb_units[slidecomb_count].end_delay;
    sample_length = x->slidecomb_units[slidecomb_count].sample_length;
    counter = x->slidecomb_units[slidecomb_count].counter;
    feedback = x->slidecomb_units[slidecomb_count].feedback;
    dv1 = x->slidecomb_units[slidecomb_count].dv1;
    dv2 = x->slidecomb_units[slidecomb_count].dv2;
    delayline1 = x->slidecomb_units[slidecomb_count].delayline1;
    delayline2 = x->slidecomb_units[slidecomb_count].delayline2;

    for( i = 0; i < vs; i++){
        if(counter < sample_length){
            m2 = (double)counter / (double)sample_length;
            m1 = 1. - m2;
            delay_time = delay1 * m1 + delay2 * m2;
            counter++;
        } else {
            // implement switchback here
            double tmp;
            // post("swapping out slidecomb");
            counter = 0;
            tmp = x->slidecomb_units[slidecomb_count].start_delay;
            delay1 = x->slidecomb_units[slidecomb_count].start_delay = x->slidecomb_units[slidecomb_count].end_delay;
            delay2 = x->slidecomb_units[slidecomb_count].end_delay = tmp;
            // delay_time = delay2;
        }

        if(delay_time > MAX_SLIDECOMB_DELAY){
            delay_time = MAX_SLIDECOMB_DELAY;
        } else if(delay_time < 0.0){
            delay_time = 0.0;
        }
        
        delput2(buf1[i] + (delsamp1 * feedback), delayline1, dv1);
        delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
        buf1[i] = delsamp1;
        
        delput2(buf2[i] + delsamp2*feedback, delayline2, dv2);
        delsamp2 = dliget2(delayline2, delay_time, dv2, srate);
        buf2[i] = delsamp2;
    }
    x->slidecomb_units[slidecomb_count].dv1 = dv1;
    x->slidecomb_units[slidecomb_count].dv2 = dv2;
    x->slidecomb_units[slidecomb_count].counter = counter;
}

void slideflam(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    double feedback, delay1, delay2;
    int i;
    int *dv1, *dv2;        /* cmix bookkeeping */
    double delsamp1 = 0, delsamp2 = 0;
    double m1, m2;
    double delay_time;
    double *params = x->params;
    double srate = x->sr;
    double *delayline1;
    double *delayline2;
    int slideflam_count;
    long vs = x->vs;
    long sample_length;
    long counter;
    
    ++(*pcount);
    slideflam_count = params[(*pcount)++];
    delay1 = params[(*pcount)++];
    delay2 = params[(*pcount)++];
    feedback = params[(*pcount)++];
    sample_length = params[(*pcount)++];
 /*
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
  params[pcount++] = x->slideflam_units[slideflam_count].sample_length = boundrand(0.25,4.0) * x->sr;
  */
    delay1 = x->slideflam_units[slideflam_count].dt1;
    delay2 = x->slideflam_units[slideflam_count].dt2;
    sample_length = x->slideflam_units[slideflam_count].sample_length;
    counter = x->slideflam_units[slideflam_count].counter;
    feedback = x->slideflam_units[slideflam_count].feedback;
    dv1 = x->slideflam_units[slideflam_count].dv1;
    dv2 = x->slideflam_units[slideflam_count].dv2;
    delayline1 = x->slideflam_units[slideflam_count].delayline1;
    delayline2 = x->slideflam_units[slideflam_count].delayline2;
    for( i = 0; i < vs; i++){
        if(counter < sample_length){
            m2 = (double)counter / (double)sample_length;
            m1 = 1. - m2;
            delay_time = delay1 * m1 + delay2 * m2;
            counter++;
        } else {
            double tmp;
            counter = 0;
            // delay_time = delay2;
            counter = 0;
            tmp = x->slideflam_units[slideflam_count].dt1;
            delay1 = x->slideflam_units[slideflam_count].dt1 = x->slideflam_units[slideflam_count].dt2;
            delay2 = x->slideflam_units[slideflam_count].dt2 = tmp;
        }
        if( (delay_time < 0.0) || (delay_time >= MAX_SLIDEFLAM_DELAY) ){
            post("bad delay time: %f\n", delay_time);
            delay_time = MAX_SLIDEFLAM_DELAY;
        }
        delput2(buf1[i] + (delsamp1 * feedback), delayline1, dv1);
        delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
        buf1[i] = delsamp1;
        
        delput2(buf2[i] + delsamp2*feedback, delayline2, dv2);
        delsamp2 = dliget2(delayline2, delay_time, dv2, srate);
        buf2[i] = delsamp2;
    }
    x->slideflam_units[slideflam_count].dv1 = dv1;
    x->slideflam_units[slideflam_count].dv2 = dv2;
    x->slideflam_units[slideflam_count].counter = counter;
}


void bendy(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    // double feedback, delay1, delay2;
    int i;
    int *dv1, *dv2;        /* cmix bookkeeping */
    double delsamp1 = 0, delsamp2 = 0;
    double delay_time;
    double *params = x->params;
    double srate = x->sr;
    double *delayline1;
    double *delayline2;
    int bendy_count;
    long vs = x->vs;
    long counter;
    long segment_samples;
    double frak;
    double val1, val2;
    double segdur;
    ++(*pcount);
    bendy_count = params[(*pcount)++];
    val1 = params[(*pcount)++];
    val2 = params[(*pcount)++];
    
    counter = x->bendy_units[bendy_count].counter;
    val1 = x->bendy_units[bendy_count].val1;
    val2 = x->bendy_units[bendy_count].val2;
    dv1 = x->bendy_units[bendy_count].dv1;
    dv2 = x->bendy_units[bendy_count].dv2;
    delayline1 = x->bendy_units[bendy_count].delayline1;
    delayline2 = x->bendy_units[bendy_count].delayline2;
    segment_samples = x->bendy_units[bendy_count].segment_samples;
    
    for( i = 0; i < vs; i++){
        if(counter <= 0){
            val1 = val2;
            val2 = boundrand(0.01,BENDY_MAXDEL);
            segdur = boundrand(BENDY_MINSEG, BENDY_MAXSEG);
            counter = (long) (segdur * srate);
            segment_samples = counter;
            // post("v1 %f v2 %f segsamps %d\n", val1,val2, segment_samples);
        }
        frak = 1.0 - ((double)counter / (double) segment_samples);
        delay_time = val2 + (frak * (val1 - val2));
        if(delay_time > BENDY_MAXDEL){
            delay_time = BENDY_MAXDEL;
        } else if(delay_time < 0.0){
            delay_time = 0.0;
        }
        delput2(buf1[i], delayline1, dv1); // this is a crasher
        delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
        buf1[i] = delsamp1;
        
        delput2(buf2[i], delayline2, dv2);
        delsamp2 = dliget2(delayline2, delay_time, dv2, srate);
        buf2[i] = delsamp2;
        counter--;
    }
    x->bendy_units[bendy_count].counter = counter;
    x->bendy_units[bendy_count].val1 = val1;
    x->bendy_units[bendy_count].val2 = val2;
    x->bendy_units[bendy_count].dv1 = dv1;
    x->bendy_units[bendy_count].dv2 = dv2;
    x->bendy_units[bendy_count].segment_samples = segment_samples;
}


void reverb1(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    float revtime;
    double *params = x->params;
    long vs = x->vs;
    int reverb1_count;
    double wet, dry, raw_wet;
    double a1,a2,a3,a4;
    double **alpo1, **alpo2;
    int nsects;
    double xnorm;
    LSTRUCT *eel1, *eel2;
    double *dels;
    int i;
    
    ++(*pcount);
    reverb1_count = params[(*pcount)++];
    revtime = params[(*pcount)++];
    raw_wet = params[(*pcount)++];
    wet = sin(1.570796 * raw_wet);
    dry = cos(1.570796 * raw_wet);
    
    dels = x->reverb1_units[reverb1_count].dels;
    for(i = 0; i < 4; i++){
         dels[i] = params[(*pcount)++];
    }
    
    wet = x->reverb1_units[reverb1_count].wet;
    dry = x->reverb1_units[reverb1_count].dry;
    revtime = x->reverb1_units[reverb1_count].revtime;
    alpo1 = x->reverb1_units[reverb1_count].alpo1;
    alpo2 = x->reverb1_units[reverb1_count].alpo2;
    nsects = x->reverb1_units[reverb1_count].nsects;
    xnorm = x->reverb1_units[reverb1_count].xnorm;
    eel1 = x->reverb1_units[reverb1_count].eel1;
    eel2 = x->reverb1_units[reverb1_count].eel2;
    
    if( revtime >= 1. ){
        error("reverb1 does not like feedback values over 1.");
        revtime = .99 ;
    }

    for( i = 0 ; i < vs; i++){
        a1 = allpass(buf1[i], alpo1[0]);
        a2 = allpass(buf1[i], alpo1[1]);
        a3 = allpass(buf1[i], alpo1[2]);
        a4 = allpass(buf1[i], alpo1[3]);
        buf1[i] = (buf1[i] * dry) + (ellipse((a1+a2+a3+a4), eel1, nsects,xnorm) * wet);
        
        
        a1 = allpass(buf2[i], alpo2[0]);
        a2 = allpass(buf2[i], alpo2[1]);
        a3 = allpass(buf2[i], alpo2[2]);
        a4 = allpass(buf2[i], alpo2[3]);
        buf2[i] = (buf2[i] * dry) + (ellipse((a1+a2+a3+a4), eel2, nsects,xnorm) * wet);
    }
}

void ellipseme(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    int i;
    int nsects;
    double xnorm;
    int ellipseme_count;
    double *params = x->params;
    long vs = x->vs;
    int filtercode;
    LSTRUCT *eel1, *eel2;
    
    ++(*pcount);
    ellipseme_count = params[(*pcount)++];
    filtercode = params[(*pcount)++];

    eel1 = x->ellipseme_units[ellipseme_count].eel1;
    eel2 = x->ellipseme_units[ellipseme_count].eel2;
    nsects = x->ellipseme_units[ellipseme_count].nsects;
    xnorm = x->ellipseme_units[ellipseme_count].xnorm;

    for( i = 0; i < vs; i++){
        buf1[i] = ellipse(buf1[i], eel1, nsects, xnorm);
        buf2[i] = ellipse(buf2[i], eel2, nsects, xnorm);
    }
}

void feed1me(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    int i;
    int feed1_count;
    double *params = x->params;
    long vs = x->vs;
    double mindelay, maxdelay, speed1, speed2;
    
    double srate = x->sr;
    double *func1;
    double *func2;
    double *func3;
    double *func4;
    double *delayLine1a;
    double *delayLine2a;
    double *delayLine1b;
    double *delayLine2b;
    int *dv1a, *dv2a;        /* cmix bookkeeping */
    int *dv1b, *dv2b;        /* cmix bookkeeping */
    double delsamp1a=0, delsamp2a=0 ;
    double delsamp1b=0, delsamp2b=0 ;
    double delay1, delay2, feedback1, feedback2;
    double funcSi, funcPhs;
    double putsamp;
    double duration;
    long ddl_func_len = (long) (x->sr * MAX_MINI_DELAY);
    
    ++(*pcount);
    feed1_count = params[(*pcount)++];
    mindelay = params[(*pcount)++];
    maxdelay = params[(*pcount)++];
    speed1 = params[(*pcount)++];
    speed2 = params[(*pcount)++];
    duration = params[(*pcount)++];

    func1 = x->feed1_units[feed1_count].func1;
    func2 = x->feed1_units[feed1_count].func2;
    func3 = x->feed1_units[feed1_count].func3;
    func4 = x->feed1_units[feed1_count].func4;
    delayLine1a = x->feed1_units[feed1_count].delayLine1a;
    delayLine2a = x->feed1_units[feed1_count].delayLine2a;
    delayLine1b = x->feed1_units[feed1_count].delayLine1b;
    delayLine2b = x->feed1_units[feed1_count].delayLine2b;
    dv1a = x->feed1_units[feed1_count].dv1a;
    dv2a = x->feed1_units[feed1_count].dv2a;
    dv1b = x->feed1_units[feed1_count].dv1b;
    dv2b = x->feed1_units[feed1_count].dv2b;
    
    mindelay = x->feed1_units[feed1_count].mindelay;
    maxdelay = x->feed1_units[feed1_count].maxdelay;
    speed1 = x->feed1_units[feed1_count].speed1;
    speed2 = x->feed1_units[feed1_count].speed2;
    duration = x->feed1_units[feed1_count].duration;
    
    if( maxdelay > MAX_MINI_DELAY ){
        error("feed1: too high max delay, adjusted");
        maxdelay = MAX_MINI_DELAY;
    }
    
    funcSi = ((double) FEEDFUNCLEN / srate) / duration;
    funcPhs = x->feed1_units[feed1_count].funcPhs;
    
    for(i = 0; i < vs; i++){
        delay1 = func1[ (int) funcPhs ];
        delay2 = func2[ (int) funcPhs ];
        if(delay1 < 0.0){
            delay1 = 0.0;
        } else if (delay1 > ddl_func_len){
            delay1 = ddl_func_len - 1;
        }
        if(delay2 < 0.0){
            delay2 = 0.0;
        } else if (delay2 > ddl_func_len){
            delay2 = ddl_func_len - 1;
        }
        feedback1 = func3[ (int) funcPhs ];
        feedback2 = func4[ (int) funcPhs ];
        funcPhs += funcSi;
        if( funcPhs >= (double) FEEDFUNCLEN ){
            funcPhs = 0.0;
        }
        buf1[i] = buf1[i] + delsamp1a * feedback1;
        delput2( putsamp, delayLine1a, dv1a);
        delsamp1a = dliget2(delayLine1a, delay1, dv1a, srate);
        putsamp = delsamp1a + (delsamp2a * feedback2);
        delput2( putsamp, delayLine2a, dv2a);
        delsamp2a = dliget2(delayLine2a, delay2, dv2a, srate);
        buf1[i] += delsamp2a;
        putsamp = buf2[i] + delsamp1b * feedback1;
        buf2[i] = putsamp; // zero instead ??
        delput2( putsamp, delayLine1b, dv1b);
        delsamp1b = dliget2(delayLine1b, delay1, dv1b,srate);
        putsamp = delsamp1b + (delsamp2b * feedback2);
        delput2( putsamp, delayLine2b, dv2b);
        delsamp2b = dliget2(delayLine2b, delay2, dv2b, srate);
        buf2[i] += delsamp2b;
    }
    x->feed1_units[feed1_count].funcPhs = funcPhs;
}

void flam1(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    int i;
    int flam1_count;
    int *dv1, *dv2;
    double *delayline1, *delayline2, delaytime;
    long counter, sample_length;
    double *params = x->params;
    double srate = x->sr;
    long vs = x->vs;
    double feedback;
    double delsamp1 = 0, delsamp2 = 0;
    
    ++(*pcount);
    flam1_count = params[(*pcount)++];
    sample_length = params[(*pcount)++];
    delaytime = params[(*pcount)++];

    delaytime = x->flam1_units[flam1_count].dt;
    sample_length = x->flam1_units[flam1_count].sample_length;
    counter = x->flam1_units[flam1_count].counter;
    dv1 = x->flam1_units[flam1_count].dv1;
    dv2 = x->flam1_units[flam1_count].dv2;
    delayline1 = x->flam1_units[flam1_count].delayline1;
    delayline2 = x->flam1_units[flam1_count].delayline2;

    if(delaytime >= FLAM1_MAX_DELAY){
        delaytime = FLAM1_MAX_DELAY * 0.99;
    } else if(delaytime < 0.0){
        delaytime = 0.0;
    }
    for( i = 0; i < vs; i++){

        if( counter < sample_length ){
            feedback = 1.0 - ((double)counter / (double)sample_length);
            delput2(buf1[i] + (delsamp1 * feedback), delayline1, dv1);
            delsamp1 = dliget2(delayline1, delaytime, dv1, srate);
            buf1[i] += delsamp1;
            delput2(buf2[i] + delsamp2*feedback, delayline2, dv2);
            delsamp2 = dliget2(delayline2, delaytime, dv2, srate);
            buf2[i] += delsamp2;
        }
        counter++;
        if( counter >= sample_length){
            counter = 0;
        }
    }
    x->flam1_units[flam1_count].counter = counter;
}

void comb4(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    
    int i, j;
    double insamp;
    double *params = x->params;
    long vs = x->vs;
    int comb4_count;
    double basefreq;
    int dex; // throwaway
    
    ++(*pcount);
    comb4_count = params[(*pcount)++];
    basefreq = params[(*pcount)++];
    for(i = 0; i < 4; i++){
        dex = params[(*pcount)++];
    }
/*
 
 params[pcount++] = COMB4;
 params[pcount++] = comb4_count;
 rvt = 0.99;
 params[pcount++] = basefreq = boundrand(100.0,400.0);
 for( i = 0; i < 4; i++ ){
     dex = (int)floor( boundrand(0.0, 4.0));
     lpt = 1. / basefreq;
     if( dex > 4) { dex = 4; };
     params[pcount++] = dex;
     basefreq *= ratios[dex];
     mycombset(lpt, rvt, 0, x->comb4_units[comb4_count].combs1[i], x->sr);
     mycombset(lpt, rvt, 0, x->comb4_units[comb4_count].combs2[i], x->sr);
 }
 */
    for(i = 0; i < vs; i++){
        insamp = buf1[i];
        buf1[i] = 0.0;
        for(j = 0; j < 4; j++){
            buf1[i] += mycomb(insamp, x->comb4_units[comb4_count].combs1[j]) * 0.25;
        }
        insamp = buf2[i];
        buf2[i] = 0.0;
        for(j = 0; j < 4; j++){
            buf2[i] += mycomb(insamp, x->comb4_units[comb4_count].combs2[j]) * 0.25;
        }
    }
}

void compdist(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    
    int i;
    // double *params = x->params;
    long vs = x->vs;
    double *distortion_function = x->distortion_function;
    int distortion_length = x->distortion_length;
    // double insamp;
    
    ++(*pcount);
    
    for(i = 0; i < vs; i++){
        buf1[i] = dlookup(buf1[i], distortion_function, distortion_length);
        buf2[i] = dlookup(buf2[i], distortion_function, distortion_length);
    }
    
}

void ringfeed(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    int i; //, k;
    /* main variables */
    double *params = x->params;
    long vs = x->vs;
    /*function specific*/
    int ringfeed_count;
    double si1, si2, osc1phs, osc2phs;
    double rescale = 1.5;
    double cf, bw, lpt, rvt;
    
    ++(*pcount);
    ringfeed_count = params[(*pcount)++];
    cf = params[(*pcount)++];
    bw = params[(*pcount)++];
    si1 = params[(*pcount)++];
    si2 = params[(*pcount)++];
    lpt = params[(*pcount)++];
    rvt = params[(*pcount)++];
    /*
     params[pcount++] = RINGFEED;
     params[pcount++] = ringfeed_count;
     params[pcount++] = cf = boundrand(90.0,1500.0);
     params[pcount++] = bw = boundrand(0.02,0.4) * cf;

     params[pcount++] = x->resonfeed_units[ringfeed_count].osc1si = boundrand(90.0,1500.0) * ((double)x->sinelen / x->sr);
     params[pcount++] = x->resonfeed_units[ringfeed_count].osc2si = boundrand(90.0,1500.0) * ((double)x->sinelen / x->sr);
     params[pcount++] = lpt = 1.0 / boundrand(90.0,1500.0);
     params[pcount++] = rvt = boundrand(.01,.8);
     */
    
    si1 = x->resonfeed_units[ringfeed_count].osc1si;
    si2 = x->resonfeed_units[ringfeed_count].osc2si;
    osc1phs = x->resonfeed_units[ringfeed_count].osc1phs;
    osc2phs = x->resonfeed_units[ringfeed_count].osc2phs;

    for(i = 0; i < vs; i++){
        buf1[i] *= oscil(1.0, si1, x->sinewave, x->sinelen, &osc1phs);
        buf1[i] += mycomb(buf1[i], x->resonfeed_units[ringfeed_count].comb1arr);
        buf1[i] = reson(buf1[i], x->resonfeed_units[ringfeed_count].res1q) * rescale;
        buf2[i] *= oscil(1.0, si2,x->sinewave, x->sinelen, &osc2phs);
        buf2[i] += mycomb(buf2[i], x->resonfeed_units[ringfeed_count].comb2arr);
        buf2[i] = reson(buf2[i], x->resonfeed_units[ringfeed_count].res2q) * rescale;
    }
    x->resonfeed_units[ringfeed_count].osc1phs = osc1phs;
    x->resonfeed_units[ringfeed_count].osc2phs = osc2phs;

}

void resonadsr(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    int i;
    float bwfac;
    double *q1, *q2;
    float cf, bw;
    float si;
    double phase;
    double *params = x->params;
    float srate = x->sr;
    long vs = x->vs;
    int resonadsr_count;
    double a,d,r,v1,v2,v3,v4,notedur;
    
    /*function specific*/

    int funclen;
    double *adsrfunc;
    
    ++(*pcount);
    resonadsr_count = params[(*pcount)++];
    a = params[(*pcount)++];
    d = params[(*pcount)++];
    r = params[(*pcount)++];
    v1 = params[(*pcount)++];
    v2 = params[(*pcount)++];
    v3 = params[(*pcount)++];
    v4 = params[(*pcount)++];
    bwfac = params[(*pcount)++];
    notedur = params[(*pcount)++];
 
    /*
     params[pcount++] = RESONADSR;
     params[pcount++] = resonadsr_count;
     params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->a = boundrand(.01,.1);
     params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->d = boundrand(.01,.05);
     params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->r = boundrand(.05,.5);
     params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v1 = boundrand(150.0,4000.0);
     params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v2 = boundrand(150.0,4000.0);
     params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v3 = boundrand(150.0,4000.0);
     params[pcount++] = x->resonadsr_units[resonadsr_count].adsr->v4 = boundrand(150.0,4000.0);
     params[pcount++] = x->resonadsr_units[resonadsr_count].bwfac = boundrand(.03,.7);
     params[pcount++] =  notedur = boundrand(0.7, 1.2);
     
     x->resonadsr_units[resonadsr_count].phs = 0.0;
     */
    q1 = x->resonadsr_units[resonadsr_count].q1;
    q2 = x->resonadsr_units[resonadsr_count].q2;
    phase = x->resonadsr_units[resonadsr_count].phs;
    adsrfunc = x->resonadsr_units[resonadsr_count].adsr->func;
    funclen = x->resonadsr_units[resonadsr_count].adsr->len;
    si = x->resonadsr_units[resonadsr_count].si;
    bwfac = x->resonadsr_units[resonadsr_count].bwfac;

    for(i = 0; i < vs; i++){
        cf = adsrfunc[ (int) phase ];
        bw = bwfac * cf;
        phase += si;
        // this could be DANGEROUS:
        if( phase > funclen - 1){
            phase = funclen - 1;
        }
        rsnset2( cf, bw, 2.0, 1.0, q1, srate );
        rsnset2( cf, bw, 2.0, 1.0, q2, srate );
        buf1[i] = reson(buf1[i], q1);
        buf2[i] = reson(buf2[i], q2);
    }
}

void stv(t_chameleon *x, long *pcount, t_double *buf1, t_double *buf2)
{
    int i;
    /* main variables */
    double *params = x->params;
    float srate = x->sr;
    long vs = x->vs;
    int stv_count;

    /*function specific*/
    double *sinewave = x->sinewave;
    int sinelen = x->sinelen ;
    double *delayline1;
    double *delayline2;
    double fac1, fac2;
    int *dv1, *dv2; /* cmix bookkeeping */
    double delay_time;
    double si1, si2, speed1, speed2, maxdelay;
    double osc1phs, osc2phs;
    
    
    ++(*pcount);
    stv_count = params[(*pcount)++];
    speed1 = params[(*pcount)++];
    speed2 = params[(*pcount)++];
    maxdelay = params[(*pcount)++];
    
    /*
     params[pcount++] = STV;
      params[pcount++] = stv_count;
      params[pcount++] = speed1 = boundrand(.025,0.5);
      params[pcount++] = speed2 = boundrand(.025,0.5);
      params[pcount++] = maxdelay = boundrand(.001,.01);
     */
    dv1 = x->stv_units[stv_count].dv1;
    dv2 = x->stv_units[stv_count].dv2;
    delayline1 = x->stv_units[stv_count].delayline1;
    delayline2 = x->stv_units[stv_count].delayline2;
    si1 = x->stv_units[stv_count].osc1si;
    si2 = x->stv_units[stv_count].osc2si;
    osc1phs = x->stv_units[stv_count].osc1phs;
    osc2phs = x->stv_units[stv_count].osc2phs;
    fac1 = x->stv_units[stv_count].fac1;
    fac2 = x->stv_units[stv_count].fac2;

    for(i = 0; i < vs; i++){
        delay_time = fac1 + oscil(fac2, si1, sinewave, sinelen, &osc1phs);
        if(delay_time < 0.0){
            delay_time = 0.0;
        } else if( delay_time > STV_MAX_DELAY){
            delay_time = STV_MAX_DELAY;
        }
        delput2( buf1[i], delayline1, dv1);
        buf1[i] = dliget2(delayline1, delay_time, dv1,srate);

        delay_time = fac1 + oscil(fac2, si2, sinewave, sinelen, &osc2phs);
        if(delay_time < 0.0){
            delay_time = 0.0;
        } else if( delay_time > STV_MAX_DELAY){
            delay_time = STV_MAX_DELAY;
        }
        delput2( buf2[i], delayline2, dv2);
        buf2[i] = dliget2(delayline2, delay_time, dv2,srate);
    }

    x->stv_units[stv_count].osc1phs = osc1phs;
    x->stv_units[stv_count].osc2phs = osc2phs;
    x->stv_units[stv_count].dv1 = dv1;
    x->stv_units[stv_count].dv2 = dv2;
}
