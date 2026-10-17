/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../shortcut.h"

static int checks, mock_view = AZ_VIEW_DECK, banners, fallbacks, registrations;
#define pending() __atomic_load_n(&gp_shortcut_request_word, __ATOMIC_ACQUIRE)
#define sent() ((__atomic_load_n(&gp_shortcut_completion, __ATOMIC_ACQUIRE) & 3) == GP_SHORTCUT_SENT)
#define CHECK(x) do { assert(x); checks++; } while (0)
static int view(void) { return mock_view; }
static void banner(const char *id, const char *text, int ms)
{
    CHECK(!strcmp(id, "mods-shortcut"));
    if (!strcmp(text, "MODS timed out. Use SOURCE > MOD MENU")) { CHECK(ms == 3000); fallbacks++; }
    else { CHECK(!strcmp(text, "Tap MOD MENU in the source list")); CHECK(ms == 2200); banners++; }
}
static int register_filter(const char *id, unsigned mask, az_frame_fn fn, void *user)
{
    CHECK(!strcmp(id, "mods-shortcut"));
    CHECK(mask == (1u << AZ_BUS_MIXER_IN));
    CHECK(fn == gp_shortcut_frame && user == NULL);
    registrations++;
    return 0;
}
static const az_core_v1 core = {
    .size = sizeof(az_core_v1), .view = view,
    .frame_filter = register_filter, .banner = banner,
};
static void reset(void)
{
    gp_shortcut_request_word = gp_shortcut_completion = 0;
    gp_shortcut_pulse_token = gp_shortcut_until = gp_shortcut_notice_token = 0;
    gp_shortcut_notice = gp_shortcut_knob_known = 0;
    gp_shortcut_last_request = -10000;
    mock_view = AZ_VIEW_DECK;
    banners = fallbacks = 0;
}
static void idle(unsigned char *f)
{
    for (int i = 0; i < 152; i++) f[i] = (unsigned char)(i * 17 + 3);
    f[6] = 0x28; f[31] = 0; f[32] = f[33] = 0;
}
static void unchanged(unsigned char *f, const unsigned char *before)
{
    CHECK(!memcmp(f, before, 152));
}
int main(void)
{
    unsigned char f[152], before[152];
    CHECK(!gp_shortcut_init(NULL, "mods-shortcut"));
    az_core_v1 missing = core;
    missing.frame_filter = NULL;
    CHECK(!gp_shortcut_init(&missing, "mods-shortcut"));
    CHECK(gp_shortcut_init(&core, "mods-shortcut"));
    CHECK(gp_shortcut_init(&core, "mods-shortcut"));
    CHECK(registrations == 1);
    reset(); idle(f); memcpy(before, f, 152);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 100); unchanged(f, before);
    CHECK(gp_shortcut_request(100)); CHECK(!gp_shortcut_request(101));
    gp_shortcut_frame_at(AZ_BUS_DECK_IN, f, 152, 110); unchanged(f, before);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 97, 110); unchanged(f, before);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, NULL, 152, 110);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 98, 120);
    CHECK(f[32] == 0x80 && f[6] == 0x20);
    for (int i = 0; i < 152; i++) if (i != 6 && i != 32) CHECK(f[i] == before[i]);
    idle(f); gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 319); CHECK(f[32] == 0x80);
    idle(f); gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 320); unchanged(f, before);
    CHECK(!pending()); CHECK(!gp_shortcut_request(321));
    gp_shortcut_tick_at(321); CHECK(!banners); /* Must see SOURCE before claiming arrival. */
    mock_view = AZ_VIEW_SOURCE;
    gp_shortcut_tick_at(400); CHECK(banners == 1);
    gp_shortcut_tick_at(401); CHECK(banners == 1);
    CHECK(!gp_shortcut_request(700)); /* SOURCE-on-SOURCE would switch to BROWSE. */
    mock_view = AZ_VIEW_DECK; CHECK(gp_shortcut_request(700));

    /* Every native screen, LOAD and BACK bit cancels a pending pulse unchanged. */
    for (int bit = 0; bit < 16; bit++) {
        reset(); CHECK(gp_shortcut_request(100)); idle(f);
        f[32 + bit / 8] = (unsigned char)(1 << (bit % 8)); memcpy(before, f, 152);
        gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 101); unchanged(f, before);
        CHECK(!pending() && !sent());
    }
    reset(); CHECK(gp_shortcut_request(100)); idle(f); f[31] = 1; memcpy(before, f, 152);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 101); unchanged(f, before);
    CHECK(!pending());

    /* Native input also wins after the synthetic button has already gone down. */
    reset(); CHECK(gp_shortcut_request(100)); idle(f);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 101); CHECK(f[32] == 0x80);
    idle(f); f[33] = 1; memcpy(before, f, 152);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 102); unchanged(f, before);
    CHECK(!pending() && !sent());
    mock_view = AZ_VIEW_SOURCE; gp_shortcut_tick_at(103); CHECK(!banners);

    reset(); idle(f); gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 90);
    CHECK(gp_shortcut_request(100)); idle(f); f[34]++; memcpy(before, f, 152);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 101); unchanged(f, before);
    CHECK(!pending());

    /* Delayed requests, reversed clocks and unrelated navigation do nothing. */
    long bad_times[] = {99, 1101};
    for (unsigned i = 0; i < sizeof bad_times / sizeof *bad_times; i++) {
        reset(); CHECK(gp_shortcut_request(100)); idle(f); memcpy(before, f, 152);
        gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, bad_times[i]); unchanged(f, before);
        CHECK(!pending());
    }
    int other_views[] = {AZ_VIEW_BROWSE, AZ_VIEW_SEARCH, AZ_VIEW_OTHER, AZ_VIEW_MENU};
    for (unsigned i = 0; i < sizeof other_views / sizeof *other_views; i++) {
        reset(); CHECK(gp_shortcut_request(100)); idle(f);
        gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 101);
        idle(f); memcpy(before, f, 152); mock_view = other_views[i];
        gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 102); unchanged(f, before);
        CHECK(!pending() && !sent());
    }
    reset(); CHECK(gp_shortcut_request(100)); idle(f);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 101);
    mock_view = AZ_VIEW_SOURCE; idle(f); memcpy(before, f, 152);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 102); unchanged(f, before);
    CHECK(!pending() && sent());
    gp_shortcut_tick_at(103); CHECK(banners == 1);

    reset(); CHECK(!gp_shortcut_request(-1)); CHECK(gp_shortcut_request(100));
    idle(f); gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 101);
    mock_view = AZ_VIEW_SOURCE; gp_shortcut_tick_at(1601); CHECK(!banners && fallbacks == 1 && !pending());
    /* A disabled filter or absent mixer callbacks cannot strand pending forever. */
    reset(); CHECK(gp_shortcut_request(100));
    gp_shortcut_tick_at(1100); CHECK(pending() && !fallbacks);
    gp_shortcut_tick_at(1101); CHECK(!pending() && fallbacks == 1);
    gp_shortcut_tick_at(1102); CHECK(fallbacks == 1);
    CHECK(!gp_shortcut_request(99)); CHECK(!gp_shortcut_request(100));
    CHECK(gp_shortcut_request(1200)); idle(f); memcpy(before, f, 152);
    gp_shortcut_tick_at(2201); CHECK(!pending() && fallbacks == 2);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 2202); unchanged(f, before);
    /* If SOURCE was reached but no later frame arrives, cleanup still runs. */
    reset(); CHECK(gp_shortcut_request(100)); idle(f);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 101);
    mock_view = AZ_VIEW_SOURCE; gp_shortcut_tick_at(102); CHECK(banners == 1);
    gp_shortcut_tick_at(1101); CHECK(!pending() && !fallbacks);
    /* SOURCE completion observed in the same tick as expiry wins over the
     * initial expired-pending snapshot, within the original notice window. */
    reset(); CHECK(gp_shortcut_request(100)); idle(f);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 101);
    mock_view = AZ_VIEW_SOURCE; gp_shortcut_tick_at(1101);
    CHECK(!pending() && banners == 1 && !fallbacks);
    gp_shortcut_tick_at(1102); CHECK(banners == 1 && !fallbacks);
    /* New request cannot extend a half-sent old pulse: one native release first. */
    reset(); CHECK(gp_shortcut_request(100)); idle(f);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 1090); CHECK(f[32] == 0x80);
    gp_shortcut_tick_at(1101); CHECK(!pending()); CHECK(gp_shortcut_request(1200));
    idle(f); memcpy(before, f, 152);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 1201); unchanged(f, before);
    gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 1202); CHECK(f[32] == 0x80);
    idle(f); gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 1401); CHECK(f[32] == 0x80);
    idle(f); gp_shortcut_frame_at(AZ_BUS_MIXER_IN, f, 152, 1402); unchanged(f, before);
    reset(); CHECK(!gp_shortcut_request(LONG_MAX));
    gp_shortcut_tick(); /* Public UI wrapper, without pending feedback. */
    CHECK(gp_shortcut_clock_ms() >= 0);
    printf("PASS: %d MODS shortcut assertions\n", checks);
    return 0;
}
