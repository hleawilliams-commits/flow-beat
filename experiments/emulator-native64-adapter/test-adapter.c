/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual adapter + frozen broker + exact kernel methods under owned-memory
 * usercopy and fd-reference shims. No real fd/device, transport or service.
 * Syscall-result faults are explicit synthetic boundary replies, not evidence
 * about which errors a real running kernel/controller would produce. */
#include "kernel-shim.inc"
#include "candidate.inc"
#include "broker-under-test.inc"
#include "adapter-under-test.inc"

static unsigned checks,failures;
static void ck(bool ok,const char *label){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",label);}}
static void fill_bytes(uint8_t *p,unsigned seed){for(unsigned i=0;i<152;i++)p[i]=(uint8_t)(seed+23*i);}
static bool bytes_zero(const void *p,size_t n){const unsigned char *b=p;for(size_t i=0;i<n;i++)if(b[i])return false;return true;}
static bool same_state(const struct mb_state *a,const struct mb_state *b){return a->instance[0]==b->instance[0]&&a->instance[1]==b->instance[1]&&a->run_id==b->run_id&&a->epoch==b->epoch&&a->sequence==b->sequence&&a->flags==b->flags&&a->crc_len==b->crc_len&&!memcmp(a->frame,b->frame,152);}
static void tls_clean(void){from_calls=to_calls=fail_from=fail_to=pause_from=pause_before_lock=pause_after_unlock=0;admin=1;allocation_failure=interrupted=0;}

/* Every table entry denotes a reference to the same file object, not a new
 * kernel owner. Only the final modeled close runs the actual ctrl_release.
 * Caller-fd stability during attach and serialized use/disposal are assumed. */
#define FD_COUNT 32
static struct file file_owner,file_other;
static struct file *fd_table[FD_COUNT];
static unsigned duplicate_calls,ioctl_calls,close_calls,native_calls,release_calls;
static unsigned state_calls,mutation_calls;
static int last_duplicate_source,last_ioctl_fd,last_close_fd,duplicate_target;
static unsigned long last_request;
static uint8_t last_wire[208];static size_t last_wire_size;
static bool state_input_zero,force_duplicate,force_ioctl,force_close,forced_native,close_effect,fault_mutator_only;
static struct mba_sys_result duplicate_reply,ioctl_reply,close_reply;
static int wire_mutation;
static struct mba_adapter **watch_consumed;
static bool close_saw_consumed;
static void raw_close(int fd){assert(fd>=0&&fd<FD_COUNT&&fd_table[fd]);struct file *file=fd_table[fd];fd_table[fd]=NULL;for(int i=0;i<FD_COUNT;i++)if(fd_table[i]==file)return;release_calls++;assert(!ctrl_release(NULL,file));}
static size_t wire_size(unsigned long op){switch(op){case AZ_OVERLAY_STATE:return sizeof(struct az_overlay_state);case AZ_OVERLAY_FRAME:case AZ_OVERLAY_ROTATE:return sizeof(struct az_overlay_frame);case AZ_OVERLAY_RESTORE:return sizeof(struct az_overlay_restore);default:return 0;}}
static void mutate_wire(struct az_overlay_state *s,int which){switch(which){case 1:s->version=1;break;case 2:s->reserved=1;break;case 3:s->instance[0]=s->instance[1]=0;break;case 4:s->instance[0]^=1;break;case 5:s->instance[1]^=1;break;case 6:s->run_id=0;break;case 7:s->run_id++;break;case 8:s->epoch=0;break;case 9:s->epoch++;break;case 10:s->sequence++;break;case 11:s->flags=0;break;case 12:s->flags^=8;break;case 13:s->crc_len=95;break;case 14:s->frame[0]^=1;break;case 15:s->frame[96]^=1;break;case 16:s->frame[97]^=1;break;case 17:s->frame[151]^=1;break;case 18:s->flags|=32;break;case 19:s->flags&=~4u;break;case 20:s->flags&=~16u;break;default:break;}}
struct mba_sys_result mba_sys_duplicate(int fd){
 duplicate_calls++;last_duplicate_source=fd;
 if(force_duplicate&&duplicate_reply.result<0)return duplicate_reply;
 if(fd<0||fd>=FD_COUNT||!fd_table[fd])return(struct mba_sys_result){-1,EBADF};
 int target=force_duplicate?duplicate_reply.result:duplicate_target;
 if(target==fd)return(struct mba_sys_result){target,force_duplicate?duplicate_reply.error:0};
 assert(target>=0&&target<FD_COUNT&&!fd_table[target]);fd_table[target]=fd_table[fd];
 return(struct mba_sys_result){target,force_duplicate?duplicate_reply.error:0};
}
struct mba_sys_result mba_sys_ioctl(int fd,unsigned long request,void *argument){
 ioctl_calls++;last_ioctl_fd=fd;last_request=request;last_wire_size=wire_size(request);memset(last_wire,0,sizeof last_wire);
 assert(last_wire_size&&argument);memcpy(last_wire,argument,last_wire_size);
 if(request==AZ_OVERLAY_STATE){state_calls++;state_input_zero=bytes_zero(argument,last_wire_size);}else mutation_calls++;
 bool injected_result=force_ioctl&&(!fault_mutator_only||request!=AZ_OVERLAY_STATE);
 if(injected_result&&!forced_native)return ioctl_reply;
 if(fd<0||fd>=FD_COUNT||!fd_table[fd])return(struct mba_sys_result){-1,EBADF};
 native_calls++;long rc=ctrl_ioctl(fd_table[fd],(unsigned)request,(unsigned long)argument);
 if(request==AZ_OVERLAY_STATE&&rc==0)mutate_wire(argument,wire_mutation);
 if(injected_result)return ioctl_reply;
 return(struct mba_sys_result){rc<0?-1:(int)rc,rc<0?(int)-rc:0};
}
struct mba_sys_result mba_sys_close(int fd){
 close_calls++;last_close_fd=fd;if(watch_consumed)close_saw_consumed=*watch_consumed==NULL;
 if(fd<0||fd>=FD_COUNT||!fd_table[fd])return(struct mba_sys_result){-1,EBADF};
 if(!force_close||close_effect)raw_close(fd);
 return force_close?close_reply:(struct mba_sys_result){0,0};
}
static struct mb_state kernel_state(void){struct az_overlay_state s={0};assert(!ctrl_ioctl(&file_owner,AZ_OVERLAY_STATE,(unsigned long)&s));assert(s.version==2&&!s.reserved);struct mb_state out={.instance={s.instance[0],s.instance[1]},.run_id=s.run_id,.epoch=s.epoch,.sequence=s.sequence,.flags=s.flags,.crc_len=s.crc_len};memcpy(out.frame,s.frame,152);return out;}
static void reset(unsigned injected){
 tls_clean();memset(fd_table,0,sizeof fd_table);memset(&file_owner,0,sizeof file_owner);memset(&file_other,0,sizeof file_other);file_owner.f_mode=file_other.f_mode=FMODE_WRITE;fd_table[3]=&file_owner;
 duplicate_calls=ioctl_calls=close_calls=native_calls=release_calls=state_calls=mutation_calls=0;last_duplicate_source=last_ioctl_fd=last_close_fd=-1;duplicate_target=8;last_request=0;last_wire_size=0;memset(last_wire,0,sizeof last_wire);force_duplicate=force_ioctl=force_close=forced_native=fault_mutator_only=false;close_effect=true;state_input_zero=false;wire_mutation=0;watch_consumed=NULL;close_saw_consumed=false;
 duplicate_reply=ioctl_reply=close_reply=(struct mba_sys_result){0,0};
 memset(inject_pending,0,sizeof inject_pending);memset(has_inject,0,sizeof has_inject);int crc[4]={28,38,96,38};memcpy(spi_crc_len,crc,sizeof crc);n_crc=4;mixer_fenced=false;mixer_owner=NULL;mixer_run=mixer_epoch=mixer_sequence=0;allow_overlay_ownership=true;memset(mixer_instance,0,sizeof mixer_instance);random_wait_result=0;assert(!mixer_instance_init());testmode_held=0;
 if(injected){uint8_t packet[153]={2};fill_bytes(packet+1,31);assert(ctrl_write(&file_other,(char *)packet,sizeof packet,NULL)==153);}
 struct az_overlay_command c={.version=2,.instance={mixer_instance[0],mixer_instance[1]},.run_id=7};assert(!ctrl_ioctl(&file_owner,AZ_OVERLAY_CLAIM,(unsigned long)&c));
}
static void cleanup(struct mba_adapter **a){
 /* Only fixture-owned, externally quiescent references are destroyed here.
  * This is never labeled permission to close an unresolved real session. */
 watch_consumed=NULL;force_close=false;close_effect=true;
 if(a&&*a){struct mba_close_report report;assert(!mba_dispose(a,&report)&&!*a&&report.attempted);}
 for(int fd=0;fd<FD_COUNT;fd++)if(fd_table[fd])raw_close(fd);
}
static struct mba_adapter *attach(void){struct mb_state s=kernel_state();struct mba_adapter *a=NULL;struct mba_attach_report report;assert(!mba_attach(3,&s,&a,&report)&&a&&!report.error&&!report.cleanup_attempted);return a;}
static struct mb_backend backend(struct mba_adapter *a){struct mb_backend b;assert(!mba_get_backend(a,&b));return b;}
static struct mb_command command(unsigned injected,unsigned seed){struct mb_state s=kernel_state();struct mb_command c={.instance={s.instance[0],s.instance[1]},.run_id=s.run_id,.epoch=s.epoch,.sequence=s.sequence+1,.injected=injected};if(injected)fill_bytes(c.frame,seed);else memcpy(c.frame,az_bus_frame[2],152);return c;}
static bool expected_wire(enum mb_operation op,const struct mb_command *c){
 struct az_overlay_command h={.version=2,.reserved=0,.instance={c->instance[0],c->instance[1]},.run_id=c->run_id,.epoch=c->epoch,.sequence=c->sequence};
 if(op==MB_OP_RESTORE){struct az_overlay_restore r={.header=h,.injected=c->injected,.reserved=0};memcpy(r.frame,c->frame,152);return last_request==AZ_OVERLAY_RESTORE&&last_wire_size==sizeof r&&!memcmp(last_wire,&r,sizeof r);}
 struct az_overlay_frame r={.header=h};memcpy(r.frame,c->frame,152);return last_request==(op==MB_OP_FRAME?AZ_OVERLAY_FRAME:AZ_OVERLAY_ROTATE)&&last_wire_size==sizeof r&&!memcmp(last_wire,&r,sizeof r);
}

/* Each selector has one functional ck. Cleanup precedes that oracle. */
static void mode_oracle(void){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_command c=command(0,0);struct mb_backend_reply r=b.submit(b.context,MB_OP_RESTORE,&c);struct mb_state s=kernel_state();bool wire=expected_wire(MB_OP_RESTORE,&c);cleanup(&a);ck(r.outcome==MB_OUTCOME_COMMITTED&&!r.error&&wire&&s.flags==23&&!memcmp(s.frame,c.frame,152),"adapter_restore_preserves_explicit_idle_mode");}
static void payload_oracle(void){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_command c=command(1,97);struct mb_backend_reply r=b.submit(b.context,MB_OP_RESTORE,&c);struct mb_state s=kernel_state();bool wire=expected_wire(MB_OP_RESTORE,&c);cleanup(&a);ck(r.outcome==MB_OUTCOME_COMMITTED&&wire&&!memcmp(s.frame,c.frame,152),"adapter_copies_all_raw_restore_bytes");}
static void success_errno_oracle(void){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_command c=command(1,97);force_ioctl=forced_native=true;ioctl_reply=(struct mba_sys_result){0,EINTR};unsigned calls=ioctl_calls;struct mb_backend_reply r=b.submit(b.context,MB_OP_RESTORE,&c);unsigned delta=ioctl_calls-calls;cleanup(&a);ck(r.outcome==MB_OUTCOME_COMMITTED&&!r.error&&delta==1,"adapter_success_ignores_stale_errno");}
static void unknown_oracle(void){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_command c=command(1,97);force_ioctl=true;forced_native=false;ioctl_reply=(struct mba_sys_result){-1,EINTR};unsigned calls=ioctl_calls;struct mb_backend_reply r=b.submit(b.context,MB_OP_RESTORE,&c);bool untouched=mixer_epoch==1&&close_calls==0&&ioctl_calls==calls+1;cleanup(&a);ck(r.outcome==MB_OUTCOME_UNKNOWN&&r.error==EINTR&&untouched,"adapter_eintr_is_unknown_without_retry_or_close");}
static void positive_oracle(void){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_command c=command(1,97);force_ioctl=forced_native=true;ioctl_reply=(struct mba_sys_result){1,0};struct mb_backend_reply r=b.submit(b.context,MB_OP_RESTORE,&c);bool committed=mixer_epoch==2;cleanup(&a);ck(r.outcome==MB_OUTCOME_UNKNOWN&&r.error==EPROTO&&committed,"adapter_unexpected_positive_return_is_unknown");}
static void rejected_oracle(void){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_command c=command(1,97);force_ioctl=true;forced_native=false;ioctl_reply=(struct mba_sys_result){-1,EACCES};unsigned calls=ioctl_calls;struct mb_backend_reply r=b.submit(b.context,MB_OP_RESTORE,&c);bool once=ioctl_calls==calls+1&&close_calls==0;cleanup(&a);ck(r.outcome==MB_OUTCOME_REJECTED&&r.error==EACCES&&once,"adapter_exact_precommit_error_is_rejected");}
static void state_zero_oracle(void){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_state out;memset(&out,0xA5,sizeof out);fail_to=to_calls+1;unsigned calls=ioctl_calls;int rc=b.snapshot(b.context,&out);fail_to=0;bool once=ioctl_calls==calls+1;cleanup(&a);ck(rc==EFAULT&&bytes_zero(&out,sizeof out)&&once,"adapter_failed_partial_state_never_publishes_fields");}
static void reserved_oracle(void){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);wire_mutation=2;struct mb_state out;memset(&out,0xA5,sizeof out);int rc=b.snapshot(b.context,&out);cleanup(&a);ck(rc==EPROTO&&bytes_zero(&out,sizeof out),"adapter_state_reserved_field_must_be_zero");}
static void binding_oracle(void){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);wire_mutation=7;struct mb_state out;memset(&out,0xA5,sizeof out);int rc=b.snapshot(b.context,&out);cleanup(&a);ck(rc==ESTALE&&bytes_zero(&out,sizeof out),"adapter_snapshot_remains_bound_to_run");}
static void attach_tail_oracle(void){reset(1);struct mb_state expected=kernel_state();expected.frame[151]^=1;struct mba_adapter *a=NULL;struct mba_attach_report report;int rc=mba_attach(3,&expected,&a,&report);bool failed=a==NULL&&report.error==ESTALE&&report.cleanup_attempted&&close_calls==1&&fd_table[3]==&file_owner;cleanup(&a);ck(rc==ESTALE&&failed,"adapter_attach_matches_full_expected_raw_state");}
static void fd_oracle(void){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_state expected=kernel_state();raw_close(3);fd_table[3]=&file_other;struct mb_state out;int rc=b.snapshot(b.context,&out);bool retained=last_ioctl_fd==8&&mixer_owner==&file_owner&&same_state(&expected,&out);cleanup(&a);ck(!rc&&retained,"adapter_private_duplicate_survives_original_fd_reuse");}
static void consume_oracle(void){reset(1);struct mba_adapter *a=attach();watch_consumed=&a;force_close=true;close_effect=true;close_reply=(struct mba_sys_result){-1,EINTR};struct mba_close_report report;int rc=mba_dispose(&a,&report);bool consumed=a==NULL&&close_saw_consumed&&close_calls==1&&report.attempted&&report.call.result==-1&&report.call.error==EINTR;a=NULL;cleanup(&a);ck(!rc&&consumed,"adapter_dispose_consumes_before_one_uncertain_close");}
static void local_oracle(void){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_command c=command(1,33);c.sequence=0;unsigned calls=ioctl_calls;struct mb_backend_reply r=b.submit(b.context,MB_OP_RESTORE,&c);bool noioctl=ioctl_calls==calls;cleanup(&a);ck(r.outcome==MB_OUTCOME_REJECTED&&r.error==EINVAL&&noioctl,"adapter_local_zero_sequence_never_calls_ioctl");}

static void attachment(void){
 for(unsigned mode=0;mode<2;mode++){reset(mode);struct mb_state expected=kernel_state();struct mba_adapter *a=NULL;struct mba_attach_report report;int rc=mba_attach(3,&expected,&a,&report);ck(!rc&&a&&!report.error&&!report.cleanup_attempted&&duplicate_calls==1&&ioctl_calls==1&&close_calls==0,"attach_one_duplicate_one_owned_snapshot");struct mb_backend b=backend(a);struct mb_state actual;ck(!b.snapshot(b.context,&actual)&&same_state(&expected,&actual)&&state_input_zero,"snapshot_roundtrips_mode_and_all_bytes");cleanup(&a);}
 reset(1);duplicate_target=0;force_duplicate=true;duplicate_reply=(struct mba_sys_result){0,EIO};struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_state out;ck(!b.snapshot(b.context,&out)&&last_ioctl_fd==0,"duplicate_fd_zero_is_valid_even_with_stale_errno");cleanup(&a);
 struct mba_sys_result baddup[]={{-1,EMFILE},{-1,0},{-1,-1},{-2,EBADF},{3,0}};int errors[]={EMFILE,EPROTO,EPROTO,EPROTO,EPROTO};
 for(unsigned i=0;i<sizeof baddup/sizeof *baddup;i++){reset(1);force_duplicate=true;duplicate_reply=baddup[i];struct mb_state expected=kernel_state();struct mba_attach_report report;memset(&report,0xA5,sizeof report);a=(void *)(uintptr_t)1;int rc=mba_attach(3,&expected,&a,&report);ck(rc==errors[i]&&a==NULL&&report.error==errors[i]&&!report.cleanup_attempted&&duplicate_calls==1&&ioctl_calls==0&&close_calls==0&&fd_table[3]==&file_owner,"failed_or_impossible_duplicate_never_closes_original");cleanup(&a);}
 /* Only valid-shaped disagreement reaches duplicate + exact STATE comparison. */
 const int fields[]={4,5,7,9,10,12,14,15,16,17};
 for(unsigned i=0;i<sizeof fields/sizeof *fields;i++){reset(1);struct mb_state expected=kernel_state();wire_mutation=fields[i];struct mba_attach_report report;a=NULL;int rc=mba_attach(3,&expected,&a,&report);ck(rc==ESTALE&&a==NULL&&report.error==ESTALE&&report.cleanup_attempted&&report.cleanup.result==0&&duplicate_calls==1&&ioctl_calls==1&&close_calls==1&&fd_table[3]==&file_owner,"attach_refuses_every_fresh_state_mismatch");cleanup(&a);}
 reset(1);struct mb_state expected=kernel_state();wire_mutation=2;force_close=true;close_effect=true;close_reply=(struct mba_sys_result){-1,EINTR};struct mba_attach_report report;a=NULL;int rc=mba_attach(3,&expected,&a,&report);ck(rc==EPROTO&&a==NULL&&report.error==EPROTO&&report.cleanup_attempted&&report.cleanup.result==-1&&report.cleanup.error==EINTR&&close_calls==1,"attach_primary_error_survives_uncertain_cleanup");cleanup(&a);
 reset(1);expected=kernel_state();a=(void *)(uintptr_t)1;memset(&report,0xA5,sizeof report);ck(mba_attach(-1,&expected,&a,&report)==EINVAL&&a==NULL&&report.error==EINVAL&&!duplicate_calls&&!report.cleanup_attempted,"attach_invalid_original_clears_outputs_without_io");ck(mba_attach(3,NULL,&a,&report)==EINVAL&&!a&&report.error==EINVAL&&!duplicate_calls,"attach_missing_expected_rejected");ck(mba_attach(3,&expected,NULL,&report)==EINVAL&&report.error==EINVAL&&!duplicate_calls,"attach_missing_handle_output_rejected");ck(mba_attach(3,&expected,&a,NULL)==EINVAL&&!a&&!duplicate_calls,"attach_missing_report_rejected");expected.flags=0;ck(mba_attach(3,&expected,&a,&report)==EINVAL&&!a&&!duplicate_calls,"attach_invalid_expected_shape_before_duplication");cleanup(&a);
}
static void state_validation(void){
 const int malformed[]={1,2,3,6,8,11,13,18,19,20};
 for(unsigned i=0;i<sizeof malformed/sizeof *malformed;i++){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);wire_mutation=malformed[i];struct mb_state out;memset(&out,0xA5,sizeof out);unsigned calls=ioctl_calls;int rc=b.snapshot(b.context,&out);ck(rc==EPROTO&&bytes_zero(&out,sizeof out)&&ioctl_calls==calls+1&&close_calls==0,"malformed_state_is_zeroed_failure_without_cleanup");cleanup(&a);}
 const int stale[]={4,5,7};for(unsigned i=0;i<sizeof stale/sizeof *stale;i++){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);wire_mutation=stale[i];struct mb_state out;int rc=b.snapshot(b.context,&out);ck(rc==ESTALE&&bytes_zero(&out,sizeof out),"snapshot_identity_and_run_drift_refused");cleanup(&a);}
 struct mba_sys_result failureset[]={{-1,EIO},{-1,0},{-1,-1},{1,0},{-2,EFAULT}};int error[]={EIO,EPROTO,EPROTO,EPROTO,EPROTO};
 for(unsigned i=0;i<sizeof failureset/sizeof *failureset;i++){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);force_ioctl=true;forced_native=false;ioctl_reply=failureset[i];struct mb_state out;memset(&out,0xA5,sizeof out);unsigned calls=ioctl_calls;int rc=b.snapshot(b.context,&out);ck(rc==error[i]&&bytes_zero(&out,sizeof out)&&ioctl_calls==calls+1&&close_calls==0,"state_syscall_failure_never_publishes_or_retries");cleanup(&a);}
 reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);force_ioctl=forced_native=true;ioctl_reply=(struct mba_sys_result){0,EIO};struct mb_state out;ck(!b.snapshot(b.context,&out)&&out.flags==31&&state_input_zero,"successful_state_ignores_stale_errno");force_ioctl=false;unsigned calls=ioctl_calls;ck(b.snapshot(b.context,NULL)==EINVAL&&ioctl_calls==calls,"snapshot_null_output_has_no_syscall");memset(&out,0xA5,sizeof out);ck(b.snapshot(NULL,&out)==EINVAL&&bytes_zero(&out,sizeof out)&&ioctl_calls==calls,"snapshot_null_context_zeroes_output");cleanup(&a);
}
static void wire_and_local_validation(void){
 enum mb_operation ops[]={MB_OP_FRAME,MB_OP_ROTATE,MB_OP_RESTORE,MB_OP_RESTORE};unsigned modes[]={1,1,0,1};
 for(unsigned i=0;i<4;i++){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_command c=command(modes[i],70+i);unsigned calls=ioctl_calls;struct mb_backend_reply result=b.submit(b.context,ops[i],&c);struct mb_state s=kernel_state();
  ck(result.outcome==MB_OUTCOME_COMMITTED&&!result.error&&ioctl_calls==calls+1&&last_ioctl_fd==8&&expected_wire(ops[i],&c),"explicit_wire_translation_all_fields_and_bytes");
  ck(s.flags==(modes[i]?31u:23u)&&!memcmp(s.frame,c.frame,152)&&s.epoch==(ops[i]==MB_OP_FRAME?1u:2u)&&s.sequence==(ops[i]==MB_OP_FRAME?1u:0u),"translated_operation_has_exact_native_state_effect");cleanup(&a);
 }
 reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_command good=command(1,41);unsigned calls=ioctl_calls;
 for(int which=0;which<12;which++){struct mb_command c=good;enum mb_operation op=MB_OP_RESTORE;int expected=EINVAL;
  if(which==0)op=MB_OP_NONE;if(which==1)op=(enum mb_operation)99;if(which==2)c.injected=2;if(which==3){op=MB_OP_FRAME;c.injected=0;}if(which==4){op=MB_OP_ROTATE;c.injected=0;}if(which==5)c.epoch=0;if(which==6)c.sequence=0;
  if(which==7){c.instance[0]^=1;expected=ESTALE;}if(which==8){c.instance[1]^=1;expected=ESTALE;}if(which==9){c.run_id++;expected=ESTALE;}if(which==10){c.run_id=0;expected=ESTALE;}if(which==11){c.instance[0]=c.instance[1]=0;expected=ESTALE;}
  struct mb_backend_reply r=b.submit(b.context,op,&c);ck(r.outcome==MB_OUTCOME_REJECTED&&r.error==expected&&ioctl_calls==calls,"invalid_local_command_never_invokes_ioctl");
 }
 struct mb_backend_reply r=b.submit(b.context,MB_OP_RESTORE,NULL);ck(r.outcome==MB_OUTCOME_REJECTED&&r.error==EINVAL&&ioctl_calls==calls,"null_command_rejected_locally");r=b.submit(NULL,MB_OP_RESTORE,&good);ck(r.outcome==MB_OUTCOME_REJECTED&&r.error==EINVAL&&ioctl_calls==calls,"null_context_rejected_locally");
 /* Local validation does not claim the current generation: the actual driver
  * supplies the definitive stale rejection for a nonzero but old/new epoch. */
 good.epoch++;r=b.submit(b.context,MB_OP_RESTORE,&good);ck(r.outcome==MB_OUTCOME_REJECTED&&r.error==ESTALE&&ioctl_calls==calls+1&&mixer_epoch==1,"actual_kernel_stale_generation_definitive_rejection");good.epoch--;fail_from=from_calls+1;r=b.submit(b.context,MB_OP_RESTORE,&good);fail_from=0;ck(r.outcome==MB_OUTCOME_REJECTED&&r.error==EFAULT&&mixer_epoch==1,"actual_kernel_partial_input_definitive_rejection");cleanup(&a);
}
static void syscall_outcomes(void){
 const int known[]={EBADF,EPERM,EOPNOTSUPP,EFAULT,EINVAL,ESTALE,EACCES,EOVERFLOW,ENOTTY};
 for(unsigned i=0;i<sizeof known/sizeof *known;i++){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_command c=command(1,66);force_ioctl=true;forced_native=false;ioctl_reply=(struct mba_sys_result){-1,known[i]};unsigned calls=ioctl_calls;struct mb_backend_reply r=b.submit(b.context,MB_OP_RESTORE,&c);ck(r.outcome==MB_OUTCOME_REJECTED&&r.error==known[i]&&ioctl_calls==calls+1&&close_calls==0&&mixer_epoch==1,"pinned_precommit_errno_classifies_exact_rejection");cleanup(&a);}
 struct mba_sys_result unknown[]={{-1,EINTR},{-1,EIO},{-1,ETIMEDOUT},{-1,EBUSY},{-1,ERANGE},{-1,0},{-1,-1},{1,0},{-2,EFAULT}};int errors[]={EINTR,EIO,ETIMEDOUT,EBUSY,ERANGE,EPROTO,EPROTO,EPROTO,EPROTO};
 for(unsigned apply=0;apply<2;apply++)for(unsigned i=0;i<sizeof unknown/sizeof *unknown;i++){reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_command c=command(1,66);force_ioctl=true;forced_native=apply;ioctl_reply=unknown[i];unsigned calls=ioctl_calls;struct mb_backend_reply r=b.submit(b.context,MB_OP_RESTORE,&c);ck(r.outcome==MB_OUTCOME_UNKNOWN&&r.error==errors[i]&&ioctl_calls==calls+1&&close_calls==0&&mixer_epoch==(apply?2u:1u),"unrecognized_reply_unknown_before_or_after_actual_commit");cleanup(&a);}
}
static void descriptor_lifecycle(void){
 reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);raw_close(3);fd_table[3]=&file_other;struct mb_command c=command(0,0);struct mb_backend_reply r=b.submit(b.context,MB_OP_RESTORE,&c);ck(r.outcome==MB_OUTCOME_COMMITTED&&last_ioctl_fd==8&&mixer_owner==&file_owner&&fd_table[3]==&file_other,"original_number_reuse_cannot_redirect_mutation");struct mb_state before=kernel_state();watch_consumed=&a;struct mba_close_report report;int rc=mba_dispose(&a,&report);ck(!rc&&!a&&report.attempted&&report.call.result==0&&close_saw_consumed&&close_calls==1&&mixer_owner==NULL&&mixer_fenced,"explicit_final_duplicate_close_orphans_without_unfencing");ck(!has_inject[2]&&mixer_epoch==before.epoch&&mixer_sequence==before.sequence,"dispose_does_not_restore_or_clear_selected_overlay");unsigned closes=close_calls;ck(!mba_dispose(&a,&report)&&!report.attempted&&close_calls==closes,"repeated_consumed_dispose_never_retries");cleanup(&a);
 for(unsigned effect=0;effect<2;effect++){reset(1);a=attach();force_close=true;close_effect=effect;close_reply=(struct mba_sys_result){-1,EINTR};watch_consumed=&a;int result=mba_dispose(&a,&report);ck(!result&&!a&&report.attempted&&report.call.result==-1&&report.call.error==EINTR&&close_saw_consumed&&close_calls==1,"uncertain_close_retains_raw_result_and_consumes_handle");ck(!mba_dispose(&a,&report)&&!report.attempted&&close_calls==1,"uncertain_close_never_retried_by_dispose");cleanup(&a);}
 reset(1);a=attach();closes=close_calls;ck(mba_dispose(&a,NULL)==EINVAL&&a&&close_calls==closes,"missing_close_report_preserves_live_handle");memset(&report,0xA5,sizeof report);ck(mba_dispose(NULL,&report)==EINVAL&&!report.attempted&&!report.call.result&&!report.call.error&&close_calls==closes,"missing_handle_slot_zeroes_close_report");struct mb_backend missing;memset(&missing,0xA5,sizeof missing);ck(mba_get_backend(NULL,&missing)==EINVAL&&bytes_zero(&missing,sizeof missing),"missing_adapter_zeroes_backend_output");ck(mba_get_backend(a,NULL)==EINVAL,"missing_backend_output_rejected");cleanup(&a);
}
static void broker_integration(void){
 for(unsigned committed=0;committed<2;committed++){
  reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_state expected=kernel_state();struct mb_broker *broker=NULL;assert(!mb_create(&b,&expected,&broker));struct mb_restore_request request={.restore_id=1,.source=expected,.injected=0};memcpy(request.frame,az_bus_frame[2],152);struct mb_receipt receipt;force_ioctl=fault_mutator_only=true;forced_native=committed;ioctl_reply=(struct mba_sys_result){-1,committed?ETIMEDOUT:EINTR};unsigned calls=ioctl_calls;int rc=mb_restore(broker,&request,&receipt);
  ck(!rc&&receipt.outcome==MB_OUTCOME_UNKNOWN&&receipt.operation==MB_OP_RESTORE&&receipt.command.injected==0&&ioctl_calls==calls+2&&close_calls==0,"actual_broker_adapter_retains_unknown_restore");
  /* One fresh pre-submit STATE is unaffected by the mutator-only result fault.
   * The wrapper deliberately returns uncertainty either before or after the
   * real mutation. Neither variant schedules a retry or delayed command. */
  force_ioctl=false;struct mb_status st;assert(!mb_inspect(broker,&st));ck(st.unresolved&&!st.admission_open&&st.next_restore_id==2,"broker_unknown_closes_and_consumes_restore_identity");unsigned before=ioctl_calls;struct mb_receipt duplicate;ck(!mb_restore(broker,&request,&duplicate)&&duplicate.outcome==MB_OUTCOME_UNKNOWN&&ioctl_calls==before,"broker_unknown_duplicate_does_not_query_adapter");
  rc=mb_reconcile(broker,&receipt);assert(!mb_inspect(broker,&st));ck(!rc&&receipt.outcome==(committed?MB_OUTCOME_COMMITTED:MB_OUTCOME_UNKNOWN)&&st.unresolved==!committed&&!st.admission_open,"broker_adapter_only_exact_positive_state_reconciles");
  if(committed){ck(st.expected.flags==23&&!st.grant_issued&&st.expected.epoch==2,"broker_positive_idle_reconcile_remains_nonproducer");}else{ck(mb_rotate(broker,request.frame,&receipt)==MB_ERR_BUSY&&close_calls==0,"unknown_before_commit_cannot_rotate_or_auto_dispose");}
  before=ioctl_calls;unsigned closes=close_calls;mb_destroy(broker);ck(ioctl_calls==before&&close_calls==closes,"broker_destroy_does_not_dispose_adapter");cleanup(&a);
 }
 reset(1);struct mba_adapter *a=attach();struct mb_backend b=backend(a);struct mb_state expected=kernel_state();struct mb_broker *broker=NULL;assert(!mb_create(&b,&expected,&broker));struct mb_restore_request r={.restore_id=1,.source=expected,.injected=0};memcpy(r.frame,az_bus_frame[2],152);r.frame[151]^=1;struct mb_receipt out;ck(!mb_restore(broker,&r,&out)&&out.outcome==MB_OUTCOME_REJECTED&&out.backend_error==EINVAL,"broker_adapter_actual_idle_rejection_is_not_unknown");unsigned calls=ioctl_calls;ck(!mb_restore(broker,&r,&out)&&out.outcome==MB_OUTCOME_REJECTED&&ioctl_calls==calls,"broker_definitive_rejection_cached_without_second_ioctl");mb_destroy(broker);cleanup(&a);
}
static void oracles(void){mode_oracle();payload_oracle();success_errno_oracle();unknown_oracle();positive_oracle();rejected_oracle();state_zero_oracle();reserved_oracle();binding_oracle();attach_tail_oracle();fd_oracle();consume_oracle();local_oracle();}
int main(int argc,char **argv){alarm(30);const char *which=argc>1?argv[1]:"all";
 if(!strcmp(which,"all")){attachment();state_validation();wire_and_local_validation();syscall_outcomes();descriptor_lifecycle();broker_integration();oracles();}
 else if(!strcmp(which,"attach"))attachment();else if(!strcmp(which,"state"))state_validation();else if(!strcmp(which,"wire"))wire_and_local_validation();else if(!strcmp(which,"outcomes"))syscall_outcomes();else if(!strcmp(which,"lifecycle"))descriptor_lifecycle();else if(!strcmp(which,"broker"))broker_integration();
 else if(!strcmp(which,"mode"))mode_oracle();else if(!strcmp(which,"payload"))payload_oracle();else if(!strcmp(which,"success-errno"))success_errno_oracle();else if(!strcmp(which,"unknown"))unknown_oracle();else if(!strcmp(which,"positive"))positive_oracle();else if(!strcmp(which,"rejected"))rejected_oracle();else if(!strcmp(which,"state-zero"))state_zero_oracle();else if(!strcmp(which,"reserved"))reserved_oracle();else if(!strcmp(which,"binding"))binding_oracle();else if(!strcmp(which,"attach-tail"))attach_tail_oracle();else if(!strcmp(which,"fd"))fd_oracle();else if(!strcmp(which,"consume"))consume_oracle();else if(!strcmp(which,"local"))local_oracle();else return 2;
 printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
