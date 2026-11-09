/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include "loader.h"
#include <elf.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
_Alignas(8) uint64_t rc_target_hits[4] = {0};
_Alignas(8) uint64_t rc_replacement_hits[4] = {0};
_Static_assert(sizeof rc_target_hits == 32 && sizeof rc_replacement_hits == 32,
               "four exact frozen assembly counters per family");
static const char *check_names[64], *failed_checks[64];
static size_t checks, failures;
static cc_module_snapshot module;
static uintptr_t main_lo, main_hi;
static cc_status lifecycle_status;
typedef struct { const char *label; uintptr_t address; int owner; } query_observation;
static query_observation lookup_queries[64];
static unsigned lookup_count;
static const char *consumer_return;
static void infra(const char *message) {
    fprintf(stderr,"FIXTURE_ABORT %s\n",message); exit(2);
}
static void check(bool okay,const char *name) {
    if(checks==64)infra("check capacity");
    check_names[checks++]=name;
    if(!okay)failed_checks[failures++]=name;
}
static bool no_invocations(void) {
    for(unsigned i=0;i<4;i++)
        if(rc_target_hits[i] || rc_replacement_hits[i])return false;
    return true;
}
static uint64_t invocation_count(void) {
    uint64_t result=0;
    for(unsigned i=0;i<4;i++)result+=rc_target_hits[i]+rc_replacement_hits[i];
    return result;
}
static int lookup(uintptr_t address,const char *label) {
    if(lookup_count==64)infra("lookup capacity");
    int owner=ma_owner_index(address);
    lookup_queries[lookup_count++]=(query_observation){label,address,owner};
    return owner;
}
static bool id_equal(const char *id,const char *expected) {
    return id && strcmp(id,expected)==0;
}
static unsigned first_event(unsigned index,unsigned kind) {
    for(unsigned i=0;i<dp_event_count();i++) {
        const dp_event *event=dp_event_at(i);
        if(event->index==index && event->event==kind)return i;
    }
    return UINT32_MAX;
}
static uintptr_t exact_symbol(unsigned index,const char *name,unsigned type,unsigned flags) {
    uintptr_t result=0;
    if(!dp_loader_symbol_exact(index,name,type,flags,&result))infra("exact fixed symbol");
    return result;
}
static dp_claim_observation claim_at(unsigned index) {
    dp_claim_observation result={0};
    if(!dp_claim_get(index,&result))infra("claim observation");
    return result;
}
static void register_root(unsigned index) {
    if(!dp_loader_register(index))infra("fixed root publication");
}
static void run_a(void) {
    register_root(0);
    if(!dp_loader_entry_decision(0))infra("exact owned entry");
    lifecycle_status=dp_loader_run(0,&module);
}
static void configure_io(int write_mode) {
    ma_model_io_config(7,write_mode,0,0);
}
static bool exact_write(const char *id) {
    const ma_observations *o=ma_model_observations();
    size_t length=strlen(id);
    return !o->violation && o->path_matches && o->open_mode==0644 &&
        o->open_flags==(O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC) &&
        o->io_count==4 && o->io[0].kind==1 && o->io[1].kind==2 &&
        o->io[2].kind==3 && o->io[3].kind==4 &&
        o->write_requested==length && o->payload_length==length &&
        !memcmp(o->payload,id,length);
}
static void actual_envelope(void) {
    register_root(0); register_root(1);
    uintptr_t pc=exact_symbol(0,"dp_init",STT_FUNC,PF_R|PF_X);
    uintptr_t data=exact_symbol(0,"dp_mod_descriptor",STT_OBJECT,PF_R);
    uintptr_t lr=exact_symbol(1,"dp_init",STT_FUNC,PF_R|PF_X);
    bool data_nonexec=dp_loader_covered(0,data,sizeof(az_mod_v1),PF_R) &&
        !dp_loader_covered(0,data,sizeof(az_mod_v1),PF_X);
    check(lookup(pc,"actual-A-function")==0,"actual-module-exec-envelope-owned");
    check(data_nonexec && lookup(data,"actual-A-descriptor")==0,"actual-module-nonexec-envelope-owned");
    check(lookup(dp_loader_object(0)->hi,"actual-A-upper-bound")!=0,"module-upper-bound-excluded");
    configure_io(0);
    consumer_return=ma_consume_fault(pc,lr,0);
    check(id_equal(consumer_return,"mod-a") && exact_write("mod-a"),"actual-pc-owner-and-payload");
    check(ma_model_observations()->peek_count==0,"no-frame-reads-after-pc");
    check(no_invocations(),"fixed-target-replacement-counters-zero");
}
static void constructor_gaps(void) {
    register_root(0); register_root(1);
    bool unowned=ma_constructor_count()==4, ordered=unowned;
    for(unsigned i=0;i<ma_constructor_count();i++) {
        const ma_constructor_observation *c=ma_constructor_at(i);
        unowned=unowned && c->raw_ra!=0 && c->owner==-1;
        ordered=ordered && !c->handle_present && c->session==0 &&
            c->ordinal<first_event(c->index,1) && c->ordinal<first_event(0,2);
    }
    check(unowned,"constructor-lookups-unowned");
    check(ordered,"constructors-before-handle-and-publication");
    uintptr_t dependency=exact_symbol(3,"dp_dep_force",STT_FUNC,PF_R|PF_X);
    check(dp_loader_object(3)->handle && dp_loader_object(3)->provenance &&
          lookup(dependency,"unassigned-dependency")==-1,
          "dependency-handle-retained-without-community-owner");
    bool all=true;
    for(unsigned i=0;i<4;i++)all=all && first_event(i,1)<first_event(0,2);
    check(all && dp_preinit_sessions_zero() && dp_active_session()==0,
          "all-loads-before-session");
    check(no_invocations(),"fixed-target-replacement-counters-zero");
}
static void query_hole(void) {
    const dp_object *a=dp_loader_object(0),*b=dp_loader_object(1);
    (void)ma_loader_register(0,true,false,true);
    register_root(1);
    check(a->query_raw==0 && a->raw_map && a->map && a->headers_exact &&
          a->query_used==-1 && !a->provenance,"raw-query-and-consumed-failure-distinct");
    check(lookup(a->lo,"query-refused-A")==-1,"unproven-module-remains-invisible");
    check(lookup(b->lo,"published-B-after-hole")==1,"published-module-after-hole-found");
    check(a->handle && b->handle,"both-successful-handles-retained");
    check(dp_claim_count()==0 && first_event(0,13)==UINT32_MAX &&
          first_event(1,13)==UINT32_MAX,"no-installer-on-query-refusal");
}
static void descriptor_retention(void) {
    run_a();
    check(lifecycle_status==CC_DESCRIPTOR_REFUSED && !module.init_attempted &&
          !module.session_used,"actual-descriptor-refused-before-init");
    check(lookup(dp_loader_object(0)->lo,"descriptor-refused-A")==0,
          "descriptor-refused-module-owner-retained");
    check(dp_loader_object(0)->handle && module.handle_retained,"successful-handle-retained");
    check(dp_claim_count()==0,"no-installer-on-descriptor-refusal");
}
static void partial_attribution(void) {
    run_a();
    rc_attempt receipt={0};
    if(!rc_snapshot(0,&receipt))infra("partial retained receipt");
    rc_metadata metadata={0};
    bool raw_found=rc_lookup(receipt.emission.island,&metadata);
    check(lifecycle_status==CC_INIT_FAILED && module.init_returned &&
          module.init_result==-7 && !module.session_open && dp_active_session()==0,
          "actual-init-minus7-and-session-closed");
    check(lookup(dp_loader_object(0)->lo,"failed-init-A")==0,
          "failed-init-module-owner-retained");
    configure_io(0);
    consumer_return=ma_consume_fault(receipt.emission.island,0,0);
    check(id_equal(consumer_return,"mod-a") && exact_write("mod-a"),
          "failed-init-generated-owner-resolved");
    rc_binding binding;
    if(!rc_binding_get(0,&binding))infra("host binding");
    check(module.nhooks==1 && receipt.metadata_published && receipt.original_published &&
          receipt.redirect_published && *binding.original==(void*)receipt.emission.trampoline &&
          dp_loader_object(0)->handle && module.handle_retained,
          "receipt-original-code-count-retained");
    check(raw_found && metadata.owner.kind==RC_OWNER_COMMUNITY &&
          metadata.owner.process_id==100 && id_equal(metadata.owner.id,"mod-a"),
          "raw-generated-record-still-visible");
    check(no_invocations(),"fixed-target-replacement-counters-zero");
}
static void permission_owner(void) {
    lifecycle_status=ma_loader_register(0,true,true,false);
    if(!cc_module_get(0,&module))infra("permission lifecycle snapshot");
    ma_module_value value={0};
    bool committed=ma_module_snapshot(0,&value);
    check(lookup(dp_loader_object(0)->lo,"permission-refused-A")==0,
          "positive-provenance-owner-despite-permission-refusal");
    check(lifecycle_status==CC_HOOKS_REFUSED && !module.hooks_ok &&
          !module.range_published,"permission-result-still-refused");
    check(!module.entry_attempted && !module.init_attempted && dp_claim_count()==0,
          "no-entry-init-or-installer");
    check(committed && value.committed && id_equal(value.id,"mod-a") &&
          dp_loader_object(0)->handle && module.handle_retained,
          "retained-handle-and-copied-id");
}
static void generated_pc(void) {
    register_root(1); run_a();
    rc_attempt receipt={0};
    if(!rc_snapshot(0,&receipt))infra("ordinary generated receipt");
    uintptr_t lr=exact_symbol(1,"dp_init",STT_FUNC,PF_R|PF_X);
    if(!ma_model_add_frame(0x50000,0x50020,lr))infra("modeled unused frame");
    configure_io(2);
    consumer_return=ma_consume_fault(receipt.emission.island,lr,0x50000);
    check(id_equal(consumer_return,"mod-a"),"generated-community-pc-precedes-module-lr");
    check(exact_write("mod-a"),"generated-owner-write-payload");
    check(ma_model_observations()->peek_count==0,"no-frame-reads-after-generated-pc");
    const ma_observations *o=ma_model_observations();
    check(id_equal(consumer_return,"mod-a") && o->write_returned==(ssize_t)o->write_requested-1,
          "ownership-survives-modeled-short-write");
    unsigned io=o->io_count,reads=o->peek_count;
    const char *second=ma_consume_fault(lr,0,0);
    check(!second && o->io_count==io && o->peek_count==reads,"second-call-suppressed");
    check(no_invocations(),"fixed-target-replacement-counters-zero");
}
#ifdef MA_MODEL
static rc_metadata modeled_records[RC_BINDINGS];
static bool modeled_record_present[RC_BINDINGS];
static void model_module(unsigned index,const char *id,uint64_t pid,uintptr_t lo,uintptr_t hi) {
    if(!ma_model_stage(index,id,pid,lo,hi) || !ma_model_commit(index))infra("model module");
}
static void model_record(unsigned binding,rc_owner_kind kind,uint64_t pid,const char *id,
                         uintptr_t island,uintptr_t trampoline) {
    rc_metadata m={0};
    m.binding=binding; m.owner.kind=kind; m.owner.process_id=pid;
    size_t n=strlen(id);
    if(n>=sizeof m.owner.id)infra("model id bound");
    memcpy(m.owner.id,id,n+1);
    m.ncopy=1; m.out=1;
    m.target=0x900000+binding*0x100; m.replacement=0xa00000+binding*0x100;
    m.original_slot=0xb00000+binding*0x100; m.continuation=m.target+4;
    m.target_exclusion=(rc_span){m.target,m.target+4};
    m.island=island; m.trampoline=trampoline;
    m.island_instructions=(rc_span){island,island+8};
    m.island_storage=(rc_span){island,island+16};
    m.trampoline_instructions=(rc_span){trampoline,trampoline+12};
    m.trampoline_storage=(rc_span){trampoline,trampoline+20};
    if(!ma_model_record(binding,&m))infra("model generated record");
    if(!rc_lookup(island,&modeled_records[binding]))infra("modeled generated readback");
    modeled_record_present[binding]=true;
}
static void core_frame(unsigned count) {
    model_module(0,"mod-a",100,0x100000,0x101000);
    model_record(0,RC_OWNER_CORE,1,"",0x300000,0x310000);
    model_record(1,RC_OWNER_COMMUNITY,100,"mod-a",0x200000,0x210000);
    for(unsigned i=0;i<count;i++) {
        uintptr_t fp=0x50000+i*0x20;
        uintptr_t next=count==1 ? fp : fp+0x20;
        if(!ma_model_add_frame(fp,next,i+1==count ? 0x200000 : 0xdead000))
            infra("modeled frame cells");
    }
    configure_io(0);
    if(count==1)check(lookup(0x300000,"typed-Core-PC")==-1,
                      "core-does-not-fabricate-community-owner");
    consumer_return=ma_consume_fault(0x300000,0,0x50000);
    const ma_observations *o=ma_model_observations();
    if(count==1) {
        check(id_equal(consumer_return,"mod-a") && exact_write("mod-a"),
              "generated-first-frame-owner-and-payload");
        check(id_equal(consumer_return,"mod-a") && o->peek_count==2,
              "lookup-before-nonincreasing-break");
        check(o->peek_count==2 && o->peeks[0].address==0x50000 &&
              o->peeks[1].address==0x50008 && o->peeks[0].success &&
              o->peeks[1].success,"ordered-frame-read-trace");
    } else if(count==16) {
        bool ordered=o->peek_count==32;
        for(unsigned i=0;i<o->peek_count && i<32;i++)
            ordered=ordered && o->peeks[i].success &&
                o->peeks[i].address==0x50000+(i/2)*0x20+(i%2)*8;
        check(ordered && id_equal(consumer_return,"mod-a") && exact_write("mod-a"),
              "sixteenth-generated-frame-owned");
    } else {
        bool ordered=o->peek_count==32;
        for(unsigned i=0;i<o->peek_count && i<32;i++)
            ordered=ordered && o->peeks[i].success &&
                o->peeks[i].address==0x50000+(i/2)*0x20+(i%2)*8;
        check(ordered && !consumer_return && o->unblamed==1 && o->io_count==0,
              "seventeenth-generated-frame-unread");
    }
}
static void first_index(void) {
    model_module(1,"mod-b",101,0x100000,0x102000);
    model_module(0,"mod-a",100,0x100000,0x101000);
    check(lookup(0x100100,"overlap-original-index")==0,"first-module-index-wins");
}
static void same_address(void) {
    model_module(0,"mod-a",100,0x100000,0x101000);
    model_module(1,"mod-b",101,0x200000,0x201000);
    model_record(0,RC_OWNER_COMMUNITY,100,"mod-a",0x200000,0x210000);
    check(lookup(0x200000,"module-B-generated-A")==1,"module-beats-generated-at-same-address");
}
static void later_mask(void) {
    model_module(0,"mod-a",100,0x100000,0x101000);
    model_record(0,RC_OWNER_COMMUNITY,100,"mod-a",0x200000,0x210000);
    int before=lookup(0x200000,"generated-before-later-module");
    check(lookup(0x200008,"literal-before-later-module")==-1,"generated-literal-alone-unowned");
    model_module(1,"mod-b",101,0x200000,0x201000);
    check(before==0 && lookup(0x200000,"later-module-mask")==1,"later-module-can-mask-generated");
    check(lookup(0x200008,"later-module-literal")==1,"module-can-own-generated-literal-address");
}
static void identity_case(bool ambiguous) {
    model_module(0,"mod-a",100,0x100000,0x101000);
    if(ambiguous)model_module(1,"mod-a",100,0x110000,0x111000);
    model_record(0,RC_OWNER_COMMUNITY,ambiguous?100:999,"mod-a",0x200000,0x210000);
    check(lookup(0x200000,ambiguous?"ambiguous-full-identity":"same-id-wrong-process")==-1,
          ambiguous?"ambiguous-identity-not-resolved":"mismatched-process-identity-not-resolved");
}
static void publication_copy(void) {
    static char staging[33]="mod-a";
    if(!ma_model_stage(0,staging,100,0x100000,0x101000) || !ma_model_commit(0))
        infra("copy model publication");
    memcpy(staging,"mod-z",6);
    configure_io(0);
    consumer_return=ma_consume_fault(0x100000,0,0);
    check(id_equal(consumer_return,"mod-a") && exact_write("mod-a"),
          "copied-module-id-survives-input-change");
}
static void publication_bound(void) {
    if(!ma_model_stage(0,"mod-a",100,0x100000,0x101000))infra("unpublished stage");
    check(lookup(0x100000,"staged-but-unpublished")==-1,"unpublished-slot-does-not-return-owner");
    if(!ma_model_commit(0))infra("first commit");
    bool repeated=ma_model_commit(0),reopened=ma_model_stage(0,"mod-z",999,0x120000,0x121000);
    ma_module_value value={0};
    bool present=ma_module_snapshot(0,&value);
    check(!repeated && !reopened && present && value.committed && id_equal(value.id,"mod-a"),
          "published-slot-cannot-reopen");
    check(!ma_model_stage(MA_MODULES,"mod-b",101,0x110000,0x111000) &&
          !ma_model_commit(MA_MODULES),"bounded-index-capacity-refused");
}
static void *held_reader(void *unused) {
    (void)unused;
    return (void*)(intptr_t)(ma_owner_index(0x100000)+1);
}
static void held_writer(void) {
    model_module(0,"mod-a",100,0x100000,0x101000);
    if(!ma_model_hold_locks())infra("hold ordinary writer");
    pthread_t thread;
    if(pthread_create(&thread,NULL,held_reader,NULL)!=0)infra("reader thread");
    void *value=NULL;
    if(pthread_join(thread,&value)!=0)infra("reader join");
    bool released=ma_model_release_locks();
    check(released && (intptr_t)value==1,"reader-does-not-acquire-writer-lock");
}
#endif
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

static void attribution_json(FILE *f) {
    const ma_observations *o=ma_model_observations();
    fprintf(f,"{\"violation\":%d,\"peek_count\":%u,\"io_count\":%u,\"unblamed\":%u,\"open_flags\":%d,\"open_mode\":%u,\"path_matches\":%d,\"write_requested\":%zu,\"payload_length\":%zu,\"write_returned\":%jd,\"payload\":",
        o->violation,o->peek_count,o->io_count,o->unblamed,o->open_flags,
        o->open_mode,o->path_matches,o->write_requested,o->payload_length,(intmax_t)o->write_returned);
    hex(f,o->payload,o->payload_length);
    fputs(",\"peeks\":[",f);
    for(unsigned i=0;i<o->peek_count && i<MA_PEEKS;i++) {
        if(i)fputc(',',f);
        fprintf(f,"{\"address\":%"PRIuPTR",\"success\":%d}",o->peeks[i].address,o->peeks[i].success);
    }
    fputs("],\"io\":[",f);
    for(unsigned i=0;i<o->io_count && i<MA_IO_CALLS;i++) {
        if(i)fputc(',',f);
        fprintf(f,"{\"kind\":%d,\"fd\":%d,\"length\":%zu,\"result\":%jd}",
            o->io[i].kind,o->io[i].fd,o->io[i].length,(intmax_t)o->io[i].result);
    }
    fputs("],\"modules\":[",f);
    for(unsigned i=0;i<MA_MODULES;i++) {
        ma_module_value value={0};
        bool available=ma_module_snapshot(i,&value);
        if(i)fputc(',',f);
        fprintf(f,"{\"slot\":%u,\"available\":%d,\"committed\":%d,\"index\":%u,\"process_id\":%"PRIu64",\"lo\":%"PRIuPTR",\"hi\":%"PRIuPTR",\"id\":",
            i,available,value.committed,value.index,value.process_id,value.lo,value.hi);
        js(f,value.id); fputs(",\"id_bytes\":",f); hex(f,value.id,sizeof value.id); fputc('}',f);
    }
    fputs("],\"queries\":[",f);
    for(unsigned i=0;i<lookup_count;i++) {
        if(i)fputc(',',f);
        fputs("{\"label\":",f); js(f,lookup_queries[i].label);
        fprintf(f,",\"address\":%"PRIuPTR",\"owner\":%d}",lookup_queries[i].address,lookup_queries[i].owner);
    }
    fputs("],\"constructors\":[",f);
    for(unsigned i=0;i<ma_constructor_count();i++) {
        const ma_constructor_observation *c=ma_constructor_at(i);
        if(i)fputc(',',f);
        fprintf(f,"{\"index\":%u,\"session\":%u,\"ordinal\":%u,\"raw_ra\":%"PRIuPTR",\"owner\":%d,\"handle_present\":%d}",
            c->index,c->session,c->ordinal,c->raw_ra,c->owner,c->handle_present);
    }
    fputs("],\"modeled_generated\":[",f);
#ifdef MA_MODEL
    for(unsigned i=0;i<RC_BINDINGS;i++) {
        if(i)fputc(',',f);
        fprintf(f,"{\"slot\":%u,\"present\":%d,\"metadata\":",i,modeled_record_present[i]);
        metadata_json(f,&modeled_records[i]); fputc('}',f);
    }
#endif
    fputs("],\"consumer_return\":",f);
    if(consumer_return)js(f,consumer_return);else fputs("null",f);
    fputc('}',f);
}
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
    }fputs("],\"receipt\":",f);rc_attempt receipt={0};(void)rc_snapshot(0,&receipt);attempt_json(f,&receipt);fputs(",\"attribution\":",f);attribution_json(f);fputs("}\n",f);if(fclose(f))infra("evidence close");
}

int main(int argc,char **argv) {
    if(argc!=4)infra("usage SELECTOR EVIDENCE_JSON DSO_DIR");
    static const char *names[]={
        "actual-module-envelope","constructor-and-dependency-gaps",
        "query-refusal-and-hole","descriptor-refusal-retention",
        "partial-init-attribution","permission-independent-owner","generated-pc-module-lr",
        "core-first-frame","frame-sixteen","frame-seventeen","first-index","same-address",
        "later-mask","identity-mismatch","identity-ambiguity","publication-copy",
        "publication-bound","held-writer"};
    unsigned selected=18;
    for(unsigned i=0;i<18;i++)if(!strcmp(argv[1],names[i]))selected=i;
    if(selected==18)infra("selector");
#ifdef MA_MODEL
    if(selected<7)infra("actual selector in model build");
#else
    if(selected>=7)infra("model selector in actual build");
#endif
    if(!dp_main_envelope(&main_lo,&main_hi) ||
       dp_core_initialize(main_lo,main_hi)!=CC_OK || dp_core_reserve(4)!=CC_OK)
        infra("main and core initialization");
    if(selected<7) {
        static const unsigned modes[]={DP_SYMBOLS,DP_CONSTRUCTOR,DP_QUERY_FAULT,
            DP_DESCRIPTOR_BAD,DP_PARTIAL,DP_SYMBOLS,DP_ORDINARY};
        dp_loader_initialize(modes[selected]);
        if(!dp_loader_load_all(argv[3]))infra("fixed retained DSO loads");
        bool queried=dp_loader_query_all();
        if(selected!=2 && !queried)infra("actual positive provenance");
    }
    switch(selected) {
        case 0:actual_envelope();break;
        case 1:constructor_gaps();break;
        case 2:query_hole();break;
        case 3:descriptor_retention();break;
        case 4:partial_attribution();break;
        case 5:permission_owner();break;
        case 6:generated_pc();break;
#ifdef MA_MODEL
        case 7:core_frame(1);break;
        case 8:core_frame(16);break;
        case 9:core_frame(17);break;
        case 10:first_index();break;
        case 11:same_address();break;
        case 12:later_mask();break;
        case 13:identity_case(false);break;
        case 14:identity_case(true);break;
        case 15:publication_copy();break;
        case 16:publication_bound();break;
        case 17:held_writer();break;
#endif
        default:infra("compiled selector scope");
    }
    write_evidence(argv[2],argv[1]);
    common(stdout,argv[1]); puts("}");
    return failures ? 1 : 0;
}
