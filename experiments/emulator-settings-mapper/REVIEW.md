# Independent source and fixture review

The production author, fixture author and independent reviewer worked on
separate source/review tasks. The root reviewed their contract and integration,
then ran the final focused matrix. The independent reviewer separately audited
the source, preparation lineage, control anchors and retained runs.

The reviewed projection matches the pinned writer audit: 51 complete analog
bytes, knob byte 34, optional selector high nibble and paired TIME bytes.
Unknown settings and ambiguous input are refused; valid unselected extras
retain values and absence. All refreshed values come from one immutable ACK.
Complete ACK custody remains distinct from authentication or native consumption.

The independent fixture uses an enumerated offset set rather than the mapper's
helper. It also checks actual preview parser/control interoperability. Every
mutation has a unique production anchor and a function-local assertion location;
the runner requires exact names, counts, exit status and assertion location.
The initial custom-assert diagnostic mismatch and equivalent fixture repair
are retained in [HOST-TESTS.md](HOST-TESTS.md).

## Preparation issue found and corrected

The inherited preparers use Python assertions to verify frozen source pins.
An optimized interpreter could strip those checks, including dynamically
compiled parent code. The new preparer and runner now explicitly raise before
preparation when `__debug__` is false. These checks are ordinary conditionals,
not assertions. Independent read-only `python3 -O` invocations of both entry
points exited 1 at that refusal before argument processing/private output.
Frozen parent files were not edited.

Preparation verifies the 20-file settings-pair parent and all captured parent
inputs. The successor contains 22 files; only main's inactive module declaration
changes among parent files. Dependencies, producer, worker, lifecycle and
settings-pair source remain byte-identical. Parent and successor captured inputs
use separate directories. Main still supplies a literal absent owned pair.

## Limits and next concrete composition

No source correctness defect remained in the reviewed mapper. Passing host tests
do not supply a real `SettingsSink` bridge. That bridge must select policy
explicitly, derive from the producer's validated ACK, retain the same pair
engine/originals, and expose callback refusal/panic plus partial file stages.
It must not replay a known kernel FRAME to repair a host setting error.

External preview/driver/boot writers and pair readers still need enforceable
exclusion. Native backend acquisition/FFI, authoritative original overlay and
host baselines, scoped stopped-process identity, both-file/overlay restoration,
durable custody, final Drop/process-death handling and guarded restart remain
open. No runtime, staging, native ownership, USB or release gate is reopened.
