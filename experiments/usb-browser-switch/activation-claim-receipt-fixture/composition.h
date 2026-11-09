/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CLAIM_RECEIPT_COMPOSITION_H
#define CLAIM_RECEIPT_COMPOSITION_H
#include "receipt-types.h"

/* Separately versioned OFFLINE Linux AArch64 authored-target composition.
 * No real SDK loader/capability/native-RA authorization. Fixed code, plain
 * original slots, sessions and receipts retain process lifetime. No reset,
 * unpatch, dlclose, retry, shutdown or signal-safety guarantee.
 *
 * One recursive SDK cm -> common nonrecursive writer -> protection mutex.
 * No caller may enter an API while holding writer/protection. Recursive cm is
 * tested only through the explicit fixture seam, not arbitrary reentrancy.
 * ID strings/input structs must remain readable and unmodified through copying. All fixed
 * original/replacement/target pointer identities are checked before original
 * dereference. Wrong identity is not an arbitrary-pointer probe.
 * Snapshot APIs may block. Receipt lookup reads only immutable publication.
 */
#define CC_MODULES 4u
#define CC_LOADS 4u
#define CC_ID_BYTES 33u
typedef enum {
    CC_OK, CC_INVALID, CC_ALREADY, CC_BUSY, CC_NOT_READY, CC_LOCK,
    CC_RANGE_REFUSED, CC_DESCRIPTOR_REFUSED, CC_INIT_FAILED,
    CC_UNKNOWN_ID, CC_CALLER_REFUSED, CC_INIT_REFUSED, CC_HOOKS_REFUSED,
    CC_TARGET_REFUSED, CC_SESSION_REFUSED, CC_CUSTODY_REFUSED,
    CC_INSTALLER_REFUSED, CC_INCOMPLETE
} cc_status;
typedef enum {
    CC_REGISTERED, CC_RANGE_FAILED, CC_DESCRIPTOR_FAILED,
    CC_INITIALIZING, CC_LOADED, CC_FAILED
} cc_phase;
typedef struct {
    bool load;                 /* authored PT_LOAD selection */
    uintptr_t virtual_address;
    size_t memory_size;
} cc_load;
typedef struct {
    char id[CC_ID_BYTES];
    uint64_t process_id;
    bool handle_present, link_query_succeeded, link_map_present;
    bool object_matched, base_valid;
    uintptr_t base;
    unsigned load_count;
    cc_load loads[CC_LOADS];
    bool hooks, firmware_any;  /* authored manifest values */
} cc_loader_input;
typedef int (*cc_init_fn)(unsigned index, void *context);
typedef struct {
    const char *id;            /* readable/unmodified through cc_run_module's post-entry copy */
    bool abi_ok, firmware_ok;
    cc_init_fn init;
} cc_descriptor;
typedef cc_descriptor (*cc_entry_fn)(unsigned index, void *context);
typedef struct {
    bool present, range_published, handle_retained, session_used, session_open;
    bool entry_attempted, entry_returned, descriptor_accepted;
    bool init_attempted, init_returned, callbacks_retained;
    bool hooks_ok, firmware_any;
    char id[CC_ID_BYTES];
    uint64_t process_id;
    uintptr_t lo, hi;
    unsigned index, nhooks;
    int init_result;
    cc_phase phase;
    cc_status load_status;
} cc_module_snapshot;
typedef struct {
    cc_status status;
    int sdk_result;            /* only 0 or -1; not raw installer or rc_status */
    unsigned captured_session, resolved_session, module_index;
    unsigned nhooks_before, nhooks_after;
    bool installer_called, accounted, incomplete;
    rc_attempt receipt;        /* actual attempt copy; binding slot retains custody */
} cc_claim_result;

/* Each process invokes initialize and loader_begin at most once. Text range is
 * an authored executable-envelope value; this does not enumerate an ELF.
 * loader_begin copies all records; a refused range leaves an indexed hole.
 * It returns CC_OK when records were retained, even if an individual range was
 * refused (see module snapshot). Entry/init callbacks occur only in run_module,
 * outside both locks. One active runner; each entry/init path runs at most once and may
 * never reopen. Distinct indices may run sequentially. */
cc_status cc_initialize(uintptr_t text_lo, uintptr_t text_hi);
cc_status cc_loader_begin(const cc_loader_input *inputs, unsigned count);
cc_status cc_run_module(unsigned index, cc_entry_fn entry, void *context,
                        cc_module_snapshot *out);
bool cc_module_get(unsigned index, cc_module_snapshot *out);
unsigned cc_capture_session(void);

/* Exact SDK signature. The actual builtin RA is captured once at this noinline
 * entry, before helpers; the ordinary build is compile-only in this checkpoint.
 * This captures provenance bits, not authentication or a native lifetime lease. */
int cc_claim_hook(const char *id, uintptr_t target, void *replacement, void **original);
rc_status cc_install_core(unsigned binding, const rc_request *request, rc_attempt *out);

/* Unchanged receipt-facing diagnostics and fixed bindings. No child/raw mutable
 * installer exposure. Read returned copies only after the API returns. */
bool rc_binding_get(unsigned binding, rc_binding *out);
bool rc_snapshot(unsigned binding, rc_attempt *out);
bool rc_lookup(uintptr_t pc, rc_metadata *out);
int rc_query_protection(uintptr_t address, size_t length);
bool rc_checked_pages(uintptr_t address, size_t length, long page_size,
                      uintptr_t *base, size_t *rounded_length);

#ifdef CC_FIXTURE
typedef enum {
    CC_TEST_BEFORE_ADMISSION, CC_TEST_AFTER_REDIRECT_BEFORE_ACCOUNT,
    CC_TEST_CM_BUSY, CC_TEST_WRITER_BUSY
} cc_test_point;
/* Authored scheduler only. No mutation/reentry while locks held. The borrowed
 * result/receipt is live only during the call. No hook is inside emission cut. */
void cc_fixture_checkpoint(cc_test_point point, unsigned index,
                           const cc_claim_result *result, const rc_attempt *attempt);
int cc_claim_authored(const char *id, uintptr_t target, void *replacement,
                     void **original, uintptr_t caller_ra, unsigned captured_session,
                     uint64_t faults, cc_claim_result *out);
/* Narrow recursive-cm check: actual claim worker invoked with cm already held,
 * with no writer held. This is not a generic lock callback or raw child getter. */
int cc_claim_authored_recursive(const char *id, uintptr_t target, void *replacement,
                               void **original, uintptr_t caller_ra,
                               unsigned captured_session, uint64_t faults,
                               cc_claim_result *out);
#endif
#endif
