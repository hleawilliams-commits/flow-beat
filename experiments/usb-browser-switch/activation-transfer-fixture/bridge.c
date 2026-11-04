/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE 1
#include "bridge.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static struct transfer_bridge_report report;
static void transfer_debug(const char* format, ...) {
    ++report.diagnostics;
    va_list ap;
    va_start(ap, format);
    fputs("SDK_DIAGNOSTIC ", stderr);
    vfprintf(stderr, format, ap);
    va_end(ap);
}
#define dbg(...) transfer_debug(__VA_ARGS__)
/* Exact git-show source and its exact relative ../common/azprot.h. The runner
 * hashes both. Real libc mapping/protection, /proc/self/maps and cache flush. */
#include "sdk/src/azmod/inline_hooks.c"
#undef dbg

extern void az_usb_action_entry(void);
extern void az_usb_service_entry(void);
extern void* az_usb_action_original;
extern void* az_usb_service_original;

int transfer_prepare(void) {
    if (report.prepared) return -1;
    report.prepared = 1;
    /* Mark otherwise unused audited SDK entry points as referenced, not run. */
    (void)&azm_hooks_dump;
    (void)&azm_patch_vtable;
    azm_hooks_go_late();
    return __atomic_load_n(&g_azm_late, __ATOMIC_ACQUIRE) == 1 ? 0 : -1;
}
static int protection(uintptr_t p, size_t bytes) {
    if (!p || p + bytes < p) return -1;
    return azprot_query(p, p + bytes);
}
static int retained_island(uintptr_t p) {
    for (int k = 0; k < 4; ++k) {
        uintptr_t base = (uintptr_t)g_azm_island[k].base;
        if (base && p >= base && p - base < (uintptr_t)g_azm_island[k].used * 16 &&
            (p - base) % 16 == 0) return 1;
    }
    return 0;
}
static void copy_generated(struct transfer_claim_record* r) {
    r->target_prot_after = protection((uintptr_t)r->target, 16);
    if ((r->target_prot_after & PROT_READ) == 0 || r->target_prot_after < 0) return;
    memcpy(r->after, (const void*)(uintptr_t)r->target, 16);
    uint32_t branch;
    memcpy(&branch, r->after, 4);
    if ((branch & 0xfc000000u) != 0x14000000u) return;
    int64_t displacement = (int64_t)(branch & 0x03ffffffu);
    if (displacement & (INT64_C(1) << 25)) displacement -= INT64_C(1) << 26;
    r->island = r->target + displacement * 4;
    if (!retained_island((uintptr_t)r->island)) return;
    r->island_prot = protection((uintptr_t)r->island, 16);
    r->trampoline_prot = protection((uintptr_t)r->original, 20);
    if (r->island_prot < 0 || r->trampoline_prot < 0 ||
        !(r->island_prot & PROT_READ) || !(r->trampoline_prot & PROT_READ)) return;
    memcpy(r->island_bytes, (const void*)(uintptr_t)r->island, 16);
    memcpy(r->trampoline_bytes, (const void*)(uintptr_t)r->original, 20);
    r->bytes_available = 1;
}
int transfer_claim(const char* id, uintptr_t requested, void* hook, void** orig) {
    const unsigned index = report.claims++;
    if (index >= 2) { ++report.contract_errors; return -1; }
    struct transfer_claim_record* r = &report.claim[index];
    r->requested = requested;
    r->target = index == 0 ? (uintptr_t)&fixture_action_target : (uintptr_t)&fixture_service_target;
    r->hook = (uintptr_t)hook;
    r->output_slot = (uintptr_t)orig;
    const uintptr_t wanted = index == 0 ? UINT64_C(0x1be7e38) : UINT64_C(0x20a1550);
    void* const replacement = index == 0 ? (void*)&az_usb_action_entry : (void*)&az_usb_service_entry;
    void** const slot = index == 0 ? &az_usb_action_original : &az_usb_service_original;
    r->contract_ok = report.prepared == 1 && id && strcmp(id, "usb-entry-binding") == 0 &&
        requested == wanted && hook == replacement && orig == slot &&
        ((uintptr_t)orig & 7) == 0 && __atomic_load_n(slot, __ATOMIC_ACQUIRE) == NULL;
    if (!r->contract_ok) { ++report.contract_errors; return -1; }
    r->target_prot_before = protection((uintptr_t)r->target, 16);
    if (r->target_prot_before < 0 || !(r->target_prot_before & PROT_READ)) return -1;
    memcpy(r->before, (const void*)(uintptr_t)r->target, 16);
    const int before_record = __atomic_load_n(&g_azm_rec_n, __ATOMIC_ACQUIRE);
    g_hook_owner = id; /* Stable actual binder ModuleId; process-lifetime storage. */
    r->internal_result = azm_install_hook_at((uintptr_t)r->target, hook, orig, "fixture-owned-target");
    r->original = (uintptr_t)__atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (before_record >= 0 && before_record < AZM_REC_MAX &&
        __atomic_load_n(&g_azm_rec_n, __ATOMIC_ACQUIRE) == before_record + 1) {
        r->path = __atomic_load_n(&g_azm_rec[before_record].path, __ATOMIC_ACQUIRE);
        r->late = g_azm_rec[before_record].late;
    }
    if (r->internal_result == 1) copy_generated(r);
    /* Sole provider seam: selected constant -> fixture text, internal1 -> SDK0.
     * It is not community.c module/ownership/firmware authorization. */
    return r->internal_result == 1 && r->path == AZM_ISLAND && r->late == 1 ? 0 : -1;
}
void transfer_report(struct transfer_bridge_report* out) {
    if (out) *out = report; /* No new syscall, native inspection or resource action. */
}
