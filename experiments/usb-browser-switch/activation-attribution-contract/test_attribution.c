/* SPDX-License-Identifier: GPL-3.0-only
 * Offline contract fixture: the generated include contains three exact pinned
 * SDK functions. All addresses, ELF headers, frame reads and blame-file results
 * below are synthetic. No real loader, signal, native memory or blame-file I/O
 * is used. stdout is only the fixture's deterministic test report.
 *
 * Run ONE selector in each fresh process. The included function-local static
 * written is neither reset nor exposed. This fixture proves selected source
 * decisions, not signal safety, durable logging, causation or a registry repair.
 */
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define PT_LOAD 1
#define CM_BLAME "fixture-only-blame"

struct model_phdr {
    uint32_t p_type;
    uint32_t p_flags; /* Deliberately ignored by the exact range callback. */
    uintptr_t p_vaddr;
    uintptr_t p_memsz;
};
struct dl_phdr_info {
    uintptr_t dlpi_addr;
    const struct model_phdr *dlpi_phdr;
    uint16_t dlpi_phnum;
};
typedef struct {
    uintptr_t lo;
    uintptr_t hi;
    char id[16];
    int state; /* Deliberately ignored by the exact ownership lookup. */
} cm_mod;

static cm_mod g_cm[4];
static int g_cm_n;
static int unblamed_calls;
static int model_violation;

struct cell { uintptr_t address, value; };
static struct cell cells[80];
static size_t cell_count;
static uintptr_t peek_trace[80];
static size_t peek_count;

enum io_kind { IO_OPEN = 1, IO_WRITE, IO_FSYNC, IO_CLOSE };
struct io_call { int kind, fd; size_t length; ssize_t result; };
static struct io_call io_trace[16];
static size_t io_count;
static int open_result = 7;
static int write_mode; /* 0 full, 1 returned -1, 2 returned short. */
static int fsync_result;
static int close_result;
static int observed_flags;
static unsigned observed_mode;
static int observed_path;
static unsigned char payload[32];
static size_t payload_length;

static void add_cell(uintptr_t address, uintptr_t value) {
    if (cell_count == sizeof cells / sizeof cells[0]) {
        model_violation = 1;
        return;
    }
    cells[cell_count++] = (struct cell){address, value};
}
static void add_frame(uintptr_t address, uintptr_t next, uintptr_t ra) {
    add_cell(address, next);
    add_cell(address + 8, ra);
}
static int crash_peek8(uintptr_t address, uintptr_t *out) {
    if (peek_count == sizeof peek_trace / sizeof peek_trace[0]) {
        model_violation = 1;
        return 0;
    }
    peek_trace[peek_count++] = address;
    for (size_t i = 0; i < cell_count; ++i) {
        if (cells[i].address == address) {
            *out = cells[i].value;
            return 1;
        }
    }
    /* No raw-pointer fallback and no output write on a modeled failed read. */
    return 0;
}
static void cm_note_unblamed_crash(void) { ++unblamed_calls; }

static void note_io(int kind, int fd, size_t length, ssize_t result) {
    if (io_count == sizeof io_trace / sizeof io_trace[0]) {
        model_violation = 1;
        return;
    }
    io_trace[io_count++] = (struct io_call){kind, fd, length, result};
}
static int fixture_open(const char *path, int flags, unsigned mode) {
    observed_path = strcmp(path, CM_BLAME) == 0;
    observed_flags = flags;
    observed_mode = mode;
    note_io(IO_OPEN, -1, 0, open_result);
    return open_result;
}
static ssize_t fixture_write(int fd, const void *bytes, size_t length) {
    payload_length = length;
    if (length <= sizeof payload) {
        memcpy(payload, bytes, length);
    } else {
        model_violation = 1;
    }
    ssize_t result = write_mode == 1 ? -1 :
                     write_mode == 2 ? (ssize_t)length - 1 : (ssize_t)length;
    note_io(IO_WRITE, fd, length, result);
    return result;
}
static int fixture_fsync(int fd) {
    note_io(IO_FSYNC, fd, 0, fsync_result);
    return fsync_result;
}
static int fixture_close(int fd) {
    note_io(IO_CLOSE, fd, 0, close_result);
    return close_result;
}

/* Only these file-operation names are substituted in the exact SDK bodies. */
#define open fixture_open
#define write fixture_write
#define fsync fixture_fsync
#define close fixture_close
#include "pinned-attribution.inc"
#undef open
#undef write
#undef fsync
#undef close

static unsigned checks;
static const char *failed[64];
static unsigned failures;

static void check(int value, const char *name) {
    ++checks;
    if (!value) {
        if (failures < sizeof failed / sizeof failed[0]) {
            failed[failures] = name;
        }
        ++failures;
    }
}

#define MOD_A ((uintptr_t)0x100000)
#define MOD_B ((uintptr_t)0x200000)
#define FRAME ((uintptr_t)0x300000)
#define NATIVE_PC ((uintptr_t)0x900010)
#define NATIVE_LR ((uintptr_t)0x900020)

static void setup_mods(void) {
    g_cm_n = 2;
    g_cm[0] = (cm_mod){MOD_A, MOD_A + 0x1000, "mod-a", 0};
    g_cm[1] = (cm_mod){MOD_B, MOD_B + 0x1000, "mod-b", 99};
}
static int trace_is(const uintptr_t *expected, size_t count) {
    if (peek_count != count) return 0;
    for (size_t i = 0; i < count; ++i) {
        if (peek_trace[i] != expected[i]) return 0;
    }
    return 1;
}
static int full_io_sequence(void) {
    return io_count == 4 &&
        io_trace[0].kind == IO_OPEN && io_trace[0].result == open_result &&
        io_trace[1].kind == IO_WRITE && io_trace[1].fd == open_result &&
        io_trace[2].kind == IO_FSYNC && io_trace[2].fd == open_result &&
        io_trace[2].result == fsync_result &&
        io_trace[3].kind == IO_CLOSE && io_trace[3].fd == open_result &&
        io_trace[3].result == close_result;
}
static void check_file(const char *id) {
    size_t length = strlen(id);
    check(full_io_sequence(), "io-sequence");
    check(observed_path && observed_flags == (O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC) &&
          observed_mode == 0644, "open-arguments");
    check(payload_length == length && io_trace[1].length == length &&
          memcmp(payload, id, length) == 0, "write-payload");
}

static void anonymous_miss(void) {
    setup_mods();
    add_frame(FRAME, FRAME + 0x20, NATIVE_LR);
    add_frame(FRAME + 0x20, 0, NATIVE_LR + 4);
    const char *owner = cm_blame_fault(NATIVE_PC, NATIVE_LR, FRAME);
    const uintptr_t expected[] = {FRAME, FRAME + 8, FRAME + 0x20, FRAME + 0x28};
    check(owner == NULL, "owner");
    check(trace_is(expected, 4), "peek-trace");
    check(unblamed_calls == 1, "unblamed-count");
    check(io_count == 0, "no-file-io");
}
static void pc_precedence(void) {
    setup_mods();
    add_frame(FRAME, 0, MOD_B + 16);
    const char *owner = cm_blame_fault(MOD_A + 16, MOD_B + 16, FRAME);
    check(owner == g_cm[0].id, "owner");
    check(peek_count == 0, "peek-trace");
    check(unblamed_calls == 0, "unblamed-count");
    check_file("mod-a");
}
static void lr_precedence(void) {
    setup_mods();
    add_frame(FRAME, 0, MOD_A + 16);
    const char *owner = cm_blame_fault(NATIVE_PC, MOD_B + 16, FRAME);
    check(owner == g_cm[1].id, "owner");
    check(peek_count == 0, "peek-trace");
    check(unblamed_calls == 0, "unblamed-count");
    check_file("mod-b");
}
static void frame_hit(void) {
    setup_mods();
    add_frame(FRAME, FRAME + 0x20, NATIVE_LR);
    add_frame(FRAME + 0x20, FRAME + 0x40, MOD_B + 16);
    add_frame(FRAME + 0x40, 0, MOD_A + 16);
    const char *owner = cm_blame_fault(NATIVE_PC, NATIVE_LR, FRAME);
    const uintptr_t expected[] = {FRAME, FRAME + 8, FRAME + 0x20, FRAME + 0x28};
    check(owner == g_cm[1].id, "owner");
    check(trace_is(expected, 4), "peek-trace");
    check(unblamed_calls == 0, "unblamed-count");
    check_file("mod-b");
}
static void range_envelope(void) {
    g_cm_n = 1;
    g_cm[0] = (cm_mod){MOD_A, 0, "mod-a", 0};
    const struct model_phdr headers[] = {
        {PT_LOAD, 5, 0x100, 0x100},
        {PT_LOAD, 6, 0x400, 0x80}, /* Non-executable PT_LOAD still belongs to envelope. */
        {2, 5, 0x800, 0x100}
    };
    struct dl_phdr_info info = {MOD_A, headers, 3};
    struct dl_phdr_info wrong = {MOD_B, headers, 3};
    check(cm_range_cb(&wrong, 0, &g_cm[0]) == 0 &&
          g_cm[0].lo == MOD_A && g_cm[0].hi == 0, "different-image-untouched");
    check(cm_range_cb(&info, 0, &g_cm[0]) == 1 &&
          g_cm[0].lo == MOD_A + 0x100 && g_cm[0].hi == MOD_A + 0x480,
          "load-envelope");
    check(cm_owner_of(MOD_A + 0x100) == 0 &&
          cm_owner_of(MOD_A + 0x47f) == 0, "included-boundaries");
    check(cm_owner_of(MOD_A + 0xff) == -1 &&
          cm_owner_of(MOD_A + 0x480) == -1, "excluded-boundaries");
    check(cm_owner_of(MOD_A + 0x300) == 0, "envelope-gap-owned");
    check(cm_owner_of(MOD_A + 0x440) == 0, "non-executable-load-owned");
    check(cm_owner_of(MOD_A + 0x810) == -1, "non-load-excluded");
    g_cm[1] = (cm_mod){MOD_A + 0x200, MOD_A + 0x600, "mod-b", 99};
    g_cm_n = 2;
    check(cm_owner_of(MOD_A + 0x300) == 0 &&
          cm_owner_of(MOD_A + 0x500) == 1, "overlap-first-index");
    g_cm[0].hi = 0;
    check(cm_owner_of(MOD_A + 0x100) == -1 &&
          cm_owner_of(MOD_A + 0x300) == 1, "zero-hi-absent");
    cm_mod empty = {MOD_B, 123, "empty", 0};
    const struct model_phdr nonload[] = {{2, 5, 0x100, 0x100}};
    struct dl_phdr_info no_load = {MOD_B, nonload, 1};
    check(cm_range_cb(&no_load, 0, &empty) == 0 &&
          empty.lo == MOD_B && empty.hi == 123, "no-load-unchanged");
    check(peek_count == 0 && io_count == 0 && unblamed_calls == 0, "pure-range-model");
}
static void frame_first_read_failure(void) {
    setup_mods();
    /* The saved RA exists, but its preceding next-FP cell is unreadable. */
    add_cell(FRAME + 8, MOD_A + 16);
    const char *owner = cm_blame_fault(NATIVE_PC, NATIVE_LR, FRAME);
    const uintptr_t expected[] = {FRAME};
    check(owner == NULL, "owner");
    check(trace_is(expected, 1), "peek-trace");
    check(unblamed_calls == 1, "unblamed-count");
    check(io_count == 0, "no-file-io");
}
static void frame_second_read_failure(void) {
    setup_mods();
    add_cell(FRAME, FRAME + 0x20);
    /* The first RA is unreadable; the later owned frame must not be reached. */
    add_frame(FRAME + 0x20, 0, MOD_A + 16);
    const char *owner = cm_blame_fault(NATIVE_PC, NATIVE_LR, FRAME);
    const uintptr_t expected[] = {FRAME, FRAME + 8};
    check(owner == NULL, "owner");
    check(trace_is(expected, 2), "peek-trace");
    check(unblamed_calls == 1, "unblamed-count");
    check(io_count == 0, "no-file-io");
}
static void frame_limit(int hit) {
    setup_mods();
    for (size_t i = 0; i < 17; ++i) {
        uintptr_t here = FRAME + i * 0x20;
        uintptr_t ra = i == (hit ? 15u : 16u) ? MOD_A + 16 : NATIVE_LR;
        add_frame(here, here + 0x20, ra);
    }
    const char *owner = cm_blame_fault(NATIVE_PC, NATIVE_LR, FRAME);
    uintptr_t expected[32];
    for (size_t i = 0; i < 16; ++i) {
        expected[i * 2] = FRAME + i * 0x20;
        expected[i * 2 + 1] = FRAME + i * 0x20 + 8;
    }
    check(owner == (hit ? g_cm[0].id : NULL), "owner");
    check(trace_is(expected, 32), "peek-trace");
    check(unblamed_calls == (hit ? 0 : 1), "unblamed-count");
    if (hit) check_file("mod-a");
    else check(io_count == 0, "no-file-io");
}
static void invalid_frame(uintptr_t fp) {
    setup_mods();
    /* Data is present: only the source guard prevents these modeled reads. */
    add_frame(fp, 0, MOD_A + 16);
    const char *owner = cm_blame_fault(NATIVE_PC, NATIVE_LR, fp);
    check(owner == NULL, "owner");
    check(peek_count == 0, "peek-trace");
    check(unblamed_calls == 1, "unblamed-count");
    check(io_count == 0, "no-file-io");
}
static void nonincreasing_frame(void) {
    setup_mods();
    add_frame(FRAME, FRAME - 0x20, MOD_A + 16);
    const char *owner = cm_blame_fault(NATIVE_PC, NATIVE_LR, FRAME);
    const uintptr_t expected[] = {FRAME, FRAME + 8};
    check(owner == g_cm[0].id, "owner-before-next-check");
    check(trace_is(expected, 2), "peek-trace");
    check(unblamed_calls == 0, "unblamed-count");
    check_file("mod-a");
}
static void first_call_guard(void) {
    setup_mods();
    const char *first = cm_blame_fault(NATIVE_PC, NATIVE_LR, 0);
    check(first == NULL && unblamed_calls == 1 && io_count == 0 &&
          peek_count == 0, "first-unblamed");
    const char *second = cm_blame_fault(MOD_A + 16, MOD_B + 16, FRAME);
    check(second == NULL, "second-owner");
    check(unblamed_calls == 1 && io_count == 0 && peek_count == 0,
          "second-no-side-effects");
}
static void open_failure(void) {
    setup_mods();
    open_result = -1;
    const char *first = cm_blame_fault(MOD_A + 16, NATIVE_LR, 0);
    check(first == g_cm[0].id, "owner-despite-open-failure");
    check(io_count == 1 && io_trace[0].kind == IO_OPEN &&
          io_trace[0].result == -1, "open-only");
    check(observed_path && observed_flags == (O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC) &&
          observed_mode == 0644, "open-arguments");
    check(unblamed_calls == 0 && peek_count == 0, "no-fallback");
    const char *second = cm_blame_fault(MOD_B + 16, NATIVE_LR, 0);
    check(second == NULL && io_count == 1 && unblamed_calls == 0,
          "second-no-retry");
}
static void write_failure(int short_write) {
    setup_mods();
    write_mode = short_write ? 2 : 1;
    fsync_result = -1;
    close_result = -1;
    const char *first = cm_blame_fault(MOD_A + 16, NATIVE_LR, 0);
    check(first == g_cm[0].id, "owner-despite-file-failure");
    check_file("mod-a");
    check(io_trace[1].result == (short_write ? 4 : -1), "write-result");
    check(unblamed_calls == 0 && peek_count == 0, "no-fallback");
    const char *second = cm_blame_fault(MOD_B + 16, NATIVE_LR, 0);
    check(second == NULL && io_count == 4 && unblamed_calls == 0,
          "second-no-retry");
}

static void fsync_close_failure(void) {
    setup_mods();
    fsync_result = -1;
    close_result = -1;
    const char *first = cm_blame_fault(MOD_A + 16, NATIVE_LR, 0);
    check(first == g_cm[0].id, "owner-despite-sync-close-failure");
    check_file("mod-a");
    check(io_trace[1].result == 5, "full-write-result");
    check(unblamed_calls == 0 && peek_count == 0, "no-fallback");
    const char *second = cm_blame_fault(MOD_B + 16, NATIVE_LR, 0);
    check(second == NULL && io_count == 4 && unblamed_calls == 0,
          "second-no-retry");
}

static void frame_limit_hit(void) { frame_limit(1); }
static void frame_limit_miss(void) { frame_limit(0); }
static void invalid_low_frame(void) { invalid_frame((uintptr_t)0xfff8); }
static void invalid_unaligned_frame(void) { invalid_frame((uintptr_t)0x10001); }
static void write_error_case(void) { write_failure(0); }
static void write_short_case(void) { write_failure(1); }

struct selector { const char *name; void (*run)(void); };
static const struct selector selectors[] = {
    {"anonymous-miss", anonymous_miss},
    {"pc-precedence", pc_precedence},
    {"lr-precedence", lr_precedence},
    {"frame-hit", frame_hit},
    {"range-envelope", range_envelope},
    {"frame-first-read-failure", frame_first_read_failure},
    {"frame-second-read-failure", frame_second_read_failure},
    {"frame-limit-hit", frame_limit_hit},
    {"frame-limit-miss", frame_limit_miss},
    {"invalid-low-frame", invalid_low_frame},
    {"invalid-unaligned-frame", invalid_unaligned_frame},
    {"nonincreasing-frame", nonincreasing_frame},
    {"first-call-guard", first_call_guard},
    {"open-failure", open_failure},
    {"write-failure", write_error_case},
    {"write-short", write_short_case},
    {"fsync-close-failure", fsync_close_failure}
};

int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: test-attribution <selector>\n", stderr);
        return 2;
    }
    const struct selector *selected = NULL;
    for (size_t i = 0; i < sizeof selectors / sizeof selectors[0]; ++i) {
        if (strcmp(argv[1], selectors[i].name) == 0) selected = &selectors[i];
    }
    if (selected == NULL) {
        fputs("unknown selector\n", stderr);
        return 2;
    }
    selected->run();
    check(!model_violation, "model-bounds");
    printf("{\"selector\":\"%s\",\"checks\":%u,\"failures\":%u,\"failed_checks\":[",
           selected->name, checks, failures);
    for (unsigned i = 0; i < failures && i < sizeof failed / sizeof failed[0]; ++i) {
        printf("%s\"%s\"", i ? "," : "", failed[i]);
    }
    puts("]}");
    return failures ? 1 : 0;
}
