/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MODULE_ATTRIBUTION_H
#define MODULE_ATTRIBUTION_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include "receipt-types.h"
#define MA_MODULES 4u
#define MA_CELLS 64u
#define MA_PEEKS 64u
#define MA_IO_CALLS 16u
/* Authored fixed root handoff. The loader establishes these values outside locks.
 * Ordinary bounds and fault bounds are equal except the explicit EXEC_ONLY control. */
typedef struct {
    unsigned index;
    const char *id;
    uint64_t process_id;
    uintptr_t lo, hi, fault_lo, fault_hi;
    bool hooks, firmware_any, held, raw_valid, consumed_success, dry;
} ma_root_input;
typedef struct {
    bool committed;
    unsigned index;
    uint64_t process_id;
    uintptr_t lo, hi;
    char id[33];
} ma_module_value;
typedef struct { uintptr_t address; bool success; } ma_peek_event;
typedef struct { int kind, fd; size_t length; ssize_t result; } ma_io_event;
typedef struct {
    bool violation;
    unsigned peek_count, io_count, unblamed;
    ma_peek_event peeks[MA_PEEKS];
    ma_io_event io[MA_IO_CALLS];
    int open_flags;
    unsigned open_mode;
    bool path_matches;
    size_t write_requested, payload_length;
    ssize_t write_returned;
    unsigned char payload[33];
} ma_observations;
/* Snapshot and lookup use independent acquire loads; no coherent global view. */
int ma_owner_index(uintptr_t address);
bool ma_module_snapshot(unsigned index, ma_module_value *out);
const char *ma_consume_fault(uintptr_t pc, uintptr_t lr, uintptr_t fp);
/* Explicit integer-cell/file providers, never native reads or actual I/O.
 * No clear/reset operation exists; selectors use fresh processes. */
bool ma_model_add_frame(uintptr_t fp, uintptr_t next, uintptr_t ra);
void ma_model_io_config(int open_result, int write_mode, int sync_result, int close_result);
const ma_observations *ma_model_observations(void);
#ifdef MA_MODEL
/* Synthetic policy path only. Duplicate identities/overlapping envelopes are
 * deliberate model values; ordinary authored-root publication remains strict. */
bool ma_model_stage(unsigned index, const char *id, uint64_t process_id,
                    uintptr_t lo, uintptr_t hi);
bool ma_model_commit(unsigned index);
bool ma_model_record(unsigned binding, const rc_metadata *metadata);
bool ma_model_hold_locks(void);
bool ma_model_release_locks(void);
#endif
#endif
