/* SPDX-License-Identifier: GPL-3.0-only
 * Serial actual-installer fixture. Memory/protection calls and publication are
 * instrumented, no emitted ARM64 instruction is executed on this host. */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdatomic.h>

static unsigned checks, failures;
static const char *scenario;
#define CHECK(expr, label) do { checks++; if (!(expr)) { failures++; fprintf(stderr,"FAIL %s/%s\n",scenario,label); } } while (0)
static _Alignas(4096) uint32_t page[1024];
static _Alignas(16) uint32_t island[4], function_words[8];
static void *original;
static int want_island, fail_map, fail_seal, fail_unlock, warn_relock;
static int unlocked, sealed, unmapped, redirect_count, note_count, last_path;
static int orig_ready_at_redirect, executable_at_redirect, publication_order;
static int g_azm_late;
typedef int azprot_t;
enum {AZM_ISLAND=1,AZM_FALLBACK=2,AZM_DECLINED=-1,AZM_REFUSED_LATE=-3};
static void dbg(const char *fmt, ...) {(void)fmt;}
static void *test_mmap(void *a,size_t n,int p,int f,int fd,ptrdiff_t off) {
    (void)a;(void)p;(void)f;(void)fd;(void)off;
    if (n!=4096) abort();
    return fail_map ? MAP_FAILED : (void*)page;
}
static int test_mprotect(void *a,size_t n,int p) {
    if(a!=page || n!=4096 || p!=(PROT_READ|PROT_EXEC)) abort();
    if(fail_seal) return -1;
    sealed=1; return 0;
}
static int test_munmap(void *a,size_t n) {if(a!=page||n!=4096)abort(); unmapped++;return 0;}
static void test_cache(char *a,char *b) {(void)a;(void)b;}
static int azprot_unlock(uintptr_t a,size_t n,azprot_t *p) {
    (void)a;(void)n; *p=0; unlocked++; return unlocked==fail_unlock ? -1:0;
}
static int azprot_relock(azprot_t *p) {(void)p;return warn_relock ? -1:0;}
static uint32_t *azm_island_slot(uintptr_t a) {(void)a;return want_island?island:NULL;}
static void azm_note_hooked(uintptr_t a) {(void)a;note_count++;}
static int azm_rec(uintptr_t a,const char*n,int p) {(void)a;(void)n;last_path=p;return p;}
static void test_store(void *p,uintptr_t v,size_t size,int order) {
    if(p==&original) {
        if(size!=sizeof original||order!=__ATOMIC_RELAXED)abort();
        memcpy(p,&v,size);publication_order=1;
    } else if(p==function_words) {
        if(size!=4||order!=__ATOMIC_RELEASE)abort();
        redirect_count++;
        orig_ready_at_redirect=original==page && publication_order==1;
        executable_at_redirect=sealed;
        memcpy(p,&v,size);publication_order=2;
    } else abort();
}
#define mmap test_mmap
#define mprotect test_mprotect
#define munmap test_munmap
#define __builtin___clear_cache test_cache
#define __atomic_store_n(p,v,m) test_store((void*)(p),(uintptr_t)(v),sizeof(*(p)),(m))
#include "installer-extracted.inc"
#undef __atomic_store_n

static const uint32_t submit[4]={0xa9bd7bfd,0x910003fd,0xf9000bf3,0xaa0003f3};
static const uint32_t execute[4]={0xf9400c03,0x90006fc1,0x9102d021,0x91067062};
static uint64_t tail_target(unsigned at) {uint64_t v;memcpy(&v,&page[at+2],8);return v;}
static void reset(const char *name,const uint32_t *words) {
    scenario=name;memset(page,0,sizeof page);memset(island,0,sizeof island);
    memset(function_words,0,sizeof function_words);memcpy(function_words,words,16);
    original=NULL;want_island=1;fail_map=fail_seal=fail_unlock=warn_relock=0;
    unlocked=sealed=unmapped=redirect_count=note_count=last_path=0;
    orig_ready_at_redirect=executable_at_redirect=publication_order=0;g_azm_late=1;
}
static int install(void) {return azm_install_hook_at((uintptr_t)function_words,(void*)(uintptr_t)0x123456789abcdef0ULL,&original,"fixture");}
static void success(const char *name,const uint32_t *words) {
    reset(name,words);int r=install();
    CHECK(r==1&&last_path==AZM_ISLAND,"island-success");
    CHECK(original==page,"direct-original-storage");
    CHECK(orig_ready_at_redirect,"original-before-redirection");
    CHECK(executable_at_redirect,"sealed-before-redirection");
    CHECK(redirect_count==1&&note_count==1,"one-entry-store");
    CHECK(page[0]==words[0],"one-native-instruction-copied");
    CHECK(page[1]==0x58000051u&&page[2]==0xd61f0220u,"tail-ldr-br-x17");
    CHECK(tail_target(1)==(uintptr_t)function_words+4,"original-plus-four");
    CHECK(!memcmp(function_words+1,words+1,12),"uncovered-instructions-unchanged");
    uint64_t target;memcpy(&target,island+2,8);
    CHECK(island[0]==0x58000051u&&island[1]==0xd61f0220u&&target==0x123456789abcdef0ULL,"island-target-exact");
    CHECK(unmapped==0,"installed-page-retained");
}
static void declined(const char*name,int map,int seal,int unlock) {
    reset(name,submit);fail_map=map;fail_seal=seal;fail_unlock=unlock;
    CHECK(install()==AZM_DECLINED,"declined");
    CHECK(original==NULL&&redirect_count==0,"no-publication");
    CHECK(!memcmp(function_words,submit,16)&&note_count==0,"no-entry-change");
    CHECK(unmapped==(map?0:1),"private-trampoline-discarded");
}
int main(void) {
    success("submission",submit);success("executor",execute);
    declined("mmap-failure",1,0,0);declined("seal-failure",0,1,0);
    declined("island-unlock-failure",0,0,1);declined("entry-unlock-failure",0,0,2);
    reset("late-no-island",submit);want_island=0;
    CHECK(install()==AZM_REFUSED_LATE,"refused");
    CHECK(original==NULL&&redirect_count==0&&sealed==0&&unmapped==0,"no-trampoline-publication");
    CHECK(!memcmp(function_words,submit,16),"no-entry-change");
    success("partial-init-first-claim",submit);
    scenario="partial-init-second-refusal";void *saved=original;uint32_t first=function_words[0];
    void *second_original=NULL;
    /* A refused second address does not undo the earlier installed target. */
    CHECK(azm_install_hook_at(0,(void*)(uintptr_t)0x123456789abcdef0ULL,&second_original,"second")==AZM_DECLINED && second_original==NULL,"second-refused");
    CHECK(original==saved&&function_words[0]==first&&unmapped==0,"first-hook-retained");
    reset("relock-warning",submit);warn_relock=1;
    CHECK(install()==1&&original==page&&unmapped==0,"warning-keeps-successful-hook");
    CHECK(orig_ready_at_redirect&&executable_at_redirect,"published-forwarding-valid");
    printf("checks=%u failures=%u scenarios=10\n",checks,failures);
    return failures?1:0;
}
