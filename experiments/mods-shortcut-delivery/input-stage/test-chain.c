/* SPDX-License-Identifier: GPL-3.0-only */
/* Appended to the exact pinned SDK dispatch functions, with timing/log stand-ins. */
static unsigned checks, failures;
static const char *scenario;
#define CHECK(x, name) do { checks++; if (!(x)) { failures++; fprintf(stderr, "FAIL %s/%s\n", scenario, name); } } while (0)
struct action { int calls, value, result, seen, length; uint64_t cost; };
static int callback(int bus, unsigned char *f, int len, void *opaque) {
    (void)bus;
    struct action *a = opaque;
    a->calls++; a->length = len;
    a->seen = len > 34 ? f[34] : -1;
    if (len > 34 && a->value >= 0) f[34] = (unsigned char)a->value;
    mock_tick += a->cost;
    return a->result;
}
static cm_mod mods[4];
static struct action actions[4];
static void reset(void) {
    memset(g_sdk_ff, 0, sizeof g_sdk_ff);
    memset(actions, 0, sizeof actions);
    for (int i=0; i<4; ++i) { mods[i].state=AZCM_LOADED; actions[i].value=-1; }
    g_sdk_ff_n=0; g_sdk_ff_bus=0; g_cm_in_frame=0; mock_tick=0; logged=0;
    g_sdk_budget_ticks=200;
}
static void add(int i, unsigned mask) {
    g_sdk_ff[i]=(struct sdk_filter){ &mods[i], mask, callback, &actions[i], 0, 0 };
    g_sdk_ff_n=i+1; g_sdk_ff_bus |= mask & ~AZ_FILTER_RAW;
}
static void raw_copy(void) {
    scenario="raw-copy"; reset();
    unsigned char f[98]={0}; f[34]=52;
    add(0, (1u<<3)|AZ_FILTER_RAW); add(1, (1u<<3)|AZ_FILTER_RAW); add(2, 1u<<3);
    actions[0].value=9; actions[0].result=AZ_FRAME_EATEN;
    sdk_filter_raw(3, f, sizeof f);
    CHECK(actions[0].seen==52, "first-sees-input");
    CHECK(actions[1].seen==52, "next-copy-reset");
    CHECK(actions[0].calls==1 && actions[1].calls==1, "raw-eaten-does-not-stop-copy-chain");
    CHECK(actions[2].calls==0, "normal-not-in-raw-chain");
    CHECK(f[34]==52, "raw-cannot-edit-input");
    CHECK(g_cm_in_frame==0, "callback-context-cleared");
}
static void normal_chain(void) {
    scenario="normal-chain"; reset();
    unsigned char f[98]={0}; f[34]=52;
    for (int i=0; i<3; ++i) add(i, 1u<<3);
    actions[0].value=51; actions[0].result=AZ_FRAME_EATEN;
    CHECK(sdk_filter_run(3,f,sizeof f)==2, "eaten-result");
    CHECK(f[34]==51, "eaten-does-not-revert-frame");
    CHECK(actions[1].calls==0 && actions[2].calls==0, "later-observer-skipped");
    CHECK(g_cm_in_frame==0, "eaten-context-cleared");
    reset(); for (int i=0; i<3; ++i) add(i,1u<<3);
    f[34]=52; actions[2].value=51;
    CHECK(sdk_filter_run(3,f,sizeof f)==1, "normal-result");
    CHECK(actions[0].seen==52 && actions[1].seen==52, "early-observers-see-detent");
    CHECK(f[34]==51, "later-editor-rewrites-after-observer");
    CHECK(actions[0].calls==1 && actions[1].calls==1 && actions[2].calls==1, "all-normal-called");
}
static void eligibility(void) {
    scenario="eligibility"; reset();
    unsigned char f[98]={0}; f[34]=52;
    add(0,1u<<3); add(1,1u<<3); add(2,1u<<2); add(3,(1u<<3)|AZ_FILTER_RAW);
    g_sdk_ff[0].off=1; mods[1].state=0;
    CHECK(sdk_filter_run(3,f,sizeof f)==0, "no-eligible-normal");
    CHECK(actions[0].calls==0 && actions[1].calls==0 && actions[2].calls==0 && actions[3].calls==0, "normal-all-gates");
    sdk_filter_raw(3,f,sizeof f);
    CHECK(actions[3].calls==1 && actions[3].seen==52, "raw-only-eligible");
    g_sdk_ff[3].off=1;
    sdk_filter_raw(3,f,sizeof f);
    CHECK(actions[3].calls==1, "off-raw-skipped");
    CHECK(f[34]==52, "ineligible-preserves-input");
    reset(); add(0,(1u<<3)|AZ_FILTER_RAW);
    sdk_filter_raw(3,f,0); sdk_filter_raw(2,f,sizeof f);
    CHECK(actions[0].calls==0, "raw-length-and-bus-gates");
}
static void budget(int raw) {
    scenario=raw?"raw-budget":"normal-budget"; reset();
    unsigned char f[98]={0}; f[34]=52;
    add(0,(1u<<3)|(raw?AZ_FILTER_RAW:0));
    actions[0].cost=200;
    if (raw) sdk_filter_raw(3,f,sizeof f); else sdk_filter_run(3,f,sizeof f);
    CHECK(g_sdk_ff[0].strikes==0 && !g_sdk_ff[0].off, "budget-equality-allowed");
    actions[0].cost=201;
    for (unsigned n=1; n<=8; ++n) {
        if (raw) sdk_filter_raw(3,f,sizeof f); else sdk_filter_run(3,f,sizeof f);
        CHECK(g_sdk_ff[0].strikes==n, "cumulative-strikes");
        CHECK(g_sdk_ff[0].off==(n==8), "exact-off-threshold");
    }
    if (raw) sdk_filter_raw(3,f,sizeof f); else sdk_filter_run(3,f,sizeof f);
    CHECK(actions[0].calls==9 && g_sdk_ff[0].strikes==8 && g_sdk_ff[0].off==1, "future-call-skipped-no-reset");
    CHECK(logged==1, "one-forced-diagnostic-call");
    CHECK(f[34]==52 && g_cm_in_frame==0, "frame-and-context-preserved");
}
int main(void) {
    raw_copy(); normal_chain(); eligibility(); budget(0); budget(1);
    printf("checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
