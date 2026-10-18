/* SPDX-License-Identifier: GPL-3.0-only
 * Crate Remote: browse both USBs' libraries on an iPad or phone, see what fits the playing
 * deck, and queue picks. The deck serves one web page on the local network, gated by a PIN
 * shown in this mod's Mod Menu page. Read-only: no loading, no library writes, no controls.
 *
 * Threads: one HTTP thread does all network and library work (lib_* must not run on the UI
 * thread). The UI thread only draws the status page and shows a banner when a pick arrives.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include "azmod-mod.h"
#include "server.h"
#include "ui_html.h"
#include "nav.h"

#define MOD_ID "crate-remote"
#ifndef SOCK_CLOEXEC            /* macOS host tests; the deck is Linux */
#define SOCK_CLOEXEC 0
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#define CR_PORT 8765
#define CR_QUEUE_MAX 12

static const az_core_v1 *C;
static int cr_pin;                 /* fixed for this EP147 run; shown on the mod page */
static int cr_enabled = 1;         /* __atomic: the Mod Menu toggle */
static int cr_row = -1;

/* Shared with the UI thread under cr_mu (UI side only ever trylocks). */
static pthread_mutex_t cr_mu = PTHREAD_MUTEX_INITIALIZER;
static char cr_addr[64];           /* the address to show first: Wi-Fi, else cable, else any */
static char cr_addr_wifi[64], cr_addr_wired[64];
static int cr_mdns_on;             /* the crate-remote.local helper is running */
static char cr_banner_text[112];
static unsigned cr_banner_seq;     /* __atomic */
static int cr_tracks_seen[2] = {-1, -1};
static long cr_requests;           /* __atomic */
static int cr_port_state;          /* __atomic: 0 closed, 1 serving, -1 port busy; logged by tick */

/* A deck request from the page, handed to the UI thread under cr_mu. */
typedef struct { int stick, deck, depth, path[8]; uint32_t id; char title[96], where[160]; } cr_navreq;
static cr_navreq cr_nreq;
static unsigned cr_nreq_seq;       /* __atomic */
static char cr_nav_msg[160];       /* status for the page, written by the UI thread */
static int cr_load_allowed = 1;    /* __atomic: Mod Menu "iPad can load decks" */
static int cr_source_touched = 1;  /* __atomic: the DJ pressed SOURCE (or we never switched) */

typedef struct { int stick; uint32_t id; char title[96], artist[64], key[8]; double bpm, len; } cr_pick;
static cr_pick cr_queue[CR_QUEUE_MAX];   /* HTTP thread only */
static int cr_nqueue;

/* ---- library cache (HTTP thread only) ---- */
typedef struct { uint32_t id; char *title, *artist, *key; double bpm, len; } cr_track;
typedef struct { int id, parent, folder; char *name; } cr_pl;
typedef struct { int state; cr_track *t; int nt, ct; cr_pl *p; int np, cp; long at;
                 uint32_t **mem; int *nmem; int members; } cr_stick;  /* mem[i]: playlist i's ids */
static cr_stick cr_lib[2];

/* Seconds the HTTP thread has been running, counted by its own 1 s poll loop (HTTP thread
 * only). Approximate is fine for cache ageing and address refresh, and it keeps the thread
 * off the clock the core and harness own. */
static long cr_secs;
static long cr_now_s(void) { return cr_secs; }
static char *cr_dup(const char *s) { return strdup(s ? s : ""); }

static void cr_stick_free(cr_stick *s)
{
    for (int i = 0; i < s->nt; i++) { free(s->t[i].title); free(s->t[i].artist); free(s->t[i].key); }
    for (int i = 0; i < s->np; i++) { free(s->p[i].name); if (s->mem) free(s->mem[i]); }
    free(s->t); free(s->p); free(s->mem); free(s->nmem);
    memset(s, 0, sizeof *s);
}
static int cr_collect_track(const az_track *t, void *user)
{
    cr_stick *s = user;
    if (s->nt == s->ct) {
        int c = s->ct ? s->ct * 2 : 1024;
        cr_track *n = realloc(s->t, sizeof *n * (size_t)c);
        if (!n) return 1;
        s->t = n; s->ct = c;
    }
    cr_track *x = &s->t[s->nt++];
    x->id = t->id; x->title = cr_dup(t->title); x->artist = cr_dup(t->artist); x->key = cr_dup(t->key);
    x->bpm = t->bpm > 0 ? t->bpm : 0; x->len = t->len_s > 0 ? t->len_s : 0;
    return 0;
}
static int cr_collect_pl(int pl_id, int parent, const char *name, int folder, void *user)
{
    cr_stick *s = user;
    if (s->np == s->cp) {
        int c = s->cp ? s->cp * 2 : 128;
        cr_pl *n = realloc(s->p, sizeof *n * (size_t)c);
        if (!n) return 1;
        s->p = n; s->cp = c;
    }
    s->p[s->np++] = (cr_pl){pl_id, parent, folder, cr_dup(name)};
    return 0;
}
/* Load (or reload when stale/forced) one stick's tracks and playlists. */
static cr_stick *cr_stick_get(int stick, int force)
{
    if (stick < 0 || stick > 1) return NULL;
    cr_stick *s = &cr_lib[stick];
    if (!force && s->state && cr_now_s() - s->at < 300) return s;
    cr_stick_free(s);
    int r = C->lib_tracks(MOD_ID, stick, 0, cr_collect_track, s);
    if (r < 0) { s->state = -1; s->at = cr_now_s(); }
    else {
        C->lib_playlists(MOD_ID, stick, cr_collect_pl, s);
        s->state = 1; s->at = cr_now_s();
    }
    pthread_mutex_lock(&cr_mu);
    cr_tracks_seen[stick] = s->state == 1 ? s->nt : 0;
    pthread_mutex_unlock(&cr_mu);
    return s;
}
static const cr_track *cr_find(cr_stick *s, uint32_t id)
{
    for (int i = 0; s && i < s->nt; i++) if (s->t[i].id == id) return &s->t[i];
    return NULL;
}

/* ---- playlist paths for the navigator (HTTP thread) ---- */
typedef struct { uint32_t *v; int n, cap; } cr_ids;
static int cr_collect_id(const az_track *t, void *user)
{
    cr_ids *a = user;
    if (a->n == a->cap) {
        int c = a->cap ? a->cap * 2 : 64;
        uint32_t *n = realloc(a->v, sizeof *n * (size_t)c);
        if (!n) return 1;
        a->v = n; a->cap = c;
    }
    a->v[a->n++] = t->id;
    return 0;
}
static void cr_members(cr_stick *s, int stick)
{
    if (s->members || s->state != 1) return;
    s->mem = calloc((size_t)s->np + 1, sizeof *s->mem);
    s->nmem = calloc((size_t)s->np + 1, sizeof *s->nmem);
    if (!s->mem || !s->nmem) return;
    for (int i = 0; i < s->np; i++) {
        if (s->p[i].folder) continue;
        cr_ids a = {0};
        C->lib_tracks(MOD_ID, stick, s->p[i].id, cr_collect_id, &a);
        s->mem[i] = a.v; s->nmem[i] = a.n;
    }
    s->members = 1;
}
static int cr_pl_index(cr_stick *s, int id)
{
    for (int i = 0; i < s->np; i++) if (s->p[i].id == id) return i;
    return -1;
}
/* Rows from the top of the PLAYLIST tree down to playlist index pi (siblings in the DJ's
 * order, as lib_playlists reports them). 0 = not reachable (too deep, >249, a loop). */
static int cr_tree_path(cr_stick *s, int pi, int *path, int *depth, char *where, size_t wn)
{
    int chain[8], n = 0;
    for (int cur = pi; cur >= 0; cur = cr_pl_index(s, s->p[cur].parent)) {
        if (n == 7) return 0;
        for (int k = 0; k < n; k++) if (chain[k] == cur) return 0;
        chain[n++] = cur;
        if (s->p[cur].parent == 0) break;
    }
    *depth = 0; where[0] = 0;
    for (int k = n - 1; k >= 0; k--) {
        int me = chain[k], parent = s->p[me].parent, row = 0;
        int top = cr_pl_index(s, parent) < 0;      /* unknown parent = top level */
        for (int j = 0; j < s->np && j != me; j++)
            if (s->p[j].parent == parent || (top && cr_pl_index(s, s->p[j].parent) < 0)) row++;
        if (row > 249) return 0;
        path[(*depth)++] = row;
        size_t l = strlen(where);
        snprintf(where + l, wn - l, "%s%s", l ? " / " : "", s->p[me].name);
    }
    return 1;
}
/* Find a playlist row for (stick, id): the given playlist first, else the shortest path
 * with the earliest row. Assumes the deck shows that playlist sorted by #. */
static int cr_nav_path(int stick, uint32_t id, int prefer_pl, cr_navreq *r)
{
    cr_stick *s = cr_stick_get(stick, 0);
    if (!s || s->state != 1) return -1;
    cr_members(s, stick);
    if (!s->mem) return -1;
    int best = -1, bestrow = 0, bestdepth = 99;
    for (int i = 0; i < s->np; i++) {
        if (!s->mem[i]) continue;
        for (int row = 0; row < s->nmem[i] && row < 250; row++) {
            if (s->mem[i][row] != id) continue;
            int path[8], depth; char where[160];
            if (!cr_tree_path(s, i, path, &depth, where, sizeof where) || depth > 7) break;
            int pref = s->p[i].id == prefer_pl;
            if (best < 0 || pref || (depth < bestdepth || (depth == bestdepth && row < bestrow))) {
                best = i; bestrow = row; bestdepth = depth;
                memcpy(r->path, path, sizeof path);
                r->path[depth] = row; r->depth = depth + 1;
                snprintf(r->where, sizeof r->where, "%s / #%d", where, row + 1);
            }
            if (pref) return 1;
            break;
        }
    }
    return best >= 0 ? 1 : 0;
}
static void cr_api_deck(cr_sb *b, const char *query)
{
    int stick = (int)cr_qint(query, "stick", -1), deck = (int)cr_qint(query, "deck", 0);
    long id = cr_qint(query, "id", -1);
    cr_navreq r; memset(&r, 0, sizeof r);
    r.stick = stick; r.deck = deck - 1; r.id = (uint32_t)id;
    const char *err = NULL;
    cr_stick *s = cr_stick_get(stick, 0);
    const cr_track *t = id > 0 ? cr_find(s, (uint32_t)id) : NULL;
    if (deck < 0 || deck > 4) err = "bad deck";
    else if (!t) err = "track not found on that USB";
    else if (deck > 0 && !__atomic_load_n(&cr_load_allowed, __ATOMIC_RELAXED)) err = "loading from the iPad is off in Mod Menu";
    else {
        int k = cr_nav_path(stick, (uint32_t)id, (int)cr_qint(query, "pl", -1), &r);
        if (k < 0) err = "can't read that USB";
        else if (!k) err = "not in a playlist the deck can reach (first 250 rows); search for it on the deck";
    }
    if (err) { cr_sb_str(b, "{\"ok\":0,\"error\":"); cr_json_str(b, err); cr_sb_str(b, "}"); return; }
    snprintf(r.title, sizeof r.title, "%s", t->title);
    pthread_mutex_lock(&cr_mu);
    cr_nreq = r;
    snprintf(cr_nav_msg, sizeof cr_nav_msg, "Sent to the deck: %.120s", r.where);
    pthread_mutex_unlock(&cr_mu);
    __atomic_add_fetch(&cr_nreq_seq, 1u, __ATOMIC_RELEASE);
    cr_sb_str(b, "{\"ok\":1,\"where\":"); cr_json_str(b, r.where); cr_sb_str(b, "}");
}

/* ---- API ---- */
static void cr_api_state(cr_sb *b)
{
    cr_sb_str(b, "{\"decks\":[");
    for (int d = 0; d < 4; d++) {
        az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di; di.len_s = -1; di.playing = -1;
        int ok = AZ_HAS(C, deck_info) && C->deck_info(d, &di) == 0 && di.loaded == 1;
        if (d) cr_sb_str(b, ",");
        if (!ok) { cr_sb_fmt(b, "{\"d\":%d,\"loaded\":0}", d + 1); continue; }
        di.title[sizeof di.title - 1] = di.artist[sizeof di.artist - 1] = di.key[sizeof di.key - 1] = 0;
        cr_sb_fmt(b, "{\"d\":%d,\"loaded\":1,\"playing\":%d,\"bpm\":%.2f,\"pos\":%.1f,\"len\":%.1f,"
                  "\"stems\":%d,\"stems_on\":%d,\"id\":%u,\"title\":", d + 1, di.playing == 1,
                  di.bpm > 0 ? di.bpm : 0, di.pos_s >= 0 ? di.pos_s : -1, di.len_s > 0 ? di.len_s : -1,
                  di.has_stems == 1, di.stems_on == 1, di.content_id);
        cr_json_str(b, di.title); cr_sb_str(b, ",\"artist\":"); cr_json_str(b, di.artist);
        cr_sb_str(b, ",\"key\":"); cr_json_str(b, di.key); cr_sb_str(b, "}");
    }
    cr_sb_str(b, "],\"queue\":[");
    for (int i = 0; i < cr_nqueue; i++) {
        const cr_pick *q = &cr_queue[i];
        cr_sb_fmt(b, "%s{\"stick\":%d,\"id\":%u,\"bpm\":%.2f,\"len\":%.0f,\"title\":", i ? "," : "",
                  q->stick, q->id, q->bpm, q->len);
        cr_json_str(b, q->title); cr_sb_str(b, ",\"artist\":"); cr_json_str(b, q->artist);
        cr_sb_str(b, ",\"key\":"); cr_json_str(b, q->key); cr_sb_str(b, "}");
    }
    char msg[160];
    pthread_mutex_lock(&cr_mu); snprintf(msg, sizeof msg, "%s", cr_nav_msg); pthread_mutex_unlock(&cr_mu);
    cr_sb_fmt(b, "],\"nav\":{\"state\":%d,\"load\":%d,\"view\":%d,\"msg\":", __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE),
              __atomic_load_n(&cr_load_allowed, __ATOMIC_RELAXED), AZ_HAS(C, view) ? C->view() : -1);
    cr_json_str(b, msg);
    cr_sb_str(b, "}}");
}
static void cr_api_tracks(cr_sb *b, int stick, int force)
{
    cr_stick *s = cr_stick_get(stick, force);
    cr_sb_fmt(b, "{\"stick\":%d,\"ok\":%d,\"tracks\":[", stick, s && s->state == 1);
    for (int i = 0; s && s->state == 1 && i < s->nt; i++) {
        const cr_track *t = &s->t[i];
        cr_sb_fmt(b, "%s[%u,", i ? "," : "", t->id);
        cr_json_str(b, t->title); cr_sb_str(b, ","); cr_json_str(b, t->artist); cr_sb_str(b, ",");
        cr_json_str(b, t->key); cr_sb_fmt(b, ",%.2f,%.0f]", t->bpm, t->len);
    }
    cr_sb_str(b, "]}");
}
static void cr_api_playlists(cr_sb *b, int stick)
{
    cr_stick *s = cr_stick_get(stick, 0);
    cr_sb_fmt(b, "{\"stick\":%d,\"ok\":%d,\"playlists\":[", stick, s && s->state == 1);
    for (int i = 0; s && s->state == 1 && i < s->np; i++) {
        const cr_pl *p = &s->p[i];
        cr_sb_fmt(b, "%s[%d,%d,%d,", i ? "," : "", p->id, p->parent, p->folder);
        cr_json_str(b, p->name); cr_sb_str(b, "]");
    }
    cr_sb_str(b, "]}");
}
static int cr_pl_ids(const az_track *t, void *user)
{
    cr_sb *b = user;
    cr_sb_fmt(b, "%s%u", b->s[b->n - 1] == '[' ? "" : ",", t->id);
    return 0;
}
static void cr_api_playlist(cr_sb *b, int stick, int id)
{
    cr_sb_fmt(b, "{\"stick\":%d,\"id\":%d,\"ids\":[", stick, id);
    if (id > 0 && stick >= 0 && stick <= 1) C->lib_tracks(MOD_ID, stick, id, cr_pl_ids, b);
    cr_sb_str(b, "]}");
}
static int cr_api_queue(cr_sb *b, const char *path, const char *query)
{
    if (!strcmp(path, "/api/queue/add")) {
        int stick = (int)cr_qint(query, "stick", -1);
        long id = cr_qint(query, "id", -1);
        cr_stick *s = cr_stick_get(stick, 0);
        const cr_track *t = id > 0 ? cr_find(s, (uint32_t)id) : NULL;
        if (!t) return 404;
        if (cr_nqueue == CR_QUEUE_MAX) {
            memmove(cr_queue, cr_queue + 1, sizeof cr_queue[0] * (CR_QUEUE_MAX - 1));
            cr_nqueue--;
        }
        cr_pick *q = &cr_queue[cr_nqueue++];
        memset(q, 0, sizeof *q);
        q->stick = stick; q->id = t->id; q->bpm = t->bpm; q->len = t->len;
        snprintf(q->title, sizeof q->title, "%s", t->title);
        snprintf(q->artist, sizeof q->artist, "%s", t->artist);
        snprintf(q->key, sizeof q->key, "%s", t->key);
        pthread_mutex_lock(&cr_mu);
        snprintf(cr_banner_text, sizeof cr_banner_text, "iPad pick: %.60s \xc2\xb7 %s \xc2\xb7 %.1f \xc2\xb7 USB%d",
                 q->title, q->key[0] ? q->key : "--", q->bpm, stick + 1);
        pthread_mutex_unlock(&cr_mu);
        __atomic_add_fetch(&cr_banner_seq, 1u, __ATOMIC_RELEASE);
    } else if (!strcmp(path, "/api/queue/remove")) {
        long i = cr_qint(query, "i", -1);
        if (i < 0 || i >= cr_nqueue) return 404;
        memmove(cr_queue + i, cr_queue + i + 1, sizeof cr_queue[0] * (size_t)(cr_nqueue - i - 1));
        cr_nqueue--;
    } else if (!strcmp(path, "/api/queue/clear")) {
        cr_nqueue = 0;
    } else return 404;
    cr_api_state(b);
    return 200;
}

/* Route one request into `body`; returns the status code and sets the content type. */
static int cr_route(const cr_req *r, cr_sb *body, const char **type)
{
    *type = "application/json; charset=utf-8";
    if (!strcmp(r->path, "/") || !strcmp(r->path, "/index.html")) {
        *type = "text/html; charset=utf-8";
        cr_sb_str(body, CR_UI_HTML);
        return 200;
    }
    if (strncmp(r->path, "/api/", 5)) return 404;
    if (!cr_pin_ok(r->query, cr_pin)) { cr_sb_str(body, "{\"error\":\"pin\"}"); return 403; }
    int stick = (int)cr_qint(r->query, "stick", 0);
    if (!strcmp(r->path, "/api/state")) { cr_api_state(body); return 200; }
    if (!strcmp(r->path, "/api/tracks")) { cr_api_tracks(body, stick, cr_qint(r->query, "refresh", 0) == 1); return 200; }
    if (!strcmp(r->path, "/api/playlists")) { cr_api_playlists(body, stick); return 200; }
    if (!strcmp(r->path, "/api/playlist")) { cr_api_playlist(body, stick, (int)cr_qint(r->query, "id", -1)); return 200; }
    if (!strncmp(r->path, "/api/queue/", 11)) return cr_api_queue(body, r->path, r->query);
    if (!strcmp(r->path, "/api/deck")) { cr_api_deck(body, r->query); return 200; }
    return 404;
}

/* ---- network ---- */
/* The deck's interfaces: mlan0 = Wi-Fi (Marvell), eth0 = the LINK port (internal switch).
 * Only IPv4; a direct cable with no DHCP gets 169.254.x.x from avahi-autoipd. */
static int cr_mdns_pid = -1;
static char cr_assets[320];         /* assets_dir, read once in init (it touches the disk) */
static char cr_mdns_for[160];
static void cr_mdns_sync(const char *wifi, const char *wired)
{
    char want[160];
    snprintf(want, sizeof want, "%s %s", wifi, wired);
    if (!strcmp(want, cr_mdns_for) && cr_mdns_pid > 0 && C->launch_alive(MOD_ID, cr_mdns_pid)) return;
    if (cr_mdns_pid > 0) { C->launch_stop(MOD_ID, cr_mdns_pid); cr_mdns_pid = -1; }
    snprintf(cr_mdns_for, sizeof cr_mdns_for, "%s", want);
    if (AZ_HAS(C, launch) && cr_assets[0] && (wifi[0] || wired[0])) {
        char path[400];
        snprintf(path, sizeof path, "%s/mdns.sh", cr_assets);
        const char *av[4]; int n = 0;
        if (wifi[0]) av[n++] = wifi;
        if (wired[0]) av[n++] = wired;
        av[n] = NULL;
        az_launch_opts o = {.size = sizeof o, .nice = 10, .cpu_mask = 0, .mem_mb = 32, .log_path = NULL};
        cr_mdns_pid = C->launch(MOD_ID, path, av, &o);
    }
    __atomic_store_n(&cr_mdns_on, cr_mdns_pid > 0, __ATOMIC_RELAXED);
}
static void cr_update_addr(void)
{
    struct ifaddrs *ifa = NULL, *i;
    char wifi[64] = "", wired[64] = "", other[64] = "";
    if (getifaddrs(&ifa) == 0) {
        for (i = ifa; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) ||
                !(i->ifa_flags & IFF_UP)) continue;
            char a[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, a, sizeof a);
            if (!strncmp(i->ifa_name, "mlan", 4) || !strncmp(i->ifa_name, "wlan", 4)) { if (!wifi[0]) snprintf(wifi, sizeof wifi, "%s", a); }
            else if (!strncmp(i->ifa_name, "eth", 3)) { if (!wired[0]) snprintf(wired, sizeof wired, "%s", a); }
            else if (!other[0]) snprintf(other, sizeof other, "%s", a);
        }
        freeifaddrs(ifa);
    }
    pthread_mutex_lock(&cr_mu);
    snprintf(cr_addr_wifi, sizeof cr_addr_wifi, "%s", wifi);
    snprintf(cr_addr_wired, sizeof cr_addr_wired, "%s", wired);
    snprintf(cr_addr, sizeof cr_addr, "%s", wifi[0] ? wifi : wired[0] ? wired : other);
    pthread_mutex_unlock(&cr_mu);
    if (__atomic_load_n(&cr_enabled, __ATOMIC_RELAXED)) cr_mdns_sync(wifi, wired);
}
static void cr_send_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) { if (w < 0 && errno == EINTR) continue; return; }
        p += w; n -= (size_t)w;
    }
}
static void cr_serve_one(int fd)
{
    struct timeval tv = {3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    char buf[4096]; size_t n = 0;
    while (n < sizeof buf - 1) {
        ssize_t r = recv(fd, buf + n, sizeof buf - 1 - n, 0);
        if (r <= 0) break;
        n += (size_t)r; buf[n] = 0;
        if (strstr(buf, "\r\n\r\n")) break;
    }
    buf[n] = 0;
    cr_req req;
    cr_sb body = {0}, head = {0};
    const char *type = "text/plain";
    int code = cr_parse_request(buf, &req) ? cr_route(&req, &body, &type) : 400;
    if (body.oom) { cr_sb_free(&body); code = 503; }
    if (code != 200 && !body.n) cr_sb_str(&body, "{}");
    cr_http_head(&head, code, type, body.n);
    if (!head.oom) cr_send_all(fd, head.s, head.n);
    if (body.s) cr_send_all(fd, body.s, body.n);
    cr_sb_free(&head); cr_sb_free(&body);
    __atomic_add_fetch(&cr_requests, 1, __ATOMIC_RELAXED);
}
static int cr_listen(void)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(CR_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(fd, (struct sockaddr *)&a, sizeof a) || listen(fd, 8)) { close(fd); return -1; }
    return fd;
}
/* Idle unless a request arrives; closes the port while the Mod Menu toggle is off. */
static void *cr_http_thread(void *arg)
{
    (void)arg;
    int lfd = -1;
    long next_addr = 0;
    for (;;) {
        int on = __atomic_load_n(&cr_enabled, __ATOMIC_RELAXED);
        if (on && lfd < 0) {
            lfd = cr_listen();
            __atomic_store_n(&cr_port_state, lfd < 0 ? -1 : 1, __ATOMIC_RELAXED);
            if (lfd < 0) { sleep(5); cr_secs += 5; continue; }
        }
        if (!on && lfd >= 0) { close(lfd); lfd = -1; __atomic_store_n(&cr_port_state, 0, __ATOMIC_RELAXED); }
        if (!on && cr_mdns_pid > 0) {                    /* off: stop publishing the name too */
            C->launch_stop(MOD_ID, cr_mdns_pid); cr_mdns_pid = -1; cr_mdns_for[0] = 0;
            __atomic_store_n(&cr_mdns_on, 0, __ATOMIC_RELAXED);
        }
        if (cr_now_s() >= next_addr) { cr_update_addr(); next_addr = cr_now_s() + 15; }
        if (lfd < 0) { sleep(1); cr_secs++; continue; }
        struct pollfd p = {lfd, POLLIN, 0};
        if (poll(&p, 1, 1000) <= 0) { cr_secs++; continue; }
#ifdef __linux__
        int fd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
#else
        int fd = accept(lfd, NULL, NULL);
        if (fd >= 0) fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
        if (fd < 0) continue;
        cr_serve_one(fd);
        close(fd);
    }
    return NULL;
}

/* ---- deck navigator (mixer bus thread) ---- */
static int cr_deck_busy(int deck)
{
    az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di; di.playing = -1;
    if (!AZ_HAS(C, deck_info) || C->deck_info(deck, &di) != 0) return 1;   /* unknown: refuse */
    return di.loaded == 1 && di.playing != 0;                               /* playing or unknown */
}
static long cr_bus_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}
static int cr_frame(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    if (bus != AZ_BUS_MIXER_IN || !f || len < 98) return AZ_FRAME_OK;
    if (f[32] & 0x80) __atomic_store_n(&cr_source_touched, 1, __ATOMIC_RELAXED);
    int st = __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);
    if (st != NAV_ARMED && st != NAV_RUNNING && !nav_off) { nav_real_last = f[34]; return AZ_FRAME_OK; }
    nav_frame(f, len, cr_bus_ms(), C->view(), cr_deck_busy);
    return AZ_FRAME_OK;
}

/* ---- UI thread ---- */
static unsigned cr_banner_seen;
static int cr_port_logged = -2;
static int cr_filter_ok;
/* UI-thread navigator bookkeeping. */
static unsigned cr_nreq_seen;
static cr_navreq cr_active;
static int cr_nav_seen_state = -1, cr_last_stick = -1, cr_verify_left;
static long cr_ui_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}
static long cr_armed_at, cr_settle_until;
static void cr_nav_status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void cr_nav_status(const char *fmt, ...)
{
    char m[160]; va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
    if (pthread_mutex_trylock(&cr_mu) == 0) { snprintf(cr_nav_msg, sizeof cr_nav_msg, "%s", m); pthread_mutex_unlock(&cr_mu); }
}
static void cr_nav_tick(void)
{
    if (!cr_filter_ok) return;
    long now = cr_ui_ms();
    unsigned seq = __atomic_load_n(&cr_nreq_seq, __ATOMIC_ACQUIRE);
    /* After a load the deck closes its browser on its own a moment later; starting the
     * next pick inside that window races it. Hold the request until the deck settles. */
    if (seq != cr_nreq_seen && now >= cr_settle_until && pthread_mutex_trylock(&cr_mu) == 0) {
        cr_navreq r = cr_nreq;
        pthread_mutex_unlock(&cr_mu);
        cr_nreq_seen = seq;
        nav_script sc;
        int sel = r.stick != cr_last_stick || __atomic_load_n(&cr_source_touched, __ATOMIC_RELAXED);
        if (r.deck >= 0 && cr_deck_busy(r.deck)) {
            cr_nav_status("Deck %d is playing: not loaded", r.deck + 1);
        } else if (!nav_build(&sc, sel, r.stick, r.path, r.depth, r.deck) || !nav_arm(&sc)) {
            cr_nav_status("The deck is busy with another pick; try again");
        } else {
            cr_active = r; cr_armed_at = now; cr_verify_left = 0;
            if (sel) { __atomic_store_n(&cr_source_touched, 0, __ATOMIC_RELAXED); cr_last_stick = -1; }
            cr_nav_status("Finding %s on USB %d: %s", r.title, r.stick + 1, r.where);
        }
    }
    int st = __atomic_load_n(&nav_state, __ATOMIC_ACQUIRE);
    if (st == NAV_ARMED && now - cr_armed_at > 4000) {
        int expect = NAV_ARMED;
        if (__atomic_compare_exchange_n(&nav_state, &expect, NAV_IDLE, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            cr_nav_status("Hands on the controls: try again in a moment");
    }
    if (st != cr_nav_seen_state) {
        cr_nav_seen_state = st;
        if (st == NAV_DONE) {
            cr_last_stick = cr_active.stick;
            if (cr_active.deck >= 0) { cr_verify_left = 40; cr_nav_status("Loading on Deck %d...", cr_active.deck + 1); }
            else if (C->view() != AZ_VIEW_BROWSE) {
                /* The screen changed under the script (the deck's own auto-close, a menu). */
                cr_nav_status("Didn't land on the deck (the screen changed): tap Show again");
            } else {
                cr_nav_status("On the deck: %s. Press LOAD.", cr_active.where);
                if (AZ_HAS(C, banner)) C->banner(MOD_ID, "iPad: track highlighted, press LOAD", 2500);
            }
        } else if (st == NAV_CANCELLED) {
            cr_nav_status("Stopped: you used the controls");
        } else if (st == NAV_REFUSED) {
            cr_nav_status("Deck %d started playing: not loaded", __atomic_load_n(&nav_refused_deck, __ATOMIC_RELAXED));
        }
    }
    /* After a load, confirm the deck reports the track we meant (about 8 s of ticks). */
    if (cr_verify_left > 0) {
        az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
        if (AZ_HAS(C, deck_info) && C->deck_info(cr_active.deck, &di) == 0 && di.loaded == 1 &&
            di.content_id == cr_active.id) {
            cr_verify_left = 0; cr_settle_until = now + 3500;
            cr_nav_status("Loaded on Deck %d: %s", cr_active.deck + 1, cr_active.title);
            if (AZ_HAS(C, banner)) C->banner(MOD_ID, "iPad: loaded", 1500);
        } else if (--cr_verify_left == 0) {
            cr_settle_until = now + 3500;
            cr_nav_status("Check Deck %d: it didn't report %s (playlist sorted differently?)",
                          cr_active.deck + 1, cr_active.title);
            if (AZ_HAS(C, banner)) C->banner(MOD_ID, "iPad: check the deck, the track may differ", 3000);
        }
    }
}
static int cr_load_row = -1;
static void cr_tick(void)
{
    if (cr_load_row >= 0) __atomic_store_n(&cr_load_allowed, C->row_value(cr_load_row) == 1, __ATOMIC_RELAXED);
    cr_nav_tick();
    int ps = __atomic_load_n(&cr_port_state, __ATOMIC_RELAXED);
    if (ps != cr_port_logged && AZ_HAS(C, log)) {
        cr_port_logged = ps;
        C->log(MOD_ID, ps == 1 ? "serving on port %d" : ps < 0 ? "port %d busy; retrying" : "port %d closed", CR_PORT);
    }
    if (cr_row >= 0) __atomic_store_n(&cr_enabled, C->row_value(cr_row) == 1, __ATOMIC_RELAXED);
    unsigned seq = __atomic_load_n(&cr_banner_seq, __ATOMIC_ACQUIRE);
    if (seq != cr_banner_seen && pthread_mutex_trylock(&cr_mu) == 0) {
        char text[112];
        snprintf(text, sizeof text, "%s", cr_banner_text);
        pthread_mutex_unlock(&cr_mu);
        cr_banner_seen = seq;
        if (AZ_HAS(C, banner) && text[0]) C->banner(MOD_ID, text, 2500);
    }
}
/* One line, shrunk to fit the area's width and dropped if it can't fit at all. Returns the
 * next line's y. */
static int cr_line(az_draw *d, az_rect a, int y, const char *t, int px, uint32_t rgb)
{
    int x = a.x + (a.w >= 600 ? 30 : 8), room = a.x + a.w - x - 4;
    while (px > 10 && AZ_HAS(C, text_width) && C->text_width(t, px) > room) px -= 2;
    if (px * 3 / 2 + y > a.y + a.h) return y;
    if (AZ_HAS(C, text_width) && C->text_width(t, px) > room) return y;
    C->draw_text(d, x, y, t, px, rgb);
    return y + px * 3 / 2 + px / 2;
}
static void cr_draw(az_draw *d, az_rect a, void *user)
{
    (void)user;
    char addr[64] = "", line[160];
    int t0 = -1, t1 = -1;
    if (pthread_mutex_trylock(&cr_mu) == 0) {
        snprintf(addr, sizeof addr, "%s", cr_addr); t0 = cr_tracks_seen[0]; t1 = cr_tracks_seen[1];
        pthread_mutex_unlock(&cr_mu);
    }
    uint32_t text = C->color(AZ_COL_TEXT), dim = C->color(AZ_COL_TEXT_DIM), hi = C->color(AZ_COL_HI);
    C->draw_header(d, "Crate Remote");
    int y = a.y + (AZ_HAS(C, row_height) ? C->row_height(d) : 48) + 16;
    if (!__atomic_load_n(&cr_enabled, __ATOMIC_RELAXED)) {
        cr_line(d, a, y, "Off. Turn on \"iPad browser\" in Mod Menu.", 22, dim);
        return;
    }
    char wifi[64] = "", wired[64] = "";
    if (pthread_mutex_trylock(&cr_mu) == 0) {
        snprintf(wifi, sizeof wifi, "%s", cr_addr_wifi); snprintf(wired, sizeof wired, "%s", cr_addr_wired);
        pthread_mutex_unlock(&cr_mu);
    }
    y = cr_line(d, a, y, "On your iPad or phone (same Wi-Fi, or a cable to the LINK port), open:", 20, dim);
    if (__atomic_load_n(&cr_mdns_on, __ATOMIC_RELAXED) && (wifi[0] || wired[0]))
        y = cr_line(d, a, y, "http://crate-remote.local:8765", 36, hi);
    if (wifi[0]) { snprintf(line, sizeof line, "Wi-Fi   http://%s:%d", wifi, CR_PORT); y = cr_line(d, a, y, line, 24, text); }
    if (wired[0]) { snprintf(line, sizeof line, "Cable   http://%s:%d", wired, CR_PORT); y = cr_line(d, a, y, line, 24, text); }
    if (!wifi[0] && !wired[0]) {
        snprintf(line, sizeof line, "%s", addr[0] ? addr : "No network: join Wi-Fi, or plug a cable into LINK");
        y = cr_line(d, a, y, line, 24, addr[0] ? text : dim);
    }
    snprintf(line, sizeof line, "PIN  %04d", cr_pin);
    y = cr_line(d, a, y, line, 48, text);
    char a0[24], a1[24];
    if (t0 > 0) snprintf(a0, sizeof a0, "%d tracks", t0); else snprintf(a0, sizeof a0, "%s", t0 == 0 ? "none" : "not read yet");
    if (t1 > 0) snprintf(a1, sizeof a1, "%d tracks", t1); else snprintf(a1, sizeof a1, "%s", t1 == 0 ? "none" : "not read yet");
    snprintf(line, sizeof line, "USB 1: %s    USB 2: %s    Requests: %ld", a0, a1,
             __atomic_load_n(&cr_requests, __ATOMIC_RELAXED));
    y = cr_line(d, a, y, line, 18, dim);
    if (__atomic_load_n(&cr_port_state, __ATOMIC_RELAXED) < 0)
        y = cr_line(d, a, y, "Port 8765 is busy; retrying.", 18, dim);
    cr_line(d, a, y, "Read-only: picks show on the deck; you still load with LOAD.", 18, dim);
}
static int cr_input(const az_event *ev, void *user) { (void)user; return ev && ev->kind == AZ_EV_BACK ? 0 : 1; }

static int cr_init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    /* A Reload calls init again while the HTTP thread (started once) keeps using C: only
     * store it when it really changes. */
    if (__atomic_load_n(&C, __ATOMIC_ACQUIRE) != core) __atomic_store_n(&C, core, __ATOMIC_RELEASE);
    if (!AZ_HAS(C, lib_tracks) || !AZ_HAS(C, lib_playlists) || !AZ_HAS(C, thread_start) ||
        !AZ_HAS(C, register_screen) || !AZ_HAS(C, draw_text) || !AZ_HAS(C, draw_header) || !AZ_HAS(C, color))
        return 2;
    /* A fresh PIN each EP147 start, from the kernel's random source. */
    unsigned r = 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) { if (read(fd, &r, sizeof r) != sizeof r) r = 0; close(fd); }
    if (!r) r = (unsigned)time(NULL) * 2654435761u;
    cr_pin = (int)(r % 9000u) + 1000;
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        az_row_v1 row = {.size = sizeof row, .key = "on", .label = "iPad browser", .kind = AZ_ROW_TOGGLE,
            .flags = AZ_ROW_DEFAULT_ON,
            .help = "Browse both USBs on an iPad or phone on the deck's Wi-Fi, see what fits the playing deck, "
                    "and queue picks. Opens network port 8765, PIN on this mod's page. Read-only."};
        cr_row = C->register_row(MOD_ID, &row);
        az_row_v1 load = {.size = sizeof load, .key = "load", .label = "iPad can load decks", .kind = AZ_ROW_TOGGLE,
            .flags = AZ_ROW_DEFAULT_ON, .parent = "on",
            .help = "Show on deck and Load put a pick on the deck by pressing PLAYLIST, turning the knob and "
                    "clicking for you, from a playlist the track is in. Load only presses LOAD for a stopped deck. "
                    "Touching any control stops it."};
        cr_load_row = C->register_row(MOD_ID, &load);
    }
    if (AZ_HAS(C, assets_dir) && !cr_assets[0]) {
        const char *ad = C->assets_dir(MOD_ID);
        if (ad) snprintf(cr_assets, sizeof cr_assets, "%s", ad);
    }
    static const az_screen_v1 scr = {.size = sizeof scr, .on_draw = cr_draw, .on_input = cr_input};
    C->register_screen(MOD_ID, &scr);
    /* One server for the life of EP147: a Mod Menu Reload calls init again in the same
     * process, and the first thread (still running, never unloaded) keeps the port. */
    static int started;
    if (!started && AZ_HAS(C, frame_filter) && AZ_HAS(C, view))
        cr_filter_ok = C->frame_filter(MOD_ID, 1u << AZ_BUS_MIXER_IN, cr_frame, NULL) == 0;
    if (!started) {
        if (C->thread_start(MOD_ID, "crate http", cr_http_thread, NULL) != 0) return 3;
        started = 1;
    }
    return 0;
}
static const char *const TAGS[] = {"any", NULL};
static const az_mod_v1 MOD = {.abi = AZ_MOD_ABI, .size = sizeof MOD, .id = MOD_ID, .fw_tags = TAGS,
                              .init = cr_init, .tick = cr_tick};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &MOD; }
