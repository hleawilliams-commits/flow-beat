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

#include "azmod-mod.h"

#define MOD_ID "flow-beat"
#define NDECKS 4
#define POLL_MS 5            /* beat edges are found within 5 ms */
#define STATE_STEPS 4        /* a state packet every 4 steps: about 50 a second */
#define TRACK_STEPS 400      /* titles repeated about every 2 s, so a late receiver learns them */
#define CFG_STEPS 400        /* settings re-read about every 2 s */
#define DEFAULT_HOST "255.255.255.255"
#define DEFAULT_PORT 9100

static const az_core_v1 *C;
static int row_send = -1;    /* the "Send beat to FLOW" toggle */
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

    if (!changed && ndests == old_n && memcmp(old, dests, sizeof dests[0] * (size_t)ndests) == 0)
        return;
    snprintf(dest_host, sizeof dest_host, "%s", host);
    dest_port = port;
    if (!quiet || changed) {
        for (int i = 0; i < ndests; i++) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &dests[i].sin_addr, ip, sizeof ip);
            C->log(MOD_ID, "sending to %s:%d", ip, dest_port);
        }
    }
}

static void send_pkt(const char *buf, int len)
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
                n += snprintf(buf + n, sizeof buf - (size_t)n,
                              "%s{\"d\":%d,\"loaded\":%d,\"play\":%d,\"bpm\":%.2f,\"ph\":%.4f,\"pos\":%.3f,\"id\":%u,\"start\":%u}",
                              d ? "," : "", d + 1, di[d].loaded == 1, di[d].loaded == 1 && di[d].playing == 1,
                              di[d].loaded == 1 ? di[d].bpm : 0.0, ph[d],
                              di[d].loaded == 1 ? di[d].pos_s : -1.0, (unsigned)di[d].content_id, since[d]);
            }
            n += snprintf(buf + n, sizeof buf - (size_t)n, "]}");
            send_pkt(buf, n);
        }

        struct timespec ts = { 0, POLL_MS * 1000000L };
        nanosleep(&ts, NULL);
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
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value))
        row_send = C->register_row(MOD_ID, &send_row);
    read_cfg(0);
    if (C->on_deck_event(MOD_ID, on_deck, NULL) != 0) C->log(MOD_ID, "on_deck_event refused");
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
