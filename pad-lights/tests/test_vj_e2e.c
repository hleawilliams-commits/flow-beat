/* SPDX-License-Identifier: GPL-3.0-only
 * VJ Pads end to end on the host: the SDK harness has no frame_filter, so this drives the mod's real
 * filter and sender thread with a small fake core. ERP input frames go in; we check what the deck would
 * see, the UDP JSON that reaches 127.0.0.1, and the LED frame's violet mode buttons after a tick.
 *   cc -I<sdk>/include -o /tmp/vje tests/test_vj_e2e.c -lpthread -lm && /tmp/vje */
#include <assert.h>
#include <sys/time.h>
#include "../pad-lights.c"

static int checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
static az_frame_fn filt;
static int vj_row = 0, port;
static int f_frame_filter(const char *id, unsigned mask, az_frame_fn fn, void *u) { (void)id; (void)u; assert(mask & 1u << AZ_BUS_ERP_IN); filt = fn; return 0; }
static int f_register_row(const char *id, const az_row_v1 *r) { (void)id; return !strcmp(r->key, "vj") ? 1 : 0; }
static int f_row_value(int h) { return h == 1 ? vj_row : PL_IDLE; }
static int f_deck_info(int d, az_deck_info *o) { (void)d; o->loaded = 0; return 0; }
static int f_thread_start(const char *id, const char *n, void *(*fn)(void *), void *a) { (void)id; (void)n; pthread_t t; return pthread_create(&t, NULL, fn, a); }
static int f_cfg_get(const char *id, const char *k, int def) { (void)id; return !strcmp(k, "port") ? port : def; }
static int f_cfg_get_str(const char *id, const char *k, char *b, int n, const char *def) { (void)id; snprintf(b, (size_t)n, "%s", !strcmp(k, "host") ? "127.0.0.1" : def); return 0; }
static void f_log(const char *id, const char *fmt, ...) { (void)id; (void)fmt; }

static int rx;
static int recv_json(char *b, int n)        /* next datagram within 300 ms, or 0 */
{
    ssize_t r = recv(rx, b, (size_t)n - 1, 0);
    if (r <= 0) return 0;
    b[r] = 0; return 1;
}
static void drain(void) { char b[256]; while (recv_json(b, sizeof b)) {} }
static void sleep_ms(int ms) { struct timespec t = {0, ms * 1000000L}; nanosleep(&t, NULL); }
static unsigned char in[30];
static void press(int modes, int pads, int shift)   /* side A */
{
    memset(in, 0, sizeof in);
    in[0x0d] = (unsigned char)modes; in[0x0e] = (unsigned char)pads; in[0x09] = shift ? 0x80 : 0;
    filt(AZ_BUS_ERP_IN, in, sizeof in, NULL);
    sleep_ms(20);
}
int main(void)
{
    rx = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(rx, (struct sockaddr *)&a, sizeof a) == 0);
    socklen_t al = sizeof a; getsockname(rx, (struct sockaddr *)&a, &al); port = ntohs(a.sin_port);
    struct timeval tv = {0, 300000}; setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    static az_core_v1 core;
    core.abi = AZ_MOD_ABI; core.size = sizeof core;
    core.frame_filter = f_frame_filter; core.register_row = f_register_row; core.row_value = f_row_value;
    core.deck_info = f_deck_info; core.thread_start = f_thread_start; core.cfg_get = f_cfg_get;
    core.cfg_get_str = f_cfg_get_str; core.log = f_log;
    CHECK(pl_init(&core) == 0 && filt);
    pl_tick();
    char b[256];

    /* row off: the combo does nothing, pads reach the deck */
    press(0x9, 0x01, 0); CHECK(in[0x0e] == 0x01 && in[0x0d] == 0x9);
    press(0, 0, 0); drain();
    /* row on: combo opens the page (the deck doesn't see it), the sender says so */
    vj_row = 1; pl_tick();
    press(0x9, 0, 0); CHECK(in[0x0d] == 0);
    CHECK(recv_json(b, sizeof b) && strstr(b, "\"type\":\"vjpage\"") && strstr(b, "\"side\":1") && strstr(b, "\"on\":1"));
    press(0, 0, 0);
    /* pad 3 press + release: eaten, sent twice each with the same n */
    press(0, 0x04, 0); CHECK(in[0x0e] == 0);
    char b2[256];
    CHECK(recv_json(b, sizeof b) && recv_json(b2, sizeof b2) && !strcmp(b, b2));
    CHECK(strstr(b, "\"type\":\"vjpad\"") && strstr(b, "\"pad\":3") && strstr(b, "\"down\":1") && strstr(b, "\"shift\":0"));
    press(0, 0, 0);
    CHECK(recv_json(b, sizeof b) && strstr(b, "\"pad\":3") && strstr(b, "\"down\":0")); drain();
    /* SHIFT + pad 2: the release keeps shift 1 even after SHIFT is let go first */
    press(0, 0x02, 1); CHECK(recv_json(b, sizeof b) && strstr(b, "\"pad\":2") && strstr(b, "\"shift\":1")); drain();
    press(0, 0x02, 0); press(0, 0, 0);
    CHECK(recv_json(b, sizeof b) && strstr(b, "\"down\":0") && strstr(b, "\"shift\":1")); drain();
    /* LED frame: pads lit by the VJ page, mode buttons violet */
    pl_tick();
    unsigned char led[128]; memset(led, 0, sizeof led);
    filt(AZ_BUS_ERP_LED, led, sizeof led, NULL);
    CHECK(led[0x0e] == pl_gam[150] && led[0x0e + 2] == 255 && led[0x0e + 12 + 2] == 255);
    CHECK(led[0x1e + 4 * 2] || led[0x1e + 4 * 2 + 1] || led[0x1e + 4 * 2 + 2]);    /* pad 3 = the running Look */
    CHECK(led[0x38 + 0x0e] == 0);                                                   /* right side untouched */
    /* HOT CUE alone: page closes and the press reaches the deck */
    press(0x1, 0, 0); CHECK(in[0x0d] == 0x1);
    CHECK(recv_json(b, sizeof b) && strstr(b, "\"vjpage\"") && strstr(b, "\"on\":0"));
    press(0, 0x01, 0); CHECK(in[0x0e] == 0x01);                                     /* pads are the deck's again */
    printf("vj e2e: %d checks passed\n", checks);
    return 0;
}
