# Independent source and fixture review

No blocking defect was found for this **uninstalled kernel primitive**. The
reviewed patch is
`fdf40df07b76e22f1684186e4d725ccc5d1cc56e24d5cb9a7f7813534a045352`.
Its candidate C SHA-256 is
`5ea2023ccb8e058fad66b7317d545fc7a0328bcc5aae624cfc18c9769cf87124`
and ownership-header SHA-256 is
`a9f2cfa4d007053806ca97f3058c72bffd43756995cb23235c415358a3c7e990`.
The [pins](candidate-pins.json) preserve the frame header and Makefile unchanged.
This conclusion is not a stopped-recovery, installation or live-input permit.

## Operation and observable-state boundary

The new command has its own exact native-64 encoding and 208-byte input object.
The ioctl selects that local object and its header before usercopy; the older
48-byte command and 200-byte frame destinations are unchanged. A failed or
partial copy returns before validation or mutation. The complete V2 header,
strict 0/1 mode and both reserved fields are checked. Mode 0 requires an exact
match of every raw template byte, including CRC positions and tail; immutable
template comparison can safely precede the state lock. There is no copyout after
a successful mutation.

Under the existing final lock, module instance, owning file, run, epoch and exact
next sequence are checked before any state change. The additional epoch-capacity
check prevents wrap. The complete payload/mode, epoch increment and sequence
reset commit together. File ownership, run, instance, fence, CRC policy and
other buses are retained. FRAME and ROTATE keep their prior request shape and
behavior. Sequence MAX already reached refuses; a request at MAX from current
MAX−1 may still perform the final permitted rotation when epoch capacity exists.
A resulting epoch MAX supports the existing FRAME semantics but cannot rotate
again. This is exhaustion, not a recovery escape hatch.

The relevant raw state is **selected source bytes plus mode**. STATE copies
`inject_pending[2]` when injected and the immutable mixer template otherwise.
A mode-0 RESTORE also overwrites the dormant injection buffer with the template;
that is not restoration of its previous, unobservable buffer history. Identical
bytes in injected and idle modes remain different states. The kernel does not
know whether the supplied mode/frame was the original admitted baseline.

SPI reads snapshot selected bytes and the CRC prefix under the same lock, then
stamp CRC into a local copy after unlocking. Thus a delivered SPI frame is not
necessarily the raw storage representation needed for a restoration receipt.
The existing claim requires mixer prefix 96; the permanent fence prevents the
CRC setter from changing it afterward. RESTORE neither changes that prefix nor
adds arbitrary CRC restoration. Service-mode button OR affects only ErP bus 0,
not owned mixer bus 2. An old SPI or STATE snapshot can finish copyout after
RESTORE; the operation cannot retract that observation or a native event already
sampled from it.

The generation transition rejects old tagged mutations reaching the lock later.
It does not authenticate a producer or prevent a cooperating caller from
reattaching a fresh valid tag to old bytes. Kernel return zero acknowledges this
mutation only. Historical STATE, worker join, mode 0 or byte equality supplies no
native-stop, neutrality, producer-readiness, native-consumption or host-file
restoration proof.

## Exact-source test review

The runner exports pinned Git blobs, validates all three captured patches and
checks each intermediate inventory before further application. It does not use
uncommitted emulator working-tree sources. Fourteen complete candidate functions
and their actual shared declarations are extracted into a host fixture, with the
unchanged UAPI and frame headers. Tests call those functions rather than a
parallel model of the transition. The pthread lock, usercopy, privilege, entropy,
integer-parser and scheduler shims remain explicit limitations; they do not
prove Linux page-fault, VFS, spinlock, interrupt or module-lifecycle behavior.

The complete candidate path contains 148 reported checks: 46 authorization/ABI,
42 mode/fence and 60 scheduling checks. The 152 individually altered idle bytes
are one aggregate assertion; thread creation and joining checks are included.
There are 26 additional static source/ABI integration checks. The frozen parents'
whole suites were not rerun.

All 11 controls have unique edits and intended failure labels. Nine select a
single functional check; stale-generation and atomic-transition cases include
thread creation/join plus their functional check. Each control must compile,
terminate and fail exactly its named assertion with the expected count and exit.
Sanitizer diagnostics, compilation failure, timeout or unrelated failure do not
satisfy the oracle. Controls distinguish forced injected mode, omitted payload,
missing epoch advance, missing sequence reset, ignored idle mismatch, epoch wrap,
ignored partial copy, missing owner/instance checks, accepted stale epoch and
splitting mode/payload from generation publication.

Forced schedules cover old FRAME/ROTATE/RELEASE/RESTORE copies across restoration,
a copied RESTORE losing to each competing committed operation, completed private
copy isolation, coherent historical STATE and SPI returns, and a single whole
winner for concurrent mutations. The retained fresh-tag counterexample preserves
the upstream provenance limitation. Checks exercise both initial selected modes,
full payload/tail matching, mode distinctions despite equal bytes, fencing,
other-bus operation, CRC behavior, close retention and integer exhaustion.

## Retained fixture corrections

Two earlier attempts remain private and are not passing evidence:

- The first stopped at compilation because a fixture-only `readonly` variable
  was unused under `-Werror`. Removing it did not change kernel candidate bytes.
- The next plain run reported 148 checks and two failures at
  `restore_cannot_retract_already_sampled_spi_frame`. Source inspection explains
  the defective schedule: consecutive STATE then SPI launches reused
  `gate.waiting=gate.resume=1` from the completed first job. The second
  `wait_held()` could return before its SPI snapshot, and its `hold()` could
  proceed without pausing. Those assertions did not establish the intended
  already-sampled boundary.

The final fixture resets both flags under the gate mutex before each worker is
created, after the previous paused worker has joined. Thread creation error now
fails immediately. This supplies a fresh handshake for each actual pause; it
neither relaxes the assertions nor changes the kernel. The final fixture hash is
`20d6c0b18b594e5b7c2c496aefd215cc81b14e4748fc58bbe47659a066bd715d`.
The comparison records earlier fixture/log hashes and confirms identical kernel
candidate bytes across those attempts. Passing repeats alone were not used to
explain the failure.

## Independent reproduction

One fresh independent matrix passed 148 candidate checks in each of plain,
ASan/UBSan and TSan, plus all 11 strict controls in each mode: 36 variant/mode
executions per matrix. [Primary](test-result.json) and
[independent](test-result-independent.json) JSON are byte-identical with SHA-256
`9f66159d2acedc789efbe32513392748ff4dee4f5cf1b4e5cecfe2b8190662a4`.
No fields were excluded or normalized.

The [independent comparison](comparison.json), SHA-256
`cd71a606bcd719192a308e12859840347ebe4a6fbb3786c923574401ea70df03`,
re-extracted the 14 inspected functions from both retained C sources without
importing the runner's extraction routine. It reconstructed globals and every
single declared source mutation, checked all 24 generated variants, and matched
all 72 retained mode outputs, failure labels, counts and hashes across the two
matrices. Compile logs were empty. Full source snapshots, generated programs,
compiler outputs and failed-attempt logs remain private. This does not claim
byte-identical linked binaries.

No kernel, native application, real backend, device endpoint, host-file restore,
service operation or physical control ran in this review. ARM64 compilation is
separate evidence. Baseline authority, producer exclusion, uncertain-request
accounting, verified native stop, both-file restoration, guarded restart, native
dwell and final resource custody remain unresolved. Runtime and staging gates
remain closed.
