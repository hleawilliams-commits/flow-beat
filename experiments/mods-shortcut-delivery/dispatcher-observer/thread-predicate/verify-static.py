#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the exact firmware's dispatcher owner-thread predicate, read-only.

No process memory, native calls, hook-policy tests, or emulator/device access.
Output contains semantic relationships and hashes, not firmware dumps.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from elftools.elf.elffile import ELFFile

FIRMWARE = "6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925"
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--elf", type=Path, required=True)
args = ap.parse_args()
digest = lambda data: hashlib.sha256(data).hexdigest()
raw = args.elf.read_bytes()
assert digest(raw) == FIRMWARE, "Different firmware; no fallback"
elf = ELFFile(io.BytesIO(raw))
assert elf.elfclass == 64 and elf.little_endian
assert elf["e_machine"] == "EM_AARCH64" and elf["e_type"] == "ET_EXEC"


def read(address, size):
    for seg in elf.iter_segments():
        if (seg["p_type"] == "PT_LOAD" and seg["p_vaddr"] <= address
                and address + size <= seg["p_vaddr"] + seg["p_filesz"]):
            offset = seg["p_offset"] + address - seg["p_vaddr"]
            return raw[offset:offset + size]
    raise AssertionError("Address is not file-backed")


decoder = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)


def check_block(start, expected):
    actual = [(i.mnemonic, i.op_str)
              for i in decoder.disasm(read(start, 4 * len(expected)), start)]
    assert actual == expected, hex(start)
    return len(expected)


count = 0
# Both dispatcher entries load the same shared taskbox and call slot +0x28.
count += check_block(0x1adea70, [
    ("adrp", "x20, #0x3fe8000"), ("add", "x25, x20, #0xf88"),
    ("ldr", "x1, [x20, #0xf88]"), ("str", "x21, [sp, #0xa0]"),
    ("mov", "x0, x25"), ("ldr", "x1, [x1, #0x28]"),
    ("blr", "x1"), ("tst", "w0, #0xff"), ("b.ne", "#0x1adeba0"),
])
count += check_block(0x1adef18, [
    ("adrp", "x20, #0x3fe8000"), ("add", "x25, x20, #0xf88"),
    ("ldr", "x1, [x20, #0xf88]"), ("mov", "x0, x25"),
    ("ldr", "x1, [x1, #0x28]"), ("blr", "x1"),
    ("tst", "w0, #0xff"), ("b.ne", "#0x1adf048"),
])
# The adjusted dispatcher's initialization branch constructs this concrete vptr.
count += check_block(0x1adef74, [("adrp", "x20, #0x3fe8000")])
count += check_block(0x1adefd4, [
    ("add", "x25, x20, #0xf88"), ("ldr", "w3, [x1, #0xcf0]"),
    ("adrp", "x2, #0x321b000"), ("adrp", "x1, #0x321a000"),
    ("add", "x2, x2, #0x108"), ("add", "x1, x1, #0xf58"),
    ("stp", "x27, x26, [x25, #8]"), ("mov", "w0, #1"),
    ("str", "x1, [x26, #0x10]"), ("stp", "xzr, xzr, [x25, #0x18]"),
    ("str", "w3, [x26, #0x18]"), ("str", "x2, [x20, #0xf88]"),
])
count += check_block(0x1adf028, [
    ("ldr", "x1, [x20, #0xf88]"), ("mov", "x0, x25"),
    ("ldp", "x27, x28, [sp, #0x50]"), ("ldr", "x1, [x1, #0x28]"),
    ("blr", "x1"), ("tst", "w0, #0xff"), ("b.eq", "#0x1adef38"),
])
assert struct.unpack("<Q", read(0x321b108 + 0x28, 8))[0] == 0x1e61cd8
# Entire predicate: compare pthread_self() against nullable global owner's +0x10.
count += check_block(0x1e61cd8, [
    ("stp", "x29, x30, [sp, #-0x20]!"), ("mov", "x29, sp"),
    ("str", "x19, [sp, #0x10]"), ("bl", "#0x249b0f0"),
    ("mov", "x19, x0"), ("bl", "#0x2503090"),
    ("cbz", "x0, #0x1e61cf8"), ("ldr", "x0, [x0, #0x10]"),
    ("cmp", "x0, x19"), ("cset", "w0, ne"),
    ("ldr", "x19, [sp, #0x10]"), ("ldp", "x29, x30, [sp], #0x20"),
    ("ret", ""),
])
count += check_block(0x2503090, [
    ("adrp", "x0, #0x4044000"), ("ldr", "x0, [x0, #0x3a8]"),
    ("ret", ""),
])
count += check_block(0x249b0f0, [("b", "#0x425670")])
# Resolve the exact PLT GOT cell to its dynamic symbol, rather than guessing a name.
count += check_block(0x425670, [
    ("adrp", "x16, #0x3f84000"), ("ldr", "x17, [x16, #0x38]"),
    ("add", "x16, x16, #0x38"), ("br", "x17"),
])
relocations = elf.get_section_by_name(".rela.plt")
symbols = elf.get_section(relocations["sh_link"])
matches = [r for r in relocations.iter_relocations() if r["r_offset"] == 0x3f84038]
assert len(matches) == 1
relocation = matches[0]
assert relocation["r_info_type"] == 1026  # R_AARCH64_JUMP_SLOT
assert relocation["r_addend"] == 0
symbol = symbols.get_symbol(relocation["r_info_sym"])
assert symbol.name == "pthread_self" and symbol["st_shndx"] == "SHN_UNDEF"

print(json.dumps({
    "result": "PASS",
    "verifier_sha256": digest(Path(__file__).read_bytes()),
    "firmware_sha256": FIRMWARE,
    "format": "ELF64 little-endian AArch64 ET_EXEC",
    "instruction_anchors": count,
    "taskbox": {
        "global_object": "0x3fe8f88", "constructor_vptr": "0x321b108",
        "predicate_slot": "0x28", "predicate": "0x1e61cd8",
        "nonzero_branch": "queued", "zero_branch": "synchronous native virtual dispatch",
    },
    "predicate": {
        "current_thread_wrapper": "0x249b0f0", "plt": "0x425670",
        "got_cell": "0x3f84038", "relocation": "R_AARCH64_JUMP_SLOT",
        "symbol": "pthread_self", "owner_getter": "0x2503090",
        "owner_global_pointer": "0x40443a8", "owner_thread_field": "0x10",
        "comparison": "64-bit inequality; null owner contributes zero",
        "result": "w0 is 0 or 1",
    },
    "limits": [
        "Static constructor and predicate code only; no live object or thread observed.",
        "Native owner pthread identity is not proven equivalent to Linux PID or TID.",
        "Installed owner, panel-input entry thread, event ordering and execution remain unknown.",
        "No hook claim, observation, input consumption, native pointer read or runtime validation.",
        "No proof of owner/view lifetime, absence of later vptr mutation, or complete thread coverage.",
    ],
}, indent=2))
