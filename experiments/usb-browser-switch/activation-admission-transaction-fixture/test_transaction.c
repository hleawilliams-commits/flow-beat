/* Offline C transaction tests. Only pthread schedules/publication are real;
 * loader, caller, allocation, protection and cleanup outcomes are authored. */
#include "transaction.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef TX_FIXTURE
#error "this harness requires the explicitly instrumented fixture build"
#endif

#define PROCESS UINT64_C(0x1122334455667788)
#define TARGET ((uintptr_t)0x100000)
#define ISLAND ((uintptr_t)0x200000)
#define TRAMP ((uintptr_t)0x300000)
#define MODULE ((uintptr_t)0x500000)
#define REPLACEMENT ((uintptr_t)0x600000)

static const char *names[128], *failures[128];
static size_t nchecks, nfailures;
static void check(bool value, const char *name) {
    if (nchecks == 128) { fputs("FIXTURE_ABORT check capacity\n",stderr); exit(2); }
    names[nchecks++] = name;
    if (!value) failures[nfailures++] = name;
}
static void infrastructure(const char *what) {
    fprintf(stderr,"FIXTURE_ABORT %s\n",what);
    exit(2);
}
static tx_system *create_system(uint64_t instance) {
    tx_system *s = NULL;
    if (tx_create(PROCESS,instance,&s) != TX_OK || !s) infrastructure("create");
    return s;
}
static void destroy_system(tx_system **s) {
    if (tx_destroy(s) != TX_OK || *s) infrastructure("quiescent destroy");
}
static tx_snapshot snapshot(tx_system *s) {
    tx_snapshot out;
    memset(&out,0,sizeof out);
    if (tx_inspect(s,&out) != TX_OK) infrastructure("inspect after joins");
    return out;
}
static tx_provenance provenance(uintptr_t lo, uintptr_t hi) {
    tx_provenance p = {true,true,true,UINT64_C(0xabc123),lo-0x1000,lo,hi};
    return p;
}
static tx_plan plan(unsigned index, bool fallback) {
    tx_plan p;
    memset(&p,0,sizeof p);
    p.target = TARGET + (uintptr_t)index*0x100;
    p.replacement = REPLACEMENT;
    p.island = fallback ? 0 : ISLAND + (uintptr_t)index*0x100;
    p.trampoline = TRAMP + (uintptr_t)index*0x100;
    p.ncopy = fallback ? 4 : 1;
    p.island_saved_bits = 5;
    p.target_saved_bits = 5;
    return p;
}
static tx_epoch open_module(tx_system *s, uint32_t index, const char *id, uintptr_t lo) {
    tx_epoch epoch;
    memset(&epoch,0,sizeof epoch);
    if (tx_record_module(s,index,id,strlen(id),provenance(lo,lo+0x1000)) != TX_OK ||
        tx_record_descriptor(s,index,true,true) != TX_OK ||
        tx_open_init(s,index,&epoch) != TX_OK) infrastructure("module setup");
    return epoch;
}
static bool match(tx_match m, tx_match_source source, tx_owner_kind kind,
                  uint32_t index, const char *id) {
    return m.source == source && m.owner.kind == kind && m.owner.process_id == PROCESS &&
           m.owner.module_index == index && strcmp(m.owner.id,id) == 0;
}
static bool no_match(tx_match m) { return m.source == TX_NO_MATCH; }
static bool raw_is(tx_raw r, int result, int error) { return r.result == result && r.error == error; }
static bool unpublished(const tx_receipt *r) {
    return !r->metadata_published && !r->original_published && !r->redirect_published;
}

/* Scheduling callback is instrumentation, including the extra trylock which
 * reports real EBUSY. Never reenter a mutator or blocking inspector here. */
typedef struct {
    tx_test_point point;
    tx_lock_kind lock;
    int raw;
    bool has_receipt;
    tx_receipt receipt;
    tx_match visible;
} event;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    tx_system *system;
    uintptr_t probe;
    tx_test_point hold_point;
    bool hold_enabled, held, release, failed, cm_busy, writer_busy;
    event events[128];
    size_t count;
} gate;
static struct timespec deadline(unsigned seconds) {
    struct timespec time;
    if (timespec_get(&time,TIME_UTC) != TIME_UTC) infrastructure("clock");
    time.tv_sec += (time_t)seconds;
    return time;
}
static void hook(tx_test_point point, tx_lock_kind lock, int raw,
                 const tx_receipt *receipt, void *context) {
    gate *g = context;
    if (pthread_mutex_lock(&g->mutex) != 0) abort();
    if (g->count < 128) {
        event *e = &g->events[g->count++];
        memset(e,0,sizeof *e);
        e->point = point; e->lock = lock; e->raw = raw;
        e->has_receipt = receipt != NULL;
        if (receipt) e->receipt = *receipt;
        e->visible = tx_lookup(g->system,g->probe);
    } else g->failed = true;
    if (point == TX_TEST_LOCK_BUSY && raw == EBUSY) {
        if (lock == TX_LOCK_CM) g->cm_busy = true;
        if (lock == TX_LOCK_WRITER) g->writer_busy = true;
    }
    if (g->hold_enabled && point == g->hold_point && !g->held) {
        g->held = true;
        pthread_cond_broadcast(&g->cond);
        const struct timespec until = deadline(15);
        while (!g->release) {
            int rc = pthread_cond_timedwait(&g->cond,&g->mutex,&until);
            if (rc != 0) { g->failed = true; g->release = true; break; }
        }
    }
    pthread_cond_broadcast(&g->cond);
    if (pthread_mutex_unlock(&g->mutex) != 0) abort();
}
static void gate_init(gate *g, tx_system *s, bool hold, tx_test_point point, uintptr_t probe) {
    memset(g,0,sizeof *g);
    g->system=s; g->hold_enabled=hold; g->hold_point=point; g->probe=probe;
    if (pthread_mutex_init(&g->mutex,NULL) != 0 || pthread_cond_init(&g->cond,NULL) != 0)
        infrastructure("gate initialization");
    if (tx_set_test_hook(s,hook,g) != TX_OK) infrastructure("set hook");
}
static bool gate_wait(gate *g, unsigned which) {
    if (pthread_mutex_lock(&g->mutex) != 0) infrastructure("gate lock");
    const struct timespec until = deadline(5);
    bool ready = false;
    for (;;) {
        ready = which == 0 ? g->held : which == 1 ? g->cm_busy : g->writer_busy;
        if (ready || g->failed) break;
        if (pthread_cond_timedwait(&g->cond,&g->mutex,&until) != 0) break;
    }
    pthread_mutex_unlock(&g->mutex);
    return ready;
}
static void gate_release(gate *g) {
    pthread_mutex_lock(&g->mutex);
    g->release=true;
    pthread_cond_broadcast(&g->cond);
    pthread_mutex_unlock(&g->mutex);
}
static void gate_destroy(gate *g) {
    if (pthread_cond_destroy(&g->cond) != 0 || pthread_mutex_destroy(&g->mutex) != 0)
        infrastructure("gate destruction after joins");
}
static size_t first_event(const gate *g, tx_test_point point) {
    for (size_t i=0;i<g->count;++i) if (g->events[i].point == point) return i;
    return g->count;
}
typedef enum { WORK_COMMUNITY, WORK_CORE, WORK_CLOSE, WORK_LATE } work_kind;
typedef struct {
    tx_system *system;
    tx_epoch epoch;
    tx_plan plan;
    work_kind kind;
    int init_result;
    tx_error result;
    tx_receipt receipt;
} work;
static void *worker(void *context) {
    work *w=context;
    switch (w->kind) {
    case WORK_COMMUNITY:
        w->result=tx_install_community(w->system,&w->epoch,MODULE+8,&w->plan,&w->receipt); break;
    case WORK_CORE:
        w->result=tx_install_core(w->system,&w->plan,&w->receipt); break;
    case WORK_CLOSE:
        w->result=tx_close_init(w->system,&w->epoch,w->init_result); break;
    case WORK_LATE:
        w->result=tx_go_late(w->system); break;
    }
    return NULL;
}
static work make_work(tx_system *s, work_kind kind, tx_epoch epoch, tx_plan p) {
    work w;
    memset(&w,0,sizeof w);
    w.system=s; w.kind=kind; w.epoch=epoch; w.plan=p; w.result=TX_INVALID;
    return w;
}
static bool launch(pthread_t *thread, work *w) { return pthread_create(thread,NULL,worker,w) == 0; }
static void join(pthread_t thread) { if (pthread_join(thread,NULL) != 0) infrastructure("thread join"); }

static void queued_close(void) {
    tx_system *s=create_system(1);
    gate g; gate_init(&g,s,true,TX_TEST_BEFORE_ADMISSION,ISLAND);
    if (tx_loader_begin(s) != TX_OK) infrastructure("loader begin");
    tx_epoch epoch=open_module(s,0,"alpha",MODULE);
    work claim=make_work(s,WORK_COMMUNITY,epoch,plan(0,false));
    pthread_t thread;
    bool started=launch(&thread,&claim), held=started && gate_wait(&g,0);
    tx_error closed=held ? tx_close_init(s,&epoch,0) : TX_INVALID;
    gate_release(&g);
    if (started) join(thread);
    tx_snapshot snap=snapshot(s);
    bool clean=!g.failed;
    gate_destroy(&g); destroy_system(&s);
    check(started && held && clean && closed == TX_OK,"queued-close-completes");
    check(claim.result == TX_EPOCH_CLOSED && !claim.receipt.admitted,"queued-claim-refused");
    check(unpublished(&claim.receipt) && !claim.receipt.resources.allocation_attempted &&
          snap.reserved_slots == 0 && snap.modules[0].nhooks == 0 && snap.modules[0].init_finished,
          "queued-claim-has-no-side-effects");
}
static void admitted_close(void) {
    tx_system *s=create_system(2);
    gate g; gate_init(&g,s,true,TX_TEST_RESERVED,ISLAND);
    if (tx_loader_begin(s) != TX_OK) infrastructure("loader begin");
    tx_epoch epoch=open_module(s,0,"alpha",MODULE);
    work claim=make_work(s,WORK_COMMUNITY,epoch,plan(0,false));
    work close=make_work(s,WORK_CLOSE,epoch,plan(0,false));
    pthread_t a,b;
    bool launched_a=launch(&a,&claim), held=launched_a && gate_wait(&g,0);
    bool launched_b=held && launch(&b,&close);
    bool busy=launched_b && gate_wait(&g,1);
    gate_release(&g);
    if (launched_a) join(a);
    if (launched_b) join(b);
    tx_snapshot snap=snapshot(s);
    size_t cut=first_event(&g,TX_TEST_AFTER_CUT), closed=first_event(&g,TX_TEST_CLOSED);
    bool ordered=cut<closed && closed<g.count, clean=!g.failed;
    tx_receipt refused;
    tx_plan next=plan(1,false);
    tx_error again=tx_install_community(s,&epoch,MODULE+8,&next,&refused);
    gate_destroy(&g); destroy_system(&s);
    check(launched_a && launched_b && held && busy && clean,"real-cm-contention");
    check(claim.result == TX_OK && close.result == TX_OK && ordered,"claim-completes-before-close");
    check(claim.receipt.nhooks_accounted && snap.modules[0].nhooks == 1 &&
          snap.modules[0].init_finished && snap.slots[0].published,"accounting-retained-at-close");
    check(again == TX_EPOCH_CLOSED && unpublished(&refused),"epoch-stays-closed");
}
static void common_admission(void) {
    tx_system *s=create_system(3);
    if (tx_loader_begin(s) != TX_OK) infrastructure("loader begin");
    tx_epoch epoch=open_module(s,0,"alpha",MODULE);
    tx_receipt first,overlap,second,reverse,full;
    tx_plan p=plan(0,false), q=plan(1,true), conflict=plan(9,false);
    tx_error a=tx_install_community(s,&epoch,MODULE+8,&p,&first);
    conflict.target=p.target+4;
    tx_error b=tx_install_core(s,&conflict,&overlap);
    q.target=p.target+16;
    tx_error c=tx_install_core(s,&q,&second);
    conflict.target=q.target+4;
    tx_error d=tx_install_community(s,&epoch,MODULE+8,&conflict,&reverse);
    bool filled=true;
    for (unsigned i=2;i<TX_SLOTS;++i) {
        tx_plan next=plan(i,false);
        tx_receipt r;
        tx_error rc=i%2 ? tx_install_core(s,&next,&r) :
                         tx_install_community(s,&epoch,MODULE+8,&next,&r);
        filled=rc == TX_OK && r.metadata_published && filled;
    }
    tx_plan ninth=plan(20,false);
    tx_error exhausted=tx_install_core(s,&ninth,&full);
    tx_snapshot snap=snapshot(s);
    destroy_system(&s);
    check(a == TX_OK && b == TX_TARGET_CONFLICT && !overlap.reserved &&
          !overlap.resources.allocation_attempted,"community-target-excludes-core");
    check(c == TX_OK && d == TX_TARGET_CONFLICT && !reverse.reserved &&
          !reverse.resources.allocation_attempted,"core-target-excludes-community");
    check(filled && exhausted == TX_CAPACITY && !full.resources.allocation_attempted &&
          !full.reserved,"common-capacity-refuses-before-allocation");
    check(snap.reserved_slots == TX_SLOTS && snap.modules[0].nhooks == 4 &&
          first.owner.kind == TX_COMMUNITY && second.owner.kind == TX_CORE,
          "common-owner-and-capacity-accounting");
}
static void fallback_phase(void) {
    tx_system *s=create_system(4);
    tx_epoch empty={0};
    gate g; gate_init(&g,s,true,TX_TEST_RESERVED,TRAMP);
    work claim=make_work(s,WORK_CORE,empty,plan(0,true));
    work late=make_work(s,WORK_LATE,empty,plan(0,false));
    pthread_t a,b;
    bool launched_a=launch(&a,&claim), held=launched_a && gate_wait(&g,0);
    bool launched_b=held && launch(&b,&late), busy=launched_b && gate_wait(&g,2);
    gate_release(&g);
    if (launched_a) join(a);
    if (launched_b) join(b);
    size_t cut=first_event(&g,TX_TEST_AFTER_CUT), transition=first_event(&g,TX_TEST_LATE);
    bool ordered=cut<transition && transition<g.count, clean=!g.failed;
    tx_plan fallback=plan(1,true), island=plan(2,false);
    tx_receipt denied, accepted;
    tx_error refusal=tx_install_core(s,&fallback,&denied);
    tx_error success=tx_install_core(s,&island,&accepted);
    tx_snapshot snap=snapshot(s);
    gate_destroy(&g); destroy_system(&s);
    check(launched_a && launched_b && held && busy && clean,"real-writer-contention");
    check(claim.result == TX_OK && late.result == TX_OK && ordered &&
          claim.receipt.layout.ncopy == 4 && snap.late,"fallback-completes-before-late");
    check(refusal == TX_LATE && !denied.reserved && !denied.resources.allocation_attempted &&
          unpublished(&denied),"late-fallback-refuses-before-reserve");
    check(success == TX_OK && accepted.metadata_published && accepted.late_at_admission,
          "island-remains-eligible-late");
}
static void permit_boundary(void) {
    tx_system *s=create_system(5);
    const tx_permit_fault faults[]={TX_PERMIT_WRONG_SYSTEM,TX_PERMIT_WRONG_SLOT,
                                   TX_PERMIT_WRONG_OWNER,TX_PERMIT_CONSUMED};
    const char *labels[]={"wrong-system-before-unlock","wrong-slot-before-unlock",
                          "wrong-owner-before-unlock","consumed-permit-before-unlock"};
    bool outcomes[4];
    for (unsigned i=0;i<4;++i) {
        tx_plan p=plan(i,false); p.permit_fault=faults[i];
        tx_receipt r;
        tx_error rc=tx_install_core(s,&p,&r);
        outcomes[i]=rc == TX_PERMIT && r.prepared && !r.permit_validated &&
                    !r.target_protection.unlock_attempted && unpublished(&r);
    }
    tx_plan p=plan(4,false); tx_receipt valid;
    tx_error rc=tx_install_core(s,&p,&valid);
    tx_snapshot snap=snapshot(s);
    destroy_system(&s);
    for (unsigned i=0;i<4;++i) check(outcomes[i],labels[i]);
    check(rc == TX_OK && valid.permit_validated && valid.metadata_published &&
          snap.reserved_slots == 5,"fresh-bound-permit-still-works");
}
static void module_provenance(void) {
    tx_system *s=create_system(6);
    if (tx_loader_begin(s) != TX_OK) infrastructure("loader begin");
    tx_error result[3]; tx_match seen[3];
    const char *bad_ids[]={"bad-handle","bad-map","bad-object"};
    for (unsigned i=0;i<3;++i) {
        tx_provenance p=provenance(MODULE+0x2000*i,MODULE+0x2000*i+0x1000);
        if (i==0) p.handle_succeeded=false;
        if (i==1) p.link_map_succeeded=false;
        if (i==2) p.intended_object_matched=false;
        result[i]=tx_record_module(s,i,bad_ids[i],strlen(bad_ids[i]),p);
        seen[i]=tx_lookup(s,p.lo);
    }
    tx_provenance zero=provenance(MODULE+0x10000,MODULE+0x11000); zero.base=0;
    tx_error good=tx_record_module(s,3,"zero",4,zero);
    tx_match accepted=tx_lookup(s,zero.lo);
    tx_error repeat=tx_record_module(s,1,"fixed",5,zero);
    tx_snapshot snap=snapshot(s);
    destroy_system(&s);
    check(result[0] == TX_PROVENANCE && no_match(seen[0]) && snap.modules[0].recorded &&
          !snap.modules[0].published,"failed-handle-not-published");
    check(result[1] == TX_PROVENANCE && no_match(seen[1]) && snap.modules[1].provenance.handle_succeeded &&
          !snap.modules[1].published,"failed-link-map-retains-handle-fact");
    check(result[2] == TX_PROVENANCE && no_match(seen[2]) && !snap.modules[2].published,
          "wrong-object-not-published");
    check(good == TX_OK && match(accepted,TX_MODULE_MATCH,TX_COMMUNITY,3,"zero") &&
          snap.modules[3].provenance.base == 0,"proven-zero-base-is-valid");
    check(repeat == TX_ALREADY_RECORDED && !snap.modules[1].published &&
          !snap.modules[1].provenance.link_map_succeeded,"failed-provenance-is-not-overwritten");
}
static void module_holes(void) {
    tx_system *s=create_system(7);
    gate g; gate_init(&g,s,false,TX_TEST_RESERVED,MODULE+8);
    if (tx_loader_begin(s) != TX_OK) infrastructure("loader begin");
    tx_error later=tx_record_module(s,3,"later",5,provenance(MODULE,MODULE+0x1000));
    tx_match through_hole=tx_lookup(s,MODULE+8);
    tx_error earlier=tx_record_module(s,0,"first",5,provenance(MODULE,MODULE+0x2000));
    tx_match winner=tx_lookup(s,MODULE+8);
    tx_error repeated=tx_record_module(s,0,"changed",7,provenance(MODULE+0x10000,MODULE+0x11000));
    tx_match immutable=tx_lookup(s,MODULE+8);
    size_t publications=0; bool before[2]={false,false};
    for (size_t i=0;i<g.count;++i) if (g.events[i].point == TX_TEST_BEFORE_MODULE_PUBLISH) {
        if (publications<2) before[publications]=publications == 0 ? no_match(g.events[i].visible) :
            match(g.events[i].visible,TX_MODULE_MATCH,TX_COMMUNITY,3,"later");
        ++publications;
    }
    bool clean=!g.failed;
    gate_destroy(&g); destroy_system(&s);
    check(later == TX_OK && match(through_hole,TX_MODULE_MATCH,TX_COMMUNITY,3,"later"),
          "published-slot-after-hole-is-readable");
    check(clean && publications == 2 && before[0] && before[1],"module-publication-is-not-early-prefix");
    check(earlier == TX_OK && match(winner,TX_MODULE_MATCH,TX_COMMUNITY,0,"first"),
          "first-published-index-wins-overlap");
    check(repeated == TX_ALREADY_RECORDED && match(immutable,TX_MODULE_MATCH,TX_COMMUNITY,0,"first"),
          "module-publication-is-immutable");
}
static void lifecycle_retention(void) {
    tx_system *s=create_system(8);
    if (tx_loader_begin(s) != TX_OK) infrastructure("loader begin");
    tx_error recorded=tx_record_module(s,0,"rejected",8,provenance(MODULE,MODULE+0x1000));
    tx_error descriptor=tx_record_descriptor(s,0,false,false);
    tx_epoch none={0}; tx_error refused=tx_open_init(s,0,&none);
    tx_match rejected=tx_lookup(s,MODULE+8);
    tx_epoch epoch=open_module(s,1,"partial",MODULE+0x2000);
    tx_plan p=plan(0,false); tx_receipt installed;
    tx_error install=tx_install_community(s,&epoch,MODULE+0x2008,&p,&installed);
    tx_error closed=tx_close_init(s,&epoch,-7);
    tx_match generated=tx_lookup(s,ISLAND), module_match=tx_lookup(s,MODULE+0x2008);
    tx_receipt retry; tx_plan q=plan(1,false);
    tx_error again=tx_install_community(s,&epoch,MODULE+0x2008,&q,&retry);
    tx_snapshot snap=snapshot(s);
    destroy_system(&s);
    check(recorded == TX_OK && descriptor == TX_DESCRIPTOR && refused == TX_DESCRIPTOR &&
          match(rejected,TX_MODULE_MATCH,TX_COMMUNITY,0,"rejected"),"descriptor-rejection-keeps-module");
    check(install == TX_OK && closed == TX_OK && snap.modules[1].init_result == -7 &&
          snap.modules[1].nhooks == 1,"partial-init-retains-hook-accounting");
    check(match(generated,TX_GENERATED_MATCH,TX_COMMUNITY,1,"partial") &&
          match(module_match,TX_MODULE_MATCH,TX_COMMUNITY,1,"partial") && snap.slots[0].published,
          "partial-init-retains-code-owner-records");
    check(again == TX_EPOCH_CLOSED && unpublished(&retry),"failed-init-closes-new-admission");
}
static void cleanup_receipts(void) {
    tx_system *s=create_system(9);
    tx_receipt r[4]; tx_error rc[4];
    for (unsigned i=0;i<4;++i) {
        tx_plan p=plan(i,i==3);
        if (i==0) p.allocation=(tx_raw){-1,ENOMEM};
        if (i==1) { p.seal=(tx_raw){-1,EPERM}; p.cleanup=(tx_raw){0,0}; }
        if (i==2) { p.island_unlock=(tx_raw){-1,EACCES}; p.cleanup=(tx_raw){-1,EINTR}; }
        if (i==3) { p.target_unlock=(tx_raw){-1,EIO}; p.cleanup=(tx_raw){-1,EINTR}; }
        rc[i]=tx_install_core(s,&p,&r[i]);
    }
    tx_snapshot snap=snapshot(s);
    destroy_system(&s);
    check(rc[0] == TX_ALLOCATION && r[0].resources.allocation_attempted &&
          !r[0].resources.mapping_held && !r[0].resources.cleanup_attempted &&
          r[0].resources.island_slot_burned && raw_is(r[0].resources.allocation,-1,ENOMEM),
          "allocation-failure-keeps-burned-slot-fact");
    check(rc[1] == TX_SEAL && r[1].resources.seal_attempted && r[1].resources.cleanup_attempted &&
          !r[1].resources.mapping_held && raw_is(r[1].resources.seal,-1,EPERM) &&
          raw_is(r[1].resources.cleanup,0,0),"seal-failure-keeps-distinct-cleanup-return");
    check(rc[2] == TX_ISLAND_UNLOCK && r[2].resources.mapping_held &&
          r[2].resources.cleanup_attempted && raw_is(r[2].resources.cleanup,-1,EINTR) &&
          raw_is(r[2].island_protection.unlock,-1,EACCES),"island-failure-retains-uncertain-cleanup");
    check(rc[3] == TX_TARGET_UNLOCK && r[3].resources.mapping_held &&
          !r[3].resources.island_slot_burned && !r[3].island_protection.unlock_attempted &&
          raw_is(r[3].target_protection.unlock,-1,EIO) && raw_is(r[3].resources.cleanup,-1,EINTR),
          "fallback-unlock-failure-retains-mapping");
    bool invisible=true;
    for (unsigned i=0;i<4;++i) invisible=unpublished(&r[i]) && !snap.slots[i].published && invisible;
    check(invisible && snap.reserved_slots == 4,"failure-reservations-and-receipts-stay-retained");
}
static void protection_receipts(void) {
    tx_system *s=create_system(10);
    tx_plan p=plan(0,false);
    p.island_saved_bits=7; p.target_saved_bits=5;
    p.island_relock=(tx_raw){0,91}; p.target_relock=(tx_raw){-1,EPERM};
    tx_receipt r; tx_error rc=tx_install_core(s,&p,&r);
    tx_snapshot snap=snapshot(s);
    destroy_system(&s);
    check(rc == TX_OK && r.island_protection.saved_bits == 7 && r.target_protection.saved_bits == 5,
          "island-and-target-tokens-stay-distinct");
    check(r.island_protection.relock_attempted && r.island_protection.final_bits_known &&
          r.island_protection.final_bits == 7 && raw_is(r.island_protection.relock,0,91),
          "successful-relock-can-restore-writable-bits");
    check(r.target_protection.relock_attempted && !r.target_protection.final_bits_known &&
          raw_is(r.target_protection.relock,-1,EPERM),"target-warning-is-not-rx-proof");
    check(r.metadata_published && r.original_published && r.redirect_published &&
          r.resources.mapping_held && !r.resources.cleanup_attempted && snap.slots[0].published,
          "postpublication-warning-retains-emission");
}
static void publication_order(void) {
    tx_system *s=create_system(11);
    gate g; gate_init(&g,s,false,TX_TEST_RESERVED,ISLAND);
    tx_plan p=plan(0,false); tx_receipt r;
    tx_error rc=tx_install_core(s,&p,&r);
    size_t before=first_event(&g,TX_TEST_BEFORE_UNLOCK), after=first_event(&g,TX_TEST_AFTER_CUT);
    bool invisible=before<g.count && no_match(g.events[before].visible);
    bool visible=after<g.count && g.events[after].visible.source == TX_GENERATED_MATCH;
    bool clean=!g.failed;
    gate_destroy(&g); destroy_system(&s);
    check(rc == TX_OK && clean && before<after && invisible && visible,"cut-has-invisible-and-visible-boundaries");
    check(r.metadata_order == 1 && r.original_order == 2 && r.redirect_order == 3,
          "metadata-original-redirect-order");
    check(r.original_saw_metadata && r.redirect_saw_original,"publication-observes-prerequisites");
    check(r.permit_validated && r.original_value == p.trampoline && r.redirect_value == p.replacement &&
          r.metadata_published && r.original_published && r.redirect_published,
          "published-values-match-bound-plan");
}
/* Deliberate same-thread mutation at the pre-admission fixture checkpoint.
 * The public values are no longer read by the transaction after its entry copy;
 * there is no concurrent write and no claim about a native pointer lifetime. */
typedef struct { tx_plan *plan; tx_epoch *epoch; unsigned mutations; } input_copy;
static void mutate_inputs(tx_test_point point, tx_lock_kind lock, int raw,
                          const tx_receipt *receipt, void *context) {
    (void)lock; (void)raw; (void)receipt;
    input_copy *copy=context;
    if (point == TX_TEST_BEFORE_ADMISSION) {
        memset(copy->plan,0,sizeof *copy->plan);
        memset(copy->epoch,0,sizeof *copy->epoch);
        ++copy->mutations;
    }
}
static void loader_and_caller(void) {
    tx_system *s=create_system(12), *other=create_system(13);
    tx_error first=tx_loader_begin(s), repeated=tx_loader_begin(s);
    tx_epoch epoch=open_module(s,0,"alpha",MODULE);
    tx_plan p=plan(0,false); tx_receipt invalid, foreign;
    tx_error wrong_ra=tx_install_community(s,&epoch,MODULE+0x2000,&p,&invalid);
    tx_error wrong_system=tx_install_community(other,&epoch,MODULE+8,&p,&foreign);
    tx_error closed=tx_close_init(s,&epoch,0), reopen=tx_open_init(s,0,&epoch);
    tx_snapshot snap=snapshot(s);
    destroy_system(&other); destroy_system(&s);
    tx_system *copied=create_system(14);
    tx_plan public_plan=plan(0,false);
    tx_epoch public_epoch={0};
    input_copy copy={&public_plan,&public_epoch,0};
    if (tx_set_test_hook(copied,mutate_inputs,&copy) != TX_OK ||
        tx_loader_begin(copied) != TX_OK) infrastructure("input copy setup");
    public_epoch=open_module(copied,0,"copied",MODULE);
    const tx_epoch retained_epoch=public_epoch;
    tx_receipt receipt;
    tx_error copied_result=tx_install_community(copied,&public_epoch,MODULE+8,&public_plan,&receipt);
    tx_error copied_close=tx_close_init(copied,&retained_epoch,0);
    tx_snapshot copied_snapshot=snapshot(copied);
    destroy_system(&copied);
    check(first == TX_OK && repeated == TX_ALREADY_STARTED,"loader-start-is-one-shot");
    check(wrong_ra == TX_CALLER && unpublished(&invalid) && !invalid.resources.allocation_attempted,
          "captured-caller-value-is-checked");
    check(wrong_system != TX_OK && unpublished(&foreign) && !foreign.resources.allocation_attempted,
          "epoch-is-bound-to-system");
    check(closed == TX_OK && reopen != TX_OK && snap.modules[0].init_finished && snap.reserved_slots == 0,
          "finished-init-epoch-does-not-reopen");
    check(copy.mutations == 1 && public_plan.target == 0 && public_epoch.system == NULL &&
          copied_result == TX_OK && copied_close == TX_OK && receipt.target == TARGET &&
          receipt.replacement == REPLACEMENT && receipt.original_value == TRAMP &&
          receipt.epoch == retained_epoch.epoch && receipt.metadata_published &&
          copied_snapshot.modules[0].nhooks == 1 && copied_snapshot.slots[0].published,
          "admission-copies-public-input-values");
}

typedef struct { const char *name; void (*run)(void); } selector;
static const selector selectors[]={
    {"queued-close",queued_close},{"admitted-close",admitted_close},{"common-admission",common_admission},
    {"fallback-phase",fallback_phase},{"permit-boundary",permit_boundary},{"module-provenance",module_provenance},
    {"module-holes",module_holes},{"lifecycle-retention",lifecycle_retention},{"cleanup-receipts",cleanup_receipts},
    {"protection-receipts",protection_receipts},{"publication-order",publication_order},{"loader-and-caller",loader_and_caller}
};
static void strings(const char *const *items, size_t count) {
    putchar('[');
    for (size_t i=0;i<count;++i) printf("%s\"%s\"",i?",":"",items[i]);
    putchar(']');
}
int main(int argc, char **argv) {
    if (argc != 2) infrastructure("one selector required");
    const selector *selected=NULL;
    for (size_t i=0;i<sizeof selectors/sizeof selectors[0];++i)
        if (strcmp(argv[1],selectors[i].name) == 0) selected=&selectors[i];
    if (!selected) infrastructure("unknown selector");
    selected->run();
    printf("{\"selector\":\"%s\",\"check_names\":",selected->name);
    strings(names,nchecks);
    printf(",\"checks\":%zu,\"failures\":%zu,\"failed_checks\":",nchecks,nfailures);
    strings(failures,nfailures);
    puts("}");
    return nfailures?1:0;
}
