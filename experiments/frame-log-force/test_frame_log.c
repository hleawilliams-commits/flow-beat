/* SPDX-License-Identifier: GPL-3.0-only */
/* Production logging functions and the exact drain block are supplied by run-tests.py. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct unused_mod az_mod_v1;
typedef struct unused_screen az_screen_v1;
#define CM_LOGDIR "logs"
#include PRODUCTION_INC

static unsigned checks, failed, case_n;
static char root_dir[PATH_MAX];
#define CHECK(cond, name) do { checks++; if (!(cond)) { \
    failed++; fprintf(stderr, "FAIL: %s (line %d)\n", name, __LINE__); } } while (0)

static cm_mod *reset_case(void) {
    char dir[32];
    if (chdir(root_dir)) { perror("chdir"); exit(2); }
    snprintf(dir, sizeof dir, "case-%u", ++case_n);
    if (mkdir(dir, 0700) || chdir(dir)) { perror("case directory"); exit(2); }
    memset(g_cm, 0, sizeof g_cm);
    g_cm_n = 1;
    g_cm_logs_on = 0;
    g_cm_in_frame = 0;
    strcpy(g_cm[0].id, "test-mod");
    strcpy(g_cm[1].id, "second-mod");
    return &g_cm[0];
}

static const char *contents(const char *id) {
    static char text[32768];
    char path[160];
    snprintf(path, sizeof path, CM_LOGDIR "/%s.log", id);
    FILE *f = fopen(path, "rb");
    text[0] = 0;
    if (!f) {
        if (errno != ENOENT) { perror("read log"); exit(2); }
        return text;
    }
    size_t n = fread(text, 1, sizeof text - 1, f);
    if (ferror(f) || !feof(f)) { fprintf(stderr, "log read error/overflow\n"); exit(2); }
    text[n] = 0;
    fclose(f);
    return text;
}

static unsigned lines(const char *s) {
    unsigned n = 0;
    while (*s) if (*s++ == '\n') n++;
    return n;
}

static void queue_line(cm_mod *c, int force, const char *text) {
    g_cm_in_frame = 1;
    cm_logf(c, force, "%s", text);
    g_cm_in_frame = 0;
}

static int empty_queue(cm_mod *c) {
    for (int k = 0; k < 8; k++)
        if (__atomic_load_n(&c->fq.ready[k], __ATOMIC_ACQUIRE)) return 0;
    return 1;
}

static void forced_and_ordinary_off(void) {
    cm_mod *c = reset_case();
    queue_line(c, 0, "ordinary-off");
    CHECK(c->fq.w == 0 && empty_queue(c), "logs-off ordinary line never consumes a slot");
    queue_line(c, 1, "forced-off");
    CHECK(c->fq.w == 1 && !empty_queue(c), "forced line reaches queue with logs off");
    CHECK(!*contents(c->id), "frame enqueue writes no log file");
    drain_actual();
    CHECK(!strcmp(contents(c->id), "forced-off\n"), "forced line survives logs-off drain");
    CHECK(empty_queue(c), "logs-off drain releases slot");
    drain_actual();
    CHECK(lines(contents(c->id)) == 1, "second drain does not duplicate forced line");
}

static void both_on_and_setting_changes(void) {
    cm_mod *c = reset_case();
    g_cm_logs_on = 1;
    queue_line(c, 0, "ordinary-on");
    queue_line(c, 1, "forced-on");
    CHECK(!*contents(c->id), "logs-on enqueue still writes no file");
    drain_actual();
    CHECK(!strcmp(contents(c->id), "ordinary-on\nforced-on\n"), "logs-on drains both kinds");

    c = reset_case();
    g_cm_logs_on = 1;
    queue_line(c, 0, "ordinary-before-disable");
    queue_line(c, 1, "forced-before-disable");
    g_cm_logs_on = 0;
    drain_actual();
    CHECK(!strcmp(contents(c->id), "forced-before-disable\n"), "only forced queued line survives logs being disabled");
    CHECK(empty_queue(c), "suppressed ordinary line also releases its slot");

    c = reset_case();
    queue_line(c, 0, "ordinary-before-enable");
    queue_line(c, 1, "forced-before-enable");
    g_cm_logs_on = 1;
    drain_actual();
    CHECK(!strcmp(contents(c->id), "forced-before-enable\n"), "enabling logs does not revive a refused ordinary line");
}

static void overflow(void) {
    cm_mod *c = reset_case();
    g_cm_logs_on = 1;
    for (int i = 0; i < 8; i++) {
        char text[32]; snprintf(text, sizeof text, "accepted-%d", i);
        queue_line(c, i & 1, text);
    }
    queue_line(c, 1, "overflow-forced");
    queue_line(c, 0, "overflow-ordinary");
    CHECK(c->fq.dropped == 2 && c->fq.w == 10, "full queue drops forced and ordinary alike");
    CHECK(!*contents(c->id), "full queue and overflow do not write on frame thread");
    drain_actual();
    const char *s = contents(c->id);
    CHECK(lines(s) == 9, "eight accepted lines plus existing ordinary overflow notice");
    CHECK(!strstr(s, "overflow-forced") && !strstr(s, "overflow-ordinary"), "overflow never overwrites accepted slots");
    CHECK(strstr(s, "(2 log lines from on_frame dropped: log less there)"), "overflow count and text remain unchanged");
    for (int i = 0; i < 8; i++) {
        char text[32]; snprintf(text, sizeof text, "accepted-%d\n", i);
        CHECK(strstr(s, text), "all accepted mixed-priority lines survive");
    }
    CHECK(c->fq.dropped == 0 && empty_queue(c), "drain resets drop count and releases all slots");

    c = reset_case();
    for (int i = 0; i < 8; i++) queue_line(c, 1, "forced-kept");
    queue_line(c, 1, "forced-dropped");
    CHECK(c->fq.dropped == 1, "forced overflow still counted with logs off");
    drain_actual();
    s = contents(c->id);
    CHECK(lines(s) == 8, "full forced queue survives logs-off drain");
    CHECK(!strstr(s, "forced-dropped") && !strstr(s, "log lines"), "logs-off overflow remains best effort without forced summary");
    CHECK(c->fq.dropped == 0 && empty_queue(c), "logs-off overflow drain resets queue");
}

static void slot_reuse(void) {
    cm_mod *c = reset_case();
    g_cm_logs_on = 1;
    queue_line(c, 1, "first-forced-slot-zero");
    drain_actual();
    for (int i = 0; i < 7; i++) queue_line(c, 0, "filler");
    drain_actual();
    CHECK(c->fq.w == 8 && empty_queue(c), "write index has wrapped after real slot consumption");
    queue_line(c, 0, "ordinary-reused-slot-zero");
    g_cm_logs_on = 0;
    drain_actual();
    CHECK(!strstr(contents(c->id), "ordinary-reused"), "reused ordinary slot does not inherit force");
    queue_line(c, -7, "forced-reused-slot-one");
    drain_actual();
    CHECK(strstr(contents(c->id), "forced-reused-slot-one\n"), "nonzero force survives ordinary-to-forced slot reuse");
    CHECK(empty_queue(c), "reused slots released");
}

static void format_cap_and_direct(void) {
    cm_mod *c = reset_case();
    char long_text[400]; memset(long_text, 'x', sizeof long_text - 1); long_text[399] = 0;
    queue_line(c, 1, long_text);
    drain_actual();
    const char *s = contents(c->id);
    CHECK(strlen(s) == 160 && s[159] == '\n', "forced queue preserves 159-byte text bound plus newline");

    c = reset_case();
    cm_logf(c, 0, "direct-ordinary");
    cm_logf(c, 1, "direct-forced");
    CHECK(!strcmp(contents(c->id), "direct-forced\n"), "non-frame logging behavior unchanged");
    c->log_bytes = CM_LOG_CAP;
    queue_line(c, 1, "forced-over-cap");
    cm_logf(c, 1, "direct-over-cap");
    CHECK(empty_queue(c) && c->fq.w == 0, "forced logging does not bypass enqueue cap");
    CHECK(!strcmp(contents(c->id), "direct-forced\n"), "forced direct logging does not bypass file cap");

    c = reset_case();
    queue_line(c, 1, "cap-reached-before-drain");
    c->log_bytes = CM_LOG_CAP;
    drain_actual();
    CHECK(!*contents(c->id) && empty_queue(c), "cap reached while queued drops and releases forced line");
}

static void production_strike(void) {
    cm_mod *c = reset_case();
    unsigned strikes = 0;
    int off = 0;
    c->state = 77;
    g_cm_in_frame = 1;
    for (int i = 1; i < SDK_FILTER_STRIKES; i++) sdk_strike(c, &strikes, &off, "frame filter");
    CHECK(!off && empty_queue(c), "production strikes below threshold do not disable or log");
    sdk_strike(c, &strikes, &off, "frame filter");
    CHECK(off == 1 && strikes == SDK_FILTER_STRIKES && c->state == 77, "production strike still disables only filter at unchanged threshold");
    CHECK(c->fq.w == 1 && !*contents(c->id), "production forced overrun message queues without file IO");
    sdk_strike(c, &strikes, &off, "frame filter");
    CHECK(c->fq.w == 1, "production strike message still emitted once");
    g_cm_in_frame = 0;
    drain_actual();
    CHECK(strstr(contents(c->id), "frame filter switched off for this session:"), "production overrun diagnostic survives logs-off drain");
    CHECK(lines(contents(c->id)) == 1, "production overrun diagnostic writes exactly one line");
}

static void separate_mods_and_refusals(void) {
    cm_mod *c = reset_case();
    cm_mod *other = &g_cm[1];
    g_cm_n = 2;
    g_cm_logs_on = 1;
    queue_line(c, 0, "first-ordinary");
    queue_line(other, 1, "second-forced");
    g_cm_logs_on = 0;
    drain_actual();
    CHECK(!*contents(c->id), "ordinary first mod stays silent with logs off");
    CHECK(!strcmp(contents(other->id), "second-forced\n"), "force metadata stays with owning mod");
    c->fq.refused = 3;
    drain_actual();
    CHECK(strstr(contents(c->id), "3 calls to settings or file services"), "existing forced refusal diagnostic unchanged");
    CHECK(!c->fq.refused && empty_queue(c) && empty_queue(other), "both mods and refusal counter drain independently");
}

int main(void) {
    if (!getcwd(root_dir, sizeof root_dir)) { perror("getcwd"); return 2; }
    forced_and_ordinary_off();
    both_on_and_setting_changes();
    overflow();
    slot_reuse();
    format_cap_and_direct();
    production_strike();
    separate_mods_and_refusals();
    printf("%u checks, %u failed\n", checks, failed);
    return failed ? 1 : 0;
}
