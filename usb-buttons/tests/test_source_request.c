/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../source_request.h"

static int checks, mock_view = AZ_VIEW_DECK, registrations, registration_fails;
#define CHECK(x) do { assert(x); checks++; } while (0)
static int view(void) { return mock_view; }
static int register_filter(const char *id, unsigned mask, az_frame_fn fn, void *user)
{
    CHECK(!strcmp(id, "usb-buttons"));
    CHECK(mask == (1u << AZ_BUS_MIXER_IN));
    CHECK(fn == usb_source_frame && user == NULL);
    registrations++;
    return registration_fails;
}
static const az_core_v1 core = {
    .size = sizeof(az_core_v1), .view = view, .frame_filter = register_filter,
};
static void reset(void)
{
    usb_source_state = USB_SOURCE_IDLE;
    usb_source_seen = usb_source_had_request = usb_source_phase = 0;
    usb_source_requested_at = usb_source_last_request = usb_source_pressed_at = 0;
    usb_source_knob_known = 0;
    mock_view = AZ_VIEW_DECK;
}
static void idle(unsigned char *f)
{
    for (int i = 0; i < 152; i++) f[i] = (unsigned char)(i * 17 + 3);
    f[6] = 0x28;
    f[31] = f[32] = f[33] = 0;
}
static void unchanged(const unsigned char *f, const unsigned char *before)
{
    CHECK(!memcmp(f, before, 152));
}
static void frame(unsigned char *f, long now)
{
    CHECK(usb_source_frame_at(AZ_BUS_MIXER_IN, f, 152, now) == AZ_FRAME_OK);
}
static void begin_and_release(unsigned char *f)
{
    CHECK(usb_source_request(100));
    idle(f); frame(f, 101); CHECK(f[32] == 0x80);
    idle(f); frame(f, 301); CHECK(f[32] == 0);
    CHECK(usb_source_tick(302) == USB_SOURCE_SENT);
}
int main(void)
{
    unsigned char f[152], before[152];
    CHECK(usb_source_tick(0) == USB_SOURCE_IDLE);
    CHECK(!usb_source_init(NULL, "usb-buttons"));
    CHECK(!usb_source_init(&core, NULL));
    CHECK(!usb_source_init(&core, ""));
    az_core_v1 missing = core;
    missing.frame_filter = NULL;
    CHECK(!usb_source_init(&missing, "usb-buttons"));
    missing = core; missing.view = NULL;
    CHECK(!usb_source_init(&missing, "usb-buttons"));
    registration_fails = 1;
    CHECK(!usb_source_init(&core, "usb-buttons"));
    registration_fails = 0;
    CHECK(usb_source_init(&core, "usb-buttons"));
    CHECK(usb_source_init(&core, "usb-buttons"));
    CHECK(registrations == 2);

    reset(); idle(f); memcpy(before, f, sizeof f);
    frame(f, 90); unchanged(f, before);
    CHECK(!usb_source_request(-1));
    CHECK(usb_source_request(100));
    CHECK(usb_source_tick(100) == USB_SOURCE_PENDING);
    CHECK(!usb_source_request(101));
    CHECK(usb_source_frame_at(AZ_BUS_DECK_IN, f, 152, 110) == AZ_FRAME_OK);
    unchanged(f, before);
    CHECK(usb_source_frame_at(AZ_BUS_MIXER_IN, f, 97, 110) == AZ_FRAME_OK);
    unchanged(f, before);
    CHECK(usb_source_frame_at(AZ_BUS_MIXER_IN, NULL, 152, 110) == AZ_FRAME_OK);
    CHECK(usb_source_frame_at(AZ_BUS_MIXER_IN, f, 98, 120) == AZ_FRAME_OK);
    CHECK(f[32] == 0x80 && f[6] == 0x20);
    for (int i = 0; i < 152; i++) if (i != 6 && i != 32) CHECK(f[i] == before[i]);
    idle(f); frame(f, 319); CHECK(f[32] == 0x80);
    idle(f); frame(f, 320); unchanged(f, before);
    CHECK(usb_source_tick(321) == USB_SOURCE_SENT); /* Release is not arrival. */
    idle(f); frame(f, 400); unchanged(f, before); /* No second pulse. */
    mock_view = AZ_VIEW_SOURCE;
    CHECK(usb_source_tick(401) == USB_SOURCE_ARRIVED);
    CHECK(usb_source_tick(402) == USB_SOURCE_ARRIVED);
    CHECK(!usb_source_request(700));
    usb_source_end();
    CHECK(usb_source_tick(403) == USB_SOURCE_IDLE);
    mock_view = AZ_VIEW_DECK;
    CHECK(!usb_source_request(700)); /* Consumer has not retired phase yet. */
    idle(f); frame(f, 404); unchanged(f, before);
    CHECK(usb_source_phase == 0);
    CHECK(!usb_source_request(599)); /* 500 ms debounce measured from request. */
    CHECK(usb_source_request(600));
    idle(f); frame(f, 601); CHECK(f[32] == 0x80);
    usb_source_end(); idle(f); frame(f, 602);
    CHECK(!usb_source_request(599)); /* Reversed request time stays blocked. */

    /* Every physical screen/LOAD bit cancels, before and after synthetic release. */
    for (int released = 0; released < 2; released++) {
        for (int bit = 0; bit < 16; bit++) {
            reset();
            if (released) begin_and_release(f);
            else CHECK(usb_source_request(100));
            idle(f); f[32 + bit / 8] = (unsigned char)(1 << (bit % 8));
            memcpy(before, f, sizeof f);
            frame(f, released ? 350 : 101); unchanged(f, before);
            CHECK(usb_source_cancelled());
            mock_view = AZ_VIEW_SOURCE;
            CHECK(usb_source_tick(360) == USB_SOURCE_CANCELLED);
            CHECK(!usb_source_request(900));
        }
    }
    reset(); begin_and_release(f);
    mock_view = AZ_VIEW_SOURCE;
    CHECK(usb_source_tick(310) == USB_SOURCE_ARRIVED);
    idle(f); f[31] = 1; memcpy(before, f, sizeof f);
    frame(f, 311); unchanged(f, before);
    CHECK(usb_source_cancelled());
    CHECK(usb_source_tick(312) == USB_SOURCE_CANCELLED);

    /* Knob changes still cancel after arriving, not just during the pulse. */
    reset(); begin_and_release(f);
    mock_view = AZ_VIEW_SOURCE;
    idle(f); frame(f, 310);
    CHECK(usb_source_tick(311) == USB_SOURCE_ARRIVED);
    idle(f); f[34]++; memcpy(before, f, sizeof f);
    frame(f, 312); unchanged(f, before);
    CHECK(usb_source_cancelled());

    /* SOURCE before our first injected frame must never count as our arrival. */
    reset(); CHECK(usb_source_request(100)); mock_view = AZ_VIEW_SOURCE;
    CHECK(usb_source_tick(101) == USB_SOURCE_CANCELLED);
    idle(f); memcpy(before, f, sizeof f); frame(f, 102); unchanged(f, before);

    long expired[] = {99, 1100, 1101};
    for (unsigned i = 0; i < sizeof expired / sizeof *expired; i++) {
        reset(); CHECK(usb_source_request(100));
        idle(f); memcpy(before, f, sizeof f);
        frame(f, expired[i]); unchanged(f, before);
        CHECK(usb_source_cancelled());
    }
    reset(); CHECK(usb_source_request(100));
    CHECK(usb_source_tick(1100) == USB_SOURCE_CANCELLED); /* No bus is required to expire UI. */
    idle(f); memcpy(before, f, sizeof f); frame(f, 1101); unchanged(f, before);
    reset(); begin_and_release(f); mock_view = AZ_VIEW_SOURCE;
    CHECK(usb_source_tick(1099) == USB_SOURCE_ARRIVED);
    CHECK(usb_source_tick(1100) == USB_SOURCE_CANCELLED);

    int other_views[] = {AZ_VIEW_BROWSE, AZ_VIEW_SEARCH, AZ_VIEW_OTHER, AZ_VIEW_MENU};
    for (unsigned i = 0; i < sizeof other_views / sizeof *other_views; i++) {
        reset(); mock_view = other_views[i]; CHECK(!usb_source_request(100));
        mock_view = AZ_VIEW_DECK; begin_and_release(f);
        mock_view = other_views[i]; idle(f); memcpy(before, f, sizeof f);
        frame(f, 350); unchanged(f, before);
        CHECK(usb_source_tick(351) == USB_SOURCE_CANCELLED);
    }
    /* Leaving SOURCE through a UI action also invalidates a pending activation. */
    reset(); begin_and_release(f); mock_view = AZ_VIEW_SOURCE;
    CHECK(usb_source_tick(310) == USB_SOURCE_ARRIVED);
    mock_view = AZ_VIEW_DECK;
    CHECK(usb_source_tick(311) == USB_SOURCE_CANCELLED);
    reset(); begin_and_release(f); mock_view = AZ_VIEW_SOURCE;
    idle(f); frame(f, 310); mock_view = AZ_VIEW_DECK;
    idle(f); memcpy(before, f, sizeof f); frame(f, 311); unchanged(f, before);
    CHECK(usb_source_cancelled());

    /* End while held stops the pulse; bus retirement precedes the next request. */
    reset(); CHECK(usb_source_request(100)); idle(f); frame(f, 101);
    CHECK(f[32] == 0x80); usb_source_end(); usb_source_end();
    CHECK(!usb_source_request(800));
    idle(f); memcpy(before, f, sizeof f); frame(f, 102); unchanged(f, before);
    CHECK(usb_source_tick(103) == USB_SOURCE_IDLE);
    CHECK(usb_source_request(800));
    usb_source_end(); idle(f); frame(f, 801);
    usb_source_end(); CHECK(usb_source_tick(802) == USB_SOURCE_IDLE);
    CHECK(usb_source_clock_ms() >= 0);
    CHECK(usb_source_frame(AZ_BUS_DECK_IN, NULL, 0, NULL) == AZ_FRAME_OK);
    printf("PASS: %d SOURCE request assertions\n", checks);
    return 0;
}
