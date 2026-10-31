/* SPDX-License-Identifier: GPL-3.0-only
 *
 * flow-beat's Ableton Link (link.cpp): the deck leads a Link session, so Ableton, AUTO/PILOT,
 * Traktor or anything with Link follows the lead deck's tempo, beat and bar without a computer
 * in between. With Link Audio on, each deck's own sound is also offered to the session as a
 * channel ("Deck 1".."Deck 4"). C interface, called from flow-beat.c's worker thread only,
 * except flowlink_audio_send (the deck's audio taps).
 */
#ifndef FLOW_LINK_H
#define FLOW_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

/* What the lead deck is doing right now. */
typedef struct {
    int    playing;     /* 1: a deck is playing and leads */
    double bpm;         /* its current tempo (pitch included) */
    double ph;          /* position in the current beat, 0..1; -1 = no grid */
    int    bar_beat;    /* which beat of the bar the current beat is: 0 = the "1" .. 3; -1 unknown */
    int    new_lead;    /* 1: the lead deck or its track just changed: line up at once */
    double ahead_ms;    /* how far the deck's sound runs behind its playhead (output latency) */
} flowlink_in;

/* What one step did, for the log (each field 1 once per event). */
typedef struct {
    int forced;         /* Link's beat or bar was moved onto the deck's */
    int tempo;          /* Link's tempo was set */
    double err;         /* the beat error before the step, in beats */
} flowlink_out;

/* Turns Link on (made once, on the first call) or off. 0 when done, -1 when Link can't start. */
int  flowlink_enable(int on);
/* One step: follow the lead deck. Never blocks; catches everything. */
void flowlink_step(const flowlink_in *in, flowlink_out *out);
/* Peers in the session (not counting the deck), or -1 while Link is off. */
int  flowlink_peers(void);

/* Link Audio (needs flowlink_enable(1) first): offers each deck's sound as a channel, or stops.
 * rate forces the taps' sample rate in Hz; 0 = measure each tap and send at the standard rate
 * it is (44100, 48000, ...); extra_ms is added to each block's time on Link's
 * timeline (on top of ahead_ms). 0 when done, -1 when it can't start. */
int  flowlink_audio_enable(int on, unsigned rate, double extra_ms);
/* From deck d's audio tap (0..3): interleaved stereo float, -1..1. Sends only while Link
 * Audio is on and a peer listens to that deck; never blocks for long; catches everything. */
void flowlink_audio_send(int d, const float *lr, int frames);
/* Samples a second deck d's tap has delivered since Link Audio came on (0 = not yet known). */
double flowlink_audio_rate_seen(int d);
/* The sample rate deck d's blocks are sent at (0 = not known yet). */
unsigned flowlink_audio_rate_used(int d);
/* Blocks deck d has sent to a listening peer since Link Audio came on. */
unsigned long flowlink_audio_sent(int d);

#ifdef __cplusplus
}
#endif
#endif
