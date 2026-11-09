/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RECEIPT_H
#define RECEIPT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Offline authored-target fixture only. Four fixed process-image descriptors,
 * not arbitrary SDK/loader authority. All slots, code, receipts and mappings
 * retain published/held/uncertain custody until process exit; successful private
 * prepublication cleanup releases only its own mapping. No reset, retry,
 * unpatch or destruction API.
 * Normal nonrecursive pthread cm -> writer -> protection ordering; not signal
 * safe and not an exact SDK mutex ABI. No concurrent external code/slot writers.
 * Diagnostic pointers are usable capabilities: no alias isolation is enforced. */
#define RC_BINDINGS 4u
#define RC_MAPS 7u
#define RC_READS 64u
#define RC_ID_BYTES 32u

typedef enum { RC_OWNER_NONE, RC_OWNER_CORE, RC_OWNER_COMMUNITY } rc_owner_kind;
typedef enum {
    RC_OK, RC_WARNINGS, RC_INVALID, RC_NOT_READY, RC_ALREADY, RC_EPOCH,
    RC_CONFLICT, RC_MAPPING, RC_SEAL, RC_ISLAND_UNLOCK, RC_TARGET_UNLOCK,
    RC_NO_ISLAND, RC_STORAGE, RC_POLICY, RC_PERMIT, RC_LOCK, RC_CAPACITY
} rc_status;
typedef struct {
    bool attempted, returned, underlying_called, injected, errno_valid;
    intptr_t result;
    int error;
} rc_raw;
typedef struct { uintptr_t begin, end; } rc_span;
typedef struct {
    rc_owner_kind kind;
    uint64_t process_id;
    char id[RC_ID_BYTES];
} rc_owner;
typedef struct {
    uint64_t process_id;
    char id[RC_ID_BYTES];
    uintptr_t lo, hi; /* Authored immutable envelope, not discovered ELF proof. */
} rc_module;
typedef struct { uint64_t generation; } rc_epoch;
typedef struct {
    uintptr_t target;
    void *replacement;
    void **original;
} rc_binding;
typedef enum { RC_MAP_ISLAND, RC_MAP_TRAMPOLINE } rc_mapping_kind;
typedef struct {
    rc_mapping_kind kind;
    rc_raw allocation, cleanup;
    uintptr_t hint, address;
    size_t length;
    int protection;
    bool held, accepted_pool, placement_rejected, cleanup_uncertain;
} rc_mapping;
typedef struct {
    rc_raw open, reads[RC_READS], close;
    unsigned read_count;
    bool read_capacity_refused, returned;
    int parser_result;
} rc_query;
typedef struct {
    rc_raw unlock, page_size, mutex_lock, make_writable, relock, mutex_release;
    rc_query query;
    bool token_valid;
    uintptr_t base;
    size_t length;
    int saved_bits;
} rc_protection;
typedef struct {
    rc_owner owner;
    unsigned binding, ncopy, out;
    uintptr_t target, replacement, original_slot, island, trampoline, continuation;
    rc_span target_exclusion, island_instructions, island_storage;
    rc_span trampoline_instructions, trampoline_storage;
    uint32_t source_words[4];
} rc_metadata;
typedef struct {
    bool admitted, installer_entered, trampoline_prewrite_validated, prepared, permit_validated;
    bool metadata_published, original_published, redirect_published;
    bool original_saw_metadata, redirect_saw_original;
    bool island_slot_burned, nhooks_accounted;
    unsigned metadata_order, original_order, redirect_order;
    unsigned mapping_count, pool_index, pool_slot;
    uintptr_t pool_base;
    size_t pool_length;
    rc_status status;
    int installer_result;
    rc_metadata emission;
    rc_mapping mappings[RC_MAPS];
    rc_raw initial_page_size, seal;
    rc_protection island_protection, target_protection;
    bool trampoline_cache_armed, trampoline_cache_returned;
    bool island_cache_armed, island_cache_returned;
    bool target_cache_armed, target_cache_returned;
    unsigned diagnostic_before, diagnostic_after, diagnostic_lost;
} rc_attempt;
typedef struct {
    void *replacement;
    void **original;
#ifdef RC_FIXTURE
    uint64_t faults;
#endif
} rc_request;

/* The exact objects consumed by the actual original store. Descriptor3
 * deliberately aliases target0, with a distinct replacement and original. */
extern void *rc_originals[RC_BINDINGS];
long rc_target0(long);
long rc_target1(long);
long rc_target2(long);
long rc_target3(long);
long rc_replacement0(long);
long rc_replacement1(long);
long rc_replacement2(long);
long rc_replacement3(long);

rc_status rc_initialize(void);
bool rc_binding_get(unsigned binding, rc_binding *out);
rc_status rc_open_epoch(const rc_module *module, rc_epoch *out);
rc_status rc_close_epoch(const rc_epoch *epoch, uint64_t *nhooks);
rc_status rc_install_core(unsigned binding, const rc_request *request, rc_attempt *out);
rc_status rc_install_community(const rc_epoch *epoch, unsigned binding,
                               const rc_request *request, rc_attempt *out);
/* Copies mutable receipts under writer (may block). Never call from a checkpoint.
 * lookup copies only release-published immutable metadata, without a mutex.
 * Module-first blame/stack walking is deliberately not provided by this slice. */
bool rc_snapshot(unsigned binding, rc_attempt *out);
bool rc_lookup(uintptr_t pc, rc_metadata *out);
/* Diagnostic actual maps query, externally serialized through writer; no
 * mutation of receipt. Returns -1 on unreadable/nonuniform/invalid range. */
int rc_query_protection(uintptr_t address, size_t length);
/* Value-only checked arithmetic; does not dereference address or call sysconf. */
bool rc_checked_pages(uintptr_t address, size_t length, long page_size,
                      uintptr_t *base, size_t *rounded_length);

#ifdef RC_FIXTURE
enum {
    RC_F_TRAMPOLINE_MAP = UINT64_C(1) << 0,
    RC_F_SEAL = UINT64_C(1) << 1,
    RC_F_ISLAND_UNLOCK = UINT64_C(1) << 2,
    RC_F_TARGET_UNLOCK = UINT64_C(1) << 3,
    RC_F_ISLAND_RELOCK = UINT64_C(1) << 4,
    RC_F_TARGET_RELOCK = UINT64_C(1) << 5,
    RC_F_CLEANUP = UINT64_C(1) << 6,
    RC_F_REJECT_CANDIDATE = UINT64_C(1) << 7,
    RC_F_ISLAND_MAP = UINT64_C(1) << 8
};
typedef enum {
    RC_TEST_BEFORE_ADMISSION, RC_TEST_ADMITTED, RC_TEST_BEFORE_UNLOCK,
    RC_TEST_AFTER_CUT, RC_TEST_CM_BUSY, RC_TEST_WRITER_BUSY
} rc_test_point;
/* Authored synchronous scheduler seam, outside publication cut. The pointer
 * is borrowed for this call only; do not reenter receipt APIs or mutate inputs.
 * All test threads must be released/joined before assertions/process exit. */
void rc_fixture_checkpoint(rc_test_point point, unsigned binding, const rc_attempt *attempt);
/* Diagnostic ring setup only, before installs and under writer. */
void rc_fixture_saturate_diagnostics(void);
#endif
#endif
