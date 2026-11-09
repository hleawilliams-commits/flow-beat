/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include "loader.h"
#include <dlfcn.h>
#include <elf.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include "expected-symbols.h"

/* Every successful handle and observation has reserved static custody before
 * loading. Only fixed authored artifacts exist in this experiment. Never close,
 * reopen or erase a handle, even after later validation/init refusal. */
static dp_object objects[DP_OBJECTS];
static dp_event events[DP_EVENTS];
static unsigned event_count,mode_value;
static bool event_overflow,probe_ok=true,preinit_zero=true;
static unsigned probe_count;
typedef struct { unsigned index;
    char name[64],error[160];
    uintptr_t address,map;
    int map_rc,sym_rc;
    unsigned type;
    uint64_t value,size;
    } symbol_query;
static symbol_query queries[96];
static unsigned query_count;
static bool query_overflow;
static bool initialized,load_attempted,loads_complete,query_attempted;
static bool add(uintptr_t a,uint64_t b,uintptr_t *out) {
    if(b>UINTPTR_MAX-a)return false;
    *out=a+(uintptr_t)b;
    return true;
}
static void error_copy(char *out,size_t size,const char *value) {
    if(!value){out[0]=0;
        return;
        }size_t n=strnlen(value,size-1);
    memcpy(out,value,n);
    out[n]=0;
}
static void event(unsigned index,unsigned value) {
    unsigned session=dp_active_session();
    if(value==0 || value==1 || value==2 || value==3 || value==13) preinit_zero&=session==0;
    if(event_count==DP_EVENTS){event_overflow=true;
        return;
        }
    events[event_count++]=(dp_event){index,value,session};
}
void dp_loader_initialize(unsigned mode) {
    if(initialized)return;
    initialized=true;
    mode_value=mode;
}
unsigned dp_fixture_mode(void) { return mode_value;
    }
void dp_constructor_marker(unsigned index) {
#ifdef DP_CONTROL_CONSTRUCTOR_MARKER
    if(index==0)return;
#endif
    event(index,0);
}
void dp_lifecycle_event(unsigned index,unsigned value) { event(index,value);
    }
static void *read_probe(void *arg) {
    unsigned index=*(unsigned*)arg;
    cc_module_snapshot module;
    rc_attempt attempt;
    bool okay=cc_module_get(index,&module);
    (void)rc_snapshot(0,&attempt);
    return (void*)(uintptr_t)(okay ? 1u : 0u);
}
void dp_callback_probe(unsigned index,unsigned phase) {
    event(index,phase+10);
    pthread_t thread;
    void *result=0;
    if(pthread_create(&thread,0,read_probe,&index)!=0){probe_ok=false;
        return;
        }
    if(pthread_join(thread,&result)!=0 || (uintptr_t)result!=1)probe_ok=false;
    ++probe_count;
}
bool dp_probes_complete(void) { return probe_ok && probe_count>=2;
    }
bool dp_preinit_sessions_zero(void) { return preinit_zero && !event_overflow;
    }
unsigned dp_event_count(void) { return event_count;
    }
const dp_event *dp_event_at(unsigned i) { return i<event_count?&events[i]:0;
    }
const dp_object *dp_loader_object(unsigned index) { return index<DP_OBJECTS?&objects[index]:0;
    }

bool dp_loader_load_all(const char *directory) {
    if(!initialized || load_attempted || !directory)return false;
    load_attempted=true;
    /* All root/dependency opens complete before any entry/session. This also
     * gives the harmless dependency an explicit permanent successful handle. */
    for(unsigned i=0;i<DP_OBJECTS;i++) {
        char path[4096];
        int n=snprintf(path,sizeof path,"%s/%s",directory,dp_expected[i].file);
        if(n<0 || (size_t)n>=sizeof path || dp_active_session()!=0)return false;
        dlerror();
        void *held=dlopen(path,RTLD_NOW|RTLD_LOCAL);
        /* Immediate fixed-slot custody, before any subsequent loader query. */
        objects[i].handle=held;
#ifdef DP_CONTROL_HANDLE_RECORD
        if(i==0){objects[i].safety_hold=held;
            objects[i].handle=0;
            }
#endif
        const char *err=dlerror();
        error_copy(objects[i].load_error,sizeof objects[i].load_error,err);
        event(i,1);
        if(!held || err)return false;
    }
    loads_complete=true;
    return true;
}
typedef struct { unsigned index;
    struct link_map *map;
    dp_object found;
    unsigned matches;
    bool invalid;
    } scan_context;
static int scan_object(struct dl_phdr_info *info,size_t size,void *data) {
    (void)size;
    scan_context *scan=data;
    uintptr_t dynamic=0;
    unsigned dynamic_count=0;
    if((uintptr_t)info->dlpi_addr!=(uintptr_t)scan->map->l_addr)return 0;
    for(unsigned i=0;i<info->dlpi_phnum;i++)if(info->dlpi_phdr[i].p_type==PT_DYNAMIC) {
        if(!add((uintptr_t)info->dlpi_addr,info->dlpi_phdr[i].p_vaddr,&dynamic)){scan->invalid=true;
            return 0;
            }
        ++dynamic_count;
    }
    if(dynamic_count!=1 || dynamic!=(uintptr_t)scan->map->l_ld)return 0;
    ++scan->matches;
    const dp_elf_manifest *expected=&dp_expected[scan->index];
    if(info->dlpi_phnum!=expected->phnum || info->dlpi_phnum>DP_PHDRS){scan->invalid=true;
        return 0;
        }
    dp_object *out=&scan->found;
    out->base=(uintptr_t)info->dlpi_addr;
    out->dynamic=dynamic;
    out->lo=UINTPTR_MAX;
    for(unsigned i=0;i<info->dlpi_phnum;i++) {
        const ElfW(Phdr)*p=&info->dlpi_phdr[i];
        const dp_elf_phdr *q=&expected->phdrs[i];
        if(p->p_type!=q->type || p->p_flags!=q->flags || p->p_vaddr!=q->vaddr || p->p_memsz!=q->memsz ||
           p->p_filesz!=q->filesz || p->p_offset!=q->offset || p->p_align!=q->align){scan->invalid=true;
            return 0;
            }
        if(p->p_type!=PT_LOAD)continue;
        uintptr_t lo,hi;
        if(p->p_filesz>p->p_memsz || !p->p_memsz || !add(out->base,p->p_vaddr,&lo) ||
           !add(lo,p->p_memsz,&hi) || lo>=hi || out->segment_count==DP_PHDRS){scan->invalid=true;
            return 0;
            }
        out->segments[out->segment_count++]=(dp_segment){lo,hi,p->p_flags};
        if(lo<out->lo)out->lo=lo;
        if(hi>out->hi)out->hi=hi;
    }
    out->headers_exact=out->segment_count>0 && out->lo<out->hi;
    return 0;
}
static bool query_object(unsigned index) {
    dp_object *object=&objects[index];
    void *held=object->handle?object->handle:object->safety_hold;
    if(!held)return false;
    struct link_map *map=0;
    dlerror();
    int raw=dlinfo(held,RTLD_DI_LINKMAP,&map);
    const char *err=dlerror();
    error_copy(object->query_error,sizeof object->query_error,err);
    object->query_raw=raw;
    object->query_used=raw;
    object->raw_map=map!=0;
    object->map=map;
    if(index==0 && mode_value==DP_QUERY_FAULT)object->query_used=-1;
    /* Raw actual success/map remains required, even in the consumption control. */
    if(raw!=0 || !map || err)return false;
    scan_context scan={.index=index,.map=map};
    dl_iterate_phdr(scan_object,&scan);
    object->matches=scan.matches;
    object->headers_exact=scan.found.headers_exact;
    object->base=scan.found.base;
    object->dynamic=scan.found.dynamic;
    object->lo=scan.found.lo;
    object->hi=scan.found.hi;
    object->segment_count=scan.found.segment_count;
    memcpy(object->segments,scan.found.segments,sizeof object->segments);
    bool accepted=!scan.invalid && scan.matches==1 && scan.found.headers_exact;
#ifndef DP_CONTROL_QUERY_CONSUMPTION
    accepted=accepted && object->query_used==0;
#endif
    object->provenance=accepted;
    return accepted;
}
bool dp_loader_query_all(void) {
    if(!loads_complete || query_attempted)return false;
    query_attempted=true;
    bool all=true;
    for(unsigned i=0;i<DP_OBJECTS;i++)if(!query_object(i))all=false;
    return all;
}
/* Logical fixture data names map to distinct exported A/B definitions.
 * This avoids duplicate instrumented global names without disabling ASan ODR checks. */
static const char *actual_symbol_name(unsigned index,const char *name,char buffer[80]) {
    static const char *data_names[]={"dp_mod_id","dp_mod_tag","dp_mod_tags","dp_mod_descriptor","dp_bad_descriptor","dp_constructor_count"};
    if(index<2)for(unsigned i=0;i<sizeof(data_names)/sizeof(data_names[0]);i++) {
        if(!strcmp(name,data_names[i])) {
            int n=snprintf(buffer,80,"dp_%c_%s",index?'b':'a',name+3);
            if(n<0 || n>=80)return "";
            return buffer;
        }
    }
    return name;
}
static const dp_elf_symbol *expected_symbol(unsigned index,const char *name) {
    char actual[80];
    name=actual_symbol_name(index,name,actual);
    for(unsigned i=0;i<dp_expected[index].symbol_count;i++)if(!strcmp(dp_expected[index].symbols[i].name,name))return &dp_expected[index].symbols[i];
    return 0;
}
bool dp_loader_covered(unsigned index,uintptr_t address,size_t length,unsigned flags) {
    if(index>=DP_OBJECTS || !length || length>UINTPTR_MAX-address)return false;
    const dp_object *object=&objects[index];
    uintptr_t end=address+length;
    /* Complete individual segment custody; caller min/max is not readable coverage. */
    for(unsigned i=0;i<object->segment_count;i++)if((object->segments[i].flags&flags)==flags && address>=object->segments[i].lo && end<=object->segments[i].hi)return true;
    return false;
}
static bool resolve(unsigned index,const char *name,uintptr_t *address,struct link_map **map,ElfW(Sym)*symbol) {
    char actual[80];
    name=actual_symbol_name(index,name,actual);
    if(query_count==96){query_overflow=true;
        return false;
        }
    symbol_query *q=&queries[query_count++];
    q->index=index;
    error_copy(q->name,sizeof q->name,name);
    void *held=objects[index].handle?objects[index].handle:objects[index].safety_hold;
    if(!held)return false;
    dlerror();
    void *pointer=dlsym(held,name);
    const char *err=dlerror();
    error_copy(q->error,sizeof q->error,err);
    q->address=(uintptr_t)pointer;
    if(err || !pointer)return false;
    Dl_info info;
    void *extra=0;
    q->map_rc=dladdr1(pointer,&info,&extra,RTLD_DL_LINKMAP);
    if(!q->map_rc || !extra)return false;
    *map=(struct link_map*)extra;
    q->map=(uintptr_t)*map;
    extra=0;
    q->sym_rc=dladdr1(pointer,&info,&extra,RTLD_DL_SYMENT);
    if(!q->sym_rc || !extra)return false;
    *symbol=*(const ElfW(Sym)*)extra;
    q->type=ELF64_ST_TYPE(symbol->st_info);
    q->value=symbol->st_value;
    q->size=symbol->st_size;
    *address=(uintptr_t)pointer;
    return true;
}
uintptr_t dp_loader_symbol(unsigned index,const char *name) {
    uintptr_t address=0;
    struct link_map *map=0;
    ElfW(Sym)symbol;
    return resolve(index,name,&address,&map,&symbol)?address:0;
}
bool dp_loader_symbol_exact(unsigned index,const char *name,unsigned type,unsigned flags,uintptr_t *out) {
    uintptr_t address=0,want=0;
    struct link_map *map=0;
    ElfW(Sym)symbol;
    const dp_elf_symbol *expected=expected_symbol(index,name);
    if(!expected || expected->type!=type || !expected->size ||
       !resolve(index,name,&address,&map,&symbol) || !add(objects[index].base,expected->value,&want))return false;
    bool okay=map==objects[index].map && address==want && ELF64_ST_TYPE(symbol.st_info)==type &&
        symbol.st_value==expected->value && symbol.st_size==expected->size &&
        dp_loader_covered(index,address,(size_t)expected->size,flags);
    if(okay && out)*out=address;
    return okay;
}
bool dp_loader_register(unsigned index) {
    dp_object *o=&objects[index];
    if(!o->provenance || index>1)return false;
    cc_status status=dp_core_register(index,index==0?"mod-a":"mod-b",100+index,o->lo,o->hi,true,false,o->handle!=0);
    if(status!=CC_OK)return false;
    o->range_registered=true;
    event(index,2);
    return true;
}
bool dp_loader_entry_decision(unsigned index) {
    uintptr_t address=0,want=0;
    struct link_map *map=0;
    ElfW(Sym)symbol;
    dp_object *o=&objects[index];
    if(!o->provenance || !resolve(index,"az_mod_v1_entry",&address,&map,&symbol))return false;
    o->entry_address=address;
    o->raw_entry_map=(uintptr_t)map;
    /* Independently validate the actual resolved harmless object, so omitting
     * the one combined intended-owner veto remains a safe dry decision. */
    unsigned actual=DP_OBJECTS;
    for(unsigned i=0;i<DP_OBJECTS;i++)if(objects[i].map==map)actual=i;
    if(actual==DP_OBJECTS)return false;
    const dp_elf_symbol *resolved=expected_symbol(actual,"az_mod_v1_entry");
    uintptr_t actual_want;
    if(!resolved || resolved->type!=STT_FUNC || !resolved->size ||
       !add(objects[actual].base,resolved->value,&actual_want) || address!=actual_want ||
       ELF64_ST_TYPE(symbol.st_info)!=STT_FUNC || symbol.st_value!=resolved->value || symbol.st_size!=resolved->size ||
       !dp_loader_covered(actual,address,(size_t)resolved->size,PF_R|PF_X))return false;
    const dp_elf_symbol *expected=expected_symbol(index,"az_mod_v1_entry");
    bool owner=map==o->map && expected && expected->type==STT_FUNC && expected->size &&
        add(o->base,expected->value,&want) && want==address &&
        expected->value==symbol.st_value && expected->size==symbol.st_size &&
        dp_loader_covered(index,address,(size_t)expected->size,PF_R|PF_X);
#ifdef DP_CONTROL_ENTRY_OWNER
    owner=true;
#endif
    o->entry_owned=owner;
    return owner;
}
static bool exact_data(unsigned index,const char *name,uintptr_t address,size_t bytes) {
    uintptr_t expected=0;
    const dp_elf_symbol *symbol=expected_symbol(index,name);
    return symbol && symbol->size==bytes && dp_loader_symbol_exact(index,name,STT_OBJECT,PF_R,&expected) && expected==address;
}
int dp_descriptor_validate(unsigned index,const az_mod_v1 *descriptor,void *context) {
    (void)context;
    dp_object *o=&objects[index];
    const char *name=mode_value==DP_DESCRIPTOR_BAD?"dp_bad_descriptor":"dp_mod_descriptor";
    if(!exact_data(index,name,(uintptr_t)descriptor,sizeof *descriptor))return false;
    o->descriptor_address=(uintptr_t)descriptor;
    o->descriptor_bounded=true;
    /* It is now safe under the fixed immutable fixture premise to read fields. */
    const dp_elf_symbol *id=expected_symbol(index,"dp_mod_id"),*tag=expected_symbol(index,"dp_mod_tag");
    if(!id || !tag || id->size<1 || id->size>33 || tag->size!=sizeof("fixture-fw") ||
       !exact_data(index,"dp_mod_id",(uintptr_t)descriptor->id,(size_t)id->size) ||
       !exact_data(index,"dp_mod_tags",(uintptr_t)descriptor->fw_tags,2*sizeof(char*)))return false;
    if(descriptor->id[id->size-1]!=0 || strnlen(descriptor->id,(size_t)id->size)!=id->size-1)return false;
    /* Bound is established before each array read. Exactly one permitted tag. */
    if(!dp_loader_covered(index,(uintptr_t)&descriptor->fw_tags[0],sizeof(char*),PF_R))return false;
    const char *tag_pointer=descriptor->fw_tags[0];
    if(!exact_data(index,"dp_mod_tag",(uintptr_t)tag_pointer,(size_t)tag->size) ||
       memcmp(tag_pointer,"fixture-fw",sizeof("fixture-fw")))return false;
    if(!dp_loader_covered(index,(uintptr_t)&descriptor->fw_tags[1],sizeof(char*),PF_R) || descriptor->fw_tags[1]!=0)return false;
    o->id_tags_bounded=true;
    uintptr_t init=0;
    if(!dp_loader_symbol_exact(index,"dp_init",STT_FUNC,PF_R|PF_X,&init) || init!=(uintptr_t)descriptor->init)return false;
    o->init_address=init;
    o->init_owned=true;
    return true;
}
cc_status dp_loader_run(unsigned index,cc_module_snapshot *out) {
    if(!objects[index].entry_owned)return CC_DESCRIPTOR_REFUSED;
    return dp_core_run(index,(dp_entry_fn)objects[index].entry_address,dp_descriptor_validate,0,out);
}
unsigned dp_constructor_local(unsigned index) {
    uintptr_t pointer=0;
    const char *name=index==3?"dp_dep_constructor_count":index==2?"dp_root_marker":"dp_constructor_count";
    if(!dp_loader_symbol_exact(index,name,STT_OBJECT,PF_R,&pointer))return UINT_MAX;
    return *(const unsigned*)pointer;
}
int dp_foreign_probe(const az_core_v1 *core) {
    uintptr_t address=0;
    if(!dp_loader_symbol_exact(1,"dp_foreign_claim",STT_FUNC,PF_R|PF_X,&address))return -99;
    return ((int(*)(const az_core_v1*,const char*))address)(core,"mod-a");
}
int dp_loader_foreign_late(void) {
    uintptr_t address=0;
    if(!dp_loader_symbol_exact(0,"dp_late_claim",STT_FUNC,PF_R|PF_X,&address))return -99;
    return ((int(*)(void))address)();
}
typedef struct { uintptr_t lo,hi;
    unsigned matches;
    bool invalid;
    } main_scan;
static int scan_main(struct dl_phdr_info *info,size_t size,void *data) {
    (void)size;
    main_scan *scan=data;
    if(info->dlpi_name && info->dlpi_name[0])return 0;
    ++scan->matches;
    for(unsigned i=0;i<info->dlpi_phnum;i++) {
        const ElfW(Phdr)*p=&info->dlpi_phdr[i];
        if(p->p_type!=PT_LOAD || !(p->p_flags&PF_X))continue;
        uintptr_t lo,hi;
        if(!p->p_memsz || p->p_filesz>p->p_memsz || !add(info->dlpi_addr,p->p_vaddr,&lo) || !add(lo,p->p_memsz,&hi) || lo>=hi){scan->invalid=true;
            return 0;
            }
        if(lo<scan->lo)scan->lo=lo;
        if(hi>scan->hi)scan->hi=hi;
    }return 0;
}
bool dp_main_envelope(uintptr_t *lo,uintptr_t *hi) {
    main_scan scan={.lo=UINTPTR_MAX};
    dl_iterate_phdr(scan_main,&scan);
    if(scan.invalid || scan.matches!=1 || scan.lo>=scan.hi)return false;
    *lo=scan.lo;
    *hi=scan.hi;
    return true;
}
static void json_text(FILE *f,const char *s) {
    fputc('"',f);
    for(;*s;s++){unsigned char c=(unsigned char)*s;
        if(c=='"' || c=='\\'){fputc('\\',f);
            fputc(c,f);
            }else if(c<32)fprintf(f,"\\u%04x",c);
        else fputc(c,f);
        }fputc('"',f);
}
void dp_loader_write_evidence(FILE *f) {
    fprintf(f,"{\"probes_complete\":%s,\"probe_count\":%u,\"preinit_zero\":%s,\"event_overflow\":%s,\"query_overflow\":%s,\"objects\":[",dp_probes_complete()?"true":"false",probe_count,preinit_zero?"true":"false",event_overflow?"true":"false",query_overflow?"true":"false");
    for(unsigned i=0;i<DP_OBJECTS;i++){dp_object *o=&objects[i];
        if(i)fputc(',',f);
        fprintf(f,"{\"index\":%u,\"handle\":%llu,\"safety_hold\":%llu,\"map\":%llu,\"base\":%llu,\"dynamic\":%llu,\"query_raw\":%d,\"query_used\":%d,\"raw_map\":%s,\"matches\":%u,\"headers_exact\":%s,\"provenance\":%s,\"lo\":%llu,\"hi\":%llu,\"range_registered\":%s,\"entry_address\":%llu,\"entry_map\":%llu,\"entry_owned\":%s,\"init_address\":%llu,\"descriptor_address\":%llu,\"descriptor_bounded\":%s,\"id_tags_bounded\":%s,\"init_owned\":%s,\"load_error\":",i,(unsigned long long)(uintptr_t)o->handle,(unsigned long long)(uintptr_t)o->safety_hold,(unsigned long long)(uintptr_t)o->map,(unsigned long long)o->base,(unsigned long long)o->dynamic,o->query_raw,o->query_used,o->raw_map?"true":"false",o->matches,o->headers_exact?"true":"false",o->provenance?"true":"false",(unsigned long long)o->lo,(unsigned long long)o->hi,o->range_registered?"true":"false",(unsigned long long)o->entry_address,(unsigned long long)o->raw_entry_map,o->entry_owned?"true":"false",(unsigned long long)o->init_address,(unsigned long long)o->descriptor_address,o->descriptor_bounded?"true":"false",o->id_tags_bounded?"true":"false",o->init_owned?"true":"false");
        json_text(f,o->load_error);
        fputs(",\"query_error\":",f);
        json_text(f,o->query_error);
        fputs(",\"segments\":[",f);
        for(unsigned j=0;j<o->segment_count;j++){if(j)fputc(',',f);
            fprintf(f,"{\"lo\":%llu,\"hi\":%llu,\"flags\":%u}",(unsigned long long)o->segments[j].lo,(unsigned long long)o->segments[j].hi,o->segments[j].flags);
            }fputs("]}",f);
    }
    fputs("],\"events\":[",f);
    for(unsigned i=0;i<event_count;i++){if(i)fputc(',',f);
        fprintf(f,"{\"index\":%u,\"event\":%u,\"session\":%u}",events[i].index,events[i].event,events[i].session);
        }fputs("],\"symbols\":[",f);
    for(unsigned i=0;i<query_count;i++){symbol_query*q=&queries[i];
        if(i)fputc(',',f);
        fprintf(f,"{\"index\":%u,\"name\":",q->index);
        json_text(f,q->name);
        fprintf(f,",\"address\":%llu,\"map\":%llu,\"map_rc\":%d,\"sym_rc\":%d,\"type\":%u,\"value\":%llu,\"size\":%llu,\"error\":",(unsigned long long)q->address,(unsigned long long)q->map,q->map_rc,q->sym_rc,q->type,(unsigned long long)q->value,(unsigned long long)q->size);
        json_text(f,q->error);
        fputc('}',f);
        }fputs("]}",f);
}
