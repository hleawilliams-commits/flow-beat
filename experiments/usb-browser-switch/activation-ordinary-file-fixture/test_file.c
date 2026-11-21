/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE 1
#include "file_provider.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef AO_WITNESS_SETUP
#error "the ordinary file witness requires only the five authored setup APIs"
#endif
/* This translation unit has no control-dependent branches. The same harness
 * object is linked to every separately compiled provider configuration. */
#define REPORT_MAGIC UINT64_C(0x4f46494c45523031)
#define HANDOFF_MAGIC UINT64_C(0x4f4648414e443031)
#define JOURNAL_CAP 48u
#define CHECK_CAP 16u
#define SOURCE_SCOPE "isolated-Linux-owned-temporary-file; authored-owner-model; ordinary-synchronous-callbacks"
_Static_assert(sizeof(dev_t)<=sizeof(uintmax_t) && sizeof(ino_t)<=sizeof(uintmax_t) &&
               sizeof(uid_t)<=sizeof(uintmax_t) && sizeof(nlink_t)<=sizeof(uintmax_t) &&
               sizeof(mode_t)<=sizeof(uintmax_t) && sizeof(off_t)<=sizeof(intmax_t),
               "native identity fields must fit the checked reporting types");
#if !defined(__linux__) || !defined(__aarch64__)
#error "the owned-file witness is scoped to the isolated Linux AArch64 target"
#endif
static const char sentinel[]="owned-collision!";
_Static_assert(sizeof sentinel-1==16,"the collision sentinel is sixteen bytes");
enum selector_id {
    DIRECT, GENERATED, COLLISION, READONLY, CLOSE_REFUSAL, INVALID_WRITE,
    NO_OWNER, REPEAT, C_ERROR, C_TOKEN, C_BYTES, C_SYNC, C_CLOSE, C_ONCE, SELECTORS
};
static const char *const names[SELECTORS]={
    "file-direct","file-generated","collision-refusal","readonly-write-error",
    "supplied-close-refusal","supplied-invalid-write-result","opaque-no-owner",
    "repeat-after-completion","control-error-pair","control-open-token",
    "control-file-bytes","control-sync-call","control-close-call","control-write-once"
};
typedef struct {
    uint64_t magic;
    unsigned selector;
    int directory_fd,report_fd;
    of_identity directory;
    uid_t uid;
} handoff;
typedef struct { uintptr_t pc,lr,fp; } request_value;
typedef struct {
    uint64_t magic;
    size_t bytes;
    unsigned selector,infrastructure;
    bool inherited_fd_list_valid,complete_table,prepared,model_stage,model_commit;
    bool generated_stage,generated_commit,freeze_ok,cleanup_ok,repeat_context_unchanged;
    int directory_fd,report_fd;
    char owner_before[AC_ID_BYTES],owner_after[AC_ID_BYTES];
    request_value request_before,request_after,repeat_request;
    ac_snapshot initial,first,second,after_cleanup;
    unsigned calls;
    ac_request_result first_return,second_return;
    of_report precleanup;
    of_cleanup_record external_cleanup;
} child_report;
typedef struct {
    const char *name;
    of_native_result result;
} supervisor_event;
typedef struct {
    supervisor_event events[JOURNAL_CAP];
    unsigned count;
    bool infrastructure,child_confirmed_exit,report_valid,conversions_valid;
    bool directory_valid,sentinel_ready,identity_checked,observer_identity_checked;
    bool absent,file_present,file_unlinked,directory_removed;
    bool report_identity_checked;
    int child_status,child_exit,dirfd,rawfd;
    pid_t child_pid;
    of_identity directory,sentinel_identity,raw_report_identity,file_identity,observer_identity;
    unsigned content_length;
    unsigned char content[65];
    child_report child;
} supervisor;
static const char *check_names[CHECK_CAP],*failed_names[CHECK_CAP];
static unsigned checks,failures;
static void check(bool ok,const char *name)
{
    if(checks>=CHECK_CAP) abort();
    check_names[checks++]=name;
    if(!ok) failed_names[failures++]=name;
}
static bool collision_case(enum selector_id s) { return s==COLLISION || s==C_ERROR; }
static void owner_id(char id[AC_ID_BYTES],char c)
{
    memset(id,c,32); id[32]=0;
}
static of_native_result result_pair(int64_t raw,int saved_error)
{
    of_native_result r={true,true,raw,raw<0,raw<0?saved_error:0};
    return r;
}
static void event(supervisor *s,const char *name,int64_t raw,int saved_error)
{
    if(s->count>=JOURNAL_CAP) { s->infrastructure=true; return; }
    s->events[s->count++]=(supervisor_event){name,result_pair(raw,saved_error)};
}
static of_identity identity(const struct stat *st)
{
    return (of_identity){st->st_dev,st->st_ino,st->st_uid,st->st_nlink,st->st_mode,st->st_size};
}
static bool identity_convertible(const of_identity *id)
{
    return (dev_t)(uintmax_t)id->device==id->device &&
           (ino_t)(uintmax_t)id->inode==id->inode &&
           (uid_t)(uintmax_t)id->uid==id->uid &&
           (nlink_t)(uintmax_t)id->links==id->links &&
           (mode_t)(uintmax_t)id->mode==id->mode &&
           id->size>=0 && (off_t)(intmax_t)id->size==id->size;
}
static bool same_object(const of_identity *a,const of_identity *b)
{
    return a->device==b->device && a->inode==b->inode;
}
static bool regular_owned(const of_identity *id,uid_t uid)
{
    return S_ISREG(id->mode) && id->uid==uid && id->links==(nlink_t)1 &&
           identity_convertible(id);
}
static bool owner_equal(const ac_owner *a,const ac_owner *b)
{
    return a->kind==b->kind && a->index==b->index && a->process_id==b->process_id &&
           memcmp(a->id,b->id,AC_ID_BYTES)==0;
}
static bool snapshot_equal(const ac_snapshot *a,const ac_snapshot *b)
{
    if(a->state!=b->state || a->has_payload!=b->has_payload ||
       a->result.selection_known!=b->result.selection_known ||
       !owner_equal(&a->result.owner,&b->result.owner) ||
       a->result.known_failure!=b->result.known_failure ||
       a->result.short_write!=b->result.short_write ||
       a->result.violation!=b->result.violation ||
       a->result.event_count!=b->result.event_count) return false;
    for(unsigned i=0;i<AC_EVENT_CAPACITY;++i) {
        const ac_event *x=&a->result.events[i],*y=&b->result.events[i];
        if(x->kind!=y->kind || x->entered!=y->entered ||
           x->returned_recorded!=y->returned_recorded || x->raw!=y->raw ||
           x->errno_valid!=y->errno_valid || x->error_number!=y->error_number ||
           x->requested_length!=y->requested_length ||
           memcmp(x->payload,y->payload,AC_ID_BYTES)!=0) return false;
    }
    return true;
}
static bool pair_success(const of_native_result *r,int64_t raw)
{
    return r->entered && r->returned && r->raw==raw && !r->error_valid && r->error_number==0;
}
static bool pair_failure(const of_native_result *r,int error)
{
    return r->entered && r->returned && r->raw==-1 && r->error_valid && r->error_number==error;
}
static bool same_pair(const of_native_result *r,ac_io_value p)
{
    return r->entered && r->returned && r->raw==p.raw &&
           p.failure_errno==(r->error_valid?r->error_number:0);
}
/* Enumeration is solely before exec/ordinary entry. All explicit inherited
 * descriptors are declared; no close sweep occurs after the attempt begins. */
static bool fd_inventory(int directory_fd,int report_fd,bool close_others)
{
    DIR *d=opendir("/proc/self/fd");
    if(!d) return false;
    int iterator=dirfd(d);
    bool ok=true;
    errno=0;
    struct dirent *entry;
    while((entry=readdir(d))!=NULL) {
        char *end=NULL;
        errno=0;
        long number=strtol(entry->d_name,&end,10);
        if(errno || !end || *end || number<0 || number>INT_MAX) {
            if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) ok=false;
            continue;
        }
        int fd=(int)number;
        if(fd<=2 || fd==directory_fd || fd==report_fd || fd==iterator) continue;
        if(!close_others || close(fd)!=0) ok=false;
        errno=0;
    }
    if(errno) ok=false;
    if(closedir(d)!=0) ok=false;
    return ok;
}
static bool parse_fd(const char *text,int *out)
{
    char *end=NULL;
    errno=0;
    long v=strtol(text,&end,10);
    if(errno || !end || *end || v<3 || v>INT_MAX) return false;
    *out=(int)v; return true;
}
static int child_main(int argc,char **argv)
{
    static handoff h;
    static child_report out;
    static of_context context,unused_context;
    int directory_fd=-1,report_fd=-1;
    if(argc!=4 || !parse_fd(argv[2],&directory_fd) || !parse_fd(argv[3],&report_fd) ||
       directory_fd==report_fd) return 121;
    ssize_t got=pread(report_fd,&h,sizeof h,0);
    if(got!=(ssize_t)sizeof h || h.magic!=HANDOFF_MAGIC || h.selector>=SELECTORS ||
       h.directory_fd!=directory_fd || h.report_fd!=report_fd) return 122;
    out.magic=REPORT_MAGIC; out.bytes=sizeof out; out.selector=h.selector;
    out.directory_fd=directory_fd; out.report_fd=report_fd;
    out.inherited_fd_list_valid=fd_inventory(directory_fd,report_fd,false);
    out.complete_table=of_provider_table.open && of_provider_table.write &&
        of_provider_table.sync && of_provider_table.close && of_provider_table.unblamed;
    owner_id(out.owner_before,'a');
    memcpy(out.owner_after,out.owner_before,AC_ID_BYTES);
    enum selector_id selected=(enum selector_id)h.selector;
    of_mode mode=selected==READONLY?OF_READONLY:selected==CLOSE_REFUSAL?OF_CLOSE_REFUSAL_MODE:
        selected==INVALID_WRITE?OF_INVALID_WRITE_MODE:OF_NORMAL;
    out.prepared=of_prepare(&context,directory_fd,&h.directory,h.uid,mode);
    out.model_stage=ac_model_stage_module(0,out.owner_after,UINT64_C(4294967297),0x1000,0x2000);
    out.model_commit=ac_model_commit_module(0);
    if(selected==GENERATED) {
        out.generated_stage=ac_model_stage_generated(3,RC_OWNER_COMMUNITY,UINT64_C(4294967297),
            out.owner_after,(rc_span){0x8000,0x8008},(rc_span){0x9000,0x9010});
        out.generated_commit=ac_model_commit_generated(3);
    }
    /* Both request/table/context objects exist before entry. The second context
     * stays unused because the ordinary terminal loser cannot bind providers. */
    const ap_provider_table second_table=of_provider_table;
    ac_request first={selected==GENERATED?0x8000:selected==NO_OWNER?0xdead0000:0x1100,
                      0,0,&of_provider_table,&context};
    const ac_request second={0xbeef0000,0xfeed0000,0,&second_table,&unused_context};
    out.request_before=(request_value){first.pc,first.lr,first.fp};
    out.repeat_request=(request_value){second.pc,second.lr,second.fp};
    ac_observe(&out.initial);
    if(!out.inherited_fd_list_valid || !out.complete_table || !out.prepared ||
       !out.model_stage || !out.model_commit ||
       (selected==GENERATED && (!out.generated_stage || !out.generated_commit))) {
        out.infrastructure=1;
    } else {
        out.first_return=ac_run(&first); out.calls=1;
        ac_observe(&out.first);
        if(selected==REPEAT) {
            owner_id(out.owner_after,'z');
            first.pc=0xabc0000; first.lr=0xdef0000; first.fp=0x12340000;
            out.second_return=ac_run(&second); out.calls=2;
            ac_observe(&out.second);
        }
        out.repeat_context_unchanged=unused_context.directory_fd==0 &&
            unused_context.report.callback_count==0 && unused_context.report.native_count==0 &&
            unused_context.report.query_count==0 && !unused_context.report.setup_complete &&
            !unused_context.cleanup_requested;
    }
    out.request_after=(request_value){first.pc,first.lr,first.fp};
    out.freeze_ok=of_freeze(&context,&out.precleanup);
    out.cleanup_ok=of_cleanup_held(&context,&out.external_cleanup);
    ac_observe(&out.after_cleanup);
    /* This envelope precedes its own output operation. It makes no claim that
     * the following write returned. Parent exact-size pread plus normal child
     * exit are separate evidence. No explicit descriptor acquisition occurs here. */
    ssize_t written=pwrite(report_fd,&out,sizeof out,0);
    int saved_error=errno;
    if(written!=(ssize_t)sizeof out) {
        fprintf(stderr,"child evidence write raw=%jd error=%d\n",(intmax_t)written,
                written<0?saved_error:0);
        return 123;
    }
    return out.infrastructure?124:0; /* ordinary main return permits sanitizer exit work */
}
static bool envelope_shape(const child_report *r,enum selector_id selected)
{
    const of_report *p=&r->precleanup;
    return r->magic==REPORT_MAGIC && r->bytes==sizeof *r && r->selector==(unsigned)selected &&
        r->calls<=2 && p->callback_count<=OF_CALLBACK_CAPACITY &&
        p->native_count<=OF_NATIVE_CAPACITY && p->query_count<=OF_QUERY_CAPACITY &&
        r->first.result.event_count<=AC_EVENT_CAPACITY &&
        r->second.result.event_count<=AC_EVENT_CAPACITY &&
        r->after_cleanup.result.event_count<=AC_EVENT_CAPACITY;
}
static int record_fstat(supervisor *s,const char *name,int fd,of_identity *out)
{
    struct stat st;
    int raw=fstat(fd,&st); int e=errno;
    event(s,name,raw,e);
    if(raw==0) *out=identity(&st);
    return raw;
}
static int record_name(supervisor *s,const char *name,int fd,const char *leaf,of_identity *out)
{
    struct stat st;
    int raw=fstatat(fd,leaf,&st,AT_SYMLINK_NOFOLLOW); int e=errno;
    event(s,name,raw,e);
    if(raw==0) *out=identity(&st);
    return raw;
}
static bool report_name(const char *path,char *name,size_t capacity)
{
    const char prefix[]="/evidence/";
    if(strncmp(path,prefix,sizeof prefix-1)!=0) return false;
    const char *p=path+sizeof prefix-1;
    size_t n=strlen(p);
    if(n<6 || n+1>capacity || strcmp(p+n-5,".json")!=0) return false;
    for(size_t i=0;i<n;++i)
        if(!((p[i]>='a'&&p[i]<='z') || (p[i]>='A'&&p[i]<='Z') ||
             (p[i]>='0'&&p[i]<='9') || p[i]=='-' || p[i]=='_' || p[i]=='.')) return false;
    if(p[0]=='.') return false;
    memcpy(name,p,n+1); return true;
}
static bool conversions(const supervisor *s)
{
    if(!identity_convertible(&s->directory) || !identity_convertible(&s->raw_report_identity))
        return false;
    if(s->sentinel_ready && !identity_convertible(&s->sentinel_identity)) return false;
    if(s->file_present && (!identity_convertible(&s->file_identity) ||
       !identity_convertible(&s->observer_identity))) return false;
    const of_report *r=&s->child.precleanup;
    if(!identity_convertible(&r->expected_directory) ||
       (uid_t)(uintmax_t)r->expected_uid!=r->expected_uid) return false;
    if(r->creation_identity_available && !identity_convertible(&r->creation_identity)) return false;
    for(unsigned i=0;i<r->query_count;++i)
        if(r->queries[i].identity_available && !identity_convertible(&r->queries[i].identity)) return false;
    return true;
}
static bool supervise(supervisor *s,enum selector_id selected,const char *output,int *jsonfd)
{
    char name[128],raw_name[160],directory_path[]="/tmp/of-witness-XXXXXX",executable[4096];
    int evidence=-1,tmp=-1,directory=-1,rawfd=-1;
    *jsonfd=-1; s->dirfd=-1; s->rawfd=-1; s->child_exit=-1;
    if(!report_name(output,name,sizeof name)) return false;
    if(snprintf(raw_name,sizeof raw_name,"%s.child.bin",name)<0) return false;
    evidence=open("/evidence",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    event(s,"evidence-directory-open",evidence,errno);
    if(evidence<0) return false;
    *jsonfd=openat(evidence,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    event(s,"json-output-open",*jsonfd,errno);
    if(*jsonfd<0) goto failed;
    rawfd=openat(evidence,raw_name,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    event(s,"child-report-open",rawfd,errno); s->rawfd=rawfd;
    if(rawfd<0) goto failed;
    tmp=open("/tmp",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    event(s,"temporary-root-open",tmp,errno);
    if(tmp<0) goto failed;
    char *made=mkdtemp(directory_path); int me=errno;
    event(s,"private-directory-create",made?0:-1,me);
    if(!made) goto failed;
    const char *leaf=strrchr(directory_path,'/')+1;
    directory=openat(tmp,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    event(s,"private-directory-open",directory,errno); s->dirfd=directory;
    if(directory<0 || record_fstat(s,"private-directory-fstat",directory,&s->directory)!=0) goto failed;
    of_identity named_directory={0};
    if(record_name(s,"private-directory-name",tmp,leaf,&named_directory)!=0) goto failed;
    s->directory_valid=S_ISDIR(s->directory.mode) && s->directory.uid==getuid() &&
        (s->directory.mode&0777)==0700 && same_object(&s->directory,&named_directory);
    if(!s->directory_valid) goto failed;
    if(collision_case(selected)) {
        int fd=openat(directory,OF_FILE_NAME,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        event(s,"sentinel-open",fd,errno);
        if(fd<0) goto failed;
        ssize_t put=write(fd,sentinel,sizeof sentinel-1); int pe=errno;
        event(s,"sentinel-write",put,pe);
        int qr=record_fstat(s,"sentinel-fstat",fd,&s->sentinel_identity);
        int cl=close(fd); int ce=errno; event(s,"sentinel-close",cl,ce);
        if(put!=(ssize_t)sizeof sentinel-1 || qr!=0 || cl!=0 ||
           !regular_owned(&s->sentinel_identity,getuid()) || s->sentinel_identity.size!=16)
            goto failed;
        s->sentinel_ready=true;
    }
    handoff h={HANDOFF_MAGIC,(unsigned)selected,directory,rawfd,s->directory,getuid()};
    ssize_t put=pwrite(rawfd,&h,sizeof h,0); int pe=errno;
    event(s,"handoff-write",put,pe);
    if(put!=(ssize_t)sizeof h) goto failed;
    ssize_t pathsize=readlink("/proc/self/exe",executable,sizeof executable-1); int re=errno;
    event(s,"self-executable-readlink",pathsize,re);
    if(pathsize<=0 || (size_t)pathsize>=sizeof executable-1) goto failed;
    executable[pathsize]=0;
    char dirarg[32],rawarg[32];
    if(snprintf(dirarg,sizeof dirarg,"%d",directory)<0 ||
       snprintf(rawarg,sizeof rawarg,"%d",rawfd)<0) goto failed;
    pid_t pid=fork(); int fe=errno; event(s,"fresh-witness-fork",pid,fe);
    if(pid<0) goto failed;
    if(pid==0) {
        if(fcntl(directory,F_SETFD,0)<0 || fcntl(rawfd,F_SETFD,0)<0 ||
           !fd_inventory(directory,rawfd,true)) _exit(125);
        char *const child_argv[]={executable,"--child",dirarg,rawarg,NULL};
        execv(executable,child_argv);
        _exit(126);
    }
    s->child_pid=pid;
    int status=0;
    pid_t waited=waitpid(pid,&status,0); int we=errno;
    event(s,"fresh-witness-wait",waited,we);
    if(waited!=pid || !WIFEXITED(status)) goto failed;
    s->child_confirmed_exit=true; s->child_status=status; s->child_exit=WEXITSTATUS(status);
    if(s->child_exit!=0) goto failed;
    if(record_fstat(s,"child-report-fstat",rawfd,&s->raw_report_identity)!=0) goto failed;
    s->report_identity_checked=regular_owned(&s->raw_report_identity,getuid()) &&
        s->raw_report_identity.size==(off_t)sizeof(child_report);
    if(!s->report_identity_checked) goto failed;
    ssize_t got=pread(rawfd,&s->child,sizeof s->child,0); int ge=errno;
    event(s,"child-report-pread",got,ge);
    if(got!=(ssize_t)sizeof s->child || !envelope_shape(&s->child,selected)) goto failed;
    s->report_valid=true;
    int nr=record_name(s,"post-exit-file-name",directory,OF_FILE_NAME,&s->file_identity);
    const of_native_result *name_result=&s->events[s->count-1].result;
    if(nr<0) {
        if(selected!=NO_OWNER || !pair_failure(name_result,ENOENT)) goto failed;
        s->absent=true;
    } else {
        s->file_present=true;
        const of_identity *expected=collision_case(selected)?&s->sentinel_identity:
            &s->child.precleanup.creation_identity;
        s->identity_checked=(collision_case(selected)||s->child.precleanup.creation_identity_available) &&
            regular_owned(&s->file_identity,getuid()) && s->file_identity.size<=64 &&
            same_object(&s->file_identity,expected);
        if(!s->identity_checked) goto failed;
        int observe=openat(directory,OF_FILE_NAME,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        event(s,"post-exit-observer-open",observe,errno);
        if(observe<0) goto failed;
        int fr=record_fstat(s,"post-exit-observer-fstat",observe,&s->observer_identity);
        s->observer_identity_checked=fr==0 && regular_owned(&s->observer_identity,getuid()) &&
            same_object(&s->observer_identity,&s->file_identity) &&
            s->observer_identity.size==s->file_identity.size;
        if(!s->observer_identity_checked) {
            int cl=close(observe); int ce=errno; event(s,"invalid-observer-close",cl,ce);
            goto failed;
        }
        ssize_t bytes=pread(observe,s->content,sizeof s->content,0); int be=errno;
        event(s,"post-exit-file-pread",bytes,be);
        int cl=close(observe); int ce=errno; event(s,"post-exit-observer-close",cl,ce);
        if(bytes<0 || bytes!=s->file_identity.size || bytes>64 || cl!=0) goto failed;
        s->content_length=(unsigned)bytes;
        of_identity before_unlink={0};
        if(record_name(s,"owned-file-before-unlink",directory,OF_FILE_NAME,&before_unlink)!=0 ||
           !same_object(&before_unlink,&s->file_identity) || !regular_owned(&before_unlink,getuid()))
            goto failed;
        int un=unlinkat(directory,OF_FILE_NAME,0); int ue=errno;
        event(s,"owned-file-unlink",un,ue);
        if(un!=0) goto failed;
        s->file_unlinked=true;
    }
    of_identity before_remove={0};
    if(record_name(s,"owned-directory-before-remove",tmp,leaf,&before_remove)!=0 ||
       !same_object(&before_remove,&s->directory) || !S_ISDIR(before_remove.mode)) goto failed;
    int rm=unlinkat(tmp,leaf,AT_REMOVEDIR); int de=errno;
    event(s,"owned-empty-directory-remove",rm,de);
    if(rm!=0) goto failed;
    s->directory_removed=true;
    s->conversions_valid=conversions(s);
    if(!s->conversions_valid) goto failed;
    /* Ordinary parent descriptor cleanup is separate from witness/attempt state. */
    { int cl=close(rawfd); int e=errno; event(s,"child-report-close",cl,e); rawfd=-1; if(cl) goto failed; }
    { int cl=close(directory); int e=errno; event(s,"directory-capability-close",cl,e); directory=-1; if(cl) goto failed; }
    { int cl=close(tmp); int e=errno; event(s,"temporary-root-close",cl,e); tmp=-1; if(cl) goto failed; }
    { int cl=close(evidence); int e=errno; event(s,"evidence-directory-close",cl,e); evidence=-1; if(cl) goto failed; }
    return true;
failed:
    /* No retry, recursive cleanup, unknown-token use or forced child signal.
     * Infrastructure failure preserves any outstanding owned names for diagnosis. */
    s->infrastructure=true;
    return false;
}

static const of_callback_record *callback(const of_report *r,of_operation op)
{
    for(unsigned i=0;i<r->callback_count;++i) if(r->callbacks[i].operation==op) return &r->callbacks[i];
    return NULL;
}
static const of_native_record *native_call(const of_report *r,of_operation op)
{
    for(unsigned i=0;i<r->native_count;++i) if(r->native[i].operation==op) return &r->native[i];
    return NULL;
}
static unsigned native_count(const of_report *r,of_operation op)
{
    unsigned n=0;
    for(unsigned i=0;i<r->native_count;++i) if(r->native[i].operation==op) ++n;
    return n;
}
static bool content_is(const supervisor *s,char value,unsigned count)
{
    if(!s->file_present || s->content_length!=count || s->file_identity.size!=(off_t)count) return false;
    for(unsigned i=0;i<count;++i) if(s->content[i]!=(unsigned char)value) return false;
    return true;
}
static bool public_matches_prepared(const child_report *r,enum selector_id selected)
{
    ac_snapshot expected={0};
    if(selected==INVALID_WRITE) {
        expected.state=AC_ACTIVE_INCOMPLETE;
        return snapshot_equal(&r->first,&expected);
    }
    expected.state=AC_ORDINARY_RETURNED; expected.has_payload=true;
    expected.result.selection_known=true;
    if(selected!=NO_OWNER) {
        expected.result.owner=(ac_owner){AC_COMMUNITY,0,UINT64_C(4294967297),{0}};
        memcpy(expected.result.owner.id,r->owner_before,AC_ID_BYTES);
    }
    ac_event *selection=&expected.result.events[0];
    selection->kind=AC_SELECTION; selection->entered=true; selection->returned_recorded=true;
    expected.result.event_count=1;
    const of_report *p=&r->precleanup;
    for(unsigned i=0;i<p->callback_count;++i) {
        const of_callback_record *c=&p->callbacks[i];
        unsigned at=expected.result.event_count++;
        if(at>=AC_EVENT_CAPACITY) return false;
        ac_event *e=&expected.result.events[at];
        e->kind=c->operation==OF_UNBLAMED?AC_UNBLAMED:c->operation==OF_OPEN?AC_OPEN:
            c->operation==OF_WRITE?AC_WRITE:c->operation==OF_SYNC?AC_FSYNC:AC_CLOSE;
        e->entered=true; e->returned_recorded=true;
        if(c->operation!=OF_UNBLAMED) {
            e->raw=c->supplied.raw;
            e->errno_valid=e->raw==-1;
            e->error_number=e->errno_valid?c->supplied.failure_errno:0;
            if(e->raw==-1) expected.result.known_failure=true;
        }
        if(c->operation==OF_WRITE) {
            e->requested_length=32; memcpy(e->payload,r->owner_before,AC_ID_BYTES);
            if(e->raw>=0 && e->raw<32) expected.result.short_write=true;
        }
    }
    return snapshot_equal(&r->first,&expected);
}
static bool public_entry(const child_report *r,enum selector_id selected)
{
    ac_snapshot initial={0};
    ac_request_result wanted=selected==INVALID_WRITE?AC_INCOMPLETE:AC_RETURNED;
    bool ordinary=r->infrastructure==0 && r->inherited_fd_list_valid && r->complete_table &&
        r->prepared && r->model_stage && r->model_commit &&
        snapshot_equal(&r->initial,&initial) && r->calls==(selected==REPEAT?2u:1u) &&
        r->first_return==wanted && r->request_before.lr==0 && r->request_before.fp==0 &&
        r->request_before.pc==(selected==GENERATED?0x8000u:selected==NO_OWNER?0xdead0000u:0x1100u);
    if(selected==GENERATED) ordinary=ordinary && r->generated_stage && r->generated_commit;
    if(selected!=REPEAT)
        ordinary=ordinary && r->request_before.pc==r->request_after.pc &&
            r->request_before.lr==r->request_after.lr && r->request_before.fp==r->request_after.fp &&
            memcmp(r->owner_before,r->owner_after,AC_ID_BYTES)==0;
    return ordinary;
}
static bool journal_valid(const child_report *r,enum selector_id selected)
{
    const of_report *p=&r->precleanup;
    unsigned expected_callbacks=selected==NO_OWNER?1u:collision_case(selected)?1u:
        selected==INVALID_WRITE?2u:4u;
    if(!p->setup_complete || !p->directory_validated || !p->frozen ||
       p->infrastructure!=OF_INFRA_NONE || p->callback_count!=expected_callbacks ||
       p->file_callback_count!=(selected==NO_OWNER?0u:expected_callbacks) ||
       p->unblamed_count!=(selected==NO_OWNER?1u:0u) ||
       p->query_count!=(selected==NO_OWNER||collision_case(selected)?1u:3u)) return false;
    unsigned consumed=0;
    for(unsigned i=0;i<p->callback_count;++i) {
        const of_callback_record *c=&p->callbacks[i];
        of_operation wanted=selected==NO_OWNER?OF_UNBLAMED:(of_operation)i;
        if(c->operation!=wanted || !c->entered || !c->prepared ||
           c->native_first!=consumed || c->native_count>p->native_count-consumed) return false;
        consumed+=c->native_count;
        if(c->operation==OF_OPEN) {
            ac_owner expected={AC_COMMUNITY,0,UINT64_C(4294967297),{0}};
            memcpy(expected.id,r->owner_before,AC_ID_BYTES);
            if(!owner_equal(&c->owner_argument,&expected)) return false;
        } else if(c->operation!=OF_UNBLAMED) {
            const of_callback_record *opened=callback(p,OF_OPEN);
            if(!opened || c->token_argument!=opened->supplied.raw) return false;
            if(c->operation==OF_WRITE &&
               (c->requested_length!=32 || memcmp(c->payload_argument,r->owner_before,AC_ID_BYTES)!=0))
                return false;
        }
    }
    if(consumed!=p->native_count) return false;
    for(unsigned i=0;i<p->query_count;++i) {
        const of_query_record *q=&p->queries[i];
        if(q->kind!=(of_query_kind)i || !pair_success(&q->result,0) || !q->identity_available ||
           !identity_convertible(&q->identity)) return false;
        if(i==0 && (q->descriptor!=r->directory_fd || !S_ISDIR(q->identity.mode) ||
           !same_object(&q->identity,&p->expected_directory)))
            return false;
        if(i==1 && q->descriptor!=p->saved_descriptor) return false;
        if(i==2 && (q->descriptor!=r->directory_fd || q->flags!=AT_SYMLINK_NOFOLLOW)) return false;
        if(i && (!regular_owned(&q->identity,p->expected_uid) ||
           !same_object(&q->identity,&p->creation_identity))) return false;
    }
    return true;
}
static bool native_owned(const child_report *r,enum selector_id selected)
{
    const of_report *p=&r->precleanup;
    if(native_count(p,OF_OPEN)>1 || native_count(p,OF_WRITE)>2 ||
       native_count(p,OF_SYNC)>1 || native_count(p,OF_CLOSE)>1) return false;
    bool opened=false,closed=false;
    for(unsigned i=0;i<p->native_count;++i) {
        const of_native_record *n=&p->native[i];
        const of_native_result *v=&n->result;
        if(!v->entered || !v->returned || v->raw < -1 ||
           v->error_valid!=(v->raw==-1) || (!v->error_valid && v->error_number!=0) || closed)
            return false;
        if(n->operation==OF_OPEN) {
            int flags=(p->mode==OF_READONLY?O_RDONLY:O_WRONLY)|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC;
            if(i!=0 || n->descriptor!=r->directory_fd || n->flags!=flags ||
               n->creation_mode!=(mode_t)0600) return false;
            if(collision_case(selected)) { if(!pair_failure(v,EEXIST)) return false; }
            else if(v->raw<0) return false;
            opened=v->raw>=0;
            if(opened && (!p->has_saved_descriptor || v->raw!=p->saved_descriptor ||
               !p->creation_identity_available)) return false;
        } else {
            if(!opened || !p->has_saved_descriptor || n->descriptor!=p->saved_descriptor ||
               n->descriptor<0) return false;
            if(n->operation==OF_WRITE) {
                if(n->requested_length!=32 || n->payload[32]!=0 || v->raw>32) return false;
                if(p->mode==OF_READONLY) { if(!pair_failure(v,EBADF)) return false; }
                else if(!pair_success(v,32)) return false;
                for(unsigned j=0;j<32;++j)
                    if(n->payload[j]!='a' && n->payload[j]!='z') return false;
            } else if(n->operation==OF_CLOSE) {
                if(!pair_success(v,0)) return false;
                closed=true;
            } else if(n->operation==OF_SYNC) {
                if(v->raw!=0 && v->raw!=-1) return false;
                if(p->mode!=OF_READONLY && !pair_success(v,0)) return false;
            } else return false;
        }
    }
    return true;
}
static bool cleanup_valid(const child_report *r)
{
    const of_report *p=&r->precleanup;
    const of_cleanup_record *c=&r->external_cleanup;
    bool eligible=p->has_saved_descriptor && p->saved_descriptor>=0 &&
        (p->custody==OF_VERIFIED_HELD || p->custody==OF_OWNED_HELD_UNVALIDATED);
    bool okay=r->freeze_ok && r->cleanup_ok && c->requested && c->before==p->custody &&
        c->descriptor==(p->has_saved_descriptor?p->saved_descriptor:-1) &&
        c->eligible==eligible && c->attempted==eligible &&
        snapshot_equal(&r->after_cleanup,r->calls==2?&r->second:&r->first);
    if(eligible)
        okay=okay && pair_success(&c->result,0) && c->after==OF_CLOSED_RETURNED;
    else
        okay=okay && !c->result.entered && !c->result.returned &&
            c->result.raw==0 && !c->result.error_valid && c->result.error_number==0 &&
            c->after==c->before;
    return okay;
}
static bool normal_native(const of_report *p)
{
    if(p->callback_count!=4 || p->native_count!=4 || p->custody!=OF_CLOSED_RETURNED) return false;
    for(unsigned i=0;i<4;++i) {
        const of_callback_record *c=&p->callbacks[i];
        const of_native_record *n=&p->native[i];
        if(c->operation!=(of_operation)i || n->operation!=(of_operation)i ||
           c->origin!=OF_NATIVE || c->native_count!=1 || !same_pair(&n->result,c->supplied))
            return false;
        if(i==0) { if(n->result.raw<0) return false; }
        else if(!pair_success(&n->result,i==1?32:0)) return false;
    }
    return true;
}
static bool exact_error(const supervisor *s)
{
    const of_report *p=&s->child.precleanup;
    const of_callback_record *c=callback(p,OF_OPEN);
    const of_native_record *n=native_call(p,OF_OPEN);
    return c && n && p->native_count==1 && c->origin==OF_NATIVE &&
        pair_failure(&n->result,EEXIST) && same_pair(&n->result,c->supplied) &&
        s->child.first.result.known_failure && s->child.first.result.events[1].errno_valid &&
        s->child.first.result.events[1].error_number==EEXIST &&
        s->content_length==16 && memcmp(s->content,sentinel,16)==0;
}
static bool token_corresponds(const supervisor *s)
{
    const of_report *p=&s->child.precleanup;
    const of_callback_record *c=callback(p,OF_OPEN);
    const of_native_record *n=native_call(p,OF_OPEN);
    return c && n && normal_native(p) && n->result.raw==p->saved_descriptor &&
        c->supplied.raw==n->result.raw && !s->child.external_cleanup.eligible &&
        content_is(s,'a',32);
}
static bool bytes_correspond(const supervisor *s)
{
    const of_report *p=&s->child.precleanup;
    const of_callback_record *c=callback(p,OF_WRITE);
    const of_native_record *n=native_call(p,OF_WRITE);
    return c && n && c->requested_length==n->requested_length &&
        memcmp(c->payload_argument,n->payload,AC_ID_BYTES)==0 && content_is(s,'a',32);
}
static bool sync_corresponds(const supervisor *s)
{
    const of_report *p=&s->child.precleanup;
    const of_callback_record *c=callback(p,OF_SYNC);
    const of_native_record *n=native_call(p,OF_SYNC);
    return c && n && c->origin==OF_NATIVE && c->native_count==1 &&
        native_count(p,OF_SYNC)==1 && pair_success(&n->result,0) &&
        same_pair(&n->result,c->supplied);
}
static bool readonly_sync_corresponds(const supervisor *s)
{
    const of_report *p=&s->child.precleanup;
    const of_callback_record *c=callback(p,OF_SYNC);
    const of_native_record *n=native_call(p,OF_SYNC);
    return c && n && c->origin==OF_NATIVE && c->native_count==1 &&
        native_count(p,OF_SYNC)==1 && (n->result.raw==0 || n->result.raw==-1) &&
        same_pair(&n->result,c->supplied);
}
static bool close_corresponds(const supervisor *s)
{
    const of_report *p=&s->child.precleanup;
    const of_callback_record *c=callback(p,OF_CLOSE);
    const of_native_record *n=native_call(p,OF_CLOSE);
    return c && n && c->origin==OF_NATIVE && c->native_count==1 &&
        native_count(p,OF_CLOSE)==1 && pair_success(&n->result,0) &&
        same_pair(&n->result,c->supplied) && p->custody==OF_CLOSED_RETURNED &&
        !s->child.external_cleanup.eligible && !s->child.external_cleanup.attempted;
}
static bool write_once(const supervisor *s)
{
    const of_report *p=&s->child.precleanup;
    const of_callback_record *c=callback(p,OF_WRITE);
    const of_native_record *n=native_call(p,OF_WRITE);
    return c && n && c->native_count==1 && native_count(p,OF_WRITE)==1 &&
        pair_success(&n->result,32) && same_pair(&n->result,c->supplied) &&
        content_is(s,'a',32);
}
static void assess(const supervisor *s,enum selector_id selected)
{
    const child_report *r=&s->child;
    const of_report *p=&r->precleanup;
    check(!s->infrastructure && s->directory_valid && s->child_confirmed_exit &&
          s->child_exit==0 && s->report_valid && s->report_identity_checked && s->directory_removed &&
          r->directory_fd==s->dirfd && r->report_fd==s->rawfd &&
          same_object(&p->expected_directory,&s->directory),
          "supervisor-owned-isolated-custody");
    check(public_entry(r,selected),"fresh-child-ordinary-entry-public-state");
    check(native_owned(r,selected),"native-calls-use-owned-descriptors");
    check(public_matches_prepared(r,selected),"prepared-values-match-public-receipts");
    check(journal_valid(r,selected),"provider-journals-have-bounded-custody");
    check(selected==NO_OWNER?s->absent && !s->file_present:
          s->file_present && s->identity_checked && s->observer_identity_checked &&
          s->content_length<=64 && s->file_unlinked,
          "post-exit-observation-is-owned-and-bounded");
    check(cleanup_valid(r),"external-cleanup-does-not-rewrite-attempt");
    check(s->conversions_valid,"native-identity-conversions-are-exact");
    switch(selected) {
    case DIRECT:
    case GENERATED:
        check(normal_native(p) && content_is(s,'a',32) && bytes_correspond(s),
              selected==DIRECT?"direct-owner-reaches-owned-file":"generated-owner-reaches-owned-file");
        break;
    case COLLISION:
        check(exact_error(s) && !p->has_saved_descriptor && p->custody==OF_NO_DESCRIPTOR,
              "exclusive-collision-preserves-owned-sentinel"); break;
    case READONLY: {
        const of_native_record *w=native_call(p,OF_WRITE);
        const of_callback_record *c=callback(p,OF_WRITE);
        check(w && c && p->native_count==4 && pair_failure(&w->result,EBADF) &&
              c->origin==OF_NATIVE && same_pair(&w->result,c->supplied) &&
              readonly_sync_corresponds(s) && close_corresponds(s) && content_is(s,'a',0) &&
              r->first.result.known_failure,"readonly-native-error-still-syncs-and-closes");
        break;
    }
    case CLOSE_REFUSAL: {
        const of_callback_record *c=callback(p,OF_CLOSE);
        check(c && c->origin==OF_SUPPLIED_REFUSAL && c->supplied.raw==-1 &&
              c->supplied.failure_errno==OF_CLOSE_REFUSAL && c->native_count==0 &&
              p->native_count==3 && native_count(p,OF_CLOSE)==0 &&
              p->custody==OF_VERIFIED_HELD && r->external_cleanup.eligible &&
              r->first.result.known_failure && content_is(s,'a',32),
              "supplied-close-refusal-retains-held-custody"); break;
    }
    case INVALID_WRITE: {
        const of_callback_record *c=callback(p,OF_WRITE);
        const of_native_record *w=native_call(p,OF_WRITE);
        check(c && w && c->origin==OF_SUPPLIED_INVALID && c->supplied.raw==33 &&
              c->supplied.failure_errno==0 && pair_success(&w->result,32) &&
              p->native_count==2 && p->custody==OF_VERIFIED_HELD &&
              r->external_cleanup.eligible && content_is(s,'a',32),
              "invalid-supplied-write-keeps-public-attempt-incomplete"); break;
    }
    case NO_OWNER: {
        const of_callback_record *c=callback(p,OF_UNBLAMED);
        check(c && c->origin==OF_OPAQUE_AUTHORED && p->native_count==0 &&
              !p->has_saved_descriptor && p->custody==OF_NO_DESCRIPTOR && s->absent,
              "opaque-no-owner-creates-no-file"); break;
    }
    case REPEAT: {
        char changed[AC_ID_BYTES]; owner_id(changed,'z');
        check(normal_native(p) && content_is(s,'a',32) && r->second_return==AC_SUPPRESSED &&
              snapshot_equal(&r->first,&r->second) && r->repeat_context_unchanged &&
              memcmp(r->owner_after,changed,AC_ID_BYTES)==0 &&
              r->request_after.pc==0xabc0000 && r->request_after.lr==0xdef0000 &&
              r->request_after.fp==0x12340000 && r->repeat_request.pc==0xbeef0000 &&
              r->repeat_request.lr==0xfeed0000 && r->repeat_request.fp==0,
              "terminal-repeat-preserves-copied-owner-and-single-file"); break;
    }
    case C_ERROR: check(exact_error(s),"native-failure-pair-survives-instrumentation"); break;
    case C_TOKEN: check(token_corresponds(s),"native-open-token-and-custody-correspond"); break;
    case C_BYTES: check(bytes_correspond(s),"owned-write-preserves-copied-bytes"); break;
    case C_SYNC: check(sync_corresponds(s),"sync-receipt-has-native-call-evidence"); break;
    case C_CLOSE: check(close_corresponds(s),"close-receipt-has-native-call-evidence"); break;
    case C_ONCE: check(write_once(s),"terminal-provider-writes-once"); break;
    default: abort();
    }
}

static void js_bool(FILE *f,bool value) { fputs(value?"true":"false",f); }
static void js_hex(FILE *f,const void *value,size_t count)
{
    const unsigned char *p=value;
    fputc('"',f);
    for(size_t i=0;i<count;++i) fprintf(f,"%02x",(unsigned)p[i]);
    fputc('"',f);
}
static void js_pair(FILE *f,const of_native_result *r)
{
    fputs("{\"entered\":",f); js_bool(f,r->entered);
    fputs(",\"returned\":",f); js_bool(f,r->returned);
    fprintf(f,",\"raw\":%" PRId64 ",\"error_valid\":",r->raw); js_bool(f,r->error_valid);
    fprintf(f,",\"error_number\":%d}",r->error_number);
}
static void js_identity(FILE *f,const of_identity *id)
{
    /* Caller separately verifies every present native identity is losslessly
     * representable. Absent zero cells remain zeros, not inferred observations. */
    fprintf(f,"{\"device\":%ju,\"inode\":%ju,\"uid\":%ju,\"links\":%ju,\"mode\":%ju,\"size\":%jd}",
        (uintmax_t)id->device,(uintmax_t)id->inode,(uintmax_t)id->uid,
        (uintmax_t)id->links,(uintmax_t)id->mode,(intmax_t)id->size);
}
static void js_owner(FILE *f,const ac_owner *o)
{
    fprintf(f,"{\"kind\":%u,\"index\":%" PRIu32 ",\"process_id\":%" PRIu64 ",\"id_hex\":",
        (unsigned)o->kind,o->index,o->process_id);
    js_hex(f,o->id,AC_ID_BYTES); fputc('}',f);
}
static void js_snapshot(FILE *f,const ac_snapshot *s)
{
    fprintf(f,"{\"state\":%u,\"has_payload\":",(unsigned)s->state); js_bool(f,s->has_payload);
    fputs(",\"result\":{\"selection_known\":",f); js_bool(f,s->result.selection_known);
    fputs(",\"owner\":",f); js_owner(f,&s->result.owner);
    fputs(",\"known_failure\":",f); js_bool(f,s->result.known_failure);
    fputs(",\"short_write\":",f); js_bool(f,s->result.short_write);
    fputs(",\"violation\":",f); js_bool(f,s->result.violation);
    fprintf(f,",\"event_count\":%u,\"events\":[",s->result.event_count);
    for(unsigned i=0;i<AC_EVENT_CAPACITY;++i) {
        const ac_event *e=&s->result.events[i];
        if(i) fputc(',',f);
        fprintf(f,"{\"kind\":%u,\"entered\":",(unsigned)e->kind); js_bool(f,e->entered);
        fputs(",\"returned_recorded\":",f); js_bool(f,e->returned_recorded);
        fprintf(f,",\"raw\":%" PRId64 ",\"errno_valid\":",e->raw); js_bool(f,e->errno_valid);
        fprintf(f,",\"error_number\":%d,\"requested_length\":%zu,\"payload_hex\":",
            e->error_number,e->requested_length); js_hex(f,e->payload,AC_ID_BYTES); fputc('}',f);
    }
    fputs("]}}",f);
}
static void js_request(FILE *f,const request_value *r)
{
    fprintf(f,"{\"pc\":%" PRIuPTR ",\"lr\":%" PRIuPTR ",\"fp\":%" PRIuPTR "}",r->pc,r->lr,r->fp);
}
static void js_provider(FILE *f,const of_report *r)
{
    fputs("{\"setup_complete\":",f); js_bool(f,r->setup_complete);
    fputs(",\"directory_validated\":",f); js_bool(f,r->directory_validated);
    fputs(",\"frozen\":",f); js_bool(f,r->frozen);
    fprintf(f,",\"mode\":%u,\"expected_directory\":",(unsigned)r->mode); js_identity(f,&r->expected_directory);
    fprintf(f,",\"expected_uid\":%ju,\"custody\":%u,\"has_saved_descriptor\":",
        (uintmax_t)r->expected_uid,(unsigned)r->custody); js_bool(f,r->has_saved_descriptor);
    fprintf(f,",\"saved_descriptor\":%d,\"creation_identity_available\":",r->saved_descriptor);
    js_bool(f,r->creation_identity_available);
    fputs(",\"creation_identity\":",f); js_identity(f,&r->creation_identity);
    fprintf(f,",\"infrastructure\":%u,\"callback_count\":%u,\"file_callback_count\":%u,"
        "\"unblamed_count\":%u,\"native_count\":%u,\"query_count\":%u,\"callbacks\":[",
        (unsigned)r->infrastructure,r->callback_count,r->file_callback_count,
        r->unblamed_count,r->native_count,r->query_count);
    for(unsigned i=0;i<r->callback_count && i<OF_CALLBACK_CAPACITY;++i) {
        const of_callback_record *c=&r->callbacks[i];
        if(i) fputc(',',f);
        fprintf(f,"{\"operation\":%u,\"entered\":",(unsigned)c->operation); js_bool(f,c->entered);
        fputs(",\"prepared\":",f); js_bool(f,c->prepared);
        fprintf(f,",\"origin\":%u,\"supplied\":{\"raw\":%" PRId64 ",\"failure_errno\":%d},\"owner_argument\":",
            (unsigned)c->origin,c->supplied.raw,c->supplied.failure_errno);
        js_owner(f,&c->owner_argument);
        fprintf(f,",\"token_argument\":%" PRId64 ",\"requested_length\":%zu,\"payload_argument_hex\":",
            c->token_argument,c->requested_length); js_hex(f,c->payload_argument,AC_ID_BYTES);
        fprintf(f,",\"native_first\":%u,\"native_count\":%u,\"custody_before\":%u,\"custody_after\":%u}",
            c->native_first,c->native_count,(unsigned)c->custody_before,(unsigned)c->custody_after);
    }
    fputs("],\"native\":[",f);
    for(unsigned i=0;i<r->native_count && i<OF_NATIVE_CAPACITY;++i) {
        const of_native_record *n=&r->native[i];
        if(i) fputc(',',f);
        fprintf(f,"{\"operation\":%u,\"descriptor\":%d,\"flags\":%d,\"creation_mode\":%ju,"
            "\"requested_length\":%zu,\"payload_hex\":",(unsigned)n->operation,n->descriptor,
            n->flags,(uintmax_t)n->creation_mode,n->requested_length);
        js_hex(f,n->payload,AC_ID_BYTES); fputs(",\"result\":",f); js_pair(f,&n->result); fputc('}',f);
    }
    fputs("],\"queries\":[",f);
    for(unsigned i=0;i<r->query_count && i<OF_QUERY_CAPACITY;++i) {
        const of_query_record *q=&r->queries[i];
        if(i) fputc(',',f);
        fprintf(f,"{\"kind\":%u,\"descriptor\":%d,\"flags\":%d,\"result\":",
            (unsigned)q->kind,q->descriptor,q->flags); js_pair(f,&q->result);
        fputs(",\"identity_available\":",f); js_bool(f,q->identity_available);
        fputs(",\"identity\":",f); js_identity(f,&q->identity); fputc('}',f);
    }
    fputs("]}",f);
}
static void js_cleanup(FILE *f,const of_cleanup_record *c)
{
    fputs("{\"requested\":",f); js_bool(f,c->requested);
    fputs(",\"eligible\":",f); js_bool(f,c->eligible);
    fputs(",\"attempted\":",f); js_bool(f,c->attempted);
    fprintf(f,",\"descriptor\":%d,\"before\":%u,\"after\":%u,\"result\":",
        c->descriptor,(unsigned)c->before,(unsigned)c->after); js_pair(f,&c->result); fputc('}',f);
}
static void js_child(FILE *f,const child_report *r)
{
    fprintf(f,"{\"magic\":%" PRIu64 ",\"bytes\":%zu,\"selector_index\":%u,\"infrastructure\":%u,"
        "\"directory_fd\":%d,\"report_fd\":%d,\"inherited_fd_list_valid\":",
        r->magic,r->bytes,r->selector,r->infrastructure,r->directory_fd,r->report_fd);
    js_bool(f,r->inherited_fd_list_valid);
    fputs(",\"complete_table\":",f); js_bool(f,r->complete_table);
    fputs(",\"prepared\":",f); js_bool(f,r->prepared);
    fputs(",\"model_stage\":",f); js_bool(f,r->model_stage);
    fputs(",\"model_commit\":",f); js_bool(f,r->model_commit);
    fputs(",\"generated_stage\":",f); js_bool(f,r->generated_stage);
    fputs(",\"generated_commit\":",f); js_bool(f,r->generated_commit);
    fputs(",\"freeze_ok\":",f); js_bool(f,r->freeze_ok);
    fputs(",\"cleanup_ok\":",f); js_bool(f,r->cleanup_ok);
    fputs(",\"repeat_context_unchanged\":",f); js_bool(f,r->repeat_context_unchanged);
    fputs(",\"owner_before_hex\":",f); js_hex(f,r->owner_before,AC_ID_BYTES);
    fputs(",\"owner_after_hex\":",f); js_hex(f,r->owner_after,AC_ID_BYTES);
    fputs(",\"request_before\":",f); js_request(f,&r->request_before);
    fputs(",\"request_after\":",f); js_request(f,&r->request_after);
    fputs(",\"repeat_request\":",f); js_request(f,&r->repeat_request);
    fprintf(f,",\"calls\":%u,\"first_return\":%u,\"second_return\":%u,\"initial\":",
        r->calls,(unsigned)r->first_return,(unsigned)r->second_return);
    js_snapshot(f,&r->initial); fputs(",\"first\":",f); js_snapshot(f,&r->first);
    fputs(",\"second\":",f); js_snapshot(f,&r->second);
    fputs(",\"after_cleanup\":",f); js_snapshot(f,&r->after_cleanup);
    fputs(",\"precleanup\":",f); js_provider(f,&r->precleanup);
    fputs(",\"external_cleanup\":",f); js_cleanup(f,&r->external_cleanup); fputc('}',f);
}
static void common(FILE *f,enum selector_id selected)
{
    fprintf(f,"\"schema\":1,\"selector\":\"%s\",\"checks\":%u,\"check_names\":[",names[selected],checks);
    for(unsigned i=0;i<checks;++i) fprintf(f,"%s\"%s\"",i?",":"",check_names[i]);
    fprintf(f,"],\"failures\":%u,\"failed_checks\":[",failures);
    for(unsigned i=0;i<failures;++i) fprintf(f,"%s\"%s\"",i?",":"",failed_names[i]);
    fputc(']',f);
}
static void evidence(FILE *f,const supervisor *s,enum selector_id selected)
{
    fputc('{',f); common(f,selected);
    fprintf(f,",\"source_scope\":\"%s\",\"input\":{\"owner_id_hex\":",SOURCE_SCOPE);
    char id[AC_ID_BYTES]; owner_id(id,'a'); js_hex(f,id,AC_ID_BYTES);
    fprintf(f,",\"owner_process_id\":%" PRIu64 ",\"file_name\":\"%s\",\"sentinel_hex\":",
        UINT64_C(4294967297),OF_FILE_NAME); js_hex(f,sentinel,16);
    fputs(",\"report_self_write_claim\":false,\"native_fault_context\":false},\"supervisor\":{",f);
    fputs("\"infrastructure\":",f); js_bool(f,s->infrastructure);
    fputs(",\"directory_valid\":",f); js_bool(f,s->directory_valid);
    fputs(",\"child_confirmed_exit\":",f); js_bool(f,s->child_confirmed_exit);
    fprintf(f,",\"child_pid\":%jd,\"child_status\":%d,\"child_exit\":%d,\"directory_fd\":%d,\"raw_report_fd\":%d,"
        "\"report_valid\":",(intmax_t)s->child_pid,s->child_status,s->child_exit,s->dirfd,s->rawfd);
    js_bool(f,s->report_valid);
    fputs(",\"report_identity_checked\":",f); js_bool(f,s->report_identity_checked);
    fputs(",\"conversions_valid\":",f); js_bool(f,s->conversions_valid);
    fputs(",\"directory\":",f); js_identity(f,&s->directory);
    fputs(",\"raw_report_identity\":",f); js_identity(f,&s->raw_report_identity);
    fputs(",\"events\":[",f);
    for(unsigned i=0;i<s->count;++i) {
        if(i) fputc(',',f);
        fprintf(f,"{\"operation\":\"%s\",\"result\":",s->events[i].name);
        js_pair(f,&s->events[i].result); fputc('}',f);
    }
    fputs("]},\"child\":",f); js_child(f,&s->child);
    fputs(",\"parent_file\":{\"sentinel_ready\":",f); js_bool(f,s->sentinel_ready);
    fputs(",\"sentinel_identity\":",f); js_identity(f,&s->sentinel_identity);
    fputs(",\"absent\":",f); js_bool(f,s->absent);
    fputs(",\"present\":",f); js_bool(f,s->file_present);
    fputs(",\"identity_checked\":",f); js_bool(f,s->identity_checked);
    fputs(",\"observer_identity_checked\":",f); js_bool(f,s->observer_identity_checked);
    fputs(",\"identity\":",f); js_identity(f,&s->file_identity);
    fputs(",\"observer_identity\":",f); js_identity(f,&s->observer_identity);
    fprintf(f,",\"content_length\":%u,\"content_hex\":",s->content_length);
    js_hex(f,s->content,s->content_length<=65?s->content_length:0);
    fputs("},\"parent_cleanup\":{\"fixed_file_unlinked\":",f); js_bool(f,s->file_unlinked);
    fputs(",\"owned_empty_directory_removed\":",f); js_bool(f,s->directory_removed);
    fputs(",\"failure_containment_is_not_acknowledged_cleanup\":true},\"binary_layout\":{",f);
    fprintf(f,"\"handoff_bytes\":%zu,\"child_report_bytes\":%zu,\"identity_bytes\":%zu,"
        "\"provider_report_bytes\":%zu,\"snapshot_bytes\":%zu,\"cleanup_bytes\":%zu,"
        "\"dev_bytes\":%zu,\"ino_bytes\":%zu,\"uid_bytes\":%zu,\"nlink_bytes\":%zu,"
        "\"mode_bytes\":%zu,\"off_bytes\":%zu,"
        "\"child_precleanup_offset\":%zu,\"child_cleanup_offset\":%zu,"
        "\"child_initial_offset\":%zu,\"child_first_offset\":%zu,"
        "\"child_second_offset\":%zu,\"child_after_cleanup_offset\":%zu}}\n",
        sizeof(handoff),sizeof(child_report),sizeof(of_identity),sizeof(of_report),sizeof(ac_snapshot),
        sizeof(of_cleanup_record),sizeof(dev_t),sizeof(ino_t),sizeof(uid_t),sizeof(nlink_t),
        sizeof(mode_t),sizeof(off_t),offsetof(child_report,precleanup),offsetof(child_report,external_cleanup),
        offsetof(child_report,initial),offsetof(child_report,first),offsetof(child_report,second),
        offsetof(child_report,after_cleanup));
}
int main(int argc,char **argv)
{
    if(argc>1 && strcmp(argv[1],"--child")==0) return child_main(argc,argv);
    if(argc!=3) return 120;
    enum selector_id selected=SELECTORS;
    for(unsigned i=0;i<SELECTORS;++i) if(strcmp(argv[1],names[i])==0) selected=(enum selector_id)i;
    if(selected==SELECTORS) return 120;
    static supervisor s;
    int output_fd=-1;
    bool completed=supervise(&s,selected,argv[2],&output_fd);
    if(s.report_valid) assess(&s,selected);
    else check(false,"supervisor-owned-isolated-custody");
    if(output_fd<0) return 2;
    FILE *output=fdopen(output_fd,"w");
    if(!output) return 2;
    evidence(output,&s,selected);
    bool output_error=ferror(output)!=0;
    if(fclose(output)!=0) output_error=true;
    fputc('{',stdout); common(stdout,selected); fputs("}\n",stdout);
    if(fflush(stdout)!=0) output_error=true;
    return !completed||output_error?2:failures?1:0;
}
