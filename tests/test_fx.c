/* SPDX-License-Identifier: GPL-3.0-only
 *
 * flow-beat 0.5.3/0.6.1: Stem FX, Drum Roll HOLD, loop state and Touch FX to FLOW (fx.h), end to end.
 * Builds flow-beat.c into this test with a fake core: real threads, the LED frame filter fed with
 * frames painted the way AZ-MODS 1.2.6 paints them (bus_interpose.c drum_hold_led) and EP147 lights
 * its loop buttons (xdj-az-emu DECK_LEDS), a fake azstem_stem_fx_on found by the mod's dlsym, and
 * the mod's UDP packets received on 127.0.0.1.        make -C tests fx
 */
#include "../flow-beat.c"

int flowlink_enable(int on) { (void)on; return 0; }
void flowlink_step(const flowlink_in *in, flowlink_out *out) { (void)in; memset(out, 0, sizeof *out); }
int flowlink_peers(void) { return -1; }
int flowlink_audio_enable(int on, unsigned rate, double extra_ms) { (void)on; (void)rate; (void)extra_ms; return 0; }
void flowlink_audio_send(int d, const float *lr, int frames) { (void)d; (void)lr; (void)frames; }
double flowlink_audio_rate_seen(int d) { (void)d; return 0; }
unsigned long flowlink_audio_sent(int d) { (void)d; return 0; }
unsigned flowlink_audio_rate_used(int d) { (void)d; return 0; }

#include <pthread.h>
#include <stdarg.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the fake azstem export (dlsym RTLD_DEFAULT finds it: linked with -rdynamic) ---- */
static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}
static _Atomic unsigned fake_sfx[4];
__attribute__((visibility("default"))) unsigned azstem_stem_fx_on(int deck);
unsigned azstem_stem_fx_on(int deck) { return deck >= 0 && deck < 4 ? atomic_load(&fake_sfx[deck]) : 0; }
/* 0.6.1: MLT's Touch FX azstem (the mailbox the panel posts into; layout as in its sdk_export.c) */
static _Atomic unsigned long long fake_touch[4];
__attribute__((visibility("default"))) int mrdjjag_touchfx_vj_abi(void);
int mrdjjag_touchfx_vj_abi(void) { return 1; }
__attribute__((visibility("default"))) unsigned long long mrdjjag_touchfx_vj_get(int deck);
unsigned long long mrdjjag_touchfx_vj_get(int deck) { return deck >= 0 && deck < 4 ? atomic_load(&fake_touch[deck]) : 0; }
static unsigned long long touch_word(int x, int y, int fx, int kind, int send, int latch, int col, int tg, unsigned count)
{
    return (unsigned long long)x | (unsigned long long)y << 10 | (unsigned long long)fx << 20 | (unsigned long long)kind << 24 |
           (unsigned long long)send << 26 | (unsigned long long)latch << 27 | (unsigned long long)col << 28 |
           (unsigned long long)tg << 32 | (unsigned long long)count << 36;
}
/* the panel posts every tick while it is open (~60 a second) */
static _Atomic int poster_run = 1;
static _Atomic unsigned long long poster_word[4];
static _Atomic int poster_on[4];
static void *poster(void *arg)
{
    (void)arg;
    unsigned c = 0;
    while (atomic_load(&poster_run)) {
        c++;
        for (int d = 0; d < 4; d++)
            if (atomic_load(&poster_on[d])) atomic_store(&fake_touch[d], atomic_load(&poster_word[d]) | (unsigned long long)(c & 0xFFFFFFFu) << 36);
        struct timespec t = { 0, 16000000 };
        nanosleep(&t, NULL);
    }
    return NULL;
}

/* ---- the LED frame a deck would paint ---- */
typedef struct {
    int sel;            /* 1 = deck 1/2, 2 = deck 3/4 */
    int drum, hold, smp;
    int reloop, in, out;
    int padlights;      /* another mod repainted the pads (all violet) */
} side_paint;
static side_paint paint_[2];
static pthread_mutex_t paint_mu = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int bus_run = 1;

static void px(unsigned char *p, int r, int g, int b) { p[0] = (unsigned char)r; p[1] = (unsigned char)g; p[2] = (unsigned char)b; }
static void paint_frame(unsigned char *f, const side_paint s[2], unsigned tick)
{
    memset(f, 0, 128);
    for (int side = 0; side < 2; side++) {
        unsigned char *b = f + side * 0x38;
        b[0x0c] = (unsigned char)(s[side].sel == 2 ? (1 | 2 << 2) : (2 | 1 << 2)) | 0x30;  /* + JOG MODE bits */
        b[0x09] = (unsigned char)((s[side].reloop & 3) | 0x40);                           /* + BEAT SYNC level */
        b[0x0a] = (unsigned char)((s[side].in & 3) << 6 | (s[side].out & 3) << 4 | 0x04); /* + QUANTIZE */
        unsigned char *pb = b + 0x1e;
        for (int i = 0; i < 8; i++) px(pb + 4 * i, 0x40, 0x10, 0x00);   /* stock SLIP LOOP pads */
        if (s[side].drum) {   /* bus_interpose.c, armed and on */
            int blink = ((tick >> 5) & 1u) != 0;
            for (int i = 0; i < 4; i++) {
                if (i == s[side].smp) px(pb + 4 * i, blink ? 0x37 : 0x06, blink ? 0xFF : 0x22, 0);
                else px(pb + 4 * i, 0x06, 0x22, 0);
            }
            px(pb + 16, 0, 0, 0); px(pb + 20, 0, 0, 0);
            px(pb + 24, 0, 0, s[side].hold ? (blink ? 0xFF : 0x00) : 0x22);
            px(pb + 28, 0x00, 0xFF, 0x00);
        }
        if (s[side].padlights)
            for (int i = 0; i < 8; i++) px(pb + 4 * i, 0x80, 0x20, 0xFF);
    }
}

static int (*led_fn)(int, unsigned char *, int, void *);
static void *bus_thread(void *arg)
{
    (void)arg;
    unsigned tick = 0;
    unsigned char f[128];
    while (atomic_load(&bus_run)) {
        side_paint s[2];
        pthread_mutex_lock(&paint_mu);
        memcpy(s, paint_, sizeof s);
        pthread_mutex_unlock(&paint_mu);
        paint_frame(f, s, ++tick);
        if (led_fn) led_fn(AZ_BUS_ERP_LED, f, 0x78, NULL);
        struct timespec t = { 0, 1000000 };   /* ~1000 frames a second, as on the deck */
        nanosleep(&t, NULL);
    }
    return NULL;
}

/* ---- a fake core ---- */
static int port;
static void t_log(const char *id, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("     [%s] ", id);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
static int t_cfg_get(const char *id, const char *key, int def) { (void)id; return !strcmp(key, "port") ? port : def; }
static int t_cfg_get_str(const char *id, const char *key, char *buf, int n, const char *def)
{
    (void)id;
    snprintf(buf, (size_t)n, "%s", !strcmp(key, "host") ? "127.0.0.1" : def ? def : "");
    return 0;
}
static int t_deck_info(int i, az_deck_info *o) { (void)i; int sz = o->size; memset(o, 0, sizeof *o); o->size = sz; return 0; }
static int t_deck_grid(int i, double *beat, double *bar) { (void)i; *beat = -1; *bar = -1; return -1; }
static int t_thread_start(const char *id, const char *name, void *(*fn)(void *), void *arg)
{
    (void)id;
    if (strcmp(name, "flow-beat") != 0) return -1;   /* only the worker matters here */
    pthread_t t;
    return pthread_create(&t, NULL, fn, arg) == 0 ? 0 : -1;
}
static int t_on_deck_event(const char *id, void (*fn)(int, int, void *), void *user) { (void)id; (void)fn; (void)user; return 0; }
static int nfilters, led_mask_ok;
static int t_frame_filter(const char *id, unsigned mask, az_frame_fn fn, void *user)
{
    (void)id; (void)user;
    nfilters++;
    if (mask == (1u << AZ_BUS_ERP_LED)) { led_fn = fn; led_mask_ok = 1; }   /* not RAW: the core skips RAW filters on the LED frame */
    return 0;
}

/* ---- receive ---- */
static int rx = -1;
typedef struct { int sfx, drum, hold, smp, loop; int t_have, t_on, t_x, t_y, t_tg; char t_fx[16], t_k[8]; double t_beats; } got_deck;
static void str_field(const char *p, const char *key, char *out, int n)
{
    char k[24];
    snprintf(k, sizeof k, "\"%s\":\"", key);
    const char *q = strstr(p, k);
    out[0] = 0;
    if (!q) return;
    q += strlen(k);
    int i = 0;
    while (q[i] && q[i] != '"' && i < n - 1) { out[i] = q[i]; i++; }
    out[i] = 0;
}
static int field(const char *p, const char *key)
{
    char k[24];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *q = strstr(p, k);
    return q ? atoi(q + strlen(k)) : -99;
}
/* The newest fx packet within ms: 1 = got one (decks filled). */
static int newest_fx(got_deck d[4], int ms, int *count)
{
    double end = now_ms() + ms;
    int have = 0;
    if (count) *count = 0;
    char buf[2048];
    while (now_ms() < end) {
        ssize_t n = recv(rx, buf, sizeof buf - 1, MSG_DONTWAIT);
        if (n <= 0) { struct timespec t = { 0, 1000000 }; nanosleep(&t, NULL); continue; }
        buf[n] = 0;
        if (!strstr(buf, "\"type\":\"fx\"")) continue;
        if (count) (*count)++;
        const char *p = buf;
        for (int i = 0; i < 4; i++) {
            char k[16];
            snprintf(k, sizeof k, "{\"d\":%d,", i + 1);
            const char *q = strstr(p, k);
            if (!q) return 0;
            d[i] = (got_deck){ field(q, "sfx"), field(q, "drum"), field(q, "hold"), field(q, "smp"), field(q, "loop"), 0, 0, 0, 0, 0, "", "", 0 };
            const char *end = strstr(q, i < 3 ? (i == 0 ? "{\"d\":2," : i == 1 ? "{\"d\":3," : "{\"d\":4,") : "]}");
            const char *t = strstr(q, "\"tfx\":{");
            if (t && (!end || t < end)) {
                d[i].t_have = 1;
                d[i].t_on = field(t, "on");
                d[i].t_x = field(t, "x");
                d[i].t_y = field(t, "y");
                d[i].t_tg = field(t, "tg");
                str_field(t, "fx", d[i].t_fx, sizeof d[i].t_fx);
                str_field(t, "k", d[i].t_k, sizeof d[i].t_k);
                const char *b = strstr(t, "\"beats\":");
                d[i].t_beats = b ? atof(b + 8) : -1;
            }
        }
        have = 1;
    }
    return have;
}
static void set_side(int side, side_paint s) { pthread_mutex_lock(&paint_mu); paint_[side] = s; pthread_mutex_unlock(&paint_mu); }
static side_paint get_side(int side) { pthread_mutex_lock(&paint_mu); side_paint s = paint_[side]; pthread_mutex_unlock(&paint_mu); return s; }

static void *blinker(void *arg)   /* LOOP IN / OUT blinking on the left side while *arg */
{
    _Atomic int *on = arg;
    int lvl = 2;
    while (atomic_load(on) >= 0) {
        if (atomic_load(on) == 1) {
            lvl = lvl == 2 ? 0 : 2;
            side_paint s = get_side(0);
            s.in = s.out = lvl;
            set_side(0, s);
        }
        struct timespec t = { 0, 250000000 };
        nanosleep(&t, NULL);
    }
    return NULL;
}

/* ---- unit checks of fx.h ---- */
static void unit(void)
{
    unsigned char f[128];
    side_paint s[2] = { { 1, 1, 1, 2, 2, 2, 2, 0 }, { 2, 0, 0, 0, 0, 2, 2, 0 } };
    paint_frame(f, s, 32);   /* blink on */
    fx_side a, b;
    fx_side_parse(f, 0, -1, &a);
    fx_side_parse(f, 1, -1, &b);
    CHECK(a.drum == 1 && a.hold == 1 && a.smp == 2 && a.sel == 1, "unit: left side drum on, HOLD on, sample 3 (drum %d hold %d smp %d sel %d)", a.drum, a.hold, a.smp, a.sel);
    CHECK(a.reloop == 2 && a.in == 2 && a.out == 2, "unit: loop lights decoded under the other bits in the same bytes");
    CHECK(b.drum == 0 && b.hold == 0 && b.smp == -1 && b.sel == 2, "unit: right side stock pads, deck 3/4 selected");
    paint_frame(f, s, 0);    /* blink off: HOLD blue dark, the selected sample dim */
    fx_side_parse(f, 0, 2, &a);
    CHECK(a.hold == 1 && a.smp == 2, "unit: HOLD stays on and the sample is kept in the dark half of the blink");
    s[0].hold = 0;
    paint_frame(f, s, 0);
    fx_side_parse(f, 0, 2, &a);
    CHECK(a.drum == 1 && a.hold == 0, "unit: HOLD off = pad 7 dim blue");
    s[0].padlights = 1;
    paint_frame(f, s, 32);
    fx_side_parse(f, 0, 2, &a);
    CHECK(a.drum == 0 && a.hold == 0, "unit: pads repainted by another mod read as no Drum Roll (never a false HOLD)");
    fx_side p = { 1, 2, 1, 1, 3, 1, 2, 3 }, q;
    fx_unpack(fx_pack(&p), &q);
    CHECK(!memcmp(&p, &q, sizeof p), "unit: pack / unpack round trip");
    fx_blink bl = { 0, 0, 0 };
    int on1 = fx_blinking(&bl, 1, 1000), on2 = fx_blinking(&bl, 2, 1250), on3 = fx_blinking(&bl, 2, 1700), on4 = fx_blinking(&bl, 2, 2000);
    CHECK(on1 && on2 && on3 && !on4, "unit: blink window follows a 250 ms blink (on %d %d %d, off after 750 ms: %d)", on1, on2, on3, on4);
    fx_deck d[4] = { { -1, -1, -1, -1, -1, { 0 } }, { 5, 1, 1, 2, 2, { 0 } }, { 0, 0, 0, -1, 0, { 0 } }, { 15, 0, 0, -1, 3, { 0 } } };
    char buf[600];
    for (int i = 0; i < 4; i++) memset(&d[i].t, 0, sizeof d[i].t);
    int n = fx_json(buf, (int)sizeof buf, 7, d);
    CHECK(n > 0 && n < (int)sizeof buf && strstr(buf, "{\"d\":2,\"sfx\":5,\"drum\":1,\"hold\":1,\"smp\":2,\"loop\":2}") && buf[n - 1] == '}',
          "unit: JSON %s", buf);
    n = fx_json(buf, 40, 7, d);
    CHECK(n < 40 && (int)strlen(buf) == n, "unit: JSON never runs past a short buffer");
    CHECK(!strstr(buf, "tfx"), "unit: no Touch FX report -> no tfx in the packet");
    fx_touch tt;
    fx_touch_decode(touch_word(420, 180, 3, 1, 1, 0, 5, 3, 77), &tt);
    CHECK(tt.rep && tt.on == 1 && tt.x == 420 && tt.y == 180 && tt.fx == 3 && tt.kind == 1 && tt.col == 5 && tt.tg == 3 && !tt.latch,
          "unit: Touch FX word decodes (x %d y %d fx %d kind %d col %d tg %d)", tt.x, tt.y, tt.fx, tt.kind, tt.col, tt.tg);
    fx_touch_decode(touch_word(1, 1, 6, 1, 1, 0, 0, 9, 1), &tt);
    CHECK(tt.tg == 9 && tt.fx == 6, "unit: PER STEM with the drums bit (tg %d)", tt.tg);
    fx_touch_decode(touch_word(420, 180, 3, 1, 1, 0, 5, 3, 77), &tt);
    CHECK(!strcmp(fx_touch_name(1, 3), "gate") && !strcmp(fx_touch_name(1, 0), "echo") && !strcmp(fx_touch_name(2, 3), "pitch") &&
          !strcmp(fx_touch_name(1, 6), "per_stem") && !strcmp(fx_touch_name(1, 7), "mix") && !strcmp(fx_touch_name(1, 9), "?"), "unit: effect names (STEMS 3 = gate, Drum Roll 3 = pitch, out of range = ?)");
    CHECK(fx_touch_beats(1, 0) == 4.0 && fx_touch_beats(1, 3) == 0.75 && fx_touch_beats(1, 7) == 0.0625 && fx_touch_beats(2, 0) == 0.0625 &&
          fx_touch_beats(2, 5) == 2.0, "unit: beat columns (STEMS 4 .. 1/16, Drum Roll 1/16 .. 2)");
    d[1].t = tt;
    n = fx_json(buf, (int)sizeof buf, 8, d);
    CHECK(strstr(buf, "\"loop\":2,\"tfx\":{\"on\":1,\"k\":\"stems\",\"fx\":\"gate\",\"x\":420,\"y\":180,\"beats\":0.25,\"tg\":3,\"latch\":0}}") && buf[n - 1] == '}',
          "unit: JSON with Touch FX %s", buf);
    for (int cap = 1; cap < n + 2; cap++) {
        char small[600];
        int m = fx_json(small, cap, 8, d);
        if (m >= cap || (int)strlen(small) != m) { CHECK(0, "unit: JSON with tfx overran a %d byte buffer", cap); break; }
    }
    fx_touch_age age = { 0, 0, 0 };
    fx_touch_read(&age, touch_word(1, 2, 0, 1, 1, 0, 0, 0, 5), 100, &tt);
    int on_fresh = tt.on;
    fx_touch_read(&age, touch_word(1, 2, 0, 1, 1, 0, 0, 0, 5), 100 + FX_TOUCH_STALE, &tt);
    int on_edge = tt.on;
    fx_touch_read(&age, touch_word(1, 2, 0, 1, 1, 0, 0, 0, 5), 101 + FX_TOUCH_STALE, &tt);
    int on_stale = tt.on;
    fx_touch_read(&age, touch_word(1, 2, 0, 1, 1, 0, 0, 0, 6), 102 + FX_TOUCH_STALE, &tt);
    CHECK(on_fresh && on_edge && !on_stale && tt.on, "unit: a report whose count stops for 1 s reads as off, a new post is on again");
    fx_touch_read(&age, 0, 1, &tt);
    CHECK(!tt.rep, "unit: an empty mailbox is no report");
    /* bus thread cost: well under the core's 200 us budget */
    paint_frame(f, s, 32);
    static fx_leds L;
    double t0 = now_ms();
    for (int i = 0; i < 100000; i++) fx_led_take(&L, f, 0x78);
    double us = (now_ms() - t0) * 1000.0 / 100000;
    CHECK(us < 5, "unit: one LED frame takes %.3f us on the bus thread (budget 200)", us);
}

int main(void)
{
    unit();

    rx = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = 0 };
    if (bind(rx, (struct sockaddr *)&a, sizeof a) != 0) { perror("bind"); return 2; }
    socklen_t al = sizeof a;
    getsockname(rx, (struct sockaddr *)&a, &al);
    port = ntohs(a.sin_port);

    for (int i = 0; i < 2; i++) paint_[i] = (side_paint){ 1, 0, 0, -1, 0, 2, 2, 0 };
    static az_core_v1 core;
    memset(&core, 0, sizeof core);
    core.abi = AZ_MOD_ABI;
    core.size = sizeof core;
    core.log = t_log;
    core.cfg_get = t_cfg_get;
    core.cfg_get_str = t_cfg_get_str;
    core.deck_info = t_deck_info;
    core.deck_grid = t_deck_grid;
    core.thread_start = t_thread_start;
    core.on_deck_event = t_on_deck_event;
    core.frame_filter = t_frame_filter;
    pthread_t bt, blt;
    const az_mod_v1 *mod = az_mod_v1_entry();
    int r = mod->init(&core);
    pthread_create(&bt, NULL, bus_thread, NULL);   /* the core starts the bus after init() */
    CHECK(r == 0, "init returns 0 (%d)", r);
    CHECK(nfilters == 2 && led_mask_ok, "two frame filters: mixer (raw) and the LED frame (not raw)");

    got_deck d[4];
    int cnt = 0;
    int got = newest_fx(d, 2600, &cnt);   /* fx_resolve runs at step 2 of each 2 s */
    CHECK(got && d[0].sfx == 0 && d[0].drum == 0 && d[0].loop == 0 && d[1].loop == 0, "idle: no Stem FX, no Drum Roll, no loop on decks 1/2");
    CHECK(got && d[2].drum == -1 && d[2].loop == -1 && d[3].hold == -1, "idle: decks 3/4 are not on a side: unknown (-1)");

    atomic_store(&fake_sfx[0], 1u | 4u);
    got = newest_fx(d, 150, NULL);
    CHECK(got && d[0].sfx == 5 && d[1].sfx == 0, "Stem FX: VOCAL ECHO + INST BRAKE held on deck 1 -> sfx 5 within 150 ms (got %d)", got ? d[0].sfx : -99);
    atomic_store(&fake_sfx[0], 0);
    atomic_store(&fake_sfx[3], 8u);
    got = newest_fx(d, 150, NULL);
    CHECK(got && d[0].sfx == 0 && d[3].sfx == 8, "Stem FX: released on deck 1, DRUMS ECHO on deck 4 (a deck not on a side still reports Stem FX)");
    atomic_store(&fake_sfx[3], 0);

    set_side(0, (side_paint){ 1, 1, 0, 1, 0, 2, 2, 0 });
    got = newest_fx(d, 300, NULL);
    CHECK(got && d[0].drum == 1 && d[0].hold == 0 && d[0].smp == 1, "Drum Roll on deck 1, sample 2 selected, HOLD off (drum %d hold %d smp %d)", d[0].drum, d[0].hold, d[0].smp);
    set_side(0, (side_paint){ 1, 1, 1, 1, 0, 2, 2, 0 });
    newest_fx(d, 120, NULL);
    int holds = 0, samples = 0, n = 0;
    double end = now_ms() + 1500;
    while (now_ms() < end) {   /* through several blink phases */
        if (newest_fx(d, 60, NULL)) { n++; holds += d[0].hold == 1; samples += d[0].smp == 1; }
    }
    CHECK(n > 0 && holds == n && samples == n, "HOLD on stays on through 1.5 s of pad 7 blinking: no flicker packets (%d/%d say HOLD, %d/%d keep the sample)", holds, n, samples, n);
    set_side(0, (side_paint){ 1, 0, 0, -1, 0, 2, 2, 0 });
    got = newest_fx(d, 150, NULL);
    CHECK(got && d[0].drum == 0 && d[0].hold == 0 && d[0].smp == -1, "Drum Roll off clears HOLD and the sample");

    set_side(1, (side_paint){ 2, 1, 1, 3, 0, 2, 2, 0 });
    got = newest_fx(d, 300, NULL);
    CHECK(got && d[3].drum == 1 && d[3].hold == 1 && d[3].smp == 3 && d[1].drum == -1 && d[1].loop == -1,
          "right side on deck 4: deck 4 Drum Roll + HOLD, deck 2 unknown (deck4 drum %d hold %d, deck2 drum %d)", d[3].drum, d[3].hold, d[1].drum);
    set_side(1, (side_paint){ 1, 0, 0, -1, 0, 2, 2, 0 });

    _Atomic int blinking = 0;
    pthread_create(&blt, NULL, blinker, (void *)&blinking);
    side_paint s = get_side(0);
    s.reloop = 2;
    set_side(0, s);
    atomic_store(&blinking, 1);
    got = newest_fx(d, 900, NULL);
    CHECK(got && d[0].loop == 2, "loop: RELOOP lit + IN/OUT blinking -> looping (2) (got %d)", got ? d[0].loop : -99);
    int loops = 0; n = 0;
    end = now_ms() + 2000;
    while (now_ms() < end) if (newest_fx(d, 80, NULL)) { n++; loops += d[0].loop == 2; }
    CHECK(n > 0 && loops == n, "loop: stays 2 between blinks (%d/%d)", loops, n);
    atomic_store(&blinking, 0);
    s = get_side(0); s.in = s.out = 2; set_side(0, s);
    double t_exit = now_ms();
    int when = -1;
    while (now_ms() - t_exit < 2000) if (newest_fx(d, 20, NULL) && d[0].loop == 1) { when = (int)(now_ms() - t_exit); break; }
    CHECK(when >= 0 && when < 1300, "loop exit (lights steady, RELOOP still lit) -> held (1) after %d ms", when);
    s = get_side(0); s.reloop = 0; set_side(0, s);
    got = newest_fx(d, 150, NULL);
    CHECK(got && d[0].loop == 0, "loop cleared -> 0");
    s = get_side(0); s.reloop = 0; set_side(0, s);
    atomic_store(&blinking, 1);
    got = newest_fx(d, 900, NULL);
    CHECK(got && d[0].loop == 3, "IN/OUT blinking without RELOOP -> a loop being set (3)");
    atomic_store(&blinking, -1);
    s = get_side(0); s.in = s.out = 2; set_side(0, s);

    /* ---- 0.6.1: Touch FX ---- */
    pthread_t pt;
    pthread_create(&pt, NULL, poster, NULL);
    got = newest_fx(d, 1100, NULL);   /* the keepalive */
    CHECK(got && !d[0].t_have && !d[1].t_have, "Touch FX: nothing posted yet -> no tfx");
    atomic_store(&poster_word[1], touch_word(300, 100, 3, 1, 1, 0, 5, 1, 0));
    atomic_store(&poster_on[1], 1);
    got = newest_fx(d, 150, NULL);
    CHECK(got && d[1].t_have && d[1].t_on == 1 && !strcmp(d[1].t_fx, "gate") && !strcmp(d[1].t_k, "stems") && d[1].t_x == 300 &&
          d[1].t_y == 100 && d[1].t_tg == 1 && d[1].t_beats == 0.25 && !d[0].t_have,
          "Touch FX: GATE on deck 2's vocals, finger at 300/100, 1/4 beat column -> tfx within 150 ms (on %d fx %s x %d y %d beats %g)",
          d[1].t_on, d[1].t_fx, d[1].t_x, d[1].t_y, d[1].t_beats);
    int moves = 0, last_x = -1;
    for (int x = 400; x <= 900; x += 100) {
        atomic_store(&poster_word[1], touch_word(x, 100, 3, 1, 1, 0, 5, 1, 0));
        if (newest_fx(d, 60, NULL) && d[1].t_x == x && d[1].t_x != last_x) { moves++; last_x = d[1].t_x; }
    }
    CHECK(moves == 6, "Touch FX: a finger drag shows up packet by packet (%d of 6 positions seen)", moves);
    atomic_store(&poster_word[1], touch_word(900, 100, 3, 1, 0, 0, 5, 1, 0));
    got = newest_fx(d, 150, NULL);
    CHECK(got && d[1].t_have && d[1].t_on == 0, "Touch FX: finger lifted -> on 0");
    atomic_store(&poster_word[1], touch_word(500, 500, 0, 2, 1, 1, 2, 0, 0));
    got = newest_fx(d, 150, NULL);
    CHECK(got && d[1].t_on == 1 && !strcmp(d[1].t_k, "drum") && !strcmp(d[1].t_fx, "on") && d[1].t_beats == 0.25,
          "Touch FX: Drum Roll panel latched -> k drum, beats from its own columns");
    atomic_store(&poster_on[1], 0);   /* the panel went away with the FX latched */
    double t_gone = now_ms();
    int off_after = -1;
    while (now_ms() - t_gone < 2500) if (newest_fx(d, 40, NULL) && d[1].t_on == 0) { off_after = (int)(now_ms() - t_gone); break; }
    CHECK(off_after >= 800 && off_after < 1600, "Touch FX: posts stop while latched -> off after %d ms (never stuck on)", off_after);
    atomic_store(&poster_run, 0);
    pthread_join(pt, NULL);

    newest_fx(d, 1400, NULL);
    got = newest_fx(d, 1300, &cnt);
    CHECK(got && cnt >= 1 && cnt <= 3, "nothing changes: one keepalive a second (%d in 1.3 s)", cnt);

    atomic_store(&bus_run, 0);
    pthread_join(bt, NULL);
    pthread_join(blt, NULL);
    printf("%s: %d of %d checks failed\n", fails ? "FAILED" : "ALL PASS", fails, checks);
    return fails ? 1 : 0;
}
