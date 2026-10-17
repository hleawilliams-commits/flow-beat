/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "shortcut-diagnostic.h"
static int checks, current_view = AZ_VIEW_DECK, banner_count;
static char last_banner[128];
static az_frame_fn registered;
#define CHECK(x) do { assert(x); checks++; } while (0)
static int view(void) { return current_view; }
static void banner(const char *id, const char *text, int ms)
{
    CHECK(!strcmp(id,"mods-shortcut")); CHECK(ms == 3500 || ms == 2200);
    snprintf(last_banner, sizeof last_banner, "%s", text); banner_count++;
}
static int filter(const char *id, unsigned mask, az_frame_fn fn, void *user)
{
    CHECK(!strcmp(id,"mods-shortcut")); CHECK(mask == (1u << AZ_BUS_MIXER_IN));
    CHECK(user == NULL); registered = fn; return 0;
}
static const az_core_v1 core = {.size=sizeof(az_core_v1), .view=view, .frame_filter=filter, .banner=banner};
static void reset(void)
{
    gp_shortcut_phase = gp_shortcut_sent = gp_shortcut_pending = gp_shortcut_knob_known = 0;
    gp_shortcut_last_request = -10000; gp_shortcut_until = 0;
    gp_shortcut_notice = gp_shortcut_wait_notice = 0;
    gp_shortcut_cancel_reason = GP_CANCEL_NONE; current_view = AZ_VIEW_DECK;
    last_banner[0] = 0; banner_count = 0;
}
static void idle(unsigned char f[98])
{
    memset(f,0,98); f[0]=3; f[6]=8; f[34]=128; f[35]=17;
}
static void cancelled(int reason, unsigned char f[98], long at, const char *message)
{
    unsigned char before[98]; memcpy(before,f,98);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN,f,98,at);
    CHECK(!memcmp(f,before,98)); CHECK(!gp_shortcut_pending && !gp_shortcut_sent);
    CHECK(gp_shortcut_cancel_reason == reason); CHECK(gp_shortcut_saw_frame);
    CHECK(gp_shortcut_observed_delay == at-100);
    CHECK(gp_shortcut_observed_view == current_view);
    CHECK((gp_shortcut_observed_input & 0xff) == before[6]);
    CHECK(((gp_shortcut_observed_input >> 8) & 0xff) == before[31]);
    CHECK(((gp_shortcut_observed_input >> 16) & 0xff) == before[32]);
    CHECK(((gp_shortcut_observed_input >> 24) & 0xff) == before[33]);
    CHECK(((gp_shortcut_observed_input >> 32) & 0xff) == before[34]);
    CHECK(((gp_shortcut_observed_input >> 40) & 0xff) == before[35]);
    CHECK(!banner_count); /* The real-time callback never calls a UI service. */
    gp_shortcut_tick_at(at); CHECK(banner_count==1); CHECK(!strcmp(last_banner,message));
    gp_shortcut_tick_at(at+1); CHECK(banner_count==1);
}
int main(void)
{
    unsigned char f[98]; CHECK(gp_shortcut_init(&core,"mods-shortcut")); CHECK(registered);
    /* Native inputs must remain byte-for-byte intact, including LOAD. */
    reset(); CHECK(gp_shortcut_request(100)); idle(f); f[32]=0x40;
    cancelled(GP_CANCEL_SCREEN,f,101,"MODS: screen button active");
    reset(); CHECK(gp_shortcut_request(100)); idle(f); f[33]=1;
    cancelled(GP_CANCEL_CORNER,f,101,"MODS: LOAD/BACK button active");
    reset(); CHECK(gp_shortcut_request(100)); idle(f); f[31]=1;
    cancelled(GP_CANCEL_CLICK,f,101,"MODS: knob pressed");
    reset(); idle(f); gp_shortcut_frame_at(AZ_BUS_MIXER_IN,f,98,90);
    CHECK(gp_shortcut_request(100)); f[34]++;
    cancelled(GP_CANCEL_KNOB,f,101,"MODS: knob moved");
    reset(); CHECK(gp_shortcut_request(100)); idle(f);
    cancelled(GP_CANCEL_CLOCK,f,99,"MODS: clock unavailable");
    reset(); CHECK(gp_shortcut_request(100)); idle(f);
    cancelled(GP_CANCEL_EXPIRED,f,1101,"MODS: input arrived too late");
    reset(); CHECK(gp_shortcut_request(100)); idle(f); current_view=AZ_VIEW_OTHER;
    cancelled(GP_CANCEL_VIEW,f,101,"MODS: deck view changed");
    /* A SOURCE transition before any pulse isn't counted as delivery. */
    reset(); CHECK(gp_shortcut_request(100)); idle(f); current_view=AZ_VIEW_SOURCE;
    cancelled(GP_CANCEL_VIEW,f,101,"MODS: deck view changed");
    /* No mixer frames after touch is a different observation from cancellation. */
    reset(); CHECK(gp_shortcut_request(100)); gp_shortcut_tick_at(1100); CHECK(!banner_count);
    gp_shortcut_tick_at(1101); CHECK(banner_count==1);
    CHECK(!strcmp(last_banner,"MODS: waiting for mixer input"));
    CHECK(gp_shortcut_pending && !gp_shortcut_saw_frame && !gp_shortcut_cancel_reason);
    gp_shortcut_tick_at(1102); CHECK(banner_count==1);
    idle(f); gp_shortcut_frame_at(AZ_BUS_MIXER_IN,f,98,1103);
    CHECK(!gp_shortcut_pending && !f[32] && f[6]==8);
    gp_shortcut_tick_at(1103); CHECK(banner_count==2);
    /* Successful SOURCE still uses the existing bound and success message. */
    reset(); CHECK(gp_shortcut_request(100)); idle(f);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN,f,98,101); CHECK(f[32]==128 && !f[6]);
    CHECK(!gp_shortcut_cancel_reason && gp_shortcut_until==301);
    idle(f); current_view=AZ_VIEW_SOURCE; gp_shortcut_frame_at(AZ_BUS_MIXER_IN,f,98,102);
    CHECK(!gp_shortcut_pending && gp_shortcut_sent && !gp_shortcut_cancel_reason);
    gp_shortcut_tick_at(102); CHECK(!strcmp(last_banner,"Tap MOD MENU in the source list"));
    /* The real emulator showed SOURCE while SDK view transiently said BROWSE.
     * An injected press followed by a view transition is not proof of failure. */
    reset(); CHECK(gp_shortcut_request(100)); idle(f);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN,f,98,101); CHECK(f[32]==128);
    current_view=AZ_VIEW_BROWSE; idle(f);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN,f,98,102);
    CHECK(gp_shortcut_cancel_reason==GP_CANCEL_VIEW_AFTER_SEND);
    gp_shortcut_tick_at(102); CHECK(!strcmp(last_banner,"MODS: SOURCE press sent"));
    gp_shortcut_tick(); CHECK(gp_shortcut_clock_ms()>=0);
    printf("PASS: %d diagnostic assertions\n",checks);
}
