/* SPDX-License-Identifier: GPL-3.0-only
 *
 * flow-beat's Mod Menu rows against the core's rules (azmod-mod.h, az_row_v1): key [a-z0-9-],
 * 1..24 characters, unique; label 1..31 bytes; at most 16 rows; a known kind; parents earlier.
 * The SDK harness has no register_row, so a row the deck refuses would pass it: 0.5.2's first
 * build had a 34-byte label and the key "link_audio", and the emulator refused the row.
 * Builds flow-beat.c into this test with a fake core that records the rows.   make -C tests rows
 */
#include "../flow-beat.c"

/* Link itself is not part of this test */
int flowlink_enable(int on) { (void)on; return 0; }
void flowlink_step(const flowlink_in *in, flowlink_out *out) { (void)in; memset(out, 0, sizeof *out); }
int flowlink_peers(void) { return -1; }
int flowlink_audio_enable(int on, unsigned rate, double extra_ms) { (void)on; (void)rate; (void)extra_ms; return 0; }
void flowlink_audio_send(int d, const float *lr, int frames) { (void)d; (void)lr; (void)frames; }
double flowlink_audio_rate_seen(int d) { (void)d; return 0; }
unsigned long flowlink_audio_sent(int d) { (void)d; return 0; }
unsigned flowlink_audio_rate_used(int d) { (void)d; return 0; }

#include <stdarg.h>

static int fails, nrows;
static char keys[32][32];

static void bad(const char *key, const char *why)
{
    printf("FAIL row \"%s\": %s\n", key ? key : "(null)", why);
    fails++;
}

static int t_register_row(const char *id, const az_row_v1 *r)
{
    if (strcmp(id, MOD_ID) != 0) bad(r->key, "registered with another mod's id");
    if (r->size != (int)sizeof(az_row_v1)) bad(r->key, "size is not sizeof(az_row_v1)");
    size_t kl = r->key ? strlen(r->key) : 0;
    if (kl < 1 || kl > 24) bad(r->key, "key must be 1 to 24 characters");
    for (size_t i = 0; i < kl; i++) {
        char c = r->key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            bad(r->key, "key may only use a-z, 0-9 and -");
            break;
        }
    }
    for (int i = 0; i < nrows; i++)
        if (r->key && !strcmp(keys[i], r->key)) bad(r->key, "key used twice");
    size_t ll = r->label ? strlen(r->label) : 0;
    if (ll < 1 || ll > 31) {
        char m[96];
        snprintf(m, sizeof m, "label is %zu bytes (1 to 31)", ll);
        bad(r->key, m);
    }
    if (r->kind < AZ_ROW_TOGGLE || r->kind > AZ_ROW_CHOICE) bad(r->key, "unknown kind");
    if (r->parent) {
        int found = 0;
        for (int i = 0; i < nrows; i++) found |= !strcmp(keys[i], r->parent);
        if (!found) bad(r->key, "parent is not an earlier row");
    }
    if (r->kind == AZ_ROW_CHOICE) {
        if (!r->choices || r->n_choices < 2 || r->n_choices > 8) bad(r->key, "a choice needs 2 to 8 labels");
        for (int i = 0; r->choices && i < r->n_choices; i++)
            if (!r->choices[i] || !r->choices[i][0] || strlen(r->choices[i]) > 31) bad(r->key, "choice label empty or over 31 bytes");
        if (r->default_choice < 0 || r->default_choice >= r->n_choices) bad(r->key, "default_choice out of range");
    }
    if (nrows >= 16) bad(r->key, "more than 16 rows");
    if (nrows < 32 && r->key) snprintf(keys[nrows], sizeof keys[nrows], "%s", r->key);
    printf("     row %-12s %2zu bytes  \"%s\"\n", r->key ? r->key : "?", ll, r->label ? r->label : "");
    return nrows++;
}

static int t_row_value(int h) { (void)h; return 0; }
static void t_log(const char *id, const char *fmt, ...) { (void)id; (void)fmt; }
static int t_cfg_get(const char *id, const char *key, int def) { (void)id; (void)key; return def; }
static int t_cfg_get_str(const char *id, const char *key, char *buf, int n, const char *def)
{
    (void)id; (void)key;
    snprintf(buf, (size_t)n, "%s", def ? def : "");
    return 0;
}
static int t_deck_info(int i, az_deck_info *o) { (void)i; memset(o, 0, sizeof *o); return -1; }
static int t_deck_grid(int i, double *beat, double *bar) { (void)i; *beat = 0; *bar = -1; return -1; }
static int t_thread_start(const char *id, const char *name, void *(*fn)(void *), void *arg)
{
    (void)id; (void)name; (void)fn; (void)arg;
    return -1;   /* no threads: only the rows matter here (init returns 4 after them) */
}
static int t_on_deck_event(const char *id, void (*fn)(int, int, void *), void *user)
{
    (void)id; (void)fn; (void)user;
    return 0;
}

int main(void)
{
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
    core.register_row = t_register_row;
    core.row_value = t_row_value;

    const az_mod_v1 *mod = az_mod_v1_entry();
    (void)mod->init(&core);
    if (nrows != 8) {
        printf("FAIL expected 8 rows, got %d\n", nrows);
        fails++;
    }
    int has_audio = 0;
    for (int i = 0; i < nrows; i++) has_audio |= !strcmp(keys[i], "link-audio");
    if (!has_audio) {
        printf("FAIL no link-audio row\n");
        fails++;
    }
    printf("%s: %d failure%s\n", fails ? "FAILED" : "ALL PASS", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
