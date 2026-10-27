/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual candidate functions are included from a private source extraction.
 * Kernel service shims use owned memory and pthread locks, never devices. */
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
static size_t copy_from_user(void *dst,const void *src,size_t n){from_calls++;if(fail_from==from_calls){memcpy(dst,src,n/2);return n-n/2;}memcpy(dst,src,n);if(pause_from && !--pause_from)hold();return 0;}
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
static struct file owner={FMODE_WRITE,0},other={FMODE_WRITE,0},readonly={0,0},spi={0,(void *)2};
static void tls_reset(void){from_calls=to_calls=fail_from=fail_to=pause_from=pause_before_lock=pause_after_unlock=0;admin=1;allocation_failure=interrupted=0;}
static void reset(void){tls_reset();memset(inject_pending,0,sizeof inject_pending);memset(has_inject,0,sizeof has_inject);int c[4]={28,38,96,38};memcpy(spi_crc_len,c,sizeof c);n_crc=4;mixer_fenced=false;mixer_owner=NULL;mixer_run=mixer_epoch=mixer_sequence=0;allow_overlay_ownership=true;memset(mixer_instance,0,sizeof mixer_instance);random_wait_calls=random_fill_calls=0;random_wait_result=0;random_values[0]=0x1122334455667788ULL;random_values[1]=0xfedcba9876543210ULL;assert(mixer_instance_init()==0);testmode_held=0;owner.f_mode=other.f_mode=FMODE_WRITE;gate.waiting=gate.resume=0;}
static void pattern(u8 *b,unsigned seed){for(unsigned i=0;i<MISO_SIZE;i++)b[i]=(u8)(seed+i*23);}
static void stamp(u8 *b,int len){if(len>=0 && len<MISO_SIZE-1){u16 c=crc16_x25(b,len);b[len]=c&255;b[len+1]=c>>8;}}
static struct az_overlay_command command(u64 run,u64 epoch,u64 seq){return (struct az_overlay_command){.version=AZ_OVERLAY_VERSION,.instance={mixer_instance[0],mixer_instance[1]},.run_id=run,.epoch=epoch,.sequence=seq};}
static long io(struct file *f,unsigned cmd,void *p){return ctrl_ioctl(f,cmd,(unsigned long)p);}
static long claim(struct file *f,u64 run,u64 epoch){struct az_overlay_command h=command(run,epoch,0);return io(f,AZ_OVERLAY_CLAIM,&h);}
static struct az_overlay_frame frame(u64 run,u64 epoch,u64 seq,unsigned seed){struct az_overlay_frame r={.header=command(run,epoch,seq)};pattern(r.frame,seed);return r;}
static ssize_t legacy(unsigned bus,unsigned seed,size_t n){u8 b[1+MISO_SIZE+8];b[0]=(u8)bus;pattern(b+1,seed);return ctrl_write(&other,(char *)b,n,NULL);}
static void unchanged(long got,long expected,struct snapshot before,const char *label){ck(got==expected && same(before,snap()),label);}

static void basic(void){
 reset();ck(sizeof(struct az_overlay_command)==48 && sizeof(struct az_overlay_frame)==200 && sizeof(struct az_overlay_state)==208,"uapi_sizes");
 ck(AZ_OVERLAY_CLAIM==0x40305a50u && AZ_OVERLAY_FRAME==0x40c85a51u && AZ_OVERLAY_RELEASE==0x40305a52u && AZ_OVERLAY_STATE==0x80d05a53u && AZ_OVERLAY_ROTATE==0x40c85a54u,"uapi_exact_commands");
 struct inode inode={2};struct file opened={0};ck(spi_open(&inode,&opened)==0 && opened.private_data==(void *)2,"spi_open_bus");ck(spi_release(&inode,&opened)==0 && ctrl_open(NULL,&owner)==0,"open_close_legacy");
 for(unsigned b=0;b<4;b++){ck(legacy(b,11+b,153)==153,"legacy_full_write");ck(has_inject[b] && inject_pending[b][0]==11+b,"legacy_full_values");ck(legacy(b,5,1)==1 && !has_inject[b],"legacy_single_clear");}
 ck(legacy(2,41,161)==153,"legacy_oversize_preserved");struct snapshot before=snap();unchanged(legacy(2,7,152),-EINVAL,before,"legacy_short_unchanged");unchanged(legacy(4,7,153),-EINVAL,before,"legacy_bad_bus");
 fail_from=from_calls+1;unchanged(legacy(2,8,153),-EFAULT,before,"legacy_header_copy_fault");fail_from=from_calls+2;unchanged(legacy(2,9,153),-EFAULT,before,"legacy_payload_partial_fault");fail_from=0;
 ck(ctrl_write(&owner,NULL,0,NULL)==0 && !has_inject[2],"legacy_clear_all");
 struct az_overlay_command h=command(7,0,0);before=snap();allow_overlay_ownership=false;unchanged(io(&owner,AZ_OVERLAY_CLAIM,&h),-EOPNOTSUPP,before,"disabled_acquire");allow_overlay_ownership=true;
 unchanged(io(&readonly,AZ_OVERLAY_CLAIM,&h),-EBADF,before,"readonly_acquire");admin=0;unchanged(io(&owner,AZ_OVERLAY_CLAIM,&h),-EPERM,before,"unprivileged_acquire");admin=1;
 unchanged(io(&owner,AZ_OVERLAY_CLAIM^0x10000,&h),-ENOTTY,before,"wrong_ioctl_size");unchanged(io(&owner,AZ_OVERLAY_CLAIM^0xc0000000,&h),-ENOTTY,before,"wrong_ioctl_direction");
 for(unsigned field=0;field<4;field++){struct az_overlay_command bad=h;if(field==0)bad.version=1;if(field==1)bad.reserved=1;if(field==2)bad.run_id=0;if(field==3)bad.sequence=1;unchanged(io(&owner,AZ_OVERLAY_CLAIM,&bad),-EINVAL,before,"claim_bad_header");}
 h.epoch=1;unchanged(io(&owner,AZ_OVERLAY_CLAIM,&h),-ESTALE,before,"claim_stale_epoch");h.epoch=0;fail_from=from_calls+1;unchanged(io(&owner,AZ_OVERLAY_CLAIM,&h),-EFAULT,before,"claim_partial_copy_fault");fail_from=0;
 ck(crc_lengths_set("28,38,38,38",NULL)==0,"legacy_crc_change");before=snap();unchanged(claim(&owner,7,0),-EINVAL,before,"claim_wrong_crc_no_normalize");ck(crc_lengths_set("28,38,96,38",NULL)==0,"restore_fixture_crc");
 legacy(2,53,153);before=snap();ck(claim(&owner,7,0)==0,"claim_success");struct snapshot after=snap();ck(after.owner==&owner&&after.fenced&&after.run==7&&after.epoch==1&&after.sequence==0&&!memcmp(before.bytes,after.bytes,sizeof before.bytes),"claim_retains_full_overlay");
 before=snap();unchanged(claim(&other,8,1),-EBUSY,before,"second_owner_refused");
 struct az_overlay_state state;admin=0;ck(io(&readonly,AZ_OVERLAY_STATE,&state)==0 && state.version==2&&state.reserved==0&&state.flags==(AZ_OVERLAY_FENCED|AZ_OVERLAY_ACTIVE|AZ_OVERLAY_INJECTED|AZ_OVERLAY_ENABLED)&&!memcmp(state.frame,inject_pending[2],152),"readonly_state_raw_snapshot");admin=1;
 ck(io(&owner,AZ_OVERLAY_STATE,&state)==0 && (state.flags&AZ_OVERLAY_CALLER_OWNER),"same_file_alias_status");
 fail_to=to_calls+1;unchanged(io(&owner,AZ_OVERLAY_STATE,&state),-EFAULT,before,"state_copyout_fault_no_mutation");fail_to=0;
 struct az_overlay_frame r=frame(7,1,1,91);before=snap();fail_from=from_calls+1;unchanged(io(&owner,AZ_OVERLAY_FRAME,&r),-EFAULT,before,"tagged_partial_copy_fault");fail_from=0;
 ck(io(&owner,AZ_OVERLAY_FRAME,&r)==0 && mixer_sequence==1&&!memcmp(inject_pending[2],r.frame,152),"tagged_full_frame");
 before=snap();r.header.run_id=8;unchanged(io(&owner,AZ_OVERLAY_FRAME,&r),-ESTALE,before,"tagged_wrong_run");r.header.run_id=7;r.header.epoch=2;unchanged(io(&owner,AZ_OVERLAY_FRAME,&r),-ESTALE,before,"tagged_wrong_epoch");r.header.epoch=1;r.header.sequence=3;unchanged(io(&owner,AZ_OVERLAY_FRAME,&r),-ESTALE,before,"tagged_sequence_gap");
 h=command(7,1,2);ck(io(&owner,AZ_OVERLAY_RELEASE,&h)==0 && mixer_owner==NULL && mixer_fenced&&mixer_sequence==2&&!memcmp(inject_pending[2],r.frame,152),"explicit_release_retains_frame_fence");
 before=snap();unchanged(io(&owner,AZ_OVERLAY_RELEASE,&h),-EACCES,before,"release_not_reusable");ck(claim(&other,8,1)==0&&mixer_epoch==2,"reacquire_rotates_epoch");
 r.header=command(7,1,1);before=snap();unchanged(io(&other,AZ_OVERLAY_FRAME,&r),-ESTALE,before,"old_epoch_never_revived");ck(ctrl_release(NULL,&owner)==0&&same(before,snap()),"unrelated_close_no_effect");
 ck(ctrl_release(NULL,&other)==0&&mixer_owner==NULL&&mixer_fenced&&!memcmp(before.bytes,inject_pending,sizeof before.bytes),"owner_close_orphans_only");
 ck(claim(&other,8,2)==0&&mixer_epoch==3,"same_file_address_new_epoch");before=snap();r.header=command(8,2,1);unchanged(io(&other,AZ_OVERLAY_FRAME,&r),-ESTALE,before,"reused_address_stale_epoch");
 mixer_sequence=UINT64_MAX-1;r.header=command(8,3,UINT64_MAX);ck(io(&other,AZ_OVERLAY_FRAME,&r)==0&&mixer_sequence==UINT64_MAX,"last_sequence_accepted_without_wrap");r.header=command(8,3,0);before=snap();unchanged(io(&other,AZ_OVERLAY_FRAME,&r),-EOVERFLOW,before,"sequence_exhaustion_no_wrap");unchanged(io(&other,AZ_OVERLAY_RELEASE,&r.header),-EOVERFLOW,before,"release_exhaustion_no_wrap");ck(ctrl_release(NULL,&other)==0&&mixer_owner==NULL&&mixer_fenced,"close_after_sequence_exhaustion");mixer_epoch=UINT64_MAX-1;ck(claim(&owner,9,UINT64_MAX-1)==0&&mixer_epoch==UINT64_MAX,"last_epoch_accepted_without_wrap");ctrl_release(NULL,&owner);before=snap();unchanged(claim(&owner,9,UINT64_MAX),-EOVERFLOW,before,"epoch_exhaustion_no_wrap");
}

static void mutation_authorization(void){
 for(unsigned op=0;op<3;op++){
  reset();claim(&owner,7,0);unsigned cmd=op==2?AZ_OVERLAY_ROTATE:op?AZ_OVERLAY_RELEASE:AZ_OVERLAY_FRAME;
  struct az_overlay_frame r=frame(7,1,1,42);struct snapshot b=snap();
  admin=0;unchanged(io(&owner,cmd,&r),-EPERM,b,"owner_mutation_capability_loss");admin=1;
  owner.f_mode=0;unchanged(io(&owner,cmd,&r),-EBADF,b,"owner_mutation_write_mode_loss");owner.f_mode=FMODE_WRITE;
  allow_overlay_ownership=false;unchanged(io(&owner,cmd,&r),-EOPNOTSUPP,b,"owner_mutation_opt_in_disabled");allow_overlay_ownership=true;
  fail_from=from_calls+1;unchanged(io(&owner,cmd,&r),-EFAULT,b,"owner_mutation_partial_copy_fault");fail_from=0;
  for(unsigned bad=0;bad<3;bad++){struct az_overlay_frame invalid=r;if(bad==0)invalid.header.version=1;if(bad==1)invalid.header.reserved=1;if(bad==2)invalid.header.run_id=0;unchanged(io(&owner,cmd,&invalid),-EINVAL,b,"owner_mutation_malformed_header");}
 }
}

/* These single oracles are also run against targeted, compilable mutants. */
static void owner_oracle(void){reset();claim(&owner,7,0);struct snapshot b=snap();struct az_overlay_frame r=frame(7,1,1,42);unchanged(io(&other,AZ_OVERLAY_FRAME,&r),-EACCES,b,"file_identity_required");}
static void sequence_oracle(void){reset();claim(&owner,7,0);struct az_overlay_frame r=frame(7,1,1,42);io(&owner,AZ_OVERLAY_FRAME,&r);struct snapshot b=snap();unchanged(io(&owner,AZ_OVERLAY_FRAME,&r),-ESTALE,b,"duplicate_sequence_refused");}
static void close_oracle(void){reset();legacy(2,44,153);claim(&owner,7,0);struct snapshot b=snap();ctrl_release(NULL,&owner);ck(mixer_fenced&&mixer_owner==NULL&&!memcmp(b.bytes,inject_pending,sizeof b.bytes)&&legacy(2,99,153)==-EBUSY,"close_keeps_permanent_fence");}
static void clear_oracle(void){reset();for(unsigned i=0;i<4;i++)legacy(i,40+i,153);claim(&owner,7,0);struct snapshot b=snap();unchanged(ctrl_write(&other,NULL,0,NULL),-EBUSY,b,"clear_all_atomically_refused");}
static void parse_oracle(void){reset();struct snapshot b=snap();unchanged(crc_lengths_set("29,bad,96,38",NULL),-EINVAL,b,"parse_failure_no_partial_commit");}

struct job {int op;unsigned pause_copy,pause_lock,pause_unlock;struct file *file;unsigned cmd;struct az_overlay_frame request;u8 bytes[153];size_t size;const char *text;long result;struct az_overlay_state state;};
static void *work(void *v){struct job *j=v;tls_reset();pause_from=j->pause_copy;pause_before_lock=j->pause_lock;pause_after_unlock=j->pause_unlock;
 if(j->op==0)j->result=ctrl_write(j->file,(char *)j->bytes,j->size,NULL);
 if(j->op==1)j->result=io(j->file,j->cmd,&j->request);
 if(j->op==2)j->result=crc_lengths_set(j->text,NULL);
 if(j->op==3)j->result=spi_read(&spi,(char *)j->bytes,152,NULL);
 if(j->op==4)j->result=io(j->file,AZ_OVERLAY_STATE,&j->state);
 if(j->op==5)j->result=crc_lengths_get((char *)j->bytes,NULL);
 return NULL;}
static pthread_t launch(struct job *j){pthread_t t;ck(!pthread_create(&t,NULL,work,j),"pthread_create");wait_held();return t;}
static void finish(pthread_t t){let_go();ck(!pthread_join(t,NULL),"pthread_join");}
static void fence_oracle(void){reset();legacy(2,23,153);struct job j={.op=0,.file=&other,.size=153,.pause_copy=2};j.bytes[0]=2;pattern(j.bytes+1,98);pthread_t t=launch(&j);claim(&owner,7,0);struct snapshot b=snap();finish(t);unchanged(j.result,-EBUSY,b,"queued_legacy_write_refused_at_commit");}
static void crc_snapshot_oracle(void){reset();legacy(2,11,153);u8 expected[152];memcpy(expected,inject_pending[2],152);stamp(expected,96);struct job j={.op=3,.pause_unlock=1};pthread_t t=launch(&j);legacy(2,87,153);crc_lengths_set("28,38,38,38",NULL);finish(t);ck(j.result==152&&!memcmp(j.bytes,expected,152),"read_frame_and_crc_same_snapshot");}
static void schedules(void){
 reset();struct job getter={.op=5,.pause_unlock=1};pthread_t gt=launch(&getter);crc_lengths_set("31",NULL);finish(gt);ck(getter.result==11&&!strcmp((char *)getter.bytes,"28,38,96,38"),"crc_get_values_and_count_same_snapshot");
 for(int which=0;which<2;which++){reset();legacy(2,11,153);struct job j={.op=0,.file=&other,.size=which?0:1,.pause_lock=1};j.bytes[0]=2;pthread_t t=launch(&j);claim(&owner,7,0);struct snapshot b=snap();finish(t);unchanged(j.result,-EBUSY,b,which?"queued_clear_all_refused":"queued_single_clear_refused");}
 reset();struct job crc={.op=2,.text="29,39,38,39",.pause_lock=1};pthread_t t=launch(&crc);claim(&owner,7,0);struct snapshot b=snap();finish(t);unchanged(crc.result,-EBUSY,b,"queued_crc_change_refused_at_commit");
 reset();claim(&owner,7,0);struct job old={.op=1,.file=&owner,.cmd=AZ_OVERLAY_FRAME,.request=frame(7,1,1,34),.pause_copy=1};t=launch(&old);struct az_overlay_command h=command(7,1,1);io(&owner,AZ_OVERLAY_RELEASE,&h);claim(&other,8,1);b=snap();finish(t);unchanged(old.result,-EACCES,b,"stale_inflight_old_file_refused");
 reset();claim(&owner,7,0);old=(struct job){.op=1,.file=&owner,.cmd=AZ_OVERLAY_FRAME,.request=frame(7,1,1,34),.pause_copy=1};t=launch(&old);h=command(7,1,1);io(&owner,AZ_OVERLAY_RELEASE,&h);claim(&owner,8,1);b=snap();finish(t);unchanged(old.result,-ESTALE,b,"stale_inflight_same_file_epoch_refused");
 reset();legacy(2,13,153);claim(&owner,7,0);struct job state={.op=4,.file=&owner,.pause_unlock=1};t=launch(&state);h=command(7,1,1);io(&owner,AZ_OVERLAY_RELEASE,&h);claim(&other,8,1);finish(t);u8 expected[152];pattern(expected,13);ck(state.result==0&&state.state.run_id==7&&state.state.epoch==1&&state.state.sequence==0&&state.state.flags==31&&!memcmp(state.state.frame,expected,152),"state_is_coherent_historical_snapshot");
}
static void crc_and_reads(void){
 reset();allocation_failure=1;struct snapshot b=snap();unchanged(crc_lengths_set("28,38,96,38",NULL),-ENOMEM,b,"crc_allocation_failure");allocation_failure=0;
 const char *bad[]={"","28,","28,38,96,38,1","2147483648","28,38,x,38"};for(unsigned i=0;i<5;i++){b=snap();long rc=crc_lengths_set(bad[i],NULL);ck(rc<0&&same(b,snap()),"malformed_crc_unchanged");}
 claim(&owner,7,0);b=snap();unchanged(crc_lengths_set("1,2,95,4",NULL),-EBUSY,b,"fenced_crc96_frozen");ck(crc_lengths_set("29",NULL)==0&&spi_crc_len[2]==96&&n_crc==1,"short_crc_list_one_preserves_mixer");ck(crc_lengths_set("29,39",NULL)==0&&spi_crc_len[2]==96&&n_crc==2,"short_crc_list_two_preserves_mixer");ck(crc_lengths_set("29,39,96,39",NULL)==0&&n_crc==4,"unrelated_crc_changes_allowed");char text[PAGE_SIZE];ck(crc_lengths_get(text,NULL)==11&&!strcmp(text,"29,39,96,39"),"crc_get_snapshot");
 for(unsigned bus=0;bus<4;bus++){b=snap();if(bus==2){unchanged(legacy(bus,90,153),-EBUSY,b,"fenced_mixer_write");unchanged(legacy(bus,90,1),-EBUSY,b,"fenced_mixer_clear");}else{ck(legacy(bus,90,153)==153,"unrelated_bus_write");ck(legacy(bus,90,1)==1,"unrelated_bus_clear");}}
 reset();legacy(2,83,153);u8 expected[152],got[152];memcpy(expected,inject_pending[2],152);stamp(expected,96);ck(spi_read(&spi,(char *)got,152,NULL)==152&&!memcmp(expected,got,152),"read_preserves_all_noncrc_bytes");b=snap();fail_to=to_calls+1;unchanged(spi_read(&spi,(char *)got,152,NULL),-EFAULT,b,"read_copyout_fault_no_consume");fail_to=0;interrupted=1;unchanged(spi_read(&spi,(char *)got,152,NULL),-ERESTARTSYS,b,"read_interrupt_no_consume");interrupted=0;
 ck(spi_read(&spi,(char *)got,37,NULL)==37&&!memcmp(got,expected,37),"short_read_preserved");
 ck(crc_lengths_set("28,38,2147483647,38",NULL)==0,"crc_intmax_accepted_legacy");ck(spi_read(&spi,(char *)got,152,NULL)==152&&!memcmp(got,inject_pending[2],152),"crc_intmax_read_no_overflow");ck(crc_lengths_set("28,38,-2147483648,38",NULL)==0,"crc_intmin_accepted_legacy");ck(spi_read(&spi,(char *)got,152,NULL)==152&&!memcmp(got,inject_pending[2],152),"crc_intmin_read_no_overflow");
}
static pthread_mutex_t start_mu=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t start_cv=PTHREAD_COND_INITIALIZER;static unsigned starters;static bool start_now;
static void *together(void *v){pthread_mutex_lock(&start_mu);starters++;pthread_cond_broadcast(&start_cv);while(!start_now)pthread_cond_wait(&start_cv,&start_mu);pthread_mutex_unlock(&start_mu);return work(v);}
static void pair(struct job *a,struct job *b){pthread_t x,y;starters=0;start_now=false;ck(!pthread_create(&x,NULL,together,a)&&!pthread_create(&y,NULL,together,b),"pair_create");pthread_mutex_lock(&start_mu);while(starters!=2)pthread_cond_wait(&start_cv,&start_mu);start_now=true;pthread_cond_broadcast(&start_cv);pthread_mutex_unlock(&start_mu);ck(!pthread_join(x,NULL)&&!pthread_join(y,NULL),"pair_join");}
static void simultaneous(void){reset();struct job a={.op=1,.file=&owner,.cmd=AZ_OVERLAY_CLAIM,.request={.header=command(7,0,0)}},b={.op=1,.file=&other,.cmd=AZ_OVERLAY_CLAIM,.request={.header=command(8,0,0)}};pair(&a,&b);ck((a.result==0&&b.result==-EBUSY)||(b.result==0&&a.result==-EBUSY),"simultaneous_claim_single_owner");reset();claim(&owner,7,0);a=(struct job){.op=1,.file=&owner,.cmd=AZ_OVERLAY_FRAME,.request=frame(7,1,1,1)};b=(struct job){.op=1,.file=&owner,.cmd=AZ_OVERLAY_FRAME,.request=frame(7,1,1,99)};pair(&a,&b);ck(((a.result==0&&b.result==-ESTALE)||(b.result==0&&a.result==-ESTALE))&&mixer_sequence==1&&!memcmp(inject_pending[2],a.result==0?a.request.frame:b.request.frame,152),"simultaneous_sequence_single_full_frame");}
static atomic_bool stress_done,stress_mid,stress_seen;static atomic_uint stress_errors,stress_reads;
static void *writer(void *unused){(void)unused;tls_reset();for(u64 i=1;i<=1000;i++){struct az_overlay_frame r=frame(7,1,i,(unsigned)(i%251));if(io(&owner,AZ_OVERLAY_FRAME,&r))atomic_fetch_add(&stress_errors,1);if(i==500){atomic_store_explicit(&stress_mid,true,memory_order_release);while(!atomic_load_explicit(&stress_seen,memory_order_acquire))sched_yield();}}atomic_store_explicit(&stress_done,true,memory_order_release);return NULL;}
static void *reader(void *unused){(void)unused;tls_reset();do{bool mid=atomic_load_explicit(&stress_mid,memory_order_acquire);u8 b[152],expected[152];if(spi_read(&spi,(char *)b,152,NULL)!=152){atomic_fetch_add(&stress_errors,1);continue;}pattern(expected,b[0]);stamp(expected,96);if(memcmp(b,expected,152))atomic_fetch_add(&stress_errors,1);atomic_fetch_add(&stress_reads,1);if(mid)atomic_store_explicit(&stress_seen,true,memory_order_release);}while(!atomic_load_explicit(&stress_done,memory_order_acquire));return NULL;}
static void concurrent(void){reset();legacy(2,0,153);claim(&owner,7,0);atomic_store(&stress_done,false);atomic_store(&stress_mid,false);atomic_store(&stress_seen,false);atomic_store(&stress_errors,0);atomic_store(&stress_reads,0);pthread_t w,r;
 int rc=pthread_create(&r,NULL,reader,NULL);ck(rc==0,"stress_reader_create");if(rc)exit(3);rc=pthread_create(&w,NULL,writer,NULL);ck(rc==0,"stress_writer_create");if(rc)exit(3);
 ck(!pthread_join(w,NULL),"stress_writer_join");ck(!pthread_join(r,NULL),"stress_reader_join");ck(atomic_load(&stress_mid)&&atomic_load(&stress_seen),"reader_consumed_while_writer_live");ck(atomic_load(&stress_reads)>0&&atomic_load(&stress_errors)==0&&mixer_sequence==1000,"concurrent_full_frames_no_torn_payload");}
/* V2-only boundaries. RNG is deterministic; no entropy/kernel-init proof. */
static void init_wait_oracle(void){reset();memset(mixer_instance,0,sizeof mixer_instance);random_wait_calls=random_fill_calls=0;random_wait_result=-ERESTARTSYS;ck(mixer_instance_init()==-ERESTARTSYS&&random_wait_calls==1&&random_fill_calls==0&&!mixer_instance[0]&&!mixer_instance[1],"random_wait_failure_before_fill");}
static void init_zero_oracle(void){reset();memset(mixer_instance,0,sizeof mixer_instance);random_wait_calls=random_fill_calls=0;random_values[0]=random_values[1]=0;ck(mixer_instance_init()==-EIO&&random_wait_calls==1&&random_fill_calls==1&&!mixer_instance[0]&&!mixer_instance[1],"zero_instance_refused");}
static void instance_oracle(void){reset();claim(&owner,7,0);struct az_overlay_frame r=frame(7,1,1,77);r.header.instance[1]^=0x8000000000000000ULL;struct snapshot b=snap();unchanged(io(&owner,AZ_OVERLAY_FRAME,&r),-ESTALE,b,"instance_identity_required");}
static void rotation_oracle(void){reset();legacy(2,14,153);claim(&owner,7,0);struct az_overlay_frame r=frame(7,1,1,77);struct snapshot b=snap();long rc=io(&owner,AZ_OVERLAY_ROTATE,&r);ck(rc==0&&mixer_epoch==2&&mixer_sequence==0&&mixer_owner==b.owner&&mixer_run==b.run&&mixer_fenced&&!memcmp(mixer_instance,b.instance,sizeof mixer_instance)&&!memcmp(inject_pending[2],r.frame,152),"rotation_commits_frame_epoch_and_zero_sequence");}
static void overflow_oracle(void){reset();claim(&owner,7,0);mixer_epoch=UINT64_MAX;struct az_overlay_frame r=frame(7,UINT64_MAX,1,77);struct snapshot b=snap();unchanged(io(&owner,AZ_OVERLAY_ROTATE,&r),-EOVERFLOW,b,"rotation_epoch_exhaustion_unchanged");}
static void stale_rotate_oracle(unsigned cmd){reset();claim(&owner,7,0);struct job old={.op=1,.file=&owner,.cmd=cmd,.request=frame(7,1,1,34),.pause_copy=1};pthread_t t=launch(&old);struct az_overlay_frame r=frame(7,1,1,88);assert(io(&owner,AZ_OVERLAY_ROTATE,&r)==0);struct snapshot b=snap();finish(t);unchanged(old.result,-ESTALE,b,"copied_old_generation_refused_after_rotation");}
static void atomic_rotate_oracle(void){reset();legacy(2,14,153);claim(&owner,7,0);struct job j={.op=1,.file=&owner,.cmd=AZ_OVERLAY_ROTATE,.request=frame(7,1,1,88),.pause_unlock=1};pthread_t t=launch(&j);struct az_overlay_state s;long rc=io(&owner,AZ_OVERLAY_STATE,&s);ck(rc==0&&s.epoch==2&&s.sequence==0&&s.run_id==7&&s.flags==31&&!memcmp(s.instance,mixer_instance,sizeof s.instance)&&!memcmp(s.frame,j.request.frame,152),"rotation_is_one_locked_state_transition");finish(t);}
static void v2_initialization_and_identity(void){
 reset();allow_overlay_ownership=false;memset(mixer_instance,0,sizeof mixer_instance);random_wait_calls=random_fill_calls=0;ck(mixer_instance_init()==0&&random_wait_calls==0&&random_fill_calls==0&&!mixer_instance[0]&&!mixer_instance[1],"disabled_init_avoids_rng");struct az_overlay_state s;ck(io(&readonly,AZ_OVERLAY_STATE,&s)==0&&s.version==2&&s.flags==0&&!s.instance[0]&&!s.instance[1],"disabled_state_exposes_no_enabled_identity");
 reset();ck(random_wait_calls==1&&random_fill_calls==1&&!memcmp(mixer_instance,random_values,sizeof mixer_instance),"enabled_init_exact_one_wait_and_fill");ck(io(&readonly,AZ_OVERLAY_STATE,&s)==0&&s.flags==AZ_OVERLAY_ENABLED&&!memcmp(s.instance,mixer_instance,sizeof s.instance),"enabled_state_identity_before_claim");
 for(unsigned word=0;word<2;word++){reset();memset(mixer_instance,0,sizeof mixer_instance);random_values[0]=random_values[1]=0;random_values[word]=1ULL<<63;ck(mixer_instance_init()==0&&mixer_instance[word]==(1ULL<<63),"either_instance_word_may_be_nonzero");}
 init_wait_oracle();init_zero_oracle();instance_oracle();
 reset();struct az_overlay_frame r=frame(7,0,0,21);const unsigned v1[]={0x40205a50u,0x40b85a51u,0x40205a52u,0x80c05a53u};for(unsigned i=0;i<4;i++){struct snapshot b=snap();unsigned f=from_calls,t=to_calls;unchanged(io(&owner,v1[i],&r),-ENOTTY,b,"v1_exact_command_refused");ck(from_calls==f&&to_calls==t,"v1_never_copies_user_storage");}
 for(unsigned op=0;op<4;op++){reset();if(op)claim(&owner,7,0);r=frame(7,op?1:0,op?1:0,21);unsigned cmds[]={AZ_OVERLAY_CLAIM,AZ_OVERLAY_FRAME,AZ_OVERLAY_RELEASE,AZ_OVERLAY_ROTATE};for(unsigned word=0;word<2;word++){struct az_overlay_frame bad=r;bad.header.instance[word]^=1ULL<<63;struct snapshot b=snap();unchanged(io(&owner,cmds[op],&bad),-ESTALE,b,"every_mutator_binds_full_instance");}}
 reset();claim(&owner,7,0);r=frame(7,1,1,23);r.header.instance[0]^=1;struct snapshot b=snap();unchanged(io(&other,AZ_OVERLAY_FRAME,&r),-ESTALE,b,"wrong_instance_checked_before_file");
 /* Simulated new module has equal file/run/epoch/sequence, differing identity. */
 reset();claim(&owner,7,0);r=frame(7,1,1,23);mixer_instance[1]^=1;b=snap();unchanged(io(&owner,AZ_OVERLAY_FRAME,&r),-ESTALE,b,"equal_counters_different_module_refuse_replay");
}
static void v2_rotations(void){
 rotation_oracle();overflow_oracle();reset();claim(&owner,7,0);struct az_overlay_frame r=frame(7,1,1,66);struct snapshot b=snap();unchanged(io(&other,AZ_OVERLAY_ROTATE,&r),-EACCES,b,"rotation_requires_same_file");
 r.header.run_id=8;unchanged(io(&owner,AZ_OVERLAY_ROTATE,&r),-ESTALE,b,"rotation_wrong_run");r.header.run_id=7;r.header.epoch=2;unchanged(io(&owner,AZ_OVERLAY_ROTATE,&r),-ESTALE,b,"rotation_wrong_epoch");r.header.epoch=1;r.header.sequence=2;unchanged(io(&owner,AZ_OVERLAY_ROTATE,&r),-ESTALE,b,"rotation_wrong_sequence");
 mixer_sequence=UINT64_MAX;r.header.sequence=0;b=snap();unchanged(io(&owner,AZ_OVERLAY_ROTATE,&r),-EOVERFLOW,b,"rotation_sequence_exhaustion_unchanged");ctrl_release(NULL,&owner);ck(mixer_fenced&&!mixer_owner,"close_after_rotation_sequence_exhaustion");
 reset();claim(&owner,7,0);mixer_epoch=UINT64_MAX-1;mixer_sequence=UINT64_MAX-1;r=frame(7,UINT64_MAX-1,UINT64_MAX,66);ck(io(&owner,AZ_OVERLAY_ROTATE,&r)==0&&mixer_epoch==UINT64_MAX&&mixer_sequence==0,"rotation_last_epoch_and_sequence_accepted");r.header=command(7,UINT64_MAX,1);ck(io(&owner,AZ_OVERLAY_FRAME,&r)==0&&mixer_sequence==1,"frame_still_allowed_in_last_epoch");struct az_overlay_command h=command(7,UINT64_MAX,2);ck(io(&owner,AZ_OVERLAY_RELEASE,&h)==0&&mixer_fenced&&!mixer_owner,"release_still_allowed_in_last_epoch");
 reset();claim(&owner,7,0);r=frame(7,1,1,66);io(&owner,AZ_OVERLAY_ROTATE,&r);r.header=command(7,2,1);ck(io(&owner,AZ_OVERLAY_FRAME,&r)==0&&mixer_sequence==1,"new_generation_first_sequence_accepted");b=snap();unchanged(io(&owner,AZ_OVERLAY_FRAME,&r),-ESTALE,b,"new_generation_duplicate_refused");
 /* Payload provenance is intentionally unknowable to the kernel. A broker
  * which retags old bytes can submit them; this is a limitation counterexample. */
 reset();claim(&owner,7,0);struct az_overlay_frame old=frame(7,1,1,13);r=frame(7,1,1,88);io(&owner,AZ_OVERLAY_ROTATE,&r);old.header=command(7,2,1);ck(io(&owner,AZ_OVERLAY_FRAME,&old)==0&&!memcmp(inject_pending[2],old.frame,152),"retagged_old_payload_is_accepted_limitation");
}
static void v2_schedules(void){
 /* STATE can show seq0 while a copied request is still capable of committing. */
 reset();claim(&owner,7,0);struct job old={.op=1,.file=&owner,.cmd=AZ_OVERLAY_FRAME,.request=frame(7,1,1,34),.pause_copy=1};pthread_t t=launch(&old);struct az_overlay_state s;ck(io(&owner,AZ_OVERLAY_STATE,&s)==0&&s.epoch==1&&s.sequence==0,"state_does_not_classify_pending_request_cancelled");finish(t);ck(old.result==0&&mixer_sequence==1&&!memcmp(inject_pending[2],old.request.frame,152),"pending_request_commits_after_earlier_state");
 stale_rotate_oracle(AZ_OVERLAY_FRAME);stale_rotate_oracle(AZ_OVERLAY_RELEASE);stale_rotate_oracle(AZ_OVERLAY_ROTATE);atomic_rotate_oracle();
 reset();claim(&owner,7,0);struct job rotate={.op=1,.file=&owner,.cmd=AZ_OVERLAY_ROTATE,.request=frame(7,1,1,88),.pause_copy=1};t=launch(&rotate);struct az_overlay_frame f=frame(7,1,1,34);io(&owner,AZ_OVERLAY_FRAME,&f);struct snapshot b=snap();finish(t);unchanged(rotate.result,-ESTALE,b,"copied_rotation_loses_to_frame_sequence");
 reset();legacy(2,13,153);claim(&owner,7,0);struct job state={.op=4,.file=&owner,.pause_unlock=1};t=launch(&state);f=frame(7,1,1,88);io(&owner,AZ_OVERLAY_ROTATE,&f);finish(t);u8 expected[152];pattern(expected,13);ck(state.result==0&&state.state.epoch==1&&state.state.sequence==0&&state.state.flags==31&&!memcmp(state.state.instance,mixer_instance,16)&&!memcmp(state.state.frame,expected,152),"delayed_state_keeps_whole_pre_rotation_snapshot");ck(io(&owner,AZ_OVERLAY_STATE,&s)==0&&s.epoch==2&&s.sequence==0&&!memcmp(s.frame,f.frame,152),"later_state_observes_rotation");
 reset();legacy(2,13,153);claim(&owner,7,0);struct job read={.op=3,.pause_unlock=1};t=launch(&read);f=frame(7,1,1,88);io(&owner,AZ_OVERLAY_ROTATE,&f);finish(t);stamp(expected,96);ck(read.result==152&&!memcmp(read.bytes,expected,152),"sampled_pre_rotation_frame_survives_rotation");
 for(unsigned otherop=0;otherop<2;otherop++){reset();claim(&owner,7,0);struct job a={.op=1,.file=&owner,.cmd=AZ_OVERLAY_ROTATE,.request=frame(7,1,1,11)},j={.op=1,.file=&owner,.cmd=otherop?AZ_OVERLAY_ROTATE:AZ_OVERLAY_FRAME,.request=frame(7,1,1,99)};pair(&a,&j);bool aw=a.result==0;ck((aw&&j.result==-ESTALE)||(!aw&&a.result==-ESTALE&&j.result==0),"simultaneous_rotation_exact_single_winner");bool rotated=aw||otherop;ck(mixer_epoch==(rotated?2u:1u)&&mixer_sequence==(rotated?0u:1u)&&mixer_owner==&owner&&mixer_run==7&&mixer_fenced&&!memcmp(inject_pending[2],aw?a.request.frame:j.request.frame,152),"simultaneous_rotation_winner_whole_state");}
}

int main(int argc,char **argv){alarm(30);const char *s=argc>1?argv[1]:"all";
 if(!strcmp(s,"all")){basic();mutation_authorization();owner_oracle();sequence_oracle();close_oracle();clear_oracle();parse_oracle();fence_oracle();crc_snapshot_oracle();schedules();crc_and_reads();simultaneous();concurrent();v2_initialization_and_identity();v2_rotations();v2_schedules();}
 else if(!strcmp(s,"owner"))owner_oracle();else if(!strcmp(s,"sequence"))sequence_oracle();else if(!strcmp(s,"close"))close_oracle();else if(!strcmp(s,"clear"))clear_oracle();else if(!strcmp(s,"parse"))parse_oracle();else if(!strcmp(s,"fence"))fence_oracle();else if(!strcmp(s,"crc-snapshot"))crc_snapshot_oracle();else if(!strcmp(s,"init-wait"))init_wait_oracle();else if(!strcmp(s,"init-zero"))init_zero_oracle();else if(!strcmp(s,"instance"))instance_oracle();else if(!strcmp(s,"rotation"))rotation_oracle();else if(!strcmp(s,"overflow"))overflow_oracle();else if(!strcmp(s,"stale-rotation"))stale_rotate_oracle(AZ_OVERLAY_FRAME);else if(!strcmp(s,"atomic-rotation"))atomic_rotate_oracle();else return 2;
 printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;}
