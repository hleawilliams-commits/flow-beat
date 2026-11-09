/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DP_FIXTURE_H
#define DP_FIXTURE_H
#include "azmod-mod.h"
typedef struct { const char *id; uintptr_t target; void *replacement; void **original; } dp_fixture_input;
/* Fixture-only immutable host binding API; never part of az_core_v1. */
const dp_fixture_input *dp_fixture_input_get(unsigned index);
unsigned dp_fixture_mode(void);
void dp_constructor_marker(unsigned index);
void dp_callback_probe(unsigned index,unsigned phase);
int dp_foreign_probe(const az_core_v1 *core);
enum { DP_ORDINARY,DP_CONSTRUCTOR,DP_DEPENDENCY,DP_QUERY_FAULT,DP_DESCRIPTOR_BAD,DP_PARTIAL,DP_FOREIGN,DP_SYMBOLS };
int dso_claim_call(int (*fn)(const char*,uintptr_t,void*,void**),const char*,uintptr_t,void*,void**);
#endif
