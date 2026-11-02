/* SPDX-License-Identifier: GPL-3.0-only
 *
 * flow-beat 0.6.2: the FLOW Lights panel (panel.h + its glue in flow-beat.c), end to end.
 * Builds flow-beat.c into this test with a fake core: the real worker thread, the touch filter fed
 * like the bus thread feeds it, the UI tick, the painter with a recording az_draw, and the mod's UDP
 * packets received on 127.0.0.1 (vjpad on the flow-beat port, FVC1 on 9103, FVS1 sent to 9104).
 * Also the deck show's new switches in lights.h.                     make -C tests panel
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
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
static void sleep_ms(int ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, NULL); }

/* ---- the fake core ---- */
static int port;
static int view_now = AZ_VIEW_DECK;
static int rows[32], nrows;
static const az_row_v1 *row_def[32];
static int nsurf, shown[4], redraws[4];
static az_rect placed[4];
static az_surface_v1 surf[4];
static az_touch_fn touch_fn;

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
static void t_cfg_set(const char *id, const char *key, int v) { (void)id; (void)key; (void)v; }
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
    if (strcmp(name, "flow-beat") != 0) return -1;
    pthread_t t;
    return pthread_create(&t, NULL, fn, arg) == 0 ? 0 : -1;
}
static int t_on_deck_event(const char *id, void (*fn)(int, int, void *), void *user) { (void)id; (void)fn; (void)user; return 0; }
static int t_register_row(const char *id, const az_row_v1 *r)
{
    (void)id;
    row_def[nrows] = r;
    __atomic_store_n(&rows[nrows], r->kind == AZ_ROW_CHOICE ? r->default_choice : (r->flags & AZ_ROW_DEFAULT_ON) ? 1 : 0, __ATOMIC_RELEASE);
    return __atomic_add_fetch(&nrows, 1, __ATOMIC_ACQ_REL) - 1;
}
static int t_row_value(int h) { return h >= 0 && h < __atomic_load_n(&nrows, __ATOMIC_ACQUIRE) ? __atomic_load_n(&rows[h], __ATOMIC_ACQUIRE) : -1; }
static int t_row_set(int h, int v) { if (h < 0 || h >= nrows) return -1; __atomic_store_n(&rows[h], v, __ATOMIC_RELEASE); return 0; }
static int row_of(const char *key) { for (int i = 0; i < nrows; i++) if (!strcmp(row_def[i]->key, key)) return i; return -1; }
static int t_view(void) { return __atomic_load_n(&view_now, __ATOMIC_ACQUIRE); }
static az_surface *t_surface_create(const char *id, const az_surface_v1 *s)
{
    (void)id;
    if (nsurf >= 4) return NULL;
    surf[nsurf] = *s;
    return (az_surface *)(intptr_t)(++nsurf);
}
static int sidx(az_surface *s) { return (int)(intptr_t)s - 1; }
static void t_surface_place(az_surface *s, az_rect r) { placed[sidx(s)] = r; }
static void t_surface_show(az_surface *s, int on) { shown[sidx(s)] = on; }
static void t_surface_redraw(az_surface *s) { redraws[sidx(s)]++; }
static int t_touch_filter(const char *id, az_touch_fn fn, void *user) { (void)id; (void)user; touch_fn = fn; return 0; }

/* the recording az_draw: everything must land inside the surface */
static az_rect area_now;
static int ops, outside, texts;
static char text_seen[4096];
static void inside(int x, int y, int w, int h)
{
    ops++;
    if (x < area_now.x || y < area_now.y || x + w > area_now.x + area_now.w || y + h > area_now.y + area_now.h) outside++;
}
static void t_fill(az_draw *d, int x, int y, int w, int h, uint32_t c) { (void)d; (void)c; inside(x, y, w, h); }
static void t_rect(az_draw *d, int x, int y, int w, int h, int wd, uint32_t c) { (void)d; (void)wd; (void)c; inside(x, y, w, h); }
static void t_line(az_draw *d, int x0, int y0, int x1, int y1, int wd, uint32_t c) { (void)d; (void)wd; (void)c; inside(x0, y0, x1 - x0, y1 - y0 + 1); }
static int t_text_width(const char *s, int px) { return (int)strlen(s) * px * 6 / 10; }
static void t_text(az_draw *d, int x, int y, const char *s, int px, uint32_t c)
{
    (void)d; (void)c;
    texts++;
    inside(x, y, t_text_width(s, px), px * 3 / 2);
    if (strlen(text_seen) + strlen(s) + 2 < sizeof text_seen) { strcat(text_seen, s); strcat(text_seen, "|"); }
}

/* ---- receive ---- */
static int rx_beat = -1, rx_tap = -1;
static int recv_all(int s, char pk[][256], int max, int wait_ms)
{
    int n = 0;
    double end = now_ms() + wait_ms;
    while (now_ms() < end && n < max) {
        char b[256];
        ssize_t l = recv(s, b, sizeof b - 1, MSG_DONTWAIT);
        if (l > 0) { b[l] = 0; snprintf(pk[n++], 256, "%s", b); continue; }
        sleep_ms(2);
    }
    return n;
}
static void drain(int s) { char b[512]; while (recv(s, b, sizeof b, MSG_DONTWAIT) > 0) {} }
static int count_has(char pk[][256], int n, const char *needle)
{
    int c = 0;
    for (int i = 0; i < n; i++) c += strstr(pk[i], needle) != NULL;
    return c;
}

/* a finger: down, maybe moves, up. The tick runs like the core's UI thread would. */
static int touch(int x, int y, int down) { az_touch t = { 0, x, y, down }; return touch_fn(&t, NULL); }
static int tap(int x, int y)
{
    int r = touch(x, y, 1);
    touch(x, y, 0);
    pn_tick();
    return r;
}
static void center(int id, int *x, int *y)
{
    const pn_btn *b = pn_btn_of(id);
    *x = PN_X + b->x + b->w / 2;
    *y = PN_Y + b->y + b->h / 2;
}
static int tap_id(int id) { int x, y; center(id, &x, &y); return tap(x, y); }
static void paint(void)
{
    area_now = (az_rect){ PN_X, PN_Y, PN_W, PN_H };
    ops = outside = texts = 0;
    text_seen[0] = 0;
    surf[1].on_paint((az_draw *)&area_now, area_now, NULL);
}

/* ---- the deck show's new switches (lights.h) ---- */
static void show_unit(void)
{
    lt_fix fix[LT_MAX_FIX];
    int nf = lt_parse_patch(LT_DEFAULT_PATCH, fix, LT_MAX_FIX, NULL);
    uint8_t a[512], b[512];
    lt_state s;
    lt_in in;
    memset(&in, 0, sizeof in);
    in.playing = 1; in.bpm = 128; in.ph = 0.1; in.bar_beat = 0; in.bass = 0.8; in.dim = 1; in.look = 1; in.dt_ms = 25;

    lt_init(&s); lt_track(&s, &in); lt_render(&s, &in, fix, nf, a);
    in.black = 1; lt_render(&s, &in, fix, nf, b);
    int lit = 0; for (int i = 0; i < 512; i++) lit |= b[i];
    CHECK(!lit, "BLACK: every channel 0");
    in.black = 0;

    in.cut = 0.5; lt_render(&s, &in, fix, nf, b);
    /* SlimPAR @74: master ch1 stays 255, red ch2 halves; Shocker @432 background dimmer (255 = off) stays */
    CHECK(b[73] == 255 && a[74] > 20 && abs(b[74] - a[74] / 2) <= 1, "INTENSITY 50 %%: colour halves (%d -> %d), master stays %d", a[74], b[74], b[73]);
    CHECK(b[432] == 255 && b[433] == a[433], "INTENSITY leaves the Shocker's master/background channels alone");
    in.cut = 0;

    /* follow off: the 4BAR pods all the same, beat to beat */
    in.no_follow = 1;
    lt_render(&s, &in, fix, nf, a);
    int same = a[58 + 3] == a[58 + 6] && a[58 + 6] == a[58 + 9] && a[58 + 9] == a[58 + 12];
    in.bar_beat = 2; in.ph = 0.6;
    lt_render(&s, &in, fix, nf, b);
    CHECK(same && !memcmp(a + 58, b + 58, 15), "FOLLOW BEAT off: a steady wash, no pod chase");
    in.no_follow = 0; in.bar_beat = 0; in.ph = 0.1;

    /* strobe: 8 a second, dark between */
    in.strobe = 1;
    int on = 0, off = 0, edges = 0, was = -1;
    for (int i = 0; i < 200; i++) {                                   /* 1 s in 5 ms steps */
        in.dt_ms = 5; lt_track(&s, &in); lt_render(&s, &in, fix, nf, b);
        int w = b[73 + 5] == 255;                                     /* SlimPAR white */
        int dark = 1; for (int k = 0; k < 512; k++) if (b[k]) { dark = 0; break; }
        on += w; off += dark;
        if (was >= 0 && w && !was) edges++;
        was = w;
    }
    CHECK(edges >= 7 && edges <= 9 && on > 0 && off > 0, "STROBE held: %d flashes in 1 s, dark between", edges);
    in.strobe = 0; in.dt_ms = 25;

    /* a panel hit while nothing plays */
    lt_init(&s);
    lt_in idle = in; idle.playing = 0; idle.ph = -1; idle.bar_beat = -1;
    lt_flash(&s, 0, 0);
    lt_track(&s, &idle); lt_render(&s, &idle, fix, nf, b);
    CHECK(b[73 + 5] == 255 && b[74] == 255, "WHITE with nothing playing: the rig flashes white");
    for (int i = 0; i < 12; i++) lt_track(&s, &idle);
    lt_render(&s, &idle, fix, nf, b);
    CHECK(b[73 + 5] == 0, "and stops after about a beat");
    lt_flash(&s, 0, 1);
    lt_track(&s, &idle); lt_render(&s, &idle, fix, nf, b);
    CHECK(b[73 + 5] == 0 && b[74] == LT_PAL[0][1].r && b[76] == LT_PAL[0][1].b, "COLOR: the accent colour, no white channel");

    /* drop flash off: the drop is found, no hit */
    lt_init(&s);
    in.no_dropflash = 1;
    for (int beat = 0; beat < 40; beat++) {
        in.beat = 1;
        in.bass = beat < 16 || beat >= 26 ? 0.9 : 0.05;
        lt_track(&s, &in);
        in.beat = 0;
        lt_track(&s, &in);
    }
    CHECK(s.drops == 1 && s.hit_ms <= 0 && s.drop_beats == 0, "DROP FLASH off: the drop is seen (%d) but no white hit", s.drops);
    lt_drop_now(&s, 128, 1);
    CHECK(s.flash_ms > 400 && s.drop_beats == 16, "DROP from the panel: a beat of white and 4 bars at full");
    in.no_dropflash = 0;
}

static void unit(void)
{
    int pad, shift;
    CHECK(pn_hit(PN_X + 20, PN_Y + 70) == PN_LOOK1 && pn_hit(PN_X + 860, PN_Y + 10) == PN_CLOSE, "hit test: LOOK 1 and CLOSE");
    CHECK(pn_hit(PN_X + 465, PN_Y + 100) == PN_NONE && pn_hit(100, 100) == PN_NONE, "hit test: gaps and outside are nothing");
    for (int i = 0; i < PN_NBTNS; i++) {
        const pn_btn *b = &PN_BTNS[i];
        int ok = b->x >= 0 && b->y >= 0 && b->x + b->w <= PN_W && b->y + b->h <= PN_H;
        for (int j = 0; j < i && ok; j++) {
            const pn_btn *c = &PN_BTNS[j];
            if (b->x < c->x + c->w && c->x < b->x + b->w && b->y < c->y + c->h && c->y < b->y + b->h) ok = 0;
        }
        if (!ok) { CHECK(0, "button %s inside the panel, not overlapping", b->label); }
    }
    CHECK(1, "%d buttons inside the panel, none overlapping", PN_NBTNS);
    CHECK(PN_X + PN_W <= 1090 && PN_Y + PN_H <= 560, "the panel leaves Beat FX and the deck strips free");
    CHECK(pn_level_at(PN_Y + PN_LEVEL_TOP) == 100 && pn_level_at(PN_Y + PN_LEVEL_BOT) == 0 && pn_level_at(0) == 100 && pn_level_at(9999) == 0,
          "fader: top 100, bottom 0, clamped");
    CHECK(pn_vjpad(PN_LOOK3, &pad, &shift) && pad == 3 && !shift, "LOOK 3 = VJ pad 3");
    CHECK(pn_vjpad(PN_WHITE, &pad, &shift) && pad == 1 && shift && pn_vjpad(PN_COLOR, &pad, &shift) && pad == 5 && shift,
          "WHITE / COLOR = SHIFT + pad 1 / 5");
    static const char *const acts[] = { "next", "prev", "hold", "safe", "auto", "drop", "black", "fog", "strobe", "strobe_off" };
    int all = 1;
    for (int id = 1; id < PN_IDS; id++) {
        const char *a = pn_act(id);
        if (!a) continue;
        int known = 0;
        for (size_t k = 0; k < sizeof acts / sizeof acts[0]; k++) known |= !strcmp(a, acts[k]);
        all &= known;
    }
    CHECK(all, "every FVC1 act is one FlowVJ's deck_status.py accepts");
    pn_fv fv;
    const char *t = "FVS1\nseq=3\nlook=Tunnel \xc3\xa9\x01\nhold=1\nsafe=0\nauto=1\nblack=1\n";
    CHECK(pn_fv_parse(t, (int)strlen(t), &fv) && !strcmp(fv.look, "Tunnel \xe9") && fv.hold && !fv.safe && fv.autop && fv.black,
          "FVS1: look (UTF-8 to Latin-1, control bytes dropped), hold, auto, black");
    CHECK(!pn_fv_parse("FVC1 1 next", 11, &fv), "not FVS1: ignored");
    char b[200];
    CHECK(pn_vjpad_json(b, sizeof b, 3, 1, 0, PN_N_BASE + 1) > 0 && strstr(b, "\"type\":\"vjpad\",\"side\":1,\"pad\":3,\"down\":1,\"shift\":0"),
          "vjpad packet is Pad Lights' shape");
    CHECK(pn_vjpad_json(b, 20, 3, 1, 0, 1) == 0 && pn_fvc(b, 6, 1, "strobe") == 0, "short buffers: nothing, never cut");
}

int main(void)
{
    unit();
    show_unit();

    rx_beat = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = 0 };
    if (bind(rx_beat, (struct sockaddr *)&a, sizeof a) != 0) { perror("bind"); return 2; }
    socklen_t al = sizeof a;
    getsockname(rx_beat, (struct sockaddr *)&a, &al);
    port = ntohs(a.sin_port);
    rx_tap = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in at = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = htons(PN_TAP_PORT) };
    if (bind(rx_tap, (struct sockaddr *)&at, sizeof at) != 0) { perror("bind 9103"); return 2; }

    static az_core_v1 core;
    memset(&core, 0, sizeof core);
    core.abi = AZ_MOD_ABI;
    core.size = sizeof core;
    core.log = t_log;
    core.cfg_get = t_cfg_get;
    core.cfg_set = t_cfg_set;
    core.cfg_get_str = t_cfg_get_str;
    core.deck_info = t_deck_info;
    core.deck_grid = t_deck_grid;
    core.thread_start = t_thread_start;
    core.on_deck_event = t_on_deck_event;
    core.register_row = t_register_row;
    core.row_value = t_row_value;
    core.row_set = t_row_set;
    core.view = t_view;
    core.surface_create = t_surface_create;
    core.surface_place = t_surface_place;
    core.surface_show = t_surface_show;
    core.surface_redraw = t_surface_redraw;
    core.touch_filter = t_touch_filter;
    core.fill_rect = t_fill;
    core.draw_rect = t_rect;
    core.draw_line = t_line;
    core.draw_text = t_text;
    core.text_width = t_text_width;

    const az_mod_v1 *mod = az_mod_v1_entry();
    CHECK(mod->init(&core) == 0, "init");
    CHECK(nsurf == 2 && touch_fn && mod->tick, "two surfaces (button + panel), a touch filter, a tick");
    CHECK(surf[0].views == AZ_VIEW_BIT(AZ_VIEW_DECK) && surf[1].views == AZ_VIEW_BIT(AZ_VIEW_DECK), "both on the deck screen only");
    int r_panel = row_of("lights-panel"), r_look = row_of("artnet-look"), r_lights = row_of("artnet");
    CHECK(r_panel >= 0 && rows[r_panel] == 1, "row \"FLOW Lights button\", on by default");

    mod->tick();
    CHECK(shown[0] == 1 && shown[1] == 0, "the LIGHTS button shows, the panel doesn't");
    CHECK(placed[0].x == PN_BTN_X && placed[1].x == PN_X && placed[1].w == PN_W, "placed from the tick (UI thread)");

    /* not the deck screen: every touch is the deck's */
    view_now = AZ_VIEW_BROWSE;
    CHECK(tap(PN_BTN_X + 10, PN_BTN_Y + 10) == AZ_TOUCH_PASS && !shown[1], "browse screen: the LIGHTS button's spot is the deck's");
    view_now = AZ_VIEW_DECK;

    /* a touch in the panel's area while it is closed is the deck's */
    int x, y;
    center(PN_LOOK1, &x, &y);
    CHECK(tap(x, y) == AZ_TOUCH_PASS, "panel closed: its area is the deck's (waveforms)");

    CHECK(tap(PN_BTN_X + 40, PN_BTN_Y + 12) == AZ_TOUCH_TAKE && shown[1] == 1 && redraws[1] > 0, "LIGHTS opens the panel and paints it");
    paint();
    CHECK(ops > 60 && outside == 0, "the panel paints %d shapes, all inside it", ops);
    CHECK(strstr(text_seen, "FLOW LIGHTS") && strstr(text_seen, "INTENSITY") && strstr(text_seen, "DECK SHOW: OFF"), "title, fader, deck show state");

    drain(rx_beat); drain(rx_tap);
    CHECK(tap_id(PN_LOOK3) == AZ_TOUCH_TAKE, "LOOK 3 tapped");
    char pk[64][256];
    int n = recv_all(rx_beat, pk, 64, 300);
    int down = count_has(pk, n, "\"type\":\"vjpad\",\"side\":1,\"pad\":3,\"down\":1,\"shift\":0");
    int up = count_has(pk, n, "\"type\":\"vjpad\",\"side\":1,\"pad\":3,\"down\":0,\"shift\":0");
    CHECK(down == 2 && up == 2, "LOOK 3: vjpad pad 3 down and up, each twice (%d, %d)", down, up);
    unsigned nn = 0;
    for (int i = 0; i < n; i++) { char *p = strstr(pk[i], "\"n\":"); if (p && strstr(pk[i], "vjpad")) nn = (unsigned)strtoul(p + 4, NULL, 10); }
    CHECK(nn > PN_N_BASE, "n is in the panel's own range (%u)", nn);

    tap_id(PN_WHITE); tap_id(PN_COLOR);
    n = recv_all(rx_beat, pk, 64, 300);
    CHECK(count_has(pk, n, "\"pad\":1,\"down\":1,\"shift\":1") == 2 && count_has(pk, n, "\"pad\":5,\"down\":1,\"shift\":1") == 2,
          "WHITE / COLOR: SHIFT + pad 1 / 5");

    drain(rx_tap);
    tap_id(PN_NEXT); sleep_ms(20); tap_id(PN_HOLD); sleep_ms(20); tap_id(PN_FOG); sleep_ms(20); tap_id(PN_DROP);
    n = recv_all(rx_tap, pk, 64, 300);
    unsigned seq = n ? (unsigned)strtoul(pk[0] + 5, NULL, 10) : 0;
    CHECK(n == 4 && !strncmp(pk[0], "FVC1 ", 5) && strstr(pk[0], " next") && strstr(pk[1], " hold") && strstr(pk[2], " fog") && strstr(pk[3], " drop"),
          "NEXT HOLD FOG DROP: FVC1 to UDP 9103, in order (%d)", n);
    CHECK(seq >= PN_SEQ_BASE, "FVC1 seq in the panel's own range (%u)", seq);

    /* STROBE held 2.6 s: said again every second, strobe_off on release */
    center(PN_STROBE, &x, &y);
    drain(rx_tap);
    touch(x, y, 1);
    for (int i = 0; i < 26; i++) { pn_tick(); sleep_ms(100); touch(x + (i & 1), y, 1); }
    touch(x, y, 0);
    pn_tick();
    n = recv_all(rx_tap, pk, 64, 300);
    int st = count_has(pk, n, " strobe"), off = count_has(pk, n, " strobe_off");
    CHECK(st - off >= 3 && off == 1 && strstr(pk[n - 1], "strobe_off"), "STROBE held 2.6 s: strobe x%d, then strobe_off", st - off);

    /* colour, intensity, switches: the deck show */
    tap_id(PN_COL2);
    CHECK(rows[r_look] == 2, "FLOW colour sets the \"Deck lights colours\" row");
    tap_id(PN_SHOW);
    CHECK(rows[r_lights] == 1, "DECK SHOW turns \"Lights from this deck\" on");
    tap_id(PN_FOLLOW);
    CHECK(!__atomic_load_n(&pn_follow, __ATOMIC_ACQUIRE), "FOLLOW BEAT off");
    tap_id(PN_FOLLOW);
    const pn_btn *lb = pn_btn_of(PN_LEVEL);
    touch(PN_X + lb->x + 90, PN_Y + PN_LEVEL_BOT, 1);
    for (int yy = PN_Y + PN_LEVEL_BOT; yy >= PN_Y + (PN_LEVEL_TOP + PN_LEVEL_BOT) / 2; yy -= 7) touch(PN_X + lb->x + 300, yy, 1);  /* the finger slides off sideways */
    touch(PN_X + lb->x + 90, PN_Y + (PN_LEVEL_TOP + PN_LEVEL_BOT) / 2, 1);
    touch(0, 0, 0);
    pn_tick();
    int lv = __atomic_load_n(&pn_level, __ATOMIC_ACQUIRE);
    CHECK(lv >= 48 && lv <= 52, "INTENSITY dragged to %d %%", lv);
    paint();
    CHECK(strstr(text_seen, "50%") && strstr(text_seen, "DECK SHOW: ON") && outside == 0, "the panel shows 50%% and DECK SHOW: ON");
    __atomic_store_n(&rows[r_lights], 0, __ATOMIC_RELEASE);

    /* outside the panel while it is up: the deck's */
    CHECK(tap(1150, 300) == AZ_TOUCH_PASS && tap(100, 700) == AZ_TOUCH_PASS, "while up: Beat FX and the deck strips stay the deck's");

    /* FlowVJ's state on 9104 */
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in fv = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = htons(PN_FV_PORT) };
    const char *fvs = "FVS1\nseq=9\nlook=TUNNEL\nlights=WASH\nhold=1\nsafe=1\nauto=0\nblack=0\ntap=9103\n";
    for (int i = 0; i < 5; i++) { sendto(s, fvs, strlen(fvs), 0, (struct sockaddr *)&fv, sizeof fv); sleep_ms(60); pn_tick(); }
    sleep_ms(150);
    pn_tick();
    paint();
    const pn_view *v = &pn_vw[pn_vw_cur];
    CHECK(strstr(text_seen, "FlowVJ: TUNNEL") && (v->lit >> PN_HOLD & 1) && (v->lit >> PN_SAFE & 1), "FVS1 on 9104: look TUNNEL, HOLD and SAFE lit");
    close(s);

    /* timing: the touch filter runs on the bus thread */
    double t0 = now_ms();
    for (int i = 0; i < 20000; i++) { touch(PN_X + 400, PN_Y + 300, 1); touch(PN_X + 400, PN_Y + 300, 0); }
    double per = (now_ms() - t0) * 1000.0 / 40000;
    CHECK(per < 20, "touch filter: %.2f us a call (bus thread budget 200 us)", per);
    t0 = now_ms();
    for (int i = 0; i < 200; i++) pn_tick();
    CHECK((now_ms() - t0) * 1000.0 / 200 < 100, "tick: %.1f us", (now_ms() - t0) * 1000.0 / 200);
    pn_tapped = 0;

    CHECK(tap_id(PN_CLOSE) == AZ_TOUCH_TAKE && shown[1] == 0, "CLOSE hides the panel");
    center(PN_LOOK1, &x, &y);
    CHECK(tap(x, y) == AZ_TOUCH_PASS, "closed again: the waveforms are the deck's");

    /* the deck's touch panel: a passed touch's lift goes to EP147, never to us */
    touch(100, 700, 1);
    sleep_ms(150);
    CHECK(tap(PN_BTN_X + 40, PN_BTN_Y + 12) == AZ_TOUCH_TAKE && shown[1] == 1, "a passed touch that never lifted for us: LIGHTS still opens");
    sleep_ms(150);
    CHECK(tap(PN_BTN_X + 40, PN_BTN_Y + 12) == AZ_TOUCH_TAKE && shown[1] == 0, "and closes");

    /* the emulator: no touch panel, so no touch filter calls; the core hands the surface X clicks */
    sleep_ms(450);
    az_event dn = { AZ_EV_DOWN, 0, PN_BTN_X + 40, PN_BTN_Y + 12 }, tp = dn, upe = dn;
    tp.kind = AZ_EV_TAP; upe.kind = AZ_EV_UP;
    surf[0].on_input(&dn, NULL); surf[0].on_input(&tp, NULL); surf[0].on_input(&upe, NULL); pn_tick();
    CHECK(shown[1] == 1, "surface DOWN + TAP + UP on LIGHTS (the emulator's route): opens, once");
    drain(rx_beat);
    center(PN_LOOK5, &x, &y);
    az_event l5 = { AZ_EV_DOWN, 0, x, y }, l5t = l5, l5u = l5;
    l5t.kind = AZ_EV_TAP; l5u.kind = AZ_EV_UP;
    surf[1].on_input(&l5, NULL); surf[1].on_input(&l5t, NULL); surf[1].on_input(&l5u, NULL); pn_tick();
    n = recv_all(rx_beat, pk, 64, 300);
    CHECK(count_has(pk, n, "\"pad\":5,\"down\":1,\"shift\":0") == 2, "surface tap on LOOK 5: vjpad pad 5, once (sent twice)");
    center(PN_STROBE, &x, &y);
    drain(rx_tap);
    az_event sd = { AZ_EV_DOWN, 0, x, y }, st2 = sd, su = sd;
    st2.kind = AZ_EV_TAP; su.kind = AZ_EV_UP;
    surf[1].on_input(&sd, NULL); surf[1].on_input(&st2, NULL); pn_tick(); sleep_ms(50);
    surf[1].on_input(&su, NULL); pn_tick();
    n = recv_all(rx_tap, pk, 64, 300);
    CHECK(count_has(pk, n, " strobe_off") == 1 && count_has(pk, n, " strobe") == 2, "surface STROBE: strobe on DOWN, strobe_off on UP");
    /* the deck: the touch filter took it, and if the core also hands the surface the click, it counts once */
    sleep_ms(450);
    tap(PN_BTN_X + 40, PN_BTN_Y + 12);
    surf[0].on_input(&dn, NULL); surf[0].on_input(&tp, NULL); surf[0].on_input(&upe, NULL); pn_tick();
    CHECK(shown[1] == 0, "touch filter + surface click for the same tap: one toggle (closed)");
    sleep_ms(450);
    tap(PN_BTN_X + 40, PN_BTN_Y + 12);
    az_event back = { AZ_EV_BACK, 0, 0, 0 };
    CHECK(surf[1].on_input(&back, NULL) == 1 && (pn_tick(), shown[1] == 0), "BACK closes the panel");
    CHECK(surf[1].on_input(&back, NULL) == 0, "BACK with the panel shut is EP147's");

    sleep_ms(450);
    tap(PN_BTN_X + 40, PN_BTN_Y + 12);
    view_now = AZ_VIEW_BROWSE; pn_tick(); view_now = AZ_VIEW_DECK; pn_tick();
    CHECK(shown[1] == 0, "leaving the deck screen (BACK to browse) shuts the panel");

    __atomic_store_n(&rows[r_panel], 0, __ATOMIC_RELEASE);
    pn_tick();
    CHECK(shown[0] == 0 && tap(PN_BTN_X + 40, PN_BTN_Y + 12) == AZ_TOUCH_PASS, "row off: no button, its spot is the deck's");

    printf("%s: %d of %d checks failed\n", fails ? "FAILED" : "ALL PASS", fails, checks);
    return fails ? 1 : 0;
}
