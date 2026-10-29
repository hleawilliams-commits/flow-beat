/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_GUEST_DISPATCH_H
#define EMULATOR_GUEST_DISPATCH_H
#include "wire.h"
struct mbw_dispatch;
enum mbw_dispatch_result {
    MBWD_OK=0, MBWD_INVALID=-1, MBWD_NOMEM=-2, MBWD_BROKER=-3,
    MBWD_CLOSED=-4, MBWD_CONFLICT=-5, MBWD_CAPACITY=-6, MBWD_RECEIPT=-7
};
enum mbw_phase { MBWD_RETAINED=0, MBWD_ADOPT_ARMED=1, MBWD_READY=2,
                 MBWD_FRAME_ARMED=3, MBWD_TERMINAL=4 };
struct mbw_diagnostic {
    enum mbw_phase phase;
    int first_refusal;
    uint8_t session[16];
    struct mb_state seed, expected;
    uint64_t producer_id, next_request_id, last_exchange;
    struct mb_grant grant, returned_grant;
    int returned_valid;
    int32_t broker_result;
    struct mb_receipt returned_receipt;
    size_t request_length, response_length;
    uint8_t request[MBW_MAX_REQUEST_SIZE], response[MBW_MAX_RECORD_SIZE];
};
/* Borrows broker; never adopts during create and never destroys/closes/replaces
 * broker/backend. Caller must serialize ALL dispatch/inspect/destroy and aliases,
 * retain the supplied broker, and exclude callback reentry. Session is correlation
 * only. Seed correspondence is mb_inspect's local state, not native authority. */
int mbw_dispatch_create(struct mb_broker *, const uint8_t session[16],
                        const struct mb_state *, uint64_t producer_id, struct mbw_dispatch **);
void mbw_dispatch_destroy(struct mbw_dispatch *);
/* Accept exactly one record. A refusal closes new dispatch but preserves prior
 * journal/cache. Capacity failure occurs before broker calls. Exact last request
 * can retrieve cached evidence even after closure, with provenance1; never retry.
 * All objects/buffers must be disjoint; response_length zero means no response.
 * Caller must not treat MBWD_OK as broker/native success: decode the response. */
int mbw_dispatch_exchange(struct mbw_dispatch *, const uint8_t *, size_t,
                          uint8_t *, size_t, size_t *response_length);
/* Bounded copied diagnostics; no broker call or I/O. Requires serialization. */
int mbw_dispatch_inspect(const struct mbw_dispatch *, struct mbw_diagnostic *);
#endif
