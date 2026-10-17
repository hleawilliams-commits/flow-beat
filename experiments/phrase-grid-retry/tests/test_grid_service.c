/* SPDX-License-Identifier: GPL-3.0-only
 * The service, seqlock publisher, SDK phase reader and Grouped Panels follower
 * below are extracted from production files, not reimplemented in this test.
 * Only native-grid availability and the optional settings-file read are faked. */
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "azmod-mod.h"
#define NDECK 4
#define DRUM_SIDES 4
#define DRUM_SR 44100
static _Atomic unsigned g_deck_trackgen[NDECK];
static _Atomic long long g_play_obs[NDECK];
static int native_ready[4], calls[4], logs, checks, settings_reads;
static long long anchors[4];
static double periods[4];
#define CHECK(ok) do { checks++; if (!(ok)) { fprintf(stderr, "FAIL line %d: %s\n",__LINE__,#ok); return 1; } } while(0)
static FILE *settings_fopen(const char *path,const char *mode)
{
    (void)path;(void)mode;settings_reads++;return NULL;
}
static void logln(const char *fmt,...)
{
    (void)fmt;logs++;
}
static int drum_grid_native(int deck,long long *anchor,double *beat)
{
    calls[deck]++;if(!native_ready[deck])return 0;
    *anchor=anchors[deck];*beat=periods[deck];return 1;
}
#define fopen settings_fopen
#include GRID_INC
#undef fopen
#include FOLLOW_INC

static void reset(void)
{
    for(int d=0;d<4;d++) {
        atomic_store(&g_deck_trackgen[d],7u);
        atomic_store(&g_play_obs[d],100000);
        atomic_store(&g_drum_grid[d].seq,0);
        atomic_store(&g_drum_grid[d].valid,0);
        atomic_store(&g_drum_grid[d].anchor_src,0);
        atomic_store(&g_drum_grid[d].beat_src,0);
        g_drum_grid_seen[d]=0;native_ready[d]=0;calls[d]=0;
        anchors[d]=1000*(d+1);periods[d]=22050.0+1000*d;
#ifdef DRUM_GRID_RETRY_TICKS
        g_drum_grid_retry[d]=0;
#endif
    }
    g_drum_grid_init=0;logs=settings_reads=0;
    atomic_store(&g_grid_demand,0);atomic_store(&g_drum_sync_on,0);
}
static int no_grid(int deck) { double phase=-1;return azstem_deck_beat_phase(deck,&phase)!=0; }
static void steps(int n) { while(n--)drum_grid_service(); }
static az_deck_info track_info(int deck)
{
    az_deck_info di={0};di.size=sizeof di;di.loaded=1;di.playing=1;
    di.content_id=100u+(unsigned)deck;di.bpm=120;di.pos_s=2.0;return di;
}
int main(int argc,char **argv)
{
    int baseline=argc>1 && !strcmp(argv[1],"baseline");
    reset();CHECK(no_grid(1)); /* SDK request activates grid demand. */
    CHECK(atomic_load(&g_grid_demand)==1);
    drum_grid_service();CHECK(calls[1]==1 && no_grid(1));
    native_ready[1]=1;steps(20);
    struct gp_follow follower={0};az_deck_info di=track_info(1);double phase=-1;
    int grid_ok=azstem_deck_beat_phase(1,&phase)==0;
    gp_follow_step(&follower,&di,7,grid_ok,phase,0);
    if(baseline) {
        CHECK(calls[1]==1 && !grid_ok && !follower.known);
        printf("BASELINE REPRODUCED: native Deck 2 grid arrives after first read; 20 later services never retry, SDK phase stays unavailable and Grouped Panels cannot anchor (%d checks)\n",checks);
        return 0;
    }
    CHECK(grid_ok && follower.known && calls[1]==2);

    /* Each logical deck independently recovers, with existing good decks untouched. */
    for(int target=0;target<4;target++) {
        reset();for(int d=0;d<4;d++)native_ready[d]=(d!=target);
        CHECK(no_grid(target));drum_grid_service();
        CHECK(no_grid(target) && calls[target]==1);
        unsigned seq[4];for(int d=0;d<4;d++)seq[d]=atomic_load(&g_drum_grid[d].seq);
        native_ready[target]=1;
        for(int i=0;i<4;i++) {drum_grid_service();CHECK(calls[target]==1 && no_grid(target));}
        drum_grid_service();CHECK(calls[target]==2 && !no_grid(target));
        for(int d=0;d<4;d++)if(d!=target) {
            CHECK(calls[d]==1 && atomic_load(&g_drum_grid[d].seq)==seq[d]);
            double ph=-1;CHECK(azstem_deck_beat_phase(d,&ph)==0);
            double expected=fmod(100000.0-anchors[d],periods[d])/periods[d];
            CHECK(fabs(ph-expected)<1e-12);
        }
        steps(25);CHECK(calls[target]==2); /* Success stops polling. */
        memset(&follower,0,sizeof follower);di=track_info(target);
        phase=-1;grid_ok=azstem_deck_beat_phase(target,&phase)==0;
        gp_follow_step(&follower,&di,7,grid_ok,phase,0);CHECK(follower.known);
        /* Track change invalidates old grid immediately, regardless of cooldown. */
        atomic_fetch_add(&g_deck_trackgen[target],1u);native_ready[target]=0;
        drum_grid_service();CHECK(no_grid(target) && calls[target]==3);
        CHECK(atomic_load(&g_drum_grid[target].anchor_src)==0);
        gp_follow_step(&follower,&di,8,0,-1,0);CHECK(!follower.known);
        anchors[target]=2222;periods[target]=30000;native_ready[target]=1;
        steps(5);CHECK(!no_grid(target) && calls[target]==4);
        CHECK(atomic_load(&g_drum_grid[target].anchor_src)==2222);
    }
    /* Empty or permanently invalid grids remain unavailable, with bounded retries
     * and only one generic unavailable log per generation. */
    reset();CHECK(no_grid(0));drum_grid_service();steps(100);
    for(int d=0;d<4;d++)CHECK(calls[d]==21 && no_grid(d));
    CHECK(logs==4 && settings_reads==101);
    /* Disabling demand/sync suspends native reads; new track resumes immediately. */
    atomic_store(&g_grid_demand,0);atomic_store(&g_drum_sync_on,0);
    steps(100);for(int d=0;d<4;d++)CHECK(calls[d]==21);
    atomic_fetch_add(&g_deck_trackgen[2],1);native_ready[2]=1;CHECK(no_grid(2));
    drum_grid_service();CHECK(calls[2]==22 && !no_grid(2));
    /* Beat Sync alone can request grids; no community mod is required. */
    reset();atomic_store(&g_drum_sync_on,1);native_ready[3]=1;
    drum_grid_service();CHECK(calls[3]==1 && !no_grid(3));
    /* Generation wrap keeps an equal-generation retry pending and a new track immediate. */
    reset();atomic_store(&g_deck_trackgen[0],UINT32_MAX);CHECK(no_grid(0));
    drum_grid_service();CHECK(calls[0]==1);atomic_fetch_add(&g_deck_trackgen[0],1u);
    native_ready[0]=1;drum_grid_service();CHECK(calls[0]==2 && !no_grid(0));
    printf("PATCH PASSED: %d assertions; late grids on all four decks recover, SDK phases and Grouped Panels anchor, good decks stay cached, stale grids invalidate, retry cadence/logging/demand remain bounded\n",checks);
    return 0;
}
