#include "bashfest.h"

void transpose(t_bashfest *x, int slot, int *pcount)
{
    float *inbuf;
    float *outbuf;
    int i;
    int iphs = 0;
    int ip2;
    float m1, m2;
    float phs = 0;
    int out_frames;
    
    t_event *event = &x->events[slot];
    int in_start = event->in_start;
    int in_frames = event->sample_frames;
    int channels = event->out_channels;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    float *params = x->params;
    
    float tfac;
    int max_safe_samples;

    // 1. Parameter Fetch & Sanity Check
    ++(*pcount);
    tfac = params[ (*pcount)++ ];

    if (tfac <= 0.001f) {
        error("bashfest~: transpose factor %f too low, bypassing", tfac);
        return;
    }

    if (in_frames <= 0) return;

    // 2. Set up Ping-Pong Buffer Pointers
    // We write to a different section of the workbuffer than we read from
    int out_start = (in_start + halfbuffer) % buflen;
    inbuf = event->workbuffer + in_start;
    outbuf = event->workbuffer + out_start;

    // 3. Calculate Output Duration
    out_frames = (float)in_frames / tfac;

    // 4. Bound Check: Memory Write Safety
    // Ensure we don't write past the end of the allocated workbuffer
    max_safe_samples = buflen - out_start;
    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }

    // 5. The Processing Loop
    for (i = 0; i < out_frames; i++) {
        iphs = (int)phs;
        m2 = phs - (float)iphs;
        m1 = 1.0f - m2;

        if (channels == 1) {
            // Read Safety: Check if we can read index and index + 1
            if (iphs + 1 >= in_frames) {
                out_frames = i; // Update final count
                break;
            }
            outbuf[i] = inbuf[iphs] * m1 + inbuf[iphs + 1] * m2;
        }
        else if (channels == 2) {
            ip2 = iphs * 2;
            // Read Safety: We need up to ip2 + 3 (the right channel of the next frame)
            if (ip2 + 3 >= in_frames * 2) {
                out_frames = i; // Update final count
                break;
            }
            outbuf[i * 2]     = inbuf[ip2]     * m1 + inbuf[ip2 + 2] * m2;
            outbuf[i * 2 + 1] = inbuf[ip2 + 1] * m1 + inbuf[ip2 + 3] * m2;
        }
        
        phs += tfac;
    }

    // 6. Update Event State for the next process in the chain
    event->sample_frames = out_frames;
    event->out_start = in_start; // Previous in becomes next out
    event->in_start = out_start; // Current out becomes next in
}

/*
void transpose(t_bashfest *x, int slot, int *pcount)
{
    float *inbuf;
    float *outbuf;
    int i;
    int iphs = 0;
    int ip2;
    float m1, m2;
    float phs = 0;
    int out_frames;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    float *params = x->params;
    //  float srate = x->sr;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int buf_frames = x->buf_frames;
    float tfac;
    int max_safe_samples;
    
    ++(*pcount);
    tfac = params[ (*pcount)++ ];
    
    
    // out_start MUST BE SET WITH RESPECT TO in_start
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    

    
    //  fprintf(stderr,"TRANSPOSE: in %d out %d\n", w->in_start, w->out_start);
    out_frames = (float) in_frames / tfac ;

 
    max_safe_samples = buflen - out_start;
    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    
    for( i = 0; i < out_frames * channels; i += channels ){
        iphs = phs;
        m2 = phs - iphs;
        m1 = 1. - m2;
        
        if( channels == 1 ){
            *outbuf++ = inbuf[iphs] * m1 + inbuf[ iphs + 1] * m2 ;
            
        } else if( channels == 2 ){
            ip2 = iphs * 2;
            if (ip2 + 3 >= buflen - in_start) {
                break; // Stop before we read past the allocated source
            }
            *outbuf++ = inbuf[ip2] * m1 + inbuf[ ip2 + 2] * m2 ;
            *outbuf++ = inbuf[ip2 + 1] * m1 + inbuf[ ip2 + 3] * m2 ;
        }
        phs += tfac ;
        
    }
    
    x->events[slot].sample_frames =  out_frames;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
    
}
*/

void ringmod(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *sinewave = x->sinewave;
    float *inbuf, *outbuf;
    int sinelen = x->sinelen;
    int frames = event->sample_frames;
    int channels = event->out_channels;
    float *params = x->params;
    float srate = x->sr;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int i;
    float phase = 0.0;
    float si;
    float rmodFreq;
    int max_safe_samples;
    
    // 1. Parameter Fetch
    ++(*pcount);
    rmodFreq = params[(*pcount)++];
    
    // 2. Set up Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen;
    inbuf = event->workbuffer + in_start;
    outbuf = event->workbuffer + out_start;
    
    // 3. Write Safety Check
    max_safe_samples = buflen - out_start;
    if (frames * channels > max_safe_samples) {
        frames = max_safe_samples / channels;
    }
    
    // 4. Calculate Phase Increment
    if (srate <= 0) return;
    si = ((float) sinelen / srate) * rmodFreq ;
    
    // 5. Processing Loop
    for(i = 0; i < frames; i++ ){
        
        // Wrap phase BEFORE using it as an index
        while( phase >= sinelen ) phase -= (float)sinelen;
        while( phase < 0 )        phase += (float)sinelen;
        
        float mod = sinewave[(int)phase];
        
        if( channels == 1 ){
            *outbuf++ = *inbuf++ * mod;
        } else if( channels == 2 ){
            *outbuf++ = *inbuf++ * mod;
            *outbuf++ = *inbuf++ * mod;
        }
        
        phase += si;
    }

    // 6. Update Event State
    event->sample_frames = frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void ringmod(t_bashfest *x, int slot, int *pcount)
{
    float *sinewave = x->sinewave;
    float *inbuf, *outbuf;
    int sinelen = x->sinelen;
    int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    float *params = x->params;
    float srate = x->sr;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    //  int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int i;
    float phase = 0.0;
    float si;
    float rmodFreq;
    int max_safe_samples;
    
    ++(*pcount);
    rmodFreq = params[(*pcount)++];
    
    //  fprintf(stderr,"-*-*- EXECUTING RINGMOD -*-*-\n");
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    
    max_safe_samples = buflen - out_start;
    if (frames * channels > max_safe_samples) {
        frames = max_safe_samples / channels;
    }
    
    si = ((float) sinelen / srate) * rmodFreq ;
    
    //  inbuf = inbuf + in_start ;
    
    for(i = 0; i < frames*channels; i += channels ){
        *outbuf++ = *inbuf++ * sinewave[(int)phase];
        if( channels == 2 ){
            *outbuf++ = *inbuf++ * sinewave[(int)phase];
        }
        phase += si;
        while( phase > sinelen )
            phase -= sinelen;
    }
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
}
*/

/*
void retrograde(t_bashfest *x, int slot, int *pcount)
{
    
    int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    int samples_to_copy;
    int max_safe;
    //  float *params = x->params;
    //  float srate = x->sr;
    int i ;
    int swap1, swap2;
    float tmpsamp;
    
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;

    ++(*pcount);
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;

    max_safe_samples = buflen - out_start;
    if (frames * channels > max_safe_samples) {
        error("bashfest~: retrograde would overrun buffer, truncating");
        frames = max_safe_samples / channels;
    }

    
    samples_to_copy = in_frames * channels;
    max_safe = buflen - out_start;
    if (samples_to_copy > max_safe) {
        samples_to_copy = max_safe;
    }
    
    memcpy(outbuf, inbuf, samples_to_copy * sizeof(float) );
    
    if( channels == 1 ){
        for(i = 0; i < (frames/2)  ; i++ ){
            swap2 = (frames - 1 - i);
            tmpsamp = outbuf[i];
            outbuf[i] = outbuf[swap2];
            outbuf[swap2] = tmpsamp;
        }
    }
    
    // this would also work for mono, but we'll save a few multiplies
    else {
        for(i = 0; i < (frames/2)   ; i++ ){
            swap1 = i * channels ;
            swap2 = (frames - 1 - i) * channels;
            tmpsamp = outbuf[swap1];
            outbuf[swap1] = outbuf[swap2];
            outbuf[swap2] = tmpsamp;
            ++swap1;
            ++swap2;
            tmpsamp = outbuf[swap1];
            outbuf[swap1] = outbuf[swap2];
            outbuf[swap2] = tmpsamp;
            
        }
    }
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
}
*/

void retrograde(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    int frames = event->sample_frames;
    int channels = event->out_channels;
    int i;
    float tmpsamp;
    
    float *inbuf, *outbuf;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;

    // 1. Advance parameter count (Retrograde has no extra params, but we skip its code)
    ++(*pcount);
    
    if (frames <= 0) return;

    // 2. Set up Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen;
    inbuf = event->workbuffer + in_start;
    outbuf = event->workbuffer + out_start;

    // 3. Bound Check for Memory Safety
    max_safe_samples = buflen - out_start;
    if (frames * channels > max_safe_samples) {
        // Truncate frames if they would walk off the end of the workbuffer
        frames = max_safe_samples / channels;
    }
    
    // 4. Initial Copy
    // We copy the sound forward into the new location, then reverse it in place.
    memcpy(outbuf, inbuf, frames * channels * sizeof(float));
    
    // 5. The Reverse (In-Place Swap)
    if (channels == 1) {
        for (i = 0; i < (frames / 2); i++) {
            int target = (frames - 1 - i);
            tmpsamp = outbuf[i];
            outbuf[i] = outbuf[target];
            outbuf[target] = tmpsamp;
        }
    }
    else if (channels == 2) {
        for (i = 0; i < (frames / 2); i++) {
            int step1 = i * 2;
            int step2 = (frames - 1 - i) * 2;
            
            // Swap Left Channel
            tmpsamp = outbuf[step1];
            outbuf[step1] = outbuf[step2];
            outbuf[step2] = tmpsamp;
            
            // Swap Right Channel
            tmpsamp = outbuf[step1 + 1];
            outbuf[step1 + 1] = outbuf[step2 + 1];
            outbuf[step2 + 1] = tmpsamp;
        }
    }
    
    // 6. Update Event State
    // The current out_start becomes the new in_start for the next effect.
    event->sample_frames = frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void comber(t_bashfest *x, int slot, int *pcount)
{
    int channels = x->events[slot].out_channels;
    float *params = x->params;
    float srate = x->sr;
    float *delayline1 = x->delayline1;
    float *delayline2 = x->delayline2;
    float max_delay = x->maxdelay ;
    int buf_frames = x->buf_frames;
    int out_frames ;
    float overhang, revtime, delay ;
    
    int i;
    
    int max_safe_samples;
    
    //  int fade_frames;
    //float fadegain;
    //int fadestart;
    
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;

    ++(*pcount);
    delay = params[(*pcount)++];
    revtime = params[(*pcount)++];
    overhang = params[(*pcount)++];
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    
    max_safe_samples = buflen - out_start;
    
    if( delay <= 0.0 ){
        error("comber got bad delay value\n");
        return;
    }
    // paranoia
    if( delay > max_delay * 0.95){
        delay = max_delay * 0.95;
    }
    if( overhang < COMBFADE )
        overhang = COMBFADE;
    
    out_frames = in_frames + overhang * srate ;

    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    
    //  combsamps = delay * srate + 20 ;
    mycombset(delay,revtime,0,delayline1,srate);
    if( channels == 2 )
        mycombset(delay,revtime,0,delayline2,srate);
    
    // ADD IN ORIGINAL SIGNAL
    for( i = 0; i < in_frames*channels; i += channels){
        *outbuf++ += mycomb(*inbuf++, delayline1);
        if( channels == 2 ){
            *outbuf++ += mycomb(*inbuf++,delayline2);
        }
    }
    
    for( i = in_frames * channels; i < out_frames*channels; i += channels){
        *outbuf++ = mycomb( 0.0 , delayline1);
        if( channels == 2 ){
            *outbuf++ = mycomb( 0.0 , delayline2);
        }
    }
    // possible bug here, but need to reimplement somehow
    
     fade_frames = COMBFADE * srate;
     fadestart = (out_frames - fade_frames) * channels ;
     for( i = 0; i < fade_frames * channels; i += channels ){
     fadegain = 1.0 - (float) i / (float) (fade_frames * channels)  ;
     *(inbuf + fadestart + i) *= fadegain;
     if(channels == 2){
     *(inbuf + fadestart + i + 1) *= fadegain;
     }
     }
     
    x->events[slot].sample_frames = out_frames;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
}
*/


void comber(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    int channels = event->out_channels;
    float *params = x->params;
    float srate = x->sr;
    int in_frames = event->sample_frames;
    float overhang, revtime, delay;
    int i;

    ++(*pcount);
    delay = params[(*pcount)++];
    revtime = params[(*pcount)++];
    overhang = params[(*pcount)++];
    
    if (srate <= 0 || in_frames <= 0) return;

    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf_start = event->workbuffer + out_start; // Store the absolute start
    float *outbuf = outbuf_start;
    float *out_limit = outbuf + x->halfbuffer;

    int out_frames = in_frames + (int)(overhang * srate);
    if (out_frames * channels > x->halfbuffer) out_frames = x->halfbuffer / channels;

    mycombset(delay, revtime, 0, x->delayline1, srate);
    if (channels == 2) mycombset(delay, revtime, 0, x->delayline2, srate);
    
    for (i = 0; i < out_frames; i++) {
        if (outbuf + channels > out_limit) { out_frames = i; break; }
        if (i < in_frames) {
            if (channels == 1) {
                float insamp = *inbuf++;
                *outbuf++ = insamp + mycomb(insamp, x->delayline1);
            } else {
                float insL = *inbuf++; float insR = *inbuf++;
                *outbuf++ = insL + mycomb(insL, x->delayline1);
                *outbuf++ = insR + mycomb(insR, x->delayline2);
            }
        } else {
            if (channels == 1) *outbuf++ = mycomb(0.0f, x->delayline1);
            else { *outbuf++ = mycomb(0.0f, x->delayline1); *outbuf++ = mycomb(0.0f, x->delayline2); }
        }
    }

    // SAFE FADEOUT:
    int fade_frames = (int)(COMBFADE * srate);
    if (fade_frames > out_frames) fade_frames = out_frames;
    // Calculate pointer relative to the CURRENT outbuf position
    float *fade_ptr = outbuf - (fade_frames * channels);
    // Boundary double-check
    if (fade_ptr >= outbuf_start) {
        for (i = 0; i < fade_frames; i++) {
            float fadegain = 1.0f - ((float)i / (float)fade_frames);
            *fade_ptr++ *= fadegain; if (channels == 2) *fade_ptr++ *= fadegain;
        }
    }

    event->sample_frames = out_frames;
    event->out_start = in_start; event->in_start = out_start;
}

/*
void flange(t_bashfest *x, int slot, int *pcount)
{
    int i;
    float si;
    float mindel, maxdel;
    float fac1, fac2;
    int dv1[2], dv2[2];
    float delsamp1, delsamp2 ;
    float delay_time;
    float speed, feedback, phase, minres, maxres;
    float hangover ;
    int hangframes ;
    int channels = x->events[slot].out_channels;
    float *params = x->params;
    float srate = x->sr;
    float *delayline1 = x->delayline1;
    float *delayline2 = x->delayline2;
    float max_delay = x->maxdelay ;
    float *sinewave = x->sinewave;
    int sinelen = x->sinelen ;
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start;
    int in_frames = x->events[slot].sample_frames;
    int out_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;
    
    
    ++(*pcount);
    minres = params[(*pcount)++];
    maxres = params[(*pcount)++];
    speed = params[(*pcount)++];
    feedback = params[(*pcount)++];
    phase = params[(*pcount)++];
    
    hangover = feedback * 0.25 ; // maybe log relation
    hangframes = srate * hangover ;
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    
    max_safe_samples = buflen - out_start;
    
    if( minres <= 0. || maxres <= 0. ){
        error("flange: got zero frequency resonances as input");
        return;
    }
    mindel = 1.0/maxres;
    maxdel = 1.0/minres;
    // added safety
    if( maxdel > max_delay * 0.95 ){
        maxdel = max_delay * 0.95;
        error("flange: too large delay time shortened");
    }
    
    delset2(delayline1, dv1, max_delay,srate);
    if( channels == 2 ){
        delset2(delayline2, dv2, max_delay,srate);
    }
    
    
    si = ((float) sinelen/srate) * speed ;
    
    if( phase > 1.0 ){
        phase = 0;
        error("flange: given > 1 initial phase");
    }
    delsamp1 = delsamp2 = 0;
    phase *= sinelen;
    fac2 = .5 * (maxdel - mindel);
    fac1 = mindel + fac2;
    
    out_frames = in_frames;
    
    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    
    for(i = 0; i < out_frames * channels; i += channels ){
  
        delay_time = fac1 + fac2 *  sinewave[(int) phase];
        if( delay_time < .00001 ){
            delay_time = .00001;
        } else if(delay_time >= maxdel)
        {
            delay_time = maxdel;
        }
        phase += si;
        while( phase > sinelen )
            phase -= sinelen;
        delput2( *inbuf + delsamp1*feedback, delayline1, dv1);
        delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
        *outbuf++ = (*inbuf++ + delsamp1) ;
        if( channels == 2 ){
            delput2( *inbuf+delsamp2*feedback, delayline2, dv2);
            delsamp2 = dliget2(delayline2, delay_time, dv2, srate);
            *outbuf++ = (*inbuf++ + delsamp2) ;
        }
    }

    // suspected bug
    
     for(i = 0; i < hangframes*channels; i += channels ){
     
     delay_time = fac1 + fac2 *  sinewave[ (int) phase ];
     if( delay_time < .00001 ){
     delay_time = .00001;
     }
     phase += si;
     while( phase > sinelen )
     phase -= sinelen;
     delput2( delsamp1*feedback, delayline1, dv1);
     delsamp1 = dliget2(delayline1, delay_time, dv1,srate);
     *outbuf++ = delsamp1 ;
     if( channels == 2 ){
     delput2( delsamp2*feedback, delayline2, dv2);
     delsamp2 = dliget2(delayline2, delay_time, dv2,srate);
     *outbuf++ = delsamp2 ;
     }
     }
     
    x->events[slot].sample_frames += hangframes;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen;
    
}
*/

void flange(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    int i;
    float si;
    float mindel, maxdel;
    float fac1, fac2;
    int dv1[2], dv2[2]; /* bookkeeping for the delay line */
    float delsamp1, delsamp2 ;
    float delay_time;
    float speed, feedback, phase, minres, maxres;
    float hangover ;
    int hangframes ;
    int channels = event->out_channels;
    float *params = x->params;
    float srate = x->sr;
    float *delayline1 = x->delayline1;
    float *delayline2 = x->delayline2;
    float max_delay = x->maxdelay ;
    float *sinewave = x->sinewave;
    int sinelen = x->sinelen ;
    int in_frames = event->sample_frames;
    int out_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    
    // 1. Parameter Fetch
    ++(*pcount);
    minres = params[(*pcount)++];
    maxres = params[(*pcount)++];
    speed = params[(*pcount)++];
    feedback = params[(*pcount)++];
    phase = params[(*pcount)++];
    
    if (srate <= 0 || in_frames <= 0) return;

    // 2. Pointer Setup (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen ;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *buffer_end = event->workbuffer + buflen;

    // 3. Tail Calculation
    hangover = feedback * 0.25f ;
    hangframes = (int)(srate * hangover) ;
    out_frames = in_frames + hangframes;

    // 4. Delay Bounds Safety
    if( minres <= 0.1f ) minres = 0.1f;
    if( maxres <= 0.1f ) maxres = 0.1f;
    
    mindel = 1.0f / maxres;
    maxdel = 1.0f / minres;
    
    // Ensure delay doesn't exceed allocated delay line memory
    if( maxdel > max_delay * 0.95f ) maxdel = max_delay * 0.95f;
    if( mindel > maxdel ) mindel = maxdel * 0.5f;

    // 5. Reset Delay Lines
    delset2(delayline1, dv1, max_delay, srate);
    if( channels == 2 ) delset2(delayline2, dv2, max_delay, srate);
    
    // 6. Oscillator Setup
    si = ((float) sinelen / srate) * speed ;
    if( phase > 1.0f ) phase = 0.0f;
    phase *= sinelen; // Initial phase
    
    fac2 = 0.5f * (maxdel - mindel);
    fac1 = mindel + fac2;
    delsamp1 = delsamp2 = 0;

    // 7. Loop 1: Main Signal Processing
    for(i = 0; i < in_frames; i++ ){
        if (outbuf + channels > buffer_end) {
            out_frames = i;
            goto update_state;
        }

        // Calculate modulated delay time
        delay_time = fac1 + fac2 * sinewave[(int) phase];
        
        // Safety: ensure index is never out of bounds [0..sinelen-1]
        phase += si;
        while( phase >= sinelen ) phase -= (float)sinelen;
        while( phase < 0 )        phase += (float)sinelen;

        if( channels == 1 ){
            float insamp = *inbuf++;
            delput2( insamp + delsamp1 * feedback, delayline1, dv1);
            delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
            *outbuf++ = insamp + delsamp1;
        } else {
            float insampL = *inbuf++;
            float insampR = *inbuf++;
            
            delput2( insampL + delsamp1 * feedback, delayline1, dv1);
            delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
            *outbuf++ = insampL + delsamp1;
            
            delput2( insampR + delsamp2 * feedback, delayline2, dv2);
            delsamp2 = dliget2(delayline2, delay_time, dv2, srate);
            *outbuf++ = insampR + delsamp2;
        }
    }

    // 8. Loop 2: Process the Hangover (Tail)
    for(i = in_frames; i < out_frames; i++ ){
        if (outbuf + channels > buffer_end) {
            out_frames = i;
            break;
        }
        
        delay_time = fac1 + fac2 * sinewave[ (int) phase ];
        phase += si;
        while( phase >= sinelen ) phase -= (float)sinelen;
        
        if( channels == 1 ){
            delput2( delsamp1 * feedback, delayline1, dv1);
            delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
            *outbuf++ = delsamp1 ;
        } else {
            delput2( delsamp1 * feedback, delayline1, dv1);
            delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
            *outbuf++ = delsamp1 ;
            
            delput2( delsamp2 * feedback, delayline2, dv2);
            delsamp2 = dliget2(delayline2, delay_time, dv2, srate);
            *outbuf++ = delsamp2 ;
        }
    }

update_state:
    // 9. Update Event State
    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void butterme(t_bashfest *x, int slot, int *pcount)
{
    
    int ftype;
    float cutoff, cf, bw;
    int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    float *params = x->params;
    float srate = x->sr;
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    //  int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;
    
    ++(*pcount);
    ftype = params[(*pcount)++];
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    
    max_safe_samples = buflen - out_start;
    if (frames * channels > max_safe_samples) {
        // error("bashfest~: retrograde would overrun buffer, truncating");
        frames = max_safe_samples / channels;
    }
    
    if(ftype == HIPASS){
        cutoff = params[(*pcount)++];
        butterHipass(inbuf, outbuf, cutoff, frames, channels, srate);
    }
    else if(ftype == LOPASS){
        cutoff = params[(*pcount)++];
        butterLopass(inbuf, outbuf, cutoff, frames, channels, srate);
    }
    else if(ftype == BANDPASS){
        cf = params[(*pcount)++];
        bw = params[(*pcount)++];
        butterBandpass(inbuf, outbuf, cf, bw, frames, channels, srate);
    } else {
        error("%d not a valid Butterworth filter",ftype);
        return;
    }
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
}
*/

void butterme(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    int ftype;
    float cutoff, cf, bw;
    int frames = event->sample_frames;
    int channels = event->out_channels;
    float *params = x->params;
    float srate = x->sr;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    
    // 1. Parameter Fetch
    ++(*pcount);
    ftype = params[(*pcount)++];
    
    if (srate <= 0 || frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    
    // 3. Memory Boundary Safety
    int max_safe_samples = buflen - out_start;
    if (frames * channels > max_safe_samples) {
        frames = max_safe_samples / channels;
    }

    // 4. Branch Filter Processing
    // We sanitize frequencies to be between 20Hz and 45% of sample rate
    float min_freq = 20.0f;
    float max_freq = srate * 0.45f;

    if(ftype == HIPASS){
        cutoff = params[(*pcount)++];
        if(cutoff < min_freq) cutoff = min_freq;
        if(cutoff > max_freq) cutoff = max_freq;
        butterHipass(inbuf, outbuf, cutoff, frames, channels, srate);
    }
    else if(ftype == LOPASS){
        cutoff = params[(*pcount)++];
        if(cutoff < min_freq) cutoff = min_freq;
        if(cutoff > max_freq) cutoff = max_freq;
        butterLopass(inbuf, outbuf, cutoff, frames, channels, srate);
    }
    else if(ftype == BANDPASS){
        cf = params[(*pcount)++];
        bw = params[(*pcount)++];
        if(cf < min_freq) cf = min_freq;
        if(cf > max_freq) cf = max_freq;
        if(bw < 5.0f) bw = 5.0f; // Prevent division by zero/instability in bandpass
        butterBandpass(inbuf, outbuf, cf, bw, frames, channels, srate);
    }
    else {
        // Fallback: If ftype is invalid, copy input to output so the chain isn't broken
        error("bashfest~: %d not a valid Butterworth filter type", ftype);
        memcpy(outbuf, inbuf, frames * channels * sizeof(float));
    }

    // 5. Update Event State
    event->sample_frames = frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void truncateme(t_bashfest *x, int slot, int *pcount)
{
    float shortdur ;
    int out_frames;
    int i;
    float fadegain ;
    int fade_frames;
    int fadestart;
    float fadeout;
    int channels = x->events[slot].out_channels;
    float *params = x->params;
    float srate = x->sr;
    
    float *inbuf, *outbuf;
    int in_start;
    int out_start;
    int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    
    ++(*pcount);
    shortdur = params[ (*pcount)++ ];
    fadeout = params[ (*pcount)++ ];
    fade_frames = fadeout * srate ;
    out_frames = shortdur * srate ;
    if( out_frames >= in_frames ){
        // error("truncation requesting >= original duration, no truncation");
        return;
    }
    
    in_start = x->events[slot].in_start;
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    
    
    if( fade_frames <= 0 ){
        error("truncation with 0 length fade!");
        return;
    }
    
    if( fade_frames > out_frames ){
        error("truncation requested fadeout > new duration, adjusting...");
        fade_frames = out_frames;
    }
    
    memcpy(outbuf, inbuf, in_frames * sizeof(float) );
    
    fadestart = (out_frames - fade_frames) * channels ;
    
    for( i = 0; i < fade_frames * channels; i += channels ){
        fadegain = 1.0 - (float) i / (float) (fade_frames * channels)  ;
        outbuf[fadestart + i]   *= fadegain;
        if( channels == 2 ){
            outbuf[ fadestart + i + 1] *= fadegain;
        }
    }
    
    x->events[slot].sample_frames = out_frames ;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
}
*/

void truncateme(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float shortdur, fadeout;
    int out_frames, fade_frames, in_frames;
    int i;
    int channels = event->out_channels;
    float *params = x->params;
    float srate = x->sr;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    
    // 1. Parameter Fetch
    ++(*pcount);
    shortdur = params[ (*pcount)++ ];
    fadeout = params[ (*pcount)++ ];
    
    in_frames = event->sample_frames;
    if (srate <= 0 || in_frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen ;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *buffer_end = event->workbuffer + buflen;

    // 3. Calculate New Duration
    out_frames = (int)(shortdur * srate);
    fade_frames = (int)(fadeout * srate);

    // If truncation isn't actually shortening the file,
    // we still MUST copy the data to the new ping-pong position.
    if( out_frames >= in_frames ){
        out_frames = in_frames;
    }
    
    // 4. Memory Boundary Safety
    // FIXED: Pointer - Pointer calculation
    if ( (outbuf + (out_frames * channels)) > buffer_end ) {
        out_frames = (int)((buffer_end - outbuf) / channels);
    }

    // 5. Initial Copy
    if (out_frames > 0) {
        memcpy(outbuf, inbuf, out_frames * channels * sizeof(float));
    }

    // 6. Apply Fadeout
    if( fade_frames > out_frames ) {
        fade_frames = out_frames;
    }

    if (fade_frames > 0) {
        int fadestart = out_frames - fade_frames;
        float *fade_ptr = outbuf + (fadestart * channels);
        
        for( i = 0; i < fade_frames; i++ ){
            float fadegain = 1.0f - ((float)i / (float)fade_frames);
            *fade_ptr++ *= fadegain;
            if( channels == 2 ) {
                *fade_ptr++ *= fadegain;
            }
        }
    }
    
    // 7. Update Event State
    event->sample_frames = out_frames ;
    event->out_start = in_start;
    event->in_start = out_start;
}


/*
void sweepreson(t_bashfest *x, int slot, int *pcount)
{
    int i;
    float bwfac;
    float minfreq, maxfreq, speed, phase;
    float q1[5], q2[5];
    float cf, bw;
    float si;
    float fac1, fac2;
    //  float inmax, outmax, rescale ;
    //  int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    float *params = x->params;
    float srate = x->sr;
    float *sinewave = x->sinewave;
    int sinelen = x->sinelen ;
    
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int out_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;
    
    ++(*pcount);
    minfreq = params[(*pcount)++];
    maxfreq = params[(*pcount)++];
    bwfac = params[(*pcount)++];
    speed = params[(*pcount)++];
    phase = params[(*pcount)++];
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    
    max_safe_samples = buflen - out_start;
    
    si = ((float) sinelen / srate) * speed ;
    
    if( phase > 1.0 ){
        phase = 0;
        error("sweepreson: given > 1 initial phase");
    }
    
    phase *= sinelen;
    fac2 = .5 * (maxfreq - minfreq) ;
    fac1 = minfreq + fac2;
    
    cf = fac1 + fac2 * sinewave[(int) phase];
    bw = bwfac * cf;
    rsnset2( cf, bw, 2.0, 0.0, q1, srate );
    if( channels == 2 ){
        rsnset2( cf, bw, 2.0, 0.0, q2, srate );
    }
    
    out_frames = in_frames;
    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    for(i = 0; i < out_frames; i++ ){
        // homemade oscillator
        
        phase += si;
        while( phase >= sinelen )
            phase -= sinelen;
        
        
        fac2 = .5 * (maxfreq - minfreq) ;
        fac1 = minfreq + fac2;
        
        cf = fac1 + fac2 * sinewave[(int) phase];
        bw = bwfac * cf;
        if(cf < 10 || cf > 8000 || bw < 1 || srate < 100){
            post("danger values, cf %f bw %f sr %f",cf, bw, srate);
        }
        rsnset2( cf, bw, 2.0, 1.0, q1, srate );
        // clicks stop if we don't apply filter above, and if attacks come too fast
        *outbuf++ = reson(*inbuf++, q1);
        
        if( channels == 2 ){
            
            //  rsnset2( cf, bw, 2.0, 1.0, q2, srate );
            *outbuf++ = reson(*inbuf++, q2);
            
        }
    }
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
    
}
*/

void sweepreson(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    int i;
    float bwfac;
    float minfreq, maxfreq, speed, phase;
    float q1[5], q2[5]; // Filter coefficients and history
    float cf, bw;
    float si;
    float fac1, fac2;
    int channels = event->out_channels;
    float *params = x->params;
    float srate = x->sr;
    float *sinewave = x->sinewave;
    int sinelen = x->sinelen;
    
    int in_frames = event->sample_frames;
    int out_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    
    // 1. Parameter Fetch
    ++(*pcount);
    minfreq = params[(*pcount)++];
    maxfreq = params[(*pcount)++];
    bwfac = params[(*pcount)++];
    speed = params[(*pcount)++];
    phase = params[(*pcount)++];
    
    if (srate <= 0 || in_frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen ;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *buffer_end = event->workbuffer + buflen;

    // 3. Initialize Phase and Increments
    si = ((float) sinelen / srate) * speed ;
    if( phase > 1.0f ) phase = 0.0f;
    phase *= (float)sinelen;
    
    fac2 = 0.5f * (maxfreq - minfreq);
    fac1 = minfreq + fac2;
    
    // 4. Boundary Safety
    out_frames = in_frames;
    if (outbuf + (out_frames * channels) > buffer_end) {
        out_frames = (int)((buffer_end - outbuf) / channels);
    }

    // 5. Initialize Filter Coefficients (Clear history)
    // We do one initial set with xinit=0 to clear the history buffers q1 and q2
    cf = fac1 + fac2 * sinewave[(int) phase];
    bw = bwfac * cf;
    if (bw < 1.0f) bw = 1.0f;
    
    rsnset2(cf, bw, 2.0f, 0.0f, q1, srate);
    if( channels == 2 ) {
        rsnset2(cf, bw, 2.0f, 0.0f, q2, srate);
    }
    
    // 6. Processing Loop
    for(i = 0; i < out_frames; i++ ){
        
        // Calculate new coefficients for this sample
        cf = fac1 + fac2 * sinewave[(int) phase];
        bw = bwfac * cf;

        // Sanitization to prevent filter blow-up
        if (cf < 20.0f) cf = 20.0f;
        if (cf > srate * 0.45f) cf = srate * 0.45f;
        if (bw < 1.0f) bw = 1.0f;

        // Update coefficients but KEEP history (xinit = 1.0)
        rsnset2(cf, bw, 2.0f, 1.0f, q1, srate);
        
        if( channels == 1 ){
            *outbuf++ = reson(*inbuf++, q1);
        } else {
            // Keep q2 in sync if stereo
            rsnset2(cf, bw, 2.0f, 1.0f, q2, srate);
            *outbuf++ = reson(*inbuf++, q1);
            *outbuf++ = reson(*inbuf++, q2);
        }

        // Advance LFO phase
        phase += si;
        while( phase >= sinelen ) phase -= (float)sinelen;
        while( phase < 0 )        phase += (float)sinelen;
    }

    // 7. Update Event State
    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void slidecomb(t_bashfest *x, int slot, int *pcount)
{
    float overhang, feedback, delay1, delay2;
    // int combsamps;
    int i;
    //  int fade_frames;
    // float fadegain;
    // int fadestart;
    int dv1[2], dv2[2];
    float delsamp1 = 0, delsamp2 = 0;
    float m1, m2;
    float delay_time;
    int out_frames ;
    
    int channels = x->events[slot].out_channels;
    int buf_frames = x->buf_frames;
    float *params = x->params;
    float srate = x->sr;
    
    float max_delay = x->maxdelay; // adding protective pad
    float *delayline1 = x->delayline1;
    float *delayline2 = x->delayline2;
    
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;

    ++(*pcount);
    delay1 = params[(*pcount)++];
    delay2 = params[(*pcount)++];
    feedback = params[(*pcount)++];
    overhang = params[(*pcount)++];
    
    // post("del1 %f del2 %f srate %f",delay1,delay2, srate);
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;

    if( overhang < COMBFADE )
        overhang = COMBFADE;
    
    
    out_frames = in_frames + overhang * srate ;
    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    if(out_frames <= 0){
        post("bad outframes %d",out_frames);
        return;
    }
    if( out_frames > buf_frames / 2 ){
        out_frames = buf_frames / 2 ;
    }
    delset2(delayline1, dv1, max_delay, srate);
    if( channels == 2 ){
        delset2(delayline2, dv2, max_delay, srate);
    }
    
    
    
    for( i = 0; i < in_frames*channels; i += channels){
        m2 = (float) i / (float) (out_frames * channels) ;
        m1 = 1. - m2;
        delay_time = delay1 * m1 + delay2 * m2 ;
        if(delay_time >= max_delay * 0.95 || delay_time < 0.0)
            delay_time = 0.0;
        delput2(*inbuf + delsamp1*feedback, delayline1, dv1);
        delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
        *outbuf++ = *inbuf++ + delsamp1;
        if( channels == 2 ){
            delput2( *inbuf + delsamp2*feedback, delayline2, dv2);
            delsamp2 = dliget2(delayline2, delay_time, dv2, srate);
            *outbuf++ = *inbuf++ + delsamp2 ;
        }
    }
    
    for( i = in_frames * channels; i < out_frames*channels; i += channels){
        m2 = (float) i / (float) (out_frames * channels) ;
        m1 = 1. - m2;
        delay_time = delay1 * m1 + delay2 * m2 ;
        if(delay_time >= max_delay * 0.95 || delay_time < 0.0)
            delay_time = 0.0;
        delput2( delsamp1*feedback, delayline1, dv1);
        *outbuf++ = delsamp1 = dliget2( delayline1, delay_time, dv1, srate );
        if( channels == 2 ){
            delput2( delsamp2*feedback, delayline2, dv2);
            *outbuf++ = delsamp2 = dliget2( delayline2, delay_time, dv2, srate );
        }
    }
    // test if this is the problem:
    
     fade_frames = COMBFADE * srate;
     if(fade_frames <= out_frames && fade_frames > 0){
     fadestart = (out_frames - fade_frames) * channels ;
     for( i = 0; i < fade_frames * channels; i += channels ){
     fadegain = 1.0 - (float) i / (float) (fade_frames * channels)  ;
     *(outbuf + fadestart + i) *= fadegain;
     if( channels == 2 ){
     *(outbuf + fadestart + i + 1) *= fadegain;
     }
     }
     }
     
    x->events[slot].sample_frames = out_frames;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
    
}
*/

void slidecomb(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float overhang, feedback, delay1, delay2;
    int i;
    int dv1[2], dv2[2]; /* cmix bookkeeping for delay lines */
    float delsamp1 = 0, delsamp2 = 0;
    float m1, m2;
    float delay_time;
    int out_frames;
    
    int channels = event->out_channels;
    float *params = x->params;
    float srate = x->sr;
    float max_delay = x->maxdelay;
    float *delayline1 = x->delayline1;
    float *delayline2 = x->delayline2;
    
    int in_frames = event->sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;

    // 1. Parameter Fetch
    ++(*pcount);
    delay1 = params[(*pcount)++];
    delay2 = params[(*pcount)++];
    feedback = params[(*pcount)++];
    overhang = params[(*pcount)++];
    
    if (srate <= 0 || in_frames <= 0) return;

    // 2. Set up Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *buffer_end = event->workbuffer + buflen;

    // 3. Bound Check and Output Sizing
    if (overhang < COMBFADE) overhang = COMBFADE;
    out_frames = in_frames + (int)(overhang * srate);

    // 4. Sanitize Delay Times
    if (delay1 < 0.0f) delay1 = 0.0f;
    if (delay2 < 0.0f) delay2 = 0.0f;
    if (delay1 > max_delay * 0.95f) delay1 = max_delay * 0.95f;
    if (delay2 > max_delay * 0.95f) delay2 = max_delay * 0.95f;

    // 5. Initialize Delay Lines
    delset2(delayline1, dv1, max_delay, srate);
    if (channels == 2) {
        delset2(delayline2, dv2, max_delay, srate);
    }
    
    // 6. Loop 1: Main Signal (Input + Feedback)
    for (i = 0; i < in_frames; i++) {
        if (outbuf + channels > buffer_end) {
            out_frames = i;
            goto update_state;
        }

        // Calculate sliding delay time
        m2 = (float)i / (float)out_frames;
        m1 = 1.0f - m2;
        delay_time = (delay1 * m1) + (delay2 * m2);

        if (channels == 1) {
            float insamp = *inbuf++;
            delput2(insamp + delsamp1 * feedback, delayline1, dv1);
            delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
            *outbuf++ = insamp + delsamp1;
        } else {
            float insampL = *inbuf++;
            float insampR = *inbuf++;
            
            delput2(insampL + delsamp1 * feedback, delayline1, dv1);
            delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
            *outbuf++ = insampL + delsamp1;

            delput2(insampR + delsamp2 * feedback, delayline2, dv2);
            delsamp2 = dliget2(delayline2, delay_time, dv2, srate);
            *outbuf++ = insampR + delsamp2;
        }
    }
    
    // 7. Loop 2: The Tail (Overhang)
    for (i = in_frames; i < out_frames; i++) {
        if (outbuf + channels > buffer_end) {
            out_frames = i;
            break;
        }

        m2 = (float)i / (float)out_frames;
        m1 = 1.0f - m2;
        delay_time = (delay1 * m1) + (delay2 * m2);

        if (channels == 1) {
            delput2(delsamp1 * feedback, delayline1, dv1);
            delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
            *outbuf++ = delsamp1;
        } else {
            delput2(delsamp1 * feedback, delayline1, dv1);
            delsamp1 = dliget2(delayline1, delay_time, dv1, srate);
            *outbuf++ = delsamp1;

            delput2(delsamp2 * feedback, delayline2, dv2);
            delsamp2 = dliget2(delayline2, delay_time, dv2, srate);
            *outbuf++ = delsamp2;
        }
    }

    // 8. Final Tail Fadeout
    int fade_frames = (int)(COMBFADE * srate);
    if (fade_frames > out_frames) fade_frames = out_frames;
    if (fade_frames > 0) {
        float *fade_ptr = outbuf - (fade_frames * channels);
        for (i = 0; i < fade_frames; i++) {
            float fadegain = 1.0f - ((float)i / (float)fade_frames);
            *fade_ptr++ *= fadegain;
            if (channels == 2) *fade_ptr++ *= fadegain;
        }
    }

update_state:
    // 9. Update Event State
    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void reverb1(t_bashfest *x, int slot, int *pcount)
{
    //  int i;
    float revtime, overhang;
    int channel_to_compute;
    float drygain;
    int out_frames;
    //  int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    int buf_frames = x->buf_frames;
    float *params = x->params;
    float srate = x->sr;
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;
    
    ++(*pcount);
    revtime = params[(*pcount)++];
    
   
    if( revtime >= 1. ){
        error("reverb1 does not like feedback values over 1.");
        revtime = .99 ;
    }
    overhang = params[(*pcount)++];
    drygain = params[(*pcount)++];
    
   // post("skipping reverb at slot %d\n",slot);
    return;
    out_frames = in_frames + srate * overhang;

    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;
    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    for( channel_to_compute = 0; channel_to_compute < channels; channel_to_compute++) {
        reverb1me( inbuf, outbuf, in_frames, out_frames, channels, channel_to_compute, revtime, drygain, x);
    }
    x->events[slot].sample_frames = out_frames;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
    
}
*/

void reverb1(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params;
    float revtime, overhang, drygain;
    
    ++(*pcount);
    revtime = params[(*pcount)++];
    overhang = params[(*pcount)++];
    drygain = params[(*pcount)++];

    if (x->sr <= 0 || event->sample_frames <= 0) return;

    if (revtime >= 0.99f) revtime = 0.98f;
    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    int channels = event->out_channels;

    int out_frames = event->sample_frames + (int)(x->sr * overhang);
    if (out_frames * channels > x->halfbuffer) out_frames = x->halfbuffer / channels;

    memset(outbuf, 0, out_frames * channels * sizeof(float));
    for (int j = 0; j < channels; j++) { // Fixed: use 'j'
        reverb1me(inbuf, outbuf, event->sample_frames, out_frames, channels, j, revtime, drygain, x);
    }

    event->sample_frames = out_frames;
    event->out_start = in_start; event->in_start = out_start;
}

/*
void ellipseme(t_bashfest *x, int slot, int *pcount)
{
    int i,j;
    int nsects;
    float xnorm;
    int filtercode ;
    float *fltdata;
    
    //  int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    //  int buf_frames = x->buf_frames;
    float *params = x->params;
    //  float srate = x->sr;
    float **flts = x->ellipse_data;
    LSTRUCT *eel = x->eel;
    
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;
    
    ++(*pcount);
    filtercode = params[(*pcount)++];
    
    if( filtercode >= ELLIPSE_FILTER_COUNT ){
        error("there is no %d ellipse data",filtercode);
        return;
    };
    fltdata = flts[ filtercode ];
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;
    //out_frames = in_frames;
    if (in_frames * channels > max_safe_samples) {
        in_frames = max_safe_samples / channels;
    }
   //  in_frames = out_frames;
    
    for( j = 0; j < channels; j++) {
        ellipset(fltdata,eel,&nsects,&xnorm);
        for( i = j; i < in_frames * channels ; i += channels ){
            outbuf[i] = ellipse(inbuf[i], eel, nsects,xnorm);
        }
    }
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
    
}
*/

void ellipseme(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    int i, j;
    int nsects;
    float xnorm;
    int filtercode;
    float *fltdata;
    
    int channels = event->out_channels;
    float *params = x->params;
    float **flts = x->ellipse_data;
    LSTRUCT *eel = x->eel; // Internal filter state structure
    
    int in_frames = event->sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    
    // 1. Parameter Fetch
    ++(*pcount);
    filtercode = (int)params[(*pcount)++];
    
    if (in_frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen ;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *buffer_end = event->workbuffer + buflen;

    // 3. Filter Code Validation
    if (filtercode < 0 || filtercode >= ELLIPSE_FILTER_COUNT) {
        error("bashfest~: invalid ellipse filter code %d", filtercode);
        // Bypass: Copy input to output so the chain doesn't break
        int copy_frames = in_frames;
        if (outbuf + (copy_frames * channels) > buffer_end) {
            copy_frames = (int)((buffer_end - outbuf) / channels);
        }
        memcpy(outbuf, inbuf, copy_frames * channels * sizeof(float));
        in_frames = copy_frames;
        goto update_state;
    }

    // 4. Boundary Safety
    if (outbuf + (in_frames * channels) > buffer_end) {
        in_frames = (int)((buffer_end - outbuf) / channels);
    }

    // 5. Processing
    fltdata = flts[filtercode];
    
    for (j = 0; j < channels; j++) {
        // ellipset initializes the eel structure and clears internal history
        // We call it once per channel pass.
        ellipset(fltdata, eel, &nsects, &xnorm);
        
        for (i = j; i < in_frames * channels; i += channels) {
            outbuf[i] = ellipse(inbuf[i], eel, nsects, xnorm);
        }
    }

update_state:
    // 6. Update Event State
    event->sample_frames = in_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void feed1me(t_bashfest *x, int slot, int *pcount)
{
    //  int i;
    float mindelay, maxdelay, speed1, speed2;
    float phz1 = .13, phz2 = .251;
    float dur;
    float minfeedback = .1, maxfeedback = .7;
    float desired_dur;
    float overhang;
    // main variables
    
    //  int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    int buf_frames = x->buf_frames;
    float *params = x->params;
    float srate = x->sr;
    int out_frames;
    // process specfic
    int flen = x->feedfunclen ;
    float *func1 = x->feedfunc1;
    float *func2 = x->feedfunc2;
    float *func3 = x->feedfunc3;
    float *func4 = x->feedfunc4;
    float my_max_delay = x->max_mini_delay;
    float *sinewave = x->sinewave;
    int sinelen = x->sinelen ;
    
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;

    ++(*pcount);
    mindelay = params[ (*pcount)++ ];
    maxdelay = params[ (*pcount)++ ];
    speed1 = params[ (*pcount)++ ];
    speed2 = params[ (*pcount)++ ];
    overhang = params[ (*pcount)++ ];
    
    return;
    
    if( maxdelay > my_max_delay ){
        error("feed1: too high max delay, adjusted");
        maxdelay = my_max_delay ;
    }
    dur = in_frames / srate ;
    desired_dur = dur + overhang;
    out_frames = srate * desired_dur ;

    
    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;
    
    funcgen1( func1, flen, desired_dur, mindelay, maxdelay,
             speed1, speed2, 1.0, 1.0, &phz1, &phz2, sinewave, sinelen);
    
    phz1 /= (float) flen; phz2 /= (float) flen;
    
    
    funcgen1( func2, flen, desired_dur, mindelay*.5, maxdelay*2.0,
             speed1*1.25, speed2*.75, 1.0, 1.0, &phz1, &phz2, sinewave, sinelen);
    
    phz1 /= (float) flen; phz2 /= (float) flen;
    
    
    funcgen1( func3, flen, desired_dur, minfeedback, maxfeedback,
             speed1*.35, speed2*1.25, 1.0, 1.0, &phz1, &phz2, sinewave, sinelen);
    
    phz1 /= (float) flen; phz2 /= (float) flen;
    
    funcgen1( func4,flen, desired_dur, minfeedback, maxfeedback,
             speed1*.55, speed2*2.25, 1.0, 1.0, &phz1, &phz2, sinewave, sinelen);
    
    feed1( inbuf, outbuf, in_frames, out_frames, channels, func1, func2, func3, func4, flen, dur, my_max_delay, x);
    
    x->events[slot].sample_frames = out_frames;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
    
}
*/

void feed1me(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float mindelay, maxdelay, speed1, speed2;
    float phz1 = 0.13f, phz2 = 0.251f;
    float dur, desired_dur, overhang;
    
    int channels = event->out_channels;
    float *params = x->params;
    float srate = x->sr;
    int out_frames;

    int flen = x->feedfunclen ;
    float *func1 = x->feedfunc1;
    float *func2 = x->feedfunc2;
    float *func3 = x->feedfunc3;
    float *func4 = x->feedfunc4;
    float my_max_delay = x->max_mini_delay;
    float *sinewave = x->sinewave;
    int sinelen = x->sinelen ;
    
    int in_frames = event->sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;

    // 1. Parameter Fetch
    ++(*pcount);
    mindelay = params[ (*pcount)++ ];
    maxdelay = params[ (*pcount)++ ];
    speed1 = params[ (*pcount)++ ];
    speed2 = params[ (*pcount)++ ];
    overhang = params[ (*pcount)++ ];
    
    if (srate <= 0 || in_frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen ;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *buffer_end = event->workbuffer + buflen;

    // 3. Output Sizing and Memory Guard
    if (maxdelay > my_max_delay) maxdelay = my_max_delay * 0.95f;
    if (mindelay < 0.0f) mindelay = 0.0f;
    
    dur = (float)in_frames / srate ;
    desired_dur = dur + overhang;
    out_frames = (int)(srate * desired_dur);

    if (outbuf + (out_frames * channels) > buffer_end) {
        out_frames = (int)((buffer_end - outbuf) / channels);
        // Recalculate duration based on actual safe frame count
        desired_dur = (float)out_frames / srate;
    }

    // 4. Generate LFO Modulation Tables
    // We pass &phz so the phase evolves across calls
    funcgen1(func1, flen, desired_dur, mindelay, maxdelay,
             speed1, speed2, 1.0f, 1.0f, &phz1, &phz2, sinewave, sinelen);
    
    phz1 = 0.2f; phz2 = 0.5f; // reset for next func
    funcgen1(func2, flen, desired_dur, mindelay*0.5f, maxdelay*2.0f,
             speed1*1.25f, speed2*0.75f, 1.0f, 1.0f, &phz1, &phz2, sinewave, sinelen);
    
    phz1 = 0.35f; phz2 = 0.12f;
    funcgen1(func3, flen, desired_dur, 0.1f, 0.7f, // min/max feedback
             speed1*0.35f, speed2*1.25f, 1.0f, 1.0f, &phz1, &phz2, sinewave, sinelen);
    
    phz1 = 0.6f; phz2 = 0.9f;
    funcgen1(func4, flen, desired_dur, 0.1f, 0.7f,
             speed1*0.55f, speed2*2.25f, 1.0f, 1.0f, &phz1, &phz2, sinewave, sinelen);
    
    // 5. Processing
    feed1(inbuf, outbuf, in_frames, out_frames, channels, func1, func2, func3, func4, flen, desired_dur, my_max_delay, x);
    
    // 6. Update Event State
    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void flam1(t_bashfest *x, int slot, int *pcount)
{
    //  int channel_to_compute;
    int attacks;
    float gain2;
    float gainatten;
    float delay;
    float gain = 1.0;
    int i, j, k, delaysamps, delayoffset = 0;
    //  float inputmax, outputmax, rescale;
    int delay_frames;

    float *inbuf;
    float *outbuf;
    //  int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    int buflen = x->buf_samps;
    int buf_frames = x->buf_frames;
    float *params = x->params;
    float srate = x->sr;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int out_frames;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;
    
    ++(*pcount);
    attacks = params[(*pcount)++];
    gain2 = params[(*pcount)++];
    gainatten = params[(*pcount)++];
    delay = params[(*pcount)++];
    
    if( attacks <= 1 ){
        error("flam1: too few attacks: %d",attacks);
        return;
    }
    return;
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;
    
    delay_frames = srate * delay + 0.5;
    delaysamps = channels * delay_frames;
    out_frames = in_frames + (srate * delay * (float) (attacks - 1));

    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    for( i = 0; i < out_frames * channels; i++ ){
        outbuf[i] = 0.0 ;
    }
    
    for(i = 0; i < attacks; i++ ){
        if(in_frames + delay_frames * i >= out_frames){
            // error("breaking at attack %d",i);
            break;
        }
        for(j = 0; j < in_frames * channels; j += channels ){
            for( k = 0; k < channels; k++ ){
                outbuf[j + k + delayoffset] += *(inbuf +j + k) * gain;
            }
        }
        delayoffset += delaysamps;
        if( i == 0 ){
            gain = gain2;
        } else {
            gain *= gainatten;
        }
    }
    
    x->events[slot].sample_frames = out_frames;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
}
*/

void flam1(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params; // Fix: declared params
    int attacks;
    float gain2, gainatten, delay, gain = 1.0f;
    int i, j, k;

    ++(*pcount);
    attacks = (int)params[(*pcount)++];
    gain2 = params[(*pcount)++];
    gainatten = params[(*pcount)++];
    delay = params[(*pcount)++];
    
    if (x->sr <= 0 || event->sample_frames <= 0) return;
    int in_frames = event->sample_frames;
    int channels = event->out_channels;

    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;

    int delay_frames = (int)(x->sr * delay);
    int out_frames = in_frames + (delay_frames * (attacks - 1));
    
    // Safety: Wall check
    if (out_frames * channels > x->halfbuffer) {
        out_frames = x->halfbuffer / channels;
    }

    memset(outbuf, 0, out_frames * channels * sizeof(float));
    
    for (i = 0; i < attacks; i++) {
        int attack_off = i * delay_frames * channels;
        if (attack_off >= out_frames * channels) break;
        
        int frames_to_add = in_frames;
        if (attack_off + (frames_to_add * channels) > out_frames * channels)
            frames_to_add = (out_frames * channels - attack_off) / channels;

        for (j = 0; j < frames_to_add; j++) {
            for (k = 0; k < channels; k++)
                outbuf[attack_off + j * channels + k] += inbuf[j * channels + k] * gain;
        }
        gain = (i == 0) ? gain2 : gain * gainatten;
        if (gain < 0.0001f) break;
    }

    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void flam2(t_bashfest *x, int slot, int *pcount)
{
    //  int channel_to_compute;
    int attacks;
    float gain2;
    float gainatten;
    float delay1,delay2;
    float gain = 1.0;
    int i, j, k, delaysamps, delayoffset = 0;
    int f_endpoint;
    //  float inputmax, outputmax, rescale;
    int delay_frames;
    float now = 0.0;
    int index;
    float inval;
    float curdelay;

    float *inbuf;
    float *outbuf;
    int out_frames;
    //  int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    int buflen = x->buf_samps;
    int buf_frames = x->buf_frames;
    float *params = x->params;
    float srate = x->sr;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int halfbuffer = x->halfbuffer;
    float *flamfunc1 = x->flamfunc1;
    int flamfunclen = x->flamfunc1len;
    int max_safe_samples;
    
    ++(*pcount);
    attacks = params[(*pcount)++];
    gain2 = params[(*pcount)++];
    gainatten = params[(*pcount)++];
    delay1 = params[(*pcount)++];
    delay2 = params[(*pcount)++];
    
    if( attacks <= 1 ){
        error("flam2: recieved too few attacks: %d",attacks);
        return;
    }
    
    return;
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;

    for( i = 0; i < attacks - 1; i++ ){
        index = ((float)i/(float)attacks) * (float)flamfunclen ;
        inval = flamfunc1[index];
        curdelay = mapp(inval, 0., 1., delay2, delay1);
        now += curdelay;
    }
    out_frames = in_frames + (srate * now);

    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    for( i = 0; i < out_frames * channels; i++ ){
        outbuf[i] = 0.0 ;
    }
    
    f_endpoint = in_frames;
    // first time delay_offset is zero
    for( i = 0; i < attacks; i++ ){
        index = ((float)i/(float)attacks) * (float)flamfunclen ;
        inval = flamfunc1[index];
        curdelay = mapp(inval, 0., 1., delay2, delay1);
        
        delay_frames = srate * curdelay + 0.5;
        delaysamps = delay_frames * channels;
        if(f_endpoint >= out_frames){
            // error("flam2: breaking at attack %d",i);
            break;
        }
        for(j = 0; j < in_frames * channels; j += channels ){
            for( k = 0; k < channels; k++ ){
                outbuf[j + k + delayoffset] += *(inbuf + j + k) * gain;
            }
        }
        delayoffset += delaysamps;
        f_endpoint = in_frames + delayoffset/channels;
        if( i == 0 ){
            gain = gain2;
        } else {
            gain *= gainatten;
        }
    }
    
    x->events[slot].sample_frames = out_frames;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
}
*/

void flam2(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params;
    int attacks;
    float gain2, gainatten, delay1, delay2, gain = 1.0f;
    int i, j, k;

    ++(*pcount);
    attacks = (int)params[(*pcount)++];
    gain2 = params[(*pcount)++];
    gainatten = params[(*pcount)++];
    delay1 = params[(*pcount)++];
    delay2 = params[(*pcount)++];
    
    if (x->sr <= 0 || event->sample_frames <= 0) return;
    int in_frames = event->sample_frames;
    int channels = event->out_channels;

    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;

    // Calculate duration first
    float total_delay = 0;
    for (i = 0; i < attacks - 1; i++) {
        int idx = (int)(((float)i / (float)attacks) * (float)x->flamfunc1len);
        if (idx >= x->flamfunc1len) idx = x->flamfunc1len - 1;
        total_delay += mapp(x->flamfunc1[idx], 0.0f, 1.0f, delay2, delay1);
    }
    
    int out_frames = in_frames + (int)(total_delay * x->sr);
    if (out_frames * channels > x->halfbuffer) out_frames = x->halfbuffer / channels;

    memset(outbuf, 0, out_frames * channels * sizeof(float));
    
    float time_acc = 0;
    for (i = 0; i < attacks; i++) {
        int attack_off = (int)(time_acc * x->sr) * channels;
        if (attack_off >= out_frames * channels) break;
        
        int add = in_frames;
        if (attack_off + (add * channels) > out_frames * channels)
            add = (out_frames * channels - attack_off) / channels;

        for (j = 0; j < add; j++) {
            for (k = 0; k < channels; k++)
                outbuf[attack_off + j * channels + k] += inbuf[j * channels + k] * gain;
        }

        // Calculate time for next attack
        int idx = (int)(((float)i / (float)attacks) * (float)x->flamfunc1len);
        if (idx >= x->flamfunc1len) idx = x->flamfunc1len - 1;
        time_acc += mapp(x->flamfunc1[idx], 0.0f, 1.0f, delay2, delay1);

        gain = (i == 0) ? gain2 : gain * gainatten;
        if (gain < 0.0001f) break;
    }

    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void expflam(t_bashfest *x, int slot, int *pcount)
{
    int attacks;
    float gain2;
    float gainatten;
    float delay1,delay2;
    float gain = 1.0;
    int i, j, k, delaysamps, delayoffset = 0, f_endpoint;
    //  float inputmax, outputmax, rescale;
    int delay_frames;
    float now = 0.0;
    // int index;
    // float inval;
    float curdelay;
    float slope;

    float *inbuf;
    float *outbuf;
    int out_frames;
    //  int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    int buflen = x->buf_samps;
    int buf_frames = x->buf_frames;
    float *params = x->params;
    float srate = x->sr;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int halfbuffer = x->halfbuffer;
    float *expfunc = x->feedfunc1;
    int max_safe_samples;
    
    ++(*pcount);
    attacks = params[(*pcount)++];
    gain2 = params[(*pcount)++];
    gainatten = params[(*pcount)++];
    delay1 = params[(*pcount)++];
    delay2 = params[(*pcount)++];
    slope = params[(*pcount)++];
    
    if( attacks <= 1 ){
        error("expflam: recieved too few attacks: %d",attacks);
        return;
    }
    
    return;
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;

    setExpFlamFunc(expfunc, attacks, delay1, delay2, slope);
    
    for( i = 0; i < attacks - 1; i++ ){
        now += expfunc[i];
    }
    
    out_frames = in_frames + (srate * now);

    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    for( i = 0; i < out_frames * channels; i++ ){
        outbuf[i] = 0.0 ;
    }
    
    f_endpoint = in_frames;
    
    for( i = 0; i < attacks; i++ ){
        curdelay = expfunc[i];
        delay_frames = srate * curdelay + 0.5;
        delaysamps = delay_frames * channels;
        if(f_endpoint >= out_frames){
            // error("expflam: breaking at attack %d",i);
            break;
        }
        for(j = 0; j < in_frames * channels; j += channels ){
            for( k = 0; k < channels; k++ ){
                outbuf[j + k + delayoffset] += *(inbuf + j + k) * gain;
            }
        }
        delayoffset += delaysamps;
        f_endpoint = in_frames + delayoffset/channels;
        if( i == 0 ){
            gain = gain2;
        } else {
            gain *= gainatten;
        }
    }
    
    x->events[slot].sample_frames = out_frames;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
}
*/

void expflam(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params;
    int attacks;
    float gain2, gainatten, delay1, delay2, slope, gain = 1.0f;
    int i, j, k;

    ++(*pcount);
    attacks = (int)params[(*pcount)++];
    gain2 = params[(*pcount)++];
    gainatten = params[(*pcount)++];
    delay1 = params[(*pcount)++];
    delay2 = params[(*pcount)++];
    slope = params[(*pcount)++];
    
    if (x->sr <= 0 || event->sample_frames <= 0) return;
    int in_frames = event->sample_frames;
    int channels = event->out_channels;

    if (attacks <= 1) attacks = 2;
    if (attacks > x->feedfunclen) attacks = x->feedfunclen;
    setExpFlamFunc(x->feedfunc1, attacks, delay1, delay2, slope);

    float total_t = 0;
    for (i = 0; i < attacks - 1; i++) total_t += x->feedfunc1[i];
    
    int out_frames = in_frames + (int)(x->sr * total_t);
    if (out_frames * channels > x->halfbuffer) out_frames = x->halfbuffer / channels;

    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    
    memset(outbuf, 0, out_frames * channels * sizeof(float));

    float time_acc = 0;
    for (i = 0; i < attacks; i++) {
        int off = (int)(time_acc * x->sr) * channels;
        if (off >= out_frames * channels) break;
        
        int add = in_frames;
        if (off + add * channels > out_frames * channels)
            add = (out_frames * channels - off) / channels;

        for (j = 0; j < add; j++) {
            for (k = 0; k < channels; k++)
                outbuf[off + j * channels + k] += inbuf[j * channels + k] * gain;
        }
        
        if (i < attacks - 1) time_acc += x->feedfunc1[i];
        gain = (i == 0) ? gain2 : gain * gainatten;
        if (gain < 0.0001f) break;
    }

    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void comb4(t_bashfest *x, int slot, int *pcount)
{
    float overhang, revtime ;
    int i, j, k;
    int fadeFrames;
    float fadegain;
    int fadestart;
    float input_sample;
    float rez;
 
    
    int out_frames;
    //  int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    int buf_frames = x->buf_frames;
    float *params = x->params;
    float srate = x->sr;
    

    CMIXCOMB *combies = x->combies;
    float maxloop = x->max_comb_lpt;
    
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;

    ++(*pcount);
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;

    for( j = 0; j < 4; j++ ){
        rez = params[(*pcount)++] ;
        if( rez == 0.0){
            error("comb4: 0 resonance frequency not allowed");
            return;
        }
        if( 1./rez > maxloop ){
            error("comb4: %f is too long loop",1./rez);
            return;
        }
        combies[j].lpt = 1. / rez ;
    }
    
    revtime = params[(*pcount)++];
    overhang = params[(*pcount)++];
    if( overhang < COMBFADE )
        overhang = COMBFADE;
   // post("skipping comb4 at slot %d\n",slot);
    return;
    out_frames = in_frames + overhang * srate;

    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    for( j = 0; j < 4; j++ ){
        mycombset( combies[j].lpt, revtime, 0, combies[j].arr, srate);
    }
    
    inbuf = x->events[slot].workbuffer + in_start;
    
    for( j = 0; j < channels; j++ ){
        for( i = 0; i < in_frames * channels; i += channels ){
            input_sample = *(inbuf + i + j) ; // we can move inside loop
            *(outbuf + i + j ) = 0.0; // comment out to leave original sound into it
            for( k = 0; k < 4; k++ ){
                *(outbuf + i + j) += mycomb(input_sample, combies[k].arr);
            }
        }
    }
    for( i = in_frames * channels; i < out_frames * channels; i += channels ){
        for( j = 0; j < channels; j++ ){
            *(outbuf + i + j) = 0.0;
            for( k = 0; k < 4; k++ ){
                *(outbuf +i+j) += mycomb(0.0,combies[k].arr);
            }
        }
    }
    fadeFrames = COMBFADE * srate; // ok - this is just the fadeout
    fadestart = (out_frames - fadeFrames) * channels ;
    for( i = 0; i < fadeFrames * channels; i += channels ){
        fadegain = 1.0 - (float) i / (float) (fadeFrames * channels)  ;
        *(outbuf + fadestart + i) *= fadegain;
        if( channels == 2 ){
            *(outbuf + fadestart + i + 1) *= fadegain;
        }
    }
    killdc(outbuf, out_frames, channels, x);
    x->events[slot].sample_frames = out_frames;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
    
}
*/

void comb4(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float overhang, revtime;
    int i, j, k;
    float input_sample;
    float rez;
    
    int channels = event->out_channels;
    float *params = x->params;
    float srate = x->sr;
    
    CMIXCOMB *combies = x->combies;
    float maxloop = x->max_comb_lpt;
    
    int in_frames = event->sample_frames;
    int out_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;

    // 1. Parameter Fetch
    ++(*pcount);
    
    // Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen ;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *buffer_end = event->workbuffer + buflen;

    if (srate <= 0 || in_frames <= 0) return;

    // Fetch the 4 resonance frequencies
    for( j = 0; j < 4; j++ ){
        rez = params[(*pcount)++];
        if( rez < 10.0f ) rez = 10.0f; // Minimum 10Hz resonance
        
        // Ensure loop time (1/rez) fits in allocated comb memory
        if( 1.0f / rez > maxloop ){
            rez = 1.0f / maxloop;
        }
        combies[j].lpt = 1.0f / rez ;
    }
    
    revtime = params[(*pcount)++];
    overhang = params[(*pcount)++];
    if( overhang < COMBFADE ) overhang = COMBFADE;

    // 2. Calculate Output Length and Boundary Guard
    out_frames = in_frames + (int)(overhang * srate);
    if (outbuf + (out_frames * channels) > buffer_end) {
        out_frames = (int)((buffer_end - outbuf) / channels);
    }

    // 3. Main Processing
    // Because we only have 4 parallel combs but potentially 2 channels,
    // we process channels one at a time.
    for (j = 0; j < channels; j++) {
        // Initialize the 4 combs for this specific channel pass
        for (k = 0; k < 4; k++) {
            mycombset(combies[k].lpt, revtime, 0, combies[k].arr, srate);
        }

        // Process Input frames
        for (i = 0; i < in_frames; i++) {
            input_sample = inbuf[i * channels + j];
            float summed_combs = 0.0f;
            for (k = 0; k < 4; k++) {
                summed_combs += mycomb(input_sample, combies[k].arr);
            }
            outbuf[i * channels + j] = summed_combs;
        }

        // Process Tail frames
        for (i = in_frames; i < out_frames; i++) {
            float summed_combs = 0.0f;
            for (k = 0; k < 4; k++) {
                summed_combs += mycomb(0.0f, combies[k].arr);
            }
            outbuf[i * channels + j] = summed_combs;
        }
    }

    // 4. Fadeout (Prevents clicks at the end of the tail)
    int fade_frames = (int)(COMBFADE * srate);
    if (fade_frames > out_frames) fade_frames = out_frames;
    
    if (fade_frames > 0) {
        float *fade_ptr = outbuf + ((out_frames - fade_frames) * channels);
        for (i = 0; i < fade_frames; i++) {
            float fadegain = 1.0f - ((float)i / (float)fade_frames);
            for (j = 0; j < channels; j++) {
                *fade_ptr++ *= fadegain;
            }
        }
    }

    // 5. DC Blocker
    // Recursive filters like combs often build up a DC offset
    killdc(outbuf, out_frames, channels, x);

    // 6. Update Event State
    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void compdist(t_bashfest *x, int slot, int *pcount)
{
    float cutoff, maxmult;
    int lookupflag;
    int channel_to_compute;
    float maxamp;

    
    int channels = x->events[slot].out_channels;
    float *params = x->params;

    int range = x->tf_len;
    float *table = x->transfer_function;
    
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int out_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;

    
    ++(*pcount);
    cutoff = params[(*pcount)++];
    maxmult = params[(*pcount)++];
    lookupflag = params[(*pcount)++];
    
    // post("skipping compdist\n");
    return;
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;
    out_frames = in_frames;
    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }
    in_frames = out_frames;
    
    maxamp = getmaxamp(inbuf, in_frames*channels) ;
    
    if(lookupflag){
        set_distortion_table(table, cutoff, maxmult, range);
    }
    
    for( channel_to_compute = 0; channel_to_compute < channels; channel_to_compute++) {
        do_compdist(inbuf, outbuf, in_frames, channels, channel_to_compute,
                    cutoff, maxmult, lookupflag, table, range, maxamp);
    }
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen;
    
}
*/

void compdist(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float cutoff, maxmult;
    int lookupflag;
    int channel_to_compute;
    float maxamp;
    
    int channels = event->out_channels;
    float *params = x->params;
    int range = x->tf_len;
    float *table = x->transfer_function;
    
    int in_frames = event->sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;

    // 1. Parameter Fetch
    ++(*pcount);
    cutoff = params[(*pcount)++];
    maxmult = params[(*pcount)++];
    lookupflag = (int)params[(*pcount)++];
    
    if (in_frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen ;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *buffer_end = event->workbuffer + buflen;

    // 3. Boundary Safety
    if (outbuf + (in_frames * channels) > buffer_end) {
        in_frames = (int)((buffer_end - outbuf) / channels);
    }

    // 4. Peak Detection
    // We must find the maximum amplitude of the block to normalize the distortion
    maxamp = getmaxamp(inbuf, in_frames * channels);
    
    // 5. Table Initialization
    // If the user requested the lookup algorithm, we fill the table with the curve
    if(lookupflag && table != NULL){
        set_distortion_table(table, cutoff, maxmult, range);
    }
    
    // 6. Processing
    if (maxamp > 0.000001f) {
        for (channel_to_compute = 0; channel_to_compute < channels; channel_to_compute++) {
            // do_compdist is a helper that processes one channel pass
            do_compdist(inbuf, outbuf, in_frames, channels, channel_to_compute,
                        cutoff, maxmult, lookupflag, table, range, maxamp);
        }
    } else {
        // If the buffer is silent, just copy the silence to the new ping-pong position
        memcpy(outbuf, inbuf, in_frames * channels * sizeof(float));
    }

    // 7. Update Event State
    event->sample_frames = in_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void ringfeed(t_bashfest *x, int slot, int *pcount)
{
    float overhang; //, revtime, delay[4] ;
    int i, j; //, k;
    int fade_frames;
    float fadegain;
    int fadestart;
    float input_sample;
    float rez ;

    int out_frames;
    //  int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    int buf_frames = x->buf_frames;
    float *params = x->params;
    float srate = x->sr;

    float *sinewave = x->sinewave;
    int sinelen = x->sinelen ;
    CMIXCOMB *combies = x->combies;
    CMIXRESON *resies = x->resies;
    CMIXOSC oscar = x->oscar;
    float maxloop = x->max_comb_lpt;
    
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;

    ++(*pcount);
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;

    oscar.func = sinewave;
    oscar.len = sinelen;
    oscar.si = params[(*pcount)++] * ((float)oscar.len / srate);
    oscar.phs = 0;
    rez = params[(*pcount)++] ;
    if( rez > 0 )
        combies[0].lpt = 1. / rez ;
    else error("zero comb resonance is bad luck");
    if(combies[0].lpt > maxloop)
        error("ringfeed does not appreciate looptimes as large as %f",combies[0].lpt);
    
    combies[0].rvbt = params[(*pcount)++] ;
    if(combies[0].rvbt >= 1.0) {
        error("ringfeed dislikes feedback values >= 1");
        combies[0].rvbt = .99 ;
    }
    resies[0].cf = params[(*pcount)++];
    resies[0].bw  = resies[0].cf * params[(*pcount)++];
    overhang = params[(*pcount)++] ;
    
    inbuf = x->events[slot].workbuffer + in_start;
    // post("diregarding ringfeed at slot %d\n",slot);
    return;
    for( i = 0; i < channels ; i++ ){
        mycombset( combies[0].lpt, combies[0].rvbt, 0, combies[i].arr,srate);
        rsnset2(resies[0].cf, resies[0].bw, RESON_NO_SCL, 0., resies[i].q, srate);
    }
    

    
    if( overhang < COMBFADE )
        overhang = COMBFADE;
    if (in_frames * channels > max_safe_samples) {
        in_frames = max_safe_samples / channels;
    }
    out_frames = in_frames + overhang * srate ;

    if (out_frames * channels > max_safe_samples) {
        out_frames = max_safe_samples / channels;
    }

    for( i = 0; i < in_frames * channels; i += channels ){
        for( j = 0; j < channels; j++ ){
            input_sample = *(inbuf + i + j ) ;
            input_sample *= oscil(1.0, oscar.si, oscar.func, oscar.len, &oscar.phs);
            input_sample += mycomb(input_sample, combies[j].arr);
            *(outbuf +i+j) = reson(input_sample, resies[j].q);
        }
    }
    
    
    for( i = in_frames * channels; i < out_frames * channels; i += channels ){
        for( j = 0; j < channels; j++ ){
            *(outbuf +i+j) = reson(mycomb( 0.0, combies[j].arr), resies[j].q );
        }
    }
    

    
    fade_frames = COMBFADE * srate;
    fadestart = (out_frames - fade_frames) * channels ;
    for( i = 0; i < fade_frames * channels; i += channels ){
        fadegain = 1.0 - (float) i / (float) (fade_frames * channels)  ;
        *(outbuf + fadestart + i) *= fadegain;
        if( channels == 2 ){
            *(outbuf + fadestart + i + 1) *= fadegain;
        }
    }
    x->events[slot].sample_frames = out_frames;
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
    
}
*/

void ringfeed(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float overhang;
    int i, j;
    float input_sample;
    float rez, rvbt, lfo_freq, reson_cf, reson_bw_fac;
    
    int channels = event->out_channels;
    float *params = x->params;
    float srate = x->sr;
    
    float *sinewave = x->sinewave;
    int sinelen = x->sinelen;
    CMIXCOMB *combies = x->combies;
    CMIXRESON *resies = x->resies;
    float maxloop = x->max_comb_lpt;
    
    int in_frames = event->sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;

    // 1. Parameter Fetch
    ++(*pcount);
    lfo_freq     = params[(*pcount)++];
    rez          = params[(*pcount)++];
    rvbt         = params[(*pcount)++];
    reson_cf     = params[(*pcount)++];
    reson_bw_fac = params[(*pcount)++];
    overhang     = params[(*pcount)++];
    
    
    if (srate <= 0 || in_frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *buffer_end = event->workbuffer + buflen;

    // 3. DSP Sanitization
    // Oscillator (Ringmod)
    float lfo_si = lfo_freq * ((float)sinelen / srate);
    float lfo_phs = 0.0f;

    // Comb Filter
    if (rez < 10.0f) rez = 10.0f;
    float lpt = 1.0f / rez;
    if (lpt > maxloop) lpt = maxloop;
    if (rvbt >= 1.0f) rvbt = 0.99f;
    if (rvbt < 0.0f) rvbt = 0.0f;

    // Resonant Filter
    if (reson_cf < 20.0f) reson_cf = 20.0f;
    if (reson_cf > srate * 0.45f) reson_cf = srate * 0.45f;
    float reson_bw = reson_cf * reson_bw_fac;
    if (reson_bw < 1.0f) reson_bw = 1.0f;

    // 4. Initialization
    for (i = 0; i < channels; i++) {
        mycombset(lpt, rvbt, 0, combies[i].arr, srate);
        rsnset2(reson_cf, reson_bw, 2.0f, 0, resies[i].q, srate);
    }

    // 5. Output Sizing and Guard
    if (overhang < COMBFADE) overhang = COMBFADE;
    int out_frames = in_frames + (int)(overhang * srate);
    if (outbuf + (out_frames * channels) > buffer_end) {
        out_frames = (int)((buffer_end - outbuf) / channels);
    }

    // 6. Loop 1: Main Signal (Ringmod -> Comb -> Reson)
    for (i = 0; i < in_frames; i++) {
        float mod = oscil(1.0f, lfo_si, sinewave, sinelen, &lfo_phs);
        
        for (j = 0; j < channels; j++) {
            input_sample = inbuf[i * channels + j];
            // Stage 1: Ring Mod
            float sig = input_sample * mod;
            // Stage 2: Comb
            sig = sig + mycomb(sig, combies[j].arr);
            // Stage 3: Reson
            outbuf[i * channels + j] = reson(sig, resies[j].q);
        }
    }

    // 7. Loop 2: The Tail (Comb -> Reson only)
    for (i = in_frames; i < out_frames; i++) {
        for (j = 0; j < channels; j++) {
            // Stage 2: Comb (feeding zeros)
            float sig = mycomb(0.0f, combies[j].arr);
            // Stage 3: Reson
            outbuf[i * channels + j] = reson(sig, resies[j].q);
        }
    }

    // 8. Fadeout the end of the tail
    int fade_frames = (int)(COMBFADE * srate);
    if (fade_frames > out_frames) fade_frames = out_frames;
    if (fade_frames > 0) {
        float *fade_ptr = outbuf + ((out_frames - fade_frames) * channels);
        for (i = 0; i < fade_frames; i++) {
            float fadegain = 1.0f - ((float)i / (float)fade_frames);
            for (j = 0; j < channels; j++) {
                *fade_ptr++ *= fadegain;
            }
        }
    }

    // 9. Update Event State
    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void resonadsr(t_bashfest *x, int slot, int *pcount)
{
    int i;
    float bwfac;
    float q1[5], q2[5];
    float cf, bw;
    float si;
    float notedur;
    float phase = 0.;
    int channels = x->events[slot].out_channels;
    float *params = x->params;
    float srate = x->sr;
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    

    CMIXADSR *a = x->adsr;
    int funclen = a->len;
    float *adsrfunc = a->func;
    int max_safe_samples;

    ++(*pcount);
    a->a = params[(*pcount)++];
    a->d = params[(*pcount)++];
    a->r = params[(*pcount)++];
    a->v1 = params[(*pcount)++];
    a->v2 = params[(*pcount)++];
    a->v3 = params[(*pcount)++];
    a->v4 = params[(*pcount)++];
    bwfac = params[(*pcount)++];
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;
    if (in_frames * channels > max_safe_samples) {
        in_frames = max_safe_samples / channels;
    }
    notedur = (float) in_frames / srate ;
    a->s = notedur - (a->a+a->d+a->r);
    if( a->s <= 0.0 ){
        a->a=a->d=a->s=a->r= notedur/ 4. ;
    }
    buildadsr(a);
    si = ((float) funclen / srate) / notedur ;
    
    phase = 0;
    
    rsnset2(adsrfunc[(int)phase], adsrfunc[(int) phase]*bwfac, 2.0, 0.0, q1, srate);
    if( channels == 2 ){
        rsnset2( adsrfunc[(int)phase], adsrfunc[(int) phase]*bwfac, 2.0, 0.0, q2, srate );
    }
    
    for(i = 0; i < in_frames*channels; i += channels ){
        phase += si;
        if( phase > funclen - 1)
            phase = funclen - 1;
        
        cf = adsrfunc[ (int) phase ];
        bw = bwfac * cf ;
        rsnset2( cf, bw, 2.0, 1.0, q1, srate );
        outbuf[i] = reson(inbuf[i], q1);
        if( channels == 2 ){
            rsnset2( cf, bw, 2.0, 1.0, q2, srate );
            outbuf[i+1] = reson(inbuf[i+1], q2);
        }
    }
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
    
}
*/

void resonadsr(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    int i;
    float bwfac;
    float q1[5], q2[5];
    float cf, bw;
    float si;
    float notedur;
    float phase = 0.0f;
    
    int channels = event->out_channels;
    float *params = x->params;
    float srate = x->sr;
    
    /* ADSR specific */
    CMIXADSR *a = x->adsr;
    int funclen = a->len;
    float *adsrfunc = a->func;

    int in_frames = event->sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    
    // 1. Parameter Fetch
    ++(*pcount);
    a->a  = params[(*pcount)++]; // Attack (sec)
    a->d  = params[(*pcount)++]; // Decay (sec)
    a->r  = params[(*pcount)++]; // Release (sec)
    a->v1 = params[(*pcount)++]; // Start Freq
    a->v2 = params[(*pcount)++]; // Attack Freq
    a->v3 = params[(*pcount)++]; // Decay/Sustain Freq
    a->v4 = params[(*pcount)++]; // Release Freq
    bwfac = params[(*pcount)++]; // Bandwidth factor
    
    if (srate <= 0 || in_frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + halfbuffer) % buflen;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *buffer_end = event->workbuffer + buflen;

    // 3. ADSR Logic & Initialization
    notedur = (float) in_frames / srate ;
    
    // Sustain duration is whatever is left over
    a->s = notedur - (a->a + a->d + a->r);
    
    // Safety: If envelope is longer than the sound, shrink segments to fit
    if( a->s <= 0.0f ){
        float factor = notedur / (a->a + a->d + a->r + 0.001f);
        a->a *= factor;
        a->d *= factor;
        a->r *= factor;
        a->s = 0.0f;
    }
    
    // Build the envelope table (32768 points)
    buildadsr(a);
    
    // Calculate phase increment to scan the envelope over the sound duration
    si = (float) funclen / (float) in_frames;
    
    // 4. Boundary Safety
    int out_frames = in_frames;
    if (outbuf + (out_frames * channels) > buffer_end) {
        out_frames = (int)((buffer_end - outbuf) / channels);
    }

    // 5. Initialize Filters (Clear History)
    cf = adsrfunc[0];
    if (cf < 20.0f) cf = 20.0f;
    bw = cf * bwfac;
    if (bw < 1.0f) bw = 1.0f;

    rsnset2(cf, bw, 2.0f, 0.0f, q1, srate);
    if( channels == 2 ){
        rsnset2(cf, bw, 2.0f, 0.0f, q2, srate);
    }
    
    // 6. Processing Loop
    for(i = 0; i < out_frames; i++ ){
        int fidx = (int)phase;
        if (fidx >= funclen) fidx = funclen - 1;
        
        cf = adsrfunc[fidx];
        
        // Filter Sanitization
        if (cf < 20.0f) cf = 20.0f;
        if (cf > srate * 0.45f) cf = srate * 0.45f;
        bw = cf * bwfac;
        if (bw < 1.0f) bw = 1.0f;

        // Update filter (Keep History)
        rsnset2(cf, bw, 2.0f, 1.0f, q1, srate);
        
        if( channels == 1 ){
            *outbuf++ = reson(*inbuf++, q1);
        } else {
            rsnset2(cf, bw, 2.0f, 1.0f, q2, srate);
            *outbuf++ = reson(*inbuf++, q1);
            *outbuf++ = reson(*inbuf++, q2);
        }
        
        phase += si;
    }

    // 7. Update Event State
    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

/*
void stv(t_bashfest *x, int slot, int *pcount)
{
    int i,j;

    
    //  int out_frames;
    int frames = x->events[slot].sample_frames;
    int channels = x->events[slot].out_channels;
    //  int buf_frames = x->buf_frames;
    float *params = x->params;
    float srate = x->sr;

    float *sinewave = x->sinewave;
    int sinelen = x->sinelen ;
    float *delayline1 = x->delayline1;
    float *delayline2 = x->delayline2;
    float max_delay = x->maxdelay * 0.9; // trying to proctect here
    CMIXOSC osc1, osc2; // put into main object structure
    float mindel, maxdel;
    float fac1, fac2;
    int dv1[2], dv2[2];
    float delay_time;
    float speed1, speed2, depth ;
    float *inbuf, *outbuf;
    int in_start = x->events[slot].in_start;
    int out_start = x->events[slot].out_start;
    //  int in_frames = x->events[slot].sample_frames;
    int buflen = x->buf_samps;
    int halfbuffer = x->halfbuffer;
    int max_safe_samples;

    ++(*pcount);
    speed1 = params[(*pcount)++];
    speed2 = params[(*pcount)++];
    depth = params[(*pcount)++];
    
    // post("skipping stv\n");
    return;
    
    out_start = (in_start + halfbuffer) % buflen ;
    inbuf = x->events[slot].workbuffer + in_start;
    outbuf = x->events[slot].workbuffer + out_start;
    max_safe_samples = buflen - out_start;
    if (frames * channels > max_safe_samples) {
        frames = max_safe_samples / channels;
    }
    mindel = .001;
    maxdel = depth;
    
    if( maxdel > max_delay ){
        maxdel = max_delay;
    }
    
    delset2(delayline1, dv1, max_delay,srate);
    delset2(delayline2, dv2, max_delay,srate);
    
    fac2 = .5 * (maxdel - mindel) ;
    fac1 = mindel + fac2;
    
    osc1.func = sinewave;
    osc1.len = sinelen;
    osc1.si = ((float) sinelen / srate ) * speed1 ;
    osc1.phs = 0;
    osc1.amp = fac2;
    
    osc2.func = sinewave;
    osc2.len = sinelen;
    osc2.si = ((float) sinelen / srate ) * speed2 ;
    osc2.phs = 0;
    osc2.amp = fac2;
    
    if( channels == 1 ){
        // int max_safe_samples = buflen - out_start;
        if (frames * 2 > max_safe_samples) {
            frames = max_safe_samples / 2;
        }
        for(i = 0, j = 0; i < frames; i++, j+=2 ){
            
            delay_time = fac1 +
            oscil(osc1.amp, osc1.si, osc1.func, osc1.len, &osc1.phs);
            delput2( inbuf[i], delayline1, dv1);
            outbuf[j] = dliget2(delayline1, delay_time, dv1,srate);
            
            delay_time = fac1 +
            oscil(osc2.amp, osc2.si, osc2.func, osc2.len, &osc2.phs);
            delput2( inbuf[i], delayline2, dv2);
            outbuf[j + 1] = dliget2(delayline2, delay_time, dv2,srate);
        }
    }
    else if( channels == 2 ){
       // int max_safe_samples = buflen - out_start;
        if (frames * 2 > max_safe_samples) {
            frames = max_safe_samples / 2;
        }
        for(i = 0; i < frames*2; i += 2 ){
            delay_time = fac1 +
            oscil(osc1.amp, osc1.si, osc1.func, osc1.len, &osc1.phs);
            delput2( inbuf[i], delayline1, dv1);
            outbuf[i] = dliget2(delayline1, delay_time, dv1,srate);
            
            delay_time = fac1 +
            oscil(osc2.amp, osc2.si, osc2.func, osc2.len, &osc2.phs);
            delput2( inbuf[i + 1], delayline2, dv2);
            outbuf[i + 1] = dliget2(delayline2, delay_time, dv2,srate);
            
        }
    }
    x->events[slot].out_start = in_start;
    x->events[slot].in_start = (x->events[slot].out_start + halfbuffer) % buflen ;
    x->events[slot].out_channels = 2; // we are now stereo, regardless of what we were before
}
*/

void stv(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params;
    int i;
    int in_frames = event->sample_frames;
    int in_channels = event->out_channels;
    float srate = x->sr;
    
    float *sinewave = x->sinewave;
    int sinelen = x->sinelen;
    float *delayline1 = x->delayline1;
    float *delayline2 = x->delayline2;
    float max_delay = x->maxdelay;
    
    float mindel, maxdel, fac1, fac2;
    int dv1[2], dv2[2];
    float speed1, speed2, depth;
    float phs1 = 0.0f, phs2 = 0.5f; // Offset phases for width
    
    // 1. Parameter Fetch
    ++(*pcount);
    speed1 = params[(*pcount)++];
    speed2 = params[(*pcount)++];
    depth  = params[(*pcount)++];
    
    if (srate <= 0 || in_frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *out_limit = outbuf + x->halfbuffer; // THE WALL

    // 3. Sanitization
    if (depth > max_delay * 0.95f) depth = max_delay * 0.95f;
    if (depth < 0.001f) depth = 0.001f;
    mindel = 0.001f;
    maxdel = depth;
    fac2 = 0.5f * (maxdel - mindel);
    fac1 = mindel + fac2;

    // 4. Initialize Shared Delay Lines
    delset2(delayline1, dv1, max_delay, srate);
    delset2(delayline2, dv2, max_delay, srate);
    
    float si1 = ((float)sinelen / srate) * speed1;
    float si2 = ((float)sinelen / srate) * speed2;
    float cur_phs1 = phs1 * sinelen;
    float cur_phs2 = phs2 * sinelen;

    // 5. Processing Loop
    // We are converting to 2 channels regardless of input
    for (i = 0; i < in_frames; i++) {
        
        // WALL CHECK: We are writing 2 samples (one stereo frame)
        if (outbuf + 2 > out_limit) {
            in_frames = i;
            break;
        }

        // Calculate independent modulated delay times for L and R
        float del1 = fac1 + fac2 * sinewave[(int)cur_phs1];
        float del2 = fac1 + fac2 * sinewave[(int)cur_phs2];

        // Handle Mono or Stereo input sources
        float sampL, sampR;
        if (in_channels == 1) {
            sampL = sampR = *inbuf++;
        } else {
            sampL = *inbuf++;
            sampR = *inbuf++;
        }

        // Process Left Channel
        delput2(sampL, delayline1, dv1);
        *outbuf++ = dliget2(delayline1, del1, dv1, srate);

        // Process Right Channel
        delput2(sampR, delayline2, dv2);
        *outbuf++ = dliget2(delayline2, del2, dv2, srate);

        // Advance LFOs
        cur_phs1 += si1;
        while (cur_phs1 >= sinelen) cur_phs1 -= (float)sinelen;
        while (cur_phs1 < 0)        cur_phs1 += (float)sinelen;
        
        cur_phs2 += si2;
        while (cur_phs2 >= sinelen) cur_phs2 -= (float)sinelen;
        while (cur_phs2 < 0)        cur_phs2 += (float)sinelen;
    }

    // 6. Update Event State
    event->sample_frames = in_frames;
    event->out_channels = 2; // We are now officially a Stereo event
    event->out_start = in_start;
    event->in_start = out_start;
}
