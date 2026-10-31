/* SPDX-License-Identifier: GPL-3.0-only
 * Host checks for the FLOW Status Panel: FVS1 parsing (FlowVJ's real packet, missing and
 * hostile fields), the tap datagram, tap hit-testing and the vj_tap layout keys.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../layout_file.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void packets(void)
{
    fv_state s;
    const char *p = "FVS1\nseq=41\nlook=Haunt 07\nlights=CHILL 2\nlock=1\nsrc=az\nbpm=127.96\n"
                    "safe=1\nhold=0\nauto=1\nblack=0\ntap=9103\n";
    CHECK(fv_parse(p, strlen(p), &s) == 1);
    CHECK(s.seq == 41 && !strcmp(s.look, "Haunt 07") && !strcmp(s.lights, "CHILL 2") && !strcmp(s.src, "az"));
    CHECK(s.lock == 1 && s.safe == 1 && s.hold == 0 && s.autop == 1 && s.black == 0);
    CHECK(s.bpm10 == 1280 && s.tap_port == 9103);
    CHECK(fv_act_on(&s, FV_ACT_SAFE) && fv_act_on(&s, FV_ACT_AUTO) && !fv_act_on(&s, FV_ACT_HOLD) && !fv_act_on(&s, FV_ACT_NEXT));

    /* CRLF, only a few keys, unknown keys: defaults, tap port falls back to 9103 */
    const char *q = "FVS1\r\nlook=A\r\nfuture=thing\r\nlock=yes\r\n";
    CHECK(fv_parse(q, strlen(q), &s) == 1);
    CHECK(!strcmp(s.look, "A") && s.lock == 0 && s.tap_port == FV_TAP_PORT && s.bpm10 == 0);

    /* not ours: flow-beat's own JSON broadcast, FLP1 pad colours, garbage, empty */
    fv_state keep; memset(&keep, 0x5a, sizeof keep); s = keep;
    const char *j = "{\"flowbeat\":1,\"type\":\"beat\"}";
    CHECK(fv_parse(j, strlen(j), &s) == 0);
    CHECK(fv_parse("FLP1\x01\x02\x03", 7, &s) == 0);
    CHECK(fv_parse("FVS1", 4, &s) == 0);
    CHECK(fv_parse("FVS1x\n", 6, &s) == 0);
    CHECK(fv_parse("", 0, &s) == 0);
    CHECK(!memcmp(&s, &keep, sizeof s));

    /* hostile: long values are cut whole-character, control bytes blanked, bad numbers zero */
    char big[600];
    int n = snprintf(big, sizeof big, "FVS1\nlook=");
    for (int i = 0; i < 30; i++) { big[n++] = (char)0xc3; big[n++] = (char)0xa9; }   /* 30 x "é" = 60 bytes */
    n += snprintf(big + n, sizeof big - n, "\nlights=a\tb\x01" "c\nbpm=-5\ntap=70000\nseq=x\n");
    CHECK(fv_parse(big, (size_t)n, &s) == 1);
    CHECK(strlen(s.look) == 46 && ((unsigned char)s.look[45] == 0xa9));   /* 23 whole characters */
    CHECK(!strcmp(s.lights, "a b c"));
    CHECK(s.bpm10 == 0 && s.tap_port == FV_TAP_PORT && s.seq == 0);
    /* a value with no newline at the end of the datagram */
    CHECK(fv_parse("FVS1\nlook=Last", 14, &s) == 1 && !strcmp(s.look, "Last"));
}

static void taps(void)
{
    char b[64];
    CHECK(fv_cmd(b, sizeof b, 7, FV_ACT_NEXT) > 0 && !strcmp(b, "FVC1 7 next\n"));
    CHECK(fv_cmd(b, sizeof b, 8, FV_ACT_SAFE) > 0 && !strcmp(b, "FVC1 8 safe\n"));
    CHECK(fv_cmd(b, sizeof b, 1, FV_ACT_NONE) == 0 && fv_cmd(b, sizeof b, 1, FV_ACTS) == 0);
    CHECK(fv_cmd(b, 6, 1, FV_ACT_NEXT) == 0);   /* too small: nothing half-written is sent */
    for (int a = 1; a < FV_ACTS; a++) CHECK(fv_act_of(FV_ACT_NAME[a], strlen(FV_ACT_NAME[a])) == a);
    CHECK(fv_act_of("nex", 3) == FV_ACT_NONE && fv_act_of("", 0) == FV_ACT_NONE);

    fv_tap t[2] = { { 1090, 530, 56, 24, FV_ACT_NEXT }, { 1150, 530, 56, 24, FV_ACT_SAFE } };
    CHECK(fv_tap_hit(t, 2, 1090, 530) == FV_ACT_NEXT);
    CHECK(fv_tap_hit(t, 2, 1145, 553) == FV_ACT_NEXT);
    CHECK(fv_tap_hit(t, 2, 1146, 540) == FV_ACT_NONE);   /* the gap between buttons */
    CHECK(fv_tap_hit(t, 2, 1150, 540) == FV_ACT_SAFE);
    CHECK(fv_tap_hit(t, 2, 1150, 554) == FV_ACT_NONE);
    CHECK(fv_tap_hit(t, 0, 1090, 530) == FV_ACT_NONE);
}

static void layout(void)
{
    static cs_layout L;
    const char *g =
        "panel 2 x=1090 y=490 w=180 h=96 views=4,24 bg=auto border=accent bw=2 edge=top\n"
        "widget 2 flowvj x=8 y=6 w=166 h=26 size=15 text=VJ \n"
        "widget 2 vj_lights x=8 y=34 w=166 h=20 size=13 color=dim\n"
        "widget 2 vj_tap act=next x=8 y=62 w=52 h=28 size=13\n"
        "widget 2 vj_tap act=safe x=64 y=62 w=52 h=28 size=13 text=SAFE\n"
        "widget 2 vj_tap act=launch x=120 y=62 w=52 h=28 size=13\n";
    CHECK(cs_parse(g, strlen(g), &L) == 1 && L.errors == 0);
    const cs_panel *p = &L.p[0];
    CHECK(p->n == 5);
    CHECK(p->wd[0].type == CS_FLOWVJ && !strcmp(p->wd[0].text, "VJ ") && p->wd[0].act == FV_ACT_NONE);
    CHECK(p->wd[1].type == CS_VJ_LIGHTS && p->wd[1].color == CS_COL_DIM);
    CHECK(p->wd[2].type == CS_VJ_TAP && p->wd[2].act == FV_ACT_NEXT && p->wd[2].w == 52);
    CHECK(p->wd[3].act == FV_ACT_SAFE && !strcmp(p->wd[3].text, "SAFE"));
    CHECK(p->wd[4].type == CS_VJ_TAP && p->wd[4].act == FV_ACT_NONE);   /* unknown act: a dead button, not an error */
}

int main(void)
{
    packets(); taps(); layout();
    if (fails) { printf("%d FAILED\n", fails); return 1; }
    printf("flowvj: all checks passed\n");
    return 0;
}
