/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-mod.h"
int uv4_init(const az_core_v1 *core);
void uv4_tick(void);
static const char *const tags[] = {"ep147-9e9454e357389ece",0};
static const az_mod_v1 mod = {
    .abi=AZ_MOD_ABI,.size=sizeof(az_mod_v1),.id="usb-observer-v4",
    .fw_tags=tags,.init=uv4_init,.tick=uv4_tick
};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
