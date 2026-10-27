# Commit acknowledgement does not establish native button dwell

Two successfully committed owned frames can replace one another before the next
SPI read. A dedicated bounded worker preserves local admission order; it does
not turn the kernel overlay into an event queue or prove that a native handler
observed every press.

This note and the focused fixture are separate from the preview transport
implementation. No emulator, device, native handler, broker transport, service
or installed module was executed.

## Exact source boundary

The kernel source is the unchanged offline V2 candidate from
[emulator-overlay-handback](../emulator-overlay-handback/README.md), based on
emulator commit `06fc981707e69982c0554bb508e0c646c005a979`. Its prepared
`guest/modules/subucom_virt/subucom_virt.c` SHA-256 is
`bab273a308dde0ba696394d2383461af10413f1975e836b2058456dba2833717`.
Line numbers below refer to that exact prepared file, not the unpatched checkout.

- `mixer_owned_next`, lines 498–509, checks the owning file, run, epoch and exact
  next sequence. It does not inspect a consumer acknowledgement.
- `ctrl_ioctl`, lines 511–609, handles STATE and tagged mutation. FRAME copies
  all 152 payload bytes into `inject_pending[2]`, sets `has_inject[2]`, and
  commits the sequence under `miso_lock` (lines 564–607). Success returns after
  that commit. A later valid FRAME replaces the same slot.
- STATE copies the raw overlay and counters under the lock. Reading STATE does
  not invoke `spi_read`, consume the overlay, or prove firmware receipt.
- `spi_read`, lines 336–391, first calls its scheduling helper, then copies the
  current overlay under the same lock (lines 361–373). It restamps the copied
  frame CRC and copies that local frame to the reader. It does not replay older
  committed values or advance the owned mutation sequence.

These facts support a specific counterexample, not a claim that this schedule
occurred during a prior runtime. The nominal polling interval and the source
comment about approximately 850 Hz do not provide a guaranteed poll deadline.
The scheduling call can also return early on interruption. No firmware-level
acceptance follows merely from a CRC-valid copy.

## Executed counterexample

[run-dwell-test.py](run-dwell-test.py) calls only `prepare()` and `extract()` from
hash-pinned, captured helper sources. It creates a fresh private source copy,
applies the two frozen patches, checks the prepared hashes, and extracts exactly
six unchanged functions: `crc16_x25`, `xput_bytes`, `xget_bytes`, `spi_read`,
`mixer_owned_next`, and `ctrl_ioctl`. It does not rerun either frozen kernel suite.

[test-dwell.c](test-dwell.c) invokes those actual function bodies with synthetic
full frames, one synthetic owning file, a pthread lock, owned-memory usercopy
and successful permission shims. Instance initialization is deliberately outside
this fixture: it supplies a nonzero synthetic identity. The scheduling shim
counts explicit SPI polls without sleeping or measuring time.

The main schedule claims ownership, commits a synthetic “press” marker frame,
verifies STATE at sequence 1, commits its marker-cleared “release” frame, verifies
STATE at sequence 2, and only then invokes `spi_read`. These labels distinguish
the two arbitrary full-overlay values: the fixture changes byte 32 bit `0x40`
and byte 6. It does **not** use the pinned Browse mapping (byte 31 bit 0), invoke
a native knob/button handler, or prove that either synthetic payload is a valid
physical control state. The first and subsequent reads return the latest release
with a valid restamped CRC. There is no earlier press queued for a later read.
The discriminating companion schedule inserts a SPI read between those same
commits and sees the press, then sees release after the second commit.

[Results](dwell-result.json) record 20 passing assertions per plain and
ASan/UBSan mode. The compiler was Apple Clang 16.0.0. Three separate controls run
in each mode:

1. On unchanged source, the deliberately false assertion that every successful
   commit must appear at the next read fails exactly once. This is retained as
   an expected counterexample, not a candidate regression.
2. A source mutant that retains the first overlay instead of replacing it fails
   the exact latest-commit oracle.
3. A source mutant that bypasses the injected overlay when reading fails the
   exact committed-overlay oracle.

Each control must compile and return its exact failure count, named assertion
and exit status. Sanitizer diagnostics are rejected even for expected failures.
All eight compile/execution cases passed their expected outcomes on the first
run. The result SHA-256 is
`9538bde503a6eb695e21c84c0e1b6fcdc5d284596acd3b8c291070c86432efc1`.
An independent parent execution reproduced the complete result JSON byte for
byte, including the same result hash. Private snapshots retain helper bytes,
patches, declarations, all six extractions,
headers, binaries and raw logs. The public result records their provenance and
function hashes without publishing captured idle frame data or local paths.

This serial test is not a kernel scheduling, real VFS/usercopy, entropy,
registration, live polling, concurrency or native-handler test. TSan is not run
for this serial schedule. It also does not execute or authenticate a broker ACK;
the two successful kernel calls are the commit facts a truthful backend could
acknowledge. An unimplemented IPC/FFI adapter cannot acquire a stronger meaning
for ACK from this fixture.

## UI hold time and the bounded port

The pinned original preview defines `MIN_HOLD` as 100 ms in `controls.rs:286`.
The frozen preview-gate candidate records `Instant::now()` when a UI press is
created and uses `since + MIN_HOLD` for deferred release in `app.rs:680–759`;
hidden-view mixer release follows the same creation-time rule at lines 307–319.
The app candidate is pinned by SHA-256
`cacd6793cdee27b71306b74a9e84816902e3e607a784466b6947c9add9515b6f`
and [its source inventory](../emulator-preview-gate/candidate-pins.json).

A press and release authored 100 ms apart may wait in a backlog and later commit
close together. Queue ordering, exact ACK validation, bounded capacity, worker
stop and actual join tests do not establish a minimum interval between those
commits. In particular, the predecessor's 37 selected preview tests and this
successor's transport tests must not be described as native dwell validation.

A future policy measured from a verified positive commit ACK could establish a
local minimum release-submission delay, under its sole-writer and monotonic-clock
assumptions. That would require new implementation and discriminating clock,
backlog, stop and failure tests. It would still not prove that firmware polled,
accepted or dispatched the press. A fixed sleep or an approximate polling-rate
comment cannot close that gap. This checkpoint adds neither such a delay nor a
native-consumption acknowledgement.

## Reproduce the focused test only

```sh
python3 experiments/emulator-preview-transport/run-dwell-test.py \
  --emu ../xdj-az-emu \
  --result experiments/emulator-preview-transport/private/dwell-independent.json
```

The original emulator checkout, frozen kernel candidates and old test suites
remain unchanged. Results and extracted firmware-related headers stay under the
new experiment's ignored private directory.
