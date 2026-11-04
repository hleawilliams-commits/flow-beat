/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef USB_TRANSFER_BRIDGE_H
#define USB_TRANSFER_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Owned fixture values only. Assembly offsets are asserted in test_transfer.cpp. */
struct transfer_observation {
    uint64_t caller_sp, caller_lr, caller_x16, caller_x17;
    uint64_t body_x16, body_x17, body_entry_sp, body_lr;
    uint64_t body_x0, body_x1, body_x2, body_frame_sp, body_frame_fp;
};
struct transfer_claim_record {
    uint64_t requested, target, hook, output_slot, original, island;
    int contract_ok, internal_result, path, late;
    int target_prot_before, target_prot_after, island_prot, trampoline_prot;
    uint8_t before[16], after[16], island_bytes[16], trampoline_bytes[20];
    int bytes_available;
};
struct transfer_bridge_report {
    unsigned prepared, claims, contract_errors, diagnostics;
    struct transfer_claim_record claim[2];
};

/* No allocation/protection mocks or replay. Prepare consumes once, selects late. */
int transfer_prepare(void);
int transfer_claim(const char*, uintptr_t, void*, void**);
void transfer_report(struct transfer_bridge_report*);

uint64_t fixture_action_target(struct transfer_observation*, uint64_t, uint64_t);
uint64_t fixture_service_target(struct transfer_observation*, uint64_t, uint64_t);
uint64_t fixture_call_action(struct transfer_observation*, uint64_t, uint64_t);
uint64_t fixture_call_service(struct transfer_observation*, uint64_t, uint64_t);
uint64_t transfer_body(struct transfer_observation*, uint64_t, uint64_t, uint64_t);

#ifdef __cplusplus
}
#endif
#endif
