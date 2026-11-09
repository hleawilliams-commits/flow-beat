/* SPDX-License-Identifier: GPL-3.0-only */
/* Included once by core.c after the retained receipt and selected SDK helpers.
 * New static module payloads are disjoint from mutable lifecycle diagnostics. */
_Static_assert(CC_MODULES == MA_MODULES && CC_ID_BYTES == 33, "fixed owner bounds");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "bounded publication scalar");
#if (defined(MA_CONTROL_STOP_AT_HOLE) || defined(MA_CONTROL_RAW_QUERY) || \
     defined(MA_CONTROL_OMIT_DESCRIPTOR_PUBLICATION) || defined(MA_CONTROL_CLEAR_INIT) || \
     defined(MA_CONTROL_EXEC_ONLY) || defined(MA_CONTROL_GENERATED_FIRST) || \
     defined(MA_CONTROL_OMIT_PROCESS) || defined(MA_CONTROL_OMIT_ID_COPY)) && !defined(MA_FIXTURE)
#error "new safe controls require the declared offline fixture"
#endif
#if (defined(MA_CONTROL_GENERATED_FIRST) || defined(MA_CONTROL_OMIT_PROCESS) || \
     defined(MA_CONTROL_OMIT_ID_COPY)) && !defined(MA_MODEL)
#error "synthetic controls require the separately compiled model path"
#endif
typedef struct {
    _Alignas(4) _Atomic unsigned committed;
    unsigned index;
    uint64_t process_id;
    uintptr_t lo, hi;
    char id[33];
} ma_module;
static ma_module ma_modules[MA_MODULES];
/* Only the writer consults these stage bits; readers never inspect them. */
static bool ma_attempted[MA_MODULES], ma_staged[MA_MODULES], ma_commit_used[MA_MODULES];
static bool ma_copy_id(const char *id, char out[33]) {
    if (!id) return false;
    size_t n = strnlen(id, 33);
    if (!n || n >= 33) return false;
    memset(out, 0, 33);
    memcpy(out, id, n);
    return cm_id_ok(out) != 0;
}
static bool ma_stage_locked(unsigned index, const char copied_id[33],
                            uint64_t process_id, uintptr_t lo, uintptr_t hi,
                            bool model) {
    if (index >= MA_MODULES || !process_id || hi <= lo || ma_attempted[index])
        return false;
    if (!model) {
        for (unsigned i = 0; i < MA_MODULES; ++i)
            if (ma_staged[i] && !memcmp(ma_modules[i].id, copied_id, 33))
                return false;
    }
    ma_attempted[index] = true;
    ma_module *m = &ma_modules[index];
    m->index = index; m->process_id = process_id; m->lo = lo; m->hi = hi;
#ifdef MA_CONTROL_OMIT_ID_COPY
    if (!model) memcpy(m->id, copied_id, 33);
#else
    memcpy(m->id, copied_id, 33);
#endif
    ma_staged[index] = true;
    return true;
}
__attribute__((noinline)) static bool ma_commit_locked(unsigned index) {
    if (index >= MA_MODULES || !ma_staged[index] || ma_commit_used[index] ||
        atomic_load_explicit(&ma_modules[index].committed, memory_order_acquire))
        return false;
    ma_commit_used[index] = true;
    atomic_store_explicit(&ma_modules[index].committed, 1u, memory_order_release);
    return true;
}
static bool ma_storage_overlaps(rc_span value) {
    for (unsigned i = 0; i < MA_MODULES; ++i) {
        if (!atomic_load_explicit(&ma_modules[i].committed, memory_order_acquire)) continue;
        if (overlap(value, (rc_span){ma_modules[i].lo, ma_modules[i].hi})) return true;
    }
    return false;
}
__attribute__((noinline)) bool ma_module_snapshot(unsigned index, ma_module_value *out) {
    if (!out || index >= MA_MODULES) return false;
    memset(out, 0, sizeof *out);
    if (!atomic_load_explicit(&ma_modules[index].committed, memory_order_acquire))
        return false;
    const ma_module *m = &ma_modules[index];
    out->committed = true; out->index = m->index; out->process_id = m->process_id;
    out->lo = m->lo; out->hi = m->hi;
    memcpy(out->id, m->id, sizeof out->id);
    return true;
}
__attribute__((noinline)) static int ma_generated_index(uintptr_t address) {
    rc_metadata metadata;
    if (!rc_lookup(address, &metadata) || metadata.owner.kind != RC_OWNER_COMMUNITY)
        return -1;
    int found = -1;
    for (unsigned i = 0; i < MA_MODULES; ++i) {
        if (!atomic_load_explicit(&ma_modules[i].committed, memory_order_acquire)) continue;
        const ma_module *m = &ma_modules[i];
#ifndef MA_CONTROL_OMIT_PROCESS
        if (m->process_id != metadata.owner.process_id) continue;
#endif
        if (memcmp(m->id, metadata.owner.id, 33)) continue;
        if (found >= 0) return -1;
        found = (int)i;
    }
    return found;
}
__attribute__((noinline)) int ma_owner_index(uintptr_t address) {
#ifdef MA_CONTROL_GENERATED_FIRST
    int generated = ma_generated_index(address);
    if (generated >= 0) return generated;
#endif
    for (unsigned i = 0; i < MA_MODULES; ++i) {
        if (!atomic_load_explicit(&ma_modules[i].committed, memory_order_acquire)) {
#ifdef MA_CONTROL_STOP_AT_HOLE
            break;
#else
            continue;
#endif
        }
        const ma_module *m = &ma_modules[i];
        if (m->hi && address >= m->lo && address < m->hi) return (int)i;
    }
    return ma_generated_index(address);
}
/* Pure modeled dependencies for the exact consumer. All observations are serial
 * within a fresh selector; no signal-handler or concurrent-ledger claim. */
typedef struct { uintptr_t address, value; } ma_cell;
static ma_cell ma_cells[MA_CELLS];
static unsigned ma_cell_count;
static ma_observations ma_obs;
static int ma_open_result = 7, ma_write_mode, ma_sync_result, ma_close_result;
bool ma_model_add_frame(uintptr_t fp, uintptr_t next, uintptr_t ra) {
    if (fp > UINTPTR_MAX - 8 || ma_cell_count > MA_CELLS - 2) {
        ma_obs.violation = true; return false;
    }
    for (unsigned i = 0; i < ma_cell_count; ++i)
        if (ma_cells[i].address == fp || ma_cells[i].address == fp + 8) {
            ma_obs.violation = true; return false;
        }
    ma_cells[ma_cell_count++] = (ma_cell){fp, next};
    ma_cells[ma_cell_count++] = (ma_cell){fp + 8, ra};
    return true;
}
void ma_model_io_config(int open_result, int write_mode, int sync_result, int close_result) {
    if (ma_obs.peek_count || ma_obs.io_count || ma_obs.unblamed ||
        write_mode < 0 || write_mode > 2) { ma_obs.violation = true; return; }
    ma_open_result = open_result; ma_write_mode = write_mode;
    ma_sync_result = sync_result; ma_close_result = close_result;
}
const ma_observations *ma_model_observations(void) { return &ma_obs; }
static int ma_frame_read(uintptr_t address, uintptr_t *out) {
    if (ma_obs.peek_count == MA_PEEKS) { ma_obs.violation = true; return 0; }
    ma_peek_event *event = &ma_obs.peeks[ma_obs.peek_count++];
    event->address = address; event->success = false;
    for (unsigned i = 0; i < ma_cell_count; ++i)
        if (ma_cells[i].address == address) {
            *out = ma_cells[i].value; event->success = true; return 1;
        }
    return 0;
}
static void ma_note_unblamed(void) { ++ma_obs.unblamed; }
static void ma_note_io(int kind, int fd, size_t length, ssize_t result) {
    if (ma_obs.io_count == MA_IO_CALLS) { ma_obs.violation = true; return; }
    ma_obs.io[ma_obs.io_count++] = (ma_io_event){kind, fd, length, result};
}
#define MA_BLAME_PATH "offline-authored-blame"
static int ma_file_open(const char *path, int flags, unsigned mode) {
    ma_obs.path_matches = !strcmp(path, MA_BLAME_PATH);
    ma_obs.open_flags = flags; ma_obs.open_mode = mode;
    ma_note_io(1, -1, 0, ma_open_result); return ma_open_result;
}
static ssize_t ma_file_write(int fd, const void *bytes, size_t length) {
    ma_obs.write_requested = length;
    if (length > sizeof ma_obs.payload) { ma_obs.violation = true; return -1; }
    memcpy(ma_obs.payload, bytes, length); ma_obs.payload_length = length;
    ssize_t result = ma_write_mode == 1 ? -1 :
        ma_write_mode == 2 ? (ssize_t)length - 1 : (ssize_t)length;
    ma_obs.write_returned = result; ma_note_io(2, fd, length, result); return result;
}
static int ma_file_fsync(int fd) { ma_note_io(3, fd, 0, ma_sync_result); return ma_sync_result; }
static int ma_file_close(int fd) { ma_note_io(4, fd, 0, ma_close_result); return ma_close_result; }
/* Only lexical dependency substitutions surround the exact 962 retained bytes. */
#define cm_blame_fault ma_blame_fault
#define cm_owner_of ma_owner_index
#define g_cm ma_modules
#define crash_peek8 ma_frame_read
#define cm_note_unblamed_crash ma_note_unblamed
#define CM_BLAME MA_BLAME_PATH
#define open ma_file_open
#define write ma_file_write
#define fsync ma_file_fsync
#define close ma_file_close
#include "pinned-consumer.inc"
#undef close
#undef fsync
#undef write
#undef open
#undef CM_BLAME
#undef cm_note_unblamed_crash
#undef crash_peek8
#undef g_cm
#undef cm_owner_of
#undef cm_blame_fault
const char *ma_consume_fault(uintptr_t pc, uintptr_t lr, uintptr_t fp) {
    return ma_blame_fault(pc, lr, fp);
}
#ifdef MA_MODEL
bool ma_model_stage(unsigned index, const char *id, uint64_t process_id,
                    uintptr_t lo, uintptr_t hi) {
    char copied_id[33];
    if (!ma_copy_id(id, copied_id) || !cc_both(index)) return false;
    bool result = ma_stage_locked(index, copied_id, process_id, lo, hi, true);
    return cc_release_both() && result;
}
bool ma_model_commit(unsigned index) {
    if (!cc_both(index)) return false;
    bool result = ma_commit_locked(index);
    return cc_release_both() && result;
}
bool ma_model_record(unsigned binding, const rc_metadata *input) {
    if (!input || binding >= RC_BINDINGS) return false;
    rc_metadata value = *input;
    value.binding = binding; /* Authored slot index, never a module identity. */
    if ((value.owner.kind != RC_OWNER_CORE && value.owner.kind != RC_OWNER_COMMUNITY) ||
        !value.owner.process_id || value.island_instructions.end <= value.island_instructions.begin ||
        value.trampoline_instructions.end <= value.trampoline_instructions.begin) return false;
    if (value.owner.kind == RC_OWNER_COMMUNITY) {
        char copied_id[33];
        if (!ma_copy_id(value.owner.id, copied_id)) return false;
        memcpy(value.owner.id, copied_id, 33);
    } else memset(value.owner.id, 0, 33);
    if (!cc_both(binding)) return false;
    bool result = !records[binding].attempt.admitted &&
        !atomic_load_explicit(&records[binding].published, memory_order_acquire);
    if (result) {
        records[binding].immutable = value;
        atomic_store_explicit(&records[binding].published, 1u, memory_order_release);
    }
    return cc_release_both() && result;
}
bool ma_model_hold_locks(void) { return cc_both(MA_MODULES); }
bool ma_model_release_locks(void) { return cc_release_both(); }
#endif
