/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual load/decoder/lifecycle functions, synthetic codec and native fixtures. */
#include <dirent.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include FIXTURE_INC
#include RECOVERY_DECLARATIONS_INC

static _Atomic int g_stem_pending[NDECK], g_load_again[NDECK];
static unsigned char g_env[NDECK][NSTEM][AZW_N];
static long g_env_frames[NDECK], g_env_total[NDECK], g_ofs_load[NDECK];
static int g_env_filled[NDECK];
static _Atomic unsigned g_env_gen[NDECK];
static _Atomic int g_env_final[NDECK];
static char g_env_dir[NDECK][1024];
static pthread_mutex_t g_envp_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int published_one, published_zero, fake_writes, decode_arrived;
static char folder[1024];
static int codec_fail, inline_threads;
static _Atomic int decode_release;
static pthread_mutex_t decode_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t decode_cond = PTHREAD_COND_INITIALIZER;
static pthread_t loader_handle;
static int loader_created;
static pthread_mutex_t joinlog_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_t decoder_handles[32];
static int decoder_already_joined[32];
static int decoder_created,decoder_joined;
static void *loader_thread(void *arg);
static void ofs_load_measure(int deck, const char *track, const char *src);
static long ofs_stems_decoded(int deck);
static int recovery_hidden(int deck);
static void build_env(int deck);
static int env_cache_load(int deck);
static void native_resume_render(void *self, void *buf, void *midi) { (void)self;(void)buf;(void)midi; }
static _Atomic(void *) g_deck_self[NDECK];
static char g_stems_base[512]="/fixture/STEMS";
static int g_eject_empty[NDECK];
static char g_pending_name[NDECK][512],g_loaded_src[NDECK][600],g_pending_src[NDECK][600];
static const char *g_name[NDECK];
#define dev_flag(path) 0
static void find_stems_base(char *p,size_t n){(void)p;(void)n;}
static void load_stems_f32(int deck){(void)deck;}
static const char *get_loaded_track_name(int deck){return g_name[deck];}
static void publish_src(int deck){(void)deck;}

static int az_thread_create(pthread_t *thread, const pthread_attr_t *attr,
                            void *(*fn)(void *), void *arg) {
    if (inline_threads) return EAGAIN;
#ifdef FIXTURE_SERIAL_NORMAL
    /* Ordinary progressive envelope races are separately reproduced against
     * the preceding patches. Keep that baseline path serial in private TSan. */
    if(fn!=loader_thread && !recovery_hidden(((pjob_t *)arg)->deck)) return EAGAIN;
#endif
    int result = pthread_create(thread, attr, fn, arg);
    if (!result && fn == loader_thread) { loader_handle = *thread; loader_created = 1; }
    if (!result && fn != loader_thread) {
        pthread_mutex_lock(&joinlog_lock);
        if(decoder_created>=32)abort();decoder_already_joined[decoder_created]=0;decoder_handles[decoder_created++]=*thread;
        pthread_mutex_unlock(&joinlog_lock);
    }
    return result;
}
static int recorded_join(pthread_t thread,void **result) {
    int rc=pthread_join(thread,result);
    pthread_mutex_lock(&joinlog_lock);
    for(int i=0;i<decoder_created;i++)if(!decoder_already_joined[i]&&pthread_equal(thread,decoder_handles[i])){
        decoder_already_joined[i]=1;decoder_joined++;break;
    }
    pthread_mutex_unlock(&joinlog_lock);return rc;
}
#define pthread_join recorded_join
/* The test joins the otherwise detached loader so completion is deterministic. */
static int keep_joinable(pthread_t thread) { (void)thread; return 0; }
#define pthread_detach keep_joinable
static void loaded_track_path(int deck, char *dst, size_t n) {
    snprintf(dst, n, "/fixture/deck%d/track.ogg", deck);
}
static int find_track_folder_any(const char *track, const char *src, char *out, size_t n, int *ident) {
    (void)track; (void)src; *ident = 1; snprintf(out,n,"%s",folder); return 1;
}
static int has_ogg_ext(const char *name) { const char *p=strrchr(name,'.'); return p && strcmp(p,".ogg")==0; }
static int slot_for_filename(const char *name) { return name[0]>='0' && name[0]<='3' ? name[0]-'0' : -1; }
static void publish_deck_file(int deck, const char *what, const char *value) {
    (void)deck;
    if (!strcmp(what,"avail")) atomic_fetch_add(!strcmp(value,"1") ? &published_one : &published_zero,1);
}

typedef struct { long total, pos; int slot; } stb_vorbis;
typedef struct { int channels; unsigned sample_rate; } stb_vorbis_info;
static stb_vorbis *stb_vorbis_open_filename(const char *path, int *error, void *alloc) {
    (void)alloc; *error=0; stb_vorbis *v=calloc(1,sizeof *v);
    if (v) { v->total=FRAMES; v->slot=slot_for_filename(strrchr(path,'/')+1); }
    return v;
}
static stb_vorbis_info stb_vorbis_get_info(stb_vorbis *v) { (void)v; return (stb_vorbis_info){2,44100}; }
static unsigned stb_vorbis_stream_length_in_samples(stb_vorbis *v) { return (unsigned)v->total; }
static void stb_vorbis_close(stb_vorbis *v) { free(v); }
static int stb_vorbis_get_samples_float_interleaved(stb_vorbis *v,int channels,float *samples,int count) {
    pthread_mutex_lock(&decode_lock);
    atomic_fetch_add(&decode_arrived,1);
    pthread_cond_broadcast(&decode_cond);
    while (!atomic_load(&decode_release)) pthread_cond_wait(&decode_cond,&decode_lock);
    pthread_mutex_unlock(&decode_lock);
    if (codec_fail) return 0;
    long n=count/channels; if (n>v->total-v->pos) n=v->total-v->pos;
    for (long i=0;i<n;i++) { samples[2*i]=(float)(v->slot+1)*0.03f; samples[2*i+1]=-(float)(v->slot+1)*0.02f; }
    v->pos+=n; atomic_fetch_add(&fake_writes,1); return (int)n;
}
static float *decode_ogg_stereo_44100(const char *path,long *frames,long max) {
    (void)path;(void)frames;(void)max; abort(); /* fixture rate always44100 */
}
static int no_mlock(const void *p,size_t n) { (void)p;(void)n;return 0; }
#define mlock no_mlock
#define munlock no_mlock

static _Atomic int passed, failed;
#define CHECK(label, condition) do { if (condition) atomic_fetch_add(&passed,1); \
    else {atomic_fetch_add(&failed,1);fprintf(stderr,"FAIL: %s\n",label);} } while(0)
static void fixture_offset_boundary(int deck) {
    pthread_mutex_lock(&joinlog_lock);
    if(recovery_hidden(deck)) CHECK("private offset follows all decoder joins",decoder_joined==decoder_created);
    #ifndef FIXTURE_SERIAL_NORMAL
    else if(!inline_threads)
        CHECK("ordinary offset remains before decoder joins",decoder_joined<decoder_created);
    #endif
    pthread_mutex_unlock(&joinlog_lock);
}

/* Test-only atomic-store observation. The production source is unchanged;
 * intercept the exact completion stores to force lifecycle boundary schedules. */
static pthread_mutex_t boundary_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t boundary_cond=PTHREAD_COND_INITIALIZER;
static _Atomic int boundary_enabled,boundary_deck;
static int boundary_reached,boundary_release;
static _Thread_local int flusher_role;
static int decision_at_lock;
static int recorded_lock(pthread_mutex_t *lock) {
    if(flusher_role && lock==&g_adv_lock){
        pthread_mutex_lock(&boundary_lock);decision_at_lock=1;
        pthread_cond_broadcast(&boundary_cond);pthread_mutex_unlock(&boundary_lock);
    }
    return pthread_mutex_lock(lock);
}
static void after_store(const void *p,long long value) {
    for (int d=0;d<NDECK;d++)
        if (p==(const void *)&g_recovery_mode[d] && value==0)
            CHECK("mode release never precedes idle loader", atomic_load(&g_stem_loading[d])==0);
    if (!boundary_enabled || p!=(const void *)&g_stem_loading[boundary_deck] || value!=0 ||
        !recovery_hidden(boundary_deck)) return;
    pthread_mutex_lock(&boundary_lock);
    boundary_reached=1; pthread_cond_broadcast(&boundary_cond);
    while (!boundary_release) pthread_cond_wait(&boundary_cond,&boundary_lock);
    pthread_mutex_unlock(&boundary_lock);
}
#undef atomic_store_explicit
#define atomic_store_explicit(P,V,O) do { __c11_atomic_store((P),(V),(O)); after_store((P),(long long)(V)); } while(0)
#define pthread_mutex_lock recorded_lock
#include PRODUCTION_INC

static void wait_decoders(void) {
    pthread_mutex_lock(&decode_lock);
    while (atomic_load(&decode_arrived)<NSTEM) pthread_cond_wait(&decode_cond,&decode_lock);
    pthread_mutex_unlock(&decode_lock);
}
static void release_decoders(void) {
    pthread_mutex_lock(&decode_lock); atomic_store(&decode_release,1);
    pthread_cond_broadcast(&decode_cond); pthread_mutex_unlock(&decode_lock);
}
static void join_loader(void) {
    if (loader_created) { CHECK("loader joins",pthread_join(loader_handle,NULL)==0); loader_created=0; }
}
static void seed(int deck) {
    boundary_enabled=0;
    pthread_mutex_lock(&joinlog_lock);decoder_created=decoder_joined=0;pthread_mutex_unlock(&joinlog_lock);
    for (int d=0;d<NDECK;d++) {
        CHECK("previous loader idle",!atomic_load(&g_stem_loading[d]));
        recovery_free_buffers(d); free_stems(d);
        atomic_store(&g_recovery_mode[d],0); atomic_store(&g_recovery_token[d],0);
        atomic_store(&g_recovery_ready[d],0); atomic_store(&g_load_stale[d],0);
        atomic_store(&g_gate_hold[d],0); atomic_store(&g_load_again[d],0);
        atomic_store(&g_stem_pending[d],0); atomic_store(&g_stem_present[d],0);
        atomic_store(&g_deck_trackgen[d],7); atomic_store(&g_play_obs[d],-1);
        atomic_store(&g_play_activity[d],0); atomic_store(&g_retry_bytes[d],300LL<<20);
        g_retry_activity[d]=0;g_retry_still[d]=0;g_retry_after[d]=0;
        strcpy(g_dk[d].loaded_track,"unchanged title");
        g_name[d]="unchanged title";g_eject_empty[d]=0;
        g_pending_name[d][0]=g_pending_src[d][0]=0;
        loaded_track_path(d,g_loaded_src[d],sizeof g_loaded_src[d]);
        atomic_store(&g_dk[d].ctrl,0x8f); atomic_store(&g_dk[d].stem_off,31);
        g_dk[d].cur=123; g_dk[d].rd_valid=1;
        memset(g_env[d],0x41+d,sizeof g_env[d]); g_env_frames[d]=777;
        g_env_total[d]=888;g_env_filled[d]=17;atomic_store(&g_env_gen[d],23);
        atomic_store(&g_env_final[d],1);atomic_store(&g_env_ready[d],0);
        strcpy(g_env_dir[d],"prior directory");g_ofs_load[d]=29;
        for (int k=0;k<NSTEM;k++) atomic_store(&g_sgain_k[d][k],1000);
    }
    g_now=9000;g_available=900LL<<20;g_tls_deck=deck;
    atomic_store(&g_target_pbself,1);atomic_store(&g_inject_on,1);atomic_store(&g_enable,1);
    atomic_store(&g_allsum,0);atomic_store(&g_testtone_on,0);atomic_store(&g_keyratio_bits,0);
    atomic_store(&published_one,0);atomic_store(&published_zero,0);atomic_store(&decode_arrived,0);
    atomic_store(&fake_writes,0);atomic_store(&decode_release,0);
    codec_fail=0;inline_threads=0;
}

static unsigned long checksum(const void *data,size_t n) {
    const unsigned char *p=data;unsigned long hash=5381;
    for(size_t i=0;i<n;i++) hash=(hash*33)^p[i];return hash;
}
typedef struct { unsigned long dk,env;long frames,total,ofs;int filled,ready,final;unsigned gen;char dir[1024]; } snapshot;
static snapshot capture(int d) {
    snapshot s={checksum(&g_dk[d],sizeof g_dk[d]),checksum(g_env[d],sizeof g_env[d]),
        g_env_frames[d],g_env_total[d],g_ofs_load[d],g_env_filled[d],atomic_load(&g_env_ready[d]),
        azstem_env_final(d),azstem_env_gen(d),{0}};
    strcpy(s.dir,g_env_dir[d]);return s;
}
static void same_public(int d,snapshot s) {
    snapshot now=capture(d);
    CHECK("public deck fields unchanged",now.dk==s.dk);
    CHECK("public envelope bytes unchanged",now.env==s.env);
    CHECK("public envelope metadata unchanged",now.frames==s.frames && now.total==s.total &&
          now.filled==s.filled && now.ready==s.ready && now.final==s.final && now.gen==s.gen && now.ofs==s.ofs);
    CHECK("public envelope directory unchanged",!strcmp(now.dir,s.dir));
    CHECK("public availability remains absent",azstem_frames(d)==0 && azstem_present(d)==0 && atomic_load(&g_stem_pending[d])==0);
    const unsigned char *wave=NULL;int n=-1,ns=-1;float pos=-1;
    CHECK("no new envelope export",azstem_wave(d,&wave,&n,&ns,&pos)==0 && wave==NULL);
    CHECK("no availability file writes",atomic_load(&published_one)==0 && atomic_load(&published_zero)==0);
}
static void audio_native(int d) {
    g_tls_deck=d;Float2 pcm[BLOCK];Pos32 pos={{0}};long long start=64;memcpy(pos.b+POS_SAMPLE_OFF,&start,8);
    for(int mode=0;mode<5;mode++) {
        atomic_store(&g_dk[d].ctrl,mode==1?0x8e:0x8f);
        atomic_store(&g_sgain_k[d][1],mode==2?500:1000);atomic_store(&g_allsum,mode==4);
        Pos32 got=pbread_hook(NULL,pcm,&pos,BLOCK),expected=pos;long long end=start+BLOCK;
        memcpy(expected.b+POS_SAMPLE_OFF,&end,8);
        CHECK("native Position retained",!memcmp(&got,&expected,sizeof got));
        int good=1;for(int i=0;i<BLOCK;i++)good&=pcm[i].L==0.75f&&pcm[i].R==-0.75f;
        CHECK("staged buffers never enter pbread",good);
    }
    atomic_store(&g_dk[d].ctrl,0x8f);atomic_store(&g_sgain_k[d][1],1000);atomic_store(&g_allsum,0);
}

static void successful_stage(int d) {
    seed(d);
    const long long need=(long long)FRAMES*NSTEM*2*sizeof(float);
    g_available=(400LL<<20)+need-1;inline_threads=1;atomic_store(&decode_release,1);
    start_load(d,"unchanged title");
    CHECK("real ordinary loader refuses one byte below reserve",g_dk[d].stem_frames==0&&atomic_load(&g_retry_bytes[d])==need);
    CHECK("refusal does not allocate stem buffers",g_dk[d].stem[0]==NULL&&atomic_load(&fake_writes)==0);
    g_available=(400LL<<20)+need;inline_threads=0;atomic_store(&decode_release,0);
    snapshot before=capture(d);unsigned long long old=g_recovery_next[d];
    start_load_mode(d,"unchanged title",1);wait_decoders();
    CHECK("private routing selected",stem_decode_target(d)==&g_recovery_dk[d]);
    CHECK("immutable nonzero job token",atomic_load(&g_recovery_token[d])==old+1);
    same_public(d,before);audio_native(d);
    stem_play_observe(d,100);stem_play_observe(d,882);release_decoders();join_loader();
    CHECK("decoded prefix retained privately",g_recovery_dk[d].stem_frames==FRAMES && atomic_load(&g_recovery_ready[d]));
    CHECK("private offset retained",atomic_load(&g_recovery_dk[d].stem_off)==17);
    CHECK("stage does not activate",recovery_hidden(d) && g_dk[d].stem_frames==0);
    same_public(d,before);audio_native(d);
    for(int i=0;i<30;i++){g_now+=200;stem_play_observe(d,882);CHECK("prepared result does not retry",!memory_retry_ready(d));}
    unsigned long long token=atomic_load(&g_recovery_token[d]);
    start_load_mode(d,"unchanged title",1);
    CHECK("prepared launch rejected without token reuse",atomic_load(&g_recovery_token[d])==token);
    drop_stems(d);CHECK("prepared drop frees private memory",!recovery_hidden(d)&&!g_recovery_dk[d].stem[0]&&!atomic_load(&g_recovery_token[d]));
    printf("deck %d: resume, private retained prefix, unchanged public/audio state, drop passed\n",d+1);
}
static void cancelled_stage(int d) {
    seed(d);start_load_mode(d,"unchanged title",1);wait_decoders();
    const float *owned=g_recovery_dk[d].stem[0];
    deck_track_changed(d);drop_stems(d);strcpy(g_dk[d].loaded_track,"replacement");
    CHECK("cancel keeps decoder ownership until join",owned&&g_recovery_dk[d].stem[0]==owned&&recovery_hidden(d));
    release_decoders();join_loader();
    CHECK("cancel frees after join",!g_recovery_dk[d].stem[0]&&!recovery_hidden(d));
    CHECK("worker never restores old loaded title",!strcmp(g_dk[d].loaded_track,"replacement"));
    CHECK("cancel requests ordinary replacement load",atomic_load(&g_load_again[d])==1);
    CHECK("cancel clears stale state",!atomic_load(&g_load_stale[d])&&!atomic_load(&g_gate_hold[d]));
    inline_threads=1;start_load(d,"replacement");
    CHECK("replacement uses public normal path",g_dk[d].stem_frames==FRAMES&&!recovery_hidden(d)&&atomic_load(&published_one)==1);
    printf("deck %d: in-flight replacement joins, discards, ordinary reload passed\n",d+1);
}

static _Atomic int decision_started;
static int decision_deck,decision_restart;
static void *flusher_decision(void *unused) {
    (void)unused;flusher_role=1;atomic_store(&decision_started,1);
    if (decision_restart) {
        if (!atomic_load_explicit(&g_stem_loading[decision_deck],memory_order_acquire) &&
            atomic_exchange(&g_load_again[decision_deck],0)) start_load(decision_deck,"replacement");
    } else drop_stems(decision_deck);
    return NULL;
}
static void completion_boundary(int d,int restart) {
    seed(d);start_load_mode(d,"unchanged title",1);wait_decoders();
    if (restart) {deck_track_changed(d);drop_stems(d);strcpy(g_dk[d].loaded_track,"replacement");}
    else codec_fail=1;
    pthread_mutex_lock(&boundary_lock);boundary_deck=d;boundary_enabled=1;boundary_reached=0;boundary_release=0;
    pthread_mutex_unlock(&boundary_lock);release_decoders();
    pthread_mutex_lock(&boundary_lock);while(!boundary_reached)pthread_cond_wait(&boundary_cond,&boundary_lock);
    pthread_mutex_unlock(&boundary_lock);
    CHECK("completion paused in loading0/mode1 window",!atomic_load(&g_stem_loading[d])&&recovery_hidden(d));
    decision_deck=d;decision_restart=restart;atomic_store(&decision_started,0);pthread_t decision;
    pthread_mutex_lock(&boundary_lock);decision_at_lock=0;pthread_mutex_unlock(&boundary_lock);
    pthread_t completing_loader=loader_handle;loader_created=0;
    CHECK("flusher decision thread starts",pthread_create(&decision,NULL,flusher_decision,NULL)==0);
    pthread_mutex_lock(&boundary_lock);
    while(!decision_at_lock)pthread_cond_wait(&boundary_cond,&boundary_lock);
    pthread_mutex_unlock(&boundary_lock);
    /* Disable the trap for the next ordinary loader before releasing completion. */
    pthread_mutex_lock(&boundary_lock);boundary_enabled=0;boundary_release=1;pthread_cond_broadcast(&boundary_cond);
    pthread_mutex_unlock(&boundary_lock);
    CHECK("completing recovery joins",pthread_join(completing_loader,NULL)==0);
    CHECK("flusher decision joins",pthread_join(decision,NULL)==0);
    if(restart){
        join_loader();
        if(g_dk[d].stem_frames!=FRAMES||recovery_hidden(d))
            fprintf(stderr,"restart-state deck=%d frames=%ld mode=%d loading=%d stale=%d hold=%d again=%d decoded=%ld/%ld/%ld/%ld public=%d\n",
                    d+1,g_dk[d].stem_frames,recovery_hidden(d),atomic_load(&g_stem_loading[d]),atomic_load(&g_load_stale[d]),
                    atomic_load(&g_gate_hold[d]),atomic_load(&g_load_again[d]),atomic_load(&g_stem_dec[d][0]),
                    atomic_load(&g_stem_dec[d][1]),atomic_load(&g_stem_dec[d][2]),atomic_load(&g_stem_dec[d][3]),atomic_load(&published_one));
        CHECK("consumed restart still launches ordinary load",g_dk[d].stem_frames==FRAMES&&!recovery_hidden(d));
    }
    else CHECK("drop cannot strand stale state",!atomic_load(&g_load_stale[d])&&!atomic_load(&g_gate_hold[d]));
    printf("deck %d: completion boundary %s passed\n",d+1,restart?"restart":"drop");
}
static void eject_then_new_track(int d) {
    seed(d);start_load_mode(d,"unchanged title",1);wait_decoders();
    g_name[d]="";
    for(int i=0;i<EJECT_EMPTY_TICKS;i++)stems_tick();
    CHECK("actual flusher accepts eject",g_dk[d].loaded_track[0]==0&&atomic_load(&g_deck_trackgen[d])==8);
    CHECK("eject defers private free until joins",recovery_hidden(d)&&g_recovery_dk[d].stem[0]!=NULL);
    release_decoders();join_loader();
    CHECK("cancelled eject retains restart request",atomic_load(&g_load_again[d])==1);
    for(int i=0;i<3;i++)stems_tick();
    CHECK("empty ticks retain restart request",atomic_load(&g_load_again[d])==1);
    g_name[d]="replacement";inline_threads=1;
    stems_tick();stems_tick();
    CHECK("new confirmed track loads normally",g_dk[d].stem_frames==FRAMES&&!strcmp(g_dk[d].loaded_track,"replacement"));
    int writes=atomic_load(&published_one);
    for(int i=0;i<5;i++)stems_tick();
    CHECK("accepted job consumes obsolete restart request",!atomic_load(&g_load_again[d]));
    CHECK("same-track ticks do not decode replacement twice",atomic_load(&published_one)==writes);
    printf("deck %d: actual flusher eject/new track avoids duplicate reload\n",d+1);
}
int main(void) {
    char tmp[]="/tmp/az-staging-test-XXXXXX";char *made=mkdtemp(tmp);if(!made)abort();snprintf(folder,sizeof folder,"%s",made);
    for(int k=0;k<NSTEM;k++){char path[1200];snprintf(path,sizeof path,"%s/%d.ogg",folder,k);FILE *f=fopen(path,"w");if(!f)abort();fclose(f);}
#ifdef BASELINE_ORDINARY
    for(int d=0;d<NDECK;d++){
        seed(d);release_decoders();start_load(d,"unchanged title");join_loader();
        CHECK("baseline ordinary decode remains usable",g_dk[d].stem_frames==FRAMES);
    }
#else
    for(int d=0;d<NDECK;d++){successful_stage(d);cancelled_stage(d);completion_boundary(d,0);completion_boundary(d,1);eject_then_new_track(d);}
#endif
    boundary_enabled=0;
    for(int d=0;d<NDECK;d++){recovery_free_buffers(d);free_stems(d);}
    char path[1200];for(int k=0;k<NSTEM;k++){snprintf(path,sizeof path,"%s/%d.ogg",folder,k);unlink(path);}
    snprintf(path,sizeof path,"%s/azstem.env",folder);unlink(path);rmdir(folder);
    printf("%d passed, %d failed\n",atomic_load(&passed),atomic_load(&failed));return atomic_load(&failed)?1:0;
}
