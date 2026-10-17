/* SPDX-License-Identifier: GPL-3.0-only */
#include <unistd.h>
#include "azmod-mod.h"
#define ID "camelot-preview-helper"
static const az_core_v1 *C;
static void *apply(void *arg) {
    (void)arg;
    for (int i=0; i<90; ++i) {
        sleep(2);
        if (access("/media/usb/sda1/PIONEER/azspool/camelot-preview.sql",F_OK)) continue;
        int rc=C->lib_spool_kick(ID,0);
        if (rc==0) {
            C->log(ID,"Requested prepared artwork spool; inspect azmod-libspool.log for result");
            sleep(3);
            if (access("/media/usb/sda1/PIONEER/azspool/camelot-preview.sql",F_OK)) return 0;
        }
    }
    C->log(ID,"Preview helper timed out; no further requests");
    return 0;
}
static int init(const az_core_v1 *core) {
    if (!core || core->abi!=AZ_MOD_ABI || !AZ_HAS(core,lib_spool_kick) || !AZ_HAS(core,thread_start)) return 1;
    C=core;
    int rc=C->thread_start(ID,"art-preview",apply,0);
    C->log(ID,rc ? "Could not start preview helper" : "Preview worker started");
    return rc;
}
static const char *const FW[]={"any",0};
static const az_mod_v1 MOD={.abi=AZ_MOD_ABI,.size=sizeof(az_mod_v1),.id=ID,.fw_tags=FW,.init=init};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &MOD; }
