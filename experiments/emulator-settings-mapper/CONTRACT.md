# Pure ACK-to-settings mapping contract

This inactive Unix module projects an already selected full `FrameAck` into
`PairTargets`. It performs no I/O, authenticates no receipt and supplies no
sequence or lifecycle authority. The existing producer validates the exact
pending envelope and sequence before its settings callback; the separate pair
engine enforces its attached key and contiguous ordering before file changes.
The mapper alone proves neither step occurred.

## Explicit projection

Every output updates the decimal browse counter from frame byte 34 and the
51 complete analog bytes at offsets 10–16, 36–78 and 80. Complete shared
low-bit bytes are retained; reconstructing just one control's bits would
discard other controls. The caller explicitly chooses whether to refresh the
FX selector and FX TIME; there is no default selection.

When selected, selector key 22 receives only the frame's high nibble. The low
nibble contains momentary buttons and is never copied into persisted settings.
TIME keys 26 and 27 receive their exact little-endian byte pattern together;
this is a counter snapshot, not an instruction to turn the encoder. No signed
conversion, clamping, wrapping adjustment or idle substitution is performed.

Supported but unselected selector/TIME entries retain their values and absence.
They are not refreshed from another frame or cached model. A selection can add
previously absent fields. All refreshed values come from the same immutable
ACK. A held button may be present in the ACK without being persisted.

The result keeps the entire exact ACK, including identity, kernel sequence and
unselected/transient bytes, beside private target bytes. Borrowed accessors do
not permit edits; consuming the result yields that ACK and pair. Callers can
still misuse the extracted values. This type is not a capability and cannot
prevent a later caller from separating or replacing them.

## Accepted saved map

Absent input means no prior map. Present input must be a complete bounded JSON
object in a strict subset:

- Only canonical quoted decimal byte-offset keys and unsigned decimal byte
  values, with standard JSON ASCII whitespace.
- No duplicate keys, aliases, escapes, signs, leading zeros, fractions,
  exponents, booleans, nulls, comments, nested structures or trailing data.
- Only the 51 analog offsets and optional selector 22 / TIME 26–27 are accepted.
  Unknown keys, including arbitrary frame offsets, are refused.
- A prior selector value must have its low nibble clear. TIME must be present
  as both bytes or neither. These checks also apply when those fields will be
  refreshed; stale unsupported content is not silently discarded.
- Input is limited to 65,536 bytes before parsing. Present-empty input is an
  error, distinct from absence and an empty object.

Output is deterministic numerical key order, quoted decimal keys, decimal
values, colon/comma spacing and no newline. The counter is decimal digits with
no newline. Original formatting is deliberately not preserved in a mapped
target. Exact original bytes, permissions and absence remain the separate pair
engine's custody; mapping is not restoration.

Unsupported unrelated map keys are refused rather than dropped or carried into
a later raw control frame. The supported unrelated settings here are the
unselected valid selector/TIME entries. Other host files are outside this
component. This strict policy does not claim compatibility with every
permissive legacy map.

## Boundaries that stay open

`FrameAck` carries a fixed 152-byte envelope but no bus/version or native
authentication token. The mapper does not independently interpret headers or
require a particular CRC: the kernel restamps CRCs and the existing producer
uses complete opaque frames. These are projections from the caller's intended
mixer ACK, not evidence that a native process consumed it.

No real `SettingsSink` bridge, writer/reader exclusion, file restoration,
durable journal, live factory, backend acquisition or native-stop/restart
authority is introduced. A future bridge must retain the same original pair
and mapped plan across callback failure/panic and partial file outcomes.
A host-file error must never turn a known kernel FRAME commit into replayable
work. Kernel fencing, mutex serialization and file identity checks do not
exclude all legacy or external file writers.

No emulator/device endpoint is accessed. Main remains inactive. All existing
native observation, USB, staging, recovery and release gates remain closed.
