#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract pinned production functions, patch only temporary sources, run host checks."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile
here=Path(__file__).resolve().parent
experiment=here.parent
repo=experiment.parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--upstream',type=Path,default=repo/'../../../work/xdj-az-mods')
parser.add_argument('--sdk',type=Path,default=repo/'../az-mods-sdk')
args=parser.parse_args()
meta=json.loads((experiment/'provenance.json').read_text())
def source(path):
    return subprocess.check_output(['git','-C',str(args.upstream.resolve()),'show',meta['commit']+':'+path]).decode()
def region(text,start,stop):
    a=text.index(start);b=text.index(stop,a);return text[a:b]
def build_inc(drum,sdk):
    return region(drum,'/* Correction for unmeasured cases','/* ---- EP147\'s own beat grid')+region(drum,'/* Flusher:','/* Set by azmod,')+region(sdk,'int azstem_deck_beat_phase(int deck, double *phase);','\0')
def checked(command,**kwargs):
    return subprocess.run(command,check=True,**kwargs)
baseline=source(meta['path'])
assert hashlib.sha256(baseline.encode()).hexdigest()==meta['baseline_sha256']
sdk=source('src/azstem/sdk_export.c')+'\0'
data=(repo/'grouped-panels/data.h').read_text()
# Exact production types/helper/follower; unrelated worker globals are omitted.
follow=region(data,'struct gp_follow {','struct gp_phrase_value')+region(data,'static double gp_wrap_half','static void gp_counter_once')
with tempfile.TemporaryDirectory(prefix='phrase-grid-retry-') as temp:
    tmp=Path(temp);drum_path=tmp/meta['path'];drum_path.parent.mkdir(parents=True);drum_path.write_text(baseline)
    checked(['git','apply','--check',str(experiment/'grid-retry.patch')],cwd=tmp)
    checked(['git','apply',str(experiment/'grid-retry.patch')],cwd=tmp)
    patched=drum_path.read_text();assert hashlib.sha256(patched.encode()).hexdigest()==meta['patched_sha256']
    (tmp/'follow.inc').write_text(follow)
    for kind,text in [('baseline',baseline),('patched',patched)]:
        (tmp/(kind+'.inc')).write_text(build_inc(text,sdk))
        for mode,flags in [('plain',[]),('asan',['-fsanitize=address,undefined'])]:
            output=tmp/(kind+'-'+mode)
            checked([os.environ.get('CC','clang'),'-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-g','-fno-omit-frame-pointer',*flags,
                     '-I'+str(args.sdk.resolve()/'include'),'-DGRID_INC="'+str(tmp/(kind+'.inc'))+'"','-DFOLLOW_INC="'+str(tmp/'follow.inc')+'"',str(here/'test_grid_service.c'),'-lm','-o',str(output)])
            print(kind,mode,flush=True)
            checked([str(output)]+(['baseline'] if kind=='baseline' else []))
print('No upstream checkout, emulator, physical deck or production community code was changed.')
