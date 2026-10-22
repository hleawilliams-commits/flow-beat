#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind a new scalar-only observer to a different exact native input route.

Reads the owner's private ELF; does not execute it, call any native function,
install a hook, contact a device, or rerun the unchanged ARM64 ABI fixture.
The old wrapper and its evidence are immutable references, not new test results.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct

from elftools.dwarf.callframe import FDE
from elftools.elf.elffile import ELFFile

HERE = Path(__file__).resolve().parent
FIRMWARE_SHA = '6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
ENTRY = 0x1b01cf8
EXTENT = 0x2e8
SLOTS = (0x2d453e8, 0x2d45758)
PROLOGUE = (0xa9b47bfd, 0x910003fd, 0xa90153f3, 0xa9025bf5)
ABI_SHA = 'a1f1cf42cc587c3222d299ac9dd31bfd0944f1ea51ec98a19d74ba40088a5034'
ABI_RESULT_SHA = 'd46963a16c5b3b71d6834a4e3792dc79fc45fdb81cb7d3a78d6f0d026ff3e0d7'
ROUTE_RESULT_SHA = '5ebdee5b9261a6cfc1fbf1577f05d8d1ff80b7be7d194a291fc4683354058f48'
FW_TAG = 'ep147-9e9454e357389ece'

def digest(blob):
    return hashlib.sha256(blob).hexdigest()

def require(condition, label):
    if not condition:
        raise AssertionError(label)

def boundary(entry, slots, prologue, read, fdes):
    require(entry == ENTRY, 'wrong native entry')
    require(tuple(slots) == SLOTS, 'wrong base/derived slot pair')
    require(tuple(prologue) == PROLOGUE, 'wrong expected prologue words')
    require(fdes.get(entry) == EXTENT, 'wrong FDE entry/extent')
    require(read(entry, 16) == struct.pack('<4I', *prologue), 'firmware prologue mismatch')
    for slot in slots:
        require(struct.unpack('<Q', read(slot, 8))[0] == entry, 'firmware slot mismatch')

def wrapper_shape(text):
    # These targeted checks make the negative controls diagnostic; the complete
    # source hash below is the identity check for all preservation/CFI behavior.
    require(re.search(r'^\s*mov x5, x30\s*$', text, re.M), 'original LR must be helper argument six')
    require(not re.search(r'^\s*mov [xw]2, [xw]2\s*$', text, re.M), 'x2 may not be truncated')
    require(re.search(r'^\s*stp x2, x3, \[sp, #16\]\s*$', text, re.M), 'save x2/x3')
    require(re.search(r'^\s*ldp x2, x3, \[sp, #16\]\s*$', text, re.M), 'restore x2/x3')
    require(re.search(r'^\s*stp x4, x5, \[sp, #32\]\s*$', text, re.M), 'save fifth input')
    require(re.search(r'^\s*ldp x4, x5, \[sp, #32\]\s*$', text, re.M), 'restore fifth input')
    require(text.count('bl az_dispatch_observe_entry') == 1, 'one nonthrowing observer call')
    require(text.count('ldar x16, [x16]') == 1 and text.count('br x16') == 1,
            'acquire original and tail forward')
    require(digest(text.encode()) == ABI_SHA, 'immutable wrapper identity changed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--result', type=Path)
    args = parser.parse_args()
    checks = []
    controls = []
    def check(label, condition):
        require(condition, label)
        checks.append(label)
    def reject(label, callback, reason):
        try:
            callback()
        except AssertionError as error:
            require(str(error) == reason, label + ': unexpected rejection ' + str(error))
            controls.append({'name': label, 'rejected_by': reason})
        else:
            raise AssertionError(label + ': invalid binding accepted')

    blob = args.elf.read_bytes()
    check('exact firmware SHA-256', digest(blob) == FIRMWARE_SHA)
    elf = ELFFile(io.BytesIO(blob))
    check('64-bit little-endian ARM64 executable', elf.elfclass == 64 and elf.little_endian
          and elf['e_machine'] == 'EM_AARCH64' and elf['e_type'] == 'ET_EXEC')
    loads = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
    def read(address, size):
        for seg in loads:
            if seg['p_vaddr'] <= address and address + size <= seg['p_vaddr'] + seg['p_filesz']:
                off = seg['p_offset'] + address - seg['p_vaddr']
                return blob[off:off + size]
        raise AssertionError('unmapped static address')
    fdes = {x['initial_location']: x['address_range'] for x in elf.get_dwarf_info().EH_CFI_entries()
            if isinstance(x, FDE)}
    check('entry body belongs to file-backed executable segment', any(s['p_flags'] & 1
          and s['p_vaddr'] <= ENTRY and ENTRY + EXTENT <= s['p_vaddr'] + s['p_filesz'] for s in loads))

    route_blob = (HERE.parent/'input-route/static-result.json').read_bytes()
    check('immutable route evidence identity', digest(route_blob) == ROUTE_RESULT_SHA)
    route = json.loads(route_blob)
    check('route evidence exact firmware and address', route['firmware_sha256'] == FIRMWARE_SHA
          and route['recommended_passive_boundary']['address'] == hex(ENTRY))
    check('exact complete native function hash', digest(read(ENTRY, EXTENT)) ==
          route['functions'][hex(ENTRY)]['body_sha256'])
    check('exact first-four hash', digest(read(ENTRY,16)) ==
          route['recommended_passive_boundary']['prologue_sha256'])

    # Source-dependent checks are filled against the final separately owned
    # candidate; no source fallback may silently bind an old observer.
    source = (HERE/'observer.cpp').read_text()
    mod = (HERE/'mod.c').read_text()
    manifest = json.loads((HERE/'azmod.json').read_text())
    makefile = (HERE/'Makefile').read_text()
    candidate_wrapper = (HERE/'abi/entry.S').read_text()
    original_dir = HERE.parent/'dispatcher-observer/abi'
    original = (original_dir/'entry.S').read_bytes()
    previous_result_blob = (original_dir/'test-result.json').read_bytes()
    check('unchanged original ABI source', digest(original) == ABI_SHA)
    check('unchanged original ABI evidence', digest(previous_result_blob) == ABI_RESULT_SHA)
    previous = json.loads(previous_result_blob)
    for filename, sha in previous['source_sha256'].items():
        check('prior tested source identity: '+filename, digest((original_dir/filename).read_bytes()) == sha)
    check('prior ABI evidence has 55 successful assertions', previous['candidate']['passed'] == 55
          and previous['candidate']['failed'] == 0 and previous['actual_assembly_executed']
          and previous['instruction_audit_passed'] and previous['cfi_audit_passed'])
    check('prior ABI evidence has five inputs plus LR', previous['capture_shape'] ==
          ['x0','x1','x2','w3','w4','original LR in x5'])
    wrapper_shape(candidate_wrapper)
    checks.append('candidate wrapper is byte-identical tested five-input tail wrapper')
    check('copied wrapper bytes equal immutable original', candidate_wrapper.encode() == original)

    def source_values(candidate):
        values = []
        for symbol in ['ENTRY', 'SLOT', 'SLOT2']:
            found = re.findall(r'\b'+symbol+r'\s*=\s*(0x[0-9a-fA-F]+)\b', candidate)
            require(len(found) == 1, 'one source '+symbol+' constant required')
            values.append(int(found[0],16))
        found = re.findall(r'const uint32_t expected\[\]\s*=\s*\{([^}]+)\};',candidate)
        require(len(found) == 1, 'one expected prologue required')
        prologue = tuple(int(v.strip(),16) for v in found[0].split(','))
        require(len(prologue) == 4, 'exactly four prologue words required')
        return values[0], values[1:], prologue
    def runtime_guard(candidate):
        found = re.findall(r'static bool check_prologues\(\) \{(.*?)\n\}',candidate,re.S)
        require(len(found)==1, 'one runtime compatibility guard required')
        body = re.sub(r'\s+','',re.sub(r'//[^\n]*','',found[0]))
        require('memcpy(&target,(constvoid*)SLOT,sizeoftarget);' in body,
                'runtime base slot read required')
        require('memcpy(&target2,(constvoid*)SLOT2,sizeoftarget2);' in body,
                'runtime derived slot read required')
        require('returntarget==ENTRY&&target2==ENTRY&&!memcmp((constvoid*)ENTRY,expected,sizeofexpected);' in body,
                'runtime guard must require both slots and all expected words')
    runtime_guard(source)
    checks.append('runtime compatibility guard reads both slots and requires both plus all prologue words')
    check('runtime compatibility guard precedes the only claim', source.index('if (!check_prologues())') < source.index('core->claim_hook('))
    check('production build does not enable host-test bypass', 'AZDISPATCH_TEST' not in makefile)
    entry, slots, expected = source_values(source)
    boundary(entry,slots,expected,read,fdes)
    checks.append('candidate constants match exact entry, both slots, FDE and all first-four words')
    check('new unique manifest identity', manifest['id'] == 'encoder-route-observer')
    check('new manifest firmware', manifest['firmware'] == [FW_TAG] and manifest['hooks'] is True)
    check('new source identity', bool(re.search(r'static const char id\[\]\s*=\s*"encoder-route-observer";',source)))
    check('new descriptor identity', bool(re.search(r'\.id\s*=\s*"encoder-route-observer"',mod)))
    check('new output identity', bool(re.search(r'^MOD_ID\s*:=\s*encoder-route-observer\s*$',makefile,re.M)))
    check('candidate wrapper is in build source list', bool(re.search(r'^SRCS\s*:=.*\babi/entry\.S\s*$',makefile,re.M)))
    check('one exclusive hook claim uses correct entry and wrapper', len(re.findall(r'core->claim_hook\s*\(',source)) == 1
          and bool(re.search(r'core->claim_hook\s*\(id,\s*ENTRY,\s*\(void \*\)az_dispatch_entry,\s*&az_dispatch_original\)',source)))
    check('matching six-argument noexcept helper definition', bool(re.search(
        r'extern "C" void az_dispatch_observe_entry\(uintptr_t x0,\s*uintptr_t x1,\s*uintptr_t x2,\s*'
        r'uint32_t w3,\s*uint32_t w4,\s*uintptr_t original_lr\) noexcept\s*\{',source)))
    check('correct original pointer declaration', bool(re.search(r'void \*az_dispatch_original;',source)))
    check('correct wrapper declaration', bool(re.search(r'void az_dispatch_entry\(void\);',source)))
    check('no previous observer installation ID or opt-in', 'encoder-dispatch-observer' not in source
          and 'AZ_ENCODER_DISPATCH_OBSERVER_EMULATOR' not in source)
    check('separate opt-in', 'AZ_ENCODER_ROUTE_OBSERVER_EMULATOR' in source)
    check('helper forwards all six captured scalars to the observer', 'ero::observe(x0,x1,x2,w3,w4,original_lr);' in source)
    reject('derived guard omission refused', lambda: runtime_guard(source.replace('target2==ENTRY && ','')), 'runtime guard must require both slots and all expected words')
    reject('derived read using base slot refused', lambda: runtime_guard(source.replace('memcpy(&target2,(const void *)SLOT2,','memcpy(&target2,(const void *)SLOT,')), 'runtime derived slot read required')

    reject('old secondary entry refused', lambda: boundary(0x1adeee0,slots,expected,read,fdes), 'wrong native entry')
    reject('interior entry refused', lambda: boundary(ENTRY+4,slots,expected,read,fdes), 'wrong native entry')
    reject('wrong base slot refused', lambda: boundary(entry,[0x2d61d98,slots[1]],expected,read,fdes), 'wrong base/derived slot pair')
    reject('wrong derived slot refused', lambda: boundary(entry,[slots[0],0x2d61d98],expected,read,fdes), 'wrong base/derived slot pair')
    reject('duplicated slot pair refused', lambda: boundary(entry,[slots[0],slots[0]],expected,read,fdes), 'wrong base/derived slot pair')
    for i in range(4):
        wrong = list(expected); wrong[i] ^= 1
        reject('changed prologue word '+str(i), lambda w=wrong: boundary(entry,slots,w,read,fdes), 'wrong expected prologue words')
    bad_fdes = dict(fdes); bad_fdes[ENTRY] = EXTENT+4
    reject('wrong unwind extent refused', lambda: boundary(entry,slots,expected,read,bad_fdes), 'wrong FDE entry/extent')
    def changed_text(address,size):
        value = read(address,size)
        return bytes([value[0]^1])+value[1:] if address == ENTRY else value
    reject('firmware text mismatch refused', lambda: boundary(entry,slots,expected,changed_text,fdes), 'firmware prologue mismatch')
    for slot in slots:
        def changed_slot(address,size,bad=slot):
            return struct.pack('<Q',0x1adeee0) if address == bad else read(address,size)
        reject('firmware slot mismatch '+hex(slot), lambda r=changed_slot: boundary(entry,slots,expected,r,fdes), 'firmware slot mismatch')
    reject('wrong helper LR register refused', lambda: wrapper_shape(candidate_wrapper.replace('mov x5, x30','mov x4, x30')), 'original LR must be helper argument six')
    reject('truncated x2 helper capture refused', lambda: wrapper_shape(candidate_wrapper.replace('    bl az_dispatch_observe_entry','    mov w2, w2\n    bl az_dispatch_observe_entry')), 'x2 may not be truncated')
    reject('missing x2/x3 restore refused', lambda: wrapper_shape(candidate_wrapper.replace('    ldp x2, x3, [sp, #16]','    nop')), 'restore x2/x3')

    result = {
        'checks':len(checks), 'check_labels':checks, 'negative_control_count':len(controls),
        'negative_controls':controls, 'firmware_sha256':FIRMWARE_SHA,
        'observer_id':manifest['id'], 'entry':hex(entry), 'fde_extent':hex(EXTENT),
        'base_slot':hex(slots[0]), 'derived_slot':hex(slots[1]),
        'prologue_words':[hex(v) for v in expected], 'prologue_sha256':digest(read(entry,16)),
        'function_sha256':digest(read(entry,EXTENT)),
        'source_sha256':{name:digest((HERE/name).read_bytes()) for name in
            ['observer.cpp','mod.c','azmod.json','Makefile','abi/entry.S']},
        'wrapper_immutable_source_sha256':ABI_SHA,
        'wrapper_prior_evidence_sha256':ABI_RESULT_SHA,
        'prior_abi_assertions_referenced':55, 'old_abi_tests_rerun':False,
        'wrapper_capture_shape':previous['capture_shape'],
        'route_evidence_sha256':ROUTE_RESULT_SHA,
        'hook_claim_attempted':False, 'runtime_observation':False,
        'device_or_emulator_access':False,
        'scope':'New static observer-to-entry binding and unchanged ABI identity only; not a native hook claim or input delivery proof.',
        'limits':[
            'Actual SDK claim still requires exclusive runtime ownership and island availability',
            'Runtime rotary registration, view/child identity, native counter ordering and input-stage correlation remain unproven',
            'Tail-inherited LR need not name the immediate caller instruction',
            'Entry does not prove task execution, handler returns, completion, cancellation or native object lifetime',
            'No physical timing or new native return interpretation is established',
            'Source identity checks supplement independent review; they are not a complete C++ behavior proof',
        ],
    }
    output = json.dumps(result,indent=2,sort_keys=True)+'\n'
    if args.result:
        args.result.write_text(output)
    print(output,end='')

if __name__ == '__main__':
    main()
