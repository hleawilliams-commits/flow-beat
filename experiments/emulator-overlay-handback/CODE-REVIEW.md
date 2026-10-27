# Independent source and fixture review

No blocking defect was found in the reviewed offline V2 kernel candidate,
preparation helper, or frozen host fixture for the bounded contract below. This
is a read-only code review, not an installation approval or an end-to-end
producer handback result. The reviewer did not rerun the tests, load a module,
contact a device, change the original emulator checkout, or alter the candidate.

## Reviewed inputs

| Input | SHA-256 |
| --- | --- |
| Successor patch | `fa800f32c5e9227cc1953028d35b896e711a40b0d4fd6f2e03dcdf2ac6eaf0dd` |
| Prepared candidate C | `bab273a308dde0ba696394d2383461af10413f1975e836b2058456dba2833717` |
| Prepared V2 UAPI | `355316f5047509ae4c787210fddf0bd8b659c600ac1e153adf0543e28cf35aec` |
| Preparation helper | `6a35c4ac9c3d9671d1d652bbba57ea6a49f2e4f8865427c1d58751b340ffad84` |
| Frozen test runner | `1a8030db691cc6df52bf5afc7d94fd460360875f3c535a43070c859ed2745f83` |
| Frozen host fixture | `37882354077f05ee87a5f9655a8325380d02c4a43b292081d8ecb8fd49ead9a4` |
| Recorded host result | `112935e5fded38c073e40b1fb46f9b24c890f1cb187cff2ab2d1d6d3d94c7217` |
| Corrected ABI review | `86b18f2f5145d586d7e351f3afc3daf42344e91e63152a8c7d95b062362f1640` |

The prepared source is the pinned emulator base plus the unchanged final
phase-one patch, then this separate successor patch. Preparation checks all
input hashes before writing a new directory, supplies the already-verified
patch bytes on standard input, saves those exact bytes, and checks both
intermediate and final inventories and hashes. It does not reopen patch paths
for application, overwrite the original source, or implement runtime actions.

## Code boundary

Input copy and header validation occur before the final mixer lock. Inside that
lock, all four mutation commands compare both instance words. CLAIM checks the
current epoch and owner state; FRAME, RELEASE and ROTATE use the existing
same-file/run/epoch/exact-next-sequence predicate. A request copied before a
rotation therefore does not acquire authority merely by having passed usercopy.

ROTATE checks both sequence and epoch exhaustion before changing anything. Its
full frame, injected mode, incremented epoch and zero sequence commit under one
lock. It retains the same owning open file description, run, instance and legacy
fence. It neither transfers ownership to a different producer nor reopens the
legacy mixer path. The last valid epoch/sequence values do not wrap. Exhaustion
can leave no further tagged mutation available; final close still only orphans
ownership and retains the overlay and fence.

The instance helper is called and its error checked before the first device
allocation in the actual module-init body. The disabled path does no RNG work.
The enabled path waits outside a spinlock, returns a wait error before filling,
and refuses an all-zero draw. No fallback identity is installed. This establishes
a source ordering, not entropy quality, bounded initialization time or identity
uniqueness. VM snapshot restoration can repeat the complete stored identity and
state.

Exact V2 command encodings select their fixed input/output sizes. Old V1
encodings have no fallback; malformed V2 versions, reserved fields and zero run
IDs are refused. STATE is zero-initialized and snapshots the raw frame and
metadata under the lock, then copies out after unlocking. Its reserved output is
initialized. It is readable without mutation authority and is not a permit,
CRC-restamped wire frame, native-consumption acknowledgement or cancellation
acknowledgement. There is no compat ioctl handler.

The successor leaves phase-one CRC parsing, callback registration, parameter
type metadata and final-mutation fencing unchanged. This does not broaden the
phase-one parser claim: the custom parser/getter is not proven lexically or
textually identical to Linux's original array parameter machinery. File lifetime
still relies on actual VFS open-description/reference rules; a host struct and
pthread mutex cannot validate those kernel rules.

## Fixture discrimination

The runner extracts the complete actual bodies of 14 functions and their needed
declarations, together with the unedited UAPI and pinned frame header. Real
control-handler and parameter registrations, layout assertions, and module-init
ordering are checked separately in source. The full module-init body is retained
and hashed; the fixture executes only its extracted identity helper, not kernel
allocation or cdev publication. Frames are synthetic owned buffers; full source,
frame headers, binaries and logs remain private.

The review found the new generation and atomicity controls appropriately
separated:

- Old FRAME, RELEASE and ROTATE requests pause after their actual input copy.
  The same owner then rotates. Their old numeric sequence 1 still equals the
  new epoch's next sequence, so rejecting them tests generation rather than an
  accidental sequence mismatch. The epoch-removal mutant fails that boundary.
- The split-commit mutant unlocks after the new payload is installed and before
  the epoch/sequence update. The fixture's unlock handshake makes an actual
  STATE call observe the intermediate state. The ordinary candidate exposes only
  the completed transition. The payload-omission mutant suppresses only ROTATE's
  copy, keeping its other updates intact.
- Both instance words are tested on every mutation. The dedicated mutant removes
  the actual instance comparison. Equal counters with a different simulated
  module identity remain rejected; that simulation is not an actual module
  reload or VM-clone test.
- Disabled initialization, interrupted wait without fill, all-zero refusal and
  either nonzero instance word are distinguished. Their RNG service is deliberately
  deterministic. Exact V1 commands are rejected before usercopy. Partial copies,
  malformed headers, authorization loss, both exhaustion boundaries and retained
  owner/run/frame semantics have explicit assertions.
- Forced schedules cover historical STATE and SPI snapshots, old requests crossing
  rotation, a rotation losing its sequence to FRAME, and competing FRAME/ROTATE
  or ROTATE/ROTATE requests. The inherited 1,000-frame read/write case has a
  midpoint handshake, but remains an ordinary-FRAME stress case, not a rotation
  stress test.

The two limitation cases are important: STATE may report the old sequence before
a pending copied FRAME commits, and reauthoring an old payload with a current
valid header is accepted. Neither is treated as a kernel defect or hidden by an
assertion. They prevent the test result from being interpreted as cancellation
or upstream producer freshness.

The frozen runner requires exactly 228 candidate assertions with zero failures.
Its ten mutants must produce their specific single functional failure, exact
case assertion counts, and exit code 1. Sanitizer diagnostics are rejected for
both candidate and controls. The [host test note](HOST-TESTS.md) records the
three-mode result and the parent's independent byte-identical repeat. This
review inspected that evidence and the source; it did not add another run.
Inherited phase-one assertions execute on V2, while the seven earlier phase-one
mutants retain their separate frozen evidence and are not rerun by this runner.

## Documentation correction and remaining gates

Review identified an overly broad earlier description of resolving an uncertain
syscall through STATE. The corrected ABI review now limits positive reconciliation
to the same live description, one unresolved operation, exact retained command
identity/payload, matching state and no intervening admission. A lower sequence
cannot prove cancellation; failed queries and conflicting or pending operations
remain unresolved. The candidate did not change for this wording correction.

The current README and [recovery contract](RECOVERY-CONTRACT.md) retain the
necessary limits: ROTATE installs opaque bytes immediately, cannot certify
neutrality or stop an application, and cannot retract already-sampled native
events. Producer generation must be attached at creation and survive queues and
reconnects without retagging. A future broker must retain the owning descriptor
and account for uncertain writes. No such adapter exists here.

Stopped recovery must preserve an admitted full baseline, revoke old generations,
restore the host/overlay pair under retained stop and closed admission, and
separately verify restart. Matching bytes alone do not restore a noninjected
baseline: FRAME and ROTATE always set injected mode. There is no atomic host-file
and kernel-overlay transaction. These remain unimplemented requirements, not
conditions established by this review, the host tests or the isolated build.
