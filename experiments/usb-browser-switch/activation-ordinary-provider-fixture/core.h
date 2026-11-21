/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef AC_CORE_H
#define AC_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "receipt-types.h"

#if defined(AC_FIXTURE) || defined(AC_CONTROL_SELECT_BEFORE_CAS) || \
    defined(AC_CONTROL_SECOND_ADMISSION) || defined(AC_CONTROL_BINDING_AS_SLOT) || \
    defined(AC_CONTROL_NARROW_PROCESS) || defined(AC_CONTROL_RESELECT_AT_RECORD) || \
    defined(AC_CONTROL_STOP_ON_CORE_PC) || defined(AC_CONTROL_REFRESH_NEGATIVE) || \
    defined(AC_CONTROL_EXPOSE_RAW_SELECTION) || defined(AO_CONTROL_OMIT_DRIVER) || \
    defined(AO_CONTROL_STOP_AFTER_SELECTION_RAW) || defined(AO_CONTROL_WRONG_PUBLISHED_RETURN) || \
    defined(AO_CONTROL_WRONG_HALTED_RETURN) || defined(AO_CONTROL_OMIT_COMMIT_PHASE_GUARDS) || \
    defined(RC_FIXTURE) || defined(RC_CONTROL_ORIGINAL_BEFORE_METADATA) || \
    defined(RC_CONTROL_LITERAL_AS_INSTRUCTION) || defined(RC_CONTROL_ERASE_CLEANUP_FAILURE) || \
    defined(RC_CONTROL_TOKEN_EVIDENCE_ALIAS) || defined(RC_CONTROL_WARNING_AS_REFUSAL)
#error "predecessor fixtures and controls are excluded from this provider witness"
#endif
#if (defined(AP_CONTROL_EARLY_PROVIDER) || defined(AP_CONTROL_WRONG_SUPPRESSED_RETURN) || \
     defined(AP_CONTROL_OMIT_UNBLAMED) || defined(AP_CONTROL_ALTERNATE_CONTEXT) || \
     defined(AP_CONTROL_DECOY_WRITE) || defined(AP_CONTROL_REPLACE_CLOSE_RETURN) || \
     defined(AP_CONTROL_REPEAT_SHORT_WRITE) || defined(AP_CONTROL_NARROW_OPEN_TOKEN)) && !defined(AO_WITNESS_SETUP)
#error "provider witness controls require authored setup"
#endif
#if (defined(AP_CONTROL_EARLY_PROVIDER) + defined(AP_CONTROL_WRONG_SUPPRESSED_RETURN) + \
     defined(AP_CONTROL_OMIT_UNBLAMED) + defined(AP_CONTROL_ALTERNATE_CONTEXT) + \
     defined(AP_CONTROL_DECOY_WRITE) + defined(AP_CONTROL_REPLACE_CLOSE_RETURN) + \
     defined(AP_CONTROL_REPEAT_SHORT_WRITE) + defined(AP_CONTROL_NARROW_OPEN_TOKEN)) > 1
#error "one exact provider witness control per executable"
#endif
#ifdef AP_CONTROL_ALTERNATE_CONTEXT
/* Fixed authored live alternate binding; no setter/accessor or new engine authority. */
extern void *const ap_control_alternate_context;
#endif

#define MA_MODULES 4u
#define AC_ID_BYTES 33u
#define AC_EVENT_CAPACITY 6u
#define AC_SELECT_CALLS 51u
#define AC_MODEL_CELLS 64u
#define AC_SCRATCH_RUNS 4u
#define AC_DIAGNOSTICS 8u

/* Exact retained snapshot shape; immutable module storage is defined in readers.inc. */
typedef struct {
    bool committed;
    unsigned index;
    uint64_t process_id;
    uintptr_t lo, hi;
    char id[33];
} ma_module_value;

typedef enum { AC_VIRGIN=0, AC_ACTIVE_INCOMPLETE=1, AC_ORDINARY_RETURNED=2 } ac_state;
typedef enum { AC_SUPPRESSED=0, AC_ADMITTED=1, AC_RETURNED=2, AC_INCOMPLETE=3 } ac_request_result;
typedef enum { AC_NO_COMMUNITY=0, AC_COMMUNITY=1 } ac_owner_kind;
typedef enum {
    AC_SELECTION=0, AC_UNBLAMED=1, AC_OPEN=2, AC_WRITE=3, AC_FSYNC=4, AC_CLOSE=5
} ac_event_kind;
typedef enum {
    AC_READY_ENTER=0, AC_ENTERED=1, AC_RAW_AVAILABLE=2, AC_RECORDED=3,
    AC_READY_COMPLETE=4, AC_PUBLISHED=5, AC_HALTED=6
} ac_phase;
typedef struct {
    ac_owner_kind kind;
    uint32_t index;
    uint64_t process_id;
    char id[AC_ID_BYTES];
} ac_owner;
typedef struct { int64_t raw; int failure_errno; } ac_io_value;
/* Callbacks receive copies; no private owner/event/raw pointer is exposed. */
typedef struct {
    int64_t accepted_open_token;
    size_t requested_length;
    char payload[AC_ID_BYTES];
} ap_write_args;
typedef struct {
    ac_io_value (*open)(void *context, ac_owner owner_value);
    ac_io_value (*write)(void *context, ap_write_args args_value);
    ac_io_value (*sync)(void *context, int64_t accepted_open_token);
    ac_io_value (*close)(void *context, int64_t accepted_open_token);
    void (*unblamed)(void *context);
} ap_provider_table;
typedef struct {
    uintptr_t pc, lr, fp;
    const ap_provider_table *providers;
    void *context;
} ac_request;
typedef struct {
    ac_event_kind kind;
    bool entered, returned_recorded;
    int64_t raw;
    bool errno_valid;
    int error_number;
    size_t requested_length;
    char payload[AC_ID_BYTES];
} ac_event;
typedef struct {
    bool selection_known;
    ac_owner owner;
    bool known_failure, short_write, violation;
    unsigned event_count;
    ac_event events[AC_EVENT_CAPACITY];
} ac_result;
typedef struct { ac_state state; bool has_payload; ac_result result; } ac_snapshot;
typedef struct {
    bool available;
    unsigned sequence;
    ac_event_kind kind;
    ac_owner selection;
    ac_io_value io;
} ac_raw_oracle;
typedef enum {
    AC_SELECT_PC=0, AC_SELECT_LR=1, AC_SELECT_NFP=2, AC_SELECT_RA=3,
    AC_SELECT_FRAME_OWNER=4, AC_SELECT_SNAPSHOT=5, AC_SELECT_DONE=6, AC_SELECT_ERROR=7
} ac_select_phase;
typedef enum { AC_PENDING=0, AC_RETURNED_VALUE=1, AC_RETURNED_INVARIANT_ERROR=2 } ac_select_status;
typedef enum {
    AC_ERROR_NONE=0, AC_ERROR_REQUEST=1, AC_ERROR_TRACE_CAPACITY=2,
    AC_ERROR_INDEX=3, AC_ERROR_SNAPSHOT=4, AC_ERROR_TUPLE=5
} ac_select_error;
typedef struct {
    ac_select_phase operation;
    uintptr_t address;
    int owner_index;
    int64_t bytes_returned;
    uint64_t cell_value;
    bool actual_success, consumed_success, substituted;
    ma_module_value snapshot;
} ac_select_call;
typedef struct {
    bool initialized;
    ac_select_phase phase;
    ac_select_status status;
    ac_select_error error;
    uintptr_t pc, lr, fp, nfp, ra;
    int chosen_index;
    unsigned frame_count, trace_count, probe_count, cell_count, snapshot_count;
    bool address_bound;
    ac_owner value;
    ac_select_call trace[AC_SELECT_CALLS];
} ac_selector;
typedef struct {
    ac_phase phase;
    ac_event_kind next_kind;
    unsigned current_event;
    ac_result working;
    ac_raw_oracle raw_oracle;
    ac_selector selector;
    bool adapter_error_returned;
    ac_select_error adapter_error;
} ac_private;
typedef struct { uintptr_t address; int index; } ac_diagnostic;
typedef struct { uintptr_t address; bool found; rc_owner_kind kind; } ac_core_query;
typedef struct {
    unsigned scratch_count, diagnostic_count, core_query_count;
    ac_selector scratch[AC_SCRATCH_RUNS];
    ac_diagnostic diagnostics[AC_DIAGNOSTICS];
    ac_core_query core_queries[AC_SCRATCH_RUNS];
    bool violation;
} ac_auxiliary;

/* Ordinary synchronous progression with stable authored request/table/code/model
 * storage through return. Context may append its bounded transcript and schedule
 * one authored nested loser. No private API, native provider or consumer is called. */
ac_request_result ac_run(const ac_request *request);
void ac_observe(ac_snapshot *out);
/* Address-only standalone evidence anchor; neither harness nor core invokes it. */
extern int (*const ac_generated_reader_evidence)(uintptr_t);

#ifdef AC_FIXTURE
/* Serial driver only; request remains live. Only a successful begin may advance.
 * Every selection step completes at most one retained reader or authored cell call. */
ac_request_result ac_fixture_begin(const ac_request *request);
bool ac_fixture_step(void);
void ac_fixture_inspect(ac_private *out);
void ac_fixture_auxiliary(ac_auxiliary *out);
/* Configure before admission. The sole injected outcome is consumed=false AFTER
 * an actual snapshot result has been retained; the reader itself remains exact. */
bool ac_fixture_snapshot_refusal(bool enable);
#endif
#ifdef AO_WITNESS_SETUP
/* All IDs are actual 33-byte canonical arrays, not arbitrary C string pointers.
 * Complete staging and selected commits before the first ordinary ac_run call.
 * Cells are authored integer keys. Request/model storage stays fixed through return.
 * Both commit APIs refuse once the attempt leaves Virgin. Serial setup only. */
bool ac_model_stage_module(unsigned index, const char id[AC_ID_BYTES],
                           uint64_t process_id, uintptr_t lo, uintptr_t hi);
bool ac_model_commit_module(unsigned index);
bool ac_model_stage_generated(unsigned binding, rc_owner_kind kind,
                              uint64_t process_id, const char id[AC_ID_BYTES],
                              rc_span island_instructions, rc_span trampoline_instructions);
bool ac_model_commit_generated(unsigned binding);
bool ac_model_add_cell(uintptr_t address, uint64_t value, int64_t returned_bytes);
#endif
#ifdef AC_FIXTURE
/* Separate diagnostic trace; never admits or advances an attempt. */
bool ac_model_diagnostic(uintptr_t address, int *out_index);
#endif
#endif
