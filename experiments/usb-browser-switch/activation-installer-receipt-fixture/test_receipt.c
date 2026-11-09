/* New receipt integration only: authored scalar code, no firmware/old ABI suite.
 * Syscall refusals selected below are explicit pre-call injections. Only success
 * delegates to actual Linux mapping/protection/cache operations. Assembly hit
 * counters are non-atomic and not sanitizer instrumented; invocation occurs only
 * after every scheduled installer/closer thread has joined. */
#define _GNU_SOURCE 1
#include "receipt.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#ifndef RC_FIXTURE
#error "receipt tests require the explicit scheduler/fault fixture configuration"
#endif

unsigned long rc_target_hits[4], rc_replacement_hits[4];
extern char rc_target0_page_end[], rc_target1_page_end[];
extern char rc_target2_page_end[], rc_target3_page_end[];
static const char *check_names[128], *failed_checks[128];
static size_t checks, failures;
static rc_binding bindings[RC_BINDINGS];
static unsigned char before[RC_BINDINGS][16];
static rc_attempt saved[16];
static size_t saved_count;
static long page_size;

static void infra(const char *why) {
    fprintf(stderr,"FIXTURE_ABORT %s\n",why);
    exit(2);
}
static void check(bool yes, const char *name) {
    if (checks == 128) infra("check capacity");
    check_names[checks++]=name;
    if (!yes) failed_checks[failures++]=name;
}
static void save(rc_attempt a) {
    if (saved_count == 16) infra("receipt capacity");
    saved[saved_count++]=a;
}
static rc_request request(unsigned binding, uint64_t faults) {
    rc_request r={bindings[binding].replacement,bindings[binding].original,faults};
    return r;
}
static rc_status core(unsigned binding, uint64_t faults, rc_attempt *out) {
    rc_request r=request(binding,faults);
    memset(out,0,sizeof *out);
    rc_status status=rc_install_core(binding,&r,out);
    save(*out);
    return status;
}
static rc_epoch open_epoch(void) {
    rc_module module;
    memset(&module,0,sizeof module);
    module.process_id=UINT64_C(0x1020304050607080);
    strcpy(module.id,"receipt-community");
    /* Authored module provenance encloses the fixed replacement entry addresses.
     * It is not a recovered loader envelope or a target-code ownership claim. */
    module.lo=(uintptr_t)bindings[0].replacement;
    if (module.lo>UINTPTR_MAX-4) infra("replacement envelope overflow");
    module.hi=module.lo+4;
    for (unsigned i=1;i<RC_BINDINGS;++i) {
        uintptr_t entry=(uintptr_t)bindings[i].replacement;
        if (entry>UINTPTR_MAX-4) infra("replacement envelope overflow");
        if (entry<module.lo) module.lo=entry;
        if (entry+4>module.hi) module.hi=entry+4;
    }
    rc_epoch epoch={0};
    if (rc_open_epoch(&module,&epoch) != RC_OK) infra("epoch open");
    return epoch;
}
static rc_status community(rc_epoch epoch, unsigned binding, uint64_t faults, rc_attempt *out) {
    rc_request r=request(binding,faults);
    memset(out,0,sizeof *out);
    rc_status status=rc_install_community(&epoch,binding,&r,out);
    save(*out);
    return status;
}
static void initialize(void) {
    page_size=sysconf(_SC_PAGESIZE);
    if (page_size != 4096 || rc_initialize() != RC_OK) infra("selected platform/initialize");
    for (unsigned i=0;i<RC_BINDINGS;++i) {
        if (!rc_binding_get(i,&bindings[i]) || !bindings[i].target ||
            rc_query_protection(bindings[i].target,16) != (PROT_READ|PROT_EXEC))
            infra("owned target before snapshot");
        memcpy(before[i],(void *)bindings[i].target,16);
    }
}
static bool raw_failure(rc_raw r, int error, bool injected) {
    return r.attempted && r.returned && r.result == -1 && r.errno_valid &&
           r.error == error && r.injected == injected &&
           r.underlying_called == !injected;
}
static bool raw_success(rc_raw r) {
    return r.attempted && r.returned && r.result == 0 &&
           r.underlying_called && !r.injected && !r.errno_valid;
}
static const rc_mapping *trampoline(const rc_attempt *a) {
    for (unsigned i=0;i<a->mapping_count && i<RC_MAPS;++i)
        if (a->mappings[i].kind == RC_MAP_TRAMPOLINE) return &a->mappings[i];
    return NULL;
}
static bool no_publication(const rc_attempt *a) {
    return !a->metadata_published && !a->original_published && !a->redirect_published;
}
static bool unchanged(unsigned binding) {
    return __atomic_load_n(bindings[binding].original,__ATOMIC_ACQUIRE) == NULL &&
           rc_query_protection(bindings[binding].target,16) == (PROT_READ|PROT_EXEC) &&
           memcmp(before[binding],(void *)bindings[binding].target,16) == 0;
}
static uint32_t word(const void *p) { uint32_t n; memcpy(&n,p,4); return n; }
static uint64_t quad(const void *p) { uint64_t n; memcpy(&n,p,8); return n; }
static uintptr_t adrp_page(uint32_t instruction, uintptr_t pc) {
    uint64_t bits=((uint64_t)((instruction>>5)&0x7ffffu)<<2)|((instruction>>29)&3u);
    int64_t displacement=(bits&(UINT64_C(1)<<20)) ?
        (int64_t)bits-(INT64_C(1)<<21) : (int64_t)bits;
    return (uintptr_t)((pc & ~(uintptr_t)4095) + (uintptr_t)(displacement*4096));
}
static bool relocated(const rc_attempt *a, const unsigned char *code) {
    uint32_t original=a->emission.source_words[0];
    if ((original&UINT32_C(0x9f000000)) != UINT32_C(0x90000000))
        return a->emission.out == 1 && word(code) == original;
    uintptr_t wanted=adrp_page(original,a->emission.target);
    if (a->emission.out == 1) {
        uint32_t got=word(code);
        return (got&UINT32_C(0x9f00001f)) == (original&UINT32_C(0x9f00001f)) &&
               adrp_page(got,a->emission.trampoline) == wanted;
    }
    if (a->emission.out != 4) return false;
    uint64_t value=0;
    for (unsigned i=0;i<4;++i) {
        uint32_t got=word(code+4*i);
        uint32_t opcode=(i?UINT32_C(0xf2800000):UINT32_C(0xd2800000)) | (i<<21);
        if ((got&UINT32_C(0xffe0001f)) != (opcode|(original&31u))) return false;
        value|=(uint64_t)((got>>5)&0xffffu)<<(16*i);
    }
    return value == wanted;
}
static bool jump(const unsigned char *code, uintptr_t destination) {
    return word(code) == UINT32_C(0x58000051) &&
           word(code+4) == UINT32_C(0xd61f0220) && quad(code+8) == destination;
}
/* Admission for a scalar invocation, not a semantic check weakened for controls.
 * Derive byte extents from actual relocation, independently of attribution spans.
 * No route is entered after an unsafe/malformed/unreadable/non-RX observation. */
static bool safe_route(const rc_attempt *a, unsigned binding) {
    const rc_metadata *m=&a->emission;
    if (!a->redirect_published || !a->original_published || m->binding != binding ||
        m->target != bindings[binding].target ||
        m->replacement != (uintptr_t)bindings[binding].replacement ||
        m->original_slot != (uintptr_t)bindings[binding].original ||
        m->ncopy != 1 || (m->out != 1 && m->out != 4) || !m->island || !m->trampoline ||
        (m->island&3u) || (m->trampoline&3u) || m->continuation != m->target+4 ||
        (uintptr_t)__atomic_load_n(bindings[binding].original,__ATOMIC_ACQUIRE) != m->trampoline ||
        memcmp(m->source_words,before[binding],16) != 0) return false;
    size_t used=4u*(m->out+4u);
    if (rc_query_protection(m->target,16) != (PROT_READ|PROT_EXEC) ||
        rc_query_protection(m->island,16) != (PROT_READ|PROT_EXEC) ||
        rc_query_protection(m->trampoline,used) != (PROT_READ|PROT_EXEC) ||
        rc_query_protection(m->replacement,4) != (PROT_READ|PROT_EXEC)) return false;
    const unsigned char *target=(void *)m->target,*island=(void *)m->island,*tramp=(void *)m->trampoline;
    uint32_t branch=word(target);
    if ((branch&UINT32_C(0xfc000000)) != UINT32_C(0x14000000)) return false;
    int64_t d=(int64_t)(branch&UINT32_C(0x03ffffff));
    if (d&(INT64_C(1)<<25)) d-=INT64_C(1)<<26;
    uintptr_t destination=(uintptr_t)(m->target+(uintptr_t)(d*4));
    return destination == m->island && memcmp(target+4,before[binding]+4,12) == 0 &&
           jump(island,m->replacement) && relocated(a,tramp) &&
           jump(tramp+4*m->out,m->continuation);
}
static long invoke(unsigned binding, long value) {
    switch (binding) {
    case 0: return rc_target0(value);
    case 1: return rc_target1(value);
    case 2: return rc_target2(value);
    default: infra("alias binding never invoked");
    }
    return 0;
}
static bool metadata_bounds(const rc_attempt *a) {
    const rc_metadata *m=&a->emission;
    rc_metadata found;
    bool endpoints=m->target_exclusion.begin == m->target && m->target_exclusion.end == m->target+16 &&
        m->island_instructions.begin == m->island && m->island_instructions.end == m->island+8 &&
        m->island_storage.begin == m->island && m->island_storage.end == m->island+16 &&
        m->trampoline_instructions.begin == m->trampoline &&
        m->trampoline_instructions.end == m->trampoline+4*(m->out+2) &&
        m->trampoline_storage.begin == m->trampoline &&
        m->trampoline_storage.end == m->trampoline+4*(m->out+4);
    return endpoints && rc_lookup(m->island,&found) && rc_lookup(m->island+7,&found) &&
        !rc_lookup(m->island+8,&found) && !rc_lookup(m->island+15,&found) &&
        rc_lookup(m->trampoline+4*(m->out+2)-1,&found) &&
        !rc_lookup(m->trampoline+4*(m->out+2),&found) &&
        !rc_lookup(m->trampoline+4*(m->out+4),&found);
}
static bool burned_slot(const rc_attempt *a) {
    return a->island_slot_burned && a->pool_base && a->pool_length == 65536 &&
        a->pool_slot < 4096 && a->emission.island == a->pool_base+16u*a->pool_slot &&
        a->emission.island_instructions.begin == a->emission.island &&
        a->emission.island_instructions.end == a->emission.island+8 &&
        a->emission.island_storage.begin == a->emission.island &&
        a->emission.island_storage.end == a->emission.island+16;
}
static bool mapping_returns(const rc_attempt *a) {
    if (a->mapping_count == 0 || a->mapping_count > RC_MAPS || !burned_slot(a) ||
        !a->trampoline_prewrite_validated || !a->prepared || !a->permit_validated) return false;
    const rc_mapping *t=trampoline(a);
    if (!t || !t->held || t->address != a->emission.trampoline || t->length != 4096 ||
        !t->allocation.underlying_called || t->allocation.injected || !t->allocation.returned ||
        (uintptr_t)t->allocation.result != t->address || t->cleanup.attempted) return false;
    for (unsigned i=0;i<a->mapping_count;++i) {
        const rc_mapping *m=&a->mappings[i];
        if (!m->allocation.attempted || !m->allocation.returned) return false;
        if (m->accepted_pool && (!m->held || m->length != 65536 || m->kind != RC_MAP_ISLAND))
            return false;
    }
    return true;
}

/* Explicit fixed-gate scheduling. The hook never reenters receipt APIs and is
 * absent in the standalone configuration. Five-second observation limits and
 * fifteen-second release bounds are infrastructure limits, not latency proof. */
typedef struct { rc_test_point point; unsigned binding; bool has_attempt; rc_attempt attempt; } checkpoint;
static struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool hold, held, release, failed, cm_busy, writer_busy;
    rc_test_point hold_point;
    unsigned hold_binding;
    checkpoint events[64];
    size_t count;
} gate={.mutex=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER};
static struct timespec until(unsigned seconds) {
    struct timespec t;
    if (clock_gettime(CLOCK_REALTIME,&t) != 0) infra("clock");
    t.tv_sec+=(time_t)seconds;
    return t;
}
void rc_fixture_checkpoint(rc_test_point point, unsigned binding, const rc_attempt *attempt) {
    if (pthread_mutex_lock(&gate.mutex) != 0) abort();
    if (gate.count<64) {
        checkpoint *e=&gate.events[gate.count++];
        e->point=point; e->binding=binding; e->has_attempt=attempt != NULL;
        if (attempt) e->attempt=*attempt;
    } else gate.failed=true;
    if (point == RC_TEST_CM_BUSY) gate.cm_busy=true;
    if (point == RC_TEST_WRITER_BUSY) gate.writer_busy=true;
    if (gate.hold && point == gate.hold_point && binding == gate.hold_binding && !gate.held) {
        gate.held=true;
        pthread_cond_broadcast(&gate.cond);
        struct timespec end=until(15);
        while (!gate.release) {
            int rc=pthread_cond_timedwait(&gate.cond,&gate.mutex,&end);
            if (rc != 0) { gate.failed=true; gate.release=true; break; }
        }
    }
    pthread_cond_broadcast(&gate.cond);
    if (pthread_mutex_unlock(&gate.mutex) != 0) abort();
}
static bool wait_gate(bool busy) {
    pthread_mutex_lock(&gate.mutex);
    struct timespec end=until(5);
    bool got;
    for (;;) {
        got=busy?gate.cm_busy:gate.held;
        if (got || gate.failed) break;
        if (pthread_cond_timedwait(&gate.cond,&gate.mutex,&end) != 0) break;
    }
    pthread_mutex_unlock(&gate.mutex);
    return got;
}
static void release_gate(void) {
    pthread_mutex_lock(&gate.mutex); gate.release=true;
    pthread_cond_broadcast(&gate.cond); pthread_mutex_unlock(&gate.mutex);
}
typedef struct { rc_epoch epoch; unsigned binding; bool close; rc_status status; uint64_t nhooks; rc_attempt attempt; } work;
static void *worker(void *context) {
    work *w=context;
    if (w->close) w->status=rc_close_epoch(&w->epoch,&w->nhooks);
    else { rc_request r=request(w->binding,0); w->status=rc_install_community(&w->epoch,w->binding,&r,&w->attempt); }
    return NULL;
}
static void join_thread(pthread_t t) { if (pthread_join(t,NULL) != 0) infra("join"); }

static void owned_binding(void) {
    rc_request wrong=request(0,0); rc_attempt a,b,c,d;
    wrong.replacement=bindings[1].replacement;
    rc_status ra=rc_install_core(0,&wrong,&a); save(a);
    wrong=request(0,0); wrong.original=(void **)(uintptr_t)1;
    rc_status rb=rc_install_core(0,&wrong,&b); save(b);
    void *temporary=NULL;
    wrong=request(0,0); wrong.original=&temporary;
    rc_status rc=rc_install_core(0,&wrong,&c); save(c);
    __atomic_store_n(bindings[0].original,(void *)bindings[0].target,__ATOMIC_RELEASE);
    wrong=request(0,0);
    rc_status rd=rc_install_core(0,&wrong,&d); save(d);
    bool all_invisible=no_publication(&a)&&no_publication(&b)&&no_publication(&c)&&no_publication(&d);
    check(ra == RC_INVALID && !a.installer_entered,"wrong-replacement-before-installer");
    check(rb == RC_INVALID && !b.installer_entered,"untrusted-slot-never-dereferenced");
    check(rc == RC_INVALID && temporary == NULL && !c.installer_entered,"temporary-slot-never-adopted");
    check(rd != RC_OK && rd != RC_WARNINGS && !d.installer_entered &&
          (uintptr_t)__atomic_load_n(bindings[0].original,__ATOMIC_ACQUIRE) == bindings[0].target,
          "occupied-genuine-slot-is-retained");
    check(all_invisible && !a.mapping_count && !b.mapping_count && !c.mapping_count && !d.mapping_count &&
          memcmp(before[0],(void *)bindings[0].target,16) == 0,"invalid-binding-has-no-native-side-effects");
}
static void community_emission(void) {
    rc_epoch epoch=open_epoch(); rc_attempt a; rc_status status=community(epoch,0,0,&a);
    bool safe=safe_route(&a,0);
    long value=safe?invoke(0,7):0;
    uint64_t nhooks=0; rc_status closed=rc_close_epoch(&epoch,&nhooks);
    check(status == RC_OK && a.installer_result == 1 && mapping_returns(&a),"actual-mapping-and-emission-return");
    check(a.emission.owner.kind == RC_OWNER_COMMUNITY &&
          strcmp(a.emission.owner.id,"receipt-community") == 0 && closed == RC_OK &&
          nhooks == 1 && a.nhooks_accounted,"community-owner-and-accounting");
    check((a.emission.source_words[0]&UINT32_C(0x9f000000)) == UINT32_C(0x90000000) &&
          safe && (a.emission.out == 1 || a.emission.out == 4),"actual-adrp-relocation-and-route");
    check(metadata_bounds(&a),"exact-instruction-and-storage-boundaries");
    check(a.metadata_order == 1 && a.original_order == 2 && a.redirect_order == 3 &&
          a.original_saw_metadata && a.redirect_saw_original,"metadata-precedes-original-and-redirect");
    check(safe && value == 327 && rc_target_hits[0] == 1 && rc_replacement_hits[0] == 1,
          "scalar-forwarding-once-after-safe-gate");
}
static void core_emission(void) {
    rc_attempt a; rc_status status=core(1,0,&a); bool safe=safe_route(&a,1);
    long value=safe?invoke(1,9):0;
    rc_metadata m; bool looked=rc_lookup(a.emission.island,&m);
    check(status == RC_OK && a.installer_result == 1 && mapping_returns(&a),"core-actual-emission-return");
    check(looked && m.owner.kind == RC_OWNER_CORE && m.owner.id[0] == 0 &&
          !a.nhooks_accounted,"core-is-not-community-identity");
    check(safe && a.emission.out == 1 && metadata_bounds(&a),"core-exact-owned-route");
    check(safe && value == 331 && rc_target_hits[1] == 1 && rc_replacement_hits[1] == 1,
          "core-scalar-forwarding-once");
}
static void refusal(uint64_t fault, rc_status expected) {
    rc_attempt a; rc_status status=core(0,fault,&a);
    const rc_mapping *t=trampoline(&a);
    bool selected=false;
    if (fault == RC_F_TRAMPOLINE_MAP) selected=t && raw_failure(t->allocation,ENOMEM,true);
    if (fault == RC_F_SEAL) selected=raw_failure(a.seal,EIO,true) && t && raw_success(t->cleanup) && !t->held;
    if (fault == RC_F_ISLAND_UNLOCK) selected=raw_failure(a.island_protection.unlock,EIO,true) &&
        !a.island_protection.token_valid && t && raw_success(t->cleanup) && !t->held;
    if (fault == RC_F_TARGET_UNLOCK) selected=raw_failure(a.target_protection.unlock,EIO,true) &&
        !a.target_protection.token_valid && t && raw_success(t->cleanup) && !t->held;
    if (fault == RC_F_ISLAND_MAP) {
        /* The six authored hint candidates are not six guaranteed calls: the
         * pinned allocator skips rounded hints below 1 MiB. This independent
         * literal list verifies actual attempted hint order and bounded count. */
        static const intptr_t offsets[]={-1048576,-4194304,-16777216,-67108864,67108864,104857600};
        unsigned attempted=0;
        selected=t == NULL;
        for (unsigned i=0;i<6;++i) {
            uintptr_t hint=(bindings[0].target+(uintptr_t)offsets[i])&~(uintptr_t)65535;
            if (hint<1048576) continue;
            if (attempted>=a.mapping_count || attempted>=RC_MAPS) selected=false;
            else {
                const rc_mapping *m=&a.mappings[attempted];
                selected=selected && m->kind == RC_MAP_ISLAND && m->hint == hint &&
                    m->length == 65536 && raw_failure(m->allocation,ENOMEM,true);
            }
            ++attempted;
        }
        selected=selected && attempted>0 && attempted<=6 && a.mapping_count == attempted;
    }
    rc_metadata m;
    check(status == expected && a.status == expected && selected,"selected-precall-refusal-is-explicit");
    check(no_publication(&a) && unchanged(0) &&
          (!a.emission.island || !rc_lookup(a.emission.island,&m)),
          "refusal-retains-unchanged-unreachable-route");
    check(a.mapping_count<=RC_MAPS && (fault == RC_F_ISLAND_MAP ? !a.island_slot_burned && !a.pool_base && !a.pool_length : burned_slot(&a)) &&
          !a.target_cache_armed,"refusal-keeps-bounded-resource-history");
}
static void map_refusal(void) { refusal(RC_F_TRAMPOLINE_MAP,RC_MAPPING); }
static void seal_refusal(void) { refusal(RC_F_SEAL,RC_SEAL); }
static void island_refusal(void) { refusal(RC_F_ISLAND_UNLOCK,RC_ISLAND_UNLOCK); }
static void target_refusal(void) { refusal(RC_F_TARGET_UNLOCK,RC_TARGET_UNLOCK); }
static void no_island(void) { refusal(RC_F_ISLAND_MAP,RC_NO_ISLAND); }
static void cleanup_uncertainty(void) {
    rc_attempt a; rc_status status=core(0,RC_F_SEAL|RC_F_CLEANUP,&a);
    const rc_mapping *t=trampoline(&a);
    int actual=t?rc_query_protection(t->address,t->length):-1;
    check(status == RC_SEAL && raw_failure(a.seal,EIO,true),"seal-refusal-precedes-cleanup");
    check(t && raw_failure(t->cleanup,EIO,true) && t->cleanup_uncertain,"cleanup-failure-is-distinct-return");
    check(t && t->held && actual == (PROT_READ|PROT_WRITE),"failed-cleanup-retains-actual-mapping");
    check(no_publication(&a) && unchanged(0) && a.island_slot_burned,"uncertain-cleanup-never-publishes-or-retries");
}
static void partial_install(void) {
    rc_epoch epoch=open_epoch(); rc_attempt first,second;
    rc_status a=community(epoch,0,0,&first);
    void *original=__atomic_load_n(bindings[0].original,__ATOMIC_ACQUIRE);
    rc_status b=community(epoch,1,RC_F_TARGET_UNLOCK,&second);
    bool safe=safe_route(&first,0); long value=safe?invoke(0,11):0;
    rc_attempt retained; bool snapshot=rc_snapshot(0,&retained);
    uint64_t nhooks=0; rc_status closed=rc_close_epoch(&epoch,&nhooks);
    check(a == RC_OK && b == RC_TARGET_UNLOCK && no_publication(&second) && unchanged(1),
          "second-refusal-does-not-rewrite-first");
    check(snapshot && retained.metadata_published &&
          __atomic_load_n(bindings[0].original,__ATOMIC_ACQUIRE) == original,
          "first-original-and-journal-remain-retained");
    check(safe && value == 331 && rc_target_hits[0] == 1 && rc_replacement_hits[0] == 1,
          "first-route-still-forwards-once");
    check(closed == RC_OK && nhooks == 1 && first.nhooks_accounted && !second.nhooks_accounted,
          "partial-claim-accounting-is-not-two");
}
static void protection_warnings(void) {
    rc_attempt first,second;
    rc_status a=core(0,RC_F_ISLAND_RELOCK|RC_F_TARGET_RELOCK,&first);
    int target0=rc_query_protection(bindings[0].target,16);
    int island0=rc_query_protection(first.emission.island,16);
    rc_status b=core(1,0,&second);
    const rc_protection *i=&first.island_protection,*t=&first.target_protection;
    check(a == RC_WARNINGS && first.installer_result == 1 && first.metadata_published &&
          first.original_published && first.redirect_published,"warnings-remain-published-success");
    check(i->token_valid && t->token_valid && i->base != t->base &&
          i->base == (first.emission.island&~(uintptr_t)4095) &&
          t->base == (first.emission.target&~(uintptr_t)4095) &&
          i->length == 4096 && t->length == 4096 && i->saved_bits == (PROT_READ|PROT_EXEC) &&
          t->saved_bits == (PROT_READ|PROT_EXEC),"island-and-target-token-evidence-is-distinct");
    check(raw_failure(i->relock,EIO,true) && raw_failure(t->relock,EIO,true) &&
          target0 == (PROT_READ|PROT_WRITE|PROT_EXEC) && island0 == (PROT_READ|PROT_WRITE|PROT_EXEC),
          "injected-relock-refusals-match-real-maps");
    check(b == RC_OK && second.island_protection.saved_bits == (PROT_READ|PROT_WRITE|PROT_EXEC) &&
          raw_success(second.island_protection.relock) &&
          second.island_protection.base == (first.emission.island&~(uintptr_t)4095) &&
          rc_query_protection(second.emission.island,16) == (PROT_READ|PROT_WRITE|PROT_EXEC),
          "successful-relock-can-restore-prior-writable-page");
    check(!safe_route(&first,0) && !safe_route(&second,1) &&
          rc_target_hits[0] == 0 && rc_target_hits[1] == 0 &&
          rc_replacement_hits[0] == 0 && rc_replacement_hits[1] == 0,
          "non-rx-routes-are-never-invoked");
}
static void queued_close(void) {
    rc_epoch epoch=open_epoch();
    gate.hold=true; gate.hold_point=RC_TEST_BEFORE_ADMISSION; gate.hold_binding=0;
    work w={.epoch=epoch,.binding=0,.status=RC_INVALID}; pthread_t thread;
    bool started=pthread_create(&thread,NULL,worker,&w) == 0;
    bool held=started && wait_gate(false); uint64_t count=99;
    rc_status closed=held?rc_close_epoch(&epoch,&count):RC_INVALID;
    release_gate(); if (started) join_thread(thread); save(w.attempt);
    check(started && held && !gate.failed && closed == RC_OK && count == 0,"queued-close-schedule-completes");
    check(w.status == RC_EPOCH && !w.attempt.installer_entered && !w.attempt.mapping_count,
          "closed-epoch-never-enters-real-installer");
    check(no_publication(&w.attempt) && unchanged(0),"queued-close-has-no-native-side-effects");
}
static void admitted_close(void) {
    rc_epoch epoch=open_epoch();
    gate.hold=true; gate.hold_point=RC_TEST_BEFORE_UNLOCK; gate.hold_binding=0;
    work install={.epoch=epoch,.binding=0,.status=RC_INVALID};
    work close={.epoch=epoch,.close=true,.status=RC_INVALID};
    pthread_t a,b;
    bool started=pthread_create(&a,NULL,worker,&install) == 0;
    bool held=started && wait_gate(false);
    bool closing=held && pthread_create(&b,NULL,worker,&close) == 0;
    bool busy=closing && wait_gate(true);
    release_gate(); if (started) join_thread(a); if (closing) join_thread(b); save(install.attempt);
    rc_attempt later; rc_status refused=community(epoch,1,0,&later);
    bool safe=safe_route(&install.attempt,0); long value=safe?invoke(0,13):0;
    check(started && closing && held && busy && !gate.failed,"real-admission-mutex-contention");
    check(install.status == RC_OK && close.status == RC_OK && close.nhooks == 1 &&
          install.attempt.nhooks_accounted,"publication-accounted-before-epoch-close");
    check(refused == RC_EPOCH && !later.installer_entered && !later.mapping_count && unchanged(1),
          "later-claim-refuses-before-native-entry");
    check(safe && value == 333 && rc_target_hits[0] == 1,"admitted-route-forwards-after-joins");
}
static void common_target(void) {
    rc_attempt first,refused; rc_status a=core(0,0,&first);
    rc_epoch epoch=open_epoch(); rc_status b=community(epoch,3,0,&refused);
    uint64_t count=99; rc_status closed=rc_close_epoch(&epoch,&count);
    bool safe=safe_route(&first,0); long value=safe?invoke(0,17):0;
    check(bindings[3].target == bindings[0].target && bindings[3].original != bindings[0].original &&
          bindings[3].replacement != bindings[0].replacement,"fixed-authored-alias-reaches-common-ledger");
    check(a == RC_OK && b == RC_CONFLICT && !refused.installer_entered && !refused.mapping_count &&
          no_publication(&refused) && __atomic_load_n(bindings[3].original,__ATOMIC_ACQUIRE) == NULL,
          "core-target-excludes-community-before-installer");
    check(closed == RC_OK && count == 0 && safe && value == 337 &&
          rc_target_hits[0] == 1 && rc_replacement_hits[0] == 1,"core-publication-survives-alias-refusal");
}
static void diagnostic_capacity(void) {
    rc_fixture_saturate_diagnostics();
    rc_attempt a; rc_status status=core(1,0,&a);
    bool safe=safe_route(&a,1); long value=safe?invoke(1,19):0;
    check(status == RC_OK && a.installer_result == 1 && a.metadata_published,"authoritative-publication-ignores-diagnostic-capacity");
    check(a.diagnostic_before >= 256 && a.diagnostic_after > a.diagnostic_before &&
          a.diagnostic_lost > 0,"diagnostic-saturation-is-explicit");
    check(safe && value == 341 && rc_target_hits[1] == 1 && rc_replacement_hits[1] == 1,
          "diagnostic-loss-keeps-valid-original-forwarding");
}
static void rounding_input(void) {
    uintptr_t base=99; size_t length=77;
    bool zero=!rc_checked_pages(0,4,4096,&base,&length);
    bool badsize=!rc_checked_pages(0x1000,4,0,&base,&length) &&
                 !rc_checked_pages(0x1000,4,-1,&base,&length) &&
                 !rc_checked_pages(0x1000,4,3000,&base,&length);
    bool overflow=!rc_checked_pages(UINTPTR_MAX-3,8,4096,&base,&length) &&
                  !rc_checked_pages(UINTPTR_MAX-7,4,4096,&base,&length);
    bool valid=rc_checked_pages(0x1003,4096,page_size,&base,&length);
    bool untouched=true; for(unsigned i=0;i<4;++i) untouched=untouched&&unchanged(i);
    check(zero && badsize,"invalid-rounding-values-refuse");
    check(overflow,"endpoint-and-roundup-overflow-refuse");
    check(valid && base == 0x1000 && length == 8192,"selected-page-size-binds-rounded-interval");
    check(untouched && saved_count == 0,"rounding-controls-never-enter-native-installer");
}

/* Bounded private JSON evidence. All actual addresses/raw bytes remain here,
 * not in stdout or the sanitized public receipt. No source file is created. */
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
static void strings(FILE *f,const char *const *items,size_t count) {
    fputc('[',f);for(size_t i=0;i<count;++i){if(i)fputc(',',f);js(f,items[i]);}fputc(']',f);
}
static void report_json(FILE *f,const char *selector) {
    fputs("{\"selector\":",f);js(f,selector);fputs(",\"check_names\":",f);strings(f,check_names,checks);
    fprintf(f,",\"checks\":%zu,\"failures\":%zu,\"failed_checks\":",checks,failures);
    strings(f,failed_checks,failures);fputc('}',f);
}
static void write_evidence(const char *path,const char *selector) {
    FILE *f=fopen(path,"wx");if(!f)infra("exclusive evidence file");
    fprintf(f,"{\"schema\":1,\"selector\":");js(f,selector);
    fprintf(f,",\"page_size\":%ld,\"bindings\":[",page_size);
    for(unsigned i=0;i<RC_BINDINGS;++i) {
        if(i)fputc(',',f);
        fprintf(f,"{\"id\":%u,\"target\":%" PRIuPTR ",\"replacement\":%" PRIuPTR ",\"original_slot\":%" PRIuPTR ",\"original_value\":%" PRIuPTR ",\"target_protection\":%d,\"before\":",
            i,bindings[i].target,(uintptr_t)bindings[i].replacement,(uintptr_t)bindings[i].original,
            (uintptr_t)__atomic_load_n(bindings[i].original,__ATOMIC_ACQUIRE),rc_query_protection(bindings[i].target,16));
        hex(f,before[i],16);fputs(",\"after\":",f);hex(f,(void *)bindings[i].target,16);fputc('}',f);
    }
    fputs("],\"attempts\":[",f);
    for(size_t i=0;i<saved_count;++i){if(i)fputc(',',f);attempt_json(f,&saved[i]);}
    fputs("],\"generated\":[",f);
    for(size_t i=0;i<saved_count;++i) {
        const rc_attempt *a=&saved[i];const rc_metadata *m=&a->emission;
        if(i)fputc(',',f);
        int ip=m->island?rc_query_protection(m->island,16):-1;
        size_t used=m->out<=4?4u*(m->out+4u):0;
        int tp=m->trampoline&&used?rc_query_protection(m->trampoline,used):-1;
        fprintf(f,"{\"attempt_index\":%zu,\"island_protection\":%d,\"trampoline_protection\":%d,\"island_bytes\":",i,ip,tp);
        if(a->redirect_published && ip>=0 && (ip&PROT_READ))hex(f,(void *)m->island,16);else js(f,"");
        fputs(",\"trampoline_bytes\":",f);
        if(a->redirect_published && tp>=0 && (tp&PROT_READ))hex(f,(void *)m->trampoline,used);else js(f,"");
        fputc('}',f);
    }
    fputs("],\"checkpoints\":[",f);
    for(size_t i=0;i<gate.count;++i) {
        checkpoint *e=&gate.events[i];if(i)fputc(',',f);
        fprintf(f,"{\"point\":%u,\"binding\":%u,\"has_attempt\":%d,\"attempt\":",(unsigned)e->point,e->binding,e->has_attempt);
        if(e->has_attempt)attempt_json(f,&e->attempt);else fputs("null",f);fputc('}',f);
    }
    fputs("],\"target_hits\":[",f);
    for(unsigned i=0;i<4;++i)fprintf(f,"%s%lu",i?",":"",rc_target_hits[i]);
    fputs("],\"replacement_hits\":[",f);
    for(unsigned i=0;i<4;++i)fprintf(f,"%s%lu",i?",":"",rc_replacement_hits[i]);
    fprintf(f,"],\"scheduler\":{\"failed\":%d,\"held\":%d,\"released\":%d,\"cm_busy\":%d,\"writer_busy\":%d},\"report\":",
        gate.failed,gate.held,gate.release,gate.cm_busy,gate.writer_busy);
    report_json(f,selector);fputs("}\n",f);
    if(ferror(f) || fclose(f) != 0)infra("evidence write");
}
typedef struct { const char *name; void (*run)(void); } selector;
static const selector selectors[]={
    {"owned-binding",owned_binding},{"community-emission",community_emission},{"core-emission",core_emission},
    {"trampoline-map-refusal",map_refusal},{"seal-refusal",seal_refusal},{"island-unlock-refusal",island_refusal},
    {"target-unlock-refusal",target_refusal},{"no-island-refusal",no_island},{"cleanup-uncertainty",cleanup_uncertainty},
    {"partial-install",partial_install},{"protection-warnings",protection_warnings},{"queued-close",queued_close},
    {"admitted-close",admitted_close},{"common-target",common_target},{"diagnostic-capacity",diagnostic_capacity},
    {"rounding-input",rounding_input}
};
int main(int argc,char **argv) {
    if(argc != 3)infra("selector and private evidence path required");
    const selector *selected=NULL;
    for(size_t i=0;i<sizeof selectors/sizeof selectors[0];++i)
        if(strcmp(argv[1],selectors[i].name) == 0)selected=&selectors[i];
    if(!selected)infra("unknown selector");
    initialize();
    selected->run();
    write_evidence(argv[2],selected->name);
    report_json(stdout,selected->name);putchar('\n');
    return failures?1:0;
}
