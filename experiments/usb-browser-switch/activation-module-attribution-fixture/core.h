/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MODULE_ATTRIBUTION_CORE_H
#define MODULE_ATTRIBUTION_CORE_H
#include "receipt-types.h"
#include "dp_fixture.h"
#include "attribution.h"

/* Offline fixed authored Linux DSO controller. The four fixed host targets,
 * replacements and strong plain originals retain process lifetime. The loader
 * owns successful handles; this core accepts copied verified envelopes only.
 * No reset, unload, retry, quiescence or live admission contract is supplied. */
#define CC_MODULES 4u
#define CC_ID_BYTES 33u
#define DP_CLAIM_OBSERVATIONS 32u
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
    int sdk_result;
    unsigned captured_session, resolved_session, module_index;
    unsigned nhooks_before, nhooks_after;
    bool installer_called, accounted, incomplete;
    rc_attempt receipt;
} cc_claim_result;
typedef const az_mod_v1 *(*dp_entry_fn)(void);
/* Return 1 only after complete descriptor, ID, bounded tag array/strings and
 * exact init function custody have been checked without unchecked reads.
 * This callback and actual entry/init run outside cm and writer. */
typedef int (*dp_descriptor_validator)(unsigned, const az_mod_v1 *, void *);
typedef struct {
    uintptr_t raw_ra, used_ra, id_pointer, target, replacement, original;
    unsigned captured_session;
    int returned_sdk_result;
    cc_claim_result claim;
} dp_claim_observation;

cc_status dp_core_initialize(uintptr_t text_lo, uintptr_t text_hi);
cc_status dp_core_reserve(unsigned count);
cc_status ma_root_register(const ma_root_input *input);
cc_status dp_core_run(unsigned index, dp_entry_fn entry,
    dp_descriptor_validator validator, void *context, cc_module_snapshot *out);
bool cc_module_get(unsigned index, cc_module_snapshot *out);
unsigned dp_active_session(void);
unsigned dp_claim_count(void);
bool dp_claim_get(unsigned sequence, dp_claim_observation *out);
unsigned dp_claim_dropped(void);
/* Exact SDK claim signature; ordinary builtin RA is captured at this outer
 * noinline function once. The sole RA control keeps raw and used values apart. */
int cc_claim_hook(const char *id, uintptr_t target, void *replacement, void **original);
rc_status cc_install_core(unsigned binding, const rc_request *request, rc_attempt *out);
bool rc_binding_get(unsigned binding, rc_binding *out);
bool rc_snapshot(unsigned binding, rc_attempt *out);
bool rc_lookup(uintptr_t pc, rc_metadata *out);
int rc_query_protection(uintptr_t address, size_t length);
bool rc_checked_pages(uintptr_t address, size_t length, long page_size,
    uintptr_t *base, size_t *rounded_length);
/* The loader's bounded diagnostic hook is never called within the installer
 * cut or while cm/writer is held. These labels do not prove actual ordering by
 * themselves; raw DSO markers and independently read snapshots are retained. */
void dp_lifecycle_event(unsigned index, unsigned event);
#endif
