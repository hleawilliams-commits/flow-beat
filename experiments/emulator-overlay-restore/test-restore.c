/* SPDX-License-Identifier: GPL-2.0-only */
/* Focused RESTORE successor fixture, adapted from the frozen handback shims.
 * Exact candidate functions come from a private source extraction. No device,
 * transport, application-stop, producer admission or recovery permit exists. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "subucom_ownership.h"
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define loff_t fixture_loff_t
typedef long long fixture_loff_t;
#define __user
#define N_SPI 4
#define MISO_SIZE 152
#define INTERVAL_US 1176
#define U64_MAX UINT64_MAX
#define FMODE_WRITE 2
#define CAP_SYS_ADMIN 21
#define GFP_KERNEL 0
#define PAGE_SIZE 4096
#define ERESTARTSYS 512
#define MINOR(x) (x)
#define min(a,b) ((a)<(b)?(a):(b))
#define module_param(...)
#define MODULE_PARM_DESC(...)
#undef static_assert
#define static_assert(x) _Static_assert((x), #x)
struct file { unsigned f_mode; void *private_data; };
struct inode { unsigned i_rdev; };
struct kernel_param { int unused; };
static _Thread_local unsigned from_calls,to_calls,fail_from,fail_to;
static _Thread_local size_t partial_bytes=104;
static _Thread_local unsigned pause_from,pause_before_lock,pause_after_unlock;
static _Thread_local int admin=1,allocation_failure,interrupted;
static struct {pthread_mutex_t mu;pthread_cond_t cv;int waiting,resume;} gate={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,0};
static void hold(void){pthread_mutex_lock(&gate.mu);gate.waiting=1;pthread_cond_broadcast(&gate.cv);while(!gate.resume)pthread_cond_wait(&gate.cv,&gate.mu);pthread_mutex_unlock(&gate.mu);}
static void wait_held(void){pthread_mutex_lock(&gate.mu);while(!gate.waiting)pthread_cond_wait(&gate.cv,&gate.mu);pthread_mutex_unlock(&gate.mu);}
static void let_go(void){pthread_mutex_lock(&gate.mu);gate.resume=1;pthread_cond_broadcast(&gate.cv);pthread_mutex_unlock(&gate.mu);}
static void fixture_lock(pthread_mutex_t *mu){if(pause_before_lock && !--pause_before_lock)hold();assert(!pthread_mutex_lock(mu));}
static void fixture_unlock(pthread_mutex_t *mu){assert(!pthread_mutex_unlock(mu));if(pause_after_unlock && !--pause_after_unlock)hold();}
#define DEFINE_SPINLOCK(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
#define spin_lock_irqsave(l,f) do{(f)=0;fixture_lock(l);}while(0)
#define spin_unlock_irqrestore(l,f) do{(void)(f);fixture_unlock(l);}while(0)
/* Failed usercopy copies a controlled prefix and zero-fills the tail. This
 * owned-memory shim makes ignored-fault controls deterministic; it does not
 * execute Linux fault handling or establish actual VFS/user memory lifetime. */
static size_t copy_from_user(void *dst,const void *src,size_t n){from_calls++;if(fail_from==from_calls){size_t got=partial_bytes<n?partial_bytes:n-1;memcpy(dst,src,got);memset((u8 *)dst+got,0,n-got);return n-got;}memcpy(dst,src,n);if(pause_from && !--pause_from)hold();return 0;}
static size_t copy_to_user(void *dst,const void *src,size_t n){to_calls++;if(fail_to==to_calls){memcpy(dst,src,n/2);return n-n/2;}memcpy(dst,src,n);return 0;}
static int capable(int cap){assert(cap==CAP_SYS_ADMIN);return admin;}
static char *kstrdup(const char *s,int flags){(void)flags;return allocation_failure?NULL:strdup(s);}
#define kfree free
/* Host integer conversion shim, not proof of Linux kstrtoint parsing semantics. */
static int kstrtoint(const char *s,unsigned base,int *out){char *end;long n;errno=0;n=strtol(s,&end,base);if(end==s || (*end && strcmp(end,"\n")))return -EINVAL;if(errno==ERANGE || n<INT_MIN || n>INT_MAX)return -ERANGE;*out=(int)n;return 0;}
static int scnprintf(char *b,size_t n,const char *fmt,...){va_list a;va_start(a,fmt);int r=vsnprintf(b,n,fmt,a);va_end(a);return r<0?0:(r>=(int)n?(int)n-1:r);}
static unsigned usecs_to_jiffies(unsigned n){return n;}
static int schedule_timeout_interruptible(unsigned n){(void)n;return interrupted;}
#include "subucom_frames.h"
static unsigned random_wait_calls,random_fill_calls;static int random_wait_result;static u64 random_values[2]={0x1122334455667788ULL,0xfedcba9876543210ULL};
static int wait_for_random_bytes(void){random_wait_calls++;return random_wait_result;}
static void get_random_bytes(void *p,size_t n){assert(n==sizeof random_values);random_fill_calls++;memcpy(p,random_values,n);}
#include "candidate.inc"

static unsigned checks,failures;
static void ck(bool ok,const char *label){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",label);}}
struct snapshot {u8 bytes[N_SPI][MISO_SIZE];int present[N_SPI],crc[N_SPI],count;struct file *owner;u64 instance[2],run,epoch,sequence;bool fenced;};
static struct snapshot snap(void){struct snapshot s={0};pthread_mutex_lock(&miso_lock);memcpy(s.bytes,inject_pending,sizeof s.bytes);memcpy(s.present,has_inject,sizeof s.present);memcpy(s.crc,spi_crc_len,sizeof s.crc);memcpy(s.instance,mixer_instance,sizeof s.instance);s.count=n_crc;s.owner=mixer_owner;s.run=mixer_run;s.epoch=mixer_epoch;s.sequence=mixer_sequence;s.fenced=mixer_fenced;pthread_mutex_unlock(&miso_lock);return s;}
static bool same(struct snapshot a,struct snapshot b){return !memcmp(a.bytes,b.bytes,sizeof a.bytes)&&!memcmp(a.present,b.present,sizeof a.present)&&!memcmp(a.crc,b.crc,sizeof a.crc)&&!memcmp(a.instance,b.instance,sizeof a.instance)&&a.count==b.count&&a.owner==b.owner&&a.run==b.run&&a.epoch==b.epoch&&a.sequence==b.sequence&&a.fenced==b.fenced;}
static struct file owner={FMODE_WRITE,0},other={FMODE_WRITE,0},spi={0,(void *)2};
static void tls_reset(void){from_calls=to_calls=fail_from=fail_to=pause_from=pause_before_lock=pause_after_unlock=0;admin=1;allocation_failure=interrupted=0;partial_bytes=104;}
static void reset(void){tls_reset();memset(inject_pending,0,sizeof inject_pending);memset(has_inject,0,sizeof has_inject);int c[4]={28,38,96,38};memcpy(spi_crc_len,c,sizeof c);n_crc=4;mixer_fenced=false;mixer_owner=NULL;mixer_run=mixer_epoch=mixer_sequence=0;allow_overlay_ownership=true;memset(mixer_instance,0,sizeof mixer_instance);random_wait_calls=random_fill_calls=0;random_wait_result=0;random_values[0]=0x1122334455667788ULL;random_values[1]=0xfedcba9876543210ULL;assert(mixer_instance_init()==0);testmode_held=0;owner.f_mode=other.f_mode=FMODE_WRITE;gate.waiting=gate.resume=0;}
static void pattern(u8 *b,unsigned seed){for(unsigned i=0;i<MISO_SIZE;i++)b[i]=(u8)(seed+i*23);}
static void stamp(u8 *b,int len){if(len>=0 && len<MISO_SIZE-1){u16 c=crc16_x25(b,len);b[len]=c&255;b[len+1]=c>>8;}}
static struct az_overlay_command command(u64 run,u64 epoch,u64 seq){return (struct az_overlay_command){.version=AZ_OVERLAY_VERSION,.instance={mixer_instance[0],mixer_instance[1]},.run_id=run,.epoch=epoch,.sequence=seq};}
static long io(struct file *f,unsigned cmd,void *p){return ctrl_ioctl(f,cmd,(unsigned long)p);}
static long claim(struct file *f,u64 run,u64 epoch){struct az_overlay_command h=command(run,epoch,0);return io(f,AZ_OVERLAY_CLAIM,&h);}
static struct az_overlay_frame frame(u64 run,u64 epoch,u64 seq,unsigned seed){struct az_overlay_frame r={.header=command(run,epoch,seq)};pattern(r.frame,seed);return r;}
static ssize_t legacy(unsigned bus,unsigned seed,size_t n){u8 b[1+MISO_SIZE+8];b[0]=(u8)bus;pattern(b+1,seed);return ctrl_write(&other,(char *)b,n,NULL);}
static void unchanged(long got,long expected,struct snapshot before,const char *label){ck(got==expected && same(before,snap()),label);}

static struct az_overlay_restore restore(unsigned injected,u64 epoch,u64 seq,unsigned seed){
 struct az_overlay_restore r={.header=command(7,epoch,seq),.injected=injected};
 if(injected)pattern(r.frame,seed);else memcpy(r.frame,az_bus_frame[2],152);return r;
}
static bool committed(struct snapshot before,struct az_overlay_restore r){
 struct snapshot expected=before;memcpy(expected.bytes[2],r.frame,152);expected.present[2]=(int)r.injected;
 expected.epoch++;expected.sequence=0;return same(expected,snap());
}
static bool state_matches(struct az_overlay_state s,struct az_overlay_restore r,u64 epoch){
 return s.version==2&&s.reserved==0&&s.flags==(23u|(r.injected?8u:0u))&&
 !memcmp(s.instance,r.header.instance,16)&&s.run_id==r.header.run_id&&s.epoch==epoch&&s.sequence==0&&s.crc_len==96&&!memcmp(s.frame,r.frame,152);
}
static void setup_injected(void){reset();for(unsigned b=0;b<4;b++)assert(legacy(b,31+b,153)==153);assert(claim(&owner,7,0)==0);}

/* Each single oracle is independently selectable for a strict compiling mutant.
 * Setup uses assert; only the named ck may fail in an expected-failure run. */
static void mode_oracle(void){setup_injected();struct az_overlay_restore r=restore(0,1,1,0);long rc=io(&owner,AZ_OVERLAY_RESTORE,&r);struct az_overlay_state s;long sr=io(&owner,AZ_OVERLAY_STATE,&s);ck(rc==0&&sr==0&&!has_inject[2]&&s.flags==23&&!memcmp(s.frame,az_bus_frame[2],152),"restore_idle_mode_selected");}
static void frame_oracle(void){setup_injected();struct az_overlay_restore r=restore(1,1,1,97);long rc=io(&owner,AZ_OVERLAY_RESTORE,&r);ck(rc==0&&has_inject[2]&&!memcmp(inject_pending[2],r.frame,152),"restore_injected_full_payload_committed");}
static void epoch_oracle(void){setup_injected();struct az_overlay_restore r=restore(1,1,1,97);long rc=io(&owner,AZ_OVERLAY_RESTORE,&r);ck(rc==0&&mixer_epoch==2,"restore_advances_generation");}
static void sequence_oracle(void){setup_injected();struct az_overlay_frame f=frame(7,1,1,44);assert(io(&owner,AZ_OVERLAY_FRAME,&f)==0);struct az_overlay_restore r=restore(1,1,2,97);long rc=io(&owner,AZ_OVERLAY_RESTORE,&r);ck(rc==0&&mixer_sequence==0,"restore_resets_sequence");}
static void idle_oracle(void){setup_injected();struct az_overlay_restore r=restore(0,1,1,0);r.frame[151]^=1;struct snapshot before=snap();unchanged(io(&owner,AZ_OVERLAY_RESTORE,&r),-EINVAL,before,"restore_idle_mismatch_refused");}
static void overflow_oracle(void){setup_injected();mixer_epoch=UINT64_MAX;struct az_overlay_restore r=restore(0,UINT64_MAX,1,0);struct snapshot before=snap();unchanged(io(&owner,AZ_OVERLAY_RESTORE,&r),-EOVERFLOW,before,"restore_epoch_exhaustion_unchanged");}
static void partial_oracle(void){setup_injected();struct az_overlay_restore r=restore(1,1,1,99);struct snapshot before=snap();partial_bytes=56;fail_from=from_calls+1;long rc=io(&owner,AZ_OVERLAY_RESTORE,&r);fail_from=0;unchanged(rc,-EFAULT,before,"restore_partial_input_never_commits");}
static void owner_oracle(void){setup_injected();struct az_overlay_restore r=restore(0,1,1,0);struct snapshot before=snap();unchanged(io(&other,AZ_OVERLAY_RESTORE,&r),-EACCES,before,"restore_requires_same_file");}
static void instance_oracle(void){setup_injected();struct az_overlay_restore r=restore(0,1,1,0);r.header.instance[1]^=1;struct snapshot before=snap();unchanged(io(&owner,AZ_OVERLAY_RESTORE,&r),-ESTALE,before,"restore_requires_full_instance");}

/* Larger union prevents the old 200-byte job storage from truncating RESTORE. */
union request {struct az_overlay_command command;struct az_overlay_frame frame;struct az_overlay_restore restore;};
struct job {unsigned op,pause_copy,pause_lock,pause_unlock,cmd;struct file *file;union request request;long result;struct az_overlay_state state;u8 bytes[152];};
static void *work(void *v){struct job *j=v;tls_reset();pause_from=j->pause_copy;pause_before_lock=j->pause_lock;pause_after_unlock=j->pause_unlock;
 if(j->op==0)j->result=io(j->file,j->cmd,&j->request);
 if(j->op==1)j->result=io(j->file,AZ_OVERLAY_STATE,&j->state);
 if(j->op==2)j->result=spi_read(&spi,(char *)j->bytes,152,NULL);
 return NULL;}
/* Every prior paused worker is joined before another launch. Reset the gate
 * for this actual pause, including consecutive STATE then SPI observations. */
static pthread_t launch(struct job *j){pthread_t t;pthread_mutex_lock(&gate.mu);gate.waiting=gate.resume=0;pthread_mutex_unlock(&gate.mu);int rc=pthread_create(&t,NULL,work,j);ck(!rc,"pthread_create");if(rc){fprintf(stderr,"fixture thread creation failed: %d\n",rc);exit(2);}wait_held();return t;}
static void finish(pthread_t t){let_go();ck(!pthread_join(t,NULL),"pthread_join");}
static struct job old_request(unsigned cmd){struct job j={.op=0,.cmd=cmd,.file=&owner,.pause_copy=1};if(cmd==AZ_OVERLAY_RESTORE)j.request.restore=restore(1,1,1,77);else j.request.frame=frame(7,1,1,77);return j;}
static void stale_oracle(unsigned cmd){setup_injected();struct job j=old_request(cmd);pthread_t t=launch(&j);struct az_overlay_restore r=restore(0,1,1,0);assert(io(&owner,AZ_OVERLAY_RESTORE,&r)==0);struct snapshot before=snap();finish(t);unchanged(j.result,-ESTALE,before,"copied_old_generation_refused_after_restore");}
static void atomic_oracle(void){setup_injected();struct job j={.op=0,.cmd=AZ_OVERLAY_RESTORE,.file=&owner,.pause_unlock=1,.request.restore=restore(0,1,1,0)};pthread_t t=launch(&j);struct az_overlay_state s;long rc=io(&owner,AZ_OVERLAY_STATE,&s);bool complete=rc==0&&state_matches(s,j.request.restore,2);finish(t);ck(complete,"restore_mode_frame_epoch_are_one_locked_transition");}

static void abi_and_authorization(void){
 reset();ck(sizeof(struct az_overlay_restore)==208&&offsetof(struct az_overlay_restore,injected)==48&&offsetof(struct az_overlay_restore,reserved)==52&&offsetof(struct az_overlay_restore,frame)==56,"restore_native64_layout");
 ck(AZ_OVERLAY_RESTORE==0x40d05a55u&&AZ_OVERLAY_FRAME==0x40c85a51u&&AZ_OVERLAY_ROTATE==0x40c85a54u&&AZ_OVERLAY_STATE==0x80d05a53u,"additive_exact_ioctl_encodings");
 setup_injected();struct az_overlay_restore r=restore(1,1,1,87);struct snapshot before=snap();
 const unsigned badcmd[]={0x40c85a55u,0x40305a55u,0x80d05a55u,0xc0d05a55u,0x00d05a55u,0x40205a50u};
 for(unsigned i=0;i<sizeof badcmd/sizeof *badcmd;i++){unsigned f=from_calls,t=to_calls;unchanged(io(&owner,badcmd[i],&r),-ENOTTY,before,"restore_wrong_encoding_unchanged");ck(from_calls==f&&to_calls==t,"restore_wrong_encoding_no_usercopy");}
 unsigned f=from_calls;owner.f_mode=0;unchanged(io(&owner,AZ_OVERLAY_RESTORE,&r),-EBADF,before,"restore_write_mode_required");owner.f_mode=FMODE_WRITE;ck(from_calls==f,"restore_write_reject_before_copy");
 admin=0;unchanged(io(&owner,AZ_OVERLAY_RESTORE,&r),-EPERM,before,"restore_current_capability_required");admin=1;ck(from_calls==f,"restore_capability_reject_before_copy");
 allow_overlay_ownership=false;unchanged(io(&owner,AZ_OVERLAY_RESTORE,&r),-EOPNOTSUPP,before,"restore_opt_in_required");allow_overlay_ownership=true;ck(from_calls==f,"restore_opt_in_reject_before_copy");
 for(unsigned which=0;which<12;which++){struct az_overlay_restore bad=r;long error=-EINVAL;
  if(which==0)bad.header.version=1;if(which==1)bad.header.reserved=1;if(which==2)bad.header.run_id=0;
  if(which==3)bad.injected=2;if(which==4)bad.injected=UINT32_MAX;if(which==5)bad.reserved=1;
  if(which==6){bad.header.run_id=8;error=-ESTALE;}if(which==7){bad.header.epoch=2;error=-ESTALE;}
  if(which==8){bad.header.sequence=0;error=-ESTALE;}if(which==9){bad.header.sequence=2;error=-ESTALE;}
  if(which==10){bad.header.instance[0]^=1;error=-ESTALE;}if(which==11){bad.header.instance[1]^=1;error=-ESTALE;}
  unchanged(io(&owner,AZ_OVERLAY_RESTORE,&bad),error,before,"restore_invalid_field_no_mutation");
 }
 const size_t cuts[]={0,1,47,48,51,52,55,56,96,207};
 for(unsigned i=0;i<sizeof cuts/sizeof *cuts;i++){partial_bytes=cuts[i];fail_from=from_calls+1;long rc=io(&owner,AZ_OVERLAY_RESTORE,&r);fail_from=0;unchanged(rc,-EFAULT,before,"restore_partial_prefix_unchanged");}
 bool allbytes=true;for(unsigned i=0;i<152;i++){struct az_overlay_restore bad=restore(0,1,1,0);bad.frame[i]^=0x80;allbytes=io(&owner,AZ_OVERLAY_RESTORE,&bad)==-EINVAL&&same(before,snap())&&allbytes;}
 ck(allbytes,"restore_idle_checks_every_raw_byte_including_crc_tail");
 owner_oracle();instance_oracle();partial_oracle();
}

static void successful_modes_and_fence(void){
 for(unsigned original=0;original<2;original++){
  reset();for(unsigned b=0;b<4;b++)if(b!=2||original)assert(legacy(b,31+b,153)==153);assert(claim(&owner,7,0)==0);
  struct az_overlay_state baseline;assert(io(&owner,AZ_OVERLAY_STATE,&baseline)==0);
  struct az_overlay_frame changed=frame(7,1,1,98);assert(io(&owner,AZ_OVERLAY_FRAME,&changed)==0);
  struct az_overlay_restore r=restore(original,1,2,0);memcpy(r.frame,baseline.frame,152);struct snapshot before=snap();unsigned writes=to_calls;
  long rc=io(&owner,AZ_OVERLAY_RESTORE,&r);ck(rc==0&&committed(before,r),"restore_original_selected_mode_and_raw_bytes");ck(to_calls==writes,"restore_has_no_post_commit_usercopy");
  struct az_overlay_state after;ck(io(&owner,AZ_OVERLAY_STATE,&after)==0&&state_matches(after,r,2),"restore_state_exact_target_and_new_epoch");
  before=snap();unchanged(io(&owner,AZ_OVERLAY_RESTORE,&r),-ESTALE,before,"restore_exact_duplicate_not_reexecuted");
  unchanged(legacy(2,88,153),-EBUSY,before,"restore_keeps_legacy_frame_fenced");unchanged(legacy(2,88,1),-EBUSY,before,"restore_keeps_legacy_single_clear_fenced");unchanged(ctrl_write(&other,NULL,0,NULL),-EBUSY,before,"restore_keeps_legacy_clear_all_fenced");
  unchanged(crc_lengths_set("28,38,38,38",NULL),-EBUSY,before,"restore_keeps_mixer_crc_fenced");
  ck(legacy(1,85,153)==153&&has_inject[1]&&inject_pending[1][0]==85,"restore_other_bus_writes_remain_available");
  char text[64];ck(crc_lengths_get(text,NULL)==11&&!strcmp(text,"28,38,96,38"),"restore_preserves_crc_snapshot");
  struct inode ino={2};struct file opened={0};ck(spi_open(&ino,&opened)==0&&opened.private_data==(void *)2,"restore_read_open_selects_mixer");
  u8 got[152],expected[152];memcpy(expected,r.frame,152);stamp(expected,96);ck(spi_read(&opened,(char *)got,152,NULL)==152&&!memcmp(got,expected,152),"restore_spi_read_selects_mode_then_restamps_crc");ck(spi_release(&ino,&opened)==0&&ctrl_open(NULL,&other)==0,"restore_open_release_do_not_change_ownership");
  before=snap();ctrl_release(NULL,&owner);struct snapshot orphan=snap();before.owner=NULL;ck(same(before,orphan)&&mixer_fenced,"restore_owner_close_keeps_mode_frame_generation_fence");
 }
 /* Equal bytes must not conflate injected versus idle or skip revocation. */
 setup_injected();struct az_overlay_restore r=restore(0,1,1,0);assert(io(&owner,AZ_OVERLAY_RESTORE,&r)==0);r.header=command(7,2,1);struct snapshot before=snap();ck(io(&owner,AZ_OVERLAY_RESTORE,&r)==0&&committed(before,r),"restore_same_idle_target_still_revokes_generation");
 r.header=command(7,3,1);r.injected=1;before=snap();ck(io(&owner,AZ_OVERLAY_RESTORE,&r)==0&&committed(before,r)&&has_inject[2],"restore_injected_idle_bytes_keep_explicit_mode");
 r.header=command(7,4,1);before=snap();ck(io(&owner,AZ_OVERLAY_RESTORE,&r)==0&&committed(before,r),"restore_same_injected_target_still_revokes_generation");
 r.header=command(7,5,1);r.injected=0;before=snap();ck(io(&owner,AZ_OVERLAY_RESTORE,&r)==0&&committed(before,r),"restore_idle_mode_not_inferred_from_equal_bytes");
 mode_oracle();frame_oracle();epoch_oracle();sequence_oracle();idle_oracle();overflow_oracle();
 setup_injected();mixer_sequence=UINT64_MAX; r=restore(0,1,0,0);before=snap();unchanged(io(&owner,AZ_OVERLAY_RESTORE,&r),-EOVERFLOW,before,"restore_sequence_exhaustion_unchanged");
 setup_injected();mixer_epoch=UINT64_MAX-1;mixer_sequence=UINT64_MAX-1;r=restore(0,UINT64_MAX-1,UINT64_MAX,0);before=snap();ck(io(&owner,AZ_OVERLAY_RESTORE,&r)==0&&committed(before,r),"restore_last_epoch_last_sequence_no_wrap");r.header=command(7,UINT64_MAX,1);before=snap();unchanged(io(&owner,AZ_OVERLAY_RESTORE,&r),-EOVERFLOW,before,"restore_cannot_rotate_exhausted_epoch");
 struct az_overlay_frame f=frame(7,UINT64_MAX,1,21);ck(io(&owner,AZ_OVERLAY_FRAME,&f)==0&&mixer_sequence==1,"restore_preserves_existing_last_epoch_frame_semantics");
}

static pthread_mutex_t start_mu=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t start_cv=PTHREAD_COND_INITIALIZER;static unsigned starters;static bool start_now;
static void *together(void *v){pthread_mutex_lock(&start_mu);starters++;pthread_cond_broadcast(&start_cv);while(!start_now)pthread_cond_wait(&start_cv,&start_mu);pthread_mutex_unlock(&start_mu);return work(v);}
static void pair(struct job *a,struct job *b){pthread_t x,y;starters=0;start_now=false;ck(!pthread_create(&x,NULL,together,a)&&!pthread_create(&y,NULL,together,b),"pair_create");pthread_mutex_lock(&start_mu);while(starters!=2)pthread_cond_wait(&start_cv,&start_mu);start_now=true;pthread_cond_broadcast(&start_cv);pthread_mutex_unlock(&start_mu);ck(!pthread_join(x,NULL)&&!pthread_join(y,NULL),"pair_join");}
static void restore_schedules(void){
 const unsigned ops[]={AZ_OVERLAY_FRAME,AZ_OVERLAY_ROTATE,AZ_OVERLAY_RELEASE,AZ_OVERLAY_RESTORE};
 for(unsigned i=0;i<4;i++)stale_oracle(ops[i]);atomic_oracle();
 /* A copied RESTORE loses to each same-sequence old-generation mutation. */
 for(unsigned i=0;i<4;i++){
  setup_injected();struct job pending={.op=0,.cmd=AZ_OVERLAY_RESTORE,.file=&owner,.pause_copy=1,.request.restore=restore(0,1,1,0)};pthread_t t=launch(&pending);
  struct job winner=old_request(ops[i]);assert(io(&owner,winner.cmd,&winner.request)==0);struct snapshot before=snap();finish(t);
  unchanged(pending.result,i==2?-EACCES:-ESTALE,before,"copied_restore_loses_to_committed_request");
 }
 setup_injected();struct job copied={.op=0,.cmd=AZ_OVERLAY_RESTORE,.file=&owner,.pause_copy=1,.request.restore=restore(1,1,1,55)};struct az_overlay_restore original=copied.request.restore;pthread_t t=launch(&copied);copied.request.restore.injected=0;memset(copied.request.restore.frame,0,152);finish(t);ck(copied.result==0&&mixer_epoch==2&&has_inject[2]&&!memcmp(inject_pending[2],original.frame,152),"restore_uses_completed_private_request_copy");
 /* Low historical STATE does not classify a not-yet-committed RESTORE. */
 setup_injected();struct job pending={.op=0,.cmd=AZ_OVERLAY_RESTORE,.file=&owner,.pause_copy=1,.request.restore=restore(0,1,1,0)};t=launch(&pending);struct az_overlay_state state;long rc=io(&owner,AZ_OVERLAY_STATE,&state);finish(t);ck(rc==0&&state.epoch==1&&state.sequence==0&&pending.result==0&&mixer_epoch==2&&!has_inject[2],"old_state_does_not_cancel_pending_restore");
 for(unsigned original_mode=0;original_mode<2;original_mode++){
  setup_injected();if(!original_mode){struct az_overlay_restore idle=restore(0,1,1,0);assert(io(&owner,AZ_OVERLAY_RESTORE,&idle)==0);}
  struct az_overlay_state original_state;assert(io(&owner,AZ_OVERLAY_STATE,&original_state)==0);u8 expected[152];memcpy(expected,original_state.frame,152);stamp(expected,96);
  struct job history={.op=1,.file=&owner,.pause_unlock=1};t=launch(&history);struct az_overlay_restore changed=restore(!original_mode,mixer_epoch,1,91);assert(io(&owner,AZ_OVERLAY_RESTORE,&changed)==0);finish(t);ck(history.result==0&&!memcmp(&history.state,&original_state,sizeof original_state),"restore_delayed_state_is_wholly_historical");
  struct az_overlay_state latest;ck(io(&owner,AZ_OVERLAY_STATE,&latest)==0&&state_matches(latest,changed,changed.header.epoch+1),"restore_later_state_observes_new_mode");
  struct job read={.op=2,.pause_unlock=1};memcpy(expected,latest.frame,152);stamp(expected,96);t=launch(&read);struct az_overlay_restore reverse=restore(original_mode,mixer_epoch,1,63);assert(io(&owner,AZ_OVERLAY_RESTORE,&reverse)==0);finish(t);ck(read.result==152&&!memcmp(read.bytes,expected,152),"restore_cannot_retract_already_sampled_spi_frame");
 }
 for(unsigned i=0;i<4;i++){
  setup_injected();struct job a={.op=0,.cmd=AZ_OVERLAY_RESTORE,.file=&owner,.request.restore=restore(0,1,1,0)};
  struct job b=old_request(ops[i]);b.pause_copy=0;pair(&a,&b);bool aw=a.result==0;
  bool rc=aw?b.result==-ESTALE:(b.result==0&&a.result==(i==2?-EACCES:-ESTALE));
  bool state=mixer_owner==(aw||i!=2?&owner:NULL)&&mixer_fenced&&mixer_run==7;
  if(aw)state=state&&mixer_epoch==2&&mixer_sequence==0&&!has_inject[2]&&!memcmp(inject_pending[2],a.request.restore.frame,152);
  else if(i==2)state=state&&mixer_epoch==1&&mixer_sequence==1&&has_inject[2]&&inject_pending[2][0]==33;
  else {const u8 *bytes=i==3?b.request.restore.frame:b.request.frame.frame;state=state&&mixer_epoch==(i?2u:1u)&&mixer_sequence==(i?0u:1u)&&has_inject[2]&&!memcmp(inject_pending[2],bytes,152);}
  ck(rc&&state,"restore_racing_mutation_exact_single_whole_winner");
 }
 /* A forged fresh tag remains outside kernel provenance: no stopped-process
  * or producer gate is inferred from mode restoration or an exact state. */
 setup_injected();struct az_overlay_frame old=frame(7,1,1,51);struct az_overlay_restore idle=restore(0,1,1,0);assert(io(&owner,AZ_OVERLAY_RESTORE,&idle)==0);old.header=command(7,2,1);ck(io(&owner,AZ_OVERLAY_FRAME,&old)==0&&has_inject[2]&&!memcmp(inject_pending[2],old.frame,152),"freshly_retagged_old_bytes_remain_accepted_limitation");
}

int main(int argc,char **argv){alarm(30);const char *s=argc>1?argv[1]:"all";
 if(!strcmp(s,"all")){abi_and_authorization();successful_modes_and_fence();restore_schedules();}
 else if(!strcmp(s,"mode"))mode_oracle();else if(!strcmp(s,"frame"))frame_oracle();else if(!strcmp(s,"epoch"))epoch_oracle();else if(!strcmp(s,"sequence"))sequence_oracle();else if(!strcmp(s,"idle"))idle_oracle();else if(!strcmp(s,"overflow"))overflow_oracle();else if(!strcmp(s,"partial"))partial_oracle();else if(!strcmp(s,"owner"))owner_oracle();else if(!strcmp(s,"instance"))instance_oracle();else if(!strcmp(s,"stale"))stale_oracle(AZ_OVERLAY_FRAME);else if(!strcmp(s,"atomic"))atomic_oracle();else return 2;
 printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;}
