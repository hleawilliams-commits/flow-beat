/* SPDX-License-Identifier: GPL-2.0-only */
/* Serial schedule of actual extracted kernel methods; no native consumer. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define ERESTARTSYS 512
#define min(a,b) ((a)<(b)?(a):(b))
#define module_param(...)
#define MODULE_PARM_DESC(...)
#undef static_assert
#define static_assert(x) _Static_assert((x), #x)
struct file { unsigned f_mode; void *private_data; };
#define DEFINE_SPINLOCK(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
#define spin_lock_irqsave(l,f) do{(f)=0;assert(!pthread_mutex_lock(l));}while(0)
#define spin_unlock_irqrestore(l,f) do{(void)(f);assert(!pthread_mutex_unlock(l));}while(0)
static size_t copy_from_user(void *d,const void *s,size_t n){memcpy(d,s,n);return 0;}
static size_t copy_to_user(void *d,const void *s,size_t n){memcpy(d,s,n);return 0;}
static int capable(int cap){assert(cap==CAP_SYS_ADMIN);return 1;}
static unsigned polls;
static unsigned usecs_to_jiffies(unsigned n){return n;}
/* Counts an explicit fixture poll; no real sleeping/clock or polling frequency. */
static int schedule_timeout_interruptible(unsigned n){assert(n>0);polls++;return 0;}
#include "subucom_frames.h"
#include "candidate.inc"

static unsigned checks,failures;
static void ck(bool ok,const char *label){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",label);}}
static struct file owner={FMODE_WRITE,NULL},spi={0,(void *)2};
static struct az_overlay_frame press,release_frame;
static struct az_overlay_command command(u64 seq){return (struct az_overlay_command){
    .version=AZ_OVERLAY_VERSION,.instance={17,29},.run_id=7,.epoch=1,.sequence=seq};}
static long io(unsigned cmd,void *p){return ctrl_ioctl(&owner,cmd,(unsigned long)p);}
static void reset(void){
    memset(inject_pending,0,sizeof inject_pending);memset(has_inject,0,sizeof has_inject);
    mixer_fenced=false;mixer_owner=NULL;mixer_run=mixer_epoch=mixer_sequence=0;
    mixer_instance[0]=17;mixer_instance[1]=29;allow_overlay_ownership=true;polls=0;
    testmode_held=0;int crc[4]={28,38,96,38};memcpy(spi_crc_len,crc,sizeof crc);
    press=(struct az_overlay_frame){.header=command(1)};
    release_frame=(struct az_overlay_frame){.header=command(2)};
    for(unsigned i=0;i<MISO_SIZE;i++)release_frame.frame[i]=(u8)(5+17*i);
    release_frame.frame[6]=8;release_frame.frame[32]=0;release_frame.frame[34]=88;
    memcpy(press.frame,release_frame.frame,MISO_SIZE);press.frame[6]=0;press.frame[32]=0x40;
}
static long claim(void){struct az_overlay_command h=command(0);h.epoch=0;return io(AZ_OVERLAY_CLAIM,&h);}
static bool equals_stamped(const u8 *got,const u8 *raw){
    u8 expected[MISO_SIZE];memcpy(expected,raw,MISO_SIZE);
    u16 crc=crc16_x25(expected,96);expected[96]=(u8)crc;expected[97]=(u8)(crc>>8);
    return !memcmp(got,expected,MISO_SIZE);
}
static bool state_is(u64 sequence,const u8 *raw){
    struct az_overlay_state s={0};return io(AZ_OVERLAY_STATE,&s)==0&&s.flags==31&&
        s.sequence==sequence&&s.epoch==1&&s.run_id==7&&s.instance[0]==17&&s.instance[1]==29&&
        !memcmp(s.frame,raw,MISO_SIZE);
}
static void no_interposed_poll(void){
    reset();ck(claim()==0,"claim_success");
    ck(io(AZ_OVERLAY_FRAME,&press)==0,"press_commit_success");
    ck(polls==0,"press_commit_did_not_poll");
    ck(state_is(1,press.frame),"state_records_press_commit");
    ck(polls==0,"state_is_not_spi_observation");
    ck(io(AZ_OVERLAY_FRAME,&release_frame)==0,"release_commit_success");
    ck(state_is(2,release_frame.frame),"state_records_release_commit");
    ck(polls==0,"both_commits_precede_first_poll");
    u8 got[MISO_SIZE];ck(spi_read(&spi,(char *)got,sizeof got,NULL)==MISO_SIZE,"first_poll_full_frame");
    ck(polls==1,"exact_one_first_poll");
    ck(equals_stamped(got,release_frame.frame),"first_poll_sees_only_latest_release");
    ck(!equals_stamped(got,press.frame),"successful_press_commit_absent_from_first_poll");
    ck(spi_read(&spi,(char *)got,sizeof got,NULL)==MISO_SIZE&&equals_stamped(got,release_frame.frame),"later_poll_does_not_replay_prior_press");
    ck(polls==2&&state_is(2,release_frame.frame),"reads_do_not_consume_or_advance_commit_sequence");
}
static void interposed_poll_control(void){
    reset();ck(claim()==0,"interposed_claim_success");
    ck(io(AZ_OVERLAY_FRAME,&press)==0,"interposed_press_commit_success");
    u8 got[MISO_SIZE];ck(spi_read(&spi,(char *)got,sizeof got,NULL)==MISO_SIZE&&equals_stamped(got,press.frame),"interposed_poll_can_observe_press");
    ck(io(AZ_OVERLAY_FRAME,&release_frame)==0,"interposed_release_commit_success");
    ck(spi_read(&spi,(char *)got,sizeof got,NULL)==MISO_SIZE&&equals_stamped(got,release_frame.frame),"interposed_second_poll_observes_release");
    ck(polls==2&&state_is(2,release_frame.frame),"poll_schedule_changes_visibility_not_commit_order");
}
static void focused(const char *which){
    reset();assert(claim()==0);assert(io(AZ_OVERLAY_FRAME,&press)==0);
    u8 got[MISO_SIZE];
    if(!strcmp(which,"overlay-read")){
        assert(spi_read(&spi,(char *)got,sizeof got,NULL)==MISO_SIZE);
        ck(equals_stamped(got,press.frame),"poll_reads_committed_overlay");
    }else{
        assert(io(AZ_OVERLAY_FRAME,&release_frame)==0);assert(polls==0);
        assert(spi_read(&spi,(char *)got,sizeof got,NULL)==MISO_SIZE);
        if(!strcmp(which,"false-hypothesis"))
            ck(equals_stamped(got,press.frame),"every_successful_commit_must_be_observed_hypothesis");
        else ck(equals_stamped(got,release_frame.frame),"latest_commit_replaces_prior_overlay");
    }
}
int main(int argc,char **argv){
    assert(argc==2);
    if(!strcmp(argv[1],"all")){no_interposed_poll();interposed_poll_control();}
    else focused(argv[1]);
    printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
