/* SPDX-License-Identifier: GPL-2.0-only */
/* Focused successor: exact broker and frozen RESTORE kernel methods with
 * owned-memory usercopy/pthread shims. No device, socket, daemon or stop action.
 * All boundary seeds below are fixture-only; production exposes no setters. */
#include "kernel-shim.inc"
#include "candidate.inc"
#include "broker-under-test.inc"

static unsigned checks,failures;
static void ck(bool ok,const char *name){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",name);}}
static void fill(uint8_t *b,unsigned seed){for(unsigned i=0;i<152;i++)b[i]=(uint8_t)(seed+i*23);}
static void tls_clean(void){from_calls=to_calls=fail_from=fail_to=pause_from=pause_before_lock=pause_after_unlock=0;admin=1;allocation_failure=interrupted=0;}
static bool eqstate(const struct mb_state *a,const struct mb_state *b){return a->instance[0]==b->instance[0]&&a->instance[1]==b->instance[1]&&a->run_id==b->run_id&&a->epoch==b->epoch&&a->sequence==b->sequence&&a->flags==b->flags&&a->crc_len==b->crc_len&&!memcmp(a->frame,b->frame,152);}
static bool eqrestore(const struct mb_restore_request *a,const struct mb_restore_request *b){return a->restore_id==b->restore_id&&eqstate(&a->source,&b->source)&&a->injected==b->injected&&!memcmp(a->frame,b->frame,152);}
static bool eqcommand(const struct mb_command *a,const struct mb_command *b){return a->instance[0]==b->instance[0]&&a->instance[1]==b->instance[1]&&a->run_id==b->run_id&&a->epoch==b->epoch&&a->sequence==b->sequence&&a->injected==b->injected&&!memcmp(a->frame,b->frame,152);}
static bool eqreceipt(const struct mb_receipt *a,const struct mb_receipt *b){return a->outcome==b->outcome&&a->operation==b->operation&&a->backend_error==b->backend_error&&eqrestore(&a->restore,&b->restore)&&eqcommand(&a->command,&b->command)&&eqstate(&a->expected_after,&b->expected_after)&&a->request.grant.instance[0]==b->request.grant.instance[0]&&a->request.grant.instance[1]==b->request.grant.instance[1]&&a->request.grant.run_id==b->request.grant.run_id&&a->request.grant.epoch==b->request.grant.epoch&&a->request.grant.producer_id==b->request.grant.producer_id&&a->request.request_id==b->request.request_id&&!memcmp(a->request.frame,b->request.frame,152);}
static bool empty_receipt(const struct mb_receipt *r){const struct mb_receipt zero={0};return eqreceipt(r,&zero);}
static void mutate_state(struct mb_state *s,int which){switch(which){case 1:s->instance[0]^=1;break;case 2:s->instance[1]^=1;break;case 3:s->run_id++;break;case 4:s->epoch++;break;case 5:s->sequence++;break;case 6:s->flags^=4;break;case 7:s->crc_len--;break;case 8:s->frame[151]^=1;break;case 9:s->flags^=8;break;case 10:s->frame[96]^=1;break;case 11:s->frame[97]^=1;break;default:break;}}
struct fixture {
 struct file owner,other;struct mb_broker *broker;
 unsigned snapshot_calls,submit_calls,native_calls;int snapshot_error,snapshot_mutation,mode;
 struct mb_command delayed;enum mb_operation delayed_op;struct mb_backend_reply malformed;
};
enum {NORMAL,LOST_ACK,PENDING,REJECT,MALFORMED,HOLD};
/* Semantic-to-wire translation is explicit; an unknown op never defaults to
 * ROTATE. Native STATE fields omitted from mb_state are checked on receipt. */
static long native_submit(struct fixture *f,enum mb_operation op,const struct mb_command *c){
 struct az_overlay_command h={.version=2,.reserved=0,.instance={c->instance[0],c->instance[1]},.run_id=c->run_id,.epoch=c->epoch,.sequence=c->sequence};
 f->native_calls++;
 switch(op){
 case MB_OP_FRAME:case MB_OP_ROTATE:{struct az_overlay_frame k={.header=h};memcpy(k.frame,c->frame,152);return ctrl_ioctl(&f->owner,op==MB_OP_FRAME?AZ_OVERLAY_FRAME:AZ_OVERLAY_ROTATE,(unsigned long)&k);}
 case MB_OP_RESTORE:{struct az_overlay_restore k={.header=h,.injected=c->injected,.reserved=0};memcpy(k.frame,c->frame,152);return ctrl_ioctl(&f->owner,AZ_OVERLAY_RESTORE,(unsigned long)&k);}
 default:return -EINVAL;
 }
}
static int backend_snapshot(void *v,struct mb_state *out){
 struct fixture *f=v;struct az_overlay_state k={0};f->snapshot_calls++;
 if(f->snapshot_error)return f->snapshot_error;
 long rc=ctrl_ioctl(&f->owner,AZ_OVERLAY_STATE,(unsigned long)&k);if(rc)return(int)rc;
 if(k.version!=2||k.reserved)return-EPROTO;
 *out=(struct mb_state){.instance={k.instance[0],k.instance[1]},.run_id=k.run_id,.epoch=k.epoch,.sequence=k.sequence,.flags=k.flags,.crc_len=k.crc_len};memcpy(out->frame,k.frame,152);mutate_state(out,f->snapshot_mutation);return 0;
}
static struct mb_backend_reply backend_submit(void *v,enum mb_operation op,const struct mb_command *c){
 struct fixture *f=v;f->submit_calls++;
 if(f->mode==PENDING){f->delayed=*c;f->delayed_op=op;return(struct mb_backend_reply){MB_OUTCOME_UNKNOWN,ETIMEDOUT};}
 if(f->mode==REJECT)return(struct mb_backend_reply){MB_OUTCOME_REJECTED,EAGAIN};
 if(f->mode==HOLD)hold();
 long rc=native_submit(f,op,c);
 if(f->mode==MALFORMED)return f->malformed;
 if(rc)return(struct mb_backend_reply){MB_OUTCOME_REJECTED,(int)-rc};
 return(struct mb_backend_reply){f->mode==LOST_ACK?MB_OUTCOME_UNKNOWN:MB_OUTCOME_COMMITTED,f->mode==LOST_ACK?ETIMEDOUT:0};
}
static void reset(struct fixture *f,unsigned injected){
 memset(f,0,sizeof *f);tls_clean();memset(inject_pending,0,sizeof inject_pending);memset(has_inject,0,sizeof has_inject);int crc[4]={28,38,96,38};memcpy(spi_crc_len,crc,sizeof crc);n_crc=4;mixer_fenced=false;mixer_owner=NULL;mixer_run=mixer_epoch=mixer_sequence=0;allow_overlay_ownership=true;memset(mixer_instance,0,sizeof mixer_instance);random_wait_result=0;assert(!mixer_instance_init());testmode_held=0;f->owner.f_mode=f->other.f_mode=FMODE_WRITE;
 pthread_mutex_lock(&gate.mu);gate.waiting=gate.resume=0;pthread_mutex_unlock(&gate.mu);
 if(injected){uint8_t packet[153]={2};fill(packet+1,13);assert(ctrl_write(&f->other,(char *)packet,sizeof packet,NULL)==153);}
 struct az_overlay_command c={.version=2,.instance={mixer_instance[0],mixer_instance[1]},.run_id=7};assert(!ctrl_ioctl(&f->owner,AZ_OVERLAY_CLAIM,(unsigned long)&c));
}
static struct mb_state state(struct fixture *f){struct mb_state s;assert(!backend_snapshot(f,&s));return s;}
static void bind(struct fixture *f){struct mb_state s=state(f);struct mb_backend b={f,backend_snapshot,backend_submit};assert(mb_create(&b,&s,&f->broker)==MB_OK);}
static void setup(struct fixture *f,unsigned injected){reset(f,injected);bind(f);}
static struct mb_status status(struct fixture *f){struct mb_status s;assert(!mb_inspect(f->broker,&s));return s;}
static struct mb_grant adopt(struct fixture *f){struct mb_state s=state(f);struct mb_grant g;assert(!mb_adopt(f->broker,s.frame,11,&g));return g;}
static struct mb_request request(struct mb_grant g,uint64_t id,unsigned seed){uint8_t b[152];fill(b,seed);struct mb_request r;assert(!mb_stamp(&g,id,b,&r));return r;}
static struct mb_restore_request restore_request(struct fixture *f,uint64_t id,unsigned injected,unsigned seed){struct mb_status s=status(f);struct mb_restore_request r={.restore_id=id,.source=s.expected,.injected=injected};if(injected)fill(r.frame,seed);else memcpy(r.frame,az_bus_frame[2],152);return r;}
static void done(struct fixture *f){unsigned submits=f->submit_calls,snapshots=f->snapshot_calls,natives=f->native_calls;mb_destroy(f->broker);f->broker=NULL;assert(f->submit_calls==submits&&f->snapshot_calls==snapshots&&f->native_calls==natives);assert(mixer_owner==&f->owner);assert(!ctrl_release(NULL,&f->owner));}

/* Single named ck per selectable strict control. Setup/cleanup must succeed;
 * expected failures are functional oracles, never hangs or sanitizer reports. */
static void mode_oracle(void){struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,0,0);struct mb_receipt out;int rc=mb_restore(f.broker,&r,&out);struct mb_state actual=state(&f);struct mb_status s=status(&f);done(&f);ck(!rc&&out.outcome==MB_OUTCOME_COMMITTED&&out.expected_after.flags==23&&actual.flags==23&&eqstate(&actual,&out.expected_after)&&!s.admission_open,"restore_mode_prediction_matches_kernel");}
static void cached_oracle(void){struct fixture f;reset(&f,1);mixer_epoch=UINT64_MAX-1;bind(&f);f.broker->state.next_restore_id=UINT64_MAX-1;struct mb_restore_request r=restore_request(&f,UINT64_MAX-1,0,0);struct mb_receipt first,out;assert(!mb_restore(f.broker,&r,&first));unsigned submits=f.submit_calls,snapshots=f.snapshot_calls;int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);done(&f);ck(!rc&&eqreceipt(&first,&out)&&f.submit_calls==submits&&f.snapshot_calls==snapshots&&s.next_restore_id==UINT64_MAX&&!s.admission_open,"restore_duplicate_cache_precedes_stale_source_and_exhaustion");}
static void conflict_mode_oracle(void){struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,0,0);struct mb_receipt first,out;assert(!mb_restore(f.broker,&r,&first));r.injected=1;int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);done(&f);ck(rc==MB_ERR_CONFLICT&&f.submit_calls==1&&eqreceipt(&first,&s.last)&&empty_receipt(&out),"restore_duplicate_compares_explicit_mode");}
static void conflict_source_oracle(void){struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,0,0);struct mb_receipt first,out;assert(!mb_restore(f.broker,&r,&first));r.source.frame[151]^=1;int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);done(&f);ck(rc==MB_ERR_CONFLICT&&f.submit_calls==1&&eqreceipt(&first,&s.last)&&empty_receipt(&out),"restore_duplicate_compares_full_source");}
static void conflict_tail_oracle(void){struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,1,35);struct mb_receipt first,out;assert(!mb_restore(f.broker,&r,&first));r.frame[151]^=1;int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);done(&f);ck(rc==MB_ERR_CONFLICT&&f.submit_calls==1&&eqreceipt(&first,&s.last),"restore_duplicate_compares_full_target");}
static void lower_oracle(void){struct fixture f;setup(&f,1);f.mode=PENDING;struct mb_restore_request r=restore_request(&f,1,0,0);struct mb_receipt first,out;assert(!mb_restore(f.broker,&r,&first));int rc=mb_reconcile(f.broker,&out);struct mb_status s=status(&f);done(&f);ck(!rc&&out.outcome==MB_OUTCOME_UNKNOWN&&s.unresolved&&!s.admission_open&&f.submit_calls==1&&f.native_calls==0&&eqrestore(&first.restore,&out.restore),"restore_lower_state_is_not_cancellation");}
static void idle_adopt_oracle(void){struct fixture f;setup(&f,0);struct mb_state baseline=state(&f);struct mb_grant g;int rc=mb_adopt(f.broker,baseline.frame,11,&g);struct mb_status s=status(&f);done(&f);ck(rc==MB_ERR_STATE&&!s.admission_open&&!s.grant_issued&&f.submit_calls==0,"idle_owned_state_cannot_open_producer");}
static void history_oracle(void){struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,1,45);struct mb_receipt out;assert(!mb_restore(f.broker,&r,&out));assert(!mb_rotate(f.broker,r.frame,&out));struct mb_receipt previous=out;r=restore_request(&f,1,0,0);int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);done(&f);ck(rc==MB_ERR_ID&&f.submit_calls==2&&s.next_restore_id==2&&eqreceipt(&previous,&s.last),"restore_identity_not_reset_by_rotate");}
static void unresolved_oracle(void){struct fixture f;setup(&f,1);struct mb_grant g=adopt(&f);struct mb_request p=request(g,1,87);struct mb_receipt first,out;f.mode=PENDING;assert(!mb_submit(f.broker,&p,&first));struct mb_restore_request r=restore_request(&f,1,0,0);int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);done(&f);ck(rc==MB_ERR_BUSY&&f.submit_calls==1&&s.unresolved&&s.next_restore_id==1&&eqreceipt(&first,&s.last),"restore_cannot_replace_unresolved_frame");}
static void snapshot_oracle(void){struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,0,0);f.snapshot_mutation=8;struct mb_receipt out;int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);f.snapshot_mutation=0;done(&f);ck(rc==MB_ERR_STATE&&f.submit_calls==0&&s.next_restore_id==1&&empty_receipt(&out),"restore_requires_fresh_exact_source_snapshot");}
static void source_oracle(void){struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,0,0);r.source.epoch++;unsigned snapshots=f.snapshot_calls;struct mb_receipt out;int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);done(&f);ck(rc==MB_ERR_STALE&&f.submit_calls==0&&f.snapshot_calls==snapshots&&s.next_restore_id==1&&empty_receipt(&out),"restore_source_epoch_bound_before_backend");}
static void open_oracle(void){struct fixture f;setup(&f,1);(void)adopt(&f);struct mb_restore_request r=restore_request(&f,1,0,0);struct mb_receipt out;int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);done(&f);ck(rc==MB_ERR_CLOSED&&f.submit_calls==0&&s.next_restore_id==1&&empty_receipt(&out),"restore_requires_explicit_closed_admission");}
static void grant_oracle(void){struct fixture f;setup(&f,1);(void)adopt(&f);assert(!mb_quiesce(f.broker));struct mb_restore_request r=restore_request(&f,1,1,65);struct mb_receipt out;int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);done(&f);ck(!rc&&out.outcome==MB_OUTCOME_COMMITTED&&!s.admission_open&&!s.grant_issued&&!s.unresolved&&s.next_request_id==1&&s.next_restore_id==2&&s.grant.epoch==0,"restore_commit_revokes_grant_without_opening");}
static void malformed_conflict_oracle(unsigned source){struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,1,44);struct mb_receipt first,out;assert(!mb_restore(f.broker,&r,&first));(void)adopt(&f);if(source)r.source.flags=0;else r.injected=2;unsigned snapshots=f.snapshot_calls;int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);done(&f);ck(rc==MB_ERR_CONFLICT&&!s.admission_open&&eqreceipt(&first,&s.last)&&f.submit_calls==1&&f.snapshot_calls==snapshots&&empty_receipt(&out),source?"retained_identity_invalid_source_conflict_closes":"retained_identity_invalid_mode_conflict_closes");}
static void alias_oracle(void){struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,1,93);struct mb_receipt out={.restore=r};int rc=mb_restore(f.broker,&out.restore,&out);struct mb_state actual=state(&f);done(&f);ck(!rc&&out.outcome==MB_OUTCOME_COMMITTED&&eqrestore(&r,&out.restore)&&!memcmp(actual.frame,r.frame,152),"restore_input_output_alias_copied_before_clear");}

static void baseline_and_modes(void){
 for(unsigned original=0;original<2;original++)for(unsigned target=0;target<2;target++){
  struct fixture f;setup(&f,original);struct mb_state before=state(&f);struct mb_restore_request r=restore_request(&f,1,target,77);struct mb_receipt out;int rc=mb_restore(f.broker,&r,&out);struct mb_state after=state(&f);struct mb_status s=status(&f);
  ck(!rc&&out.operation==MB_OP_RESTORE&&out.outcome==MB_OUTCOME_COMMITTED&&eqrestore(&out.restore,&r),"restore_receipt_binds_exact_request");
  ck(out.command.epoch==before.epoch&&out.command.sequence==before.sequence+1&&out.command.injected==target&&!memcmp(out.command.frame,r.frame,152),"restore_command_translates_source_and_target");
  ck(after.flags==(target?31u:23u)&&after.epoch==before.epoch+1&&after.sequence==0&&!memcmp(after.frame,r.frame,152)&&eqstate(&after,&out.expected_after),"restore_actual_selected_state_matches_prediction");
  ck(after.instance[0]==before.instance[0]&&after.instance[1]==before.instance[1]&&after.run_id==before.run_id&&after.crc_len==96&&mixer_owner==&f.owner&&mixer_fenced,"restore_preserves_owner_instance_run_crc_fence");
  ck(!s.admission_open&&!s.grant_issued&&!s.unresolved&&s.next_restore_id==2,"restore_success_is_closed_and_identity_consumed");
  if(target){struct mb_grant g;ck(!mb_adopt(f.broker,after.frame,12,&g)&&g.epoch==after.epoch,"injected_restore_requires_separate_checked_adoption");}else{struct mb_grant g;ck(mb_adopt(f.broker,after.frame,12,&g)==MB_ERR_STATE&&!status(&f).admission_open,"idle_restore_has_no_producer_admission");}
  done(&f);
 }
 /* Equal selected bytes are not enough: preserve explicit idle/injected mode. */
 struct fixture f;setup(&f,0);struct mb_restore_request r=restore_request(&f,1,1,0);memcpy(r.frame,az_bus_frame[2],152);struct mb_receipt out;ck(!mb_restore(f.broker,&r,&out)&&out.expected_after.flags==31&&has_inject[2],"idle_bytes_can_be_explicitly_injected");r=restore_request(&f,2,0,0);ck(!mb_restore(f.broker,&r,&out)&&out.expected_after.flags==23&&!has_inject[2]&&mixer_epoch==3,"same_bytes_restore_to_idle_still_rotates");
 uint8_t bytes[152];fill(bytes,56);ck(!mb_rotate(f.broker,bytes,&out)&&out.expected_after.flags==31&&has_inject[2]&&out.command.injected==1,"rotate_from_idle_predicts_injected");struct mb_grant g=adopt(&f);struct mb_request p=request(g,1,88);ck(!mb_submit(f.broker,&p,&out)&&out.expected_after.flags==31&&out.command.injected==1&&mixer_sequence==1,"frame_after_idle_rotation_preserves_injected_prediction");done(&f);
}
static void identity_and_drift(void){
 struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,1,42);struct mb_receipt out;
 for(int field=1;field<=11;field++){struct mb_restore_request bad=r;mutate_state(&bad.source,field);unsigned snapshots=f.snapshot_calls;int rc=mb_restore(f.broker,&bad,&out);ck((rc==MB_ERR_STALE||rc==MB_ERR_INVALID)&&f.snapshot_calls==snapshots&&f.submit_calls==0&&status(&f).next_restore_id==1&&empty_receipt(&out),"restore_full_source_mismatch_no_attempt");}
 struct mb_restore_request bad=r;bad.restore_id=0;ck(mb_restore(f.broker,&bad,&out)==MB_ERR_INVALID&&empty_receipt(&out),"restore_zero_identity_invalid");bad=r;bad.injected=2;ck(mb_restore(f.broker,&bad,&out)==MB_ERR_INVALID&&empty_receipt(&out),"restore_nonboolean_mode_invalid");bad=r;bad.restore_id=2;ck(mb_restore(f.broker,&bad,&out)==MB_ERR_ID&&status(&f).next_restore_id==1,"restore_gap_does_not_consume_identity");
 for(int field=1;field<=11;field++){f.snapshot_mutation=field;int rc=mb_restore(f.broker,&r,&out);ck(rc==MB_ERR_STATE&&f.submit_calls==0&&status(&f).next_restore_id==1&&empty_receipt(&out),"restore_fresh_snapshot_checks_full_state");}f.snapshot_mutation=0;
 f.snapshot_error=-EIO;ck(mb_restore(f.broker,&r,&out)==MB_ERR_BACKEND&&f.submit_calls==0&&status(&f).next_restore_id==1,"restore_snapshot_error_does_not_consume_identity");f.snapshot_error=0;
 assert(!mb_restore(f.broker,&r,&out));struct mb_receipt first=out;
 for(int field=1;field<=11;field++){bad=r;mutate_state(&bad.source,field);unsigned snapshots=f.snapshot_calls;int rc=mb_restore(f.broker,&bad,&out);struct mb_status s=status(&f);ck(rc==MB_ERR_CONFLICT&&f.snapshot_calls==snapshots&&f.submit_calls==1&&eqreceipt(&first,&s.last)&&empty_receipt(&out),"restore_duplicate_source_conflict_preserves_receipt");}
 const unsigned offsets[]={0,96,97,151};for(unsigned i=0;i<sizeof offsets/sizeof *offsets;i++){bad=r;bad.frame[offsets[i]]^=1;int rc=mb_restore(f.broker,&bad,&out);struct mb_status s=status(&f);ck(rc==MB_ERR_CONFLICT&&f.submit_calls==1&&eqreceipt(&first,&s.last),"restore_duplicate_compares_target_crc_and_tail");}
 bad=r;bad.restore_id=2;ck(mb_restore(f.broker,&bad,&out)==MB_ERR_STALE&&status(&f).next_restore_id==2&&f.submit_calls==1,"fresh_identity_cannot_retag_stale_source");
 struct mb_grant g=adopt(&f);struct mb_request p=request(g,1,99);assert(!mb_submit(f.broker,&p,&out));assert(!mb_quiesce(f.broker));struct mb_receipt previous=out;bad=restore_request(&f,1,0,0);int rc=mb_restore(f.broker,&bad,&out);struct mb_status s=status(&f);ck(rc==MB_ERR_ID&&f.submit_calls==2&&s.next_restore_id==2&&eqreceipt(&previous,&s.last),"frame_does_not_reset_restore_identity");bad=restore_request(&f,2,0,0);ck(!mb_restore(f.broker,&bad,&out)&&out.outcome==MB_OUTCOME_COMMITTED&&status(&f).next_restore_id==3,"next_identity_after_frame_can_restore");done(&f);
}
static void reopened_cached_receipt(void){
 struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,1,54);struct mb_receipt first,out;assert(!mb_restore(f.broker,&r,&first));(void)adopt(&f);unsigned snapshots=f.snapshot_calls;
 ck(!mb_restore(f.broker,&r,&out)&&eqreceipt(&first,&out)&&status(&f).admission_open&&f.snapshot_calls==snapshots&&f.submit_calls==1,"historical_restore_cache_preserves_explicitly_open_grant");
 struct mb_restore_request conflict=r;conflict.injected=2;int rc=mb_restore(f.broker,&conflict,&out);struct mb_status s=status(&f);ck(rc==MB_ERR_CONFLICT&&!s.admission_open&&eqreceipt(&first,&s.last)&&f.submit_calls==1,"malformed_retained_identity_conflict_closes_open_grant");
 ck(!mb_restore(f.broker,&r,&out)&&eqreceipt(&first,&out)&&!status(&f).admission_open&&f.snapshot_calls==snapshots&&f.submit_calls==1,"historical_restore_cache_after_conflict_never_reopens");done(&f);
}
static void unknown_paths(void){
 for(unsigned target=0;target<2;target++)for(int mode=LOST_ACK;mode<=PENDING;mode++){
  struct fixture f;setup(&f,1);struct mb_grant g=adopt(&f);assert(!mb_quiesce(f.broker));struct mb_restore_request r=restore_request(&f,1,target,63);struct mb_receipt out;f.mode=mode;
  ck(!mb_restore(f.broker,&r,&out)&&out.outcome==MB_OUTCOME_UNKNOWN&&status(&f).unresolved&&!status(&f).admission_open&&status(&f).next_restore_id==2,"unknown_restore_retains_exact_attempt_and_closes");struct mb_receipt first=out;unsigned snapshots=f.snapshot_calls;
  ck(!mb_restore(f.broker,&r,&out)&&eqreceipt(&first,&out)&&f.submit_calls==1&&f.snapshot_calls==snapshots,"unknown_restore_duplicate_does_not_query_or_retry");
  struct mb_restore_request next=r;next.restore_id=2;ck(mb_restore(f.broker,&next,&out)==MB_ERR_BUSY&&f.submit_calls==1&&status(&f).next_restore_id==2,"unknown_restore_blocks_fresh_restore");
  ck(mb_rotate(f.broker,r.frame,&out)==MB_ERR_BUSY&&f.submit_calls==1,"unknown_restore_blocks_rotate");struct mb_request p=request(g,1,91);ck(mb_submit(f.broker,&p,&out)==MB_ERR_BUSY&&f.submit_calls==1,"unknown_restore_blocks_frame");struct mb_grant fresh;ck(mb_adopt(f.broker,r.frame,12,&fresh)==MB_ERR_BUSY,"unknown_restore_blocks_adoption");
  if(mode==PENDING){ck(!mb_reconcile(f.broker,&out)&&out.outcome==MB_OUTCOME_UNKNOWN&&status(&f).unresolved&&f.native_calls==0,"restore_pending_old_state_remains_unknown");assert(!native_submit(&f,f.delayed_op,&f.delayed));}
  for(int field=1;field<=11;field++){f.snapshot_mutation=field;int rc=mb_reconcile(f.broker,&out);struct mb_status s=status(&f);ck(!rc&&out.outcome==MB_OUTCOME_UNKNOWN&&s.unresolved&&eqrestore(&r,&out.restore),"restore_positive_reconcile_requires_every_state_field");}f.snapshot_mutation=0;
  f.snapshot_error=-EIO;ck(mb_reconcile(f.broker,&out)==MB_ERR_BACKEND&&out.outcome==MB_OUTCOME_UNKNOWN&&status(&f).unresolved,"restore_reconcile_snapshot_error_retains_unknown");f.snapshot_error=0;
  ck(!mb_reconcile(f.broker,&out)&&out.outcome==MB_OUTCOME_COMMITTED&&!status(&f).unresolved&&!status(&f).admission_open&&!status(&f).grant_issued&&f.submit_calls==1,"restore_exact_positive_reconcile_never_replays_or_opens");snapshots=f.snapshot_calls;first=out;ck(!mb_restore(f.broker,&r,&out)&&eqreceipt(&first,&out)&&f.snapshot_calls==snapshots&&f.submit_calls==1,"reconciled_restore_duplicate_is_historical_cache");
  if(target)ck(!mb_adopt(f.broker,r.frame,12,&fresh)&&fresh.epoch==2,"reconciled_injected_restore_separate_adoption");else ck(mb_adopt(f.broker,r.frame,12,&fresh)==MB_ERR_STATE&&!status(&f).admission_open,"reconciled_idle_restore_stays_nonproducer");done(&f);
 }
 /* Prior UNKNOWN rotation also cannot be overwritten by a restore request. */
 struct fixture f;setup(&f,1);f.mode=PENDING;uint8_t frame[152];fill(frame,41);struct mb_receipt out;assert(!mb_rotate(f.broker,frame,&out));struct mb_receipt first=out;struct mb_restore_request r=restore_request(&f,1,0,0);int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);ck(rc==MB_ERR_BUSY&&eqreceipt(&first,&s.last)&&s.next_restore_id==1&&f.submit_calls==1,"restore_cannot_replace_unresolved_rotate");done(&f);
}
static void rejected_and_malformed(void){
 struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,1,0,0);r.frame[151]^=1;struct mb_receipt out;struct mb_state before=state(&f);
 ck(!mb_restore(f.broker,&r,&out)&&out.outcome==MB_OUTCOME_REJECTED&&out.backend_error==EINVAL&&!status(&f).unresolved&&!status(&f).admission_open&&status(&f).next_restore_id==2,"actual_kernel_idle_mismatch_is_known_rejection");struct mb_state after=state(&f);ck(eqstate(&before,&after),"kernel_rejected_restore_keeps_full_state");struct mb_receipt first=out;unsigned snapshots=f.snapshot_calls;
 ck(!mb_restore(f.broker,&r,&out)&&eqreceipt(&first,&out)&&f.snapshot_calls==snapshots&&f.submit_calls==1,"rejected_restore_duplicate_cached_without_retry");r.frame[151]^=1;ck(mb_restore(f.broker,&r,&out)==MB_ERR_CONFLICT&&f.submit_calls==1,"correcting_submitted_identity_requires_new_identity");r.restore_id=2;ck(!mb_restore(f.broker,&r,&out)&&out.outcome==MB_OUTCOME_COMMITTED&&status(&f).next_restore_id==3,"explicit_next_identity_after_rejection");done(&f);
 struct mb_backend_reply malformed[]={{MB_OUTCOME_COMMITTED,1},{MB_OUTCOME_REJECTED,0},{MB_OUTCOME_REJECTED,-1},{MB_OUTCOME_NONE,0},{(enum mb_outcome)99,0},{MB_OUTCOME_UNKNOWN,-1}};
 for(unsigned i=0;i<sizeof malformed/sizeof *malformed;i++){setup(&f,1);r=restore_request(&f,1,0,0);f.mode=MALFORMED;f.malformed=malformed[i];ck(!mb_restore(f.broker,&r,&out)&&out.outcome==MB_OUTCOME_UNKNOWN&&status(&f).unresolved&&mixer_epoch==2&&!has_inject[2],"malformed_reply_after_actual_restore_is_unknown");ck(!mb_reconcile(f.broker,&out)&&out.outcome==MB_OUTCOME_COMMITTED&&!status(&f).admission_open,"malformed_reply_needs_exact_positive_reconciliation");done(&f);}
}
static void capacity_boundaries(void){
 struct fixture f;setup(&f,1);struct mb_restore_request r=restore_request(&f,UINT64_MAX,0,0);struct mb_receipt out;ck(mb_restore(f.broker,&r,&out)==MB_ERR_OVERFLOW&&f.submit_calls==0&&status(&f).next_restore_id==1,"restore_max_identity_not_attemptable");done(&f);
 reset(&f,1);mixer_sequence=UINT64_MAX;bind(&f);r=restore_request(&f,1,0,0);ck(mb_restore(f.broker,&r,&out)==MB_ERR_OVERFLOW&&f.submit_calls==0&&status(&f).next_restore_id==1,"restore_kernel_sequence_exhaustion_no_attempt");done(&f);
 reset(&f,1);mixer_epoch=UINT64_MAX;bind(&f);r=restore_request(&f,1,0,0);ck(mb_restore(f.broker,&r,&out)==MB_ERR_OVERFLOW&&f.submit_calls==0,"restore_kernel_epoch_exhaustion_no_attempt");done(&f);
 reset(&f,1);mixer_epoch=UINT64_MAX-1;mixer_sequence=UINT64_MAX-1;bind(&f);f.broker->state.next_restore_id=UINT64_MAX-1;r=restore_request(&f,UINT64_MAX-1,0,0);ck(!mb_restore(f.broker,&r,&out)&&out.outcome==MB_OUTCOME_COMMITTED&&out.command.sequence==UINT64_MAX&&mixer_epoch==UINT64_MAX&&mixer_sequence==0&&status(&f).next_restore_id==UINT64_MAX,"restore_last_identity_and_kernel_counters_do_not_wrap");struct mb_receipt first=out;r=restore_request(&f,1,0,0);int rc=mb_restore(f.broker,&r,&out);struct mb_status s=status(&f);ck(rc==MB_ERR_OVERFLOW&&f.submit_calls==1&&eqreceipt(&first,&s.last),"exhausted_restore_space_refuses_fresh_identity");done(&f);
}
struct job{struct fixture *fixture;struct mb_restore_request request;struct mb_receipt receipt;int rc;atomic_bool started,finished;};
static void *thread_restore(void *v){struct job *j=v;tls_clean();atomic_store(&j->started,true);j->rc=mb_restore(j->fixture->broker,&j->request,&j->receipt);atomic_store(&j->finished,true);return NULL;}
static pthread_t start_job(struct job *j){pthread_t t;int rc=pthread_create(&t,NULL,thread_restore,j);if(rc){fprintf(stderr,"fixture pthread_create error %d\n",rc);exit(2);}return t;}
static void join_job(pthread_t t){int rc=pthread_join(t,NULL);if(rc){fprintf(stderr,"fixture pthread_join error %d\n",rc);exit(2);}}
static void reset_gate(void){pthread_mutex_lock(&gate.mu);gate.waiting=gate.resume=0;pthread_mutex_unlock(&gate.mu);}
static void concurrency(void){
 for(unsigned conflict=0;conflict<2;conflict++){
  struct fixture f;setup(&f,1);f.mode=HOLD;struct job a={.fixture=&f,.request=restore_request(&f,1,0,0)},b={.fixture=&f,.request=a.request};if(conflict)b.request.injected=1;reset_gate();pthread_t ta=start_job(&a);wait_held();pthread_t tb=start_job(&b);while(!atomic_load(&b.started))sched_yield();
  int locked=pthread_mutex_trylock(&f.broker->mutex);if(!locked)pthread_mutex_unlock(&f.broker->mutex);bool serialized=locked==EBUSY&&!atomic_load(&a.finished)&&!atomic_load(&b.finished);let_go();join_job(ta);join_job(tb);struct mb_status s=status(&f);done(&f);
  ck(serialized,"restore_callbacks_share_broker_serialization");ck(a.rc==MB_OK&&a.receipt.outcome==MB_OUTCOME_COMMITTED&&f.submit_calls==1&&f.native_calls==1,"concurrent_restore_one_actual_kernel_write");
  ck(conflict?(b.rc==MB_ERR_CONFLICT&&eqreceipt(&a.receipt,&s.last)):(b.rc==MB_OK&&eqreceipt(&a.receipt,&b.receipt)),"serialized_duplicate_or_conflict_preserves_first_receipt");
 }
 /* Mutation starts only after the callback barrier proves input was privately
  * copied. It races no read of the caller request in the intended contract. */
 struct fixture f;setup(&f,1);f.mode=HOLD;struct job a={.fixture=&f,.request=restore_request(&f,1,1,73)};struct mb_restore_request original=a.request;reset_gate();pthread_t t=start_job(&a);wait_held();a.request.restore_id=91;a.request.source.epoch=92;a.request.injected=0;memset(a.request.frame,0,152);let_go();join_job(t);struct mb_state actual=state(&f);done(&f);ck(a.rc==MB_OK&&a.receipt.outcome==MB_OUTCOME_COMMITTED&&eqrestore(&original,&a.receipt.restore)&&!memcmp(actual.frame,original.frame,152)&&actual.flags==31,"restore_copies_mutable_caller_value_before_backend");
}
static void all_oracles(void){mode_oracle();cached_oracle();conflict_mode_oracle();conflict_source_oracle();conflict_tail_oracle();lower_oracle();idle_adopt_oracle();history_oracle();unresolved_oracle();snapshot_oracle();source_oracle();open_oracle();grant_oracle();alias_oracle();malformed_conflict_oracle(0);malformed_conflict_oracle(1);}
int main(int argc,char **argv){alarm(30);const char *which=argc>1?argv[1]:"all";
 if(!strcmp(which,"all")){baseline_and_modes();identity_and_drift();reopened_cached_receipt();unknown_paths();rejected_and_malformed();capacity_boundaries();concurrency();all_oracles();}
 else if(!strcmp(which,"baseline"))baseline_and_modes();else if(!strcmp(which,"identity"))identity_and_drift();else if(!strcmp(which,"reopened"))reopened_cached_receipt();else if(!strcmp(which,"unknown"))unknown_paths();else if(!strcmp(which,"rejected"))rejected_and_malformed();else if(!strcmp(which,"capacity"))capacity_boundaries();else if(!strcmp(which,"concurrency"))concurrency();
 else if(!strcmp(which,"mode"))mode_oracle();else if(!strcmp(which,"cached"))cached_oracle();else if(!strcmp(which,"conflict-mode"))conflict_mode_oracle();else if(!strcmp(which,"conflict-source"))conflict_source_oracle();else if(!strcmp(which,"conflict-tail"))conflict_tail_oracle();else if(!strcmp(which,"lower"))lower_oracle();else if(!strcmp(which,"adopt-idle"))idle_adopt_oracle();else if(!strcmp(which,"history"))history_oracle();else if(!strcmp(which,"unresolved"))unresolved_oracle();else if(!strcmp(which,"snapshot"))snapshot_oracle();else if(!strcmp(which,"source"))source_oracle();else if(!strcmp(which,"open"))open_oracle();else if(!strcmp(which,"grant"))grant_oracle();else if(!strcmp(which,"alias"))alias_oracle();else if(!strcmp(which,"conflict-invalid-mode"))malformed_conflict_oracle(0);else if(!strcmp(which,"conflict-invalid-source"))malformed_conflict_oracle(1);else return 2;
 printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
