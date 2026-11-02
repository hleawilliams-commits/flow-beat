/* SPDX-License-Identifier: GPL-3.0-only
 * SAMPLES pads end to end on the host: a small fake core plus fake azstem_drum_* exports (found by the mod's dlsym,
 * hence -rdynamic). ERP input frames go in; we check what the deck sees, the Drum Roll calls and their timing, and
 * the green mode buttons.
 *   cc -rdynamic -I<sdk>/include -o /tmp/sme tests/test_samples_e2e.c -lpthread -lm -ldl && /tmp/sme */
#include <assert.h>
#include "../pad-lights.c"

static int checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
static az_frame_fn filt;
static int sm_row = 0, playing = 0;
static double phase = -1;
static int f_frame_filter(const char *id, unsigned mask, az_frame_fn fn, void *u) { (void)id; (void)u; (void)mask; filt = fn; return 0; }
static int f_register_row(const char *id, const az_row_v1 *r) { (void)id; return !strcmp(r->key, "samples") ? 2 : !strcmp(r->key, "vj") ? 1 : 0; }
static int f_row_value(int h) { return h == 2 ? sm_row : h == 1 ? 0 : PL_OFF; }
static int f_deck_info(int d, az_deck_info *o) { (void)d; o->loaded = 1; o->playing = playing; o->bpm = 120.0; return 0; }
static int f_deck_grid(int d, double *bp, double *bar) { (void)d; *bp = phase; *bar = -1; return phase < 0 ? -1 : 0; }
static int f_thread_start(const char *id, const char *n, void *(*fn)(void *), void *a) { (void)id; (void)n; pthread_t t; return pthread_create(&t, NULL, fn, a); }
static int f_cfg_get(const char *id, const char *k, int def) { (void)id; (void)k; return def; }
static int f_cfg_get_str(const char *id, const char *k, char *b, int n, const char *def) { (void)id; (void)k; snprintf(b, (size_t)n, "%s", def); return 0; }
static void f_log(const char *id, const char *fmt, ...) { (void)id; (void)fmt; }

/* fake azstem: what the Drum Roll engine was told */
static int g_gate[4], g_touch[4], g_sel[4] = {-1, -1, -1, -1}, g_rate[4] = {-1, -1, -1, -1}, g_bank, g_ups[4];
static int64_t g_gate_on_ms[4], g_gate_off_ms[4];
__attribute__((visibility("default"))) void azstem_drum_gate(int s, int on)
{ __atomic_store_n(&g_touch[s], 0, __ATOMIC_SEQ_CST); __atomic_store_n(&g_gate[s], on, __ATOMIC_SEQ_CST);
  if (on) { g_gate_on_ms[s] = pl_ms(); __atomic_add_fetch(&g_ups[s], 1, __ATOMIC_SEQ_CST); } else g_gate_off_ms[s] = pl_ms(); }
__attribute__((visibility("default"))) void azstem_drum_select(int s, int k) { __atomic_store_n(&g_sel[s], k, __ATOMIC_SEQ_CST); }
__attribute__((visibility("default"))) void azstem_drum_rate(int s, int r) { __atomic_store_n(&g_rate[s], r, __ATOMIC_SEQ_CST); }
__attribute__((visibility("default"))) void azstem_drum_touch(int s, int on) { __atomic_store_n(&g_touch[s], on, __ATOMIC_SEQ_CST); }
__attribute__((visibility("default"))) void azstem_drum_bank(int b) { __atomic_store_n(&g_bank, b, __ATOMIC_SEQ_CST); }
__attribute__((visibility("default"))) int azstem_drum_available(void) { return 0x3; }

static void sleep_ms(int ms) { struct timespec t = {0, ms * 1000000L}; nanosleep(&t, NULL); }
static unsigned char in[30];
static void press(int side, int modes, int pads)
{
    memset(in, 0, sizeof in);
    in[side ? 0x15 : 0x0d] = (unsigned char)modes; in[side ? 0x16 : 0x0e] = (unsigned char)pads;
    filt(AZ_BUS_ERP_IN, in, sizeof in, NULL);
    if (getenv("DBG")) fprintf(stderr, "press %d %x %x -> state %x en %d on %d wr %d\n", side, modes, pads, pl_vj_state[side], pl_vj_enabled, pl_vjside[side].on, pl_vjside[side].wait_release);
    sleep_ms(15);
}
int main(void)
{
    static az_core_v1 core;
    core.abi = AZ_MOD_ABI; core.size = sizeof core;
    core.frame_filter = f_frame_filter; core.register_row = f_register_row; core.row_value = f_row_value;
    core.deck_info = f_deck_info; core.deck_grid = f_deck_grid; core.thread_start = f_thread_start; core.cfg_get = f_cfg_get;
    core.cfg_get_str = f_cfg_get_str; core.log = f_log;
    CHECK(pl_init(&core) == 0 && filt);
    pl_tick();

    /* row off: HOT CUE + SLIP LOOP and the pads are the deck's */
    press(0, 0x5, 0x01); CHECK(in[0x0d] == 0x5 && in[0x0e] == 0x01);
    press(0, 0, 0);
    /* row on ("next beat"): the sampler finds azstem within ~1 s */
    sm_row = 1; pl_tick();
    for (int i = 0; i < 200 && !__atomic_load_n(&pl_sm_ok, __ATOMIC_RELAXED); i++) sleep_ms(10);
    CHECK(pl_sm_ok);
    press(0, 0x5, 0); CHECK(in[0x0d] == 0);                      /* the combo opens the page */
    press(0, 0, 0);
    /* stopped deck: pad 2 = snare, now, released ~half a beat (250 ms at 120) later */
    press(0, 0, 0x02); CHECK(in[0x0e] == 0);
    sleep_ms(10);
    CHECK(g_gate[0] == 1 && g_touch[0] == 1 && g_sel[0] == 1 && g_rate[0] == 0 && g_bank == 1);   /* first use loads Bank 1 */
    press(0, 0, 0);
    sleep_ms(300);
    CHECK(g_gate[0] == 0);
    { int64_t held = g_gate_off_ms[0] - g_gate_on_ms[0]; CHECK(held >= 240 && held <= 290); }
    /* playing at phase 0.8, "next 1/4 beat" (125 ms): the hit is 100 ms away (beat 1), release half a 1/4 later = 162 ms */
    sm_row = 3; pl_tick(); playing = 1; phase = 0.8;
    press(0, 0, 0x08); sleep_ms(5);
    CHECK(g_gate[0] == 1 && g_sel[0] == 3 && g_rate[0] == 2);
    press(0, 0, 0); sleep_ms(200);
    { int64_t held = g_gate_off_ms[0] - g_gate_on_ms[0]; CHECK(held >= 150 && held <= 200); }
    /* "next beat" at phase 0.5: hit in 250 ms, release at 500 ms */
    sm_row = 1; pl_tick(); phase = 0.5;
    press(0, 0, 0x01); press(0, 0, 0); sleep_ms(600);
    { int64_t held = g_gate_off_ms[0] - g_gate_on_ms[0]; CHECK(held >= 490 && held <= 540); }
    /* a second pad while still holding: no new gate edge, the new sound takes the next hit */
    int ups = g_ups[0];
    press(0, 0, 0x01); press(0, 0, 0); press(0, 0, 0x04); press(0, 0, 0);
    CHECK(g_ups[0] == ups + 1 && g_sel[0] == 2 && g_gate[0] == 1);
    sleep_ms(700); CHECK(g_gate[0] == 0);
    /* pads 5-8 pick the bank, and never sound */
    press(0, 0, 0x20); press(0, 0, 0); sleep_ms(10);
    CHECK(g_bank == 2 && g_gate[0] == 0);
    /* right side plays deck 2 */
    press(1, 0x5, 0); press(1, 0, 0); press(1, 0, 0x01); sleep_ms(10);
    CHECK(g_gate[1] == 1 && g_sel[1] == 0); press(1, 0, 0);
    /* LEDs: green mode buttons, Bank 2 lit, bank 3 missing dark */
    pl_tick();
    unsigned char led[128]; memset(led, 0, sizeof led);
    filt(AZ_BUS_ERP_LED, led, sizeof led, NULL);
    CHECK(led[0x0e] == 0 && led[0x0e + 1] == 255 && led[0x0e + 2] == pl_gam[90]);
    CHECK(led[0x1e + 4 * 5 + 2] == 255 && led[0x1e + 4 * 6] == 0 && led[0x1e + 4 * 6 + 2] == 0);
    /* HOT CUE alone closes the page and passes; the pads are the deck's again */
    press(0, 0x1, 0); CHECK(in[0x0d] == 0x1);
    press(0, 0, 0x01); CHECK(in[0x0e] == 0x01);
    printf("samples e2e: %d checks passed\n", checks);
    return 0;
}
