# Retained original-fd and CLAIM owner

This separate inactive C component holds a caller-transferred original
controller descriptor before any syscall. It records one preflight STATE,
one CLAIM and one postflight STATE without implicit cleanup or retry. It
addresses the original-descriptor/CLAIM custody boundary identified in the
[acquisition review](../emulator-wire-native64/ACQUISITION-REVIEW.md).
The [retained adapter](../emulator-retained-adapter/) remains a separate frozen
component; no adapter, broker, dispatcher or live endpoint is constructed here.

mbco_create copies a valid plan and constructs immutable native64 CLAIM bytes
without taking a resource. mbco_take stores the supplied open descriptor,
including fd0, before setting the caller's slot to -1; it does no allocation
or I/O. The caller must already own that slot and retain the returned owner.
Taking a number does not authenticate a controller or exclude file aliases.

Each syscall stage is armed once before I/O. Its exact return is copied before
interpretation; STATE also retains all 208 initialized/returned bytes, including
partial failed copies. CLAIM receives a separate typed local copy of its
48-byte command, preserving the authored intent even if a scripted boundary
overwrites that argument. Failures retain the owner and original descriptor.
Refused repeats perform no I/O and do not replace the evidence.

## Exact transition and outcome

The expected prestate must be enabled and unowned, with nonzero instance, CRC96
and room for one epoch increment. Fresh unfenced flags16/24 require prior run,
epoch and sequence zero. Fenced unowned flags17/25 require nonzero prior run
and epoch; an arbitrary prior sequence, including UINT64_MAX, is permitted.
The selected new run must be nonzero; uniqueness/provenance is external.
Preflight checks the entire supplied state before CLAIM is admitted.

| Stage | Admission and retained result |
|---|---|
| Preflight | Complete version/reserved/shape/state match; failure retains raw STATE and fd without attempting CLAIM. |
| CLAIM | Exact version2/reserved0/instance/new-run/prior-epoch/sequence0 command. Raw return and classified outcome remain separate from readiness. |
| Postflight | Only after CLAIM returns zero. Require same instance, all152 raw bytes, CRC and injected/idle mode; new run, epoch+1, sequence0 and owned/fenced flags23/31 must match. |
| Ready getter | Copy of the historical validated poststate; no syscall, fresh ownership proof, native-stop authority or producer admission. |

CLAIM zero return is Committed; stale errno remains recorded but does not turn
success into failure. Exactly -1 with EBADF, EPERM, EOPNOTSUPP, EFAULT, EINVAL,
ESTALE, EBUSY, EOVERFLOW or ENOTTY is Rejected under the pinned handler.
All other failed/abnormal returns are Unknown. EACCES belongs to other mutator
paths and is deliberately absent from this CLAIM list.

Rejected means this command did not commit. A same-file alias may already
have claimed the overlay, so Rejected does not imply an unowned descriptor or
permission to close it. Unknown cannot enter postflight or be promoted by a
later matching snapshot. A failed/mismatched postflight after a zero CLAIM
return preserves the Committed outcome and retains the original descriptor
and acquisition evidence.
No automatic FRAME converts an idle acquisition to injected mode.

## Limits and next boundary

[Host evidence](HOST-TESTS.md) describes exact extracted driver methods under
serial synthetic syscall/fd/usercopy/lock fixtures. The runner only exports
pinned local emulator source to a new ignored private directory; it contacts no
emulator or device endpoint. Raw exports, logs and binaries remain private.

Inspection is externally serialized. Same-thread syscall-fixture inspection
does not establish blocked-I/O snapshots, concurrent access or durable custody.
Exposed diagnostic fd numbers remain usable in-process; callers must not mutate,
close or duplicate through them. External alias exclusion is unenforced.

Explicit disposal requires independent quiescence and uncertainty/ownership
accounting. It consumes the handle before one close when a descriptor is held,
retains the raw close report and never retries. Final release of the owning
file description clears that owner but preserves selected content and the
permanent fence; it is not restoration. Closing a rejected contender does not
clear another file description's ownership. Synthetic fixture cleanup supplies no runtime teardown permission.

A matching prestate can change before CLAIM; the driver does not compare that
content during CLAIM. A matching poststate can also become stale immediately.
The supplied expected state, module/run values and local Ready phase provide no
authoritative original baseline, native stop, authenticated endpoint or physical
timing proof.

Next compose this retained owner with the retained adapter, broker and dispatcher
while retaining every failed construction graph. Checked destruction, owned
transport/supervisor binding, deadlines, external writer exclusion, complete
overlay/both-file recovery and durable/process-death custody remain open.
Nothing is installed or wired into main. Existing stems-staging, USB, observer
runtime, recovery, audio-quality and community-release gates remain closed.
