#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Puts a layout from the Deck Screen Designer on the emulator.
#   ./push-layout.sh            the layout you copied (Copy layout puts it on the clipboard)
#   ./push-layout.sh my.txt     a layout saved in a file
# The emulator checkout is found by itself; set AZ_EMU=/path/to/xdj-az-emu if it isn't.
# The push restarts the emulator's deck program, so the new layout shows a few seconds later.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp "${TMPDIR:-/tmp}/custom-screen.XXXXXX")
trap 'rm -f "$tmp"' EXIT HUP INT TERM

if [ $# -ge 1 ]; then cp "$1" "$tmp"; else pbpaste > "$tmp"; fi
if ! grep -q '^version 1' "$tmp" || ! grep -q '^panel ' "$tmp"; then
    echo "That isn't a Custom Screen layout (no 'version 1' and 'panel' lines)." >&2
    echo "Press Copy layout in the Deck Screen Designer first, or pass a layout file." >&2
    exit 1
fi
cp "$tmp" "$here/assets/layout.txt"
echo "Saved assets/layout.txt ($(grep -c '^panel ' "$tmp") panels, $(grep -c '^widget ' "$tmp") widgets)"

if [ ! -f "$here/out/custom-screen.so" ]; then
    echo "out/custom-screen.so is missing: build the mod first (README, Build)." >&2
    exit 1
fi
if [ -f "$here/data/layout.txt" ]; then
    echo "Note: data/layout.txt is here and wins over assets/layout.txt; remove it to see this layout." >&2
fi

emu=${AZ_EMU:-}
if [ -z "$emu" ]; then
    for d in "$HOME/xdj-az-emu" "$HOME/agent-tools/xdj-az-emu" "$HOME/Developer/xdj-az-emu" \
             "$HOME/projects/xdj-az-emu" "$HOME/Desktop/Claude/xdj-az-emu" "$HOME/Desktop/Claude/dj-tools/xdj-az-emu"; do
        if [ -f "$d/tools/az-push-mod.py" ]; then emu=$d; break; fi
    done
fi
if [ -z "$emu" ] && command -v mdfind >/dev/null 2>&1; then
    f=$(mdfind -name az-push-mod.py 2>/dev/null | grep '/tools/az-push-mod.py$' | head -n 1 || true)
    if [ -n "$f" ]; then emu=$(dirname "$(dirname "$f")"); fi
fi
if [ -z "$emu" ]; then
    echo "Can't find the emulator (xdj-az-emu). Run: AZ_EMU=/path/to/xdj-az-emu $0" >&2
    exit 1
fi
echo "Pushing to the emulator in $emu"
cd "$emu"
python3 tools/az-push-mod.py "$here/"
