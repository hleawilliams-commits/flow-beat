#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Prepare exact frozen C closure and standalone Rust wire modules; run no tests."""
import hashlib
import json
import re
import runpy
from pathlib import Path

FUNCTIONS = ('mixer_instance_init', 'xput_bytes', 'xget_bytes', 'ctrl_release',
             'mixer_owned_next', 'ctrl_ioctl', 'ctrl_write')

def sha(data):
    return hashlib.sha256(data.encode() if isinstance(data, str) else data).hexdigest()

def prepare_sources(emu, run, frozen):
    parent = run / 'frozen' / 'emulator-overlay-restore'
    # Captured helpers only; no parent suite is executed.
    prepare = runpy.run_path(str(parent / 'prepare.py'))['prepare']
    extract = runpy.run_path(str(parent / 'run-tests.py'))['extract']
    receipt = prepare(emu, run / 'prepared')
    mod = run / 'prepared/guest/modules/subucom_virt'
    src = (mod / 'subucom_virt.c').read_text()
    methods = {n: extract(src, n) for n in FUNCTIONS}
    declarations = ''
    for name in ('spi_crc_len', 'n_crc', 'interval_us', 'testmode_held'):
        matches = re.findall(r'^static [^;\n]*\b' + name + r'[^;\n]*;', src, re.M)
        if len(matches) != 1:
            raise ValueError('declaration: ' + name)
        declarations += matches[0] + '\n'
    start = src.index('static DEFINE_SPINLOCK(miso_lock);')
    end = src.index('/* MOSI:', start)
    declarations += src[start:end].replace(methods['mixer_instance_init'], '')
    kernel = declarations + '\n'.join(methods.values())
    shim = frozen['emulator-overlay-restore/test-restore.c'].decode().split('#include "candidate.inc"')[0]
    generated = {'candidate.inc': kernel.encode(), 'kernel-shim.inc': shim.encode(),
                 'linux/types.h': b'#include <stdint.h>\ntypedef uint32_t __u32;typedef int32_t __s32;typedef uint8_t __u8;\n#define __aligned_u64 uint64_t __attribute__((aligned(8)))\n',
                 'linux/ioctl.h': b'#define _IOW(t,n,s) ((1u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n#define _IOR(t,n,s) ((2u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n'}
    for name in ('subucom_ownership.h', 'subucom_frames.h'):
        generated[name] = (mod / name).read_bytes()
    for folder, files in (
        ('emulator-native64-adapter', ('adapter.h','syscalls.h')),
        ('emulator-broker-restore', ('broker.h',)),
        ('emulator-claim-owner', ('claim.c','claim.h')),
        ('emulator-retained-adapter', ('retained.c','retained.h')),
        ('emulator-retained-broker', ('broker_owner.c','broker_owner.h','sync.h')),
        ('emulator-broker-wire', ('wire.c','wire.h','guest_dispatch.c','guest_dispatch.h')),
        ('emulator-owned-graph', ('graph.c','graph.h'))):
        for name in files:
            generated[name] = frozen[folder + '/' + name]
    # Byte-identical private core only: legacy public lifecycle is not linked.
    adapter = frozen['emulator-native64-adapter/adapter.c']
    marker = b'int mba_attach('
    if adapter.count(marker) != 1:
        raise ValueError('frozen native core boundary must be unique')
    core = adapter[:adapter.index(marker)]
    generated['native64-core.inc'] = core
    broker = frozen['emulator-broker-restore/broker.c']
    first, last = b'int mb_create(', b'int mb_adopt('
    if broker.count(first) != 1 or broker.count(last) != 1:
        raise ValueError('frozen broker lifecycle boundaries must be unique')
    before, after = broker.index(first), broker.index(last)
    if before >= after:
        raise ValueError('broker lifecycle boundary order')
    prefix, suffix = broker[:before], broker[after:]
    generated['broker-core.inc'] = prefix + suffix
    support_source = frozen['emulator-owned-graph/test-graph.c']
    support_boundary = b'static void retain_case(void)'
    if support_source.count(support_boundary) != 1:
        raise ValueError('graph fixture support boundary must be unique')
    support = support_source[:support_source.index(support_boundary)]
    if re.search(rb'\b(?:int main|static void [a-z_]+_case)\s*\(', support):
        raise ValueError('parent selector or main entered support prefix')
    generated['graph-support.inc'] = support
    for name in ('stream.c', 'stream.h', 'stream_native.h', 'stream_native.c'):
        generated[name] = frozen['emulator-guest-stream/' + name]
    for name, data in generated.items():
        dest = run / 'source' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)

    wire_helper = run / 'frozen/emulator-broker-wire/prepare-candidate.py'
    wire_output = runpy.run_path(str(wire_helper))['prepare'](emu)
    wire_receipt = json.loads((wire_output / 'wire-preparation.json').read_text())
    rust = {}
    for name in ('mixer_producer.rs', 'mixer_route.rs', 'broker_wire.rs'):
        rust[name] = (wire_output / 'tree/az-preview/src' / name).read_bytes()
        if sha(rust[name]) != wire_receipt['candidate_files_sha256']['az-preview/src/' + name]:
            raise ValueError('Rust candidate mismatch: ' + name)
        target = run / 'rust' / name
        target.parent.mkdir(exist_ok=True)
        target.write_bytes(rust[name])
    metadata = {
        'kernel_prepare': receipt,
        'kernel_function_sha256': {n: sha(v) for n,v in methods.items()},
        'native64_core': {'prefix_bytes': len(core), 'prefix_sha256': sha(core),
                          'boundary': 'int mba_attach('},
        'broker_core': {'prefix_bytes': len(prefix), 'suffix_bytes': len(suffix),
                        'prefix_sha256': sha(prefix), 'suffix_sha256': sha(suffix),
                        'combined_sha256': sha(prefix + suffix),
                        'excluded_start': 'int mb_create(', 'suffix_start': 'int mb_adopt('},
        'graph_fixture_support': {'prefix_bytes': len(support), 'prefix_sha256': sha(support),
                                  'boundary': support_boundary.decode(),
                                  'parent_selectors_compiled': False},
        'rust_preparation': wire_receipt,
        'rust_generated_sha256': {n: sha(v) for n,v in rust.items()}}
    return generated, rust, metadata
