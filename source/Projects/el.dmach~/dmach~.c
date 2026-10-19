/**
 * @file dmach~.c
 * @brief Multi-channel drum pattern sequencer and trigger generator for Max 8/9.
 * Modernized for 64-bit dsp64, deep nested memory deallocation, and thread safety.
 */

#include "MSPd.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define MAX_ATTACKS (256)
#define MAX_PATTERNS (512)
#define OBJECT_NAME "dmach~"

#define COUNTER_UP (1)
#define COUNTER_DOWN (-1)

static t_class *dmach_class;

typedef struct {
    float trigger_point;
    float increment;
    float amplitude;
} t_attack;

typedef struct {
    short active;
    int attack_count;
    int adex;
    t_attack *attacks;
} t_drumline;

typedef struct {
    float beats;
    float dsamps;
    t_drumline *drumlines;
} t_pattern;

typedef struct _dmach {
    t_pxobject x_obj;
    short mute;
    float clocker;
    float tempo;
    float tempo_factor;
    t_pattern *patterns;
    short *stored_patterns;
    float *gtranspose;
    float *gains;
    float *current_increment;
    int this_pattern;
    int next_pattern;
    float global_gain;
    float global_transpose;
    float sr;
    int drum_count;
    int outlet_count;
    short virgin;
    short playsequence;
    short loopsequence;
    int *sequence;
    int sequence_length;
    int seqptr;
    float zeroalias;
    t_atom *listdata;
    void *listraw_outlet;
    short clickincr;
    short *attackpattern;
    int attackpattern_count;
    t_attack *tmpatks;
    short *connected;
    short *muted;
} t_dmach;

/* Method Prototypes */
void *dmach_new(t_symbol *s, long argc, t_atom *argv);
void dmach_free(t_dmach *x);
void dmach_assist(t_dmach *x, void *b, long m, long a, char *s);
void dmach_store(t_dmach *x, t_symbol *s, long argc, t_atom *argv);
void dmach_mute(t_dmach *x, double toggle);
void dmach_init_pattern(t_dmach *x, int pnum);
void dmach_show(t_dmach *x, double fn);
void dmach_tempo(t_dmach *x, double new_tempo);
void dmach_recall(t_dmach *x, double pnf);
void dmach_transpose(t_dmach *x, double slotf, double new_transpose_factor);
void dmach_gain(t_dmach *x, double slotf, double new_gain_factor);
void dmach_arm(t_dmach *x, double pnf);
void dmach_playsequence(t_dmach *x, t_symbol *s, long argc, t_atom *argv);
void dmach_loopsequence(t_dmach *x, t_symbol *s, long argc, t_atom *argv);
void dmach_slotamps(t_dmach *x, t_symbol *s, long argc, t_atom *argv);
void dmach_slotampsfull(t_dmach *x, t_symbol *s, long argc, t_atom *argv);
void dmach_slotincrs(t_dmach *x, t_symbol *s, long argc, t_atom *argv);
void dmach_muteslot(t_dmach *x, t_symbol *s, long argc, t_atom *argv);
void dmach_nosequence(t_dmach *x);
void dmach_copypattern(t_dmach *x, double pn1, double pn2);
void dmach_printraw(t_dmach *x, double fn);
void dmach_readraw(t_dmach *x, t_symbol *s, long argc, t_atom *argv);
void dmach_listraw(t_dmach *x, t_symbol *s, long argc, t_atom *argv);
void dmach_clickincr(t_dmach *x, double toggle);
void dmach_dsp64(t_dmach *x, t_object *dsp64, short *count, double samplerate, long n, long flags);
void dmach_perform64(t_dmach *x, t_object *dsp64, double **ins, 
                     long numins, double **outs, long numouts, long n,
                     long flags, void *userparam);

int C74_EXPORT main(void)
{
    t_class *c;
    c = class_new("el.dmach~", 
                   (method)dmach_new, 
                   (method)dmach_free, 
                   (long)sizeof(t_dmach), 
                   0L, 
                   A_GIMME, 
                   0);

    class_addmethod(c, (method)dmach_dsp64,       "dsp64",        A_CANT, 0);
    class_addmethod(c, (method)dmach_assist,      "assist",       A_CANT, 0);
    class_addmethod(c, (method)dmach_show,        "show",         A_FLOAT, 0);
    class_addmethod(c, (method)dmach_recall,      "recall",       A_FLOAT, 0);
    class_addmethod(c, (method)dmach_store,       "store",        A_GIMME, 0);
    class_addmethod(c, (method)dmach_transpose,   "transpose",    A_FLOAT, A_FLOAT, 0);
    class_addmethod(c, (method)dmach_tempo,       "tempo",        A_FLOAT, 0);
    class_addmethod(c, (method)dmach_gain,        "gain",         A_FLOAT, A_FLOAT, 0);    
    class_addmethod(c, (method)dmach_listraw,     "listraw",      A_GIMME, 0);
    class_addmethod(c, (method)dmach_printraw,    "printraw",     A_FLOAT, 0);
    class_addmethod(c, (method)dmach_arm,         "arm",          A_FLOAT, 0);
    class_addmethod(c, (method)dmach_readraw,     "readraw",      A_GIMME, 0);
    class_addmethod(c, (method)dmach_playsequence, "playsequence", A_GIMME, 0);
    class_addmethod(c, (method)dmach_slotamps,    "slotamps",     A_GIMME, 0);
    class_addmethod(c, (method)dmach_slotampsfull,"slotampsfull", A_GIMME, 0);
    class_addmethod(c, (method)dmach_slotincrs,   "slotincrs",    A_GIMME, 0);
    class_addmethod(c, (method)dmach_loopsequence,"loopsequence", A_GIMME, 0);
    class_addmethod(c, (method)dmach_muteslot,    "muteslot",     A_GIMME, 0);
    class_addmethod(c, (method)dmach_nosequence,  "nosequence",   0);
    class_addmethod(c, (method)dmach_copypattern, "copypattern",  A_FLOAT, A_FLOAT, 0);
    class_addmethod(c, (method)dmach_clickincr,   "clickincr",    A_FLOAT, 0);

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    dmach_class = c;

    potpourri_announce(OBJECT_NAME);
    return 0;
}

void dmach_muteslot(t_dmach *x, t_symbol *s, long argc, t_atom *argv)
{
    if (argc < 2) return;
    int slot = (int)atom_getfloat(argv);
    short mutestate = (short)atom_getfloat(argv + 1);

    if (slot < 0 || slot >= x->drum_count) return;
    x->muted[slot] = mutestate;
}

void dmach_nosequence(t_dmach *x)
{
    x->playsequence = 0;
    x->loopsequence = 0;
}

void dmach_playsequence(t_dmach *x, t_symbol *s, long argc, t_atom *argv)
{
    if (argc < 1 || argc > 1024) return;
    for (int i = 0; i < argc; i++) {
        int pnum = (int)atom_getfloat(argv + i);
        if (pnum < 0 || pnum >= MAX_PATTERNS || !x->stored_patterns[pnum]) return;
        x->sequence[i] = pnum;
    }
    x->this_pattern = x->sequence[0];
    x->clocker = x->patterns[x->this_pattern].dsamps;
    x->mute = 0;
    x->playsequence = 1;
    x->loopsequence = 0;
    x->sequence_length = (int)argc;
    x->seqptr = 0;
}

void dmach_loopsequence(t_dmach *x, t_symbol *s, long argc, t_atom *argv)
{
    if (argc < 1 || argc > 1024) return;
    for (int i = 0; i < argc; i++) {
        int pnum = (int)atom_getfloat(argv + i);
        if (pnum < 0 || pnum >= MAX_PATTERNS || !x->stored_patterns[pnum]) return;
        x->sequence[i] = pnum;
    }
    x->this_pattern = x->sequence[0];
    x->clocker = x->patterns[x->this_pattern].dsamps;
    x->mute = 0;
    x->playsequence = 1;
    x->loopsequence = 1;
    x->sequence_length = (int)argc;
    x->seqptr = 0;
}

void dmach_gain(t_dmach *x, double slotf, double new_gain_factor)
{
    int slot = (int)slotf;
    if (slot < 0 || slot >= x->drum_count) return;
    x->gains[slot] = (float)new_gain_factor;
}

void dmach_transpose(t_dmach *x, double slotf, double new_transpose_factor)
{
    int slot = (int)slotf;
    if (slot < 0 || slot >= x->drum_count || new_transpose_factor == 0.0) return;
    x->gtranspose[slot] = (float)new_transpose_factor;
}

void dmach_recall(t_dmach *x, double pnf)
{
    int pnum = (int)pnf;
    if (pnum < 0 || pnum >= MAX_PATTERNS || !x->stored_patterns[pnum]) return;
    x->next_pattern = pnum;
}

void dmach_arm(t_dmach *x, double pnf)
{
    int pnum = (int)pnf;
    if (pnum < 0 || pnum >= MAX_PATTERNS || !x->stored_patterns[pnum]) return;

    x->mute = 1;
    x->clocker = 0.0f;
    x->next_pattern = x->this_pattern = pnum;
    for (int i = 0; i < x->drum_count; i++) {
        x->patterns[x->this_pattern].drumlines[i].adex = 0;
    }
}

void dmach_tempo(t_dmach *x, double new_tempo)
{
    if (new_tempo <= 0.0) return;
    float ratio = x->tempo / (float)new_tempo;
    x->clocker *= ratio;
    x->tempo = (float)new_tempo;
    x->tempo_factor = 60.0f / x->tempo;

    for (int i = 0; i < MAX_PATTERNS; i++) {
        if (x->stored_patterns[i]) {
            x->patterns[i].dsamps = x->patterns[i].beats * x->tempo_factor * x->sr;
            for (int j = 0; j < x->drum_count; j++) {
                if (x->patterns[i].drumlines[j].active) {
                    for (int k = 0; k < x->patterns[i].drumlines[j].attack_count; k++) {
                        x->patterns[i].drumlines[j].attacks[k].trigger_point *= ratio;
                    }
                }
            }
        }
    }
}

void dmach_show(t_dmach *x, double fn)
{
    int pnum = (int)fn;
    if (pnum < 0 || pnum >= MAX_PATTERNS || !x->stored_patterns[pnum]) return;

    post("showing pattern %d", pnum);
    for (int j = 0; j < x->drum_count; j++) {
        if (x->patterns[pnum].drumlines[j].active) {
            post("*** drum line for slot %d ***", j);
            t_attack *ptr = x->patterns[pnum].drumlines[j].attacks;
            for (int i = 0; i < x->patterns[pnum].drumlines[j].attack_count; i++) {
                post("amp: %f, transp: %f, trigger: %f", ptr->amplitude, ptr->increment, ptr->trigger_point);
                ptr++;
            }
        }
    }
}

void dmach_printraw(t_dmach *x, double fn)
{
    int pnum = (int)fn;
    if (pnum < 0 || pnum >= MAX_PATTERNS || !x->stored_patterns[pnum] || x->tempo_factor <= 0.0f) return;

    post("readraw %d %f", pnum, x->patterns[pnum].beats);
    for (int j = 0; j < x->drum_count; j++) {
        if (x->patterns[pnum].drumlines[j].active) {
            t_attack *ptr = x->patterns[pnum].drumlines[j].attacks;
            post("%d %d", j, x->patterns[pnum].drumlines[j].attack_count);
            for (int i = 0; i < x->patterns[pnum].drumlines[j].attack_count; i++) {
                float norm_trig = ptr->trigger_point / (x->tempo_factor * x->sr);
                post("%f %f %f", ptr->amplitude, ptr->increment, norm_trig);
                ptr++;
            }
        }
    }
}

void dmach_listraw(t_dmach *x, t_symbol *s, long argc, t_atom *argv)
{
    int pnum = (argc > 0) ? (int)atom_getfloat(argv) : x->this_pattern;
    if (pnum < 0 || pnum >= MAX_PATTERNS || !x->stored_patterns[pnum] || x->tempo_factor <= 0.0f) return;

    int ldex = 0;
    t_atom *listdata = x->listdata;

    atom_setsym(listdata + ldex++, gensym("readraw"));
    atom_setfloat(listdata + ldex++, (float)pnum);
    atom_setfloat(listdata + ldex++, x->patterns[pnum].beats);

    for (int j = 0; j < x->drum_count; j++) {
        if (x->patterns[pnum].drumlines[j].active) {
            t_attack *ptr = x->patterns[pnum].drumlines[j].attacks;
            atom_setfloat(listdata + ldex++, (float)j);
            atom_setfloat(listdata + ldex++, (float)x->patterns[pnum].drumlines[j].attack_count);

            for (int i = 0; i < x->patterns[pnum].drumlines[j].attack_count; i++) {
                float norm_trig = ptr->trigger_point / (x->tempo_factor * x->sr);
                atom_setfloat(listdata + ldex++, ptr->amplitude);
                atom_setfloat(listdata + ldex++, ptr->increment);
                atom_setfloat(listdata + ldex++, norm_trig);
                ptr++;
            }
        }
    }
    outlet_list(x->listraw_outlet, NULL, ldex, listdata);
}

void dmach_copypattern(t_dmach *x, double pn1, double pn2)
{
    int p_from = (int)pn1;
    int p_to = (int)pn2;
    if (p_from < 0 || p_from >= MAX_PATTERNS || p_to < 0 || p_to >= MAX_PATTERNS || !x->stored_patterns[p_from]) return;

    dmach_init_pattern(x, p_to);
    x->patterns[p_to].beats = x->patterns[p_from].beats;
    x->patterns[p_to].dsamps = x->patterns[p_from].dsamps;

    for (int j = 0; j < x->drum_count; j++) {
        x->patterns[p_to].drumlines[j].active = x->patterns[p_from].drumlines[j].active;
        if (x->patterns[p_from].drumlines[j].active) {
            t_attack *from = x->patterns[p_from].drumlines[j].attacks;
            t_attack *to = x->patterns[p_to].drumlines[j].attacks;
            x->patterns[p_to].drumlines[j].attack_count = x->patterns[p_from].drumlines[j].attack_count;
            for (int i = 0; i < x->patterns[p_from].drumlines[j].attack_count; i++) {
                to->amplitude = from->amplitude;
                to->increment = from->increment;
                to->trigger_point = from->trigger_point;
                from++;
                to++;
            }
        }
    }
    x->stored_patterns[p_to] = 1;
}

void dmach_readraw(t_dmach *x, t_symbol *s, long argc, t_atom *argv)
{
    if (argc < 2) return;
    int pdex = 0;
    int pnum = (int)atom_getfloat(argv + pdex++);

    if (pnum < 0 || pnum >= MAX_PATTERNS) return;

    x->stored_patterns[pnum] = 1;
    dmach_init_pattern(x, pnum);

    x->patterns[pnum].beats = atom_getfloat(argv + pdex++);
    x->patterns[pnum].dsamps = x->patterns[pnum].beats * x->tempo_factor * x->sr;

    while (pdex < argc) {
        int slot = (int)atom_getfloat(argv + pdex++);
        if (slot < 0 || slot >= x->drum_count) break;

        x->patterns[pnum].drumlines[slot].active = 1;
        x->patterns[pnum].drumlines[slot].attack_count = (int)atom_getfloat(argv + pdex++);
        x->patterns[pnum].drumlines[slot].adex = 0;
        t_attack *ptr = x->patterns[pnum].drumlines[slot].attacks;

        for (int i = 0; i < x->patterns[pnum].drumlines[slot].attack_count && pdex < argc; i++) {
            ptr->amplitude = atom_getfloat(argv + pdex++);
            ptr->increment = atom_getfloat(argv + pdex++);
            ptr->trigger_point = (x->sr * x->tempo_factor) * atom_getfloat(argv + pdex++);
            ptr++;
        }
    }
    x->this_pattern = x->next_pattern = pnum;
    x->virgin = 0;
}

void dmach_slotamps(t_dmach *x, t_symbol *s, long argc, t_atom *argv)
{
    if (argc < 3) return;
    int pdex = 0;
    int pnum = (int)atom_getfloat(argv + pdex++);
    if (pnum < 0 || pnum >= MAX_PATTERNS || !x->stored_patterns[pnum]) return;

    int slot = (int)atom_getfloat(argv + pdex++);
    if (slot < 0 || slot >= x->drum_count) return;

    float subdiv = atom_getfloat(argv + pdex++);
    if (subdiv <= 0.0f) return;

    float beat_samps = (x->patterns[pnum].beats / subdiv) * x->tempo_factor * x->sr;
    float trigger_point = 0.0f;
    int local_attacks = 0;
    memset(x->tmpatks, 0, MAX_ATTACKS * sizeof(t_attack));

    for (int i = 0; i < subdiv && pdex < argc; i++) {
        float val = atom_getfloat(argv + pdex++);
        if (val != 0.0f && local_attacks < MAX_ATTACKS) {
            x->tmpatks[local_attacks].amplitude = val;
            x->tmpatks[local_attacks].trigger_point = trigger_point;
            x->tmpatks[local_attacks].increment = 1.0f;
            local_attacks++;
        }
        trigger_point += beat_samps;
    }

    x->patterns[pnum].drumlines[slot].active = 0;
    memcpy(x->patterns[pnum].drumlines[slot].attacks, x->tmpatks, MAX_ATTACKS * sizeof(t_attack));
    x->patterns[pnum].drumlines[slot].attack_count = local_attacks;
    x->patterns[pnum].drumlines[slot].adex = 0;
    x->patterns[pnum].drumlines[slot].active = 1;
    x->this_pattern = x->next_pattern = pnum;
}

void dmach_slotampsfull(t_dmach *x, t_symbol *s, long argc, t_atom *argv)
{
    if (argc < 3) return;
    int pdex = 0;
    int pnum = (int)atom_getfloat(argv + pdex++);
    if (pnum < 0 || pnum >= MAX_PATTERNS || !x->stored_patterns[pnum]) return;

    int slot = (int)atom_getfloat(argv + pdex++);
    if (slot < 0 || slot >= x->drum_count) return;

    x->patterns[pnum].drumlines[slot].active = 1;
    float tmpbeats = x->patterns[pnum].beats;
    int attack_count = 0;
    float trigger_point = 0.0f;

    while (tmpbeats > 0.0f && pdex < argc) {
        float beatseg = atom_getfloat(argv + pdex++);
        float subdiv = atom_getfloat(argv + pdex++);
        if (subdiv <= 0.0f) break;

        float beat_samps = (beatseg / subdiv) * x->tempo_factor * x->sr;
        int local_attacks = 0;

        for (int i = 0; i < subdiv && pdex < argc; i++) {
            float val = atom_getfloat(argv + pdex++);
            if (val != 0.0f && (attack_count + local_attacks) < MAX_ATTACKS) {
                x->patterns[pnum].drumlines[slot].attacks[attack_count + local_attacks].amplitude = val;
                x->patterns[pnum].drumlines[slot].attacks[attack_count + local_attacks].trigger_point = trigger_point;
                x->patterns[pnum].drumlines[slot].attacks[attack_count + local_attacks].increment = 1.0f;
                local_attacks++;
            }
            trigger_point += beat_samps;
        }
        tmpbeats -= beatseg;
        attack_count += local_attacks;
    }
    x->patterns[pnum].drumlines[slot].attack_count = attack_count;
}

void dmach_slotincrs(t_dmach *x, t_symbol *s, long argc, t_atom *argv)
{
    if (argc < 3) return;
    int pdex = 0;
    int pnum = (int)atom_getfloat(argv + pdex++);
    int slot = (int)atom_getfloat(argv + pdex++);

    if (pnum < 0 || pnum >= MAX_PATTERNS || !x->stored_patterns[pnum] || slot < 0 || slot >= x->drum_count) return;

    int local_attacks = x->patterns[pnum].drumlines[slot].attack_count;
    for (int i = 0; i < local_attacks && pdex < argc; i++) {
        x->patterns[pnum].drumlines[slot].attacks[i].increment = (float)atom_getfloat(argv + pdex++);
    }
    x->this_pattern = x->next_pattern = pnum;
}

void dmach_store(t_dmach *x, t_symbol *s, long argc, t_atom *argv)
{
    if (argc < 2) return;
    int pdex = 0;
    int pnum = (int)atom_getfloat(argv + pdex++);
    if (pnum < 0 || pnum >= MAX_PATTERNS) return;

    dmach_init_pattern(x, pnum);

    x->patterns[pnum].beats = atom_getfloat(argv + pdex++);
    if (x->patterns[pnum].beats <= 0.0f) x->patterns[pnum].beats = 4.0f;

    x->patterns[pnum].dsamps = x->patterns[pnum].beats * x->tempo_factor * x->sr;

    while (pdex < argc) {
        int slot = (int)atom_getfloat(argv + pdex++);
        if (slot < 0 || slot >= x->drum_count) break;

        x->patterns[pnum].drumlines[slot].active = 1;
        float tmpbeats = x->patterns[pnum].beats;
        float trigger_point = 0.0f;
        int attack_count = 0;

        while (tmpbeats > 0.0f && pdex < argc) {
            float beatseg = atom_getfloat(argv + pdex++);
            float subdiv = atom_getfloat(argv + pdex++);
            if (subdiv <= 0.0f) break;

            float beat_samps = (beatseg / subdiv) * x->tempo_factor * x->sr;
            int local_attacks = 0;

            for (int i = 0; i < subdiv && pdex < argc; i++) {
                float val = atom_getfloat(argv + pdex++);
                if (val != 0.0f && (attack_count + local_attacks) < MAX_ATTACKS) {
                    x->patterns[pnum].drumlines[slot].attacks[attack_count + local_attacks].amplitude = val;
                    x->patterns[pnum].drumlines[slot].attacks[attack_count + local_attacks].trigger_point = trigger_point;
                    local_attacks++;
                }
                trigger_point += beat_samps;
            }

            for (int i = 0; i < local_attacks && pdex < argc; i++) {
                x->patterns[pnum].drumlines[slot].attacks[attack_count + i].increment = (float)atom_getfloat(argv + pdex++);
            }

            tmpbeats -= beatseg;
            attack_count += local_attacks;
        }
        x->patterns[pnum].drumlines[slot].attack_count = attack_count;
        x->patterns[pnum].drumlines[slot].adex = 0;
    }

    x->this_pattern = x->next_pattern = pnum;
    x->virgin = 0;
    x->stored_patterns[pnum] = 1;
}

void dmach_init_pattern(t_dmach *x, int pnum)
{
    if (pnum < 0 || pnum >= MAX_PATTERNS) return;

    if (x->patterns[pnum].drumlines == NULL) {
        x->patterns[pnum].drumlines = (t_drumline *)sysmem_newptrclear(x->drum_count * sizeof(t_drumline));
    }

    for (int i = 0; i < x->drum_count; i++) {
        if (x->patterns[pnum].drumlines[i].attacks == NULL) {
            x->patterns[pnum].drumlines[i].attacks = (t_attack *)sysmem_newptrclear(MAX_ATTACKS * sizeof(t_attack));
        }
        x->patterns[pnum].drumlines[i].adex = 0;
        x->patterns[pnum].drumlines[i].active = 0;
        x->patterns[pnum].drumlines[i].attack_count = 0;
    }
}

void dmach_free(t_dmach *x)
{
    dsp_free((t_pxobject *)x);

    if (x->patterns) {
        for (int i = 0; i < MAX_PATTERNS; i++) {
            if (x->patterns[i].drumlines) {
                for (int j = 0; j < x->drum_count; j++) {
                    if (x->patterns[i].drumlines[j].attacks) {
                        sysmem_freeptr(x->patterns[i].drumlines[j].attacks);
                    }
                }
                sysmem_freeptr(x->patterns[i].drumlines);
            }
        }
        sysmem_freeptr(x->patterns);
    }

    if (x->stored_patterns)    { sysmem_freeptr(x->stored_patterns); }
    if (x->current_increment)  { sysmem_freeptr(x->current_increment); }
    if (x->gtranspose)         { sysmem_freeptr(x->gtranspose); }
    if (x->gains)              { sysmem_freeptr(x->gains); }
    if (x->sequence)           { sysmem_freeptr(x->sequence); }
    if (x->listdata)           { sysmem_freeptr(x->listdata); }
    if (x->connected)          { sysmem_freeptr(x->connected); }
    if (x->tmpatks)            { sysmem_freeptr(x->tmpatks); }
    if (x->muted)              { sysmem_freeptr(x->muted); }
}

void dmach_mute(t_dmach *x, double toggle) { x->mute = (short)toggle; }
void dmach_clickincr(t_dmach *x, double toggle) { x->clickincr = (short)toggle; }

void dmach_assist(t_dmach *x, void *b, long msg, long arg, char *dst)
{
    if (msg == ASSIST_INLET) {
        strncpy_zero(dst, "(signal) Sync Click Input", 256);
    } else if (msg == ASSIST_OUTLET) {
        if (arg == x->outlet_count) {
            strncpy_zero(dst, "(list) Raw Pattern Data", 256);
        } else if (arg == x->outlet_count - 1) {
            strncpy_zero(dst, "(signal) Sync Trigger", 256);
        } else if (!(arg % 2)) {
            snprintf(dst, 256, "(signal) Trigger %ld", arg / 2 + 1);
        } else {
            snprintf(dst, 256, "(signal) Increment %ld", (arg - 1) / 2 + 1);
        }
    }
}

void *dmach_new(t_symbol *s, long argc, t_atom *argv)
{
    t_dmach *x = (t_dmach *)object_alloc(dmach_class);
    if (!x) return NULL;

    x->tempo = (argc > 0) ? (float)atom_getfloat(argv) : 120.0f;
    x->drum_count = (argc > 1) ? (int)atom_getfloat(argv + 1) : 8;
    if (x->drum_count < 1) x->drum_count = 1;

    x->outlet_count = x->drum_count * 2 + 1;

    x->listraw_outlet = listout((t_pxobject *)x);
    dsp_setup((t_pxobject *)x, 1);
    for (int i = 0; i < x->outlet_count; i++) {
        outlet_new((t_object *)x, "signal");
    }
    x->x_obj.z_misc |= Z_NO_INPLACE;

    x->patterns = (t_pattern *)sysmem_newptrclear(MAX_PATTERNS * sizeof(t_pattern));
    x->stored_patterns = (short *)sysmem_newptrclear(MAX_PATTERNS * sizeof(short));
    x->current_increment = (float *)sysmem_newptrclear(x->drum_count * sizeof(float));
    x->gtranspose = (float *)sysmem_newptrclear(x->drum_count * sizeof(float));
    x->gains = (float *)sysmem_newptrclear(x->drum_count * sizeof(float));
    x->sequence = (int *)sysmem_newptrclear(1024 * sizeof(int));
    x->listdata = (t_atom *)sysmem_newptrclear(1024 * sizeof(t_atom));
    x->connected = (short *)sysmem_newptrclear(1024 * sizeof(short));
    x->tmpatks = (t_attack *)sysmem_newptrclear(MAX_ATTACKS * sizeof(t_attack));
    x->muted = (short *)sysmem_newptrclear(x->drum_count * sizeof(short));

    x->seqptr = 0;
    x->sequence_length = 0;
    x->playsequence = 0;
    x->loopsequence = 0;
    x->zeroalias = -1.0f;
    x->clickincr = 0;

    if (x->tempo <= 0.0f || x->tempo > 6666.0f) x->tempo = 60.0f;

    for (int i = 0; i < MAX_PATTERNS; i++) {
        x->patterns[i].drumlines = NULL;
        x->stored_patterns[i] = 0;
    }
    x->this_pattern = x->next_pattern = 0;
    x->mute = 0;
    x->clocker = 0.0f;
    x->sr = sys_getsr();
    if (x->sr <= 0.0f) x->sr = 44100.0f;
    x->tempo_factor = 60.0f / x->tempo;

    for (int i = 0; i < x->drum_count; i++) {
        x->gains[i] = 1.0f;
        x->gtranspose[i] = 1.0f;
        x->current_increment[i] = 1.0f;
    }

    for (int i = 0; i < MAX_PATTERNS; i++) {
        dmach_init_pattern(x, i);
    }
    x->virgin = 1;

    return x;
}

void dmach_perform64(t_dmach *x, t_object *dsp64, double **ins, 
                     long numins, double **outs, long numouts, long n,
                     long flags, void *userparam)
{
    int i, j;
    t_double *trig_outlet, *incr_outlet;
    t_double *sync = outs[numouts - 1];

    int this_pattern = x->this_pattern;
    int next_pattern = x->next_pattern;
    t_pattern *p = x->patterns;
    float *current_increment = x->current_increment;
    float clocker = x->clocker;
    float dsamps = p[this_pattern].dsamps;
    int drum_count = x->drum_count;
    int adex;

    int *sequence = x->sequence;
    short playsequence = x->playsequence;
    short loopsequence = x->loopsequence;
    short clickincr = x->clickincr;
    int seqptr = x->seqptr;
    int sequence_length = x->sequence_length;
    short *connected = x->connected;
    short *muted = x->muted;
    float *gtranspose = x->gtranspose;
    float *gains = x->gains;

    memset((void *)sync, 0, n * sizeof(t_double));

    if (x->mute || x->virgin) {
        for (i = 0; i < drum_count; i++) {
            if (i * 2 < numouts && connected[i * 2]) {
                trig_outlet = outs[i * 2];
                memset((void *)trig_outlet, 0, n * sizeof(t_double));
            }
        }
        return;
    }

    for (i = 0; i < drum_count; i++) {
        if (i * 2 < numouts && connected[i * 2]) {
            trig_outlet = outs[i * 2];
            memset((void *)trig_outlet, 0, n * sizeof(t_double));
        }
    }

    for (j = 0; j < n; j++) {
        if (clocker >= dsamps) {
            clocker -= dsamps;
            if (playsequence) {
                if (seqptr >= sequence_length) {
                    if (loopsequence) {
                        seqptr = 0;
                    } else {
                        x->mute = 1;
                        goto escape;
                    }
                }
                this_pattern = sequence[seqptr++];
            } else if (next_pattern != this_pattern) {
                this_pattern = next_pattern;
            }

            for (i = 0; i < drum_count; i++) {
                p[this_pattern].drumlines[i].adex = 0;
            }

            if (playsequence) {
                sync[j] = (this_pattern != 0) ? (t_double)this_pattern : (t_double)x->zeroalias;
            } else {
                sync[j] = 1.0;
            }
        } else {
            sync[j] = 0.0;
        }

        for (i = 0; i < drum_count; i++) {
            if (p[this_pattern].drumlines[i].active && !muted[i]) {
                if (i * 2 >= numouts || (i * 2 + 1) >= numouts) continue;

                trig_outlet = outs[i * 2];
                incr_outlet = outs[i * 2 + 1];
                adex = p[this_pattern].drumlines[i].adex;

                if (adex < p[this_pattern].drumlines[i].attack_count &&
                    (int)clocker == (int)p[this_pattern].drumlines[i].attacks[adex].trigger_point) {
                    
                    current_increment[i] = p[this_pattern].drumlines[i].attacks[adex].increment * gtranspose[i];
                    trig_outlet[j] = (t_double)(p[this_pattern].drumlines[i].attacks[adex].amplitude * gains[i]);
                    p[this_pattern].drumlines[i].adex = ++adex;
                    incr_outlet[j] = (t_double)current_increment[i];
                } else {
                    trig_outlet[j] = 0.0;
                    if (clickincr) {
                        incr_outlet[j] = 0.0;
                    } else {
                        incr_outlet[j] = (t_double)current_increment[i];
                    }
                }
            }
        }
        ++clocker;
    }

escape:
    x->clocker = clocker;
    x->this_pattern = this_pattern;
    x->seqptr = seqptr;
}

void dmach_dsp64(t_dmach *x, t_object *dsp64, short *count, double samplerate, long n, long flags)
{
    if (samplerate <= 0.0) return;
    for (int i = 0; i < x->outlet_count + 1 && i < 1024; i++) {
        x->connected[i] = count[i];
    }
    dsp_add64(dsp64, (t_object *)x, (t_perfroutine64)dmach_perform64, 0, NULL);
}