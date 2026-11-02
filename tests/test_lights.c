/* SPDX-License-Identifier: GPL-3.0-only
 *
 * flow-beat 0.6.0: Lights from this deck (lights.h), on a computer.
 *   unit: the ArtDmx packet byte by byte, the patch parser, and a simulated track (beats, a
 *         breakdown, a drop) through the show: chases, the white hit, the fader dim, idle, and
 *         that the hazer, the laser and every fixture strobe channel stay at 0.
 *   end to end: flow-beat.c built into this test with a fake core: a deck playing at 128 BPM,
 *         its audio tap fed a kick on every beat (then a breakdown with no bass, then the drop),
 *         Art-Net received on 127.0.0.1:6454: rate, sequence, the drop's white hit and how late
 *         it lands, stepping back while another sender (127.0.0.2) talks, and lights off.
 *                                                                make -C tests lights
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

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

/* MLT's rig, by DMX address (FLOW VJ dmx_patch.json) */
#define A_4BAR 59
#define A_PAR1 74
#define A_SPECL 104
#define A_SPECR 268
#define A_SHOCK 432
#define A_BACK 479
static int ch(const uint8_t *dmx, int addr, int c) { return dmx[addr + c - 2]; }
/* every channel that must never move: the Scorpion laser (93-102), the hazer (103), strobes */
static int forbidden(const uint8_t *dmx)
{
    for (int a = 93; a <= 103; a++) if (dmx[a - 1]) return a;
    if (ch(dmx, A_4BAR, 3)) return A_4BAR + 2;
    for (int p = A_PAR1; p <= 82; p += 8) if (ch(dmx, p, 8) || ch(dmx, p, 7) || ch(dmx, p, 5)) return p + 7;
    if (ch(dmx, A_SPECL, 161)) return A_SPECL + 160;
    if (ch(dmx, A_SPECR, 161)) return A_SPECR + 160;
    if (ch(dmx, A_SHOCK, 3) || ch(dmx, A_SHOCK, 10) || ch(dmx, A_SHOCK, 4) || ch(dmx, A_SHOCK, 11) || ch(dmx, A_SHOCK, 13)) return A_SHOCK;
    for (int a = 482; a <= 512; a++) if (dmx[a - 1]) return a;
    return 0;
}
static int max_rgb(const uint8_t *dmx)
{
    int m = 0;
    for (int p = 0; p < 4; p++) for (int k = 0; k < 3; k++) if (ch(dmx, A_4BAR, 4 + 3 * p + k) > m) m = ch(dmx, A_4BAR, 4 + 3 * p + k);
    for (int k = 2; k <= 4; k++) if (ch(dmx, A_PAR1, k) > m) m = ch(dmx, A_PAR1, k);
    return m;
}
static int is_hit(const uint8_t *dmx)
{
    for (int p = 0; p < 4; p++) for (int k = 0; k < 3; k++) if (ch(dmx, A_4BAR, 4 + 3 * p + k) != 255) return 0;
    return ch(dmx, A_SPECL, 4) == 255 && ch(dmx, A_SHOCK, 39) == 255 && ch(dmx, A_PAR1, 6) == 255;
}

/* ---- unit ---- */
static void unit(void)
{
    uint8_t dmx[512], p[LT_PKT];
    for (int i = 0; i < 512; i++) dmx[i] = (uint8_t)i;
    int n = lt_artdmx(p, 7, 0x123, dmx);
    static const uint8_t head[18] = { 'A', 'r', 't', '-', 'N', 'e', 't', 0, 0x00, 0x50, 0, 14, 7, 0, 0x23, 0x01, 0x02, 0x00 };
    CHECK(n == 530 && !memcmp(p, head, 18) && !memcmp(p + 18, dmx, 512), "ArtDmx: ID, OpCode 0x5000 LE, version 14, sequence, SubUni/Net, length 512 BE, slots");
    CHECK(lt_artdmx_universe(p, n) == 0x123, "ArtDmx: universe read back (0x%x)", lt_artdmx_universe(p, n));
    p[9] = 0x20;
    CHECK(lt_artdmx_universe(p, n) == -1 && lt_artdmx_universe(p, 10) == -1, "ArtPoll / short packets are not ArtDmx");

    lt_fix f[LT_MAX_FIX];
    int bad;
    int nf = lt_parse_patch(LT_DEFAULT_PATCH, f, LT_MAX_FIX, &bad);
    CHECK(nf == 8 && bad == -1, "default patch: 8 fixtures (%d)", nf);
    int end_ok = 1, overlap = 0;
    for (int i = 0; i < nf; i++) {
        if (f[i].addr + f[i].span - 1 > 512) end_ok = 0;
        for (int j = 0; j < i; j++)
            if (f[i].addr < f[j].addr + f[j].span && f[j].addr < f[i].addr + f[i].span) overlap = 1;
    }
    CHECK(end_ok && !overlap, "default patch: no fixture overlaps another or runs past 512");
    CHECK(f[0].kind == LT_4BAR && f[0].addr == 59 && f[4].kind == LT_SPECTRA && f[4].addr == 104 && f[5].addr == 268 &&
          f[5].nth == 1 && f[6].kind == LT_SHOCK47 && f[6].addr == 432 && f[7].addr == 479,
          "default patch: addresses as in FLOW VJ's dmx_patch.json");
    nf = lt_parse_patch(" rgb3@1 , nope@5,slimpar_h6_8@506, slimpar_h6_8@505,colorband_spectra_164@200:flip,ezpar_t6_3@x", f, LT_MAX_FIX, &bad);
    CHECK(nf == 3 && bad == 1 && f[0].kind == LT_RGB3 && f[1].addr == 505 && f[2].flip == 1,
          "patch: spaces, unknown type, a fixture past 512 and a bad address skipped; flip read (%d, bad %d)", nf, bad);
    nf = lt_parse_patch("", f, LT_MAX_FIX, &bad);
    CHECK(nf == 0, "patch: empty = none (flow-beat.c then uses the default)");
    char junk[200];
    unsigned seed = 1;
    for (int k = 0; k < 2000; k++) {   /* garbage never runs off the end (ASan) */
        int len = (int)(seed % 199);
        for (int i = 0; i < len; i++) { seed = seed * 1103515245u + 12345u; junk[i] = "rgb3@1:,flip 0123456789_abcdefghijklmnopqrstuvwxyz"[(seed >> 16) % 50]; }
        junk[len] = 0;
        lt_parse_patch(junk, f, 4, &bad);
        seed = seed * 1103515245u + 12345u;
    }
    CHECK(1, "patch: 2000 garbage strings parsed without a memory error");

    /* a simulated track: 128 BPM, kick bass 0.8 on each beat for 16 bars, 4 bars with no bass,
     * then the drop; 25 ms steps as the worker runs them */
    nf = lt_parse_patch(LT_DEFAULT_PATCH, f, LT_MAX_FIX, NULL);
    lt_state s;
    lt_init(&s);
    double beat_ms = 60000.0 / 128, t = 0;
    int bad_ch = 0, pods_ok = 1, pod_checks = 0, drops_at = -1, hit_frames = 0, max_music = 0, max_brk = 999, brk_at = -1;
    int last_beat = -1;
    for (int k = 0; k < 2400; k++, t += 25) {
        int b = (int)(t / beat_ms);
        double ph = fmod(t, beat_ms) / beat_ms;
        int bar_beat = b % 4;
        int music = b < 64 || b >= 80;            /* bars 1-16, breakdown bars 17-20, drop on beat 80 */
        lt_in in = { 1, 128, ph, b != last_beat, bar_beat, music && ph < 0.25 ? 0.8 : 0.03, -1, 1, 25 };
        last_beat = b;
        lt_track(&s, &in);
        lt_render(&s, &in, f, nf, dmx);
        int fb = forbidden(dmx);
        if (fb) bad_ch = fb;
        if (s.drops && drops_at < 0) drops_at = b;
        if (is_hit(dmx)) hit_frames++;
        if (s.breakdown && brk_at < 0) brk_at = b;
        if (b >= 20 && b < 64 && !s.breakdown && !is_hit(dmx)) {
            int m = max_rgb(dmx); if (m > max_music) max_music = m;
            /* the beat's pod is the brightest of the four */
            int best = 0, bv = -1;
            for (int p = 0; p < 4; p++) { int v = ch(dmx, A_4BAR, 4 + 3 * p) + ch(dmx, A_4BAR, 5 + 3 * p) + ch(dmx, A_4BAR, 6 + 3 * p); if (v > bv) { bv = v; best = p; } }
            pod_checks++;
            if (best != bar_beat) pods_ok = 0;
        }
        if (b >= 74 && b < 80) { int m = max_rgb(dmx); if (m < max_brk) max_brk = m; }
    }
    CHECK(!bad_ch, "laser, hazer and every strobe channel stay 0 all track long (first bad: %d)", bad_ch);
    CHECK(pods_ok && pod_checks > 100, "4BAR: the pod of the beat in the bar is the brightest (%d frames)", pod_checks);
    CHECK(brk_at >= 72 && brk_at <= 73, "breakdown found after 2 bars without bass (beat %d, bass gone at 64)", brk_at);
    CHECK(drops_at == 80 && s.drops == 1, "the drop found on its own beat, once (beat %d, %d drops)", drops_at, s.drops);
    CHECK(hit_frames >= 15 && hit_frames <= 20, "the white hit lasts one beat (%d frames of 25 ms; a beat = %.0f ms)", hit_frames, beat_ms);
    CHECK(max_brk < max_music * 0.6, "breakdown dims the rig (brightest %d vs %d in the groove)", max_brk, max_music);

    /* the fader: down = 25 % of the level */
    lt_in in = { 1, 128, 0.1, 0, 0, 0.8, 1.0, 1, 25 };
    lt_render(&s, &in, f, nf, dmx);
    s.drop_beats = 0; s.hit_ms = 0;
    lt_render(&s, &in, f, nf, dmx);
    int up = max_rgb(dmx);
    in.dim = 0;
    lt_render(&s, &in, f, nf, dmx);
    int down = max_rgb(dmx);
    CHECK(up > 150 && down <= up * 0.3 + 1 && down > 0, "fader down dims to 25 %% (%d -> %d), never to black", up, down);
    /* the colour row */
    in.dim = 1; in.look = 1;
    lt_render(&s, &in, f, nf, dmx);
    int r1 = ch(dmx, A_PAR1, 2), b1 = ch(dmx, A_PAR1, 4);
    in.look = 4;
    lt_render(&s, &in, f, nf, dmx);
    int r4 = ch(dmx, A_PAR1, 2), b4 = ch(dmx, A_PAR1, 4);
    CHECK(r1 > b1 && b4 > r4, "colours: Riso par is red (%d/%d), Ice par is blue (%d/%d)", r1, b1, r4, b4);
    /* nothing playing */
    lt_in idle = { 0, 0, -1, 0, -1, 0, -1, 0, 25 };
    int idle_max = 0;
    for (int k = 0; k < 400; k++) { lt_track(&s, &idle); lt_render(&s, &idle, f, nf, dmx); int m = max_rgb(dmx); if (m > idle_max) idle_max = m; if (forbidden(dmx)) bad_ch = 1; }
    CHECK(idle_max > 0 && idle_max <= 52 && !bad_ch, "idle: a slow breath, never over 20 %% (brightest %d)", idle_max);
}

/* ---- end to end ---- */
static _Atomic int playing = 1, music = 1, lights = 1;
static double t0;
static void t_log(const char *id, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("     [%s %.0f ms] ", id, now_ms() - t0);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
static int t_cfg_get(const char *id, const char *key, int def)
{
    (void)id;
    if (!strcmp(key, "artnet")) return atomic_load(&lights);
    if (!strcmp(key, "send")) return 0;               /* FLOW packets off: only Art-Net here */
    return def;
}
static int t_cfg_get_str(const char *id, const char *key, char *buf, int n, const char *def)
{
    (void)id;
    snprintf(buf, (size_t)n, "%s", !strcmp(key, "artnet_host") ? "127.0.0.1" : def ? def : "");
    return 0;
}
#define BPM 128.0
static int t_deck_info(int i, az_deck_info *o)
{
    int sz = o->size;
    memset(o, 0, sizeof *o);
    o->size = sz;
    if (i != 0) return 0;
    o->loaded = 1;
    o->playing = atomic_load(&playing);
    o->bpm = BPM;
    o->pos_s = (now_ms() - t0) / 1000.0;
    snprintf(o->title, sizeof o->title, "Test");
    return 0;
}
static int t_deck_grid(int i, double *beat, double *bar)
{
    *bar = -1;
    if (i != 0) { *beat = -1; return -1; }
    *beat = fmod((now_ms() - t0) / 1000.0 * BPM / 60.0, 1.0);
    return 0;
}
static int t_thread_start(const char *id, const char *name, void *(*fn)(void *), void *arg)
{
    (void)id;
    if (strcmp(name, "flow-beat") != 0) return -1;
    pthread_t t;
    return pthread_create(&t, NULL, fn, arg) == 0 ? 0 : -1;
}
static int t_on_deck_event(const char *id, void (*fn)(int, int, void *), void *user) { (void)id; (void)fn; (void)user; return 0; }
static void (*tap_fn)(const float *, int, void *);
static int t_audio_tap(const char *id, int src, int frames, void (*fn)(const float *, int, void *), void *user)
{
    (void)id; (void)frames;
    if (src == AZ_TAP_DECK(0) && (intptr_t)user == 0) tap_fn = fn;
    return 0;
}
static _Atomic int run = 1;
/* deck 1's audio: a 55 Hz kick for the first 120 ms of every beat while music is on, hats always */
static void *audio_thread(void *arg)
{
    (void)arg;
    float lr[2 * 480];
    while (atomic_load(&run)) {
        double base = (now_ms() - t0) / 1000.0 - 0.010;   /* the 10 ms just played, on the deck's clock */
        for (int i = 0; i < 480; i++) {
            double ts = base + i / 48000.0;
            double ph = fmod(ts * BPM / 60.0, 1.0) * 60.0 / BPM;   /* seconds into the beat */
            double v = 0.04 * sin(ts * 2 * M_PI * 8000);
            if (atomic_load(&music) && ph < 0.12) v += 0.7 * sin(ts * 2 * M_PI * 55);
            lr[2 * i] = lr[2 * i + 1] = (float)v;
        }
        if (tap_fn) tap_fn(lr, 480, (void *)(intptr_t)0);
        struct timespec t = { 0, 10000000 };
        nanosleep(&t, NULL);
    }
    return NULL;
}

static int rx = -1;
typedef struct { double at; int seq, uni; uint8_t dmx[512]; } frame;
/* Every ArtDmx frame for ms; returns how many (out gets them, up to max). */
static int collect(frame *out, int max, double ms)
{
    double end = now_ms() + ms;
    int n = 0;
    uint8_t buf[600];
    while (now_ms() < end) {
        ssize_t r = recv(rx, buf, sizeof buf, MSG_DONTWAIT);
        if (r <= 0) { struct timespec t = { 0, 500000 }; nanosleep(&t, NULL); continue; }
        if (r != LT_PKT || lt_artdmx_universe(buf, (int)r) < 0) continue;
        if (n < max) { out[n].at = now_ms(); out[n].seq = buf[12]; out[n].uni = lt_artdmx_universe(buf, (int)r); memcpy(out[n].dmx, buf + 18, 512); }
        n++;
    }
    return n;
}

static frame fr[2000];
int main(void)
{
    unit();

    rx = socket(AF_INET, SOCK_DGRAM, 0);
    int on = 1;
    setsockopt(rx, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = htons(LT_PORT) };
    if (bind(rx, (struct sockaddr *)&a, sizeof a) != 0) { perror("bind 127.0.0.1:6454"); return 2; }

    t0 = now_ms();
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
    core.audio_tap = t_audio_tap;
    const az_mod_v1 *mod = az_mod_v1_entry();
    int r = mod->init(&core);
    CHECK(r == 0, "init returns 0 (%d)", r);
    pthread_t at;
    pthread_create(&at, NULL, audio_thread, NULL);

    int n = collect(fr, 2000, 2000);
    int seq_ok = 1, uni_ok = 1, bad = 0;
    for (int i = 1; i < n && i < 2000; i++) {
        int want = fr[i - 1].seq == 255 ? 1 : fr[i - 1].seq + 1;
        if (fr[i].seq != want) seq_ok = 0;
    }
    for (int i = 0; i < n && i < 2000; i++) { if (fr[i].uni != 0) uni_ok = 0; if (forbidden(fr[i].dmx)) bad = forbidden(fr[i].dmx); }
    CHECK(n >= 70 && n <= 90, "about 40 frames a second (%d in 2 s)", n);
    CHECK(seq_ok && uni_ok, "sequence counts 1..255 with no gaps, universe 0");
    CHECK(n > 0 && max_rgb(fr[n - 1].dmx) > 40 && !bad, "the rig is lit while the deck plays, laser / hazer / strobes at 0");

    n = collect(fr, 2000, 3000);                      /* settle into the groove */
    atomic_store(&music, 0);                          /* breakdown: no bass */
    n = collect(fr, 2000, 2.75 * 4 * 60000.0 / BPM);  /* 2.75 bars */
    int brk_dim = n > 0 && max_rgb(fr[n - 1].dmx) < 150;
    CHECK(brk_dim, "breakdown: the rig fades down (%d)", n > 0 ? max_rgb(fr[n - 1].dmx) : -1);
    /* the drop on the next beat */
    double beat_ms = 60000.0 / BPM, now = now_ms() - t0;
    double next = ceil(now / beat_ms) * beat_ms;
    while (now_ms() - t0 < next - 2) { struct timespec t = { 0, 500000 }; nanosleep(&t, NULL); }
    atomic_store(&music, 1);
    double drop_at = t0 + next;
    n = collect(fr, 2000, 1200);
    int first = -1, hits = 0;
    for (int i = 0; i < n && i < 2000; i++) if (is_hit(fr[i].dmx)) { if (first < 0) first = i; hits++; }
    double late = first >= 0 ? fr[first].at - drop_at : -1;
    CHECK(first >= 0 && late >= 0 && late <= 60, "the drop: white hit %.0f ms after the kick returns (at most 60)", late);
    CHECK(hits >= 14 && hits <= 22, "the hit lasts about one beat (%d frames)", hits);

    /* another sender (FLOW VJ with Art-Net on) on our universe: the deck steps back */
    int other = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in src = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(0x7f000002), .sin_port = 0 };
    bind(other, (struct sockaddr *)&src, sizeof src);
    struct sockaddr_in dst = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(0x7f000003), .sin_port = htons(LT_PORT) };
    uint8_t zero[512] = { 0 }, pkt[LT_PKT];
    lt_artdmx(pkt, 1, 0, zero);
    int during = 0;
    double end = now_ms() + 1500;
    collect(fr, 2000, 10);
    while (now_ms() < end) {
        sendto(other, pkt, LT_PKT, 0, (struct sockaddr *)&dst, sizeof dst);
        during += collect(fr, 2000, 25);
    }
    CHECK(during <= 4, "another Art-Net sender on universe 0: the deck stays quiet (%d frames in 1.5 s)", during);
    lt_artdmx(pkt, 1, 5, zero);                        /* a sender on another universe doesn't count */
    sendto(other, pkt, LT_PKT, 0, (struct sockaddr *)&dst, sizeof dst);
    double gone = now_ms();
    double back = -1;
    while (now_ms() - gone < 5000) if (collect(fr, 1, 20)) { back = now_ms() - gone; break; }
    CHECK(back > 2800 && back < 3600, "it takes the lights back %.0f ms after that sender stops (3 s)", back);

    /* lights off: a few dark frames, then nothing */
    atomic_store(&lights, 0);
    n = collect(fr, 2000, 3000);
    int dark = 1;
    int last = n < 2000 ? n : 2000;
    for (int i = last - 3; i >= 0 && i < last; i++) for (int k = 0; k < 512; k++) if (fr[i].dmx[k]) dark = 0;
    CHECK(n >= 3 && n < 30 && dark, "lights off: the last frames are black, then the deck stops sending (%d frames in 3 s)", n);
    n = collect(fr, 2000, 500);
    CHECK(n == 0, "and stays silent (%d)", n);

    atomic_store(&run, 0);
    pthread_join(at, NULL);
    printf("%s: %d of %d checks failed\n", fails ? "FAILED" : "ALL PASS", fails, checks);
    return fails ? 1 : 0;
}
