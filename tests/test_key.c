/* SPDX-License-Identifier: GPL-3.0-only
 *
 * 0.6.5 Key Sync: the deck key that goes out in "track" and "state" packets (lib_key_set /
 * deck_key in flow-beat.c), on a computer.   make -C tests key
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

static int fails, checks;
static void want(const char *what, const char *got, const char *exp)
{
    checks++;
    if (strcmp(got, exp)) { printf("FAIL %s: \"%s\", want \"%s\"\n", what, got, exp); fails++; }
    else printf("ok   %s: \"%s\"\n", what, got);
}

int main(void)
{
    az_deck_info di; char k[8];
    memset(&di, 0, sizeof di);
    di.size = sizeof di; di.loaded = 1; di.content_id = 42;

    deck_key(0, &di, k);                    want("nothing known yet", k, "");
    lib_key_set(0, 42, "8A");
    deck_key(0, &di, k);                    want("library key for the loaded track", k, "8A");
    deck_key(1, &di, k);                    want("not another deck's key", k, "");
    di.content_id = 43;
    deck_key(0, &di, k);                    want("a new track: the old key is not reused", k, "");
    lib_key_set(0, 43, "F#m");
    deck_key(0, &di, k);                    want("classic key", k, "F#m");
    snprintf(di.key, sizeof di.key, "11B");
    deck_key(0, &di, k);                    want("the core's own key wins", k, "11B");
    di.key[0] = 0;
    lib_key_set(2, 7, "Ab \"m\\x\x01");
    di.content_id = 7;
    deck_key(2, &di, k);                    want("quotes, spaces, control bytes dropped, 7 max", k, "Abmx");
    lib_key_set(3, 9, "0123456789");
    di.content_id = 9;
    deck_key(3, &di, k);                    want("long key cut to 7", k, "0123456");
    di.loaded = 0;
    deck_key(3, &di, k);                    want("nothing loaded", k, "");
    lib_key_set(1, 5, NULL);
    di.loaded = 1; di.content_id = 5;
    deck_key(1, &di, k);                    want("library has no key", k, "");

    printf(fails ? "FAILED: %d of %d checks\n" : "ALL PASS: 0 of %d checks failed\n", fails ? fails : checks, checks);
    return fails != 0;
}
