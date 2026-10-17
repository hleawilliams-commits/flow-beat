/* SPDX-License-Identifier: GPL-3.0-only */
#define USP_TEST 1
#include "../usb-backend.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static unsigned checks, claims, starts, forwarded_timer, forwarded_source, forwarded_dtor, forwarded_ctor;
static int fail_claim = -1, bad_firmware, fail_worker, destroy_in_source;
static uintptr_t claim_addresses[4];
static unsigned char controller_mem[0x158] __attribute__((aligned(8)));
static unsigned char rows_mem[USP_ROW_SIZE * USP_MAX_ROWS] __attribute__((aligned(8)));
static void *ctor_args[6], *source_self, *source_model;
static int source_row;
#define CHECK(c) do { ++checks; assert(c); } while (0)
static void put_ptr(void *p, uintptr_t v) { memcpy(p, &v, sizeof v); }
static void put_int(void *p, int v) { memcpy(p, &v, sizeof v); }
static void fixture(void) {
    memset(controller_mem, 0, sizeof controller_mem);
    memset(rows_mem, 0, sizeof rows_mem);
    put_ptr(controller_mem, USP_CONTROLLER_VT);
    put_ptr(controller_mem + USP_MODEL_OFFSET, USP_MODEL_VT);
    put_ptr(controller_mem + USP_MODEL_OFFSET + 8, (uintptr_t)rows_mem);
    put_int(controller_mem + USP_MODEL_OFFSET + 0x10, USP_MAX_ROWS);
    put_int(controller_mem + USP_MODEL_OFFSET + 0x18, 2);
    put_int(rows_mem + 4, 1);
    rows_mem[8] = 1; rows_mem[0xc] = 2; rows_mem[0xd] = 1;
    rows_mem[0x23] = 1;
    put_ptr(rows_mem + 0x10, (uintptr_t)"USB 1");
    put_int(rows_mem + USP_ROW_SIZE + 4, 1);
    rows_mem[USP_ROW_SIZE + 8] = 1;
    rows_mem[USP_ROW_SIZE + 0xc] = 3; rows_mem[USP_ROW_SIZE + 0xd] = 1;
    rows_mem[USP_ROW_SIZE + 0x24] = 1;
    put_ptr(rows_mem + USP_ROW_SIZE + 0x10, (uintptr_t)"USB 2");
}
static void original_timer(void *self) { CHECK(self == (void *)0x12340); ++forwarded_timer; }
static void original_dtor(void *self) {
    CHECK(self == controller_mem);
    CHECK(atomic_load(&usp_controller) == 0);
    ++forwarded_dtor;
    memset(controller_mem, 0xa5, sizeof controller_mem);
}
static void original_source(void *self, void *model, int row) {
    source_self = self; source_model = model; source_row = row; ++forwarded_source;
    if (destroy_in_source) usp_dtor_hook(self);
}
static void original_ctor(void *a0, void *a1, void *a2, void *a3, void *a4, void *a5) {
    void *args[] = {a0,a1,a2,a3,a4,a5};
    memcpy(ctor_args,args,sizeof args); ++forwarded_ctor; fixture();
}
static const char *fw(void) { return bad_firmware ? "ep147-other" : USP_FW_TAG; }
static void logger(const char *id, const char *fmt, ...) { (void)id;(void)fmt; }
static int starter(const char *id, const char *name, void *(*fn)(void *), void *arg) {
    CHECK(!strcmp(id,"usb-buttons"));CHECK(fn == usp_worker);(void)name;(void)arg;
    ++starts;return fail_worker;
}
static int claim(const char *id, uintptr_t addr, void *hook, void **orig) {
    CHECK(!strcmp(id,"usb-buttons"));CHECK(hook != NULL);
    unsigned index = claims++;
    claim_addresses[index] = addr;
    if ((int)index == fail_claim) return -1;
    if (addr == USP_DTOR_ADDR) *orig = (void *)original_dtor;
    else if (addr == USP_SOURCE_ADDR) *orig = (void *)original_source;
    else if (addr == USP_CTOR_ADDR) *orig = (void *)original_ctor;
    else if (addr == USP_TIMER_ADDR) *orig = (void *)original_timer;
    else CHECK(0);
    return 0;
}
static az_core_v1 core = {.abi=AZ_MOD_ABI,.size=sizeof(az_core_v1),.fw_tag=fw,
    .claim_hook=claim,.thread_start=starter,.log=logger};
static usp_snapshot snap(void) {
    usp_snapshot s; CHECK(!usp_backend_snapshot(&s)); return s;
}
static void timer(void) {
    CHECK(!usp_backend_request_observation());usp_timer_hook((void *)0x12340);
}
static _Atomic int reader_stop, reader_errors;
static void *reader(void *unused) {
    (void)unused;
    while (!atomic_load_explicit(&reader_stop, memory_order_acquire)) {
        usp_snapshot s;
        if (usp_backend_snapshot(&s) == USP_OK && s.serial &&
            (s.status != USP_ROWS_VALID || s.row_count != 2 ||
             s.rows[0].media_id != 0x0102 || s.rows[1].media_id != 0x0103))
            atomic_fetch_add_explicit(&reader_errors, 1, memory_order_relaxed);
    }
    return NULL;
}
int main(int argc, char **argv) {
    CHECK(argc==2);
    const char *which=argv[1];
    if (!strcmp(which,"firmware")) bad_firmware=1;
    if (!strcmp(which,"prologue")) usp_test_bad_prologue=1;
    if (!strcmp(which,"worker")) fail_worker=1;
    if (!strncmp(which,"claim",5)) fail_claim=atoi(which+5);
    usp_test_tid=(unsigned)getpid();
    int rc=usp_backend_init(&core,"usb-buttons");
    if (bad_firmware || usp_test_bad_prologue || fail_worker) {
        CHECK(rc<0);CHECK(claims==0);CHECK(atomic_load(&usp_controller)==0);
        if (!fail_worker) CHECK(starts==0);
    } else if (fail_claim>=0) {
        CHECK(rc==-20-fail_claim);CHECK(claims==(unsigned)fail_claim+1);
        CHECK(atomic_load(&usp_state)==-1);CHECK(!usp_observing());
        if (fail_claim>=1) usp_dtor_hook(controller_mem);
        if (fail_claim>=2) usp_source_hook(controller_mem,controller_mem+USP_MODEL_OFFSET,7);
        if (fail_claim>=3) usp_ctor_hook(controller_mem,(void*)1,(void*)2,(void*)3,(void*)4,(void*)5);
        CHECK(atomic_load(&usp_controller)==0);
    } else {
        CHECK(rc==0);CHECK(claims==4);CHECK(starts==1);
        CHECK(claim_addresses[0]==USP_DTOR_ADDR && claim_addresses[3]==USP_TIMER_ADDR);
        timer();CHECK(snap().status==USP_NO_CONTROLLER);
        fixture();
        usp_ctor_hook(controller_mem,(void*)1,(void*)2,(void*)3,(void*)4,(void*)5);
        for(unsigned i=0;i<6;++i) CHECK(ctor_args[i]==(i?(void*)(uintptr_t)i:controller_mem));
        timer();usp_snapshot s=snap();CHECK(s.status==USP_ROWS_VALID);CHECK(s.row_count==2);
        CHECK(s.captured_by_ctor==1);CHECK(s.rows[0].media_id==0x0102);
        CHECK(s.rows[1].media_id==0x0103);CHECK(!strcmp(s.rows[1].label,"USB 2"));
        CHECK(s.timer_tid==(unsigned)getpid());CHECK(s.ctor_tid==s.timer_tid);
        CHECK(usp_backend_request_port(1)==USP_OBSERVATION_ONLY);
        CHECK(usp_backend_request_port(2)==USP_OBSERVATION_ONLY);
        CHECK(usp_backend_request_port(0)==USP_BAD_PORT);
        CHECK(atomic_load(&usp_controller)==(uintptr_t)controller_mem);
        pthread_mutex_lock(&usp_snapshot_mu);CHECK(usp_backend_snapshot(&s)==USP_BUSY);
        pthread_mutex_unlock(&usp_snapshot_mu);
        if (!strcmp(which,"concurrent")) {
            pthread_t thread;
            CHECK(!pthread_create(&thread,NULL,reader,NULL));
            for (int i=0;i<2000;++i) timer();
            atomic_store_explicit(&reader_stop,1,memory_order_release);
            CHECK(!pthread_join(thread,NULL));
            CHECK(!atomic_load(&reader_errors));
        } else if (!strcmp(which,"bounds")) {
            put_int(controller_mem+USP_MODEL_OFFSET+0x18,USP_MAX_ROWS+1);
            timer();s=snap();CHECK(s.status==USP_BAD_BOUNDS);CHECK(s.row_count==0);
            fixture();put_int(controller_mem+USP_MODEL_OFFSET+0x10,1);
            timer();CHECK(snap().status==USP_BAD_BOUNDS);
            fixture();put_ptr(controller_mem+USP_MODEL_OFFSET,0x777777);
            timer();CHECK(snap().status==USP_BAD_MODEL);
            fixture();char long_name[100];memset(long_name,'x',sizeof long_name);long_name[99]=0;
            put_ptr(rows_mem+0x10,(uintptr_t)long_name);timer();s=snap();
            CHECK(strlen(s.rows[0].label)==USP_LABEL_SIZE-1);
        } else if (!strcmp(which,"thread")) {
            usp_test_tid=usp_pid+1;
            usp_source_hook(controller_mem,controller_mem+USP_MODEL_OFFSET,1);
            CHECK(atomic_load(&usp_controller)==0);CHECK(atomic_load(&usp_bad_thread)==usp_pid+1);
            s=snap();CHECK(s.status==USP_WRONG_THREAD);CHECK(s.row_count==0);
            usp_test_tid=usp_pid;timer();CHECK(snap().status==USP_WRONG_THREAD);
            CHECK(forwarded_source==1);
        } else {
            usp_source_hook(controller_mem,controller_mem+USP_MODEL_OFFSET,1);
            CHECK(source_self==controller_mem);CHECK(source_model==controller_mem+USP_MODEL_OFFSET);
            CHECK(source_row==1);CHECK(forwarded_source==1);
            timer();s=snap();CHECK(s.last_clicked_row==1);
            uint64_t generation=s.generation;
            destroy_in_source=1;
            usp_source_hook(controller_mem,controller_mem+USP_MODEL_OFFSET,0);
            timer();s=snap();CHECK(s.status==USP_NO_CONTROLLER);CHECK(s.row_count==0);
            CHECK(s.generation>generation);CHECK(forwarded_dtor==1);
            fixture();usp_backend_enable(0);
            s=snap();CHECK(s.status==USP_IDLE);CHECK(s.row_count==0);
            usp_source_hook(controller_mem,controller_mem+USP_MODEL_OFFSET,1);
            timer();CHECK(snap().status==USP_IDLE);CHECK(atomic_load(&usp_controller)==0);
        }
    }
    printf("%s: %u checks passed\n",which,checks);
    return 0;
}
