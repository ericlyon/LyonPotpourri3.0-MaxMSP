#include "bashfest.h"

void transpose(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params;
    float *inbuf, *outbuf;
    int i, iphs, ip2;
    float m1, m2, phs = 0, tfac;
    
    ++(*pcount);
    tfac = params[(*pcount)++];
    if (tfac <= 0.001f) tfac = 1.0f;

    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    inbuf = event->workbuffer + in_start;
    outbuf = event->workbuffer + out_start;
    float *out_limit = outbuf + x->halfbuffer; // THE WALL

    int in_frames = event->sample_frames;
    int channels = event->out_channels;
    int out_frames = (int)((float)in_frames / tfac);

    for (i = 0; i < out_frames; i++) {
        if (outbuf + channels > out_limit) { out_frames = i; break; }

        iphs = (int)phs;
        m2 = phs - (float)iphs;
        m1 = 1.0f - m2;

        if (channels == 1) {
            if (iphs + 1 >= in_frames) { out_frames = i; break; }
            *outbuf++ = inbuf[iphs] * m1 + inbuf[iphs + 1] * m2;
        } else {
            ip2 = iphs * 2;
            if (ip2 + 3 >= in_frames * 2) { out_frames = i; break; }
            *outbuf++ = inbuf[ip2] * m1 + inbuf[ip2 + 2] * m2;
            *outbuf++ = inbuf[ip2 + 1] * m1 + inbuf[ip2 + 3] * m2;
        }
        phs += tfac;
    }
    event->sample_frames = out_frames;
    event->out_start = in_start; event->in_start = out_start;
}

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
    int i;
    float phase = 0.0;
    
    ++(*pcount);
    float rmodFreq = params[(*pcount)++];
    if (srate <= 0 || frames <= 0) return;

    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    inbuf = event->workbuffer + in_start;
    outbuf = event->workbuffer + out_start;
    float *out_limit = outbuf + x->halfbuffer; // THE WALL

    float si = ((float) sinelen / srate) * rmodFreq;
    
    for(i = 0; i < frames; i++ ){
        // INTERNAL GUARD
        if (outbuf + channels > out_limit) { frames = i; break; }
        
        while( phase >= sinelen ) phase -= (float)sinelen;
        while( phase < 0 )        phase += (float)sinelen;
        float mod = sinewave[(int)phase];
        
        *outbuf++ = *inbuf++ * mod;
        if( channels == 2 ) *outbuf++ = *inbuf++ * mod;
        phase += si;
    }
    event->sample_frames = frames;
    event->out_start = in_start; event->in_start = out_start;
}


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
    max_safe_samples = x->halfbuffer;
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

    mycombset(delay, revtime, 0, event->delayline1, srate);
    if (channels == 2) mycombset(delay, revtime, 0, event->delayline2, srate);
    
    for (i = 0; i < out_frames; i++) {
        if (outbuf + channels > out_limit) { out_frames = i; break; }
        if (i < in_frames) {
            if (channels == 1) {
                float insamp = *inbuf++;
                *outbuf++ = insamp + mycomb(insamp, event->delayline1);
            } else {
                float insL = *inbuf++; float insR = *inbuf++;
                *outbuf++ = insL + mycomb(insL, event->delayline1);
                *outbuf++ = insR + mycomb(insR, event->delayline2);
            }
        } else {
            if (channels == 1) *outbuf++ = mycomb(0.0f, event->delayline1);
            else { *outbuf++ = mycomb(0.0f, event->delayline1); *outbuf++ = mycomb(0.0f, event->delayline2); }
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


void flange(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params;
    int i, dv1[2], dv2[2];
    float si, mindel, maxdel, fac1, fac2, delsamp1=0, delsamp2=0, delay_time;
    float speed, feedback, phase, minres, maxres;
    
    ++(*pcount);
    minres = params[(*pcount)++]; maxres = params[(*pcount)++];
    speed = params[(*pcount)++]; feedback = params[(*pcount)++];
    phase = params[(*pcount)++];
    
    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *out_limit = outbuf + x->halfbuffer;
    int channels = event->out_channels;

    int hangframes = (int)(x->sr * feedback * 0.25f);
    int out_frames = event->sample_frames + hangframes;

    mindel = 1.0f / (maxres > 0 ? maxres : 1.0f);
    maxdel = 1.0f / (minres > 0 ? minres : 1.0f);
    if( maxdel > x->maxdelay * 0.95f ) maxdel = x->maxdelay * 0.95f;

    delset2(event->delayline1, dv1, x->maxdelay, x->sr);
    if( channels == 2 ) delset2(event->delayline2, dv2, x->maxdelay, x->sr);
    
    si = ((float) x->sinelen / x->sr) * speed;
    phase *= x->sinelen;
    fac2 = 0.5f * (maxdel - mindel); fac1 = mindel + fac2;

    for(i = 0; i < out_frames; i++ ){
        if (outbuf + channels > out_limit) { out_frames = i; break; }
        delay_time = fac1 + fac2 * x->sinewave[(int) phase];
        phase += si;
        while( phase >= x->sinelen ) phase -= x->sinelen;
        while( phase < 0 ) phase += x->sinelen;

        if (i < event->sample_frames) {
            if( channels == 1 ){
                float insamp = *inbuf++;
                delput2( insamp + delsamp1 * feedback, event->delayline1, dv1);
                delsamp1 = dliget2(event->delayline1, delay_time, dv1, x->sr);
                *outbuf++ = insamp + delsamp1;
            } else {
                float insL = *inbuf++; float insR = *inbuf++;
                delput2( insL + delsamp1 * feedback, event->delayline1, dv1);
                delsamp1 = dliget2(event->delayline1, delay_time, dv1, x->sr);
                *outbuf++ = insL + delsamp1;
                delput2( insR + delsamp2 * feedback, event->delayline2, dv2);
                delsamp2 = dliget2(event->delayline2, delay_time, dv2, x->sr);
                *outbuf++ = insR + delsamp2;
            }
        } else {
            // Hangover loop
            delput2( delsamp1 * feedback, event->delayline1, dv1);
            *outbuf++ = delsamp1 = dliget2(event->delayline1, delay_time, dv1, x->sr);
            if( channels == 2 ) {
                delput2( delsamp2 * feedback, event->delayline2, dv2);
                *outbuf++ = delsamp2 = dliget2(event->delayline2, delay_time, dv2, x->sr);
            }
        }
    }
    event->sample_frames = out_frames;
    event->out_start = in_start; event->in_start = out_start;
}


void butterme(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params;
    int ftype;
    float cutoff, cf, bw;
    int channels = event->out_channels;
    float srate = x->sr;
    
    // 1. Parameter Fetch
    ++(*pcount);
    ftype = (int)params[(*pcount)++];
    
    if (srate <= 0 || event->sample_frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    
    // 3. Wall Safety: Limit frames to one half-buffer
    int frames = event->sample_frames;
    if (frames * channels > x->halfbuffer) {
        frames = x->halfbuffer / channels;
    }

    // 4. Branch Filter Processing with Sanitization
    float min_f = 20.0f;
    float max_f = srate * 0.45f;

    if(ftype == HIPASS){
        cutoff = params[(*pcount)++];
        if(cutoff < min_f) cutoff = min_f;
        if(cutoff > max_f) cutoff = max_f;
        butterHipass(inbuf, outbuf, cutoff, frames, channels, srate);
    }
    else if(ftype == LOPASS){
        cutoff = params[(*pcount)++];
        if(cutoff < min_f) cutoff = min_f;
        if(cutoff > max_f) cutoff = max_f;
        butterLopass(inbuf, outbuf, cutoff, frames, channels, srate);
    }
    else if(ftype == BANDPASS){
        cf = params[(*pcount)++];
        bw = params[(*pcount)++];
        if(cf < min_f) cf = min_f;
        if(cf > max_f) cf = max_f;
        if(bw < 5.0f) bw = 5.0f;
        butterBandpass(inbuf, outbuf, cf, bw, frames, channels, srate);
    }
    else {
        // Fallback: Copy input to output
        memcpy(outbuf, inbuf, frames * channels * sizeof(float));
    }

    // 5. Update Event State
    event->sample_frames = frames;
    event->out_start = in_start;
    event->in_start = out_start;
}


void truncateme(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params;
    int i, channels = event->out_channels;
    
    ++(*pcount);
    float shortdur = params[(*pcount)++];
    float fadeout = params[(*pcount)++];
    
    if (x->sr <= 0 || event->sample_frames <= 0) return;

    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf_start = event->workbuffer + out_start;

    int out_frames = (int)(shortdur * x->sr);
    if (out_frames > event->sample_frames) out_frames = event->sample_frames;
    if (out_frames * channels > x->halfbuffer) out_frames = x->halfbuffer / channels;

    if (out_frames > 0) {
        memcpy(outbuf_start, inbuf, out_frames * channels * sizeof(float));
    }

    int fade_frames = (int)(fadeout * x->sr);
    if (fade_frames > out_frames) fade_frames = out_frames;

    if (fade_frames > 0 && out_frames > 0) {
        int f_start_idx = (out_frames - fade_frames) * channels;
        // Absolute check against negative indexing
        if (f_start_idx < 0) f_start_idx = 0;
        
        float *f_ptr = outbuf_start + f_start_idx;
        for (i = 0; i < fade_frames; i++) {
            float gain = 1.0f - ((float)i / (float)fade_frames);
            *f_ptr++ *= gain;
            if (channels == 2) *f_ptr++ *= gain;
            // Never fade past the data we actually have
            if (f_ptr >= outbuf_start + (out_frames * channels)) break;
        }
    }

    event->sample_frames = out_frames;
    event->out_start = in_start; event->in_start = out_start;
}

void sweepreson(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params;
    int i, channels = event->out_channels;
    float srate = x->sr;
    float bwfac, minf, maxf, speed, phase;
    float q1[5], q2[5], cf, bw, si, fac1, fac2;
    
    // 1. Parameter Fetch
    ++(*pcount);
    minf = params[(*pcount)++]; maxf = params[(*pcount)++];
    bwfac = params[(*pcount)++]; speed = params[(*pcount)++];
    phase = params[(*pcount)++];
    
    if (srate <= 0 || event->sample_frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *out_limit = outbuf + x->halfbuffer; // THE WALL

    // 3. Oscillator & Filter Initialization
    si = ((float) x->sinelen / srate) * speed;
    if( phase > 1.0f ) phase = 0.0f;
    phase *= (float)x->sinelen;
    
    fac2 = 0.5f * (maxf - minf);
    fac1 = minf + fac2;
    
    int out_frames = event->sample_frames;

    // First initialization to clear filter history
    cf = fac1 + fac2 * x->sinewave[(int)phase];
    if (cf < 20.0f) cf = 20.0f;
    rsnset2(cf, cf * bwfac, 2.0f, 0.0f, q1, srate);
    if( channels == 2 ) rsnset2(cf, cf * bwfac, 2.0f, 0.0f, q2, srate);

    // 4. Processing Loop
    for(i = 0; i < out_frames; i++ ){
        // WALL GUARD
        if (outbuf + channels > out_limit) {
            out_frames = i;
            break;
        }

        cf = fac1 + fac2 * x->sinewave[(int) phase];
        if (cf < 20.0f) cf = 20.0f;
        if (cf > srate * 0.45f) cf = srate * 0.45f;
        bw = cf * bwfac; if (bw < 1.0f) bw = 1.0f;

        // Update coefficients but KEEP history (xinit = 1.0)
        rsnset2(cf, bw, 2.0f, 1.0f, q1, srate);
        
        if( channels == 1 ){
            *outbuf++ = reson(*inbuf++, q1);
        } else {
            rsnset2(cf, bw, 2.0f, 1.0f, q2, srate);
            *outbuf++ = reson(*inbuf++, q1);
            *outbuf++ = reson(*inbuf++, q2);
        }

        phase += si;
        while( phase >= x->sinelen ) phase -= (float)x->sinelen;
        while( phase < 0 )           phase += (float)x->sinelen;
    }

    // 5. Update Event State
    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

void slidecomb(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float overhang, feedback, delay1, delay2;
    int i, dv1[2], dv2[2];
    float delsamp1 = 0, delsamp2 = 0, m1, m2, delay_time;
    int channels = event->out_channels;
    float srate = x->sr;
    
    ++(*pcount);
    delay1 = x->params[(*pcount)++]; delay2 = x->params[(*pcount)++];
    feedback = x->params[(*pcount)++]; overhang = x->params[(*pcount)++];
    
    if (srate <= 0 || event->sample_frames <= 0) return;

    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf_start = event->workbuffer + out_start;
    float *outbuf = outbuf_start;
    float *out_limit = outbuf + x->halfbuffer; // THE WALL

    if (overhang < COMBFADE) overhang = COMBFADE;
    int in_frames = event->sample_frames;
    int out_frames = in_frames + (int)(overhang * srate);

    if (delay1 > x->maxdelay * 0.95f) delay1 = x->maxdelay * 0.95f;
    if (delay2 > x->maxdelay * 0.95f) delay2 = x->maxdelay * 0.95f;

    delset2(event->delayline1, dv1, x->maxdelay, srate);
    if (channels == 2) delset2(event->delayline2, dv2, x->maxdelay, srate);
    
    for (i = 0; i < out_frames; i++) {
        if (outbuf + channels > out_limit) { out_frames = i; break; }
        
        m2 = (float)i / (float)out_frames; m1 = 1.0f - m2;
        delay_time = (delay1 * m1) + (delay2 * m2);

        if (i < in_frames) {
            if (channels == 1) {
                float ins = *inbuf++;
                delput2(ins + delsamp1 * feedback, event->delayline1, dv1);
                delsamp1 = dliget2(event->delayline1, delay_time, dv1, srate);
                *outbuf++ = ins + delsamp1;
            } else {
                float insL = *inbuf++; float insR = *inbuf++;
                delput2(insL + delsamp1 * feedback, event->delayline1, dv1);
                delsamp1 = dliget2(event->delayline1, delay_time, dv1, srate);
                *outbuf++ = insL + delsamp1;
                delput2(insR + delsamp2 * feedback, event->delayline2, dv2);
                delsamp2 = dliget2(event->delayline2, delay_time, dv2, srate);
                *outbuf++ = insR + delsamp2;
            }
        } else {
            delput2(delsamp1 * feedback, event->delayline1, dv1);
            *outbuf++ = delsamp1 = dliget2(event->delayline1, delay_time, dv1, srate);
            if (channels == 2) {
                delput2(delsamp2 * feedback, event->delayline2, dv2);
                *outbuf++ = delsamp2 = dliget2(event->delayline2, delay_time, dv2, srate);
            }
        }
    }

    // Safe Fadeout
    int fade_frames = (int)(COMBFADE * srate);
    if (fade_frames > out_frames) fade_frames = out_frames;
    float *fade_ptr = outbuf - (fade_frames * channels);
    if (fade_ptr >= outbuf_start) {
        for (i = 0; i < fade_frames; i++) {
            float gain = 1.0f - ((float)i / (float)fade_frames);
            *fade_ptr++ *= gain; if (channels == 2) *fade_ptr++ *= gain;
        }
    }
    event->sample_frames = out_frames;
    event->out_start = in_start; event->in_start = out_start;
}

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
        reverb1me(inbuf, outbuf, event->sample_frames, out_frames, channels, j, revtime, drygain, event->eel, event->mini_delay, x);
    }

    event->sample_frames = out_frames;
    event->out_start = in_start; event->in_start = out_start;
}


void ellipseme(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    int i, j, nsects;
    float xnorm;
    
    ++(*pcount);
    int filtercode = (int)x->params[(*pcount)++];
    if (event->sample_frames <= 0) return;
    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    int channels = event->out_channels;
    int frames = event->sample_frames;
    if (frames * channels > x->halfbuffer) frames = x->halfbuffer / channels;
    if (filtercode < 0 || filtercode >= ELLIPSE_FILTER_COUNT) {
        memcpy(outbuf, inbuf, frames * channels * sizeof(float));
    } else {
        float *fltdata = x->ellipse_data[filtercode];
        for (j = 0; j < channels; j++) {
            // Re-initializing resets the history in x->eel
            ellipset(fltdata, event->eel, &nsects, &xnorm);
            for (i = j; i < frames * channels; i += channels) {
                outbuf[i] = ellipse(inbuf[i], event->eel, nsects, xnorm);
            }
        }
    }
    event->sample_frames = frames;
    event->out_start = in_start; event->in_start = out_start;
}

void feed1me(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float mindelay, maxdelay, speed1, speed2, overhang;
    float phz1, phz2; // Local phase variables
    
    ++(*pcount); // Skip Code
    mindelay = x->params[(*pcount)++];
    maxdelay = x->params[(*pcount)++];
    speed1   = x->params[(*pcount)++];
    speed2   = x->params[(*pcount)++];
    overhang = x->params[(*pcount)++];
    
    if (x->sr <= 0 || event->sample_frames <= 0) return;

    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    int channels = event->out_channels;

    // 1. Output Sizing and Wall Guard
    int out_frames = event->sample_frames + (int)(x->sr * overhang);
    if (out_frames * channels > x->halfbuffer) {
        out_frames = x->halfbuffer / channels;
    }
    float desired_dur = (float)out_frames / x->sr;

    if (maxdelay > x->max_mini_delay) maxdelay = x->max_mini_delay * 0.95f;

    // 2. Generate 4 LFO Tables
    // CRITICAL FIX: reset phz1/phz2 before each call or funcgen1 will explode indices
    phz1 = 0.13f; phz2 = 0.251f;
    funcgen1(event->feedfunc1, x->feedfunclen, desired_dur, mindelay, maxdelay,
             speed1, speed2, 1.0f, 1.0f, &phz1, &phz2, x->sinewave, x->sinelen);
    
    phz1 = 0.35f; phz2 = 0.12f;
    funcgen1(event->feedfunc2, x->feedfunclen, desired_dur, mindelay*0.5f, maxdelay*2.0f,
             speed1*1.25f, speed2*0.75f, 1.0f, 1.0f, &phz1, &phz2, x->sinewave, x->sinelen);
    
    phz1 = 0.61f; phz2 = 0.93f;
    funcgen1(event->feedfunc3, x->feedfunclen, desired_dur, 0.1f, 0.7f,
             speed1*0.35f, speed2*1.25f, 1.0f, 1.0f, &phz1, &phz2, x->sinewave, x->sinelen);
    
    phz1 = 0.22f; phz2 = 0.44f;
    funcgen1(event->feedfunc4, x->feedfunclen, desired_dur, 0.1f, 0.7f,
             speed1*0.55f, speed2*2.25f, 1.0f, 1.0f, &phz1, &phz2, x->sinewave, x->sinelen);
    
    // 3. Process the Feedback Delay Network
    feed1(inbuf, outbuf, event->sample_frames, out_frames, channels,
          event->feedfunc1, event->feedfunc2, event->feedfunc3, event->feedfunc4,
          x->feedfunclen, desired_dur, x->max_mini_delay, event->mini_delay, x);
    
    // 4. Update Event State
    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

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



void flam2(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params;
    int attacks, i, j, k;
    float gain2, gainatten, delay1, delay2, gain = 1.0f;

    ++(*pcount);
    attacks = (int)params[(*pcount)++];
    gain2 = params[(*pcount)++]; gainatten = params[(*pcount)++];
    delay1 = params[(*pcount)++]; delay2 = params[(*pcount)++];
    
    if (x->sr <= 0 || event->sample_frames <= 0) return;
    int in_frames = event->sample_frames;
    int channels = event->out_channels;

    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;

    // 1. Precise Duration Calculation
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

        // Logic fix: Advance time_acc ONLY after the current attack is summed
        int idx = (int)(((float)i / (float)attacks) * (float)x->flamfunc1len);
        if (idx >= x->flamfunc1len) idx = x->flamfunc1len - 1;
        time_acc += mapp(x->flamfunc1[idx], 0.0f, 1.0f, delay2, delay1);

        gain = (i == 0) ? gain2 : gain * gainatten;
        if (gain < 0.0001f) break;
    }

    event->sample_frames = out_frames;
    event->out_start = in_start; event->in_start = out_start;
}



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
    setExpFlamFunc(event->feedfunc1, attacks, delay1, delay2, slope);

    float total_t = 0;
    for (i = 0; i < attacks - 1; i++) total_t += event->feedfunc1[i];
    
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
        
        if (i < attacks - 1) time_acc += event->feedfunc1[i];
        gain = (i == 0) ? gain2 : gain * gainatten;
        if (gain < 0.0001f) break;
    }

    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

void comb4(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float overhang, revtime, rez;
    int i, j, k;
    
    ++(*pcount); // Skip Code
    
    int channels = event->out_channels;
    int in_frames = event->sample_frames;
    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    
    float *inbuf = event->workbuffer + in_start;
    float *outbuf_start = event->workbuffer + out_start;
    
    if (x->sr <= 0 || in_frames <= 0) return;

    // 1. Fetch and Sanitize 4 Resonance Frequencies
    for( j = 0; j < 4; j++ ){
        rez = x->params[(*pcount)++];
        if (rez < 10.0f) rez = 10.0f; // Minimum 10Hz resonance
        
        event->combies[j]->lpt = 1.0f / rez;
        if (event->combies[j]->lpt > x->max_comb_lpt) {
            event->combies[j]->lpt = x->max_comb_lpt;
        }
    }
    
    revtime = x->params[(*pcount)++];
    overhang = x->params[(*pcount)++];
    if (overhang < COMBFADE) overhang = COMBFADE;

    // 2. Calculate Output Length and Apply THE WALL
    int out_frames = in_frames + (int)(overhang * x->sr);
    
    // SAFETY GUARD: Output must not exceed exactly one half-buffer
    if (out_frames * channels > x->halfbuffer) {
        out_frames = x->halfbuffer / channels;
    }

    // 3. Processing: Loop through each channel Pass
    for (j = 0; j < channels; j++) {
        // Initialize/Clear the 4 combs for this channel PASS
        for (k = 0; k < 4; k++) {
            mycombset(event->combies[k]->lpt, revtime, 0, event->combies[k]->arr, x->sr);
        }

        // Process frames
        for (i = 0; i < out_frames; i++) {
            // Only read from inbuf if we are within the input range
            float input_sample = (i < in_frames) ? inbuf[i * channels + j] : 0.0f;
            float summed_combs = 0.0f;
            
            for (k = 0; k < 4; k++) {
                summed_combs += mycomb(input_sample, event->combies[k]->arr);
            }
            outbuf_start[i * channels + j] = summed_combs;
        }
    }

    // 4. Safe Fadeout
    int fade_frames = (int)(COMBFADE * x->sr);
    if (fade_frames > out_frames) fade_frames = out_frames;
    if (fade_frames > 0) {
        float *fade_ptr = outbuf_start + ((out_frames - fade_frames) * channels);
        for (i = 0; i < fade_frames; i++) {
            float gain = 1.0f - ((float)i / (float)fade_frames);
            for (int c = 0; c < channels; c++) {
                *fade_ptr++ *= gain;
            }
        }
    }

    // 5. Cleanup and State Update
    killdc(outbuf_start, out_frames, channels, event->eel, x);
    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}

void compdist(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float cutoff, maxmult, maxamp;
    int lookupflag, channels = event->out_channels;
    
    ++(*pcount);
    cutoff = x->params[(*pcount)++];
    maxmult = x->params[(*pcount)++];
    lookupflag = (int)x->params[(*pcount)++];
    
    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    
    int frames = event->sample_frames;
    if (frames * channels > x->halfbuffer) frames = x->halfbuffer / channels;

    maxamp = getmaxamp(inbuf, frames * channels);
    if(lookupflag) set_distortion_table(event->transfer_function, cutoff, maxmult, x->tf_len);
    
    if (maxamp > 0.0001f) {
        for (int j = 0; j < channels; j++) {
            do_compdist(inbuf, outbuf, frames, channels, j, cutoff, maxmult, lookupflag, event->transfer_function, x->tf_len, maxamp);
        }
    } else {
        memcpy(outbuf, inbuf, frames * channels * sizeof(float));
    }
    event->sample_frames = frames;
    event->out_start = in_start; event->in_start = out_start;
}


void ringfeed(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    float *params = x->params;
    float ring_freq, rez, rvbt, reson_cf, reson_bw_fac, overhang;
    float sample;
    float gain_compensation = 0.1f;
    
    // 1. Parameter Fetch
    ++(*pcount);
    ring_freq    = params[(*pcount)++];
    rez          = params[(*pcount)++];
    rvbt         = params[(*pcount)++];
    reson_cf     = params[(*pcount)++];
    reson_bw_fac = params[(*pcount)++];
    overhang     = params[(*pcount)++];

    if (x->sr <= 0 || event->sample_frames <= 0) return;

    // 2. Setup Pointers (Ping-Pong)
    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf_start = event->workbuffer + out_start;
    int channels = event->out_channels;
    int in_frames = event->sample_frames;

    // 3. DSP Sanitization
    float lfo_si = ring_freq * ((float)x->sinelen / x->sr);
    float lfo_phs = 0;
    float lpt = 1.0f / (rez > 10.0f ? rez : 10.0f);
    if (lpt > x->max_comb_lpt) lpt = x->max_comb_lpt;
    if (rvbt >= 1.0f) rvbt = 0.99f;
    if (rvbt < 0.0f)  rvbt = 0.0f;

    // Filter sanity
    if (reson_cf < 20.0f) reson_cf = 20.0f;
    if (reson_cf > x->sr * 0.45f) reson_cf = x->sr * 0.45f;
    float reson_bw = reson_cf * reson_bw_fac;
    if (reson_bw < 1.0f) reson_bw = 1.0f;

    // 4. Initialize Local Slot State
    for (int j = 0; j < channels; j++) {
        mycombset(lpt, rvbt, 0, event->combies[j]->arr, x->sr);
        rsnset2(reson_cf, reson_bw, 2.0f, 0, event->resies[j]->q, x->sr);
    }

    // 5. THE WALL: Calculate safe output frames upfront
    int out_frames = in_frames + (int)(overhang * x->sr);
    int max_safe_frames = x->halfbuffer / channels;

    if (out_frames > max_safe_frames) {
        out_frames = max_safe_frames;
    }

    // 6. Processing Loop
    for (int i = 0; i < out_frames; i++) {
        float mod = oscil(1.0f, lfo_si, x->sinewave, x->sinelen, &lfo_phs);
        
        for (int j = 0; j < channels; j++) {
            // Stage 1: Ring Mod (Feeding into Stage 2)
            float input_sample = (i < in_frames) ? inbuf[i * channels + j] : 0.0f;
            float sig = input_sample * mod;
            
            // Stage 2: Comb
            sig = sig + mycomb(sig, event->combies[j]->arr);
            
            // Stage 3: Reson
            sample = reson(sig, event->resies[j]->q);
            sample *= gain_compensation;
            
            // Anti-Denormal protection
            if (sample > -1.0e-15f && sample < 1.0e-15f) {
                sample = 0.0f;
            }
            
            outbuf_start[i * channels + j] = sample;
        }
    }

    // 7. Update Event State
    event->sample_frames = out_frames;
    event->out_start = in_start;
    event->in_start = out_start;
}
// AI version
void resonadsr(t_bashfest *x, int slot, int *pcount)
{
    t_event *event = &x->events[slot];
    CMIXADSR *a = event->adsr;
    float bwfac, q1[5], q2[5], phase = 0;
    
    ++(*pcount);
    a->a = x->params[(*pcount)++];
    a->d = x->params[(*pcount)++];
    a->r = x->params[(*pcount)++];
    a->v1 = x->params[(*pcount)++];
    a->v2 = x->params[(*pcount)++];
    a->v3 = x->params[(*pcount)++];
    a->v4 = x->params[(*pcount)++];
    bwfac = x->params[(*pcount)++];

    int in_start = event->in_start;
    int out_start = (in_start + x->halfbuffer) % x->buf_samps;
    float *inbuf = event->workbuffer + in_start;
    float *outbuf = event->workbuffer + out_start;
    float *out_limit = outbuf + x->halfbuffer;
    float sample;
    int channels = event->out_channels;

    float notedur = (float)event->sample_frames / x->sr;
    a->s = notedur - (a->a + a->d + a->r);
    if (a->s <= 0) { a->a=a->d=a->s=a->r=notedur/4.01f; }
    buildadsr(a);
    float si = (float)a->len / (float)event->sample_frames;

    for (int i = 0; i < event->sample_frames; i++) {
        if (outbuf + channels > out_limit) { event->sample_frames = i; break; }
        float cf = a->func[(int)phase];
        rsnset2(cf, cf * bwfac, 2.0f, 1.0f, q1, x->sr);
        sample = reson(*inbuf++, q1);
        if ((sample > -1.0e-15f) && (sample < 1.0e-15f)) sample = 0.0f;
        *outbuf++ = sample;
        if (channels == 2) {
            rsnset2(cf, cf * bwfac, 2.0f, 1.0f, q2, x->sr);
            if ((sample > -1.0e-15f) && (sample < 1.0e-15f)) sample = 0.0f;
            sample = reson(*inbuf++, q2);
            *outbuf++ = sample;
        }
        phase += si; if (phase >= a->len) phase = a->len - 1;
    }
    event->out_start = in_start; event->in_start = out_start;
}



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
    float *delayline1 = event->delayline1;
    float *delayline2 = event->delayline2;
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
