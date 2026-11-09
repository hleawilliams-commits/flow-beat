#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate a separately named offline receipt successor from two exact SDK inputs.
No compiler, installer, subprocess or runtime import is performed here.
"""
import argparse
import difflib
import hashlib
import json
import re
from pathlib import Path

PINS = {
    "inline_hooks.c": "f141b383c25e66c8acb1839280832a612a6ee0e25e9ada59f374ccbef8f81908",
    "azprot.h": "0f1cc9c5d4ca212f798bf2e9d74a69507a82ce5cc59bd328bacc46816a119b33",
}
def sha(data):
    return hashlib.sha256(data).hexdigest()

def function(text, name):
    # Match one static function definition; lex braces without treating comments
    # or string/character literals as syntax. Inputs are independently pinned.
    matches = list(re.finditer(r"^static [^\n]*\b" + re.escape(name) + r"\(", text, re.M))
    if len(matches) != 1:
        raise ValueError("nonunique function definition: " + name)
    start = matches[0].start()
    begin = text.index("{", matches[0].end())
    depth, mode, escaped = 0, "code", False
    i = begin
    while i < len(text):
        c, nxt = text[i], text[i:i + 2]
        if mode == "line":
            if c == "\n": mode = "code"
        elif mode == "block":
            if nxt == "*/": mode = "code"; i += 1
        elif mode in ("string", "char"):
            if escaped: escaped = False
            elif c == "\\": escaped = True
            elif c == ('"' if mode == "string" else "'"): mode = "code"
        elif nxt == "//": mode = "line"; i += 1
        elif nxt == "/*": mode = "block"; i += 1
        elif c == '"': mode = "string"
        elif c == "'": mode = "char"
        elif c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0: return start, i + 1, text[start:i + 1]
        i += 1
    raise ValueError("unclosed function " + name)

def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("nonunique transformation anchor: " + old[:80])
    return text.replace(old, new, 1)

QUERY = r'''static int azprot_query(uintptr_t a, uintptr_t b) {
    rc_query *q = rc_current_protection ? &rc_current_protection->query : NULL;
    rc_current_query = q;
    int fd = rc_open_maps(AZPROT_MAPS, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (q) { q->parser_result = -1; q->returned = true; }
        rc_current_query = NULL;
        return -1;
    }
    int p = azprot_query_fd(fd, a, b);
    if (q) { q->parser_result = p; q->returned = true; }
    (void)rc_close_maps(fd); /* Exact close outcome retained; no inferred release. */
    rc_current_query = NULL;
    return p;
}'''
UNLOCK = r'''static int azprot_unlock(uintptr_t addr, size_t len, azprot_t *t) {
    rc_protection local = {0};
    rc_protection *p = rc_current_protection ? rc_current_protection : &local;
    bool injected = protection_fault(p, false);
    begin(&p->unlock, injected);
    if (injected) {
        errno = EIO;
        returned(&p->unlock, -1, true, EIO);
        return -1;
    }
    long pg = page_size(&p->page_size);
    uintptr_t base = 0;
    size_t rounded = 0;
    /* One actual sysconf value governs both compatibility and checked rounding.
     * The retained 4096-byte trampoline recipe is not generalized to other pages. */
    if (!t || pg != 4096 || !rc_checked_pages(addr, len, pg, &base, &rounded)) {
        returned(&p->unlock, -1, false, 0);
        return -1;
    }
    if (protection_mutex(&g_azprot_mu, &p->mutex_lock, false) != 0) {
        returned(&p->unlock, -1, false, 0);
        return -1;
    }
    int prot = azprot_query(base, base + rounded);
    int rc = -1;
    if (prot >= 0)
        rc = rc_protect_call((void *)base, rounded, prot | PROT_READ | PROT_WRITE,
                             &p->make_writable, false);
    if (prot < 0 || rc != 0) {
        bool valid_errno = p->make_writable.errno_valid;
        int error = p->make_writable.error;
        (void)protection_mutex(&g_azprot_mu, &p->mutex_release, true);
        returned(&p->unlock, -1, valid_errno, error);
        return -1;
    }
    t->base = base; t->len = rounded; t->prot = prot;
    p->base = base; p->length = rounded; p->saved_bits = prot;
    p->token_valid = true;
    returned(&p->unlock, 0, false, 0);
    return 0;
}'''
RELOCK = r'''static int azprot_relock(const azprot_t *t) {
    rc_protection local = {0};
    rc_protection *p = rc_current_protection ? rc_current_protection : &local;
    int rc = rc_protect_call((void *)t->base, t->len, t->prot, &p->relock,
                             protection_fault(p, true));
    (void)protection_mutex(&g_azprot_mu, &p->mutex_release, true);
    return rc == 0 ? 0 : -1;
}'''

def transform(inline, prot):
    edits = []
    def replace_function(text, name, body, source):
        start, end, old = function(text, name)
        edits.append({"source": source, "function": name,
                      "before_sha256": sha(old.encode()), "after_sha256": sha(body.encode())})
        return text[:start] + body + text[end:]

    # Exact parsing algorithm retained. Only the read boundary is routed to a
    # real syscall receipt wrapper with a declared conservative read-count cap.
    prot = once(prot, "while ((r = read(fd, buf, sizeof buf)) > 0)",
                "while ((r = rc_read_maps(fd, buf, sizeof buf)) > 0)")
    prot = replace_function(prot, "azprot_query", QUERY, "azprot.h")
    prot = replace_function(prot, "azprot_unlock", UNLOCK, "azprot.h")
    prot = replace_function(prot, "azprot_relock", RELOCK, "azprot.h")
    inline = once(inline, '#include "../common/azprot.h"',
                  '#include "azprot-receipt.h"')
    _, _, allocator = function(inline, "azm_island_slot")
    allocator = once(allocator,
        "mmap((void *) hint, AZM_ISLAND_BYTES, PROT_READ | PROT_EXEC,\n"
        "                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)",
        "rc_map_call((void *)hint, AZM_ISLAND_BYTES, PROT_READ | PROT_EXEC, RC_MAP_ISLAND)")
    allocator = once(allocator, "if (azm_reaches(FUNC, (uintptr_t) m)) {",
                     "if (azm_reaches(FUNC, (uintptr_t)m) && rc_accept_candidate(m)) {")
    allocator = once(allocator, "munmap(m, AZM_ISLAND_BYTES);",
                     "(void)rc_unmap_call(m, AZM_ISLAND_BYTES);")
    allocator = once(allocator,
        "return g_azm_island[k].base + 4 * g_azm_island[k].used++;",
        "return rc_slot_observed((unsigned)k, (unsigned)g_azm_island[k].used,\n"
        "                g_azm_island[k].base + 4 * g_azm_island[k].used++);")
    # Avoid unsequenced used read/increment: record slot, increment, then return.
    allocator = once(allocator,
        "if (azm_reaches(FUNC, (uintptr_t) g_azm_island[k].base) &&\n"
        "            g_azm_island[k].used < AZM_ISLAND_BYTES / 16)\n"
        "            return rc_slot_observed((unsigned)k, (unsigned)g_azm_island[k].used,\n"
        "                g_azm_island[k].base + 4 * g_azm_island[k].used++);",
        "if (azm_reaches(FUNC, (uintptr_t)g_azm_island[k].base) &&\n"
        "            g_azm_island[k].used < AZM_ISLAND_BYTES / 16) {\n"
        "            unsigned slot = (unsigned)g_azm_island[k].used++;\n"
        "            return rc_slot_observed((unsigned)k, slot, g_azm_island[k].base + 4 * slot);\n"
        "        }")
    inline = replace_function(inline, "azm_island_slot", allocator, "inline_hooks.c")

    _, _, seal = function(inline, "azm_tramp_seal")
    seal = once(seal, "    __builtin___clear_cache",
                "    rc_active->trampoline_cache_armed = true;\n    __builtin___clear_cache")
    seal = once(seal, "    if (mprotect(tramp, 4096, PROT_READ | PROT_EXEC) != 0) {",
                "    rc_active->trampoline_cache_returned = true;\n"
                "    if (rc_seal_call(tramp, 4096, PROT_READ | PROT_EXEC) != 0) {")
    seal = once(seal, "munmap(tramp, 4096);", "(void)rc_unmap_call(tramp, 4096);")
    inline = replace_function(inline, "azm_tramp_seal", seal, "inline_hooks.c")
    inline = once(inline,
        " * call it. NULL (and nothing mapped) when the page cannot be made executable. */",
        " * call it. Successor records failed cleanup separately; NULL does not prove unmapping. */")

    _, _, installer = function(inline, "azm_install_hook_at")
    fallback = installer.index("    /* no island in reach, constructor phase:")
    installer = installer[:fallback] + "    return azm_rec(FUNC, name, AZM_DECLINED);\n}"
    installer = once(installer, "    if (!FUNC || (FUNC & 3))",
                     "    rc_active->installer_entered = true;\n    if (!FUNC || (FUNC & 3))")
    installer = once(installer, "    uint32_t *func = (uint32_t *) FUNC;",
                     "    uint32_t *func = (uint32_t *)FUNC;\n"
                     "    memcpy(rc_active->emission.source_words, func, 16);")
    installer = once(installer, "int ncopy = isl ? 1 : 4, out = 0;",
        "int ncopy = 1, out = 0;\n    rc_active->emission.ncopy = (unsigned)ncopy;")
    installer = once(installer, "if (!isl && __atomic_load_n(&g_azm_late, __ATOMIC_ACQUIRE)) {",
                     "if (!isl) {\n        rc_active->status = RC_NO_ISLAND;")
    # The late-only successor never reaches the original fallback validation/write.
    start = installer.index("    if (!isl && !azm_patch_ok(func, 4)) {")
    end = installer.index("    uint32_t *tramp", start)
    installer = installer[:start] + installer[end:]
    installer = once(installer,
        "uint32_t *tramp = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);",
        "rc_active->status = RC_MAPPING;\n"
        "    uint32_t *tramp = rc_map_call(NULL, 4096, PROT_READ | PROT_WRITE, RC_MAP_TRAMPOLINE);")
    installer = once(installer, "    /* Copy the instructions the patch will cover",
        "    if (!rc_prewrite_trampoline(tramp)) {\n"
        "        rc_active->status = RC_STORAGE;\n"
        "        (void)rc_unmap_call(tramp, 4096);\n"
        "        return azm_rec(FUNC, name, AZM_DECLINED);\n"
        "    }\n"
        "    /* Copy the instructions the patch will cover")
    installer = once(installer, "    if (!azm_tramp_seal(tramp, out + 4))",
        "    if (!rc_prepare_metadata(FUNC, hook, orig, isl, tramp, ncopy, out)) {\n"
        "        rc_active->status = RC_STORAGE;\n"
        "        (void)rc_unmap_call(tramp, 4096);\n"
        "        return azm_rec(FUNC, name, AZM_DECLINED);\n"
        "    }\n"
        "    rc_active->status = RC_SEAL;\n"
        "    if (!azm_tramp_seal(tramp, out + 4))")
    installer = once(installer, "    azprot_t pt;",
                     "    azprot_t island_token = {0}, target_token = {0};")
    installer = once(installer, "        if (azprot_unlock((uintptr_t) isl, 16, &pt) != 0) {",
        "        rc_active->status = RC_ISLAND_UNLOCK;\n"
        "        rc_current_protection = &rc_active->island_protection;\n"
        "        if (azprot_unlock((uintptr_t)isl, 16, &island_token) != 0) {")
    installer = once(installer, "        azm_abs_jump(isl, (uint64_t) (uintptr_t) hook);",
        "        azm_abs_jump(isl, (uint64_t)(uintptr_t)hook);\n"
        "        rc_active->island_cache_armed = true;")
    if installer.count("        if (azprot_relock(&pt) != 0)") != 2:
        raise ValueError("two selected relock sites")
    installer = installer.replace("        if (azprot_relock(&pt) != 0)",
        "        rc_active->island_cache_returned = true;\n"
        "        if (azprot_relock(&island_token) != 0)", 1)
    installer = once(installer, "        if (azprot_unlock(FUNC, 4, &pt) != 0) {",
        "        if (!rc_validate_permit(FUNC, hook, orig)) {\n"
        "            rc_active->status = RC_PERMIT;\n"
        "            (void)rc_unmap_call(tramp, 4096);\n"
        "            return azm_rec(FUNC, name, AZM_DECLINED);\n"
        "        }\n"
        "        rc_active->permit_validated = true;\n"
        "        CHECKPOINT(RC_TEST_BEFORE_UNLOCK, active_binding, rc_active);\n"
        "        rc_active->status = RC_TARGET_UNLOCK;\n"
        "        rc_current_protection = &rc_active->target_protection;\n"
        "        if (azprot_unlock(FUNC, 4, &target_token) != 0) {")
    cut_old = """        __atomic_store_n(orig, (void *) tramp, __ATOMIC_RELAXED);
        __atomic_store_n(func, b, __ATOMIC_RELEASE); /* the one store that redirects it */"""
    cut_new = """        /* Fixed no-refusal cut: metadata, exact plain pointer, actual branch.
         * All bindings/storage/permit checks occurred before successful unlock.
         * Instrumentation below only reads/writes already-bound fixed storage. */
#ifdef RC_CONTROL_ORIGINAL_BEFORE_METADATA
        __atomic_store_n(orig, (void *)tramp, __ATOMIC_RELAXED);
        rc_active->original_saw_metadata =
            atomic_load_explicit(&records[active_binding].published, memory_order_acquire) != 0;
        atomic_store_explicit(&records[active_binding].published, 1u, memory_order_release);
        rc_active->original_order = 1; rc_active->metadata_order = 2;
#else
        atomic_store_explicit(&records[active_binding].published, 1u, memory_order_release);
        __atomic_store_n(orig, (void *)tramp, __ATOMIC_RELAXED);
        rc_active->original_saw_metadata =
            atomic_load_explicit(&records[active_binding].published, memory_order_acquire) != 0;
        rc_active->metadata_order = 1; rc_active->original_order = 2;
#endif
        rc_active->metadata_published = true;
        rc_active->original_published = true;
        __atomic_store_n(func, b, __ATOMIC_RELEASE);
        rc_active->redirect_saw_original =
            __atomic_load_n(orig, __ATOMIC_RELAXED) == (void *)tramp;
        rc_active->redirect_published = true;
        rc_active->redirect_order = 3;
        /* End cut. No caller callback before the actual target branch store. */
        rc_active->target_cache_armed = true;"""
    installer = once(installer, cut_old, cut_new)
    installer = once(installer, "        if (azprot_relock(&pt) != 0)",
        "        rc_active->target_cache_returned = true;\n"
        "        if (azprot_relock(&target_token) != 0)")
    installer = once(installer, "        azm_note_hooked(FUNC);",
        "        rc_active->status = (rc_active->island_protection.relock.result != 0 ||\n"
        "                             rc_active->target_protection.relock.result != 0)\n"
        "                            ? RC_WARNINGS : RC_OK;\n"
        "#ifdef RC_CONTROL_TOKEN_EVIDENCE_ALIAS\n"
        "        rc_active->island_protection.base = rc_active->target_protection.base;\n"
        "        rc_active->island_protection.length = rc_active->target_protection.length;\n"
        "        rc_active->island_protection.saved_bits = rc_active->target_protection.saved_bits;\n"
        "#endif\n"
        "#ifdef RC_CONTROL_WARNING_AS_REFUSAL\n"
        "        if (rc_active->status == RC_WARNINGS) rc_active->status = RC_TARGET_UNLOCK;\n"
        "#endif\n"
        "        CHECKPOINT(RC_TEST_AFTER_CUT, active_binding, rc_active);\n"
        "        azm_note_hooked(FUNC);")
    # Two selected-path cleanup statements are still the literal upstream calls.
    if installer.count("            munmap(tramp, 4096);") != 2:
        raise ValueError("selected-path cleanup anchors")
    installer = installer.replace("            munmap(tramp, 4096);",
                                  "            (void)rc_unmap_call(tramp, 4096);")
    inline = replace_function(inline, "azm_install_hook_at", installer, "inline_hooks.c")
    return inline, prot, edits

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inline", dest="inline_path", type=Path, required=True)
    parser.add_argument("--azprot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    originals = {"inline_hooks.c": args.inline_path.read_bytes(), "azprot.h": args.azprot.read_bytes()}
    for name, data in originals.items():
        if sha(data) != PINS[name]: raise ValueError("source pin: " + name)
    original_inline = originals["inline_hooks.c"].decode()
    original_prot = originals["azprot.h"].decode()
    inline, prot, edits = transform(original_inline, original_prot)
    preserved = []
    for name in ("azm_pcrel_kind", "azm_adrp_target", "azm_emit_abs_addr",
                 "azm_adrp_reloc_ok", "azm_reloc_adrp", "azm_abs_jump",
                 "azm_reaches", "azm_ends_fn", "azm_patch_ok"):
        before = function(original_inline, name)[2].encode()
        after = function(inline, name)[2].encode()
        if before != after: raise ValueError("changed preserved function: " + name)
        preserved.append({"name": name, "bytes": len(before), "sha256": sha(before)})
    before = function(original_prot, "azprot_parse_line")[2].encode()
    if before != function(prot, "azprot_parse_line")[2].encode():
        raise ValueError("changed maps parser")
    preserved.append({"name": "azprot_parse_line", "bytes": len(before), "sha256": sha(before)})
    outputs = {"inline-receipt.inc": inline.encode(), "azprot-receipt.h": prot.encode()}
    for before_name, after_name, patch_name in (
        ("inline_hooks.c", "inline-receipt.inc", "inline-receipt.patch"),
        ("azprot.h", "azprot-receipt.h", "azprot-receipt.patch")):
        outputs[patch_name] = "".join(difflib.unified_diff(
            originals[before_name].decode().splitlines(keepends=True),
            outputs[after_name].decode().splitlines(keepends=True),
            fromfile=before_name, tofile=after_name)).encode()
    ledger = {
        "schema": 1, "version": "offline-receipts-0.1",
        "scope": "Transformed orchestration, bounded syscall receipts, late-island-only; no execution",
        "sources": {n: {"bytes": len(b), "sha256": sha(b)} for n, b in originals.items()},
        "outputs": {n: {"bytes": len(b), "sha256": sha(b)} for n, b in outputs.items()},
        "preserved_functions": preserved, "function_edits": edits,
        "other_edits": ["azprot_query_fd read boundary", "receipt header include",
                        "seal cleanup comment", "fallback removed from selected installer"]
    }
    outputs["transform-ledger.json"] = (json.dumps(ledger, indent=2, sort_keys=True) + "\n").encode()
    args.output.mkdir(parents=True, exist_ok=False)
    for name, data in outputs.items():
        with (args.output / name).open("xb") as stream: stream.write(data)
    print(json.dumps({"status": "transformed", "files": len(outputs),
                      "ledger_sha256": sha(outputs["transform-ledger.json"])}))
if __name__ == "__main__":
    main()
