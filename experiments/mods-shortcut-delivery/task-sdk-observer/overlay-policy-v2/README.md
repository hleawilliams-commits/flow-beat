# Full-overlay policy v2 prototype

This is a separate pure prototype for preserving the complete supported host
mixer overlay. It performs no ambient I/O, imports no driver/runtime adapter,
issues no input permit and installs nothing. The frozen 28-offset policy,
earlier private helpers and closed read-only attempt remain unchanged.

The later [writer/recovery checkpoint](RUNTIME-GAPS.md) demonstrates queued
preview replay and two sticky-overlay recovery gaps. The separate V2 planner
remains inert; none of these preservation checks enables a runtime action.

The earlier attempt refused 54 saved offsets before acquiring a frame. Dropping
those fields would change state. V2 instead validates an explicit schema from
the pinned host preview and driver source; a saved file's existence or purported
origin never makes its contents trustworthy. The preview merges prior keys and
the CLI permits numeric offsets, so unknown keys remain errors.

## Representation

The supported set contains exactly 54 byte offsets:

- 40 analog high bytes and 11 packed low-bit bytes from `MX_AT` and `MX10`;
- byte 22's high nibble for the FX selector;
- both bytes 26 and 27 for the separate FX TIME counter.

Full-byte analog fields preserve all eight bits. Byte 16 permits only bits
0–1; byte 80 permits only bits 4–7. Their other bits must remain pinned zero.
All four low bits of byte 22 must be zero: they represent active FX ON and
frequency-button bits. This is stricter than the frozen first policy's bit-0
check. FX TIME must be supplied as both bytes or neither; its exact two bytes
are preserved without interpretation, increment, reset or wrap handling here.
The local preview describes that field as signed-16 little-endian; this is a
host-source label, not a new observation of native behavior. The pinned preview
source explicitly leaves its FX selector/ON-OFF behavior in the emulator
unconfirmed; preserving those bytes makes no native-effect claim.

`parse_mixer_state(raw)` accepts `None` or a bounded UTF-8 JSON object with
unique canonical decimal keys and integer byte values. It rejects duplicates,
booleans, fractions, nested/nonfinite values, active/reserved bits, unknown
offsets and partial FX TIME pairs. It neither clamps nor drops values.

`neutral_frame(idle152, state, counter)` revalidates direct dictionaries and the
exact pinned 152-byte template. It represents every present setting, preserves
absent fields from that template, and supplies the independently scoped browse
counter in 1–254. It never edits an observed frame or claims that filling absent
settings matches the current controller. Actual full-frame comparison is still
required before any separately reviewed action workflow.

## Full-frame and action checks

`validate_frames(first152, second152, planned152)` checks both observed CRCs,
the neutral shape and all 152 bytes against the plan. Only browse counter byte
34 and CRC bytes 96–97 may differ from the plan; the two observed browse counters
must agree. FX TIME and every analog bit must match. All protected bits,
including the 24-byte tail outside the earlier diagnostic, are bound to a
masked hash of the exact pinned template. Two equally corrupted observations
and a matching bad plan cannot make protected bits valid.

`action_payloads(observed152, kind)` takes one immutable, CRC-valid neutral
observation and returns `(first152, release152)` without a bus prefix. It copies
the entire supplied observation. BROWSE changes the first frame's latch byte 6
to 0 and byte 32 to `0x40`, then returns to that baseline. CW/CCW change the first
latch and browse counter by exactly +1/−1; release retains that target counter.
Both boundary wraps are refused. Every other byte, including FX TIME, packed
analog fields, reserved bits and the copied CRC bytes, is unchanged.

Copied outgoing CRC bytes are intentionally not recomputed. The reviewed
controller writer restamps read results; these payloads are not represented as
new valid observed-input frames. `action_payloads` alone neither proves that
its argument came from a recent paired comparison nor grants ownership. The
caller must bind the exact fresh observation, process/run, action sequence,
file ownership, exclusive writer assumptions and release policy separately.

## Offline evidence

The [sanitized result](result.json) records **2,914 checks**, including 2,603
rejection cases and four discriminating mutation controls. The suite extracts
templates and analog masks from exact pinned local sources without importing
those sources. It covers all values at the three restricted offsets, every
protected bit, all 149 compared byte positions, malformed settings, incomplete
pairs, held controls, CRC failures, no-wrap actions and exact payload equality.
The four mutants drop FX TIME, omit its comparison, release the old browse
counter, or erase packed analog low bits. Each loses its targeted invariant;
no unrelated failure is accepted as discrimination.

The complete author result is retained privately with SHA-256
`98a6a23973172fd48b0fe7449cda87d286297cbff216fe4a370c23e070cf86bb`.
The policy source is `10f0cd0d161862c0b2cefa9cb817e6986261429134be2b88ebc64534969df03c`
and the runner is `667865e356c740eee2d8aa729f5e9fd7371f5fbb1690fec2ea95551ec9292977`.
Independent execution produced byte-identical input fingerprints and full result
JSON, with the same counts and four discriminated controls.

A separate pure local check parses all 54 entries of the retained refused
settings and confirms that template construction preserves every present byte.
The settings file is unchanged. This historical-file check supplied no observed
controller frame and cannot serve as a later action baseline. Raw templates,
settings, source snapshots and full result details remain in ignored private
output; the public result contains hashes and scoped summaries only.

```sh
python3 test-policy.py --driver /private/path/az-drive.py \
  --controls /private/path/controls.rs --config /private/path/config.rs \
  --frames /private/path/subucom_frames.h
```

Each run creates a new ignored `out-history/run-<uuid>/` directory and preserves
its inputs, result and any failure. No controller, emulator, service, package or
physical state is touched by this runner. Host-source field labels, copied-frame
consistency and passing pure tests do not prove exclusive overlay ownership,
native delivery, event order, timing, audio quality or crash recovery. The
kernel accepts the last complete writer's overlay; this prototype excludes no
other writer. Integration and any future fresh runtime attempt require separate
review and evidence.
