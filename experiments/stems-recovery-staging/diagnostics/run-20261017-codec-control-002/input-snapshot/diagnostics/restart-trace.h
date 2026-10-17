/* SPDX-License-Identifier: GPL-3.0-only */
/* Test-only per-thread traces. No mutex, clock or atomic counter is added to
 * the decoded-counter store -> minimum-scan interval. Read only after joins. */
enum { DIAG_STORE=1, DIAG_LOAD, DIAG_ADV_BEGIN, DIAG_ADV_END, DIAG_PUBLISH, DIAG_RESULT };
struct diag_event { int kind, deck, slot; long long value; };
struct diag_thread {
    pthread_t handle;
    void *(*fn)(void *);
    void *arg;
    int role, deck, slot, recovery, started, ended, joined, join_rc;
    int loading, mode, stale, hold;
    long frames;
    unsigned used;
    struct diag_event event[256];
};
static struct diag_thread diag_threads[32];
static unsigned diag_count;
static pthread_mutex_t diag_registry_lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local struct diag_thread *diag_self;
static unsigned diag_overflow;
static void diag_event(int kind,int deck,int slot,long long value) {
    struct diag_thread *t=diag_self;
    if(!t)return;
    if(t->used>=sizeof t->event/sizeof t->event[0])abort();
    t->event[t->used++]=(struct diag_event){kind,deck,slot,value};
}
static void diag_atomic(int kind,const void *p,long long value) {
    if(!diag_self)return;
    for(int d=0;d<NDECK;d++) {
        for(int k=0;k<NSTEM;k++)if(p==(const void *)&g_stem_dec[d][k]) {
            diag_event(kind,d,k,value);return;
        }
        if(p==(const void *)&g_gate_hold[d]){diag_event(kind,d,-1,value);return;}
        if(p==(const void *)&g_stem_loading[d]){diag_event(kind,d,-2,value);return;}
        if(p==(const void *)&g_recovery_mode[d]){diag_event(kind,d,-3,value);return;}
    }
}
static void *diag_thread_start(void *arg) {
    struct diag_thread *t=arg;diag_self=t;t->started=1;
    void *result=t->fn(t->arg);
    if(t->role==1) {
        if(!t->recovery)t->frames=g_dk[t->deck].stem_frames;
        t->loading=atomic_load(&g_stem_loading[t->deck]);
        t->mode=atomic_load(&g_recovery_mode[t->deck]);
        t->stale=atomic_load(&g_load_stale[t->deck]);
        t->hold=atomic_load(&g_gate_hold[t->deck]);
    }
    t->ended=1;return result;
}
static int diag_create(pthread_t *thread,const pthread_attr_t *attr,
                       void *(*fn)(void *),void *arg) {
    /* Creation belongs to one loader/flusher at a time in these schedules.
     * Child slots are initialized before pthread_create publishes their address. */
    pthread_mutex_lock(&diag_registry_lock);
    if(diag_count>=sizeof diag_threads/sizeof diag_threads[0])abort();
    struct diag_thread *t=&diag_threads[diag_count++];
    memset(t,0,sizeof *t);t->fn=fn;t->arg=arg;t->slot=-1;
    if(fn==loader_thread) {
        const loadjob_t *j=arg;t->role=1;t->deck=j->deck;t->recovery=j->recovery;
#ifdef DIAGNOSTIC_EMPTY_ORDINARY
        /* Named negative control: genuine codec EOF, never altered atomic reads. */
        if(!j->recovery)codec_fail=1;
#endif
    } else {
        const pjob_t *j=arg;t->role=2;t->deck=j->deck;t->slot=j->slot;
        t->recovery=atomic_load(&g_recovery_mode[t->deck]);
    }
    int rc=pthread_create(thread,attr,diag_thread_start,t);
    if(!rc)t->handle=*thread;else{t->joined=1;t->join_rc=rc;}
    pthread_mutex_unlock(&diag_registry_lock);
    return rc;
}
static void diag_join(pthread_t thread,int rc) {
    /* Called after real pthread_join, never in the publication interval. */
    pthread_mutex_lock(&diag_registry_lock);
    for(unsigned i=0;i<diag_count;i++) {
        struct diag_thread *t=&diag_threads[i];
        if(!t->joined&&pthread_equal(t->handle,thread)) {
            t->joined=1;t->join_rc=rc;break;
        }
    }
    pthread_mutex_unlock(&diag_registry_lock);
}
static void diag_result(int deck,int recovery,long frames) {
    diag_event(DIAG_RESULT,deck,recovery,frames);
}
static int diag_all_joined(void) {
    for(unsigned i=0;i<diag_count;i++) {
        const struct diag_thread *t=&diag_threads[i];
        if(!t->started||!t->ended||!t->joined||t->join_rc)return 0;
    }
    return diag_overflow==0;
}
static void diag_dump(unsigned attempt,int deck) {
    printf("TRACE attempt=%u deck=%d threads=%u\n",attempt,deck+1,diag_count);
    for(unsigned i=0;i<diag_count;i++) {
        const struct diag_thread *t=&diag_threads[i];
        printf("thread=%u role=%s deck=%d slot=%d recovery=%d started=%d ended=%d joined=%d join_rc=%d final_frames=%ld loading=%d mode=%d stale=%d hold=%d\n",
            i,t->role==1?"loader":"decoder",t->deck+1,t->slot,t->recovery,t->started,t->ended,t->joined,t->join_rc,
            t->frames,t->loading,t->mode,t->stale,t->hold);
        for(unsigned j=0;j<t->used;j++) {
            const struct diag_event *e=&t->event[j];
            printf("  event=%u kind=%d deck=%d slot=%d value=%lld\n",j,e->kind,e->deck+1,e->slot,e->value);
        }
    }
}
