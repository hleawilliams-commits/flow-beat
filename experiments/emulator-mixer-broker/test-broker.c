/* SPDX-License-Identifier: GPL-2.0-only */
/* Includes exact frozen kernel methods, with owned-memory/pthread host shims.
 * No module, device, daemon, socket or UI is opened by this executable. */
#include "kernel-shim.inc"
#include "candidate.inc"
#include "broker-under-test.inc"

static unsigned checks,failures;
static void ck(bool ok,const char *name){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",name);}}
static void fill(uint8_t *b,unsigned seed){for(unsigned i=0;i<152;i++)b[i]=(uint8_t)(seed+i*23);}
static void tls_clean(void){from_calls=to_calls=fail_from=fail_to=pause_from=pause_before_lock=pause_after_unlock=0;admin=1;allocation_failure=interrupted=0;}
struct fixture {
 struct file owner,other; struct mb_broker *broker;
 unsigned snapshot_calls,submit_calls; int snapshot_error,snapshot_mutation,mode;
 struct mb_command delayed;enum mb_operation delayed_op;
 struct mb_backend_reply malformed;int reenter_result;
};
enum {NORMAL,LOST_ACK,PENDING,REJECT,MALFORMED,HOLD,REENTER};
static long native_submit(struct fixture *f,enum mb_operation op,const struct mb_command *c){
 struct az_overlay_frame k={.header={.version=2,.reserved=0,.instance={c->instance[0],c->instance[1]},.run_id=c->run_id,.epoch=c->epoch,.sequence=c->sequence}};
 memcpy(k.frame,c->frame,152);
 return ctrl_ioctl(&f->owner,op==MB_OP_FRAME?AZ_OVERLAY_FRAME:AZ_OVERLAY_ROTATE,(unsigned long)&k);
}
static int backend_snapshot(void *v,struct mb_state *out){
 struct fixture *f=v;struct az_overlay_state k={0};f->snapshot_calls++;
 if(f->snapshot_error)return f->snapshot_error;
 long rc=ctrl_ioctl(&f->owner,AZ_OVERLAY_STATE,(unsigned long)&k);if(rc)return (int)rc;
 /* Validate the fields intentionally omitted from the semantic mb_state. */
 if(k.version!=2||k.reserved)return -EPROTO;
 *out=(struct mb_state){.instance={k.instance[0],k.instance[1]},.run_id=k.run_id,.epoch=k.epoch,.sequence=k.sequence,.flags=k.flags,.crc_len=k.crc_len};memcpy(out->frame,k.frame,152);
 switch(f->snapshot_mutation){case 1:out->instance[0]^=1;break;case 2:out->instance[1]^=1;break;case 3:out->run_id++;break;case 4:out->epoch++;break;case 5:out->sequence++;break;case 6:out->flags^=4;break;case 7:out->crc_len--;break;case 8:out->frame[151]^=1;break;default:break;}
 return 0;
}
static struct mb_backend_reply backend_submit(void *v,enum mb_operation op,const struct mb_command *c){
 struct fixture *f=v;f->submit_calls++;
 if(f->mode==PENDING){f->delayed=*c;f->delayed_op=op;return(struct mb_backend_reply){MB_OUTCOME_UNKNOWN,ETIMEDOUT};}
 if(f->mode==REJECT)return(struct mb_backend_reply){MB_OUTCOME_REJECTED,EAGAIN};
 if(f->mode==HOLD)hold();
 if(f->mode==REENTER)f->reenter_result=mb_quiesce(f->broker);
 long rc=native_submit(f,op,c);
 if(f->mode==MALFORMED)return f->malformed;
 if(rc)return(struct mb_backend_reply){MB_OUTCOME_REJECTED,(int)-rc};
 return(struct mb_backend_reply){f->mode==LOST_ACK?MB_OUTCOME_UNKNOWN:MB_OUTCOME_COMMITTED,f->mode==LOST_ACK?ETIMEDOUT:0};
}
static void reset(struct fixture *f){
 memset(f,0,sizeof *f);tls_clean();memset(inject_pending,0,sizeof inject_pending);memset(has_inject,0,sizeof has_inject);int crc[4]={28,38,96,38};memcpy(spi_crc_len,crc,sizeof crc);n_crc=4;mixer_fenced=false;mixer_owner=NULL;mixer_run=mixer_epoch=mixer_sequence=0;allow_overlay_ownership=true;memset(mixer_instance,0,sizeof mixer_instance);random_wait_result=0;assert(!mixer_instance_init());testmode_held=0;f->owner.f_mode=f->other.f_mode=FMODE_WRITE;gate.waiting=gate.resume=0;
 uint8_t packet[153]={2};fill(packet+1,13);assert(ctrl_write(&f->other,(char *)packet,sizeof packet,NULL)==153);
 struct az_overlay_command c={.version=2,.instance={mixer_instance[0],mixer_instance[1]},.run_id=7};assert(!ctrl_ioctl(&f->owner,AZ_OVERLAY_CLAIM,(unsigned long)&c));
}
static struct mb_state state(struct fixture *f){struct mb_state s;assert(!backend_snapshot(f,&s));return s;}
static void bind(struct fixture *f){struct mb_state s=state(f);struct mb_backend b={f,backend_snapshot,backend_submit};assert(mb_create(&b,&s,&f->broker)==MB_OK);}
static struct mb_grant adopt(struct fixture *f){struct mb_state s=state(f);struct mb_grant g;assert(!mb_adopt(f->broker,s.frame,11,&g));return g;}
static void setup(struct fixture *f,struct mb_grant *g){reset(f);bind(f);*g=adopt(f);}
static struct mb_request request(struct mb_grant g,uint64_t id,unsigned seed){uint8_t b[152];fill(b,seed);struct mb_request r;assert(!mb_stamp(&g,id,b,&r));return r;}
static struct mb_status status(struct fixture *f){struct mb_status s;assert(!mb_inspect(f->broker,&s));return s;}
static void done(struct fixture *f){unsigned calls=f->submit_calls;mb_destroy(f->broker);f->broker=NULL;assert(f->submit_calls==calls);assert(mixer_owner==&f->owner);assert(!ctrl_release(NULL,&f->owner));}

/* Single-oracle cases also serve as strict, bounded mutation controls. */
static void stale_oracle(void){struct fixture f;struct mb_grant g;setup(&f,&g);struct mb_request r=request(g,1,21);r.grant.epoch++;struct mb_receipt a;ck(mb_submit(f.broker,&r,&a)==MB_ERR_STALE&&f.submit_calls==0,"stale_epoch_refused_before_backend");done(&f);}
static void conflict_oracle(void){struct fixture f;struct mb_grant g;setup(&f,&g);struct mb_request r=request(g,1,21);struct mb_receipt a;assert(!mb_submit(f.broker,&r,&a));r.frame[151]^=1;ck(mb_submit(f.broker,&r,&a)==MB_ERR_CONFLICT&&f.submit_calls==1&&!status(&f).admission_open,"duplicate_compares_all_frame_bytes");done(&f);}
static void lower_oracle(void){struct fixture f;struct mb_grant g;setup(&f,&g);struct mb_request r=request(g,1,21);struct mb_receipt a;f.mode=PENDING;assert(!mb_submit(f.broker,&r,&a));assert(!mb_reconcile(f.broker,&a));ck(a.outcome==MB_OUTCOME_UNKNOWN&&status(&f).unresolved&&f.submit_calls==1,"lower_state_is_not_cancellation");done(&f);}
static void quiesce_oracle(void){struct fixture f;struct mb_grant g;setup(&f,&g);struct mb_request r=request(g,1,21);struct mb_receipt a;assert(!mb_quiesce(f.broker));ck(mb_submit(f.broker,&r,&a)==MB_ERR_CLOSED&&f.submit_calls==0,"quiesce_blocks_new_request");done(&f);}
static void adopt_oracle(void){struct fixture f;struct mb_grant g;setup(&f,&g);struct mb_state s=state(&f);assert(!mb_quiesce(f.broker));ck(mb_adopt(f.broker,s.frame,12,&g)==MB_ERR_CLOSED&&!status(&f).admission_open,"closed_epoch_cannot_reissue_grant");done(&f);}
static void cached_oracle(void){struct fixture f;struct mb_grant g;setup(&f,&g);struct mb_request r=request(g,1,21);struct mb_receipt a;assert(!mb_submit(f.broker,&r,&a));assert(!mb_quiesce(f.broker));ck(!mb_submit(f.broker,&r,&a)&&a.outcome==MB_OUTCOME_COMMITTED&&f.submit_calls==1&&!status(&f).admission_open,"cached_ack_does_not_reopen");done(&f);}

static void verification(void){
 struct fixture f;reset(&f);struct mb_state s=state(&f);struct mb_backend b={&f,backend_snapshot,backend_submit};struct mb_broker *p=NULL;
 for(int i=1;i<=8;i++){f.snapshot_mutation=i;ck(mb_create(&b,&s,&p)==MB_ERR_STATE&&p==NULL,"create_requires_exact_snapshot");}f.snapshot_mutation=0;
 f.snapshot_error=-EIO;ck(mb_create(&b,&s,&p)==MB_ERR_BACKEND&&p==NULL,"create_requires_complete_snapshot");f.snapshot_error=0;
 for(unsigned flags=0;flags<32;flags++){if(flags==31)continue;struct mb_state bad=s;bad.flags=flags;ck(mb_create(&b,&bad,&p)==MB_ERR_INVALID&&p==NULL,"create_requires_injected_owner_enabled_flags");}
 struct mb_state bad=s;bad.crc_len=97;ck(mb_create(&b,&bad,&p)==MB_ERR_INVALID,"crc_must_be_96");bad=s;bad.instance[0]=bad.instance[1]=0;ck(mb_create(&b,&bad,&p)==MB_ERR_INVALID,"nonzero_instance_required");bad=s;bad.run_id=0;ck(mb_create(&b,&bad,&p)==MB_ERR_INVALID,"nonzero_run_required");bad=s;bad.epoch=0;ck(mb_create(&b,&bad,&p)==MB_ERR_INVALID,"nonzero_epoch_required");
 bind(&f);struct mb_grant g;uint8_t bytes[152];memcpy(bytes,s.frame,152);bytes[151]^=1;
 ck(mb_adopt(f.broker,bytes,11,&g)==MB_ERR_STATE&&!status(&f).grant_issued,"adopt_all_baseline_bytes");ck(mb_adopt(f.broker,s.frame,0,&g)==MB_ERR_INVALID,"zero_producer_refused");
 for(int i=1;i<=8;i++){f.snapshot_mutation=i;ck(mb_adopt(f.broker,s.frame,11,&g)==MB_ERR_STATE&&!status(&f).admission_open,"adopt_requires_fresh_exact_state");}f.snapshot_mutation=0;
 ck(!mb_adopt(f.broker,s.frame,11,&g)&&status(&f).admission_open,"verified_adoption_opens_once");ck(mb_adopt(f.broker,s.frame,11,&g)==MB_ERR_CLOSED,"double_adopt_refused");done(&f);
}
static void ordered_requests(void){
 struct fixture f;struct mb_grant g;setup(&f,&g);struct mb_receipt a;struct mb_request r=request(g,2,21);
 ck(mb_submit(f.broker,&r,&a)==MB_ERR_ID&&f.submit_calls==0&&status(&f).next_request_id==1,"gap_does_not_consume_id");r.request_id=1;
 for(int i=0;i<5;i++){struct mb_request bad=r;if(i<2)bad.grant.instance[i]^=1;else if(i==2)bad.grant.run_id++;else if(i==3)bad.grant.epoch++;else bad.grant.producer_id++;
 ck(mb_submit(f.broker,&bad,&a)==MB_ERR_STALE&&!f.submit_calls,"every_grant_field_bound");}
 ck(!mb_submit(f.broker,&r,&a)&&a.outcome==MB_OUTCOME_COMMITTED&&a.command.sequence==1&&mixer_sequence==1&&!memcmp(inject_pending[2],r.frame,152),"actual_kernel_commit_full_frame");
 struct mb_receipt saved=a;unsigned snapshots=f.snapshot_calls;ck(!mb_submit(f.broker,&r,&a)&&!memcmp(&saved,&a,sizeof a)&&f.submit_calls==1&&f.snapshot_calls==snapshots,"exact_duplicate_cached");
 r.request_id=3;ck(mb_submit(f.broker,&r,&a)==MB_ERR_ID&&status(&f).last.command.sequence==1&&status(&f).next_request_id==2,"gap_preserves_receipt");r.request_id=2;
 ck(!mb_submit(f.broker,&r,&a)&&a.outcome==MB_OUTCOME_COMMITTED&&a.command.sequence==2,"second_request_maps_next_kernel_sequence");r.request_id=1;
 ck(mb_submit(f.broker,&r,&a)==MB_ERR_ID&&f.submit_calls==2,"only_last_ack_cached");
 uint8_t baseline[152];fill(baseline,71);ck(mb_rotate(f.broker,baseline,&a)==MB_ERR_CLOSED,"rotate_requires_quiescence");assert(!mb_quiesce(f.broker));
 ck(!mb_rotate(f.broker,baseline,&a)&&a.outcome==MB_OUTCOME_COMMITTED&&mixer_epoch==2&&mixer_sequence==0&&mixer_owner==&f.owner&&!memcmp(inject_pending[2],baseline,152)&&!status(&f).grant_issued,"rotation_commits_generation_and_baseline");
 ck(mb_submit(f.broker,&r,&a)==MB_ERR_STALE&&f.submit_calls==3,"old_request_after_rotate_refused");struct mb_grant newer;
 ck(!mb_adopt(f.broker,baseline,12,&newer)&&newer.epoch==2&&newer.producer_id==12,"new_generation_separate_adoption");r=request(newer,1,88);
 ck(!mb_submit(f.broker,&r,&a)&&a.outcome==MB_OUTCOME_COMMITTED&&mixer_sequence==1,"new_generation_starts_request_one");
 /* Kernel and broker cannot determine semantic freshness of reauthored bytes. */
 r=request(newer,2,13);ck(!mb_submit(f.broker,&r,&a)&&!memcmp(inject_pending[2],r.frame,152),"current_grant_can_reauthor_old_payload_limitation");done(&f);
}
static void unknown_paths(void){
 for(unsigned rotate=0;rotate<2;rotate++)for(int mode=LOST_ACK;mode<=PENDING;mode++){
 struct fixture f;struct mb_grant g;setup(&f,&g);struct mb_request r=request(g,1,42);struct mb_receipt a;f.mode=mode;if(rotate)assert(!mb_quiesce(f.broker));
 int rc=rotate?mb_rotate(f.broker,r.frame,&a):mb_submit(f.broker,&r,&a);
 ck(rc==MB_OK&&a.outcome==MB_OUTCOME_UNKNOWN&&status(&f).unresolved&&!status(&f).admission_open,"unknown_closes_and_retains_operation");
 ck(mb_rotate(f.broker,r.frame,&a)==MB_ERR_BUSY&&f.submit_calls==1,"unknown_blocks_rotation");
 ck(mb_adopt(f.broker,r.frame,12,&g)==MB_ERR_BUSY,"unknown_blocks_adoption");
 if(!rotate){unsigned snapshots=f.snapshot_calls;ck(!mb_submit(f.broker,&r,&a)&&a.outcome==MB_OUTCOME_UNKNOWN&&f.submit_calls==1&&f.snapshot_calls==snapshots,"unknown_duplicate_no_retry");}
 if(mode==PENDING){ck(!mb_reconcile(f.broker,&a)&&a.outcome==MB_OUTCOME_UNKNOWN&&status(&f).unresolved,"pending_lower_snapshot_stays_unknown");assert(!native_submit(&f,f.delayed_op,&f.delayed));}
 for(int i=1;i<=8;i++){f.snapshot_mutation=i;ck(!mb_reconcile(f.broker,&a)&&a.outcome==MB_OUTCOME_UNKNOWN&&status(&f).unresolved,"positive_reconciliation_requires_full_state");}f.snapshot_mutation=0;
 f.snapshot_error=-EIO;ck(mb_reconcile(f.broker,&a)==MB_ERR_BACKEND&&a.outcome==MB_OUTCOME_UNKNOWN&&status(&f).unresolved,"failed_snapshot_retains_unknown");f.snapshot_error=0;
 ck(!mb_reconcile(f.broker,&a)&&a.outcome==MB_OUTCOME_COMMITTED&&!status(&f).unresolved&&!status(&f).admission_open&&f.submit_calls==1,"exact_positive_reconciliation_without_replay");
 struct mb_grant fresh;struct mb_state s=state(&f);
 if(!rotate){ck(mb_adopt(f.broker,s.frame,12,&fresh)==MB_ERR_CLOSED,"reconciled_frame_cannot_reopen_epoch");f.mode=NORMAL;assert(!mb_rotate(f.broker,r.frame,&a));}
 ck(!mb_adopt(f.broker,r.frame,12,&fresh)&&fresh.epoch==2&&status(&f).admission_open,"reconciled_rotation_allows_checked_new_epoch");done(&f);
 }
}
static void rejection_and_malformed(void){
 struct fixture f;struct mb_grant g;setup(&f,&g);struct mb_request r=request(g,1,21);struct mb_receipt a;f.mode=REJECT;
 ck(!mb_submit(f.broker,&r,&a)&&a.outcome==MB_OUTCOME_REJECTED&&a.backend_error==EAGAIN&&!status(&f).admission_open&&!status(&f).unresolved&&mixer_sequence==0,"definitive_rejection_closes_without_commit");
 ck(!mb_submit(f.broker,&r,&a)&&a.outcome==MB_OUTCOME_REJECTED&&f.submit_calls==1,"rejection_duplicate_cached");f.mode=NORMAL;
 ck(!mb_rotate(f.broker,r.frame,&a)&&a.outcome==MB_OUTCOME_COMMITTED&&mixer_epoch==2,"known_rejection_allows_explicit_rotation");done(&f);
 struct mb_backend_reply malformed[]={{MB_OUTCOME_COMMITTED,1},{MB_OUTCOME_COMMITTED,-1},{MB_OUTCOME_REJECTED,0},{MB_OUTCOME_REJECTED,-1},{MB_OUTCOME_NONE,0},{(enum mb_outcome)99,0},{MB_OUTCOME_UNKNOWN,-1}};
 for(unsigned i=0;i<sizeof malformed/sizeof malformed[0];i++){setup(&f,&g);r=request(g,1,21);f.mode=MALFORMED;f.malformed=malformed[i];
 ck(!mb_submit(f.broker,&r,&a)&&a.outcome==MB_OUTCOME_UNKNOWN&&status(&f).unresolved&&!status(&f).admission_open&&mixer_sequence==1,"malformed_reply_after_commit_stays_unknown");
 ck(!mb_reconcile(f.broker,&a)&&a.outcome==MB_OUTCOME_COMMITTED&&!status(&f).admission_open,"malformed_reply_reconciles_only_exact_state");done(&f);}
 setup(&f,&g);r=request(g,1,21);f.mode=REENTER;ck(!mb_submit(f.broker,&r,&a)&&a.outcome==MB_OUTCOME_COMMITTED&&f.reenter_result==MB_ERR_MUTEX,"callback_reentry_rejected_without_deadlock");done(&f);
}
static void exhaustion_and_drift(void){
 struct fixture f;struct mb_grant g;struct mb_receipt a;setup(&f,&g);struct mb_request r=request(g,1,21);
 ck(mb_stamp(&g,UINT64_MAX,r.frame,&r)==MB_ERR_OVERFLOW,"stamp_id_exhaustion_no_wrap");r=request(g,1,21);r.request_id=UINT64_MAX;
 ck(mb_submit(f.broker,&r,&a)==MB_ERR_OVERFLOW&&!status(&f).admission_open&&f.submit_calls==0,"submit_id_exhaustion_closes");done(&f);
 /* Synthetic boundary seed of broker-local ID only; production has no setter. */
 setup(&f,&g);f.broker->state.next_request_id=UINT64_MAX-1;r=request(g,UINT64_MAX-1,21);
 ck(!mb_submit(f.broker,&r,&a)&&a.outcome==MB_OUTCOME_COMMITTED&&status(&f).next_request_id==UINT64_MAX,"last_representable_request_id_accepted");r.request_id=UINT64_MAX;
 ck(mb_submit(f.broker,&r,&a)==MB_ERR_OVERFLOW&&f.submit_calls==1&&!status(&f).admission_open,"request_id_never_wraps_after_commit");done(&f);
 reset(&f);mixer_sequence=UINT64_MAX;bind(&f);g=adopt(&f);r=request(g,1,21);ck(mb_submit(f.broker,&r,&a)==MB_ERR_OVERFLOW&&f.submit_calls==0,"kernel_sequence_exhaustion_refused");ck(mb_rotate(f.broker,r.frame,&a)==MB_ERR_OVERFLOW,"rotation_sequence_exhaustion_refused");done(&f);
 reset(&f);mixer_epoch=UINT64_MAX;bind(&f);g=adopt(&f);r=request(g,1,21);assert(!mb_quiesce(f.broker));ck(mb_rotate(f.broker,r.frame,&a)==MB_ERR_OVERFLOW&&f.submit_calls==0,"epoch_exhaustion_refused");done(&f);
 for(int i=1;i<=8;i++){setup(&f,&g);r=request(g,1,21);assert(!mb_quiesce(f.broker));f.snapshot_mutation=i;ck(mb_rotate(f.broker,r.frame,&a)==MB_ERR_STATE&&f.submit_calls==0,"rotation_requires_unchanged_owned_state");f.snapshot_mutation=0;done(&f);}
 setup(&f,&g);r=request(g,1,21);assert(!ctrl_release(NULL,&f.owner));ck(!mb_submit(f.broker,&r,&a)&&a.outcome==MB_OUTCOME_REJECTED&&a.backend_error==EACCES&&!status(&f).admission_open,"lost_native_owner_definitive_rejection");ck(mb_rotate(f.broker,r.frame,&a)==MB_ERR_STATE&&f.submit_calls==1,"lost_owner_never_reclaimed_automatically");mb_destroy(f.broker);
}
struct thread_job{struct fixture *fixture;struct mb_request request;struct mb_receipt receipt;int rc;bool closing;atomic_bool started,finished;};
static void *thread_op(void *v){struct thread_job *j=v;tls_clean();atomic_store(&j->started,true);j->rc=j->closing?mb_quiesce(j->fixture->broker):mb_submit(j->fixture->broker,&j->request,&j->receipt);atomic_store(&j->finished,true);return NULL;}
static void concurrency(void){
 struct fixture f;struct mb_grant g;setup(&f,&g);f.mode=HOLD;struct thread_job a={.fixture=&f,.request=request(g,1,21)},b={.fixture=&f,.closing=true};pthread_t ta,tb;
 assert(!pthread_create(&ta,NULL,thread_op,&a));wait_held();assert(!pthread_create(&tb,NULL,thread_op,&b));while(!atomic_load(&b.started))sched_yield();
 ck(pthread_mutex_trylock(&f.broker->mutex)==EBUSY&&!atomic_load(&a.finished)&&!atomic_load(&b.finished),"backend_and_quiesce_share_serialization_lock");let_go();assert(!pthread_join(ta,NULL)&&!pthread_join(tb,NULL));
 ck(a.rc==MB_OK&&a.receipt.outcome==MB_OUTCOME_COMMITTED&&b.rc==MB_OK&&!status(&f).admission_open&&f.submit_calls==1,"quiesce_returns_after_issued_commit");done(&f);
 setup(&f,&g);enum{N=24};struct thread_job jobs[N];pthread_t threads[N];memset(jobs,0,sizeof jobs);for(unsigned i=0;i<N;i++){jobs[i].fixture=&f;jobs[i].request=request(g,1,21);assert(!pthread_create(&threads[i],NULL,thread_op,&jobs[i]));}for(unsigned i=0;i<N;i++){assert(!pthread_join(threads[i],NULL));ck(jobs[i].rc==MB_OK&&jobs[i].receipt.outcome==MB_OUTCOME_COMMITTED,"concurrent_duplicate_receipt");}
 ck(f.submit_calls==1&&mixer_sequence==1,"concurrent_duplicates_one_actual_kernel_write");done(&f);
}
int main(int argc,char **argv){alarm(30);const char *which=argc>1?argv[1]:"all";
 if(!strcmp(which,"all")){verification();ordered_requests();unknown_paths();rejection_and_malformed();exhaustion_and_drift();concurrency();stale_oracle();conflict_oracle();lower_oracle();quiesce_oracle();adopt_oracle();cached_oracle();}
 else if(!strcmp(which,"stale"))stale_oracle();else if(!strcmp(which,"conflict"))conflict_oracle();else if(!strcmp(which,"lower"))lower_oracle();else if(!strcmp(which,"quiesce"))quiesce_oracle();else if(!strcmp(which,"adopt"))adopt_oracle();else if(!strcmp(which,"cached"))cached_oracle();else return 2;
 printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
