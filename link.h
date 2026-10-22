/* SPDX-License-Identifier: GPL-3.0-only
 *
 * flow-beat's Ableton Link (link.cpp): the deck leads a Link session, so Ableton, AUTO/PILOT,
 * Traktor or anything with Link follows the lead deck's tempo, beat and bar without a computer
 * in between. C interface, called from flow-beat.c's worker thread only.
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

#ifdef __cplusplus
}
#endif
#endif
