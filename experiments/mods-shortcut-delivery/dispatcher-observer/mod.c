/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-mod.h"
int dispatch_observer_init(const az_core_v1 *core);
void dispatch_observer_tick(void);
static const char *const tags[] = {"ep147-9e9454e357389ece",0};
static const az_mod_v1 mod = {
    .abi=AZ_MOD_ABI,.size=sizeof(az_mod_v1),.id="encoder-dispatch-observer",
    .fw_tags=tags,.init=dispatch_observer_init,.tick=dispatch_observer_tick
};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
