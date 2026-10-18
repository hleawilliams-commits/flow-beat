/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual ordinary producer functions; codec/allocation/scheduling fixtures below. */
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
static int fail_slot=-1, fail_kind, resample_slot=-1, short_slot=-1;
static int tail_pause_slot=-1, tail_waiting, tail_release;
static int final_fault, final_fault_seen, active_deck;
static _Thread_local int codec_slot=-1, codec_allocs;
static _Atomic int offset_reads;
static const int CANDIDATE_BUILD = CANDIDATE;

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
    int rc=pthread_join(thread,result), found=0;
    pthread_mutex_lock(&joinlog_lock);
    for(int i=0;i<decoder_created;i++) if(!decoder_already_joined[i] && pthread_equal(thread,decoder_handles[i])) {
        decoder_already_joined[i]=1;decoder_joined++;found=1;break;
    }
    if(found && decoder_joined==decoder_created && final_fault && !final_fault_seen) {
        /* Explicit test fault: lose progressive publication after real joins.
         * This is not a reproduction of the unexplained staging assertion. */
        int d=active_deck;g_dk[d].stem_frames=0;final_fault_seen=1;
        if(final_fault==2)atomic_store(&g_gate_hold[d],1);
        if(final_fault==3)atomic_store(&g_load_stale[d],1);
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
static stb_vorbis *stb_vorbis_open_filename(const char *path,int *error,void *alloc) {
    (void)alloc;*error=0;int slot=slot_for_filename(strrchr(path,'/')+1);
    codec_slot=slot;codec_allocs=0;
    if(slot==fail_slot && fail_kind==1)return NULL;
    stb_vorbis *v=calloc(1,sizeof *v);
    if(v){v->total=FRAMES;v->slot=slot;}return v;
}
static stb_vorbis_info stb_vorbis_get_info(stb_vorbis *v) {
    return (stb_vorbis_info){2,v->slot==resample_slot?48000:44100};
}
static unsigned stb_vorbis_stream_length_in_samples(stb_vorbis *v){return (unsigned)v->total;}
static void stb_vorbis_close(stb_vorbis *v){free(v);}
static int stb_vorbis_get_samples_float_interleaved(stb_vorbis *v,int channels,float *samples,int count){
    pthread_mutex_lock(&decode_lock);
    if(v->pos==0){atomic_fetch_add(&decode_arrived,1);pthread_cond_broadcast(&decode_cond);}
    while(!atomic_load(&decode_release))pthread_cond_wait(&decode_cond,&decode_lock);
    if(v->slot==tail_pause_slot && v->pos>=PROG_CHUNK){
        tail_waiting=1;pthread_cond_broadcast(&decode_cond);
        while(!tail_release)pthread_cond_wait(&decode_cond,&decode_lock);
    }
    pthread_mutex_unlock(&decode_lock);
    if(codec_fail)return 0;
    long n=count/channels;if(n>v->total-v->pos)n=v->total-v->pos;
    if(v->slot==short_slot && n>117-v->pos)n=117-v->pos;
    if(n<0)n=0;
    for(long i=0;i<n;i++){samples[2*i]=(v->slot+1)*0.03f;samples[2*i+1]=-(v->slot+1)*0.02f;}
    v->pos+=n;atomic_fetch_add(&fake_writes,1);return (int)n;
}
static float *decode_ogg_stereo_44100(const char *path,long *frames,long max){
    (void)max;int slot=slot_for_filename(strrchr(path,'/')+1);
    if(slot==fail_slot&&fail_kind==3)return NULL;
    float *p=malloc(FRAMES*2*sizeof *p);if(!p)return NULL;
    for(long i=0;i<FRAMES;i++){p[2*i]=(slot+1)*0.03f;p[2*i+1]=-(slot+1)*0.02f;}
    *frames=FRAMES;return p;
}
static void *fixture_malloc(size_t bytes){
    codec_allocs++;
    if(codec_slot==fail_slot&&fail_kind==2&&codec_allocs==2)return NULL;
    void *p=malloc(bytes);
    if(p && bytes==FRAMES*2*sizeof(float)){
        float *f=p;for(long i=0;i<FRAMES*2;i++)f[i]=NAN;
    }
    return p;
}
static int no_mlock(const void *p,size_t n) { (void)p;(void)n;return 0; }
#define mlock no_mlock
#define munlock no_mlock

static _Atomic int passed, failed;
#define CHECK(label, condition) do { if (condition) atomic_fetch_add(&passed,1); \
    else {atomic_fetch_add(&failed,1);fprintf(stderr,"FAIL: %s\n",label);} } while(0)
static void fixture_offset_boundary(int deck) {
    pthread_mutex_lock(&joinlog_lock);
    (void)deck;
    if(!inline_threads) CHECK("ordinary offset remains before joins",decoder_joined<decoder_created);
    pthread_mutex_unlock(&joinlog_lock);
}


#define malloc fixture_malloc
#include PRODUCTION_INC

static void join_loader(void){
    if(loader_created){CHECK("worker joined",pthread_join(loader_handle,NULL)==0);loader_created=0;}
}
static void seed(int d){
    CHECK("previous worker idle",!atomic_load(&g_stem_loading[d]));
    for(int k=0;k<NDECK;k++){
        free_stems(k);memset(&g_dk[k],0,sizeof g_dk[k]);memset(g_env[k],0x7f,sizeof g_env[k]);
        g_env_frames[k]=g_env_total[k]=g_env_filled[k]=0;g_env_dir[k][0]=0;
        atomic_store(&g_env_ready[k],0);atomic_store(&g_env_gen[k],0);atomic_store(&g_env_final[k],0);
        atomic_store(&g_gate_hold[k],0);atomic_store(&g_load_stale[k],0);atomic_store(&g_load_again[k],0);
        atomic_store(&g_stem_pending[k],0);atomic_store(&g_stem_present[k],0);
        for(int j=0;j<NSTEM;j++)atomic_store(&g_stem_dec[k][j],0);
    }
    active_deck=d;codec_slot=-1;codec_allocs=0;g_now=100;g_available=900LL<<20;
    fail_slot=resample_slot=short_slot=tail_pause_slot=-1;fail_kind=codec_fail=inline_threads=0;
    tail_waiting=tail_release=final_fault=final_fault_seen=0;
    atomic_store(&published_one,0);atomic_store(&published_zero,0);atomic_store(&fake_writes,0);
    atomic_store(&decode_arrived,0);atomic_store(&decode_release,1);
    pthread_mutex_lock(&joinlog_lock);decoder_created=decoder_joined=0;pthread_mutex_unlock(&joinlog_lock);
}
static void ordinary(int d){
    seed(d);start_load(d,"fixture");join_loader();
    CHECK("normal complete prefix",g_dk[d].stem_frames==FRAMES);
    CHECK("normal slot mask",atomic_load(&g_stem_present[d])==15);
    for(int k=0;k<NSTEM;k++){
        CHECK("decoder final counter",atomic_load(&g_stem_dec[d][k])==FRAMES);
        CHECK("prefix final sample",g_dk[d].stem[k]&&g_dk[d].stem[k][(FRAMES-1)*2]==(k+1)*0.03f);
    }
    CHECK("ordinary final envelope",atomic_load(&g_env_final[d])==1&&atomic_load(&g_env_ready[d])==1);
}
static void missing_or_failed(int d,int kind){
    seed(d);fail_slot=3;fail_kind=kind;if(kind==3)resample_slot=3;
    start_load(d,"fixture");join_loader();
    CHECK("failed slot removed",atomic_load(&g_stem_present[d])==7);
    CHECK("failed slot has no positive prefix",atomic_load(&g_stem_dec[d][3])==0);
    CHECK("failed slot pointer null",g_dk[d].stem[3]==NULL);
    CHECK("other slots remain usable",g_dk[d].stem_frames==FRAMES);
    for(int k=0;k<3;k++)CHECK("surviving slot initialized",g_dk[d].stem[k]&&g_dk[d].stem[k][0]==(k+1)*0.03f);
}
static void short_and_resampled(int d){
    seed(d);short_slot=3;start_load(d,"fixture");join_loader();
    CHECK("short decode keeps only common prefix",g_dk[d].stem_frames==117);
    CHECK("short decode remains present",atomic_load(&g_stem_present[d])==15);
    seed(d);resample_slot=2;start_load(d,"fixture");join_loader();
    CHECK("resampled branch publishes whole prefix",g_dk[d].stem_frames==FRAMES&&g_dk[d].stem[2]);
}
static void prefix_while_tail_writes(int d){
    seed(d);tail_pause_slot=3;start_load(d,"fixture");
    pthread_mutex_lock(&decode_lock);while(!tail_waiting)pthread_cond_wait(&decode_cond,&decode_lock);pthread_mutex_unlock(&decode_lock);
    /* Only immutable acquired prefixes are inspected while other workers may
     * still be writing their future chunks. No consumer reads the poisoned tail. */
    long covered;
    /* The paused slot has published one chunk, but another decoder may not
     * have produced its first one yet. Wait through the actual acquired scan. */
    do {covered=ofs_stems_decoded(d);if(covered<PROG_CHUNK)sched_yield();}while(covered<PROG_CHUNK);
    CHECK("slow slot limits prefix",covered==PROG_CHUNK);
    env_progress(d);
    long want=(long)((double)PROG_CHUNK/FRAMES*AZW_N);
    pthread_mutex_lock(&g_envp_lock);
    CHECK("envelope stops at acquired prefix",g_env_filled[d]==want);
    CHECK("unfilled envelope tail preserved",g_env[d][3][want]==128);
    pthread_mutex_unlock(&g_envp_lock);
    float known=0;for(int k=0;k<NSTEM;k++)known+=(k+1)*0.03f-(k+1)*0.02f;
    CHECK("offset reader sees acquired samples",fabsf(ofs_stem_mono_lim(&g_dk[d],covered-1,covered)-known)<1e-6f);
    CHECK("offset reader rejects next sample",ofs_stem_mono_lim(&g_dk[d],covered,covered)==0);
    pthread_mutex_lock(&decode_lock);tail_release=1;pthread_cond_broadcast(&decode_cond);pthread_mutex_unlock(&decode_lock);
    join_loader();CHECK("tail completes normally",g_dk[d].stem_frames==FRAMES);
}
static void isolated_publication_guards(int d){
#if CANDIDATE
    seed(d);start_load(d,"fixture");join_loader();
    /* All buffers are now immutable. Test the helper before any loader cleanup
     * could conceal a prohibited intermediate publication. */
    g_dk[d].stem_frames=0;atomic_store(&g_gate_hold[d],0);atomic_store(&g_load_stale[d],1);
    publish_stem_prefix(d,FRAMES);CHECK("stale alone blocks publication",g_dk[d].stem_frames==0);
    atomic_store(&g_load_stale[d],0);atomic_store(&g_gate_hold[d],1);
    publish_stem_prefix(d,FRAMES);CHECK("hold alone blocks publication",g_dk[d].stem_frames==0);
    atomic_store(&g_gate_hold[d],0);publish_stem_prefix(d,0);
    CHECK("empty prefix cannot publish",g_dk[d].stem_frames==0);
    publish_stem_prefix(d,FRAMES);CHECK("valid joined prefix publishes",g_dk[d].stem_frames==FRAMES);
#else
    (void)d;
#endif
}
static void finalization_fault(int d,int fault){
    seed(d);final_fault=fault;start_load(d,"fixture");join_loader();
    CHECK("postjoin fault exercised",final_fault_seen==1);
    if(fault==1){
        if(CANDIDATE_BUILD)CHECK("postjoin reconciles missed progressive publication",g_dk[d].stem_frames==FRAMES);
        else CHECK("baseline lacks postjoin reconciliation",g_dk[d].stem_frames==0);
    }else CHECK("hold or cancellation prevents final publication",g_dk[d].stem_frames==0);
}
int main(void){
    char tmp[]="/tmp/ordinary-stems-XXXXXX";char *made=mkdtemp(tmp);if(!made)abort();snprintf(folder,sizeof folder,"%s",made);
    for(int k=0;k<NSTEM;k++){char path[1200];snprintf(path,sizeof path,"%s/%d.ogg",folder,k);FILE *f=fopen(path,"w");if(!f)abort();fclose(f);}
    for(int d=0;d<NDECK;d++){
        ordinary(d);for(int kind=1;kind<=3;kind++)missing_or_failed(d,kind);
        short_and_resampled(d);prefix_while_tail_writes(d);
        for(int fault=1;fault<=3;fault++)finalization_fault(d,fault);
        isolated_publication_guards(d);
        printf("deck %d: ordinary producers, failure slots, prefix bounds, joined finalization checked\n",d+1);
    }
    for(int d=0;d<NDECK;d++)free_stems(d);
    char path[1200];for(int k=0;k<NSTEM;k++){snprintf(path,sizeof path,"%s/%d.ogg",folder,k);unlink(path);}
    snprintf(path,sizeof path,"%s/azstem.env",folder);unlink(path);rmdir(folder);
    printf("%d passed, %d failed\n",atomic_load(&passed),atomic_load(&failed));return atomic_load(&failed)?1:0;
}
