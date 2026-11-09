/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DP_LOADER_H
#define DP_LOADER_H
#include "core.h"
#include "attribution.h"
#include <link.h>
#include <stdio.h>
#define DP_OBJECTS 4u
#define DP_PHDRS 24u
#define DP_EVENTS 64u
typedef struct{uint32_t type,flags;uint64_t vaddr,memsz,filesz,offset,align;} dp_elf_phdr;
typedef struct{const char *name;uint64_t value,size;unsigned type;} dp_elf_symbol;
typedef struct{const char *file;const dp_elf_phdr *phdrs;unsigned phnum;const dp_elf_symbol *symbols;unsigned symbol_count;} dp_elf_manifest;
typedef struct { uintptr_t lo,hi;unsigned flags; } dp_segment;
typedef struct {
    void *handle,*safety_hold;struct link_map *map;
    int query_raw,query_used;bool raw_map,raw_provenance,provenance,headers_exact;
    unsigned matches,segment_count;dp_segment segments[DP_PHDRS];
    uintptr_t base,dynamic,lo,hi;char load_error[160],query_error[160];
    bool entry_owned,descriptor_bounded,id_tags_bounded,init_owned,range_registered;
    uintptr_t entry_address,init_address,descriptor_address,raw_entry_map;
} dp_object;
typedef struct { unsigned index,event,session; } dp_event;
void dp_loader_initialize(unsigned mode);
bool dp_loader_load_all(const char *directory);
bool dp_loader_query_all(void);
bool dp_loader_register(unsigned index);
cc_status ma_loader_register(unsigned index, bool hooks, bool firmware_any, bool dry);
typedef struct { unsigned index, session, ordinal; uintptr_t raw_ra; int owner; bool handle_present; } ma_constructor_observation;
unsigned ma_constructor_count(void);
const ma_constructor_observation *ma_constructor_at(unsigned index);
bool dp_loader_entry_decision(unsigned index);
cc_status dp_loader_run(unsigned index,cc_module_snapshot *out);
int dp_descriptor_validate(unsigned index,const az_mod_v1*,void*);
const dp_object *dp_loader_object(unsigned index);
uintptr_t dp_loader_symbol(unsigned index,const char *name);
bool dp_loader_symbol_exact(unsigned index,const char *name,unsigned type,unsigned flags,uintptr_t *out);
bool dp_loader_covered(unsigned index,uintptr_t address,size_t length,unsigned flags);
bool dp_main_envelope(uintptr_t *lo,uintptr_t *hi);
unsigned dp_event_count(void);const dp_event *dp_event_at(unsigned i);
bool dp_probes_complete(void);bool dp_preinit_sessions_zero(void);
unsigned dp_constructor_local(unsigned index);
int dp_loader_foreign_late(void);
void dp_loader_write_evidence(FILE *out);
#endif
