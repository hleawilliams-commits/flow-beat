/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-mod.h"
int uv3_init(const az_core_v1 *core);
void uv3_tick(void);
static const char *const tags[] = {"ep147-9e9454e357389ece",0};
static const az_mod_v1 mod = {
    .abi=AZ_MOD_ABI,.size=sizeof(az_mod_v1),.id="usb-observer-v3",
    .fw_tags=tags,.init=uv3_init,.tick=uv3_tick
};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
