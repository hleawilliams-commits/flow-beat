/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include "loader.h"
#include <elf.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
/* Exact four 64-bit slots named by the frozen AArch64 target/replacement
 * assembly. Explicit zero initialization supplies strong storage under GCC 7;
 * these are observed only after the ordinary fixture calls return. */
_Alignas(8) uint64_t rc_target_hits[4] = {0};
_Alignas(8) uint64_t rc_replacement_hits[4] = {0};
_Static_assert(sizeof rc_target_hits == 32 && sizeof rc_replacement_hits == 32,
               "four 64-bit counter slots per assembly family");
static bool no_invocations(void) {
    for(unsigned i=0;i<4;i++)
        if(rc_target_hits[i] != 0 || rc_replacement_hits[i] != 0)return false;
    return true;
}
static uint64_t invocation_count(void) {
    uint64_t total=0;
    for(unsigned i=0;i<4;i++)total+=rc_target_hits[i]+rc_replacement_hits[i];
    return total;
}
static const char *check_names[64],*failed_checks[64];
static size_t checks,failures;
static cc_module_snapshot module;
static uintptr_t main_lo,main_hi;
static cc_status lifecycle_status;
static void infra(const char *text) { fprintf(stderr,"FIXTURE_ABORT %s\n",text);
    exit(2);
    }
static void check(bool okay,const char *name) {
    if(checks==64)infra("check capacity");
    check_names[checks++]=name;
    if(!okay)failed_checks[failures++]=name;
}
static unsigned first_event(unsigned index,unsigned event) {
    for(unsigned i=0;i<dp_event_count();i++){const dp_event *e=dp_event_at(i);
        if(e->index==index && e->event==event)return i;
        }
    return UINT32_MAX;
}
static unsigned event_occurrences(unsigned index,unsigned event) {
    unsigned n=0;
    for(unsigned i=0;i<dp_event_count();i++){const dp_event *e=dp_event_at(i);
        if(e->index==index && e->event==event)++n;
        }return n;
}
static void run_module(void) {
    if(!dp_loader_register(0) || !dp_loader_entry_decision(0))infra("publish owned module and exact entry");
    lifecycle_status=dp_loader_run(0,&module);
}
static dp_claim_observation claim_at(unsigned n) {
    dp_claim_observation result={0};
    if(!dp_claim_get(n,&result))infra("claim observation");
    return result;
}
static uintptr_t exact_symbol(unsigned index,const char *name,unsigned type,unsigned flags) {
    uintptr_t result=0;
    if(!dp_loader_symbol_exact(index,name,type,flags,&result))infra("exact symbol");
    return result;
}
static bool no_claims(void) { return dp_claim_count()==0;
    }
static void ordinary(void) {
    run_module();
    dp_claim_observation call=claim_at(0);
    uintptr_t marker=exact_symbol(0,"dp_call_marker",STT_FUNC,PF_R|PF_X);
    rc_binding binding;
    if(!rc_binding_get(0,&binding))infra("fixed binding");
    check(call.raw_ra==marker,"raw-builtin-ra-equals-dso-return-marker");
    check(call.used_ra==marker,"admission-ra-equals-dso-return-marker");
    check(call.returned_sdk_result==0 && call.claim.installer_called && call.claim.receipt.redirect_published,"ordinary-claim-sdk-success");
    check(module.nhooks==1 && call.claim.accounted && call.claim.nhooks_after==1,"ordinary-claim-counted-once");
    uintptr_t id=exact_symbol(0,"dp_mod_id",STT_OBJECT,PF_R);
    check(call.id_pointer==id && call.target==binding.target && call.replacement==(uintptr_t)binding.replacement && call.original==(uintptr_t)binding.original,"actual-arguments-match-fixed-binding");
    check(dp_loader_covered(0,call.raw_ra,4,PF_R|PF_X),"actual-ra-in-owned-executable-extent");
    /* Counters observe entry into the authored assembly bodies, not a general
     * instruction trace or concurrent native invocation guarantee. */
    check(dp_claim_count()==1 && no_invocations(),"no-patched-target-or-replacement-invocation");
}
static void constructors(void) {
    run_module();
    unsigned ctor=first_event(0,0),returned=first_event(0,1);
    check(ctor!=UINT32_MAX && ctor<returned,"constructor-marker-before-dlopen-return");
    check(dp_constructor_local(0)==1,"constructor-local-count-one");
    check(dp_preinit_sessions_zero() && dp_active_session()==0,"session-zero-before-init");
    check(first_event(0,2)<first_event(0,13),"module-range-before-entry");
    check(first_event(0,13)<first_event(0,4) && first_event(0,4)<first_event(0,15) && first_event(0,15)<first_event(0,6),"entry-before-open-init");
    check(dp_probes_complete(),"callbacks-outside-cm-and-writer");
}
static void dependency(void) {
    bool accepted=dp_loader_entry_decision(2);
    check(!accepted,"dependency-entry-owner-refused");
    uintptr_t counter=exact_symbol(3,"dp_dep_invoked",STT_OBJECT,PF_R);
    check(*(const unsigned*)counter==0 && event_occurrences(2,13)==0,"dependency-decoy-not-invoked");
    check(no_claims(),"no-installer-on-dependency-decision");
}
static void query_fault(void) {
    const dp_object *o=dp_loader_object(0);
    check(o->query_raw==0 && o->raw_map && o->map,"raw-query-success-retained");
    check(o->query_used==-1 && o->query_raw==0,"consumed-query-failure-distinct");
    check(!o->provenance,"failed-provenance-decision-refused");
    check(no_claims() && !o->range_registered && event_occurrences(0,13)==0,"no-installer-on-provenance-decision");
}
static void descriptor_bad(void) {
    run_module();
    const dp_object *o=dp_loader_object(0);
    check(o->handle!=0 && module.handle_retained,"successful-handle-retained-after-refusal");
    check(module.range_published && o->range_registered,"range-retained-after-descriptor-refusal");
    check(lifecycle_status==CC_DESCRIPTOR_REFUSED && !module.init_attempted && !module.session_used && no_claims(),"descriptor-refused-before-init");
    check(module.entry_attempted && module.entry_returned && event_occurrences(0,13)==1,"entry-attempted-once");
}
static void partial(void) {
    run_module();
    rc_attempt before={0},after={0};
    if(!rc_snapshot(0,&before))infra("published snapshot");
    check(lifecycle_status==CC_INIT_FAILED && module.init_returned && module.init_result==-7,"actual-init-return-error-retained");
    rc_metadata metadata;
    bool found=rc_lookup(before.emission.island,&metadata);
    rc_binding binding;
    if(!rc_binding_get(0,&binding))infra("binding");
    check(module.nhooks==1 && before.metadata_published && before.original_published && before.redirect_published && found && *binding.original==(void*)before.emission.trampoline,"published-route-retained-after-dso-init-error");
    check(dp_loader_object(0)->handle && module.handle_retained,"dso-handle-retained-after-init-error");
    int late=dp_loader_foreign_late();
    dp_claim_observation call=claim_at(1);
    if(!rc_snapshot(0,&after))infra("retained late snapshot");
    check(late==-1 && call.captured_session==0 && !call.claim.installer_called && !module.session_open && dp_active_session()==0 && !memcmp(&before,&after,sizeof before),"later-sdk-call-refused-after-init-close");
    check(dp_claim_count()==2 && no_invocations(),"no-patched-target-or-replacement-invocation");
}
static void foreign(void) {
    if(!dp_loader_register(1))infra("foreign object publication");
    run_module();
    dp_claim_observation call=claim_at(0);
    check(call.returned_sdk_result==-1 && call.captured_session==1 && call.claim.status==CC_CALLER_REFUSED,"foreign-dso-id-caller-refused");
    uintptr_t marker=exact_symbol(1,"dp_call_marker",STT_FUNC,PF_R|PF_X);
    check(call.raw_ra==marker && dp_loader_covered(1,call.raw_ra,4,PF_R|PF_X) && !dp_loader_covered(0,call.raw_ra,4,PF_R|PF_X),"foreign-raw-ra-belongs-to-actual-caller");
    check(!call.claim.installer_called && module.nhooks==0,"foreign-no-installer");
}
static void symbols(void) {
    run_module();
    const dp_object *o=dp_loader_object(0);
    check(o->entry_owned,"owned-entry-symbol-exact");
    check(o->init_owned,"owned-init-symbol-exact");
    check(o->descriptor_bounded,"descriptor-storage-bounded");
    check(o->id_tags_bounded,"id-and-tags-bounded");
    rc_binding binding;
    if(!rc_binding_get(0,&binding))infra("fixed binding");
    check(main_lo<main_hi && binding.target>=main_lo && binding.target<=main_hi-16,"main-target-envelope-from-actual-phdr");
}

static void js(FILE *f,const char *s) {
    fputc('"',f);
    for(const unsigned char *p=(const unsigned char *)s;*p;++p) {
        if(*p=='"' || *p=='\\') { fputc('\\',f);fputc(*p,f); }
        else if(*p<32) fprintf(f,"\\u%04x",(unsigned)*p);
        else fputc(*p,f);
    }
    fputc('"',f);
}
static void hex(FILE *f,const void *data,size_t length) {
    const unsigned char *p=data; fputc('"',f);
    for(size_t i=0;i<length;++i) fprintf(f,"%02x",p[i]);
    fputc('"',f);
}
static void raw_json(FILE *f,rc_raw r) {
    fprintf(f,"{\"attempted\":%d,\"returned\":%d,\"underlying_called\":%d,\"injected\":%d,\"errno_valid\":%d,\"result\":%" PRIdPTR ",\"error\":%d}",
        r.attempted,r.returned,r.underlying_called,r.injected,r.errno_valid,r.result,r.error);
}
static void span_json(FILE *f,rc_span s) { fprintf(f,"[%" PRIuPTR ",%" PRIuPTR "]",s.begin,s.end); }
static void metadata_json(FILE *f,const rc_metadata *m) {
    fprintf(f,"{\"owner\":{\"kind\":%u,\"process_id\":%" PRIu64 ",\"id\":",(unsigned)m->owner.kind,m->owner.process_id);
    js(f,m->owner.id);
    fprintf(f,"},\"binding\":%u,\"ncopy\":%u,\"out\":%u,\"target\":%" PRIuPTR ",\"replacement\":%" PRIuPTR ",\"original_slot\":%" PRIuPTR ",\"island\":%" PRIuPTR ",\"trampoline\":%" PRIuPTR ",\"continuation\":%" PRIuPTR ",\"target_exclusion\":",
        m->binding,m->ncopy,m->out,m->target,m->replacement,m->original_slot,m->island,m->trampoline,m->continuation);
    span_json(f,m->target_exclusion);fputs(",\"island_instructions\":",f);span_json(f,m->island_instructions);
    fputs(",\"island_storage\":",f);span_json(f,m->island_storage);
    fputs(",\"trampoline_instructions\":",f);span_json(f,m->trampoline_instructions);
    fputs(",\"trampoline_storage\":",f);span_json(f,m->trampoline_storage);
    fputs(",\"source_bytes\":",f);hex(f,m->source_words,16);fputc('}',f);
}
static void query_json(FILE *f,const rc_query *q) {
    fputs("{\"open\":",f);raw_json(f,q->open);fputs(",\"reads\":[",f);
    unsigned n=q->read_count<RC_READS?q->read_count:RC_READS;
    for(unsigned i=0;i<n;++i) { if(i)fputc(',',f);raw_json(f,q->reads[i]); }
    fputs("],\"close\":",f);raw_json(f,q->close);
    fprintf(f,",\"read_count\":%u,\"read_capacity_refused\":%d,\"returned\":%d,\"parser_result\":%d}",
        q->read_count,q->read_capacity_refused,q->returned,q->parser_result);
}
static void protection_json(FILE *f,const rc_protection *p) {
    fputs("{\"unlock\":",f);raw_json(f,p->unlock);fputs(",\"page_size\":",f);raw_json(f,p->page_size);
    fputs(",\"mutex_lock\":",f);raw_json(f,p->mutex_lock);fputs(",\"make_writable\":",f);raw_json(f,p->make_writable);
    fputs(",\"relock\":",f);raw_json(f,p->relock);fputs(",\"mutex_release\":",f);raw_json(f,p->mutex_release);
    fputs(",\"query\":",f);query_json(f,&p->query);
    fprintf(f,",\"token_valid\":%d,\"base\":%" PRIuPTR ",\"length\":%zu,\"saved_bits\":%d}",
        p->token_valid,p->base,p->length,p->saved_bits);
}
static void attempt_json(FILE *f,const rc_attempt *a) {
    fprintf(f,"{\"admitted\":%d,\"installer_entered\":%d,\"prepared\":%d,\"permit_validated\":%d,"
        "\"trampoline_prewrite_validated\":%d,\"metadata_published\":%d,\"original_published\":%d,\"redirect_published\":%d,"
        "\"original_saw_metadata\":%d,\"redirect_saw_original\":%d,\"island_slot_burned\":%d,"
        "\"nhooks_accounted\":%d,\"metadata_order\":%u,\"original_order\":%u,\"redirect_order\":%u,"
        "\"mapping_count\":%u,\"pool_index\":%u,\"pool_slot\":%u,\"pool_base\":%" PRIuPTR ",\"pool_length\":%zu,\"status\":%u,\"installer_result\":%d,\"emission\":",
        a->admitted,a->installer_entered,a->prepared,a->permit_validated,a->trampoline_prewrite_validated,a->metadata_published,
        a->original_published,a->redirect_published,a->original_saw_metadata,a->redirect_saw_original,
        a->island_slot_burned,a->nhooks_accounted,a->metadata_order,a->original_order,a->redirect_order,
        a->mapping_count,a->pool_index,a->pool_slot,a->pool_base,a->pool_length,(unsigned)a->status,a->installer_result);
    metadata_json(f,&a->emission);fputs(",\"mappings\":[",f);
    for(unsigned i=0;i<a->mapping_count && i<RC_MAPS;++i) {
        const rc_mapping *m=&a->mappings[i]; if(i)fputc(',',f);
        fprintf(f,"{\"kind\":%u,\"hint\":%" PRIuPTR ",\"address\":%" PRIuPTR ",\"length\":%zu,\"protection\":%d,"
            "\"held\":%d,\"accepted_pool\":%d,\"placement_rejected\":%d,\"cleanup_uncertain\":%d,\"allocation\":",
            (unsigned)m->kind,m->hint,m->address,m->length,m->protection,m->held,m->accepted_pool,m->placement_rejected,m->cleanup_uncertain);
        raw_json(f,m->allocation);fputs(",\"cleanup\":",f);raw_json(f,m->cleanup);fputc('}',f);
    }
    fputs("],\"initial_page_size\":",f);raw_json(f,a->initial_page_size);
    fputs(",\"seal\":",f);raw_json(f,a->seal);
    fputs(",\"island_protection\":",f);protection_json(f,&a->island_protection);
    fputs(",\"target_protection\":",f);protection_json(f,&a->target_protection);
    fprintf(f,",\"trampoline_cache_armed\":%d,\"trampoline_cache_returned\":%d,"
        "\"island_cache_armed\":%d,\"island_cache_returned\":%d,\"target_cache_armed\":%d,\"target_cache_returned\":%d,"
        "\"diagnostic_before\":%u,\"diagnostic_after\":%u,\"diagnostic_lost\":%u}",
        a->trampoline_cache_armed,a->trampoline_cache_returned,a->island_cache_armed,a->island_cache_returned,
        a->target_cache_armed,a->target_cache_returned,a->diagnostic_before,a->diagnostic_after,a->diagnostic_lost);
}


static void module_json(FILE *f,const cc_module_snapshot *m){
    fprintf(f,"{\"present\":%d,\"range_published\":%d,\"handle_retained\":%d,\"session_used\":%d,\"session_open\":%d,\"entry_attempted\":%d,\"entry_returned\":%d,\"descriptor_accepted\":%d,\"init_attempted\":%d,\"init_returned\":%d,\"callbacks_retained\":%d,\"id\":",
        m->present,m->range_published,m->handle_retained,m->session_used,m->session_open,m->entry_attempted,m->entry_returned,m->descriptor_accepted,m->init_attempted,m->init_returned,m->callbacks_retained);js(f,m->id);
    fprintf(f,",\"hooks_ok\":%d,\"firmware_any\":%d,\"process_id\":%" PRIu64 ",\"lo\":%" PRIuPTR ",\"hi\":%" PRIuPTR ",\"index\":%u,\"nhooks\":%u,\"init_result\":%d,\"phase\":%u,\"load_status\":%u}",
        m->hooks_ok,m->firmware_any,m->process_id,m->lo,m->hi,m->index,m->nhooks,m->init_result,(unsigned)m->phase,(unsigned)m->load_status);
}
static void claim_json(FILE *f,const cc_claim_result *r){
    fprintf(f,"{\"status\":%u,\"sdk_result\":%d,\"captured_session\":%u,\"resolved_session\":%u,\"module_index\":%u,\"nhooks_before\":%u,\"nhooks_after\":%u,\"installer_called\":%d,\"accounted\":%d,\"incomplete\":%d,\"receipt\":",
        (unsigned)r->status,r->sdk_result,r->captured_session,r->resolved_session,r->module_index,r->nhooks_before,r->nhooks_after,r->installer_called,r->accounted,r->incomplete);
    attempt_json(f,&r->receipt);fputc('}',f);
}
static void strings(FILE*f,const char*const*a,size_t n){fputc('[',f);for(size_t i=0;i<n;i++){if(i)fputc(',',f);js(f,a[i]);}fputc(']',f);}
static void common(FILE*f,const char*selector){fputs("{\"schema\":1,\"selector\":",f);js(f,selector);fprintf(f,",\"checks\":%zu,\"check_names\":",checks);strings(f,check_names,checks);fprintf(f,",\"failures\":%zu,\"failed_checks\":",failures);strings(f,failed_checks,failures);fprintf(f,",\"target_invocations\":%"PRIu64,invocation_count());}
static void write_evidence(const char *path,const char *selector){
    FILE*f=fopen(path,"wx");if(!f)infra("exclusive evidence");common(f,selector);
    fprintf(f,",\"main_lo\":%"PRIuPTR",\"main_hi\":%"PRIuPTR",\"lifecycle_status\":%u,\"dropped_claims\":%u,\"loader\":",main_lo,main_hi,(unsigned)lifecycle_status,dp_claim_dropped());
    dp_loader_write_evidence(f);
    fputs(",\"target_hits\":[",f);
    for(unsigned i=0;i<4;i++){if(i)fputc(',',f);fprintf(f,"%"PRIu64,rc_target_hits[i]);}
    fputs("],\"replacement_hits\":[",f);
    for(unsigned i=0;i<4;i++){if(i)fputc(',',f);fprintf(f,"%"PRIu64,rc_replacement_hits[i]);}
    fputs("],\"module\":",f);module_json(f,&module);fputs(",\"calls\":[",f);
    for(unsigned i=0;i<dp_claim_count();i++){dp_claim_observation c=claim_at(i);if(i)fputc(',',f);
        fprintf(f,"{\"raw_ra\":%"PRIuPTR",\"used_ra\":%"PRIuPTR",\"id_pointer\":%"PRIuPTR",\"target\":%"PRIuPTR",\"replacement\":%"PRIuPTR",\"original\":%"PRIuPTR",\"captured_session\":%u,\"returned_sdk_result\":%d,\"claim\":",c.raw_ra,c.used_ra,c.id_pointer,c.target,c.replacement,c.original,c.captured_session,c.returned_sdk_result);claim_json(f,&c.claim);fputc('}',f);
    }fputs("],\"receipt\":",f);rc_attempt receipt={0};(void)rc_snapshot(0,&receipt);attempt_json(f,&receipt);fputs("}\n",f);if(fclose(f))infra("evidence close");
}
int main(int argc,char **argv){
    if(argc!=4)infra("usage SELECTOR EVIDENCE_JSON DSO_DIR");
    static const char*names[]={"ordinary-ingress","constructor-order","dependency-entry","provenance-fault","descriptor-refusal-custody","partial-init-custody","foreign-caller","symbol-descriptor-custody"};
    unsigned mode=8;for(unsigned i=0;i<8;i++)if(!strcmp(argv[1],names[i]))mode=i;if(mode==8)infra("selector");
    if(!dp_main_envelope(&main_lo,&main_hi) || dp_core_initialize(main_lo,main_hi)!=CC_OK || dp_core_reserve(4)!=CC_OK)infra("main/core initialization");
    dp_loader_initialize(mode);if(!dp_loader_load_all(argv[3]))infra("fixed loads");bool query_ok=dp_loader_query_all();
    if(mode!=DP_QUERY_FAULT && !query_ok)infra("actual provenance");
    switch(mode){case DP_ORDINARY:ordinary();break;case DP_CONSTRUCTOR:constructors();break;case DP_DEPENDENCY:dependency();break;case DP_QUERY_FAULT:query_fault();break;case DP_DESCRIPTOR_BAD:descriptor_bad();break;case DP_PARTIAL:partial();break;case DP_FOREIGN:foreign();break;case DP_SYMBOLS:symbols();break;default:infra("mode");}
    write_evidence(argv[2],argv[1]);common(stdout,argv[1]);puts("}");return failures?1:0;
}
