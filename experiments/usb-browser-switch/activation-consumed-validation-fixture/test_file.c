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
#error "the consumed-validation witness requires only the five authored setup APIs"
#endif
/* No control macro occurs in this translation unit. A single harness object per
 * mode is linked unchanged to candidate and all three provider controls. */
#define REPORT_MAGIC UINT64_C(0x435646494c453031)
#define HANDOFF_MAGIC UINT64_C(0x435648414e443031)
#define ENVELOPE_VERSION 2u
#define JOURNAL_CAP 48u
#define CHECK_CAP 8u
#define OWNER_PROCESS UINT64_C(0x1234567887654321)
#define SOURCE_SCOPE "isolated-Linux-owned-temporary-file; authored-direct-owner; consumed-validation-veto"
#define OBSERVATION_BASIS "RAW_IDENTITY_AFTER_CONSUMED_VETO"
_Static_assert(sizeof(dev_t)<=sizeof(uintmax_t) && sizeof(ino_t)<=sizeof(uintmax_t) &&
               sizeof(uid_t)<=sizeof(uintmax_t) && sizeof(nlink_t)<=sizeof(uintmax_t) &&
               sizeof(mode_t)<=sizeof(uintmax_t) && sizeof(off_t)<=sizeof(intmax_t),
               "native identity fields must fit checked reporting types");
#if !defined(__linux__) || !defined(__aarch64__)
#error "the owned-file witness is scoped to the isolated Linux AArch64 target"
#endif
enum selector_id { VETO, QUERY_BASELINE, DOMAIN_BASELINE, CLEANUP_BASELINE, SELECTORS };
static const char *const names[SELECTORS]={
    "consumed-validation-veto","control-query-evidence",
    "control-refusal-domain","control-held-cleanup"
};
typedef struct {
    uint64_t magic;
    size_t bytes;
    unsigned version,selector;
    int directory_fd,report_fd;
    of_identity directory;
    uid_t uid;
} handoff;
typedef struct { uintptr_t pc,lr,fp; } request_value;
typedef struct {
    uint64_t magic;
    size_t bytes;
    unsigned version,selector,infrastructure;
    bool inherited_fd_list_valid,complete_table,prepared,model_stage,model_commit;
    bool freeze_ok,cleanup_ok;
    int directory_fd,report_fd;
    char owner[AC_ID_BYTES];
    request_value request_before,request_after;
    ac_snapshot initial,first,after_cleanup;
    unsigned calls;
    ac_request_result first_return;
    of_report precleanup;
    of_native_identity_receipt raw_identity;
    of_consumed_validation_record consumed_validation;
    of_cleanup_record external_cleanup;
} child_report;
typedef struct { const char *name; of_native_result result; } supervisor_event;
typedef struct {
    supervisor_event events[JOURNAL_CAP];
    unsigned count;
    bool infrastructure,child_confirmed_exit,report_valid,conversions_valid;
    bool directory_valid,identity_checked,observer_identity_checked;
    bool raw_identity_checked,file_present,file_unlinked,directory_removed;
    bool report_identity_checked;
    int child_status,child_exit,dirfd,rawfd;
    pid_t child_pid;
    of_identity directory,raw_report_identity,file_identity,observer_identity;
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

static bool zero_bytes(const void *bytes,size_t count)
{
    const unsigned char *p=bytes;
    for(size_t i=0;i<count;++i) if(p[i]) return false;
    return true;
}
static bool identity_equal(const of_identity *a,const of_identity *b)
{
    return a->device==b->device && a->inode==b->inode && a->uid==b->uid &&
        a->links==b->links && a->mode==b->mode && a->size==b->size;
}
static bool identity_zero(const of_identity *a)
{
    const of_identity zero={0};
    return identity_equal(a,&zero);
}
static bool pair_equal(const of_native_result *a,const of_native_result *b)
{
    return a->entered==b->entered && a->returned==b->returned && a->raw==b->raw &&
        a->error_valid==b->error_valid && a->error_number==b->error_number;
}
static bool query_equal(const of_query_record *a,const of_query_record *b)
{
    return a->kind==b->kind && a->descriptor==b->descriptor && a->flags==b->flags &&
        pair_equal(&a->result,&b->result) && a->identity_available==b->identity_available &&
        identity_equal(&a->identity,&b->identity);
}
static bool native_equal(const of_native_record *a,const of_native_record *b)
{
    return a->operation==b->operation && a->descriptor==b->descriptor &&
        a->flags==b->flags && a->creation_mode==b->creation_mode &&
        a->requested_length==b->requested_length &&
        memcmp(a->payload,b->payload,AC_ID_BYTES)==0 && pair_equal(&a->result,&b->result);
}
static bool decision_veto(const of_consumed_validation_record *d)
{
    return d->evaluated && d->raw_valid && d->authored_veto && !d->consumed_valid;
}
/* This predicate never consumes the corruptible post-open query projection.
 * It is independently recomputed by the supervisor before path observation. */
static bool raw_identity_valid(const child_report *r,const of_identity *directory,uid_t uid)
{
    const of_native_identity_receipt *v=&r->raw_identity;
    const of_report *p=&r->precleanup;
    const of_native_record *n=&v->open_record;
    const of_query_record *a=&v->by_fd,*b=&v->by_name;
    int flags=O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC;
    return v->complete && v->directory_fd==r->directory_fd &&
        v->expected_uid==uid && identity_equal(&v->expected_directory,directory) &&
        memcmp(v->fixed_name,OF_FILE_NAME,sizeof OF_FILE_NAME)==0 &&
        v->opened_fd>=0 && p->has_saved_descriptor && p->saved_descriptor==v->opened_fd &&
        n->operation==OF_OPEN && n->descriptor==r->directory_fd &&
        n->flags==flags && n->creation_mode==(mode_t)0600 &&
        n->requested_length==0 && zero_bytes(n->payload,AC_ID_BYTES) &&
        pair_success(&n->result,(int64_t)v->opened_fd) &&
        a->kind==OF_QUERY_FILE_FD && a->descriptor==v->opened_fd && a->flags==0 &&
        b->kind==OF_QUERY_FILE_NAME && b->descriptor==r->directory_fd &&
        b->flags==AT_SYMLINK_NOFOLLOW &&
        pair_success(&a->result,0) && pair_success(&b->result,0) &&
        a->identity_available && b->identity_available &&
        regular_owned(&a->identity,uid) && regular_owned(&b->identity,uid) &&
        a->identity.size==0 && b->identity.size==0 &&
        (a->identity.mode&0777)==0600 && (b->identity.mode&0777)==0600 &&
        same_object(&a->identity,&b->identity) &&
        identity_convertible(&v->expected_directory);
}
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
    static of_context context;
    int directory_fd=-1,report_fd=-1;
    if(argc!=4 || !parse_fd(argv[2],&directory_fd) || !parse_fd(argv[3],&report_fd) ||
       directory_fd==report_fd) return 121;
    ssize_t got=pread(report_fd,&h,sizeof h,0);
    if(got!=(ssize_t)sizeof h || h.magic!=HANDOFF_MAGIC || h.bytes!=sizeof h ||
       h.version!=ENVELOPE_VERSION || h.selector>=SELECTORS ||
       h.directory_fd!=directory_fd || h.report_fd!=report_fd) return 122;
    out.magic=REPORT_MAGIC; out.bytes=sizeof out; out.version=ENVELOPE_VERSION;
    out.selector=h.selector; out.directory_fd=directory_fd; out.report_fd=report_fd;
    out.inherited_fd_list_valid=fd_inventory(directory_fd,report_fd,false);
    out.complete_table=of_provider_table.open && of_provider_table.write &&
        of_provider_table.sync && of_provider_table.close && of_provider_table.unblamed;
    owner_id(out.owner,'a');
    out.prepared=of_prepare(&context,directory_fd,&h.directory,h.uid,OF_NORMAL);
    out.model_stage=ac_model_stage_module(0,out.owner,OWNER_PROCESS,0x1000,0x2000);
    out.model_commit=ac_model_commit_module(0);
    const ac_request first={0x1010,0,0,&of_provider_table,&context};
    out.request_before=(request_value){first.pc,first.lr,first.fp};
    ac_observe(&out.initial);
    if(!out.inherited_fd_list_valid || !out.complete_table || !out.prepared ||
       !out.model_stage || !out.model_commit) {
        out.infrastructure=1;
    } else {
        out.first_return=ac_run(&first); out.calls=1;
        ac_observe(&out.first);
    }
    out.request_after=(request_value){first.pc,first.lr,first.fp};
    out.freeze_ok=of_freeze(&context,&out.precleanup,&out.raw_identity,&out.consumed_validation);
    out.cleanup_ok=of_cleanup_held(&context,&out.external_cleanup);
    ac_observe(&out.after_cleanup);
    /* The enclosing envelope precedes its own write. Parent size/pread and
     * ordinary child exit are separate evidence, never a self-write receipt. */
    ssize_t written=pwrite(report_fd,&out,sizeof out,0);
    int saved_error=errno;
    if(written!=(ssize_t)sizeof out) {
        fprintf(stderr,"child evidence write raw=%jd error=%d\n",(intmax_t)written,
                written<0?saved_error:0);
        return 123;
    }
    return out.infrastructure?124:0; /* main return permits sanitizer exit work */
}
static bool envelope_shape(const child_report *r,enum selector_id selected)
{
    const of_report *p=&r->precleanup;
    return r->magic==REPORT_MAGIC && r->bytes==sizeof *r && r->version==ENVELOPE_VERSION &&
        r->selector==(unsigned)selected && r->calls<=1 &&
        p->callback_count<=OF_CALLBACK_CAPACITY && p->native_count<=OF_NATIVE_CAPACITY &&
        p->query_count<=OF_QUERY_CAPACITY &&
        r->initial.result.event_count<=AC_EVENT_CAPACITY &&
        r->first.result.event_count<=AC_EVENT_CAPACITY &&
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
    if(s->file_present && (!identity_convertible(&s->file_identity) ||
       !identity_convertible(&s->observer_identity))) return false;
    const of_report *p=&s->child.precleanup;
    const of_native_identity_receipt *v=&s->child.raw_identity;
    if(!identity_convertible(&p->expected_directory) ||
       !identity_convertible(&p->creation_identity) ||
       (uid_t)(uintmax_t)p->expected_uid!=p->expected_uid ||
       (uid_t)(uintmax_t)v->expected_uid!=v->expected_uid ||
       !identity_convertible(&v->expected_directory) ||
       !identity_convertible(&v->by_fd.identity) ||
       !identity_convertible(&v->by_name.identity)) return false;
    for(unsigned i=0;i<p->query_count;++i)
        if(!identity_convertible(&p->queries[i].identity)) return false;
    return true;
}
static bool supervise(supervisor *s,enum selector_id selected,const char *output,int *jsonfd)
{
    char name[128],raw_name[160],directory_path[]="/tmp/cv-witness-XXXXXX",executable[4096];
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
    handoff h={HANDOFF_MAGIC,sizeof(handoff),ENVELOPE_VERSION,(unsigned)selected,
               directory,rawfd,s->directory,getuid()};
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
    if(got!=(ssize_t)sizeof s->child || !envelope_shape(&s->child,selected) ||
       s->child.directory_fd!=directory || s->child.report_fd!=rawfd) goto failed;
    s->report_valid=true;
    s->raw_identity_checked=raw_identity_valid(&s->child,&s->directory,getuid()) &&
        decision_veto(&s->child.consumed_validation) &&
        !s->child.precleanup.creation_identity_available &&
        identity_zero(&s->child.precleanup.creation_identity);
    if(!s->raw_identity_checked) goto failed;
    if(record_name(s,"post-exit-file-name",directory,OF_FILE_NAME,&s->file_identity)!=0)
        goto failed;
    s->file_present=true;
    const of_native_identity_receipt *trusted=&s->child.raw_identity;
    s->identity_checked=regular_owned(&s->file_identity,getuid()) &&
        s->file_identity.size==0 &&
        same_object(&s->file_identity,&trusted->by_fd.identity) &&
        same_object(&s->file_identity,&trusted->by_name.identity);
    if(!s->identity_checked) goto failed;
    int observe=openat(directory,OF_FILE_NAME,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    event(s,"post-exit-observer-open",observe,errno);
    if(observe<0) goto failed;
    int fr=record_fstat(s,"post-exit-observer-fstat",observe,&s->observer_identity);
    s->observer_identity_checked=fr==0 && regular_owned(&s->observer_identity,getuid()) &&
        same_object(&s->observer_identity,&s->file_identity) && s->observer_identity.size==0;
    if(!s->observer_identity_checked) {
        int cl=close(observe); int ce=errno; event(s,"invalid-observer-close",cl,ce);
        goto failed;
    }
    ssize_t bytes=pread(observe,s->content,sizeof s->content,0); int be=errno;
    event(s,"post-exit-file-pread",bytes,be);
    int cl=close(observe); int ce=errno; event(s,"post-exit-observer-close",cl,ce);
    if(bytes!=0 || cl!=0) goto failed;
    s->content_length=(unsigned)bytes;
    of_identity before_unlink={0};
    if(record_name(s,"owned-file-before-unlink",directory,OF_FILE_NAME,&before_unlink)!=0 ||
       !same_object(&before_unlink,&s->file_identity) ||
       !regular_owned(&before_unlink,getuid()) || before_unlink.size!=0) goto failed;
    int un=unlinkat(directory,OF_FILE_NAME,0); int ue=errno;
    event(s,"owned-file-unlink",un,ue);
    if(un!=0) goto failed;
    s->file_unlinked=true;
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

static bool setup_valid(const supervisor *s)
{
    const child_report *r=&s->child;
    const ac_snapshot virgin={0};
    char id[AC_ID_BYTES]; owner_id(id,'a');
    return !s->infrastructure && s->directory_valid && s->child_confirmed_exit &&
        s->child_exit==0 && s->report_valid && s->report_identity_checked &&
        s->conversions_valid && r->directory_fd==s->dirfd && r->report_fd==s->rawfd &&
        r->infrastructure==0 && r->inherited_fd_list_valid && r->complete_table &&
        r->prepared && r->model_stage && r->model_commit && r->calls==1 &&
        snapshot_equal(&r->initial,&virgin) && memcmp(r->owner,id,AC_ID_BYTES)==0 &&
        r->request_before.pc==0x1010 && r->request_before.lr==0 && r->request_before.fp==0 &&
        r->request_after.pc==r->request_before.pc &&
        r->request_after.lr==r->request_before.lr && r->request_after.fp==r->request_before.fp;
}
static bool raw_native_valid(const supervisor *s)
{
    const child_report *r=&s->child;
    const of_report *p=&r->precleanup;
    const of_query_record *q=&p->queries[0];
    return raw_identity_valid(r,&s->directory,getuid()) &&
        p->native_count==1 && native_equal(&p->native[0],&r->raw_identity.open_record) &&
        identity_equal(&p->expected_directory,&s->directory) && p->expected_uid==getuid() &&
        p->query_count==3 && q->kind==OF_QUERY_DIRECTORY && q->descriptor==r->directory_fd &&
        q->flags==0 && pair_success(&q->result,0) && q->identity_available &&
        S_ISDIR(q->identity.mode) && q->identity.uid==getuid() &&
        (q->identity.mode&0777)==0700 && same_object(&q->identity,&s->directory);
}
static bool held_veto_valid(const child_report *r)
{
    const of_report *p=&r->precleanup;
    return decision_veto(&r->consumed_validation) &&
        p->setup_complete && p->directory_validated && p->frozen && r->freeze_ok &&
        p->mode==OF_NORMAL && p->infrastructure==OF_INFRA_FILE_VALIDATION &&
        p->has_saved_descriptor && p->saved_descriptor==r->raw_identity.opened_fd &&
        p->custody==OF_OWNED_HELD_UNVALIDATED &&
        !p->creation_identity_available && identity_zero(&p->creation_identity);
}
static bool operation_bound(const child_report *r)
{
    const of_report *p=&r->precleanup;
    const of_callback_record *c=&p->callbacks[0];
    ac_owner owner={AC_COMMUNITY,0,OWNER_PROCESS,{0}};
    owner_id(owner.id,'a');
    return r->calls==1 && p->callback_count==1 && p->file_callback_count==1 &&
        p->unblamed_count==0 && p->native_count==1 && p->query_count==3 &&
        c->operation==OF_OPEN && c->entered && c->prepared &&
        c->native_first==0 && c->native_count==1 &&
        c->custody_before==OF_NO_DESCRIPTOR && c->custody_after==OF_OWNED_HELD_UNVALIDATED &&
        owner_equal(&c->owner_argument,&owner) && c->token_argument==0 &&
        c->requested_length==0 && zero_bytes(c->payload_argument,AC_ID_BYTES);
}
static bool cleanup_truthful(const child_report *r)
{
    const of_cleanup_record *c=&r->external_cleanup;
    if(!r->freeze_ok || !r->cleanup_ok || !c->requested || !c->eligible ||
       c->descriptor!=r->raw_identity.opened_fd || c->descriptor<0 ||
       c->before!=OF_OWNED_HELD_UNVALIDATED ||
       c->attempted!=c->result.entered || c->attempted!=c->result.returned) return false;
    if(c->attempted)
        return pair_success(&c->result,0) && c->after==OF_CLOSED_RETURNED;
    return c->result.raw==0 && !c->result.error_valid && c->result.error_number==0 &&
        c->after==OF_OWNED_HELD_UNVALIDATED;
}
static bool postexit_safe(const supervisor *s)
{
    return s->raw_identity_checked && s->file_present && s->identity_checked &&
        s->observer_identity_checked && s->content_length==0 && s->file_identity.size==0 &&
        s->file_unlinked && s->directory_removed && cleanup_truthful(&s->child) &&
        snapshot_equal(&s->child.first,&s->child.after_cleanup);
}
static bool projected_queries_faithful(const child_report *r)
{
    return query_equal(&r->precleanup.queries[1],&r->raw_identity.by_fd) &&
        query_equal(&r->precleanup.queries[2],&r->raw_identity.by_name);
}
static bool terminal_incomplete(const child_report *r)
{
    ac_snapshot expected={0};
    expected.state=AC_ACTIVE_INCOMPLETE;
    const of_callback_record *c=&r->precleanup.callbacks[0];
    return c->origin==OF_INFRASTRUCTURE_REFUSAL && c->supplied.raw==-2 &&
        c->supplied.failure_errno==OF_INFRA_REFUSAL && r->first_return==AC_INCOMPLETE &&
        snapshot_equal(&r->first,&expected);
}
static bool explicit_cleanup(const child_report *r)
{
    const of_cleanup_record *c=&r->external_cleanup;
    return r->cleanup_ok && c->requested && c->eligible && c->attempted &&
        c->descriptor==r->raw_identity.opened_fd && c->before==OF_OWNED_HELD_UNVALIDATED &&
        pair_success(&c->result,0) && c->after==OF_CLOSED_RETURNED;
}
static void assess(const supervisor *s)
{
    check(setup_valid(s),"setup-and-envelope-custody");
    check(raw_native_valid(s),"independent-native-identity");
    check(held_veto_valid(&s->child),"consumed-veto-retains-unaccepted-held");
    check(operation_bound(&s->child),"ordinary-scope-and-operation-bound");
    check(postexit_safe(s),"postexit-observation-and-error-refusal");
    check(projected_queries_faithful(&s->child),"validation-veto-preserves-native-query-evidence");
    check(terminal_incomplete(&s->child),"validation-veto-remains-terminal-incomplete");
    check(explicit_cleanup(&s->child),"held-validation-refusal-has-explicit-cleanup");
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

static void js_query(FILE *f,const of_query_record *q)
{
    fprintf(f,"{\"kind\":%u,\"descriptor\":%d,\"flags\":%d,\"result\":",
        (unsigned)q->kind,q->descriptor,q->flags); js_pair(f,&q->result);
    fputs(",\"identity_available\":",f); js_bool(f,q->identity_available);
    fputs(",\"identity\":",f); js_identity(f,&q->identity); fputc('}',f);
}
static void js_native(FILE *f,const of_native_record *n)
{
    fprintf(f,"{\"operation\":%u,\"descriptor\":%d,\"flags\":%d,\"creation_mode\":%ju,"
        "\"requested_length\":%zu,\"payload_hex\":",(unsigned)n->operation,n->descriptor,
        n->flags,(uintmax_t)n->creation_mode,n->requested_length);
    js_hex(f,n->payload,AC_ID_BYTES); fputs(",\"result\":",f); js_pair(f,&n->result); fputc('}',f);
}
static void js_raw_identity(FILE *f,const of_native_identity_receipt *r)
{
    fputs("{\"complete\":",f); js_bool(f,r->complete);
    fprintf(f,",\"directory_fd\":%d,\"opened_fd\":%d,\"expected_uid\":%ju,\"expected_directory\":",
        r->directory_fd,r->opened_fd,(uintmax_t)r->expected_uid);
    js_identity(f,&r->expected_directory);
    fputs(",\"fixed_name_hex\":",f); js_hex(f,r->fixed_name,sizeof r->fixed_name);
    fputs(",\"open_record\":",f); js_native(f,&r->open_record);
    fputs(",\"by_fd\":",f); js_query(f,&r->by_fd);
    fputs(",\"by_name\":",f); js_query(f,&r->by_name); fputc('}',f);
}
static void js_decision(FILE *f,const of_consumed_validation_record *r)
{
    fputs("{\"evaluated\":",f); js_bool(f,r->evaluated);
    fputs(",\"raw_valid\":",f); js_bool(f,r->raw_valid);
    fputs(",\"authored_veto\":",f); js_bool(f,r->authored_veto);
    fputs(",\"consumed_valid\":",f); js_bool(f,r->consumed_valid); fputc('}',f);
}
static void js_child(FILE *f,const child_report *r)
{
    fprintf(f,"{\"magic\":%" PRIu64 ",\"bytes\":%zu,\"version\":%u,\"selector_index\":%u,"
        "\"infrastructure\":%u,\"directory_fd\":%d,\"report_fd\":%d,\"inherited_fd_list_valid\":",
        r->magic,r->bytes,r->version,r->selector,r->infrastructure,r->directory_fd,r->report_fd);
    js_bool(f,r->inherited_fd_list_valid);
    fputs(",\"complete_table\":",f); js_bool(f,r->complete_table);
    fputs(",\"prepared\":",f); js_bool(f,r->prepared);
    fputs(",\"model_stage\":",f); js_bool(f,r->model_stage);
    fputs(",\"model_commit\":",f); js_bool(f,r->model_commit);
    fputs(",\"freeze_ok\":",f); js_bool(f,r->freeze_ok);
    fputs(",\"cleanup_ok\":",f); js_bool(f,r->cleanup_ok);
    fputs(",\"owner_hex\":",f); js_hex(f,r->owner,AC_ID_BYTES);
    fputs(",\"request_before\":",f); js_request(f,&r->request_before);
    fputs(",\"request_after\":",f); js_request(f,&r->request_after);
    fprintf(f,",\"calls\":%u,\"first_return\":%u,\"initial\":",r->calls,(unsigned)r->first_return);
    js_snapshot(f,&r->initial); fputs(",\"first\":",f); js_snapshot(f,&r->first);
    fputs(",\"after_cleanup\":",f); js_snapshot(f,&r->after_cleanup);
    fputs(",\"precleanup\":",f); js_provider(f,&r->precleanup);
    fputs(",\"raw_identity\":",f); js_raw_identity(f,&r->raw_identity);
    fputs(",\"consumed_validation\":",f); js_decision(f,&r->consumed_validation);
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
    fprintf(f,",\"owner_process_id\":%" PRIu64 ",\"module_index\":0,"
        "\"range_begin\":4096,\"range_end\":8192,\"request_pc\":4112,\"request_lr\":0,"
        "\"request_fp\":0,\"generated_records\":0,\"frame_cells\":0,\"file_name\":\"%s\","
        "\"report_self_write_claim\":false,\"native_fault_context\":false},\"supervisor\":{",
        OWNER_PROCESS,OF_FILE_NAME);
    fputs("\"infrastructure\":",f); js_bool(f,s->infrastructure);
    fputs(",\"directory_valid\":",f); js_bool(f,s->directory_valid);
    fputs(",\"child_confirmed_exit\":",f); js_bool(f,s->child_confirmed_exit);
    fprintf(f,",\"child_pid\":%jd,\"child_status\":%d,\"child_exit\":%d,\"directory_fd\":%d,"
        "\"raw_report_fd\":%d,\"report_valid\":",
        (intmax_t)s->child_pid,s->child_status,s->child_exit,s->dirfd,s->rawfd);
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
    fprintf(f,",\"parent_file\":{\"observation_basis\":\"%s\",\"raw_identity_checked\":",OBSERVATION_BASIS);
    js_bool(f,s->raw_identity_checked);
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
        "\"raw_receipt_bytes\":%zu,\"decision_bytes\":%zu,"
        "\"dev_bytes\":%zu,\"ino_bytes\":%zu,\"uid_bytes\":%zu,\"nlink_bytes\":%zu,"
        "\"mode_bytes\":%zu,\"off_bytes\":%zu,"
        "\"child_version_offset\":%zu,\"child_precleanup_offset\":%zu,"
        "\"child_raw_identity_offset\":%zu,\"child_consumed_validation_offset\":%zu,"
        "\"child_cleanup_offset\":%zu,\"child_initial_offset\":%zu,"
        "\"child_first_offset\":%zu,\"child_after_cleanup_offset\":%zu,"
        "\"child_return_offset\":%zu,\"child_request_before_offset\":%zu,"
        "\"child_request_after_offset\":%zu,\"child_owner_offset\":%zu}}\n",
        sizeof(handoff),sizeof(child_report),sizeof(of_identity),sizeof(of_report),
        sizeof(ac_snapshot),sizeof(of_cleanup_record),sizeof(of_native_identity_receipt),
        sizeof(of_consumed_validation_record),sizeof(dev_t),sizeof(ino_t),sizeof(uid_t),
        sizeof(nlink_t),sizeof(mode_t),sizeof(off_t),offsetof(child_report,version),
        offsetof(child_report,precleanup),offsetof(child_report,raw_identity),
        offsetof(child_report,consumed_validation),offsetof(child_report,external_cleanup),
        offsetof(child_report,initial),offsetof(child_report,first),offsetof(child_report,after_cleanup),
        offsetof(child_report,first_return),offsetof(child_report,request_before),
        offsetof(child_report,request_after),offsetof(child_report,owner));
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
    if(s.report_valid) assess(&s);
    else check(false,"setup-and-envelope-custody");
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
