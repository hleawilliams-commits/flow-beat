#ifndef AZ_ADMISSION_TRANSACTION_FIXTURE_H
#define AZ_ADMISSION_TRANSACTION_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OFFLINE C17 model. Only pthread locking and C atomic publication are real.
 * Addresses, loader provenance, caller RA, handles, allocation/protection/
 * cleanup results and replacement values are authored fixture inputs. They do
 * not authenticate SDK callers or prove mappings, original/replacement code
 * custody, signals, cancellation, unwind safety, or process-wide exclusion.
 *
 * Retain the system until all participating callers/readers are joined.
 * No API closes a real descriptor, loads code, patches memory, calls a supplied
 * function pointer, retries cleanup, or resets published state. Mutation APIs
 * use cm -> writer; Core and late transition use writer only, never writer -> cm.
 * The authored cm mutex is nonrecursive: participating API reentry is excluded,
 * unlike the SDK's recursive cm mutex. This is not its synchronization ABI.
 * No locks span user callbacks. TX_FIXTURE scheduling callbacks are the sole
 * exception: they are explicitly authored instrumentation, never production.
 */
#define TX_MODULES 16u
#define TX_SLOTS 8u
#define TX_ATTEMPTS 32u
#define TX_ID_MAX 32u
#define TX_NO_INDEX UINT32_MAX

typedef struct tx_system tx_system;

typedef enum {
    TX_OK = 0, TX_INVALID, TX_NOMEM, TX_SYNC, TX_NOT_READY,
    TX_ALREADY_STARTED, TX_ALREADY_RECORDED, TX_DUPLICATE_ID, TX_INIT_BUSY, TX_PROVENANCE,
    TX_DESCRIPTOR, TX_EPOCH_CLOSED, TX_EPOCH_EXHAUSTED, TX_CALLER,
    TX_TARGET_CONFLICT, TX_CAPACITY, TX_ATTEMPT_CAPACITY, TX_LATE,
    TX_LAYOUT, TX_STORAGE_CONFLICT, TX_MODULE_CONFLICT, TX_ALLOCATION,
    TX_SEAL, TX_ISLAND_UNLOCK, TX_PERMIT, TX_TARGET_UNLOCK
} tx_error;
typedef enum { TX_CORE = 1, TX_COMMUNITY = 2 } tx_owner_kind;
typedef struct {
    tx_owner_kind kind;
    uint64_t process_id;
    uint32_t module_index;
    char id[TX_ID_MAX + 1u];
} tx_owner;
typedef struct { uintptr_t begin, end; } tx_span;
typedef struct {
    tx_span island_instructions, island_blob;
    tx_span trampoline_instructions, trampoline_blob;
    unsigned ncopy, expanded_adrps;
} tx_layout;
typedef struct { int result, error; } tx_raw;

/* A successful provenance result may have base == 0. Endpoints are an already
 * collected min/max PT_LOAD envelope, including holes/non-executable ranges.
 * This module does not enumerate a real loader or validate ELF program headers. */
typedef struct {
    bool handle_succeeded, link_map_succeeded, intended_object_matched;
    uint64_t handle_cookie;
    uintptr_t base, lo, hi;
} tx_provenance;
typedef struct {
    const tx_system *system;
    uint64_t instance, epoch;
    uint32_t module_index;
} tx_epoch;

/* Independent receipts; successful relock means restored saved_bits, not RX. */
typedef struct {
    bool unlock_attempted, relock_attempted, final_bits_known;
    tx_raw unlock, relock;
    unsigned saved_bits, final_bits;
} tx_protection_receipt;
typedef struct {
    bool allocation_attempted, seal_attempted, cleanup_attempted;
    bool mapping_held, island_slot_burned;
    tx_raw allocation, seal, cleanup;
} tx_resource_receipt;

#ifdef TX_FIXTURE
typedef enum {
    TX_PERMIT_NORMAL, TX_PERMIT_WRONG_SYSTEM, TX_PERMIT_WRONG_SLOT,
    TX_PERMIT_WRONG_OWNER, TX_PERMIT_CONSUMED
} tx_permit_fault;
#endif

typedef struct {
    uintptr_t target, replacement, island, trampoline;
    unsigned ncopy, expanded_adrps;
    tx_raw allocation, seal, cleanup;
    tx_raw island_unlock, island_relock, target_unlock, target_relock;
    unsigned island_saved_bits, target_saved_bits;
#ifdef TX_FIXTURE
    tx_permit_fault permit_fault;
#endif
} tx_plan;

typedef struct {
    uint64_t attempt;
    tx_error result;
    tx_owner owner;
    uint64_t epoch;
    uint32_t slot;
    bool admitted, reserved, prepared, permit_validated, late_at_admission;
    bool metadata_published, original_published, redirect_published;
    bool original_saw_metadata, redirect_saw_original, nhooks_accounted;
    unsigned metadata_order, original_order, redirect_order;
    uintptr_t target, replacement, original_value, redirect_value;
    tx_layout layout;
    tx_resource_receipt resources;
    tx_protection_receipt island_protection, target_protection;
} tx_receipt;

typedef struct {
    bool recorded, published, descriptor_recorded, descriptor_accepted;
    bool inline_capability, init_open, init_finished;
    int init_result;
    uint64_t epoch, nhooks;
    tx_owner owner;
    tx_provenance provenance;
} tx_module_snapshot;
typedef struct {
    bool reserved, published;
    tx_owner owner;
    tx_span target;
    tx_layout layout;
    uintptr_t original_value, redirect_value;
} tx_slot_snapshot;
typedef struct {
    bool initialized, loader_started, late;
    uint64_t process_id, instance, next_epoch;
    bool current_init_open;
    uint32_t current_init_index;
    uint64_t current_init_epoch;
    size_t attempts, reserved_slots;
    tx_module_snapshot modules[TX_MODULES];
    tx_slot_snapshot slots[TX_SLOTS];
    tx_receipt receipts[TX_ATTEMPTS];
} tx_snapshot;
typedef enum { TX_NO_MATCH, TX_MODULE_MATCH, TX_GENERATED_MATCH } tx_match_source;
typedef struct {
    tx_match_source source;
    tx_owner owner;
    uintptr_t target;
} tx_match;

#ifdef TX_FIXTURE
typedef enum { TX_LOCK_CM = 1, TX_LOCK_WRITER = 2 } tx_lock_kind;
typedef enum {
    TX_TEST_BEFORE_ADMISSION, TX_TEST_ADMITTED, TX_TEST_RESERVED,
    TX_TEST_BEFORE_UNLOCK, TX_TEST_AFTER_CUT, TX_TEST_BEFORE_CLOSE,
    TX_TEST_CLOSED, TX_TEST_BEFORE_LATE, TX_TEST_LATE,
    TX_TEST_BEFORE_MODULE_PUBLISH, TX_TEST_LOCK_BUSY
} tx_test_point;
/* Receipt is a borrowed diagnostic value valid only during the callback.
 * Configure hook exactly once before concurrent calls; never mutate thereafter.
 * Callbacks may coordinate fixture gates or call tx_lookup (never a mutator or
 * tx_inspect while a lock is held). They must return normally. No hook is called
 * inside the metadata/original/redirect cut. trylock instrumentation reports a
 * real EBUSY then uses the same blocking mutex; it is not a production scheduler. */
typedef void (*tx_test_hook)(tx_test_point, tx_lock_kind, int,
                             const tx_receipt *, void *);
tx_error tx_set_test_hook(tx_system *, tx_test_hook, void *);
#endif

/* instance and process_id are nonzero synthetic identities; the caller must not
 * reuse an instance with outstanding epoch tokens. Returned object is retained
 * even on partial pthread initialization failure. Inspect/operations refuse an
 * incompletely initialized object. */
tx_error tx_create(uint64_t process_id, uint64_t instance, tx_system **out);
/* Explicit external quiescence only. On pthread destruction error retain the
 * object/remaining mutexes; permanently refuse operational APIs after start.
 * No automatic destruction or join; no process-death custody claim. */
tx_error tx_destroy(tx_system **slot);

/* One-start loader guard, acquired/released before external loader callbacks.
 * These APIs only record supplied results; no callback is invoked. Each module
 * index is recorded once, including failed provenance. IDs are unique among
 * recorded modules; only one current init epoch may be open at a time. A valid envelope becomes
 * immutable/published before descriptor/init outcomes and remains on failure. */
tx_error tx_loader_begin(tx_system *);
tx_error tx_record_module(tx_system *, uint32_t index, const char *id,
                          size_t id_length, tx_provenance);
tx_error tx_record_descriptor(tx_system *, uint32_t index,
                              bool accepted, bool inline_capability);
tx_error tx_open_init(tx_system *, uint32_t index, tx_epoch *);
tx_error tx_close_init(tx_system *, const tx_epoch *, int init_result);

/* caller_ra is captured at the modeled outer boundary, supplied as a value.
 * Install copies plan/epoch into local owned values before scheduler hooks or
 * admission locks. Caller input storage must be alive and immutable during
 * that initial copy; the transaction never rereads it after copying.
 * No pre-lock admission decision is trusted. The epoch, owner, capability and
 * caller envelope are revalidated with both locks held, and nhooks increments
 * before either lock is released. Tokens do not prove a real SDK identity. */
tx_error tx_install_community(tx_system *, const tx_epoch *, uintptr_t caller_ra,
                              const tx_plan *, tx_receipt *);
tx_error tx_install_core(tx_system *, const tx_plan *, tx_receipt *);
tx_error tx_go_late(tx_system *);

/* Reservations are permanent for this bounded model, even on failure.
 * Failed allocation/seal/protection and cleanup outcomes remain in receipts.
 * A private permit binds system/slot/attempt/owner to this lexical transaction.
 * All validation completes BEFORE modeled target unlock. Success performs the
 * infallible metadata release -> internal original -> internal redirect stores
 * immediately, with no new lock, callback, allocation, check or refusal branch.
 * Subsequent protection warnings retain publication and successful nhooks.
 * These internal atomic integer slots are not arbitrary caller void** storage.
 *
 * Lookup never locks/waits/writes: one acquire flag per slot, then immutable
 * payload, in original module-index order before generated instructions.
 * Later module publications may mask prior generated code or literal addresses.
 * tx_inspect is blocking diagnostic copying under cm -> writer; it is not a
 * signal/crash-reader API and does not establish an atomic process snapshot. */
tx_match tx_lookup(const tx_system *, uintptr_t address);
tx_error tx_inspect(tx_system *, tx_snapshot *);

#ifdef __cplusplus
}
#endif
#endif
