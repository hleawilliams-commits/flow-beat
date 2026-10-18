#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the real assembly in an installed ARM64 Linux Docker image, offline.
Build artifacts remain in ignored out-history; no AZ emulator/device access.
"""
import hashlib,json,re,subprocess,uuid
from pathlib import Path
HERE=Path(__file__).resolve().parent
IMAGE='azmod-build:18.04'
EXPECTED='sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90'
def run(args):return subprocess.run(args,text=True,capture_output=True,timeout=90)
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
check=run(['docker','image','inspect',IMAGE,'--format','{{.Id}} {{.Architecture}}'])
assert check.returncode==0,check.stderr
assert check.stdout.strip()==EXPECTED+' arm64',check.stdout
sources={n:digest(HERE/n) for n in ['entry.S','test_fixture.S','test_abi.cpp','run-tests.py']}
out=HERE/'out-history'/str(uuid.uuid4());out.mkdir(parents=True)
commands='''set -eu
uname -m
g++ --version | head -n 1
g++ -std=c++17 -O2 -g -Wall -Wextra -Werror -fexceptions -fasynchronous-unwind-tables -fPIC -c /src/entry.S -o /out/entry.o
g++ -std=c++17 -O2 -g -Wall -Wextra -Werror -fexceptions -fasynchronous-unwind-tables /out/entry.o /src/test_fixture.S /src/test_abi.cpp -o /out/test_abi
/out/test_abi
objdump -d /out/entry.o > /out/entry-disassembly.txt
readelf --debug-dump=frames /out/entry.o > /out/entry-cfi.txt
readelf -r /out/entry.o > /out/entry-relocations.txt
'''
result=run(['docker','run','--rm','--pull=never','--network','none','--platform','linux/arm64',
    '-v',str(HERE)+':/src:ro','-v',str(out)+':/out',IMAGE,'sh','-c',commands])
(out/'candidate-process.txt').write_text(result.stdout+result.stderr)
print(result.stdout,end='');print(result.stderr,end='')
if result.returncode:raise SystemExit(result.returncode)
dis=(out/'entry-disassembly.txt').read_text();cfi=(out/'entry-cfi.txt').read_text();rel=(out/'entry-relocations.txt').read_text()
ins=[(m.group(1),m.group(2).strip()) for m in re.finditer(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(\S+)\s*(.*)$',dis,re.M)]
assert len(ins)==46,(len(ins),ins)
assert sum(m=='bl' for m,_ in ins)==1 and sum(m=='br' for m,_ in ins)==1
assert ins[-1]==('br','x16') and ins[-2]==('ldar','x16, [x16]')
assert ins[0]==('sub','sp, sp, #0x140') and ('add','sp, sp, #0x140') in ins
assert not any(m in ['ret','blr'] for m,_ in ins)
assert 'r30 (x30) at cfa-168' in cfi and 'DW_CFA_def_cfa_offset: 320' in cfi
assert 'DW_CFA_restore: r30 (x30)' in cfi and 'DW_CFA_def_cfa_offset: 0' in cfi
assert 'az_knob_observe_entry' in rel and 'az_knob_original' in rel
mutant=(HERE/'entry.S').read_text().replace('    ldp x2, x3, [sp, #16]\n','')
(out/'entry-missing-w3.S').write_text(mutant)
control=run(['docker','run','--rm','--pull=never','--network','none','--platform','linux/arm64',
    '-v',str(HERE)+':/src:ro','-v',str(out)+':/out',IMAGE,'sh','-c',
    'g++ -std=c++17 -O2 -g -Wall -Wextra -Werror -fexceptions -fasynchronous-unwind-tables /out/entry-missing-w3.S /src/test_fixture.S /src/test_abi.cpp -o /out/control && /out/control'])
(out/'control-process.txt').write_text(control.stdout+control.stderr)
assert control.returncode==1,(control.returncode,control.stdout,control.stderr)
assert 'fixture_register_capture.x[i]==fixture_expected_registers[i]' in control.stdout and 'FAIL' in control.stdout
print('Negative control (missing x2/x3 restore): rejected as expected')
report={'image':IMAGE,'image_id':EXPECTED,'platform':'linux/arm64','source_sha256':sources,
    'object_sha256':digest(out/'entry.o'),'test_elf_sha256':digest(out/'test_abi'),
    'actual_assembly_executed':True,'instruction_audit_passed':True,'cfi_audit_passed':True,
    'negative_control':'missing x2/x3 restore rejected by actual register/native-result assertions',
    'no_az_emulator_or_physical_device_access':True,'native_target_signature_inferred':False,
    'private_run_id':out.name}
(HERE/'test-results.txt').write_text(result.stdout+result.stderr+'\nNegative control: missing x2/x3 restore\n'+control.stdout+control.stderr)
(HERE/'test-result.json').write_text(json.dumps(report,indent=2)+'\n')
for name in ['entry-disassembly.txt','entry-cfi.txt','entry-relocations.txt']:
    (HERE/name).write_text((out/name).read_text().replace('/out','<OUTPUT>'))
