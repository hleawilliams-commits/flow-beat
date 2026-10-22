/* SPDX-License-Identifier: GPL-3.0-only
 *
 * flow-beat: sends each deck's beat, tempo, play state and track to FLOW VJ over the network.
 *
 * A worker thread reads every deck with deck_info and deck_grid every 5 ms and sends small
 * JSON datagrams over UDP (default: broadcast to port 9100, so no address needs typing):
 *
 *   {"flowbeat":1,"type":"beat","seq":N,"d":1,"bpm":128.00,"n":17}
 *        the moment a playing deck's beat phase wraps (a new beat starts)
 *   {"flowbeat":1,"type":"state","seq":N,"decks":[{"d":1,"loaded":1,"play":1,
 *        "bpm":128.00,"ph":0.4321,"pos":61.250,"id":1234,"start":3}, ...]}
 *        about every 20 ms; ph is the position in the current beat (0..1, -1 = no grid);
 *        pos is the playhead in seconds (-1 unknown) and id the rekordbox track id (0 unknown),
 *        so the receiver can find the bar (the "1") in the track's rekordbox beat grid;
 *        start orders the playing decks (higher = started later, 0 = not playing)
 *   {"flowbeat":1,"type":"track","seq":N,"d":1,"title":"...","artist":"..."}
 *        when a deck's track changes, and every 2 s for each loaded deck
 *   0.3.0: every state deck also carries "lv" (the deck's audio level, peak of 10 ms RMS since the
 *        last state, 0..1) and "lo" (the same for its bass, below ~150 Hz): the deck's OWN audio, tapped
 *        before the hardware mixer (faders / EQ / crossfader are not in it), so FLOW's lights get each
 *        deck's kick without a microphone
 *   0.4.0: {"flowbeat":1,"type":"wave","seq":N,"d":1,"sig":S,"len":L,"cnt":C,"title":"...","artist":"..."}
 *        then C binary datagrams "FBW1" (24-byte header: magic, ver 1, deck, 0, sig, total, idx, cnt,
 *        crc32 of the whole blob; then up to 1100 bytes): the loaded track's rekordbox analysis read
 *        off the deck's own USB (PQTZ beat grid, PWV3 waveform, PSSI phrases, as raw tags), so FLOW
 *        knows a guest USB's drops and waveform like its own. Sent on load, twice more, then every 10 s.
 *   0.5.0 (Cue Map): the blob also carries the track's hot and memory cues (PCOB from the .DAT, PCO2 with
 *        colours and comments from the .EXT), so FLOW can put its drops on the DJ's own cues.
 *   {"flowbeat":1,"type":"mixraw","seq":N,"b":"<hex>"}
 *        only while "Send mixer data (test)" is on: the mixer/panel input frame whenever it changes
 *        (at most 20 a second), to map which bytes the faders, EQ and crossfader move
 *   0.5.0 (FLOW Mixer Link): {"flowbeat":1,"type":"mixer","seq":N,
 *        "ch":[{"c":1,"fader":0.812,"trim":0.5,"hi":0.5,"mid":0.5,"low":0.5,"color":0.5}, ...4],
 *        "xf":0.5,"master":0.7,"mhi":..,"mmid":..,"mlow":..,"booth":..,"cfx":"FILTER","cfxp":0.5,
 *        "bfx":{"sel":3,"on":1,"ch":"MASTER","lvl":0.5,"time":0},"xpad":{"pos":62,"touch":1}}
 *        the mixer panel's knobs and faders (0..1) while "Send mixer to FLOW" is on: when one
 *        moves (at most about 30 a second) and every second. Channel c is the mixer channel
 *        (1..4), cfx the lit Sound Color FX ("" none, "?" not pressed since EP147 started),
 *        bfx.sel the Beat FX SELECT position (0..15), bfx.on the FX's active latch. See mixer.h.
 *
 * seq counts every packet, so the receiver can spot lost or reordered ones. The receiver
 * times beats by its own clock when packets arrive: the deck's clock is not sent.
 * Decks are numbered 1..4 as on the deck. The deck's own grid has no bar position (the "1"): the
 * receiver gets it from rekordbox's grid for the track, using pos and id.
 * Nothing is sent while "Send beat to FLOW" is off.
 *
 * Settings in MODS/azmods.cfg on the USB, read every 2 s:
 *   flow-beat:host = "192.168.1.20"     where to send (an IPv4 address; default: broadcast on
 *                                       every network the deck is on, Wi-Fi and LINK)
 *   flow-beat:port = 9100
 */
#include <arpa/inet.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <math.h>
#include <stdatomic.h>

#include "azmod-mod.h"
#include "mixer.h"

#define MOD_ID "flow-beat"
#define NDECKS 4
#define POLL_MS 5            /* beat edges are found within 5 ms */
#define STATE_STEPS 4        /* a state packet every 4 steps: about 50 a second */
#define TRACK_STEPS 400      /* titles repeated about every 2 s, so a late receiver learns them */
#define CFG_STEPS 400        /* settings re-read about every 2 s */
#define DEFAULT_HOST "255.255.255.255"
#define DEFAULT_PORT 9100
#define MIXER_STEPS 6        /* mixer packets at most every 30 ms (0.5.0) */
#define MIXER_KEEP 200       /* and every second even when nothing moved */

static const az_core_v1 *C;
static int row_send = -1;    /* the "Send beat to FLOW" toggle */
static int row_sniff = -1;   /* "Send mixer data (test)" (0.3.0), off by default */
static int row_mixer = -1;   /* "Send mixer to FLOW" (0.5.0), on by default */
static int row_wave = -1;    /* "Send track analysis to FLOW" (0.4.0), on by default */
static unsigned wave_seq_base = 0x40000000u;   /* the wave thread's own seq range, so the two threads never collide */
static int track_dirty;      /* set by deck events (UI thread), read by the worker: atomics */

/* ---- JSON text --------------------------------------------------------------------------- */

/* Appends s as a JSON string (quotes included), escaping what JSON needs. */
static int put_str(char *buf, int pos, int cap, const char *s)
{
    if (pos >= cap - 2) return pos;
    buf[pos++] = '"';
    for (; *s && pos < cap - 8; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            buf[pos++] = '\\';
            buf[pos++] = (char)c;
        } else if (c < 0x20) {
            pos += snprintf(buf + pos, (size_t)(cap - pos), "\\u%04x", c);
        } else {
            buf[pos++] = (char)c;
        }
    }
    buf[pos++] = '"';
    buf[pos] = 0;
    return pos;
}

/* ---- the network ------------------------------------------------------------------------- */

/* 0.4.0: two threads send now (beat + wave): one tiny spinlock guards the socket and the destination list */
static atomic_flag net_lock = ATOMIC_FLAG_INIT;
static void net_on(void) { while (atomic_flag_test_and_set_explicit(&net_lock, memory_order_acquire)) { struct timespec t = { 0, 50000 }; nanosleep(&t, NULL); } }
static void net_off(void) { atomic_flag_clear_explicit(&net_lock, memory_order_release); }
static int sock = -1;
static char dest_host[64];
static int dest_port;

/* Where packets go. With the default host (broadcast), one address per network the deck is
 * on (each interface's own broadcast, e.g. 192.168.1.255 on Wi-Fi and the LINK network's),
 * because 255.255.255.255 leaves by one interface only: on the AZ that can be the LINK port
 * while FLOW listens on Wi-Fi. A host set in azmods.cfg is used alone. */
#define MAX_DESTS 8
static struct sockaddr_in dests[MAX_DESTS];
static int ndests;

static void add_dest(struct in_addr a, int port)
{
    for (int i = 0; i < ndests; i++)
        if (dests[i].sin_addr.s_addr == a.s_addr) return;
    if (ndests >= MAX_DESTS) return;
    memset(&dests[ndests], 0, sizeof dests[ndests]);
    dests[ndests].sin_family = AF_INET;
    dests[ndests].sin_port = htons((uint16_t)port);
    dests[ndests].sin_addr = a;
    ndests++;
}

/* Logs only from init (quiet = 0): the worker re-reads silently. */
static void read_cfg(int quiet)
{
    char host[64];
    C->cfg_get_str(MOD_ID, "host", host, (int)sizeof host, DEFAULT_HOST);
    int port = C->cfg_get(MOD_ID, "port", DEFAULT_PORT);
    if (port < 1 || port > 65535) port = DEFAULT_PORT;

    struct in_addr a;
    if (inet_pton(AF_INET, host, &a) != 1) {
        if (!quiet)
            C->log(MOD_ID, "host \"%s\" is not an IPv4 address: broadcasting", host);
        snprintf(host, sizeof host, "%s", DEFAULT_HOST);
        inet_pton(AF_INET, host, &a);
    }

    net_on();
    int changed = strcmp(host, dest_host) != 0 || port != dest_port;
    int old_n = ndests;
    struct sockaddr_in old[MAX_DESTS];
    memcpy(old, dests, sizeof old);
    ndests = 0;
    if (a.s_addr == htonl(INADDR_BROADCAST)) {
        struct ifaddrs *ifs = NULL;
        if (getifaddrs(&ifs) == 0) {
            for (struct ifaddrs *i = ifs; i; i = i->ifa_next) {
                if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
                if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
                if (!(i->ifa_flags & IFF_BROADCAST) || !i->ifa_broadaddr) continue;
                add_dest(((struct sockaddr_in *)i->ifa_broadaddr)->sin_addr, port);
            }
            freeifaddrs(ifs);
        }
    }
    if (ndests == 0) add_dest(a, port);   /* a set host, or no network found yet */

    if (!changed && ndests == old_n && memcmp(old, dests, sizeof dests[0] * (size_t)ndests) == 0) {
        net_off();
        return;
    }
    snprintf(dest_host, sizeof dest_host, "%s", host);
    dest_port = port;
    if (!quiet || changed) {
        for (int i = 0; i < ndests; i++) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &dests[i].sin_addr, ip, sizeof ip);
            C->log(MOD_ID, "sending to %s:%d", ip, dest_port);
        }
    }
    net_off();
}

static void send_pkt_locked(const char *buf, int len);
static void send_pkt(const char *buf, int len)
{
    net_on();
    send_pkt_locked(buf, len);
    net_off();
}

static void send_pkt_locked(const char *buf, int len)
{
    if (sock < 0) {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) return;
        int on = 1;
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    }
    int sent = 0;
    for (int i = 0; i < ndests; i++) {
        if (sendto(sock, buf, (size_t)len, MSG_DONTWAIT, (const struct sockaddr *)&dests[i],
                   sizeof dests[i]) >= 0)
            sent = 1;
        else if (errno == EAGAIN || errno == EWOULDBLOCK)
            sent = 1;   /* busy, not broken */
    }
    if (!sent && ndests > 0) {
        /* no network yet, or it changed: try a fresh socket next time; never wait */
        close(sock);
        sock = -1;
    }
}

/* ---- each deck's own audio (0.3.0) ------------------------------------------------------- */
/* Tap threads write, the worker reads and resets: the peak since the last state packet. */
static _Atomic float lvl[NDECKS], low[NDECKS];
static float lp_l[NDECKS], lp_r[NDECKS];   /* one-pole low-pass state, touched only by that deck's tap */
#define TAP_FRAMES 480                     /* ~10 ms at 48 kHz */
static void meter(const float *lr, int frames, void *user)
{
    int d = (int)(intptr_t)user;
    if (d < 0 || d >= NDECKS || frames <= 0) return;
    const float a = 0.0194f;               /* ~150 Hz at 48 kHz (1 - exp(-2*pi*150/48000)) */
    float l = lp_l[d], r = lp_r[d];
    double sum = 0, bass = 0;
    for (int i = 0; i < frames; i++) {
        float x = lr[2 * i], y = lr[2 * i + 1];
        l += a * (x - l); r += a * (y - r);
        sum += 0.5 * (x * x + y * y);
        bass += 0.5 * (l * l + r * r);
    }
    lp_l[d] = l; lp_r[d] = r;
    float v = (float)sqrt(sum / frames), b = (float)sqrt(bass / frames) * 1.6f;
    if (v > 1.f) v = 1.f;
    if (b > 1.f) b = 1.f;
    if (v > atomic_load_explicit(&lvl[d], memory_order_relaxed)) atomic_store_explicit(&lvl[d], v, memory_order_relaxed);
    if (b > atomic_load_explicit(&low[d], memory_order_relaxed)) atomic_store_explicit(&low[d], b, memory_order_relaxed);
}

/* ---- the mixer panel's input frame (0.3.0 sniffer, 0.5.0 Mixer Link) ----------------------- */
/* The bus thread publishes each changed frame under a sequence lock (odd while writing); the
 * worker copies it and tries again later if the copy raced a write. Words, so every access is
 * atomic (sequentially consistent: 33 stores per changed frame, at most ~16 frames a second). */
#define MIX_MAX 128
#define MIX_WORDS (MIX_MAX / 4)
static _Atomic uint32_t mix_words[MIX_WORDS];
static _Atomic int mix_len;
static _Atomic unsigned mix_gen;
static int mix_filter(int bus, unsigned char *frame, int len, void *user)
{
    static unsigned char prev[MIX_MAX];   /* only the bus thread touches it */
    static int prev_len;
    (void)bus; (void)user;
    if (!frame || len <= 0) return AZ_FRAME_OK;
    if (len > MIX_MAX) len = MIX_MAX;
    if (prev_len == len && memcmp(prev, frame, (size_t)len) == 0) return AZ_FRAME_OK;
    memcpy(prev, frame, (size_t)len);
    prev_len = len;
    unsigned g = atomic_load(&mix_gen);
    atomic_store(&mix_gen, g + 1);
    for (int w = 0; w < MIX_WORDS; w++) {
        uint32_t v;
        memcpy(&v, prev + 4 * w, 4);
        atomic_store(&mix_words[w], v);
    }
    atomic_store(&mix_len, len);
    atomic_store(&mix_gen, g + 2);
    return AZ_FRAME_OK;
}

/* The latest frame into out (MIX_MAX bytes): its length, 0 if none yet or a write was in progress. */
static int mix_read(unsigned char *out, unsigned *gen)
{
    unsigned g1 = atomic_load(&mix_gen);
    if (g1 & 1u) return 0;
    int len = atomic_load(&mix_len);
    for (int w = 0; w < MIX_WORDS; w++) {
        uint32_t v = atomic_load(&mix_words[w]);
        memcpy(out + 4 * w, &v, 4);
    }
    if (atomic_load(&mix_gen) != g1) return 0;
    *gen = g1;
    return len;
}

/* ---- the worker -------------------------------------------------------------------------- */

static void *worker(void *arg)
{
    (void)arg;
    double last[NDECKS];
    unsigned beats[NDECKS] = { 0 };
    unsigned since[NDECKS] = { 0 };   /* 0 = not playing, else the play-start order */
    unsigned starts = 0;
    uint32_t track_sig[NDECKS] = { 0 };
    unsigned step = 0;
    unsigned seq = 0;
    unsigned mix_sent = 0;            /* the frame generation the sniffer last sent */
    unsigned mix_seen = 0;            /* and the one Mixer Link last decoded */
    unsigned mix_last_sent = 0;       /* the step Mixer Link last sent at */
    unsigned char mix[MIX_MAX];
    mixer_latch latch;
    mixer_state ms, ms_sent;
    int ms_have = 0;
    mixer_latch_init(&latch);
    char buf[1400];
    for (int d = 0; d < NDECKS; d++) last[d] = -1.0;

    for (;;) {
        step++;
        int on = row_send >= 0 ? C->row_value(row_send) == 1 : C->cfg_get(MOD_ID, "send", 1) != 0;
        if (on && step % CFG_STEPS == 1) read_cfg(1);

        az_deck_info di[NDECKS];
        double ph[NDECKS];
        int force_track = step % TRACK_STEPS == 1 || __atomic_exchange_n(&track_dirty, 0, __ATOMIC_ACQ_REL);
        for (int d = 0; d < NDECKS; d++) {
            memset(&di[d], 0, sizeof di[d]);
            di[d].size = (int)sizeof di[d];
            if (C->deck_info(d, &di[d]) != 0) di[d].loaded = 0;
            int playing = di[d].loaded == 1 && di[d].playing == 1;
            if (playing && !since[d]) {
                since[d] = ++starts;
                beats[d] = 0;
            } else if (!playing) {
                since[d] = 0;
            }

            ph[d] = -1.0;
            double bar = -1.0;
            if (di[d].loaded != 1 || C->deck_grid(d, &ph[d], &bar) != 0) ph[d] = -1.0;

            /* a new beat: the phase wraps from near 1 back to near 0 */
            if (playing && ph[d] >= 0.0 && last[d] >= 0.0 && ph[d] < last[d] - 0.5) {
                beats[d]++;
                if (on) {
                    int n = snprintf(buf, sizeof buf,
                                     "{\"flowbeat\":1,\"type\":\"beat\",\"seq\":%u,"
                                     "\"d\":%d,\"bpm\":%.2f,\"n\":%u}",
                                     seq++, d + 1, di[d].bpm, beats[d]);
                    send_pkt(buf, n);
                }
            }
            last[d] = playing ? ph[d] : -1.0;

            /* the track: a cheap signature of the title and artist, to send changes at once */
            uint32_t sig = 2166136261u;
            for (const char *p = di[d].title; *p; p++) sig = (sig ^ (unsigned char)*p) * 16777619u;
            for (const char *p = di[d].artist; *p; p++) sig = (sig ^ (unsigned char)*p) * 16777619u;
            if (di[d].loaded != 1) sig = 0;
            if (on && di[d].loaded == 1 && (sig != track_sig[d] || force_track)) {
                int n = snprintf(buf, sizeof buf,
                                 "{\"flowbeat\":1,\"type\":\"track\",\"seq\":%u,\"d\":%d,\"title\":",
                                 seq++, d + 1);
                n = put_str(buf, n, 700, di[d].title);
                n += snprintf(buf + n, sizeof buf - (size_t)n, ",\"artist\":");
                n = put_str(buf, n, 1300, di[d].artist);
                n += snprintf(buf + n, sizeof buf - (size_t)n, "}");
                send_pkt(buf, n);
            }
            track_sig[d] = sig;
        }

        if (on && step % STATE_STEPS == 0) {
            int n = snprintf(buf, sizeof buf, "{\"flowbeat\":1,\"type\":\"state\",\"seq\":%u,\"decks\":[",
                             seq++);
            for (int d = 0; d < NDECKS; d++) {
                float lv = atomic_exchange_explicit(&lvl[d], 0.f, memory_order_relaxed);
                float lo = atomic_exchange_explicit(&low[d], 0.f, memory_order_relaxed);
                n += snprintf(buf + n, sizeof buf - (size_t)n,
                              "%s{\"d\":%d,\"loaded\":%d,\"play\":%d,\"bpm\":%.2f,\"ph\":%.4f,\"pos\":%.3f,\"id\":%u,\"start\":%u,\"lv\":%.3f,\"lo\":%.3f}",
                              d ? "," : "", d + 1, di[d].loaded == 1, di[d].loaded == 1 && di[d].playing == 1,
                              di[d].loaded == 1 ? di[d].bpm : 0.0, ph[d],
                              di[d].loaded == 1 ? di[d].pos_s : -1.0, (unsigned)di[d].content_id, since[d], lv, lo);
            }
            n += snprintf(buf + n, sizeof buf - (size_t)n, "]}");
            send_pkt(buf, n);
        }

        /* the mixer: decode every new frame (the Color FX / FX channel latches need each
         * press), send the state when it moved, at most every 30 ms, and every second (0.5.0) */
        unsigned g = 0;
        int mlen = 0;
        if (step % 2 == 0) {
            mlen = mix_read(mix, &g);
            if (mlen >= MIXER_FRAME && g != mix_seen && mixer_decode(mix, mlen, &latch, &ms) == 0) {
                mix_seen = g;
                ms_have = 1;
            }
        }
        if (on && ms_have && step % MIXER_STEPS == 0 &&
            (row_mixer < 0 || C->row_value(row_mixer) == 1) &&
            (mix_last_sent == 0 || step - mix_last_sent >= MIXER_KEEP || mixer_changed(&ms, &ms_sent))) {
            int n = mixer_json(buf, (int)sizeof buf, seq++, &ms);
            send_pkt(buf, n);
            ms_sent = ms;
            mix_last_sent = step;
        }

        /* mixer sniffer: the changed frame as hex, at most every 50 ms */
        if (on && row_sniff >= 0 && C->row_value(row_sniff) == 1 && step % 10 == 0 && mlen > 0 && g != mix_sent) {
            mix_sent = g;
            int n = snprintf(buf, sizeof buf, "{\"flowbeat\":1,\"type\":\"mixraw\",\"seq\":%u,\"b\":\"", seq++);
            for (int i = 0; i < mlen && n < (int)sizeof buf - 4; i++) n += snprintf(buf + n, sizeof buf - (size_t)n, "%02x", mix[i]);
            n += snprintf(buf + n, sizeof buf - (size_t)n, "\"}");
            send_pkt(buf, n);
        }

        struct timespec ts = { 0, POLL_MS * 1000000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}


/* ---- 0.4.0: the loaded track's rekordbox analysis, read off the deck's USB ----------------------------- */
/* Everything here runs on the "flow-wave" thread: library calls and file reads never touch on_frame/UI. */
#define WAVE_MAX (2 * 1024 * 1024)
#define ANLZ_MAX (4 * 1024 * 1024)
#define CHUNK 1100
#define IDX_MAX 30000

static uint32_t fnv(const char *a, const char *b)
{
    uint32_t h = 2166136261u;
    for (const char *p = a; p && *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    h = (h ^ 0xffu) * 16777619u;
    for (const char *p = b; p && *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    return h;
}

static uint32_t crc32_of(const unsigned char *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t be32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* UTF-16BE → UTF-8 (PPTH stores the audio path in UTF-16BE). Returns bytes written. */
static int utf16be_to_utf8(const unsigned char *in, int n, char *out, int cap)
{
    int o = 0;
    for (int i = 0; i + 1 < n && o < cap - 5; i += 2) {
        uint32_t c = ((uint32_t)in[i] << 8) | in[i + 1];
        if (c == 0) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
            uint32_t d = ((uint32_t)in[i + 2] << 8) | in[i + 3];
            if (d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i += 2; }
        }
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
        else { out[o++] = (char)(0xF0 | (c >> 18)); out[o++] = (char)(0x80 | ((c >> 12) & 0x3F)); out[o++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[o] = 0;
    return o;
}

/* One stick's ANLZ index: hash of the volume-relative audio path (PPTH) -> its ANLZ folder. */
typedef struct { uint32_t h; char dir[40]; } anlz_ent;
typedef struct { char root[256]; long mtime; anlz_ent *e; int n; } anlz_index;
static anlz_index idx[2];

static void index_build(anlz_index *x, const char *root)
{
    char base[320], p1[400], p2[480], f[560];
    snprintf(base, sizeof base, "%s/PIONEER/USBANLZ", root);
    struct stat st;
    if (stat(base, &st) != 0) return;
    if (x->e && strcmp(x->root, root) == 0 && x->mtime == (long)st.st_mtime) return;   /* still current */
    free(x->e); x->e = NULL; x->n = 0;
    snprintf(x->root, sizeof x->root, "%s", root); x->mtime = (long)st.st_mtime;
    x->e = (anlz_ent *)calloc(IDX_MAX, sizeof(anlz_ent));
    if (!x->e) return;
    DIR *d1 = opendir(base);
    if (!d1) return;
    unsigned char hdr[2048];
    char path8[1024];
    struct dirent *a;
    while ((a = readdir(d1)) && x->n < IDX_MAX) {
        if (a->d_name[0] == '.') continue;
        snprintf(p1, sizeof p1, "%s/%s", base, a->d_name);
        DIR *d2 = opendir(p1);
        if (!d2) continue;
        struct dirent *b;
        while ((b = readdir(d2)) && x->n < IDX_MAX) {
            if (b->d_name[0] == '.') continue;
            snprintf(p2, sizeof p2, "%s/%s", p1, b->d_name);
            snprintf(f, sizeof f, "%s/ANLZ0000.DAT", p2);
            FILE *fp = fopen(f, "rbe");
            if (!fp) continue;
            size_t got = fread(hdr, 1, sizeof hdr, fp);
            fclose(fp);
            if (got < 28 || memcmp(hdr, "PMAI", 4) != 0) continue;
            uint32_t off = be32(hdr + 4);
            while (off + 16 <= got) {
                uint32_t hl = be32(hdr + off + 4), tl = be32(hdr + off + 8);
                if (hl < 12 || tl < hl) break;
                if (memcmp(hdr + off, "PPTH", 4) == 0) {
                    uint32_t len = be32(hdr + off + 12);
                    if (off + hl + len <= got && len < 2000) {
                        utf16be_to_utf8(hdr + off + hl, (int)len, path8, (int)sizeof path8);
                        x->e[x->n].h = fnv(path8, "");
                        snprintf(x->e[x->n].dir, sizeof x->e[x->n].dir, "%s/%s", a->d_name, b->d_name);
                        x->n++;
                    }
                    break;
                }
                off += tl;
            }
        }
        closedir(d2);
    }
    closedir(d1);
    C->log(MOD_ID, "ANLZ index: %d tracks on %s", x->n, root);
}

/* lib_tracks callback: find the loaded track on a stick by id (and title, ids repeat across sticks) */
typedef struct { uint32_t id; const char *title; char path[1024]; int found; } find_ctx;
static int find_track(const az_track *t, void *user)
{
    find_ctx *f = (find_ctx *)user;
    if (t->id != f->id || !t->path) return 0;
    if (f->title && f->title[0] && t->title && strcmp(t->title, f->title) != 0) return 0;
    snprintf(f->path, sizeof f->path, "%s", t->path);
    f->found = 1;
    return 1;
}

/* Appends the wanted tags of one ANLZ file to blob. */
static size_t take_tags(const char *file, const char *const *want, unsigned char *blob, size_t at)
{
    FILE *fp = fopen(file, "rbe");
    if (!fp) return at;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 28 || size > ANLZ_MAX) { fclose(fp); return at; }
    unsigned char *d = (unsigned char *)malloc((size_t)size);
    if (!d) { fclose(fp); return at; }
    size_t got = fread(d, 1, (size_t)size, fp);
    fclose(fp);
    if (got == (size_t)size && memcmp(d, "PMAI", 4) == 0) {
        uint32_t off = be32(d + 4), decl = be32(d + 8);
        if (decl > got) decl = (uint32_t)got;
        while (off + 12 <= decl) {
            uint32_t hl = be32(d + off + 4), tl = be32(d + off + 8);
            if (hl < 12 || tl < hl || tl > decl - off) break;
            for (int w = 0; want[w]; w++)
                if (memcmp(d + off, want[w], 4) == 0 && at + tl <= WAVE_MAX) { memcpy(blob + at, d + off, tl); at += tl; }
            off += tl;
        }
    }
    free(d);
    return at;
}

/* The analysis blob for deck d, or 0 bytes. */
static size_t load_analysis(const az_deck_info *di, unsigned char *blob)
{
    find_ctx f;
    memset(&f, 0, sizeof f);
    f.id = di->content_id;
    f.title = di->title;
    int stick = -1;
    for (int s = 0; s < 2 && !f.found; s++)
        if (C->lib_tracks(MOD_ID, s, 0, find_track, &f) >= 0 && f.found) stick = s;
    if (stick < 0) return 0;
    /* the mount point: the longest prefix of the audio path that holds PIONEER/USBANLZ */
    char root[256] = "", probe[320];
    for (size_t i = strlen(f.path); i > 1; i--) {
        if (f.path[i] != '/') continue;
        if (i >= sizeof root) continue;
        memcpy(root, f.path, i); root[i] = 0;
        snprintf(probe, sizeof probe, "%s/PIONEER/USBANLZ", root);
        struct stat st;
        if (stat(probe, &st) == 0) break;
        root[0] = 0;
    }
    if (!root[0]) return 0;
    index_build(&idx[stick], root);
    uint32_t h = fnv(f.path + strlen(root), "");
    const char *dir = NULL;
    for (int i = 0; i < idx[stick].n; i++) if (idx[stick].e[i].h == h) { dir = idx[stick].e[i].dir; break; }
    if (!dir) return 0;
    char file[400];
    static const char *const DAT[] = { "PQTZ", "PWAV", "PCOB", NULL }, *const EXT[] = { "PWV3", "PSSI", "PCO2", NULL };
    size_t n = 0;
    snprintf(file, sizeof file, "%s/PIONEER/USBANLZ/%s/ANLZ0000.DAT", root, dir);
    n = take_tags(file, DAT, blob, n);
    snprintf(file, sizeof file, "%s/PIONEER/USBANLZ/%s/ANLZ0000.EXT", root, dir);
    n = take_tags(file, EXT, blob, n);
    return n;
}

static void send_wave(int d, const az_deck_info *di, uint32_t sig, const unsigned char *blob, size_t n, unsigned *seq)
{
    char buf[1400];
    unsigned cnt = (unsigned)((n + CHUNK - 1) / CHUNK);
    uint32_t crc = crc32_of(blob, n);
    int m = snprintf(buf, sizeof buf, "{\"flowbeat\":1,\"type\":\"wave\",\"seq\":%u,\"d\":%d,\"sig\":%u,\"len\":%u,\"cnt\":%u,\"title\":",
                     (*seq)++, d + 1, sig, (unsigned)n, cnt);
    m = put_str(buf, m, 700, di->title);
    m += snprintf(buf + m, sizeof buf - (size_t)m, ",\"artist\":");
    m = put_str(buf, m, 1300, di->artist);
    m += snprintf(buf + m, sizeof buf - (size_t)m, "}");
    send_pkt(buf, m);
    unsigned char pkt[24 + CHUNK];
    for (unsigned i = 0; i < cnt; i++) {
        size_t off = (size_t)i * CHUNK, len = n - off < CHUNK ? n - off : CHUNK;
        memcpy(pkt, "FBW1", 4); pkt[4] = 1; pkt[5] = (unsigned char)(d + 1); pkt[6] = pkt[7] = 0;
        uint32_t v[3] = { htonl(sig), htonl((uint32_t)n), htonl(crc) };
        memcpy(pkt + 8, &v[0], 4); memcpy(pkt + 12, &v[1], 4);
        uint16_t ii = htons((uint16_t)i), cc = htons((uint16_t)cnt);
        memcpy(pkt + 16, &ii, 2); memcpy(pkt + 18, &cc, 2); memcpy(pkt + 20, &v[2], 4);
        memcpy(pkt + 24, blob + off, len);
        send_pkt((const char *)pkt, (int)(24 + len));
        struct timespec ts = { 0, 2 * 1000000L };   /* ~2 ms apart: a 70-chunk track in ~0.15 s */
        nanosleep(&ts, NULL);
    }
}

static void *wave_worker(void *arg)
{
    (void)arg;
    uint32_t sent_sig[NDECKS] = { 0 };
    unsigned repeats[NDECKS] = { 0 };
    unsigned next_at[NDECKS] = { 0 }, tick = 0;   /* in 200 ms ticks (no wall clock needed) */
    unsigned char *blob[NDECKS] = { 0 };
    size_t blen[NDECKS] = { 0 };
    unsigned seq = wave_seq_base;
    for (;;) {
        struct timespec ts = { 0, 200 * 1000000L };
        nanosleep(&ts, NULL);
        int on = row_send >= 0 ? C->row_value(row_send) == 1 : 1;
        int wave_on = row_wave >= 0 ? C->row_value(row_wave) == 1 : C->cfg_get(MOD_ID, "wave", 1) != 0;
        if (!on || !wave_on) continue;
        unsigned now = ++tick;
        for (int d = 0; d < NDECKS; d++) {
            az_deck_info di;
            memset(&di, 0, sizeof di);
            di.size = (int)sizeof di;
            if (C->deck_info(d, &di) != 0 || di.loaded != 1 || !di.content_id) { sent_sig[d] = 0; continue; }
            uint32_t sig = fnv(di.title, di.artist) ^ di.content_id;
            if (sig != sent_sig[d]) {                       /* a new track: find and read its analysis once */
                sent_sig[d] = sig; repeats[d] = 0; next_at[d] = 0; blen[d] = 0;
                if (!blob[d]) blob[d] = (unsigned char *)malloc(WAVE_MAX);
                if (blob[d]) blen[d] = load_analysis(&di, blob[d]);
            }
            if (!blen[d] || now < next_at[d]) continue;
            send_wave(d, &di, sig, blob[d], blen[d], &seq);
            repeats[d]++;
            next_at[d] = now + (repeats[d] < 3 ? 5 : 50);   /* now, twice more a second apart, then every 10 s */
        }
    }
    return NULL;
}

/* ---- deck events (UI thread): a load or eject resends the titles at once ---------------- */

static void on_deck(int deck, int what, void *user)
{
    (void)deck;
    (void)user;
    if (what == AZ_DECK_LOADED || what == AZ_DECK_EJECTED)
        __atomic_store_n(&track_dirty, 1, __ATOMIC_RELEASE);
}

/* ---- lifecycle --------------------------------------------------------------------------- */

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, deck_info) || !AZ_HAS(C, deck_grid) || !AZ_HAS(C, thread_start) ||
        !AZ_HAS(C, cfg_get_str) || !AZ_HAS(C, on_deck_event)) {
        C->log(MOD_ID, "this AZ-MODS lacks the deck services (1.2.3 or newer)");
        return 2;
    }
    static const az_row_v1 send_row = {
        .size = sizeof send_row,
        .key = "send",
        .label = "Send beat to FLOW",
        .kind = AZ_ROW_TOGGLE,
        .flags = AZ_ROW_DEFAULT_ON,
        .help = "Sends each deck's beat, BPM, play state and track title to FLOW VJ over the "
                "network (UDP port 9100). Set flow-beat:host in MODS/azmods.cfg to send to one "
                "computer instead of the whole network.",
    };
    /* without Mod Menu rows, flow-beat:send in azmods.cfg (default on) decides */
    static const az_row_v1 wave_row = {
        .size = sizeof wave_row,
        .key = "wave",
        .label = "Send track analysis to FLOW",
        .kind = AZ_ROW_TOGGLE,
        .flags = AZ_ROW_DEFAULT_ON,
        .help = "Reads each loaded track's rekordbox analysis (beat grid, waveform, phrases, cues) off the USB "
                "it plays from and sends it to FLOW VJ, so guest USBs light like your own tracks.",
    };
    static const az_row_v1 sniff_row = {
        .size = sizeof sniff_row,
        .key = "sniff",
        .label = "Send mixer data (test)",
        .kind = AZ_ROW_TOGGLE,
        .help = "For mapping the mixer: while on, sends the mixer panel's raw input data to FLOW "
                "whenever a knob or fader moves. Leave off for shows.",
    };
    static const az_row_v1 mixer_row = {
        .size = sizeof mixer_row,
        .key = "mixer",
        .label = "Send mixer to FLOW",
        .kind = AZ_ROW_TOGGLE,
        .flags = AZ_ROW_DEFAULT_ON,
        .help = "FLOW Mixer Link: sends the mixer's channel faders, crossfader, EQ, Color FX, "
                "Beat FX and X-PAD to FLOW VJ, so visuals and lights follow the mix. Reads the "
                "panel only; the sound is never touched.",
    };
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        row_send = C->register_row(MOD_ID, &send_row);
        row_mixer = C->register_row(MOD_ID, &mixer_row);
        row_sniff = C->register_row(MOD_ID, &sniff_row);
        row_wave = C->register_row(MOD_ID, &wave_row);
    }
    /* each deck's own audio, before the hardware mixer (0.3.0) */
    if (AZ_HAS(C, audio_tap))
        for (int d = 0; d < NDECKS; d++)
            if (C->audio_tap(MOD_ID, AZ_TAP_DECK(d), TAP_FRAMES, meter, (void *)(intptr_t)d) != 0)
                C->log(MOD_ID, "audio_tap deck %d refused", d + 1);
    /* the mixer panel's input frames, read-only (Mixer Link and the sniffer) */
    if (AZ_HAS(C, frame_filter) &&
        C->frame_filter(MOD_ID, (1u << AZ_BUS_MIXER_IN) | AZ_FILTER_RAW, mix_filter, NULL) != 0)
        C->log(MOD_ID, "frame_filter refused: no mixer data");
    read_cfg(0);
    if (C->on_deck_event(MOD_ID, on_deck, NULL) != 0) C->log(MOD_ID, "on_deck_event refused");
    /* 0.4.0: the analysis reader, only when the core can list the library */
    if (AZ_HAS(C, lib_tracks) && C->thread_start(MOD_ID, "flow-wave", wave_worker, NULL) != 0)
        C->log(MOD_ID, "no wave thread: guest analysis is not sent");
    if (C->thread_start(MOD_ID, "flow-beat", worker, NULL) != 0) {
        C->log(MOD_ID, "no worker thread: nothing is sent");
        return 4;
    }
    return 0;
}

static const char *const FW_TAGS[] = { "any", NULL };

static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI,
    .size = sizeof(az_mod_v1),
    .id = MOD_ID,
    .fw_tags = FW_TAGS,
    .init = init,
    .tick = NULL,
    .on_frame = NULL,
};

const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
