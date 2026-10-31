#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Puts a layout from the Deck Screen Designer on the emulator.
#   ./push-layout.sh            the layout you copied (Copy layout puts it on the clipboard)
#   ./push-layout.sh my.txt     a layout saved in a file
# It writes the layout to MODS/community-mods/custom-screen/data/layout.txt on the emulator's
# USB stick, the file the mod reads first. The mod re-reads it within a second: no restart.
# The emulator checkout is found by itself; set AZ_EMU=/path/to/xdj-az-emu if it isn't.
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
cp "$tmp" "$here/last-pushed-layout.txt"
echo "Layout: $(grep -c '^panel ' "$tmp") panels, $(grep -c '^widget ' "$tmp") widgets (copy kept in last-pushed-layout.txt)"

emu=${AZ_EMU:-}
if [ -z "$emu" ]; then
    for d in "$HOME/Desktop/Claude/dj-tools/xdj-az-emu" "$HOME/xdj-az-emu" "$HOME/agent-tools/xdj-az-emu" \
             "$HOME/Developer/xdj-az-emu" "$HOME/projects/xdj-az-emu" "$HOME/Desktop/Claude/xdj-az-emu"; do
        if [ -f "$d/tools/azemu.py" ]; then emu=$d; break; fi
    done
fi
if [ -z "$emu" ] && command -v mdfind >/dev/null 2>&1; then
    f=$(mdfind -name azemu.py 2>/dev/null | grep '/tools/azemu.py$' | head -n 1 || true)
    if [ -n "$f" ]; then emu=$(dirname "$(dirname "$f")"); fi
fi
if [ -z "$emu" ]; then
    echo "Can't find the emulator (xdj-az-emu). Run: AZ_EMU=/path/to/xdj-az-emu $0" >&2
    exit 1
fi

cd "$emu"
python3 - "$tmp" <<'PY'
import sys
sys.path.insert(0, "tools")
import azemu
rc, out = azemu.ssh(r"mount | sed -n 's|.* on \(/media/usb/[^ ]*\) type.*|\1|p' | head -1")
mnt = out.strip() if rc == 0 else ""
if not mnt:
    sys.exit("No USB stick is mounted in the emulator. Is the emulator running? (tools/az-ssh true)")
mod = f"{mnt}/MODS/community-mods/custom-screen"
if azemu.ssh(f"test -d '{mod}'")[0] != 0:
    sys.exit(f"Custom Screen hasn't run on the emulator yet ({mod} is missing). Push the mod "
             "(python3 tools/az-push-mod.py <custom-screen folder>) and turn it on in the Mod Menu first.")
dst = f"{mod}/data/layout.txt"
if not azemu.push_file(sys.argv[1], dst, "644"):
    sys.exit(f"Copy to {dst} failed")
azemu.ssh("sync")
print(f"Sent to the emulator ({dst}). It shows within a second in 4 DECK view.")
PY
