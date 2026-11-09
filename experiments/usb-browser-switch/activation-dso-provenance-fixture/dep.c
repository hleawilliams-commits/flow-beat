/* SPDX-License-Identifier: GPL-3.0-only */
#include "dp_fixture.h"
#define EX __attribute__((visibility("default")))
EX unsigned dp_dep_constructor_count;
EX unsigned dp_dep_invoked;
static void __attribute__((constructor)) dp_dep_constructor(void) {
    ++dp_dep_constructor_count;dp_constructor_marker(3);
}
EX int dp_dep_force(void) { return 1; }
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { ++dp_dep_invoked;return 0; }
