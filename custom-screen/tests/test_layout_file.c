/* SPDX-License-Identifier: GPL-3.0-only
 * Host checks for the layout parser: the designer's own output, the shipped layout, and
 * hostile input (long lines, bad numbers, too many panels and widgets, cut UTF-8).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../layout_file.h"
#include "../default_layout.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static cs_layout L;

static void shipped(void)
{
    CHECK(cs_parse(CS_DEFAULT_LAYOUT, strlen(CS_DEFAULT_LAYOUT), &L) == 4);
    CHECK(L.errors == 0);
    CHECK(!strcmp(L.name, "Big name + key on the left"));
    const cs_panel *p = &L.p[1];   /* the left strips run 3-1-2-4: panel 2 is deck 1's */
    CHECK(p->id == 2 && p->x == 0 && p->y == 202 && p->w == 140 && p->h == 77);
    CHECK(p->views == CS_VIEW_4);
    CHECK(p->bg == 0x180d25 && p->border == CS_COL_NONE);
    CHECK(p->n == 4);
    CHECK(p->wd[0].type == CS_TITLE && p->wd[0].deck == 0 && p->wd[0].w == 130 && p->wd[0].size == 18);
    CHECK(p->wd[1].type == CS_KEY && p->wd[1].size == 28 && !p->wd[1].text[0]);
    CHECK(p->wd[2].type == CS_BPM && p->wd[2].color == CS_COL_DIM && p->wd[2].align == CS_ALIGN_R);
    CHECK(p->wd[3].type == CS_TIME_LEFT && p->wd[3].y == 54);
    CHECK(L.p[0].wd[0].deck == 2 && L.p[3].wd[0].deck == 3);

    const char *g =
        "panel 2 x=650 y=561 w=520 h=46 views=4,24 bg=auto border=none bw=0 edge=top\n"
        "widget 2 key deck=2 x=54 y=25 w=90 h=21 size=14 color=auto align=l text=KEY \n";
    CHECK(cs_parse(g, strlen(g), &L) == 1);
    CHECK(L.p[0].views == (CS_VIEW_4 | CS_VIEW_24) && !strcmp(L.p[0].wd[0].text, "KEY "));   /* trailing space kept */
}

static void every_field(void)
{
    const char *t =
        "version 1\r\n"
        "name  Big   Key \r\n"
        "panel 3 x=-40 y=790 w=2000 h=30 views=2,all bg=#102030 border=accent bw=99 edge=all\n"
        "widget 3 key deck=L x=5 y=2 w=900 h=900 size=200 color=FFaa00 align=r text=K: \n"
        "widget 3 label x=0 y=0 w=10 h=10 size=1 color=dim align=c text=a text=b  # not a comment\n"
        "widget 3 title deck=R color=nonsense\n"
        "widget 3 bpm deck=9\n";
    CHECK(cs_parse(t, strlen(t), &L) == 1);
    CHECK(L.errors == 0);
    CHECK(!strcmp(L.name, "Big   Key"));
    const cs_panel *p = &L.p[0];
    CHECK(p->w == 1280 && p->x == 0 && p->h == 30 && p->y == 770);
    CHECK(p->views == (CS_VIEW_2 | CS_VIEW_4 | CS_VIEW_24));
    CHECK(p->bg == 0x102030 && p->border == CS_COL_ACCENT && p->bw == 8 && !p->edge_top);
    CHECK(p->n == 4);
    CHECK(p->wd[0].deck == CS_DECK_L && p->wd[0].color == 0xFFAA00 && p->wd[0].align == CS_ALIGN_R);
    CHECK(p->wd[0].w == 900 && p->wd[0].h == 28 && p->wd[0].size == 96 && !strcmp(p->wd[0].text, "K: "));
    CHECK(p->wd[1].size == 8 && p->wd[1].color == CS_COL_DIM && !strcmp(p->wd[1].text, "a text=b  # not a comment"));
    CHECK(p->wd[2].deck == CS_DECK_R && p->wd[2].color == CS_COL_AUTO);
    CHECK(p->wd[3].deck == CS_DECK_NONE);
}

static void errors(void)
{
    const char *t =
        "version 2\n"                          /* line 1: unknown version */
        "panel 1 x=1 y=1 w=50 h=20\n"
        "panel 1 x=1 y=1 w=50 h=20\n"          /* duplicate id */
        "widget 9 title deck=1\n"              /* no such panel */
        "widget 1 sparkles deck=1\n"           /* no such type */
        "bogus line\n"
        "panel x x=1\n";                       /* no id */
    CHECK(cs_parse(t, strlen(t), &L) == 1);
    CHECK(L.errors == 6 && L.first_error_line == 1);

    char big[CS_FILE_MAX];
    size_t n = 0;
    for (int i = 0; i < 6; i++) n += (size_t)snprintf(big + n, sizeof big - n, "panel %d x=0 y=0 w=100 h=40\n", i + 1);
    for (int i = 0; i < 40; i++) n += (size_t)snprintf(big + n, sizeof big - n, "widget 1 box x=%d y=0 w=2 h=2\n", i);
    CHECK(cs_parse(big, n, &L) == CS_PANELS);
    CHECK(L.p[0].n == CS_WIDGETS);
    CHECK(L.errors == 2 + 40 - CS_WIDGETS);

    /* a 3-byte UTF-8 character straddling the 47-byte text limit is dropped whole */
    char line[200];
    snprintf(line, sizeof line, "panel 1\nwidget 1 label text=%.46s\xE2\x80\x94tail\n", "0123456789012345678901234567890123456789012345");
    cs_parse(line, strlen(line), &L);
    CHECK(L.p[0].n == 1 && strlen(L.p[0].wd[0].text) == 46);

    /* one enormous line, no newline at the end, NULs inside */
    memset(big, 'x', sizeof big);
    memcpy(big, "widget 1 label text=", 20);
    big[100] = 0;
    cs_parse(big, sizeof big, &L);
    CHECK(L.n == 0);
    cs_parse("", 0, &L);
    CHECK(L.n == 0 && L.errors == 0);
}

static void waveforms(void)
{
    static char buf[CS_FILE_MAX];
    FILE *f = fopen("examples/four-deck-waveforms.txt", "rb");
    CHECK(f != NULL);
    if (!f) return;
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    CHECK(cs_parse(buf, n, &L) == 1 && L.errors == 0);
    const cs_panel *p = &L.p[0];
    CHECK(p->x == 212 && p->y == 80 && p->w == 876 && p->h == 480 && p->n == 16);
    CHECK(p->wd[2].type == CS_WAVE && p->wd[2].deck == 2 && p->wd[2].zoom == 16 && p->wd[2].grid == 1 && p->wd[2].w == 836);
    CHECK(p->wd[3].type == CS_OVERVIEW && p->wd[3].h == 18);
    CHECK(p->wd[14].type == CS_WAVE && p->wd[14].deck == 3 && p->wd[14].y == 388);
    const char *t = "panel 1 x=0 y=0 w=900 h=200\n"
                    "widget 1 wave deck=1 w=800 h=90 zoom=1000 grid=0\n"
                    "widget 1 wave deck=2 zoom=-5\n"
                    "widget 1 overview deck=L\n";
    CHECK(cs_parse(t, strlen(t), &L) == 1 && L.errors == 0 && L.p[0].n == 3);
    CHECK(L.p[0].wd[0].zoom == 128 && L.p[0].wd[0].grid == 0);
    CHECK(L.p[0].wd[1].zoom == 2 && L.p[0].wd[1].grid == 1);
    CHECK(L.p[0].wd[2].type == CS_OVERVIEW && L.p[0].wd[2].deck == CS_DECK_L && L.p[0].wd[2].zoom == 16);
}

int main(void)
{
    shipped();
    every_field();
    errors();
    waveforms();
    if (fails) return 1;
    printf("custom-screen: layout file OK\n");
    return 0;
}
