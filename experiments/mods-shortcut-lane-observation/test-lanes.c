/* SPDX-License-Identifier: GPL-3.0-only
 * Serial schedules of UI observations between actual SDK filter calls.
 * This is a diagnostic counterexample, not a timing/concurrency benchmark. */
#include <stdarg.h>
#include <time.h>

static unsigned checks, failures, tick_calls, mod_off_logs, core_off_logs, moves;
static int split_schedule, charge_first;
static uint64_t mock_tick;
static const char *scenario;
#define CHECK(x, name) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL %s/%s\n", scenario, name); } } while (0)

static int fake_gettime(clockid_t clock, struct timespec *t) {
    (void)clock; t->tv_sec=1; t->tv_nsec=0; return 0;
}
#define GP_SHORTCUT_GETTIME fake_gettime
#include "shortcut.h"

static int fake_view(void) { return AZ_VIEW_DECK; }
static void mod_log(const char *id, const char *fmt, ...) {
    (void)id;
    if (!strcmp(fmt, "MODS: lane %d switched off by the core")) mod_off_logs++;
    if (!strcmp(fmt, "MODS: moved to lane %d of %d")) moves++;
}
static void cm_logf(cm_mod *c, int force, const char *fmt, ...) {
    (void)c; (void)force;
    if (strstr(fmt, "switched off for this session")) core_off_logs++;
}
static uint64_t sdk_ticks(void) {
    tick_calls++;
    /* The previous filter's end sample already returned. Advance fake time
     * BEFORE taking the next filter's start sample; no callback is charged
     * for the modeled gap between calls. Production bodies are unchanged. */
    if (split_schedule && (tick_calls==3 || tick_calls==5)) {
        long now = tick_calls==3 ? 1000 : 1250;
        mock_tick += 250000;
        gp_shortcut_watch_at_time(now);
    }
    if (charge_first && tick_calls==2) mock_tick += 201;
    return mock_tick;
}

static const az_core_v1 core = {
    .size=sizeof(az_core_v1), .view=fake_view,
    .frame_filter=cm_frame_filter, .log=mod_log,
};
static void reset(void) {
    memset(g_sdk_ff, 0, sizeof g_sdk_ff);
    g_sdk_ff_n=0; g_sdk_ff_bus=0; g_cm_in_frame=0;
    g_sdk_budget_ticks=200; g_sdk_in_init=1; owner.state=AZCM_LOADED;
    gp_shortcut_ready=0;
    gp_shortcut_request_word=gp_shortcut_completion=0;
    gp_shortcut_pulse_token=gp_shortcut_until=0;
    gp_shortcut_notice=gp_shortcut_knob_known=0;
    gp_shortcut_last_request=-10000;
    memset(gp_shortcut_beat, 0, sizeof gp_shortcut_beat);
    memset(gp_shortcut_seen, 0, sizeof gp_shortcut_seen);
    memset(gp_shortcut_stall, 0, sizeof gp_shortcut_stall);
    memset(gp_shortcut_off, 0, sizeof gp_shortcut_off);
    gp_shortcut_all_off=0;
    gp_shortcut_watch_at=gp_shortcut_quiet_since=-1;
    tick_calls=mod_off_logs=core_off_logs=moves=0;
    split_schedule=charge_first=0; mock_tick=0;
    CHECK(gp_shortcut_init(&core, "mods-shortcut"), "real-mod-init");
    CHECK(g_sdk_ff_n==4 && gp_shortcut_lanes==4, "actual-sdk-registration");
    g_sdk_in_init=0;
}
static int healthy(void) {
    for (int i=0;i<g_sdk_ff_n;i++)
        if (g_sdk_ff[i].off || g_sdk_ff[i].strikes ||
            g_sdk_ff[i].c->state!=AZCM_LOADED) return 0;
    return 1;
}
static int local_off(void) {
    int n=0; for (int i=0;i<4;i++) n+=gp_shortcut_off[i]!=0; return n;
}
static int same_beats(unsigned n) {
    for (int i=0;i<4;i++) if (gp_shortcut_beat[i]!=n) return 0;
    return 1;
}
static void frame(void) {
    unsigned char f[98]={0}, before[98]; f[34]=128;
    memcpy(before, f, sizeof f); tick_calls=0;
    CHECK(sdk_filter_run(AZ_BUS_MIXER_IN,f,sizeof f)==1, "editing-chain-ran");
    CHECK(!memcmp(f,before,sizeof f), "idle-input-unchanged");
    CHECK(!g_cm_in_frame, "sdk-context-cleared");
}
static void whole_frame(void) {
    scenario="whole-frame"; reset();
    frame(); gp_shortcut_watch_at_time(1000);
    frame(); gp_shortcut_watch_at_time(1250);
    frame(); gp_shortcut_watch_at_time(1500);
    CHECK(healthy() && same_beats(3), "all-healthy-delivered");
    CHECK(!local_off() && !mod_off_logs && !moves, "no-false-off");
    CHECK(gp_shortcut_lane==0 && gp_shortcut_running(), "original-lane-running");
}
static void split_frame(void) {
    scenario="split-frame"; reset(); split_schedule=1;
    frame(); split_schedule=0; gp_shortcut_watch_at_time(1500);
    CHECK(tick_calls==8 && same_beats(1), "one-call-per-lane");
    CHECK(healthy() && !core_off_logs, "zero-sdk-strikes-and-off");
    CHECK(gp_shortcut_off[0] && local_off()==1, "healthy-lane-marked-off");
    CHECK(mod_off_logs==3, "three-false-core-off-diagnostics");
    CHECK(gp_shortcut_lane==1 && moves==1, "healthy-active-lane-replaced");
#ifdef ASSERT_AUTHORITATIVE
    CHECK(!healthy() || !local_off(), "local-off-implies-core-quarantine");
#endif
    frame(); gp_shortcut_watch_at_time(1750);
    CHECK(healthy() && same_beats(2), "same-filters-deliver-again");
    CHECK(!local_off() && gp_shortcut_running(), "off-label-cleared-without-restart");
    CHECK(gp_shortcut_lane==1 && moves==1, "replacement-persists");
}
static void no_delivery(void) {
    scenario="no-delivery"; reset();
    gp_shortcut_watch_at_time(1000); gp_shortcut_watch_at_time(3000);
    CHECK(healthy() && same_beats(0), "healthy-but-no-dispatch");
    CHECK(gp_shortcut_all_off && !gp_shortcut_running(), "quiet-reports-all-off");
    frame(); gp_shortcut_watch_at_time(3250);
    CHECK(healthy() && !gp_shortcut_all_off && gp_shortcut_running(), "delivery-clears-without-restart");
}
static void actual_quarantine(void) {
    scenario="actual-quarantine"; reset(); charge_first=1;
    /* Actual pinned strike function quarantines lane 1 at eight measured
     * mock overruns. This is not a write to a live or emulated off flag. */
    for (unsigned i=1;i<=8;i++) {
        frame(); gp_shortcut_watch_at_time(1000+(long)i*250);
        CHECK(g_sdk_ff[0].strikes==i && g_sdk_ff[0].off==(i==8), "real-strike-threshold");
    }
    charge_first=0;
    frame(); gp_shortcut_watch_at_time(3250);
    CHECK(!gp_shortcut_off[0] && gp_shortcut_lane==0, "first-missed-look");
    frame(); gp_shortcut_watch_at_time(3500);
    CHECK(g_sdk_ff[0].off && g_sdk_ff[0].strikes==8 && core_off_logs==1, "actual-quarantine-retained");
    CHECK(gp_shortcut_off[0] && gp_shortcut_lane==1 && moves==1, "delivery-inference-matches-control");
    CHECK(mod_off_logs==1 && gp_shortcut_beat[0]==8 && gp_shortcut_beat[1]==10, "quarantined-callback-skipped");
}
int main(void) {
    whole_frame(); split_frame(); no_delivery(); actual_quarantine();
    printf("checks=%u failures=%u scenarios=4\n",checks,failures);
    return failures?1:0;
}
