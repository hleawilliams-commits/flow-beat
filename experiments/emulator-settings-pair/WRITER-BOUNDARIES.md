# Existing writers and the settings-pair boundary

This is a static review of committed local emulator sources at
`06fc981707e69982c0554bb508e0c646c005a979`. No emulator script, endpoint,
filesystem behavior test or live settings writer was executed. Line references
below belong to that commit, not a later working copy. The new component
accepts an exact ACK and an explicit pair of byte strings; it does not yet map
frames to settings, implement a live `SettingsSink`, restore files or exclude
existing writers.

## Names and bytes on disk

The two target basenames are `knob.ctr` and `mixer.state`. The preview's fixed
mixer temporary basename is `mixer.state.new`. Unix sources use the public
socket directory `/tmp/az-emu-m1`; Windows normally uses `%TEMP%\az-emu-win`.
The Windows fallback is not identical: preview `config.rs:17–23` uses
`temp_dir()` when TEMP is absent, while `tools/azemu.py:27` falls back to `.`.
A future adapter must bind its selected directory, not infer identity from these
basenames alone. Windows filesystem behavior is outside this Unix component.

| Writer | Encoding and operation | Error/ordering boundary |
|---|---|---|
| Preview `config.rs:62–69` | Reads trimmed `u32`, masks to eight bits; writes `u8.to_string()` directly to `knob.ctr`: decimal digits, no newline. | Write result discarded. App `app.rs:458–465` stores before changing its model and later enqueueing a frame. This is not ACK-bound persistence. |
| Preview `config.rs:76–107` | Reads a permissive comma/colon byte map, updates the first matching key or appends; emits quoted decimal keys and decimal byte values with `: ` and `, ` separators, no newline. Writes the fixed `mixer.state.new`, then renames it over `mixer.state`. | Write failure prevents rename; rename failure is discarded. No success receipt or pair transaction. |
| Driver `tools/az-drive.py:312–333,381–393` | Counter uses trimmed Python `int` masked to eight bits, then direct text truncation/write of decimal digits without newline. Mixer uses `json.load`, integer conversion/masking, `dict.update`, and direct truncation plus default `json.dump`, without a newline. | `OSError` from writes is ignored. `mixer_set` writes the map before sending its three frames. `knob:418–433` sends detents first, then writes the counter and sends the release frame. |

Neither old implementation preserves the original byte representation. The
preview parser skips malformed fields and retains duplicate keys; it updates
only their first occurrence. Python JSON decoding keeps the last occurrence.
Consequently updating the first of two equal keys can leave the driver reading
the older last value. The driver accepts negative integers and masks them;
the preview's `u32` parser rejects negative values. Read/parse failures can become
an empty or partial map. These are source-derived differences, not executed
counterexamples in this review.

The direct writes use ordinary path opens; the preview's temporary path is also
an ordinary reusable pathname. These implementations have no explicit
symlink-refusal, exclusive temporary creation, retained inode identity,
replacement-mode preservation, file/directory fsync, or uncertain-outcome
receipt contract. A rename does not make the counter and mixer map one atomic
transaction. Existing target permissions may survive a direct truncation; a
replacement can instead inherit the temporary file's permissions. No exact
mode-restoration promise follows from either code path.

## What the preview actually persists

`controls.rs:315–333,389–413,888–905` selects the high and shared low-bit bytes
from `MX_AT` and `MX10`, sorts and deduplicates the offsets, then copies complete
bytes. The resulting **51 offsets** are `0x0a..=0x10`, `0x24..=0x4e`, and `0x50`.
Some bits share a byte with other controls or unused bits; the function copies
that full byte rather than reconstructing individual bitfields.

This selection excludes:

- Browse counter byte `0x22` (decimal 34), stored separately in `knob.ctr`.
- Beat FX selector byte `0x16` (decimal 22). The selector occupies the high
  nibble; the low nibble includes transient ON/frequency-button bits
  (`controls.rs:434–436,462–467`).
- Independent FX TIME bytes `0x1a/0x1b` (decimal 26/27), a signed 16-bit
  little-endian counter (`controls.rs:452–455`).
- Press latch, other momentary buttons, X-PAD fields and frame CRC bytes.

A saved map containing those 51 analog offsets plus selector 22 and TIME 26/27
therefore has **54 offsets**, but `mixer_levels` does not create or refresh the
extra three. The legacy preview merely retains prior map entries for them.
`az-drive` can set arbitrary integer keys through `mixer BYTE VALUE`; its frame
builder applies only in-range keys, while the JSON map can retain other keys
(`az-drive.py:335–348,381–388,562–568`). There is no source-wide invariant that
every existing map contains exactly those 54 offsets.

A future mapper must explicitly define the supported set and unrelated-key
policy. It must derive the counter, analog bytes and independently selected
selector/TIME fields from the **same exact acknowledged frame**, preserve shared
low bits, and distinguish selector position from momentary bits. It must not
turn held buttons, latches, CRC bytes or arbitrary map keys into persisted
controls. Strict refusal of ambiguous/unsupported maps is preferable to silently
normalizing them. Exact original bytes/absence remain a separate restoration
record even when a supported map is deliberately reserialized.

## Existing competing paths

This is the base-source writer map. The inherited optional owned path in the
[owned-exit successor](../emulator-owned-exit/) bypasses the legacy mixer
counter/map writes when an owned route is present; its guards are not removed
or contradicted by this audit. Main still supplies the literal absent owned
pair, and the owned settings callback remains injected-only. The paths below
therefore describe base/legacy behavior and external writers that still need
explicit exclusion before a real owned file sink is connected.

The pinned source search found these concrete writers/deleters; it is not an
inventory of every outside program or private maintenance script:

1. The preview UI reads and writes the counter independently of its control
   worker. `app.rs:784–790` also adopts a counter read from disk and updates its
   sent cache without sending a frame for that change. That assumes the other
   writer already delivered it; the file is not proof of delivery.
2. The legacy `ctrl_thread` caches frames before sending and replays the latest
   frames on reconnection. It calls `mixer_state_store` after successful socket
   `write_all`, both for replay and ordinary mixer messages
   (`controls.rs:908–952`). Socket success does not acknowledge the kernel
   generation or native consumption. A reconnected older cache can rewrite the
   map after another writer changed it.
3. Each `az-drive.py` invocation independently reads/modifies/truncates these
   files. Its transport chooses a socket or an SSH write to the control device
   (`az-drive.py:89–130`); it does not route file writes through the preview's
   settings callback or a common host-file lock. Kernel refusal of a legacy
   frame would not undo the host file change.
4. Both boot scripts remove `knob.ctr`, `mixer.state`, `deck1.state` and
   `deck2.state` at their start (`az-boot-m1.sh:25–34`,
   `az-boot-win.ps1:67–78`), before their later startup validation. Preview
   `boot.rs:50–56,169–176` invokes those scripts. A boot attempt need not succeed
   to have reached deletion. The fixed `mixer.state.new` is not in that list.
5. `tools/az-emu-test.py:236–240` independently removes the same four files
   after its boot checks. That script was only read here.

The scripts' removal of deck state does not add those files to this pair engine's
scope. Unrelated files must remain outside its writes/restoration.

## Integration still required

A writer lock used only by the new component does not exclude these older
paths. Required external ownership must cover the relevant directory and both
files across snapshot, commit/readback and eventual restoration, and must
address preview UI writes, cached replay, independent driver processes, boot
reset paths and symlink/path replacement. An ownership assertion by the caller
is not evidence that these writers have stopped. Kernel overlay fencing, a
joined local worker and a file precheck each cover different boundaries.

The real `SettingsSink` still needs a reviewed mapper and lifecycle connection
from the producer's validated full `FrameAck`. Same-session callbacks must retain
ACK order and exact instance/run/epoch/request identity; a later callback must
not rewrite older settings under a new identity. A known kernel commit remains
known if the file callback errors, panics or completes only one replacement.
Retain separate per-file progress, original identity/bytes/mode/absence and
uncertain rename/durability status; never replay the native FRAME to repair a
host-file failure. Successful file persistence does not prove a native poll.

The older [recovery contract](../emulator-overlay-handback/RECOVERY-CONTRACT.md)
records exact counter restoration but does not yet supply equivalent mixer-map
restoration. Recovery must preserve **both** original files and the original
complete overlay/mode, hold producer and host-writer exclusion, resolve pending
request outcomes, and verify the intended native application's stopped
process/start identity before overlay/host restoration and any guarded restart.
Neither a file-pair commit nor `boot::stop` supplies that native-stop proof.
Ordinary ACK-bound persistence and stopped recovery are separate operations;
this review does not require stopping native playback for every normal file
commit. No mapper, restoration coordinator, live factory or stop authority is
implemented by this document or inferred from the byte-pair engine.

## Source fingerprints

All entries below are SHA-256 of `git show` bytes from the emulator commit above.
No full source dump, host settings, private path or runtime observation is
included in this note.

| Committed source | SHA-256 |
|---|---|
| `az-preview/src/config.rs` | `0a1986611ec2642379a42f1f517d8d1decf1764e0cee8ab2f37c3e6c04782992` |
| `az-preview/src/controls.rs` | `b9145e4b41439761abbfdec461b854412748af2fe95183fd5843ad34d4298445` |
| `az-preview/src/app.rs` | `f6a7bb28c3f2dbce759582ac2c7b1147adf93f5137838a833ed5d35f1834864b` |
| `az-preview/src/boot.rs` | `f907da478aa758d4cb2a9f740f529fbe256376230d77635a061a746ce75ec1eb` |
| `tools/az-drive.py` | `6f88798ec54785dc2ce9180e5c2839a1a8e8ec370c997bc7f48e8d1cc06e640f` |
| `tools/azemu.py` | `fdc5674f1729fafa3d0f0c48cd3877780e5dddd95b26249c0d0c9ec0334baeb5` |
| `tools/az-boot-m1.sh` | `65b4c3b298c51819ccb6dbed526883bdc9a47ce74e29e096c4eccf1d9b2bc285` |
| `tools/az-boot-win.ps1` | `c6d99d0d924097234d3b9905816d0ac1860f11a599b707a387f9744e5b007fc1` |
| `tools/az-emu-test.py` | `97c3e92acfcd9333861a420bdac773807283f9b95a9a6de7fa27d6815d562796` |
