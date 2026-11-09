/* SPDX-License-Identifier: GPL-3.0-only */
#include "dp_fixture.h"
#ifndef DP_INDEX
#error DP_INDEX is required
#endif
#define EX __attribute__((visibility("default")))
#if DP_INDEX == 0
#define dp_mod_id dp_a_mod_id
#define dp_mod_tag dp_a_mod_tag
#define dp_mod_tags dp_a_mod_tags
#define dp_mod_descriptor dp_a_mod_descriptor
#define dp_bad_descriptor dp_a_bad_descriptor
#define dp_constructor_count dp_a_constructor_count
#else
#define dp_mod_id dp_b_mod_id
#define dp_mod_tag dp_b_mod_tag
#define dp_mod_tags dp_b_mod_tags
#define dp_mod_descriptor dp_b_mod_descriptor
#define dp_bad_descriptor dp_b_bad_descriptor
#define dp_constructor_count dp_b_constructor_count
#endif
#if DP_INDEX == 0
EX const char dp_mod_id[]="mod-a";
#else
EX const char dp_mod_id[]="mod-b";
#endif
EX const char dp_mod_tag[]="fixture-fw";
EX const char *const dp_mod_tags[]={dp_mod_tag,0};
EX unsigned dp_constructor_count;
static void __attribute__((constructor)) dp_constructor(void) {
    ++dp_constructor_count;
    dp_constructor_marker(DP_INDEX);
}
EX __attribute__((noinline)) int dp_foreign_claim(const az_core_v1 *core,const char *id) {
    const dp_fixture_input *input=dp_fixture_input_get(0);
    return dso_claim_call(core->claim_hook,id,input->target,input->replacement,input->original);
}
static const az_core_v1 *retained_core;
EX __attribute__((noinline)) int dp_late_claim(void) {
    const dp_fixture_input *input=dp_fixture_input_get(DP_INDEX);
    return dso_claim_call(retained_core->claim_hook,dp_mod_id,input->target,input->replacement,input->original);
}
EX __attribute__((noinline)) int dp_init(const az_core_v1 *core) {
    retained_core=core;
    dp_callback_probe(DP_INDEX,5);
    unsigned mode=dp_fixture_mode();
    if(mode==DP_FOREIGN && DP_INDEX==0) return dp_foreign_probe(core)==-1 ? 0 : -9;
    if(mode==DP_ORDINARY || mode==DP_PARTIAL) {
        const dp_fixture_input *input=dp_fixture_input_get(DP_INDEX);
        int result=dso_claim_call(core->claim_hook,dp_mod_id,input->target,input->replacement,input->original);
        if(mode==DP_PARTIAL) return -7;
        return result;
    }
    return 0;
}
EX const az_mod_v1 dp_mod_descriptor={AZ_MOD_ABI,sizeof(az_mod_v1),dp_mod_id,dp_mod_tags,dp_init,0,0};
EX const az_mod_v1 dp_bad_descriptor={0,sizeof(az_mod_v1),dp_mod_id,dp_mod_tags,dp_init,0,0};
AZ_MOD_EXPORT __attribute__((noinline)) const az_mod_v1 *az_mod_v1_entry(void) {
    dp_callback_probe(DP_INDEX,3);
    return dp_fixture_mode()==DP_DESCRIPTOR_BAD ? &dp_bad_descriptor : &dp_mod_descriptor;
}
