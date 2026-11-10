/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FA_CORE_H
#define FA_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FA_ID_BYTES 33u
#define FA_EVENT_CAPACITY 6u

typedef enum {
    FA_VIRGIN = 0, FA_ACTIVE_INCOMPLETE = 1, FA_ORDINARY_RETURNED = 2
} fa_state;
typedef enum {
    FA_SUPPRESSED = 0, FA_ADMITTED = 1, FA_RETURNED = 2, FA_INCOMPLETE = 3
} fa_request_result;
typedef enum { FA_NO_COMMUNITY = 0, FA_COMMUNITY = 1 } fa_owner_kind;
typedef enum {
    FA_SELECTION = 0, FA_UNBLAMED = 1, FA_OPEN = 2, FA_WRITE = 3,
    FA_FSYNC = 4, FA_CLOSE = 5
} fa_event_kind;
typedef enum {
    FA_READY_ENTER = 0, FA_ENTERED = 1, FA_RAW_AVAILABLE = 2,
    FA_RECORDED = 3, FA_READY_COMPLETE = 4, FA_PUBLISHED = 5, FA_HALTED = 6
} fa_phase;

typedef struct {
    fa_owner_kind kind;
    uint32_t index;
    uint64_t process_id;
    char id[FA_ID_BYTES];
} fa_owner;
typedef struct { int64_t raw; int failure_errno; } fa_io_value;
/* Authored resolved values only. No SDK manifest/reader/capability validation.
 * Caller retains this bounded script through the last scheduled access. */
typedef struct {
    fa_owner selection;
    fa_io_value open, write, sync, close;
} fa_script;

typedef struct {
    fa_event_kind kind;
    bool entered;
    bool returned_recorded;
    int64_t raw;
    bool errno_valid;
    int error_number;
    size_t requested_length;
    char payload[FA_ID_BYTES];
} fa_event;
typedef struct {
    bool selection_known;
    fa_owner owner;
    bool known_failure;
    bool short_write;
    bool violation;
    unsigned event_count;
    fa_event events[FA_EVENT_CAPACITY];
} fa_result;
typedef struct {
    fa_state state;
    bool has_payload;
    fa_result result;
} fa_snapshot;
/* The raw latch is a separate serial scheduling oracle, not a public result.
 * Its selection bytes or IO return can exist before the event is recorded. */
typedef struct {
    bool available;
    unsigned sequence;
    fa_event_kind kind;
    fa_owner selection;
    fa_io_value io;
} fa_raw_oracle;
typedef struct {
    fa_phase phase;
    fa_event_kind next_kind;
    unsigned current_event;
    fa_result working;
    fa_raw_oracle raw_oracle;
} fa_private;

/* One process-lifetime invocation can win; other invocations are suppressed.
 * This authored synchronous entry is compile-only in the first fixture.
 * It performs no native I/O, loader, frame, signal or SDK calls. */
fa_request_result fa_run(const fa_script *script);
/* Exactly one acquire load; Virgin/Active return zeroed payload fields. */
void fa_observe(fa_snapshot *out);

#ifdef FA_FIXTURE
/* Serial step-driving only. captured_state has no baseline admission authority.
 * A failed begin changes neither script nor working/returned/raw payload.
 * The driver may advance only its admitted request; no concurrent driver API. */
fa_request_result fa_fixture_begin(const fa_script *script, fa_state captured_state);
bool fa_fixture_step(void);
/* Serial inspection after the driver has stopped advancing the winner.
 * This is not a concurrent progress snapshot or a signal-context interface. */
void fa_fixture_inspect(fa_private *out);
#endif
#endif
