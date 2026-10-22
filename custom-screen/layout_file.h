/* SPDX-License-Identifier: GPL-3.0-only
 * The layout file: up to four panels (one surface each) holding widgets. Pure parsing, no
 * SDK calls, so it is host-tested. The Deck Screen Designer writes this format.
 *
 *   version 1
 *   name <text>
 *   panel <id> x= y= w= h= views=4,24,2 bg=auto|<hex> border=none|accent|<hex> bw= edge=top|all
 *   widget <panel id> <type> deck=1..4|L|R x= y= w= h= size= color=auto|dim|accent|cell|<hex>
 *          align=l|c|r act=<tap action, vj_tap only> zoom=2..128 grid=0|1
 *          text=<rest of the line, spaces kept>
 *   (zoom and grid are read by wave only: beats across the widget, beat grid on or off)
 *
 * Panel x/y are screen pixels (1280 x 800); widget x/y are inside the panel. Unknown keys are
 * ignored, an unknown type or a widget for a missing panel counts as an error and is skipped.
 */
#ifndef CS_LAYOUT_FILE_H
#define CS_LAYOUT_FILE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "flowvj.h"

#define CS_PANELS 4        /* the SDK's four surfaces per mod */
#define CS_WIDGETS 24
#define CS_TEXT 48
#define CS_FILE_MAX 32768

enum { CS_DECK_NUM = 1, CS_TITLE, CS_ARTIST, CS_KEY, CS_BPM, CS_BPM_GAP, CS_PHRASE, CS_PHRASE_BAR,
       CS_BEAT, CS_TIME_LEFT, CS_STEMS, CS_STATE, CS_SIDE, CS_CLOCK, CS_LABEL, CS_BOX,
       CS_FLOWVJ, CS_VJ_LIGHTS, CS_VJ_TAP,   /* FlowVJ's state over UDP 9102 (flowvj.h) */
       CS_WAVE, CS_OVERVIEW,                 /* 4-Deck Waveforms (waves.h) */
       CS_TYPES };
static const char *const CS_TYPE_NAME[CS_TYPES] = { "", "deck_num", "title", "artist", "key", "bpm",
    "bpm_gap", "phrase", "phrase_bar", "beat", "time_left", "stems", "state", "side", "clock",
    "label", "box", "flowvj", "vj_lights", "vj_tap", "wave", "overview" };

/* Colours: 0xRRGGBB, or one of these named ones the painter resolves against the theme. */
#define CS_COL_AUTO   0x01000000u
#define CS_COL_DIM    0x02000000u
#define CS_COL_ACCENT 0x03000000u
#define CS_COL_CELL   0x04000000u
#define CS_COL_NONE   0x05000000u

#define CS_DECK_NONE -1
#define CS_DECK_L 10       /* the deck the left side has selected (1 or 3) */
#define CS_DECK_R 11       /* the right side's (2 or 4) */

#define CS_VIEW_4  1u      /* 4 DECK */
#define CS_VIEW_24 2u      /* 2/4 DECK */
#define CS_VIEW_2  4u      /* 2 DECK */

enum { CS_ALIGN_L = 0, CS_ALIGN_C, CS_ALIGN_R };

typedef struct {
    int type, deck, x, y, w, h, size, align, act;   /* act: a vj_tap's FV_ACT_*, 0 = none */
    int zoom, grid;    /* wave: beats across the widget (2..128), beat grid on (1) or off (0) */
    uint32_t color;
    char text[CS_TEXT];
} cs_widget;

typedef struct {
    int id, x, y, w, h, bw, edge_top, n;
    unsigned views;
    uint32_t bg, border;
    cs_widget wd[CS_WIDGETS];
} cs_panel;

typedef struct {
    int n, errors, first_error_line;
    char name[CS_TEXT];
    cs_panel p[CS_PANELS];
} cs_layout;

static int cs_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int cs_type_of(const char *s, size_t n)
{
    for (int t = 1; t < CS_TYPES; t++)
        if (strlen(CS_TYPE_NAME[t]) == n && !strncmp(CS_TYPE_NAME[t], s, n)) return t;
    return 0;
}

static int cs_eq(const char *s, size_t n, const char *word) { return strlen(word) == n && !strncmp(s, word, n); }

/* A decimal integer; *ok = 0 when s is not one. */
static int cs_int(const char *s, size_t n, int *ok)
{
    char b[16];
    *ok = 0;
    if (!n || n >= sizeof b) return 0;
    memcpy(b, s, n); b[n] = 0;
    char *end;
    long v = strtol(b, &end, 10);
    if (*end || v < -100000 || v > 100000) return 0;
    *ok = 1;
    return (int)v;
}

static uint32_t cs_color(const char *s, size_t n, uint32_t def)
{
    if (cs_eq(s, n, "auto")) return CS_COL_AUTO;
    if (cs_eq(s, n, "dim")) return CS_COL_DIM;
    if (cs_eq(s, n, "accent")) return CS_COL_ACCENT;
    if (cs_eq(s, n, "cell")) return CS_COL_CELL;
    if (cs_eq(s, n, "none")) return CS_COL_NONE;
    if (n && s[0] == '#') { s++; n--; }
    if (n != 6) return def;
    uint32_t v = 0;
    for (size_t i = 0; i < 6; i++) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return def;
        v = v << 4 | (uint32_t)d;
    }
    return v;
}

static unsigned cs_views(const char *s, size_t n)
{
    unsigned v = 0;
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && s[j] != ',') j++;
        if (cs_eq(s + i, j - i, "4")) v |= CS_VIEW_4;
        else if (cs_eq(s + i, j - i, "24")) v |= CS_VIEW_24;
        else if (cs_eq(s + i, j - i, "2")) v |= CS_VIEW_2;
        else if (cs_eq(s + i, j - i, "all")) v |= CS_VIEW_4 | CS_VIEW_24 | CS_VIEW_2;
        i = j + 1;
    }
    return v;
}

static cs_panel *cs_find(cs_layout *L, int id)
{
    for (int i = 0; i < L->n; i++) if (L->p[i].id == id) return &L->p[i];
    return NULL;
}

static void cs_error(cs_layout *L, int line) { if (!L->errors++) L->first_error_line = line; }

/* One line, already without its newline. */
static void cs_line(cs_layout *L, const char *s, size_t n, int lineno)
{
    while (n && (s[n - 1] == '\r')) n--;
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    if (!n || *s == '#') return;
    /* "text=" takes the rest of the line, so it is cut off before tokenising. */
    const char *text = NULL; size_t text_n = 0;
    for (size_t i = 0; i + 5 <= n; i++)
        if ((i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t') && !strncmp(s + i, "text=", 5)) {
            text = s + i + 5; text_n = n - i - 5; n = i; break;
        }
    const char *tok[24]; size_t len[24]; int nt = 0;
    for (size_t i = 0; i < n && nt < 24;) {
        while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i >= n) break;
        size_t j = i;
        while (j < n && s[j] != ' ' && s[j] != '\t') j++;
        tok[nt] = s + i; len[nt] = j - i; nt++;
        i = j;
    }
    if (!nt) return;
    if (cs_eq(tok[0], len[0], "version")) {
        int ok; if (nt < 2 || cs_int(tok[1], len[1], &ok) != 1 || !ok) cs_error(L, lineno);
        return;
    }
    if (cs_eq(tok[0], len[0], "name")) {
        const char *v = nt > 1 ? tok[1] : ""; size_t vn = nt > 1 ? (size_t)(s + n - tok[1]) : 0;
        while (vn && (v[vn - 1] == ' ' || v[vn - 1] == '\t')) vn--;
        if (vn >= CS_TEXT) vn = CS_TEXT - 1;
        memcpy(L->name, v, vn); L->name[vn] = 0;
        return;
    }
    int ok, is_panel = cs_eq(tok[0], len[0], "panel"), is_widget = cs_eq(tok[0], len[0], "widget");
    if (!is_panel && !is_widget) { cs_error(L, lineno); return; }
    int id = nt > 1 ? cs_int(tok[1], len[1], &ok) : 0;
    if (nt < 2 || !ok) { cs_error(L, lineno); return; }
    if (is_panel) {
        if (L->n >= CS_PANELS || cs_find(L, id)) { cs_error(L, lineno); return; }
        cs_panel *p = &L->p[L->n];
        memset(p, 0, sizeof *p);
        p->id = id; p->w = 100; p->h = 40; p->views = CS_VIEW_4; p->bg = CS_COL_AUTO; p->border = CS_COL_NONE;
        for (int k = 2; k < nt; k++) {
            const char *eq = memchr(tok[k], '=', len[k]);
            if (!eq) continue;
            size_t kn = (size_t)(eq - tok[k]), vn = len[k] - kn - 1;
            const char *v = eq + 1;
            int iv = cs_int(v, vn, &ok);
            if (cs_eq(tok[k], kn, "x") && ok) p->x = iv;
            else if (cs_eq(tok[k], kn, "y") && ok) p->y = iv;
            else if (cs_eq(tok[k], kn, "w") && ok) p->w = iv;
            else if (cs_eq(tok[k], kn, "h") && ok) p->h = iv;
            else if (cs_eq(tok[k], kn, "bw") && ok) p->bw = iv;
            else if (cs_eq(tok[k], kn, "views")) p->views = cs_views(v, vn);
            else if (cs_eq(tok[k], kn, "bg")) p->bg = cs_color(v, vn, CS_COL_AUTO);
            else if (cs_eq(tok[k], kn, "border")) p->border = cs_color(v, vn, CS_COL_NONE);
            else if (cs_eq(tok[k], kn, "edge")) p->edge_top = cs_eq(v, vn, "top");
        }
        p->w = cs_clampi(p->w, 8, 1280); p->h = cs_clampi(p->h, 8, 800);
        p->x = cs_clampi(p->x, 0, 1280 - p->w); p->y = cs_clampi(p->y, 0, 800 - p->h);
        p->bw = cs_clampi(p->bw, 0, 8);
        L->n++;
        return;
    }
    cs_panel *p = cs_find(L, id);
    int type = nt > 2 ? cs_type_of(tok[2], len[2]) : 0;
    if (!p || !type || p->n >= CS_WIDGETS) { cs_error(L, lineno); return; }
    cs_widget *w = &p->wd[p->n];
    memset(w, 0, sizeof *w);
    w->type = type; w->deck = CS_DECK_NONE; w->w = 40; w->h = 20; w->size = 14; w->color = CS_COL_AUTO;
    w->zoom = 16; w->grid = 1;
    for (int k = 3; k < nt; k++) {
        const char *eq = memchr(tok[k], '=', len[k]);
        if (!eq) continue;
        size_t kn = (size_t)(eq - tok[k]), vn = len[k] - kn - 1;
        const char *v = eq + 1;
        int iv = cs_int(v, vn, &ok);
        if (cs_eq(tok[k], kn, "x") && ok) w->x = iv;
        else if (cs_eq(tok[k], kn, "y") && ok) w->y = iv;
        else if (cs_eq(tok[k], kn, "w") && ok) w->w = iv;
        else if (cs_eq(tok[k], kn, "h") && ok) w->h = iv;
        else if (cs_eq(tok[k], kn, "size") && ok) w->size = iv;
        else if (cs_eq(tok[k], kn, "zoom") && ok) w->zoom = iv;
        else if (cs_eq(tok[k], kn, "grid") && ok) w->grid = iv != 0;
        else if (cs_eq(tok[k], kn, "color")) w->color = cs_color(v, vn, CS_COL_AUTO);
        else if (cs_eq(tok[k], kn, "align")) w->align = cs_eq(v, vn, "c") ? CS_ALIGN_C : cs_eq(v, vn, "r") ? CS_ALIGN_R : CS_ALIGN_L;
        else if (cs_eq(tok[k], kn, "act")) w->act = fv_act_of(v, vn);
        else if (cs_eq(tok[k], kn, "deck")) {
            if (cs_eq(v, vn, "L")) w->deck = CS_DECK_L;
            else if (cs_eq(v, vn, "R")) w->deck = CS_DECK_R;
            else if (ok && iv >= 1 && iv <= 4) w->deck = iv - 1;
        }
    }
    if (text) {
        size_t tn = text_n < CS_TEXT ? text_n : CS_TEXT - 1;
        /* don't cut a UTF-8 character in half */
        while (tn && tn < text_n && ((unsigned char)text[tn] & 0xc0u) == 0x80u) tn--;
        memcpy(w->text, text, tn); w->text[tn] = 0;
        for (size_t i = 0; i < tn; i++) if ((unsigned char)w->text[i] < 0x20) w->text[i] = ' ';
    }
    w->x = cs_clampi(w->x, 0, p->w - 1); w->y = cs_clampi(w->y, 0, p->h - 1);
    w->w = cs_clampi(w->w, 1, p->w - w->x); w->h = cs_clampi(w->h, 1, p->h - w->y);
    w->size = cs_clampi(w->size, 8, 96);
    w->zoom = cs_clampi(w->zoom, 2, 128);
    p->n++;
}

/* Parses len bytes of s into L. Returns the number of panels (0 is a valid, empty layout). */
static int cs_parse(const char *s, size_t len, cs_layout *L)
{
    memset(L, 0, sizeof *L);
    int lineno = 1;
    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && s[j] != '\n') j++;
        cs_line(L, s + i, j - i, lineno++);
        i = j + 1;
    }
    return L->n;
}
#endif
