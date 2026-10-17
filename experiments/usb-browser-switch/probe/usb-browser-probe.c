/* SPDX-License-Identifier: GPL-3.0-only */
#include "usb-backend.h"
#define MOD_ID "usb-buttons"
static int init(const az_core_v1 *core) { return usp_backend_init(core, MOD_ID); }
static const char *const fw_tags[] = {USP_FW_TAG, 0};
static const az_mod_v1 mod = {
    .abi = AZ_MOD_ABI, .size = sizeof(az_mod_v1), .id = MOD_ID,
    .fw_tags = fw_tags, .init = init, .tick = usp_backend_tick
};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
