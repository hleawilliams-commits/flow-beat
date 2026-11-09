/* SPDX-License-Identifier: GPL-3.0-only */
#include "dp_fixture.h"
extern int dp_dep_force(void);
__attribute__((visibility("default"))) unsigned dp_root_marker;
static void __attribute__((constructor)) dp_root_constructor(void) {
    dp_root_marker=(unsigned)dp_dep_force();dp_constructor_marker(2);
}
