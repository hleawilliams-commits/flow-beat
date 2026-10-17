#!/usr/bin/env python3
"""Generate isolated emulator test source from the published component."""
import argparse,json,pathlib,shutil
p=argparse.ArgumentParser();p.add_argument('destination',type=pathlib.Path);p.add_argument('--mode',choices=['normal','gap'],required=True);a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[2];out=a.destination.resolve()
if out.exists():raise SystemExit('Use a new private build folder')
out.mkdir(parents=True)
c=(repo/'mods-shortcut/mods-shortcut.c').read_text().replace('#define MOD_ID "mods-shortcut"','#define MOD_ID "mods-shortcut-delivery"').replace('"MODS", 16','"D-MODS", 16')
(out/'mods-shortcut-delivery.c').write_text(c)
h=(repo/'experiments/mods-touch-diagnosis/shortcut-diagnostic.h').read_text()
if a.mode=='gap':
 h=h.replace('#include "azmod-mod.h"','#include "azmod-mod.h"\n#include "frame-gap.h"')
 h=h.replace('return gp_shortcut_frame_at(bus, f, len, gp_shortcut_clock_ms());','long now = gp_shortcut_clock_ms();\n    if (delivery_gap_blocks(__atomic_load_n(&gp_shortcut_pending, __ATOMIC_ACQUIRE), gp_shortcut_requested_at, now))\n        return AZ_FRAME_OK;\n    return gp_shortcut_frame_at(bus, f, len, now);')
(out/'shortcut.h').write_text(h)
shutil.copy2(repo/'experiments/mods-shortcut-delivery/frame-gap.h',out/'frame-gap.h')
m=json.loads((repo/'mods-shortcut/azmod.json').read_text());m.update(id='mods-shortcut-delivery',name='MODS delivery diagnostic',version='0.0.1' if a.mode=='normal' else '0.0.2',summary='Emulator-only SOURCE delivery experiment.',description='Not a released mod or fix. Tests SOURCE delivery with normal callbacks or a simulated1250ms transport gap.',source='https://github.com/hleawilliams-commits/flow-beat/tree/codex/az-mods-handoff-2026-10-16/experiments/mods-shortcut-delivery')
(out/'azmod.json').write_text(json.dumps(m,indent=2)+'\n')
(out/'Makefile').write_text((repo/'mods-shortcut/Makefile').read_text().replace('MOD_ID          := mods-shortcut','MOD_ID          := mods-shortcut-delivery').replace('SRCS            := mods-shortcut.c','SRCS            := mods-shortcut-delivery.c'))
for name in ['icon.png','LICENSE']:shutil.copy2(repo/'mods-shortcut'/name,out/name)
print(out)
