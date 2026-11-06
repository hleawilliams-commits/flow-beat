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
 *   0.5.3 (FX state): {"flowbeat":1,"type":"fx","seq":N,"decks":[{"d":1,"sfx":0,"drum":0,"hold":0,
 *        "smp":-1,"loop":0}, ...4]}  AZ-MODS 1.2.6+ performance state, when it changes (at most every
 *        20 ms) and every second: sfx = Stem FX pads held (bit 0 VOCAL ECHO, 1 INST ECHO, 2 INST BRAKE,
 *        3 DRUMS ECHO), drum = Drum Roll on, hold = Drum Roll HOLD, smp = selected sample pad 0-3,
 *        loop = 0 none / 1 held / 2 looping / 3 being set. -1 = unknown (a deck not on a side, or an
 *        AZ-MODS without Stem FX). See fx.h.
 *   0.6.1 (Touch FX): with MLT's Touch FX build (mrdjjag's panel + azstem.so posting the touch),
 *        each deck in the fx packet adds "tfx":{"on":1,"k":"stems","fx":"gate","x":420,"y":180,
 *        "beats":0.25,"tg":3,"latch":0}: on = the touch FX is audible, k = STEMS or Drum Roll panel,
 *        fx = the panel's effect, x/y = finger 0..1000 (y 0 = top = full effect), beats = the beat
 *        column under the finger, tg = stem targets (1 vocals, 2 instrument, 4 bass, 0 whole mix).
 *        Sent on change (at most every 20 ms) like the rest of the packet. No "tfx" without it.
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
 *
 * Ableton Link from the deck (row "Ableton Link from this deck", off by default; link.cpp):
 * the lead deck (0.6.4: the playing deck whose channel fader is up most; with faders level, the one
 * started last) leads a Link session on the deck's network, tempo, beat and bar, so Ableton or
 * AUTO/PILOT follow it with no bridge on a computer. The bar's "1" comes from a "this is 1"
 * press ({"flowbeat":1,"type":"one","late":true|false} to UDP port 9101 on the deck, from FLOW
 * VJ's phrase-sync button) and otherwise from the track's rekordbox beat grid (PQTZ).
 *   flow-beat:link_ms = 0               how far the deck's sound runs behind its playhead, in ms
 *                                       (raise it if Link peers play ahead of the deck)
 *   flow-beat:one_port = 9101
 *
 * Link Audio (0.5.2, row "Send decks to Ableton", off by default; turns Link on too):
 * the deck joins the Link session as "XDJ-AZ" with four audio channels, "Deck 1".."Deck 4", each
 * deck's own sound from its audio tap (before the hardware mixer: no faders, EQ, FX or master).
 * In Ableton Live 12.4+, pick them as a track's input (Link Audio). A channel only sends while
 * something listens to it, about 1.5 Mbit/s per deck (16-bit stereo at 44.1 or 48 kHz).
 *   flow-beat:link_audio_ms = 0         added to link_ms for the audio only (raise it if the
 *                                       decks' audio arrives in Ableton early)
 *   flow-beat:link_audio_rate = 0       0: send at each tap's own rate, measured and snapped to the
 *                                       standard rate it is (the emulator's taps run at 44100);
 *                                       else force this rate in Hz
 *
 * Lights from this deck (0.6.0, row "Lights from this deck", off by default; lights.h): the deck
 * runs a light show itself and sends it as Art-Net (ArtDmx, UDP 6454, 40 frames a second) to an
 * Art-Net node on its network, so the lights play with no computer. It follows the playing deck
 * whose fader is up most: beat chases, bass pump, a fade in breakdowns, a white hit on the drop.
 * While another computer sends ArtDmx to the same universe (FLOW VJ's lightbridge with Art-Net on,
 * a house console), the deck stays quiet, and takes over again 3 s after that sender stops.
 *   flow-beat:artnet_host = ""          one node's IPv4 address (default: broadcast on every network)
 *   flow-beat:artnet_universe = 0       the Art-Net Port-Address (0 = Net 0, Sub-Net 0, Universe 0)
 *   flow-beat:artnet_patch = "4bar_tri_15@59,slimpar_h6_8@74,..."   the rig, type@address[:flip];
 *                                       default: MLT's rig as in FLOW VJ's dmx_patch.json
 *
 * FLOW Lights panel (0.6.2, row "FLOW Lights button", on by default; panel.h): a LIGHTS button in the
 * deck screen's top bar opens a panel over the waveforms. Looks 1-8 and the WHITE / COLOR hits go to
 * FLOW VJ as the same "vjpad" packets Pad Lights' VJ page sends; PREV, NEXT, HOLD, AUTO, SAFE, DROP,
 * FOG, BLACK and STROBE (held) as Custom Screen's "FVC1 <seq> <act>" to UDP 9103. Colour, intensity,
 * follow beat, drop flash, fader dim and the hits also drive the deck's own Art-Net show. FLOW VJ's
 * state (look name, HOLD / SAFE / AUTO / BLACK) shows when it sends FVS1 to UDP 9104. Two surfaces.
 *   flow-beat:panel_level, panel_follow, panel_dropflash, panel_fader   the panel's last settings
 */
#define _GNU_SOURCE             /* RTLD_DEFAULT (0.5.3: azstem_stem_fx_on) */
#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
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
#include "link.h"
#include "fx.h"
#include "lights.h"
#include "panel.h"

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
#define FX_STEPS 4           /* fx packets at most every 20 ms (0.5.3) */
#define FX_KEEP 200          /* and every second */

static const az_core_v1 *C;
static int row_send = -1;    /* the "Send beat to FLOW" toggle */
static int row_sniff = -1;   /* "Send mixer data (test)" (0.3.0), off by default */
static int row_mixer = -1;   /* "Send mixer to FLOW" (0.5.0), on by default */
static int row_wave = -1;    /* "Send track analysis to FLOW" (0.4.0), on by default */
static unsigned wave_seq_base = 0x40000000u;   /* the wave thread's own seq range, so the two threads never collide */
static int track_dirty;      /* set by deck events (UI thread), read by the worker: atomics */
/* 0.6.5 (Key Sync): each deck's key from the stick's rekordbox library, found by the wave thread
 * with the analysis and read by the worker. The 7-character key packs into one 64-bit word; its
 * content id is cleared first and set last, so a reader never pairs a key with the wrong track. */
static uint64_t lib_key_word[4];
static uint32_t lib_key_id[4];
static void lib_key_set(int d, uint32_t id, const char *key)
{
    char k[8] = { 0 };
    for (int i = 0, o = 0; key && key[i] && o < 7; i++)
        if (key[i] > 0x20 && key[i] < 0x7f && key[i] != '"' && key[i] != '\\') k[o++] = key[i];
    uint64_t w; memcpy(&w, k, 8);
    __atomic_store_n(&lib_key_id[d], 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&lib_key_word[d], w, __ATOMIC_RELEASE);
    __atomic_store_n(&lib_key_id[d], id, __ATOMIC_RELEASE);
}
/* The key for deck d: the core's own if it gives one, else the library's for this track, else "". */
static void deck_key(int d, const az_deck_info *di, char out[8])
{
    out[0] = 0;
    if (di->loaded != 1) return;
    if (di->key[0]) {
        int o = 0;
        for (int i = 0; di->key[i] && i < (int)sizeof di->key && o < 7; i++)
            if (di->key[i] > 0x20 && di->key[i] < 0x7f && di->key[i] != '"' && di->key[i] != '\\') out[o++] = di->key[i];
        out[o] = 0;
        if (o) return;
    }
    uint32_t id = __atomic_load_n(&lib_key_id[d], __ATOMIC_ACQUIRE);
    uint64_t w = __atomic_load_n(&lib_key_word[d], __ATOMIC_ACQUIRE);
    if (!id || id != di->content_id || __atomic_load_n(&lib_key_id[d], __ATOMIC_ACQUIRE) != id) return;
    memcpy(out, &w, 8);
    out[7] = 0;
}
#ifndef FLOW_LINK_DEFAULT
#define FLOW_LINK_DEFAULT 0  /* make test TEST_CC="clang -DFLOW_LINK_DEFAULT=1" runs the harness with Link on */
#endif
#ifndef FLOW_LINK_AUDIO_DEFAULT
#define FLOW_LINK_AUDIO_DEFAULT 0
#endif
static int row_link = -1;    /* "Ableton Link from this deck", off by default */
static int row_audio = -1;   /* "Send decks to Ableton" (0.5.2, Link Audio), off by default */
static int link_audio_on(void)
{
    return row_audio >= 0 ? C->row_value(row_audio) == 1
                          : C->cfg_get(MOD_ID, "link-audio", FLOW_LINK_AUDIO_DEFAULT) != 0;
}
/* Link runs while either row is on: Link Audio needs the session (and its beat timeline) */
static int link_on(void)
{
    return (row_link >= 0 ? C->row_value(row_link) == 1 : C->cfg_get(MOD_ID, "link", FLOW_LINK_DEFAULT) != 0) ||
           link_audio_on();
}

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
static _Atomic float lt_low[NDECKS];       /* 0.6.0: the same bass peak, read and reset by the lights */
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
    if (b > atomic_load_explicit(&lt_low[d], memory_order_relaxed)) atomic_store_explicit(&lt_low[d], b, memory_order_relaxed);
}

/* Each deck's tap: the level meter, then Link Audio (it returns at once while that is off). */
static void deck_tap(const float *lr, int frames, void *user)
{
    meter(lr, frames, user);
    flowlink_audio_send((int)(intptr_t)user, lr, frames);
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

/* ---- 0.5.3: AZ-MODS 1.2.6+ Stem FX, Drum Roll and loop state (fx.h) ---------------------- */
static fx_leds fxl;
static int led_filter(int bus, unsigned char *frame, int len, void *user)
{
    (void)bus; (void)user;
    fx_led_take(&fxl, frame, len);   /* read only */
    return AZ_FRAME_OK;
}
/* azstem.so's Stem FX state (AZ-MODS 1.2.6+, not in the SDK). azstem may load after us, so the
 * worker looks it up every 2 s until it is there; none = an older AZ-MODS (sfx -1). */
static unsigned (*stem_fx_on)(int deck);
/* 0.6.1: MLT's Touch FX build (azstem.so with mrdjjag_touchfx_vj_get, ABI 1). */
static unsigned long long (*touch_vj_get)(int deck);
static void fx_resolve(void)
{
    if (!touch_vj_get) {
        int (*abi)(void) = NULL;
        *(void **)&abi = dlsym(RTLD_DEFAULT, "mrdjjag_touchfx_vj_abi");
        void *g = abi && abi() == 1 ? dlsym(RTLD_DEFAULT, "mrdjjag_touchfx_vj_get") : NULL;
        if (g) {
            *(void **)&touch_vj_get = g;
            C->log(MOD_ID, "Touch FX report found: finger and effect go to FLOW");
        }
    }
    if (stem_fx_on) return;
    void *f = dlsym(RTLD_DEFAULT, "azstem_stem_fx_on");
    if (!f) return;
    *(void **)&stem_fx_on = f;
    C->log(MOD_ID, "Stem FX state found (AZ-MODS 1.2.6+)");
}

/* ---- each loaded track's bars, for Link (rekordbox PQTZ beat grid) ---------------------------- */
/* The wave thread parses the grid when it reads a track's analysis; the worker looks up which beat
 * of the bar the playhead is in. A per-deck try-lock: the worker never waits, it skips one step. */
static uint32_t fnv(const char *a, const char *b);
static uint32_t be32(const unsigned char *p);
#define BARS_MAX 32768                     /* beats: over an hour at 128 BPM */
typedef struct {
    atomic_flag busy;
    uint32_t sig;                          /* the track (as wave_worker's sig), 0 = none */
    int n;
    uint32_t *ms;                          /* each beat's time in the track, ms */
    unsigned char *num;                    /* its beat of the bar, 1..4 */
} deck_bars;
static deck_bars bars[NDECKS] = { { ATOMIC_FLAG_INIT, 0, 0, NULL, NULL }, { ATOMIC_FLAG_INIT, 0, 0, NULL, NULL },
                                  { ATOMIC_FLAG_INIT, 0, 0, NULL, NULL }, { ATOMIC_FLAG_INIT, 0, 0, NULL, NULL } };

/* wave thread: the PQTZ tag in an analysis blob (tags as take_tags copied them) -> bars[d] */
static void bars_set(int d, uint32_t sig, const unsigned char *blob, size_t n)
{
    deck_bars *b = &bars[d];
    if (!b->ms) {
        b->ms = (uint32_t *)malloc(BARS_MAX * sizeof *b->ms);
        b->num = (unsigned char *)malloc(BARS_MAX);
        if (!b->ms || !b->num) { free(b->ms); free(b->num); b->ms = NULL; b->num = NULL; return; }
    }
    const unsigned char *q = NULL;
    uint32_t cnt = 0;
    for (size_t off = 0; off + 12 <= n;) {
        uint32_t hl = be32(blob + off + 4), tl = be32(blob + off + 8);
        if (hl < 12 || tl < hl || tl > n - off) break;
        if (memcmp(blob + off, "PQTZ", 4) == 0 && hl >= 24) {
            cnt = be32(blob + off + 20);
            if (cnt > (tl - hl) / 8) cnt = (tl - hl) / 8;
            q = blob + off + hl;
            break;
        }
        off += tl;
    }
    if (cnt > BARS_MAX) cnt = BARS_MAX;
    while (atomic_flag_test_and_set_explicit(&b->busy, memory_order_acquire)) { struct timespec t = { 0, 100000 }; nanosleep(&t, NULL); }
    b->n = 0;
    for (uint32_t i = 0; q && i < cnt; i++) {
        unsigned num = ((unsigned)q[8 * i] << 8) | q[8 * i + 1];
        if (num < 1 || num > 4) continue;
        b->num[b->n] = (unsigned char)num;
        b->ms[b->n] = be32(q + 8 * i + 4);
        b->n++;
    }
    b->sig = b->n ? sig : 0;
    atomic_flag_clear_explicit(&b->busy, memory_order_release);
}

/* worker: 0..3 for the beat of the bar deck d's playhead is in (0 = the "1"), -1 unknown or busy */
static int bars_at(int d, uint32_t sig, double pos_s, double ph)
{
    deck_bars *b = &bars[d];
    if (pos_s < 0 || ph < 0) return -1;
    if (atomic_flag_test_and_set_explicit(&b->busy, memory_order_acquire)) return -1;
    int r = -1;
    if (b->sig == sig && b->n >= 2) {
        double pos = pos_s * 1000.0;
        int lo = 0, hi = b->n - 1;            /* the last beat at or before pos */
        while (lo < hi) { int mid = (lo + hi + 1) / 2; if (b->ms[mid] <= pos) lo = mid; else hi = mid - 1; }
        int i = lo < b->n - 1 ? lo : b->n - 2;
        double period = (double)b->ms[i + 1] - b->ms[i];
        double start = pos - ph * period;     /* where the current beat started, by the deck's own phase */
        int j = lo;
        if (j + 1 < b->n && fabs(b->ms[j + 1] - start) < fabs(b->ms[j] - start)) j++;
        if (j > 0 && fabs(b->ms[j - 1] - start) < fabs(b->ms[j] - start)) j--;
        if (period > 0 && fabs(b->ms[j] - start) < period / 2) r = b->num[j] - 1;
    }
    atomic_flag_clear_explicit(&b->busy, memory_order_release);
    return r;
}

/* ---- Ableton Link: the lead deck leads (link.cpp) ------------------------------------------ */
/* "this is 1": FLOW VJ's phrase-sync press, as a datagram to the deck's port 9101 */
static int one_sock = -1, one_port_open;

static int one_pressed(int *late)
{
    int port = C->cfg_get(MOD_ID, "one_port", 9101);
    if (port < 1 || port > 65535) port = 9101;
    if (one_sock >= 0 && one_port_open != port) { close(one_sock); one_sock = -1; }
    if (one_sock < 0) {
        one_sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (one_sock < 0) return 0;
        int on = 1;
        setsockopt(one_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)port);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(one_sock, (struct sockaddr *)&a, sizeof a) != 0) {
            C->log(MOD_ID, "Link: UDP port %d is taken: no \"this is 1\" bar reset", port);
            close(one_sock);
            one_sock = -2;                    /* don't retry every 5 ms; a port change retries */
            one_port_open = port;
            return 0;
        }
        one_port_open = port;
        C->log(MOD_ID, "Link: \"this is 1\" listens on UDP %d", port);
    }
    if (one_sock < 0) return 0;
    char m[256];
    int got = 0;
    ssize_t n;
    while ((n = recv(one_sock, m, sizeof m - 1, MSG_DONTWAIT)) > 0) {
        m[n] = 0;
        if (!strstr(m, "\"flowbeat\":1") || !strstr(m, "\"type\":\"one\"")) continue;
        got = 1;
        *late = strstr(m, "\"late\":true") ? 1 : strstr(m, "\"late\":false") ? 0 : -1;
    }
    return got;
}

static void one_close(void)
{
    if (one_sock >= 0) close(one_sock);
    one_sock = -1;
    one_port_open = 0;
}

/* 0.6.4: the deck on air leads, for Link and the lights alike: the playing deck whose channel
 * fader is up most (within 20/1023 of each other, the one started last). Without mixer frames
 * yet, the playing deck started last. -1: nothing plays. */
static int on_air_deck(const unsigned *since, const mixer_state *ms, int ms_have)
{
    int lead = -1;
    for (int d = 0; d < NDECKS; d++) {
        if (!since[d]) continue;
        if (lead < 0) { lead = d; continue; }
        if (ms_have ? ms->fader[d] > ms->fader[lead] + 20 ||
                      (ms->fader[d] + 20 >= ms->fader[lead] && ms->fader[lead] + 20 >= ms->fader[d] && since[d] > since[lead])
                    : since[d] > since[lead])
            lead = d;
    }
    return lead;
}

/* One worker step for Link: picks the lead deck, works out its bar, and hands it to link.cpp. */
static void link_step(const az_deck_info *di, const double *ph, const unsigned *since,
                      const unsigned *beats, const uint32_t *track_sig, unsigned step,
                      const mixer_state *ms, int ms_have)
{
    static int was_on, lead = -1, peers = -2;
    static uint32_t lead_sig;
    static int one_deck = -1;                 /* a "this is 1" press: on which deck and track, */
    static uint32_t one_sig;
    static unsigned one_since;                /* the play start it was pressed in */
    static unsigned one_beat;                 /* and which of its beats was the 1 */
    static int ahead_ms;
    static unsigned forced_log;
    static int audio_was = 0, audio_rate = 0, audio_ms = 0;   /* audio_rate 0 = measure each tap */
    static unsigned audio_since;              /* the step Link Audio came on */
    static unsigned long audio_sent[NDECKS];
    static int audio_listened[NDECKS];
    static unsigned audio_rate_logged[NDECKS];

    int on = link_on();
    if (on != was_on) {
        if (flowlink_enable(on) != 0) {
            if (was_on != -1) C->log(MOD_ID, "Link could not start");
            was_on = -1;                      /* logged once; retried each step while the row is on */
            return;
        }
        C->log(MOD_ID, on ? "Link on: this deck leads the session" : "Link off");
        was_on = on;
        lead = -1;
        peers = -2;
        audio_was = 0;                        /* Link off turns Link Audio off with it */
        if (!on) one_close();
    }
    if (!on) return;

    if (step % CFG_STEPS == 1) {
        ahead_ms = C->cfg_get(MOD_ID, "link_ms", 0);
        if (ahead_ms < -500 || ahead_ms > 500) ahead_ms = 0;
    }

    /* Link Audio: each deck's sound as a channel of the session */
    int audio = link_audio_on();
    if (audio && step % CFG_STEPS == 1) {
        int r = C->cfg_get(MOD_ID, "link_audio_rate", 0);
        int ms = C->cfg_get(MOD_ID, "link_audio_ms", 0);
        if (r < 8000 || r > 192000) r = 0;
        if (ms < -500 || ms > 500) ms = 0;
        if (audio_was == 1 && (r != audio_rate || ms != audio_ms)) audio_was = 0;   /* apply below */
        audio_rate = r;
        audio_ms = ms;
    }
    if (audio != audio_was) {
        if (flowlink_audio_enable(audio, (unsigned)audio_rate, audio_ms) != 0) {
            if (audio_was != -1) C->log(MOD_ID, "Link Audio could not start");
            audio_was = -1;
        } else {
            if (audio)
            {
                char rate[24];
                if (audio_rate) snprintf(rate, sizeof rate, "%d Hz", audio_rate);
                else snprintf(rate, sizeof rate, "rate measured");
                C->log(MOD_ID, "Link Audio on: \"XDJ-AZ\" offers Deck 1-4 (%s, +%d ms)", rate, audio_ms);
            }
            else if (audio_was == 1)
                C->log(MOD_ID, "Link Audio off");
            audio_was = audio;
            audio_since = step;
            memset(audio_sent, 0, sizeof audio_sent);
            memset(audio_listened, 0, sizeof audio_listened);
            memset(audio_rate_logged, 0, sizeof audio_rate_logged);
        }
    }
    if (audio_was == 1 && step - audio_since == 1200) {   /* 6 s in: what the taps really run at */
        for (int d = 0; d < NDECKS; d++) {
            double r = flowlink_audio_rate_seen(d);
            if (r > 0) C->log(MOD_ID, "Link Audio: deck %d tap runs at %.0f Hz", d + 1, r);
        }
    }
    if (audio_was == 1 && step % 400 == 0) {         /* every 2 s: log when a deck gains or loses a listener */
        for (int d = 0; d < NDECKS; d++) {
            unsigned used = flowlink_audio_rate_used(d);   /* and the rate it sends at, when it changes */
            if (used && used != audio_rate_logged[d]) {
                C->log(MOD_ID, "Link Audio: deck %d sends at %u Hz%s", d + 1, used,
                       audio_rate ? " (set by link_audio_rate)" : " (the tap's own rate)");
                audio_rate_logged[d] = used;
            }
            unsigned long n = flowlink_audio_sent(d);
            int now_sending = n > audio_sent[d];
            if (now_sending != audio_listened[d])
                C->log(MOD_ID, now_sending ? "Link Audio: deck %d is being received" : "Link Audio: deck %d no longer received",
                       d + 1);
            audio_listened[d] = now_sending;
            audio_sent[d] = n;
        }
    }
    if (step % 200 == 0) {                    /* about every second */
        int p = flowlink_peers();
        if (p != peers) C->log(MOD_ID, "Link: %d peer%s", p, p == 1 ? "" : "s");
        peers = p;
    }

    int l = on_air_deck(since, ms, ms_have);  /* 0.6.4: the deck on air, not just the last started */
    flowlink_in in;
    memset(&in, 0, sizeof in);
    in.bar_beat = -1;
    in.ahead_ms = ahead_ms;
    in.ph = -1.0;
    if (l >= 0) {
        in.playing = 1;
        in.bpm = di[l].bpm;
        in.ph = ph[l];
        if (l != lead || track_sig[l] != lead_sig) {
            in.new_lead = 1;
            C->log(MOD_ID, "Link follows deck %d (%.2f BPM)", l + 1, di[l].bpm);
        }
        lead_sig = track_sig[l];

        int late = -1;
        if (one_pressed(&late)) {
            if (late < 0) late = ph[l] > 0.5;
            one_deck = l;
            one_sig = track_sig[l];
            one_since = since[l];
            one_beat = beats[l] + (late ? 1 : 0);
            in.new_lead = 1;                  /* move the session's bar now */
            C->log(MOD_ID, "Link: this is 1 on deck %d%s", l + 1, late ? " (next beat)" : "");
        }
        if (one_deck == l && one_sig == track_sig[l] && one_since == since[l])
            in.bar_beat = (int)((beats[l] - one_beat) & 3u);
        else {
            one_deck = -1;                    /* a press counts until the lead's track changes or it stops */
            in.bar_beat = bars_at(l, (uint32_t)(fnv(di[l].title, di[l].artist) ^ di[l].content_id),
                                  di[l].pos_s, ph[l]);
        }
    } else {
        int late;
        (void)one_pressed(&late);             /* nothing plays: drop presses */
    }
    lead = l;

    flowlink_out out;
    flowlink_step(&in, &out);
    if (out.forced && (in.new_lead || step - forced_log > 2000)) {   /* a jump: log at most every 10 s */
        C->log(MOD_ID, "Link moved onto deck %d (was %+.3f beats off, bar beat %d)", l + 1, out.err,
               in.bar_beat + 1);
        forced_log = step;
    }
}

/* ---- 0.6.2: FLOW Lights panel (panel.h): state shared by the touch filter, tick, worker ------- */
/* The touch filter (bus thread) only sets these bits; the UI tick acts on them; the worker sends. */
static unsigned long long pn_tapped;      /* atomic: 1 << PN_* tapped on the panel, for the tick */
static unsigned long long pn_out;         /* atomic: 1 << PN_* the worker sends to FlowVJ */
static unsigned pn_hits;                  /* atomic: 1 << PN_WHITE / PN_COLOR / PN_DROP for the deck show */
static int pn_toggle;                     /* atomic: the LIGHTS button was tapped */
static int pn_open;                       /* atomic: the panel is up (the touch filter reads it) */
static int pn_strobe_held;                /* atomic: a finger is on STROBE */
static int pn_strobe_off;                 /* atomic: STROBE was let go, the worker says strobe_off */
static int pn_level = 100;                /* atomic: INTENSITY 0..100 (the fader writes, the show reads) */
static int pn_follow = 1, pn_dflash = 1, pn_fader = 1;   /* atomics: the deck show's switches */
static int pn_black, pn_safe;             /* atomics: BLACK (deck show dark), SAFE (no deck strobe) */
static int pn_fv_serial;                  /* atomic: the worker got a new FVS1 */
static pn_fv pn_fv_shared;
static atomic_flag pn_fv_lock = ATOMIC_FLAG_INIT;
static int pn_yielding;                   /* atomic: the deck show stepped back for another sender */

/* ---- 0.6.0: Lights from this deck (lights.h): the deck's own show, as Art-Net -------------- */
static int row_lights = -1;            /* "Lights from this deck", off by default */
static int row_look = -1;              /* "Deck lights colours" */
#define LT_STEPS 5                     /* a frame every 25 ms: 40 a second, like FLOW's lightbridge */
#define LT_YIELD_MS 3000               /* another Art-Net source on our universe: stay quiet this long */
static int lt_sock = -1;               /* sends (broadcast allowed) */
static int lt_rx = -1;                 /* listens on 6454 for another sender; -2 = port taken */
static struct sockaddr_in lt_dests[MAX_DESTS];
static int lt_ndests;
static uint32_t lt_own[MAX_DESTS];     /* our own addresses: our broadcasts come back to us */
static int lt_nown;

#ifndef FLOW_LIGHTS_DEFAULT
#define FLOW_LIGHTS_DEFAULT 0          /* make test TEST_CC="clang -DFLOW_LIGHTS_DEFAULT=1": the harness with the lights on */
#endif
static int lights_on(void)
{
    return row_lights >= 0 ? C->row_value(row_lights) == 1 : C->cfg_get(MOD_ID, "artnet", FLOW_LIGHTS_DEFAULT) != 0;
}

/* Where frames go: flow-beat:artnet_host (one node), else every network's broadcast. Also learns
 * our own addresses. Returns the universe (Port-Address 0..32767). */
static int lights_cfg(lt_fix *fix, int *nfix, int quiet)
{
    char host[64], patch[600];
    C->cfg_get_str(MOD_ID, "artnet_host", host, (int)sizeof host, "");
    C->cfg_get_str(MOD_ID, "artnet_patch", patch, (int)sizeof patch, LT_DEFAULT_PATCH);
    int uni = C->cfg_get(MOD_ID, "artnet_universe", 0);
    if (uni < 0 || uni > 32767) uni = 0;
    int bad = -1;
    int n = lt_parse_patch(patch, fix, LT_MAX_FIX, &bad);
    if (n == 0) n = lt_parse_patch(LT_DEFAULT_PATCH, fix, LT_MAX_FIX, NULL);
    if (!quiet && bad >= 0) C->log(MOD_ID, "lights: artnet_patch entry %d not understood, skipped", bad + 1);
    *nfix = n;

    struct in_addr a;
    int one = host[0] && inet_pton(AF_INET, host, &a) == 1;
    if (host[0] && !one && !quiet) C->log(MOD_ID, "lights: artnet_host \"%s\" is not an IPv4 address: broadcasting", host);
    lt_ndests = 0;
    lt_nown = 0;
    struct ifaddrs *ifs = NULL;
    if (getifaddrs(&ifs) == 0) {
        for (struct ifaddrs *i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
            if (!(i->ifa_flags & IFF_UP)) continue;
            if (lt_nown < MAX_DESTS) lt_own[lt_nown++] = ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr;
            if (one || (i->ifa_flags & IFF_LOOPBACK)) continue;
            if (!(i->ifa_flags & IFF_BROADCAST) || !i->ifa_broadaddr) continue;
            if (lt_ndests >= MAX_DESTS) continue;
            memset(&lt_dests[lt_ndests], 0, sizeof lt_dests[0]);
            lt_dests[lt_ndests].sin_family = AF_INET;
            lt_dests[lt_ndests].sin_port = htons(LT_PORT);
            lt_dests[lt_ndests].sin_addr = ((struct sockaddr_in *)i->ifa_broadaddr)->sin_addr;
            lt_ndests++;
        }
        freeifaddrs(ifs);
    }
    if (one || lt_ndests == 0) {
        if (!one) inet_pton(AF_INET, "255.255.255.255", &a);
        memset(&lt_dests[0], 0, sizeof lt_dests[0]);
        lt_dests[0].sin_family = AF_INET;
        lt_dests[0].sin_port = htons(LT_PORT);
        lt_dests[0].sin_addr = a;
        lt_ndests = 1;
    }
    if (!quiet) {
        for (int i = 0; i < lt_ndests; i++) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &lt_dests[i].sin_addr, ip, sizeof ip);
            C->log(MOD_ID, "lights: Art-Net universe %d to %s:%d, %d fixtures", uni, ip, LT_PORT, n);
        }
    }
    return uni;
}

static void lights_send(const uint8_t *pkt, int len)
{
    if (lt_sock < 0) {
        lt_sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (lt_sock < 0) return;
        int on = 1;
        setsockopt(lt_sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    }
    int sent = 0;
    for (int i = 0; i < lt_ndests; i++) {
        if (sendto(lt_sock, pkt, (size_t)len, MSG_DONTWAIT, (const struct sockaddr *)&lt_dests[i], sizeof lt_dests[i]) >= 0 ||
            errno == EAGAIN || errno == EWOULDBLOCK)
            sent = 1;
    }
    if (!sent && lt_ndests > 0) { close(lt_sock); lt_sock = -1; }   /* no network yet: a fresh socket next time */
}

/* 1 if another computer sent ArtDmx to our universe since the last call (FLOW VJ's lightbridge
 * with Art-Net on, a console): then the deck stays quiet and the computer has the lights. */
static int lights_other(int uni)
{
    if (lt_rx == -2) return 0;
    if (lt_rx < 0) {
        lt_rx = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (lt_rx < 0) { lt_rx = -2; return 0; }
        int on = 1;
        setsockopt(lt_rx, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons(LT_PORT);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(lt_rx, (struct sockaddr *)&a, sizeof a) != 0) {
            C->log(MOD_ID, "lights: UDP %d is taken: the deck can't hear another Art-Net sender", LT_PORT);
            close(lt_rx);
            lt_rx = -2;
            return 0;
        }
    }
    uint8_t m[64];
    struct sockaddr_in from;
    socklen_t fl = sizeof from;
    int other = 0;
    ssize_t n;
    while ((n = recvfrom(lt_rx, m, sizeof m, MSG_DONTWAIT | MSG_TRUNC, (struct sockaddr *)&from, &fl)) > 0) {
        fl = sizeof from;
        if (lt_artdmx_universe(m, n < (ssize_t)sizeof m ? (int)n : (int)sizeof m) != uni) continue;
        int own = 0;
        for (int i = 0; i < lt_nown; i++) own |= from.sin_addr.s_addr == lt_own[i];
        if (!own) other = 1;
    }
    return other;
}

static void lights_close(void)
{
    if (lt_rx >= 0) close(lt_rx);
    lt_rx = -1;
}

/* One worker step. lead = the deck the lights follow (-1 none); beat = it started a new beat. */
static void lights_step(unsigned step, const az_deck_info *di, const double *ph, int lead, int beat,
                        const mixer_state *ms, int ms_have)
{
    static int was_on, uni, nfix, yielding;
    static unsigned other_at, last_drops;
    static lt_fix fix[LT_MAX_FIX];
    static lt_state st;
    static uint8_t seq, dmx[512], pkt[LT_PKT];
    static int beat_pending;
    static double bass_peak;

    int on = lights_on();
    if (on != was_on) {
        if (on) {
            uni = lights_cfg(fix, &nfix, 0);
            lt_init(&st);
            yielding = 0;
            __atomic_store_n(&pn_yielding, 0, __ATOMIC_RELEASE);
            other_at = 0;
            C->log(MOD_ID, "lights on: the deck runs the lights over Art-Net");
        } else {
            memset(dmx, 0, sizeof dmx);              /* lights off: a few dark frames, then nothing */
            for (int i = 0; i < 3 && !yielding; i++) {
                if (++seq == 0) seq = 1;
                lights_send(pkt, lt_artdmx(pkt, seq, uni, dmx));
            }
            lights_close();
            C->log(MOD_ID, "lights off");
        }
        was_on = on;
    }
    if (!on) { __atomic_store_n(&pn_hits, 0u, __ATOMIC_RELEASE); return; }
    if (step % CFG_STEPS == 3) uni = lights_cfg(fix, &nfix, 1);

    /* gather between frames: a beat edge or bass peak must not fall between two frames */
    if (beat) beat_pending = 1;
    for (int d = 0; d < NDECKS; d++) {
        float b = atomic_exchange_explicit(&lt_low[d], 0.f, memory_order_relaxed);
        if (d == lead && b > bass_peak) bass_peak = b;
    }
    if (step % LT_STEPS != 0) return;

    /* 0.6.2: the panel's hits, taken even between frames so none is lost */
    unsigned hits = __atomic_exchange_n(&pn_hits, 0u, __ATOMIC_ACQ_REL);
    double lbpm = lead >= 0 ? di[lead].bpm : 0;
    if (hits & (1u << PN_DROP)) lt_drop_now(&st, lbpm, lead >= 0);
    else if (hits & (1u << PN_WHITE)) lt_flash(&st, lbpm, 0);
    else if (hits & (1u << PN_COLOR)) lt_flash(&st, lbpm, 1);

    if (lights_other(uni)) {
        if (!yielding) C->log(MOD_ID, "lights: another Art-Net sender on universe %d: the deck steps back", uni);
        yielding = 1;
        other_at = step;
        __atomic_store_n(&pn_yielding, 1, __ATOMIC_RELEASE);
    } else if (yielding && (step - other_at) * POLL_MS > LT_YIELD_MS) {
        yielding = 0;
        __atomic_store_n(&pn_yielding, 0, __ATOMIC_RELEASE);
        C->log(MOD_ID, "lights: the other sender stopped: the deck runs the lights again");
    }

    lt_in in;
    memset(&in, 0, sizeof in);
    in.playing = lead >= 0;
    in.ph = -1;
    in.bar_beat = -1;
    in.dim = -1;
    in.dt_ms = LT_STEPS * POLL_MS;
    in.no_follow = !__atomic_load_n(&pn_follow, __ATOMIC_RELAXED);
    in.no_dropflash = !__atomic_load_n(&pn_dflash, __ATOMIC_RELAXED);
    in.black = __atomic_load_n(&pn_black, __ATOMIC_RELAXED);
    in.strobe = __atomic_load_n(&pn_strobe_held, __ATOMIC_ACQUIRE) && !__atomic_load_n(&pn_safe, __ATOMIC_RELAXED);
    in.cut = 1.0 - __atomic_load_n(&pn_level, __ATOMIC_RELAXED) / 100.0;
    in.look = row_look >= 0 ? C->row_value(row_look) : C->cfg_get(MOD_ID, "artnet_look", 0);
    if (in.look < 0 || in.look >= LT_NCHOICES) in.look = 0;
    if (lead >= 0) {
        in.bpm = di[lead].bpm;
        in.ph = ph[lead];
        in.beat = beat_pending;
        in.bass = bass_peak;
        in.bar_beat = bars_at(lead, (uint32_t)(fnv(di[lead].title, di[lead].artist) ^ di[lead].content_id),
                              di[lead].pos_s, ph[lead]);
        if (ms_have && __atomic_load_n(&pn_fader, __ATOMIC_RELAXED)) in.dim = ms->fader[lead] / 1023.0;
    }
    beat_pending = 0;
    bass_peak = 0;
    lt_track(&st, &in);
    if ((unsigned)st.drops != last_drops) {
        C->log(MOD_ID, "lights: drop on deck %d", lead + 1);
        last_drops = (unsigned)st.drops;
    }
    if (yielding) return;
    lt_render(&st, &in, fix, nfix, dmx);
    if (++seq == 0) seq = 1;
    lights_send(pkt, lt_artdmx(pkt, seq, uni, dmx));
}

/* ---- 0.6.2: FLOW Lights panel: touch, tick, paint, and the worker's part --------------------- */
static int row_panel = -1;                /* "FLOW Lights button" */
static az_surface *pn_btn_surf, *pn_surf;

static long pn_now_ms(void);
static long pn_press_ms;                  /* atomic: when the touch filter last handled a press */
static int pn_logs;                       /* atomic: input lines logged so far (the first few, for tests) */

/* One press at screen x, y, from the touch filter (bus thread) or a surface's on_input (UI thread):
 * atomics only. Returns 1 if it was the panel's (LIGHTS or inside the open panel). */
static int pn_press(int x, int y, int *cur, const char *from)
{
    *cur = PN_NONE;
    if (row_panel >= 0 && C->row_value(row_panel) != 1) return 0;
    if (C->view() != AZ_VIEW_DECK) return 0;
    if (pn_in_button(x, y)) {
        __atomic_store_n(&pn_toggle, 1, __ATOMIC_RELEASE);
        if (__atomic_add_fetch(&pn_logs, 1, __ATOMIC_ACQ_REL) <= 20) C->log(MOD_ID, "lights panel: LIGHTS tapped (%s)", from);
        return 1;
    }
    if (!__atomic_load_n(&pn_open, __ATOMIC_ACQUIRE) || !pn_in_panel(x, y)) return 0;
    *cur = pn_hit(x, y);
    if (*cur == PN_LEVEL) __atomic_store_n(&pn_level, pn_level_at(y), __ATOMIC_RELEASE);
    else if (*cur != PN_NONE) {
        if (*cur == PN_STROBE) __atomic_store_n(&pn_strobe_held, 1, __ATOMIC_RELEASE);
        __atomic_or_fetch(&pn_tapped, 1ull << *cur, __ATOMIC_ACQ_REL);
        if (__atomic_add_fetch(&pn_logs, 1, __ATOMIC_ACQ_REL) <= 20) C->log(MOD_ID, "lights panel: %s tapped (%s)", pn_btn_of(*cur)->label, from);
    }
    return 1;
}
static void pn_release(int cur)
{
    if (cur == PN_STROBE && __atomic_exchange_n(&pn_strobe_held, 0, __ATOMIC_ACQ_REL))
        __atomic_store_n(&pn_strobe_off, 1, __ATOMIC_RELEASE);
}

/* bus thread: rectangle tests and atomics only. The deck screen only; LIGHTS and the open panel only.
 * A passed touch may never show us its lift (EP147 has it), and a tap-only screen may send no lift
 * at all: so a contact after a gap of more than 120 ms is a new press, lift or not. */
static int pn_touch(const az_touch *t, void *user)
{
    (void)user;
    static int touching, taken, cur;
    static long last;
    if (!t) return AZ_TOUCH_PASS;
    long now = pn_now_ms();
    if (__atomic_load_n(&pn_logs, __ATOMIC_RELAXED) < 3 && t->down && !touching) {
        __atomic_add_fetch(&pn_logs, 1, __ATOMIC_ACQ_REL);
        C->log(MOD_ID, "lights panel: touch filter sees %d,%d", t->x, t->y);
    }
    if (!t->down) {
        if (touching && taken) pn_release(cur);
        int was = touching && taken;
        touching = taken = 0;
        cur = PN_NONE;
        return was ? AZ_TOUCH_TAKE : AZ_TOUCH_PASS;
    }
    if (touching && now - last <= 120) {                /* the same finger moving */
        last = now;
        if (taken && cur == PN_LEVEL) __atomic_store_n(&pn_level, pn_level_at(t->y), __ATOMIC_RELEASE);
        return taken ? AZ_TOUCH_TAKE : AZ_TOUCH_PASS;
    }
    if (touching && taken) pn_release(cur);             /* the last one never lifted */
    touching = 1;
    last = now;
    taken = pn_press(t->x, t->y, &cur, "touch");
    if (taken) __atomic_store_n(&pn_press_ms, now, __ATOMIC_RELEASE);
    return taken ? AZ_TOUCH_TAKE : AZ_TOUCH_PASS;
}

/* UI thread: a tap the core hands the surface itself (on cores or screens where the touch filter
 * doesn't see it). A press the touch filter already handled is not counted twice. */
static int pn_input(const az_event *ev, void *user)
{
    (void)user;
    static int cur;
    static long down_ms;
    if (!ev) return 0;
    if (ev->kind == AZ_EV_BACK) {                        /* BACK closes the panel, else it is EP147's */
        if (!__atomic_load_n(&pn_open, __ATOMIC_ACQUIRE)) return 0;
        __atomic_or_fetch(&pn_tapped, 1ull << PN_CLOSE, __ATOMIC_ACQ_REL);
        return 1;
    }
    long now = pn_now_ms();
    if (ev->kind == AZ_EV_UP) { pn_release(cur); cur = PN_NONE; return 1; }
    if (ev->kind != AZ_EV_DOWN && ev->kind != AZ_EV_TAP) return 1;
    if (__atomic_load_n(&pn_logs, __ATOMIC_RELAXED) < 6) {
        __atomic_add_fetch(&pn_logs, 1, __ATOMIC_ACQ_REL);
        C->log(MOD_ID, "lights panel: surface %s at %d,%d", ev->kind == AZ_EV_TAP ? "tap" : "down", ev->x, ev->y);
    }
    if (ev->kind == AZ_EV_TAP && now - down_ms < 1500) return 1;     /* its DOWN came already */
    if (ev->kind == AZ_EV_DOWN) down_ms = now;
    if (now - __atomic_load_n(&pn_press_ms, __ATOMIC_ACQUIRE) < 400) return 1;   /* the touch filter had it */
    int c;
    pn_press(ev->x, ev->y, &c, ev->kind == AZ_EV_TAP ? "surface tap" : "surface down");
    if (ev->kind == AZ_EV_DOWN) cur = c;
    else pn_release(c);                                  /* a tap with no DOWN/UP: STROBE flashes once */
    return 1;
}

/* What the painter shows: the tick fills the idle copy and flips (the painter runs on the overlay thread). */
typedef struct {
    unsigned long long lit;               /* 1 << PN_* drawn lit */
    int level, show, fv_live;             /* show: 0 off, 1 running, 2 stepped back */
    char look[40];
} pn_view;
static pn_view pn_vw[2];
static int pn_vw_cur;                     /* atomic */

static long pn_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void pn_tick(void)
{
    if (!pn_surf || !pn_btn_surf) return;
    static int shown_btn = -1, shown_panel = -1, cfg_level = -1, placed;
    if (!placed) {                                      /* place and show from the UI thread only */
        C->surface_place(pn_btn_surf, (az_rect){ PN_BTN_X, PN_BTN_Y, PN_BTN_W, PN_BTN_H });
        C->surface_place(pn_surf, (az_rect){ PN_X, PN_Y, PN_W, PN_H });
        placed = 1;
    }
    static long flash_until[PN_IDS], fv_at, level_moved;
    static pn_fv fv;
    static int fv_seen;
    long now = pn_now_ms();
    int want_btn = row_panel < 0 || C->row_value(row_panel) == 1;
    int open = __atomic_load_n(&pn_open, __ATOMIC_ACQUIRE);
    if (__atomic_exchange_n(&pn_toggle, 0, __ATOMIC_ACQ_REL)) open = !open;
    if (!want_btn) open = 0;
    if (C->view() != AZ_VIEW_DECK) open = 0;            /* BACK / browse / menu: the panel shuts; LIGHTS brings it back */

    unsigned long long tap = __atomic_exchange_n(&pn_tapped, 0ull, __ATOMIC_ACQ_REL);
    if (!open) tap = 0;
    unsigned long long out = 0;
    unsigned hits = 0;
    for (int id = 1; id < PN_IDS; id++) {
        if (!(tap & (1ull << id))) continue;
        flash_until[id] = now + 250;
        int pad, shift;
        if (pn_act(id) || pn_vjpad(id, &pad, &shift)) out |= 1ull << id;
        switch (id) {
        case PN_WHITE: case PN_COLOR: case PN_DROP: hits |= 1u << id; break;
        case PN_BLACK: __atomic_store_n(&pn_black, !__atomic_load_n(&pn_black, __ATOMIC_RELAXED), __ATOMIC_RELEASE); break;
        case PN_SAFE: __atomic_store_n(&pn_safe, !__atomic_load_n(&pn_safe, __ATOMIC_RELAXED), __ATOMIC_RELEASE); break;
        case PN_FOLLOW: { int nv = !__atomic_load_n(&pn_follow, __ATOMIC_RELAXED); __atomic_store_n(&pn_follow, nv, __ATOMIC_RELEASE); C->cfg_set(MOD_ID, "panel_follow", nv); } break;
        case PN_DFLASH: { int nv = !__atomic_load_n(&pn_dflash, __ATOMIC_RELAXED); __atomic_store_n(&pn_dflash, nv, __ATOMIC_RELEASE); C->cfg_set(MOD_ID, "panel_dropflash", nv); } break;
        case PN_FADER: { int nv = !__atomic_load_n(&pn_fader, __ATOMIC_RELAXED); __atomic_store_n(&pn_fader, nv, __ATOMIC_RELEASE); C->cfg_set(MOD_ID, "panel_fader", nv); } break;
        case PN_SHOW: if (row_lights >= 0 && AZ_HAS(C, row_set)) C->row_set(row_lights, C->row_value(row_lights) != 1); break;
        case PN_CLOSE: open = 0; break;
        default:
            if (id >= PN_COL0 && id <= PN_COL5 && row_look >= 0 && AZ_HAS(C, row_set)) C->row_set(row_look, id - PN_COL0);
        }
    }
    if (out) __atomic_or_fetch(&pn_out, out, __ATOMIC_ACQ_REL);
    if (hits) __atomic_or_fetch(&pn_hits, hits, __ATOMIC_ACQ_REL);
    if (!open && __atomic_exchange_n(&pn_strobe_held, 0, __ATOMIC_ACQ_REL))
        __atomic_store_n(&pn_strobe_off, 1, __ATOMIC_RELEASE);   /* closed while held: let go */
    __atomic_store_n(&pn_open, open, __ATOMIC_RELEASE);

    /* the intensity is saved once the fader rests for a second */
    int level = __atomic_load_n(&pn_level, __ATOMIC_ACQUIRE);
    if (cfg_level < 0) cfg_level = level;
    if (level != cfg_level) {
        if (!level_moved) level_moved = now;
        if (now - level_moved > 1000) { C->cfg_set(MOD_ID, "panel_level", level); cfg_level = level; level_moved = 0; }
    }

    int s = __atomic_load_n(&pn_fv_serial, __ATOMIC_ACQUIRE);
    if (s != fv_seen && !atomic_flag_test_and_set_explicit(&pn_fv_lock, memory_order_acquire)) {
        fv = pn_fv_shared;
        atomic_flag_clear_explicit(&pn_fv_lock, memory_order_release);
        fv_seen = s;
        fv_at = now;
    }
    int fv_live = fv_at && now - fv_at < PN_FV_STALE_MS;

    pn_view v;
    memset(&v, 0, sizeof v);
    v.fv_live = fv_live;
    v.level = level;
    v.show = row_lights < 0 || C->row_value(row_lights) != 1 ? 0 : __atomic_load_n(&pn_yielding, __ATOMIC_ACQUIRE) ? 2 : 1;
    if (fv_live) snprintf(v.look, sizeof v.look, "%s", fv.look);
    for (int id = 1; id < PN_IDS; id++) if (flash_until[id] > now) v.lit |= 1ull << id;
    if (__atomic_load_n(&pn_strobe_held, __ATOMIC_ACQUIRE)) v.lit |= 1ull << PN_STROBE;
    if (fv_live ? fv.hold : 0) v.lit |= 1ull << PN_HOLD;
    if (fv_live && fv.autop) v.lit |= 1ull << PN_AUTO;
    if (fv_live ? fv.safe : __atomic_load_n(&pn_safe, __ATOMIC_RELAXED)) v.lit |= 1ull << PN_SAFE;
    if (__atomic_load_n(&pn_black, __ATOMIC_RELAXED) || (fv_live && fv.black)) v.lit |= 1ull << PN_BLACK;
    if (__atomic_load_n(&pn_follow, __ATOMIC_RELAXED)) v.lit |= 1ull << PN_FOLLOW;
    if (__atomic_load_n(&pn_dflash, __ATOMIC_RELAXED)) v.lit |= 1ull << PN_DFLASH;
    if (__atomic_load_n(&pn_fader, __ATOMIC_RELAXED)) v.lit |= 1ull << PN_FADER;
    if (v.show) v.lit |= 1ull << PN_SHOW;
    int look = row_look >= 0 ? C->row_value(row_look) : 0;
    if (look >= 0 && look <= PN_COL5 - PN_COL0) v.lit |= 1ull << (PN_COL0 + look);

    if (want_btn != shown_btn) { C->surface_show(pn_btn_surf, want_btn); shown_btn = want_btn; }
    if (open != shown_panel) {
        C->surface_show(pn_surf, open);
        shown_panel = open;
        C->surface_redraw(pn_btn_surf);
    }
    int cur = __atomic_load_n(&pn_vw_cur, __ATOMIC_ACQUIRE);
    if (open && memcmp(&v, &pn_vw[cur], sizeof v) != 0) {
        pn_vw[!cur] = v;
        __atomic_store_n(&pn_vw_cur, !cur, __ATOMIC_RELEASE);
        C->surface_redraw(pn_surf);
    }
}

#define PN_BG 0x0E0C14u
#define PN_BTN 0x22202Au
#define PN_EDGE 0x46445Au
#define PN_TXT 0xE1E1E8u
#define PN_DIM 0x82808Cu
#define PN_BLUE 0x2896E6u
#define PN_PINK 0xE6288Cu
#define PN_GREEN 0x3CC85Au
#define PN_RED 0xDC2828u
#define PN_AMBER 0xF0961Eu
#define PN_TEAL 0x00B4AAu

static void pn_text_c(az_draw *d, int cx, int y, const char *s, int px, uint32_t rgb)
{
    C->draw_text(d, cx - C->text_width(s, px) / 2, y, s, px, rgb);
}

static void pn_paint_btn(az_draw *d, az_rect a, void *user)
{
    (void)user;
    int open = __atomic_load_n(&pn_open, __ATOMIC_ACQUIRE);
    C->fill_rect(d, a.x, a.y, a.w, a.h, open ? PN_PINK : 0xFFFFFFu);
    C->draw_rect(d, a.x, a.y, a.w, a.h, 2, PN_PINK);
    pn_text_c(d, a.x + a.w / 2, a.y + (a.h - 18) / 2, "LIGHTS", 12, open ? 0xFFFFFFu : 0x000000u);
}

static const uint32_t PN_SWATCH[6][2] = {
    { 0, 0 }, { 0xFF2418u, 0x00B2A8u }, { 0xE600C8u, 0x00C8FFu }, { 0xFF6E00u, 0x7800FFu },
    { 0x0046FFu, 0x96E6FFu }, { 0xFFFFFFu, 0xFFAA5Au },
};

static void pn_paint(az_draw *d, az_rect a, void *user)
{
    (void)user;
    const pn_view *v = &pn_vw[__atomic_load_n(&pn_vw_cur, __ATOMIC_ACQUIRE)];
    int X = a.x, Y = a.y;
    C->fill_rect(d, X, Y, a.w, a.h, PN_BG);
    C->draw_rect(d, X, Y, a.w, a.h, 2, PN_PINK);
    C->draw_text(d, X + 14, Y + 8, "FLOW LIGHTS", 18, PN_TXT);
    char st[80];
    if (v->fv_live) snprintf(st, sizeof st, "FlowVJ: %s", v->look[0] ? v->look : "seen");
    else snprintf(st, sizeof st, "Looks and hits go to FlowVJ");
    C->fill_rect(d, X + 180, Y + 8, 400, 24, v->fv_live ? 0x142D1Eu : 0x1C1A24u);
    C->fill_rect(d, X + 190, Y + 15, 10, 10, v->fv_live ? PN_GREEN : PN_DIM);
    C->draw_text(d, X + 208, Y + 10, st, 12, PN_TXT);
    C->draw_text(d, X + 14, Y + 44, "LOOKS", 10, PN_DIM);
    C->draw_text(d, X + 478, Y + 44, "HITS", 10, PN_DIM);
    C->draw_line(d, X + 10, Y + 276, X + a.w - 10, Y + 276, 1, PN_EDGE);
    C->draw_text(d, X + 14, Y + 284, "DECK SHOW COLOUR", 10, PN_DIM);
    C->draw_text(d, X + 14, Y + 390, "DECK SHOW FOLLOWS THE MUSIC", 10, PN_DIM);
    if (v->show == 0)
        C->draw_text(d, X + 14, Y + 476, "The deck's own show is off: tap DECK SHOW to run it (Art-Net node needed).", 11, PN_DIM);
    else if (v->show == 2)
        C->draw_text(d, X + 14, Y + 476, "FlowVJ or a console has the Art-Net lights: the deck show waits.", 11, PN_DIM);

    for (int i = 0; i < PN_NBTNS; i++) {
        const pn_btn *b = &PN_BTNS[i];
        int x = X + b->x, y = Y + b->y, lit = (v->lit >> b->id) & 1;
        if (b->id == PN_LEVEL) {
            C->draw_text(d, x, y - 20, "INTENSITY", 10, PN_DIM);
            C->fill_rect(d, x, y, b->w, b->h, PN_BTN);
            C->draw_rect(d, x, y, b->w, b->h, 1, PN_EDGE);
            int top = Y + PN_LEVEL_TOP, bot = Y + PN_LEVEL_BOT, lv = v->level;
            int fy = bot - (bot - top) * lv / 100;
            C->fill_rect(d, x + 70, top, 40, bot - top, 0x19191Eu);
            C->fill_rect(d, x + 70, fy, 40, bot - fy, PN_AMBER);
            C->fill_rect(d, x + 60, fy - 6, 60, 12, PN_TXT);
            char pct[8];
            snprintf(pct, sizeof pct, "%d%%", lv);
            pn_text_c(d, x + 150, y + b->h / 2 - 14, pct, 18, PN_TXT);
            continue;
        }
        uint32_t accent = PN_BLUE, fill = lit ? 0x14283Cu : PN_BTN;
        if (b->id == PN_BLACK) { accent = PN_RED; if (lit) fill = 0x460A0Au; }
        if (b->id >= PN_FOLLOW && b->id <= PN_FADER) { accent = PN_GREEN; if (lit) fill = 0x142D1Eu; }
        if (b->id == PN_SHOW) { accent = PN_TEAL; }
        if (b->id >= PN_COL0 && b->id <= PN_COL5) { accent = PN_PINK; fill = PN_BTN; }
        C->fill_rect(d, x, y, b->w, b->h, fill);
        C->draw_rect(d, x, y, b->w, b->h, lit ? 3 : 1, lit ? accent : PN_EDGE);
        if (b->id >= PN_COL0 && b->id <= PN_COL5) {
            int k = b->id - PN_COL0;
            if (k == 0) {
                static const uint32_t cyc[4] = { 0xFF2418u, 0xE600C8u, 0xFF6E00u, 0x0046FFu };
                for (int j = 0; j < 4; j++) C->fill_rect(d, x + 6 + j * 20, y + 6, 20, 40, cyc[j]);
            } else {
                C->fill_rect(d, x + 6, y + 6, 40, 40, PN_SWATCH[k][0]);
                C->fill_rect(d, x + 46, y + 6, 40, 40, PN_SWATCH[k][1]);
            }
            pn_text_c(d, x + b->w / 2, y + 50, b->label, 11, PN_TXT);
            continue;
        }
        const char *label = b->label;
        if (b->id == PN_SHOW) label = v->show == 0 ? "DECK SHOW: OFF" : v->show == 2 ? "DECK SHOW: WAITS" : "DECK SHOW: ON";
        int big = b->h >= 60 ? 20 : 13;
        uint32_t tc = lit ? accent : PN_TXT;
        if (b->sub) {
            pn_text_c(d, x + b->w / 2, y + b->h / 2 - big * 3 / 4 - 6, label, big, tc);
            pn_text_c(d, x + b->w / 2, y + b->h / 2 + big * 3 / 4 - 8, b->sub, 10, PN_DIM);
        } else {
            pn_text_c(d, x + b->w / 2, y + (b->h - big * 3 / 2) / 2, label, big, tc);
        }
    }
}


/* Worker thread: send what was tapped, keep a held STROBE alive, read FlowVJ's FVS1 on 9104. */
static int pn_rx = -1;                    /* -2: port taken */
static void pn_send_fvc(const char *act)
{
    static unsigned seq = PN_SEQ_BASE;
    char b[48];
    int n = pn_fvc(b, (int)sizeof b, seq++, act);
    if (n <= 0) return;
    net_on();
    if (sock < 0) {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        int on = 1;
        if (sock >= 0) setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    }
    for (int i = 0; i < ndests && sock >= 0; i++) {
        struct sockaddr_in to = dests[i];
        to.sin_port = htons(PN_TAP_PORT);
        sendto(sock, b, (size_t)n, MSG_DONTWAIT, (const struct sockaddr *)&to, sizeof to);
    }
    net_off();
}

static void pn_worker_step(unsigned step)
{
    static unsigned n = PN_N_BASE, strobe_at;
    unsigned long long out = __atomic_exchange_n(&pn_out, 0ull, __ATOMIC_ACQ_REL);
    for (int id = 1; out && id < PN_IDS; id++) {
        if (!(out & (1ull << id))) continue;
        int pad, shift;
        if (pn_vjpad(id, &pad, &shift)) {
            char b[160];
            n++;
            for (int down = 1; down >= 0; down--) {
                int len = pn_vjpad_json(b, (int)sizeof b, pad, down, shift, n);
                if (len > 0) { send_pkt(b, len); send_pkt(b, len); }   /* twice, like Pad Lights */
            }
        }
        const char *act = pn_act(id);
        if (act) {
            if (id == PN_STROBE) strobe_at = step;
            pn_send_fvc(act);
        }
    }
    if (__atomic_load_n(&pn_strobe_held, __ATOMIC_ACQUIRE) && step - strobe_at >= 1000 / POLL_MS) {
        strobe_at = step;
        pn_send_fvc("strobe");                       /* FlowVJ lets a strobe go after 2.5 s */
    }
    if (__atomic_exchange_n(&pn_strobe_off, 0, __ATOMIC_ACQ_REL)) pn_send_fvc("strobe_off");

    if (step % 20 != 7 || pn_rx == -2) return;      /* FVS1: every 100 ms */
    if (pn_rx < 0) {
        pn_rx = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (pn_rx < 0) { pn_rx = -2; return; }
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons(PN_FV_PORT);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(pn_rx, (struct sockaddr *)&a, sizeof a) != 0) {
            C->log(MOD_ID, "lights panel: UDP %d is taken: no FlowVJ state on the panel", PN_FV_PORT);
            close(pn_rx);
            pn_rx = -2;
            return;
        }
    }
    char b[1024];
    ssize_t len;
    pn_fv fv;
    int got = 0;
    while ((len = recv(pn_rx, b, sizeof b, MSG_DONTWAIT)) > 0)
        if (pn_fv_parse(b, (int)len, &fv)) got = 1;
    if (got) {
        while (atomic_flag_test_and_set_explicit(&pn_fv_lock, memory_order_acquire)) { struct timespec t = { 0, 50000 }; nanosleep(&t, NULL); }
        pn_fv_shared = fv;
        atomic_flag_clear_explicit(&pn_fv_lock, memory_order_release);
        __atomic_add_fetch(&pn_fv_serial, 1, __ATOMIC_ACQ_REL);
    }
}

/* ---- the worker -------------------------------------------------------------------------- */

static void *worker(void *arg)
{
    (void)arg;
    double last[NDECKS];
    unsigned beats[NDECKS] = { 0 };
    unsigned lt_beats_seen[NDECKS] = { 0 };   /* 0.6.0: beats the lights already took */
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
    fx_blink blink[2];
    fx_deck fx_sent[4];
    fx_touch_age touch_age[4];
    unsigned fx_last_sent = 0;
    memset(touch_age, 0, sizeof touch_age);
    memset(blink, 0, sizeof blink);
    memset(fx_sent, 0, sizeof fx_sent);
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
                char k[8]; deck_key(d, &di[d], k);
                n += snprintf(buf + n, sizeof buf - (size_t)n, ",\"key\":\"%s\"}", k);   /* 0.6.5 */
                send_pkt(buf, n);
            }
            track_sig[d] = sig;
        }

        link_step(di, ph, since, beats, track_sig, step, &ms, ms_have);

        if (on && step % STATE_STEPS == 0) {
            int n = snprintf(buf, sizeof buf, "{\"flowbeat\":1,\"type\":\"state\",\"seq\":%u,\"decks\":[",
                             seq++);
            for (int d = 0; d < NDECKS; d++) {
                float lv = atomic_exchange_explicit(&lvl[d], 0.f, memory_order_relaxed);
                float lo = atomic_exchange_explicit(&low[d], 0.f, memory_order_relaxed);
                char k[8]; deck_key(d, &di[d], k);   /* 0.6.5: the key, for Key Sync */
                n += snprintf(buf + n, sizeof buf - (size_t)n,
                              "%s{\"d\":%d,\"loaded\":%d,\"play\":%d,\"bpm\":%.2f,\"ph\":%.4f,\"pos\":%.3f,\"id\":%u,\"start\":%u,\"lv\":%.3f,\"lo\":%.3f,\"key\":\"%s\"}",
                              d ? "," : "", d + 1, di[d].loaded == 1, di[d].loaded == 1 && di[d].playing == 1,
                              di[d].loaded == 1 ? di[d].bpm : 0.0, ph[d],
                              di[d].loaded == 1 ? di[d].pos_s : -1.0, (unsigned)di[d].content_id, since[d], lv, lo, k);
            }
            /* 0.6.5: the deck on air (same rule as Link and the lights), 0 = none */
            n += snprintf(buf + n, sizeof buf - (size_t)n, "],\"air\":%d}", on_air_deck(since, &ms, ms_have) + 1);
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
        /* 0.6.0: the lights follow the playing deck whose channel fader is up most (else the last started) */
        {
            int lead = on_air_deck(since, &ms, ms_have);
            lights_step(step, di, ph, lead, lead >= 0 && beats[lead] != lt_beats_seen[lead], &ms, ms_have);
            for (int d = 0; d < NDECKS; d++) lt_beats_seen[d] = beats[d];
        }

        if (on && ms_have && step % MIXER_STEPS == 0 &&
            (row_mixer < 0 || C->row_value(row_mixer) == 1) &&
            (mix_last_sent == 0 || step - mix_last_sent >= MIXER_KEEP || mixer_changed(&ms, &ms_sent))) {
            int n = mixer_json(buf, (int)sizeof buf, seq++, &ms);
            send_pkt(buf, n);
            ms_sent = ms;
            mix_last_sent = step;
        }

        /* Stem FX / Drum Roll / loop (0.5.3): when one changes, at most every 20 ms, and every second */
        if (step % CFG_STEPS == 2) fx_resolve();
        if (on && step % FX_STEPS == 1) {
            fx_side sides[2];
            int blinking[2];
            double t = (double)step * POLL_MS;   /* the worker's own clock: no clock_gettime (TSan harness) */
            for (int s = 0; s < 2; s++) {
                fx_unpack(atomic_load_explicit(&fxl.word[s], memory_order_acquire), &sides[s]);
                blinking[s] = fx_blinking(&blink[s], atomic_load_explicit(&fxl.blinks[s], memory_order_relaxed), t);
            }
            fx_deck fd[4];
            fx_decks(sides, blinking, fd);
            for (int d = 0; d < 4; d++) {
                fd[d].sfx = stem_fx_on ? (int)(stem_fx_on(d) & 15u) : -1;
                fx_touch_read(&touch_age[d], touch_vj_get ? touch_vj_get(d) : 0ull, step, &fd[d].t);
            }
            int changed = fx_last_sent == 0;
            for (int d = 0; d < 4; d++) if (!fx_deck_same(&fd[d], &fx_sent[d])) changed = 1;
            if (changed || step - fx_last_sent >= FX_KEEP) {
                int n = fx_json(buf, (int)sizeof buf, seq++, fd);
                send_pkt(buf, n);
                memcpy(fx_sent, fd, sizeof fx_sent);
                fx_last_sent = step;
            }
        }

        pn_worker_step(step);   /* 0.6.2: the FLOW Lights panel's taps */

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
typedef struct { uint32_t id; const char *title; char path[1024]; char key[8]; int found; } find_ctx;
static int find_track(const az_track *t, void *user)
{
    find_ctx *f = (find_ctx *)user;
    if (t->id != f->id || !t->path) return 0;
    if (f->title && f->title[0] && t->title && strcmp(t->title, f->title) != 0) return 0;
    snprintf(f->path, sizeof f->path, "%s", t->path);
    snprintf(f->key, sizeof f->key, "%s", t->key ? t->key : "");   /* 0.6.5 */
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
static size_t load_analysis(int deck, const az_deck_info *di, unsigned char *blob)
{
    find_ctx f;
    memset(&f, 0, sizeof f);
    f.id = di->content_id;
    f.title = di->title;
    int stick = -1;
    for (int s = 0; s < 2 && !f.found; s++)
        if (C->lib_tracks(MOD_ID, s, 0, find_track, &f) >= 0 && f.found) stick = s;
    if (stick < 0) return 0;
    lib_key_set(deck, di->content_id, f.key);   /* 0.6.5: the key, found with the path */
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
        int send = on && wave_on;
        if (!send && !link_on() && !lights_on()) continue;   /* Link and the lights read the grids for the bar even when nothing is sent */
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
                if (blob[d]) blen[d] = load_analysis(d, &di, blob[d]);
                if (blob[d]) bars_set(d, sig, blob[d], blen[d]);
            }
            if (!send || !blen[d] || now < next_at[d]) continue;
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
    static const az_row_v1 audio_row = {
        .size = sizeof audio_row,
        .key = "link-audio",           /* keys are [a-z0-9-] */
        .label = "Send decks to Ableton",  /* labels are at most 31 bytes */
        .kind = AZ_ROW_TOGGLE,
        .help = "Turns on Ableton Link and offers each deck's own sound (before the mixer) to the session "
                "as Link Audio channels \"Deck 1\"-\"Deck 4\" from \"XDJ-AZ\". In Ableton Live 12.4+, pick one "
                "as a track's input to record or process it. Wired network advised. Off by default.",
    };
    static const az_row_v1 link_row = {
        .size = sizeof link_row,
        .key = "link",
        .label = "Ableton Link from this deck",
        .kind = AZ_ROW_TOGGLE,
        .help = "This deck leads an Ableton Link session on its network: Ableton, AUTO/PILOT and other "
                "Link apps follow the playing deck's tempo, beat and bar (the 1 from rekordbox's grid or "
                "FLOW VJ's phrase-sync button). No computer bridge needed. Off by default.",
    };
    static const az_row_v1 lights_row = {
        .size = sizeof lights_row,
        .key = "artnet",
        .label = "Lights from this deck",
        .kind = AZ_ROW_TOGGLE,
        .help = "The deck runs the lights itself, no computer: sends Art-Net to a node on its network "
                "(beat chases, bass pump, breakdown fade, white hit on the drop, fader dims). Steps back "
                "while FLOW VJ or a console sends Art-Net. Rig: flow-beat:artnet_patch. Off by default.",
    };
    static const az_row_v1 panel_row = {
        .size = sizeof panel_row,
        .key = "lights-panel",
        .label = "FLOW Lights button",
        .kind = AZ_ROW_TOGGLE,
        .flags = AZ_ROW_DEFAULT_ON,
        .help = "A LIGHTS button in the deck screen's top bar opens a big panel: FlowVJ Looks 1-8, hits "
                "(white, strobe, drop, colour, fog, blackout) and the deck show's colours, intensity and "
                "beat follow. Off hides the button.",
    };
    static const char *const look_choices[] = LT_CHOICES;
    static const az_row_v1 look_row = {
        .size = sizeof look_row,
        .key = "artnet-look",
        .label = "Deck lights colours",
        .kind = AZ_ROW_CHOICE,
        .parent = "artnet",
        .help = "The colours for Lights from this deck. Cycle moves to the next pair every 16 bars.",
        .choices = look_choices,
        .n_choices = LT_NCHOICES,
        .default_choice = 0,
    };
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        row_send = C->register_row(MOD_ID, &send_row);
        row_mixer = C->register_row(MOD_ID, &mixer_row);
        row_sniff = C->register_row(MOD_ID, &sniff_row);
        row_wave = C->register_row(MOD_ID, &wave_row);
        row_link = C->register_row(MOD_ID, &link_row);
        row_audio = C->register_row(MOD_ID, &audio_row);
        row_lights = C->register_row(MOD_ID, &lights_row);
        row_look = C->register_row(MOD_ID, &look_row);
        row_panel = C->register_row(MOD_ID, &panel_row);
    }
    /* 0.6.2: the FLOW Lights panel: a LIGHTS button in the top bar and the panel, deck screen only */
    pn_level = C->cfg_get(MOD_ID, "panel_level", 100);
    if (pn_level < 0 || pn_level > 100) pn_level = 100;
    pn_follow = C->cfg_get(MOD_ID, "panel_follow", 1) != 0;
    pn_dflash = C->cfg_get(MOD_ID, "panel_dropflash", 1) != 0;
    pn_fader = C->cfg_get(MOD_ID, "panel_fader", 1) != 0;
    if (AZ_HAS(C, surface_create) && AZ_HAS(C, surface_show) && AZ_HAS(C, surface_place) &&
        AZ_HAS(C, surface_redraw) && AZ_HAS(C, view) && AZ_HAS(C, touch_filter) && AZ_HAS(C, draw_line)) {
        /* AZ_LAYER_TOP: inside one layer the core restacks surfaces in creation order whenever one
         * maps, so an OVER_EP147 panel can end up under Custom Screen's waveform panel. TOP is above
         * every OVER_EP147 surface (Touch FX's panel is TOP too). It is above the Mod Menu as well,
         * so both surfaces are deck-screen only and the tick shuts the panel off the deck screen. */
        az_surface_v1 sb = { .size = sizeof sb, .layer = AZ_LAYER_TOP, .views = AZ_VIEW_BIT(AZ_VIEW_DECK),
                             .on_paint = pn_paint_btn, .on_input = pn_input, .user = NULL };
        az_surface_v1 sp = { .size = sizeof sp, .layer = AZ_LAYER_TOP, .views = AZ_VIEW_BIT(AZ_VIEW_DECK),
                             .on_paint = pn_paint, .on_input = pn_input, .user = NULL };
        az_surface *b = C->surface_create(MOD_ID, &sb);
        az_surface *p = b ? C->surface_create(MOD_ID, &sp) : NULL;
        if (b && p) {
            pn_vw[0].level = pn_level;
            if (C->touch_filter(MOD_ID, pn_touch, NULL) == 0) { pn_btn_surf = b; pn_surf = p; }
            else C->log(MOD_ID, "lights panel: touch_filter refused: no panel");
        } else {
            C->log(MOD_ID, "lights panel: no screen surface left (16 in all): no panel. The rest runs as before.");
        }
    }
    /* each deck's own audio, before the hardware mixer (0.3.0) */
    if (AZ_HAS(C, audio_tap))
        for (int d = 0; d < NDECKS; d++)
            if (C->audio_tap(MOD_ID, AZ_TAP_DECK(d), TAP_FRAMES, deck_tap, (void *)(intptr_t)d) != 0)
                C->log(MOD_ID, "audio_tap deck %d refused", d + 1);
    /* the mixer panel's input frames, read-only (Mixer Link and the sniffer) */
    if (AZ_HAS(C, frame_filter) &&
        C->frame_filter(MOD_ID, (1u << AZ_BUS_MIXER_IN) | AZ_FILTER_RAW, mix_filter, NULL) != 0)
        C->log(MOD_ID, "frame_filter refused: no mixer data");
    /* 0.5.3: the pad / button LED frame as AZ-MODS painted it (Drum Roll, HOLD, loop lights), read only */
    if (AZ_HAS(C, frame_filter) && C->frame_filter(MOD_ID, 1u << AZ_BUS_ERP_LED, led_filter, NULL) != 0)
        C->log(MOD_ID, "frame_filter refused: no Drum Roll / loop state");
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
    .tick = pn_tick,
    .on_frame = NULL,
};

const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
