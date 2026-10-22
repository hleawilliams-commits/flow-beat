/* SPDX-License-Identifier: GPL-3.0-only */
/* Appended after exact pinned read-path functions and upstream fixture stand-ins.
 * Each check compares behavior, not a replacement model of bus_mixer_frame(). */
static unsigned checks, failures;
static const char *scenario;
#define CHECK(cond, label) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s/%s\n", scenario, label); } } while (0)

static void reset_fixture(void) {
    /* Flush the two function-local button latches through actual production functions.
     * They are intentionally not exposed or replaced for the test. */
    uint8_t idle[98] = {0};
    g_nm.state = 0;
    g_menu_focused = g_menu_open = g_srcrow_want = g_sdk_focus = 0;
    mixer_screen_buttons(idle, 0);
    mixer_naming_back(idle);
    g_back_pending = g_click_pending = g_close_pending = g_enc_dir = 0;
    g_menu_focused = g_menu_open = g_search_on = g_src_visible = g_srcrow_open = g_srcrow_want = 0;
    g_srcrow_active = g_src_swallow = g_srcrow_pin_ours = g_srcrow_repaint = g_srcrow_unfocus = 0;
    g_last_click_bit = g_last_back_bit = 0;
    g_menu_top = 1;
    g_kn_frames = g_kn_wdmax = g_kn_rel = g_kn_leak = 0;
    g_sdk_focus = g_sdk_back_pending = 0;
    g_viewleave_n = 0;
    g_wave_seen_ms = 99;
    g_drum_arm = 0;
    g_drum_tone_fn = NULL;
    g_drag_on = g_drag_committing = g_reorder_enabled = 0;
    g_nm_cancel_req = 0;
    knob_seen = knob_anchored = knob_was_hold = 0;
    knob_last = knob_anchor = 0;
    wd_last = g_overlay_beat = 17;
    wd_stall = 0;
    raw_calls = run_calls = 0;
    filter_edit = 0;
    memset(raw_seen, 0, sizeof raw_seen);
    memset(run_seen, 0, sizeof run_seen);
}

static void make_frame(uint8_t *f, size_t n, uint8_t raw) {
    for (size_t i = 0; i < n; ++i) f[i] = (uint8_t) (i * 37u + n);
    if (n > 6) f[6] = 0x08;
    if (n > 22) f[22] = 0; /* independent FX stand-in stays inert */
    if (n > 31) f[31] = 0x02;
    if (n > 32) f[32] = 0;
    if (n > 33) f[33] = 0x01; /* preserve an unrelated corner-button bit */
    if (n > 34) f[34] = raw;
    if (n > 35) f[35] = 0xa7;
    if (n >= 98) frame_crc_stamp(f, 96);
}

static void owned(int sdk) {
    g_sdk_focus = sdk;
    g_menu_open = g_menu_focused = g_srcrow_want = !sdk;
    g_src_visible = g_srcrow_active = !sdk;
}

static void frame_equals(const uint8_t *actual, const uint8_t *expected, size_t n) {
    CHECK(memcmp(actual, expected, n) == 0, "all-frame-bytes");
    if (n >= 98) CHECK(frame_crc_ok(actual, 96), "valid-output-crc");
}

static void boundary(int sdk, int recovery) {
    scenario = sdk ? (recovery ? "sdk-recovery" : "sdk-stall") : (recovery ? "menu-recovery" : "menu-stall");
    reset_fixture();
    owned(sdk);
    wd_stall = recovery ? 201 : 200;
    if (recovery) g_overlay_beat++;
    knob_seen = 1;
    knob_last = 90;
    knob_anchor = 70;
    knob_anchored = knob_was_hold = !recovery;
    /* Held buttons test level suppression without changing focus via a fresh BACK edge. */
    g_last_click_bit = g_last_back_bit = 1;
    uint8_t f[128], want[128];
    make_frame(f, sizeof f, 91);
    f[31] |= 1;
    f[33] |= 0x80;
    frame_crc_stamp(f, 96);
    memcpy(want, f, sizeof f);
    if (recovery) { want[31] &= (uint8_t) ~1; want[33] &= (uint8_t) ~0x80; frame_crc_stamp(want, 96); }
    bus_mixer_frame(f, sizeof f, 0);
    frame_equals(f, want, sizeof f);
    CHECK(wd_stall == (recovery ? 0u : 201u), "fresh-watchdog");
    CHECK(wd_last == g_overlay_beat, "heartbeat-recorded");
    CHECK(knob_anchored == recovery, "anchor-state");
    CHECK(knob_was_hold == recovery, "hold-state");
    CHECK(g_srcrow_pin_ours == !recovery, "existing-release-pin");
    CHECK(g_enc_dir == 1 && knob_last == 91, "raw-counter-tracked");
    CHECK(f[35] == 0xa7, "byte35-preserved");
    CHECK(raw_calls == 1 && run_calls == 1, "checked-sdk-calls");
    CHECK(raw_seen[34] == 91 && raw_seen[31] == 3 && raw_seen[33] == 0x81, "raw-sdk-precedes-core");
    CHECK(memcmp(run_seen, f, sizeof f) == 0, "sdk-run-follows-core");
    if (!sdk) {
        CHECK(g_kn_wdmax == (recovery ? 0u : 201u), "diagnostic-current-stall");
        CHECK(g_kn_rel == (unsigned) !recovery, "diagnostic-release");
        CHECK(g_kn_leak == (unsigned) !recovery, "diagnostic-native-detent");
    }
    if (recovery) {
        CHECK(knob_anchor == 91, "recovery-anchor-is-current-counter");
        make_frame(f, sizeof f, 92);
        f[31] |= 1; f[33] |= 0x80;
        frame_crc_stamp(f, 96);
        memcpy(want, f, sizeof f);
        want[34] = 91; want[31] &= (uint8_t) ~1; want[33] &= (uint8_t) ~0x80;
        frame_crc_stamp(want, 96);
        bus_mixer_frame(f, sizeof f, 0);
        frame_equals(f, want, sizeof f);
        CHECK(wd_stall == 1 && g_enc_dir == 2, "recovery-next-frame-cadence");
    }
}

static void stable_and_wrap(void) {
    for (int sdk = 0; sdk < 2; ++sdk) for (int direction = -1; direction <= 1; direction += 2) {
        scenario = sdk ? (direction > 0 ? "sdk-wrap-cw" : "sdk-wrap-ccw") : (direction > 0 ? "menu-wrap-cw" : "menu-wrap-ccw");
        reset_fixture(); owned(sdk);
        wd_stall = 199;
        knob_seen = knob_anchored = knob_was_hold = 1;
        knob_last = direction > 0 ? 255 : 0;
        knob_anchor = 70;
        uint8_t f[98], want[98];
        make_frame(f, sizeof f, direction > 0 ? 0 : 255);
        f[31] |= 1; frame_crc_stamp(f, 96);
        memcpy(want, f, sizeof f); want[31] &= (uint8_t) ~1; want[34] = 70; frame_crc_stamp(want, 96);
        bus_mixer_frame(f, sizeof f, 0);
        frame_equals(f, want, sizeof f);
        CHECK(wd_stall == 200 && knob_anchored, "threshold-200-still-holds");
        CHECK(g_enc_dir == direction, "signed-byte34-wrap");
        CHECK(f[35] == 0xa7 && g_srcrow_pin_ours == 0, "byte35-and-pin-unchanged");
        /* Explicit focus release retains the existing native counter jump and SOURCE pin. */
        g_menu_focused = g_menu_open = g_srcrow_want = g_sdk_focus = 0;
        g_overlay_beat++;
        make_frame(f, sizeof f, direction > 0 ? 1 : 254);
        memcpy(want, f, sizeof f);
        bus_mixer_frame(f, sizeof f, 0);
        frame_equals(f, want, sizeof f);
        CHECK(g_srcrow_pin_ours == 1 && !knob_anchored, "focus-release-retains-source-pin");
        CHECK(g_enc_dir == direction * 2, "detents-follow-raw-not-rewritten-counter");
    }
}

static void lengths_and_crc(void) {
    scenario = "lengths";
    for (size_t n = 0; n <= 128; ++n) {
        reset_fixture();
        wd_stall = 199;
        uint8_t *f = malloc(n ? n : 1), *want = malloc(n ? n : 1);
        if (!f || !want) abort();
        make_frame(f, n, 91); memcpy(want, f, n);
        bus_mixer_frame(f, (ssize_t) n, 0);
        frame_equals(f, want, n);
        CHECK(wd_stall == (n < 12 ? 199u : 200u), "first-call-cadence");
        CHECK(raw_calls == (unsigned) (n >= 98) && run_calls == (unsigned) (n >= 98), "sdk-only-checked-frames");
        CHECK(knob_seen == (n >= 35), "byte34-length-gate");
        bus_mixer_frame(f, (ssize_t) n, 0);
        frame_equals(f, want, n);
        CHECK(wd_stall == (n < 12 ? 199u : 201u), "second-call-cadence");
        free(f); free(want);
    }
    for (int sdk = 0; sdk < 2; ++sdk) for (int all_ff = 0; all_ff < 2; ++all_ff) {
        scenario = sdk ? "invalid-sdk" : "invalid-menu";
        reset_fixture(); owned(sdk);
        wd_stall = 200; knob_seen = knob_was_hold = knob_anchored = 1; knob_anchor = 70; knob_last = 90;
        uint8_t f[128], want[128];
        if (all_ff) memset(f, 0xff, sizeof f);
        else { make_frame(f, sizeof f, 91); f[31] |= 1; f[33] |= 0x80; frame_crc_stamp(f, 96); f[96] ^= 1; }
        CHECK(!frame_crc_ok(f, 96), "fixture-crc-is-invalid");
        memcpy(want, f, sizeof f);
        bus_mixer_frame(f, sizeof f, 0);
        CHECK(memcmp(f, want, sizeof f) == 0, "invalid-frame-untouched");
        CHECK(wd_stall == 200 && wd_last == 17, "invalid-does-not-refresh-watchdog");
        CHECK(knob_last == 90 && knob_anchored && knob_anchor == 70, "invalid-no-knob-state-change");
        CHECK(g_enc_dir == 0 && g_click_pending == 0 && g_back_pending == 0 && g_close_pending == 0, "invalid-no-input-pending");
        CHECK(raw_calls == 0 && run_calls == 0, "invalid-no-sdk-calls");
    }
    scenario = "watchdog-saturation";
    reset_fixture(); wd_stall = 100000;
    uint8_t f[98]; make_frame(f, sizeof f, 11);
    bus_mixer_frame(f, sizeof f, 0);
    CHECK(wd_stall == 100000, "stall-count-cap-preserved");
    g_overlay_beat++;
    bus_mixer_frame(f, sizeof f, 0);
    CHECK(wd_stall == 0, "heartbeat-resets-saturated-count");
}

static void source_handback(void) {
    scenario = "existing-source-handback";
    reset_fixture(); owned(0);
    knob_seen = knob_was_hold = knob_anchored = 1; knob_anchor = 70; knob_last = 90;
    uint8_t f[98], want[98];
    make_frame(f, sizeof f, 91); f[32] = 0x80; frame_crc_stamp(f, 96);
    memcpy(want, f, sizeof f); want[32] = 0; want[34] = 70; frame_crc_stamp(want, 96);
    bus_mixer_frame(f, sizeof f, 0);
    frame_equals(f, want, sizeof f);
    CHECK(g_src_swallow == 1 && g_menu_focused == 0 && g_srcrow_unfocus == 1 && g_srcrow_repaint == 1, "source-focus-release");
    CHECK(g_srcrow_pin_ours == 0 && knob_anchored == 1, "pin-still-follows-next-knob-frame");
    CHECK(g_close_pending == 0 && g_viewleave_n == 0, "source-on-source-not-view-close");
    make_frame(f, sizeof f, 92); f[32] = 0x80; frame_crc_stamp(f, 96);
    memcpy(want, f, sizeof f); want[32] = 0; frame_crc_stamp(want, 96);
    bus_mixer_frame(f, sizeof f, 0);
    frame_equals(f, want, sizeof f);
    CHECK(g_srcrow_pin_ours == 1 && !knob_anchored && g_src_swallow == 1, "held-source-passes-raw-counter-and-pins");
    g_srcrow_pin_ours = 0;
    make_frame(f, sizeof f, 93); memcpy(want, f, sizeof f);
    bus_mixer_frame(f, sizeof f, 0);
    frame_equals(f, want, sizeof f);
    CHECK(g_src_swallow == 0 && g_srcrow_pin_ours == 0, "source-release-clears-latch-once");
}

static void sdk_edit_and_config(void) {
    scenario = "sdk-filter-and-config";
    reset_fixture(); owned(1); filter_edit = 1;
    knob_seen = knob_was_hold = knob_anchored = 1; knob_anchor = 70; knob_last = 90;
    uint8_t f[128], want[128];
    make_frame(f, sizeof f, 91); f[31] |= 1; frame_crc_stamp(f, 96);
    memcpy(want, f, sizeof f); want[31] &= (uint8_t) ~1; want[34] = 70; want[80] ^= 0x55; frame_crc_stamp(want, 96);
    struct bus_xpad xp = {0x15, -1, 1, 124, -1};
    uint64_t cfg = bus_cfg_pack(1, 0, 1, 0, &xp);
    bus_mixer_frame(f, sizeof f, cfg);
    frame_equals(f, want, sizeof f);
    CHECK(raw_seen[34] == 91 && run_seen[34] == 70, "sdk-observation-order");
    CHECK(raw_seen[80] == run_seen[80] && f[80] == (uint8_t) (run_seen[80] ^ 0x55), "sdk-edit-after-core");
    CHECK(g_reorder_enabled == 1, "reorder-published-for-current-frame");
    cfg = bus_cfg_pack(0, 0, 1, 0, &xp);
    make_frame(f, sizeof f, 92);
    bus_mixer_frame(f, sizeof f, cfg);
    CHECK(g_reorder_enabled == 0, "reorder-disable-published");
}

int main(void) {
    for (int sdk = 0; sdk < 2; ++sdk) for (int recovery = 0; recovery < 2; ++recovery) boundary(sdk, recovery);
    stable_and_wrap();
    lengths_and_crc();
    source_handback();
    sdk_edit_and_config();
    scenario = "fixture";
    CHECK(g_filt_unchecked == 0, "no-unchecked-sdk-input");
    printf("checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
