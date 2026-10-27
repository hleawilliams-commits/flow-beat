# Broker / kernel interaction evidence

The final `run-tests.py` result is
`6a39c31dba05581c3930982e4f09181ac3fae2d078d3920eb02de1b0d856c96a`.
The candidate passes **203 assertions** in each plain, ASan/UBSan and TSan mode.
Six source mutants per mode each fail exactly one named assertion, with no
sanitizer diagnostic. Independent reproduction and review are recorded in
[REVIEW.md](REVIEW.md).

The runner verifies the frozen preparation/extraction/shim helper hashes,
applies both frozen kernel patches to a fresh private copy of the pinned
emulator sources, verifies the resulting source hashes, and extracts these
seven functions unchanged:

- `mixer_instance_init`
- `xput_bytes` and `xget_bytes`
- `ctrl_release`
- `mixer_owned_next`
- `ctrl_ioctl`
- `ctrl_write`

The kernel implementation is not replaced with a state-machine model. Backend
callbacks explicitly translate C semantic fields to the actual V2 request
structs and call the extracted `ctrl_ioctl`; STATE decoding checks wire version
and reserved fields. The kernel lock, usercopy, file objects and RNG are host
shims over owned memory. Full source, generated extraction and binaries stay
private. Unused shim functions/variables are allowed because this suite selects
seven of the earlier fixture's methods; the broker itself separately compiles
with all warnings treated as errors.

## Behaviors exercised

Complete initial and adoption snapshots bind both instance words, run, epoch,
sequence, flags, CRC prefix and all 152 raw bytes. Invalid initial states,
non-injected baselines and partial/error snapshots are refused. Contiguous IDs,
full-key matching, exact duplicate caching, payload conflict, retained receipts,
explicit rotation, same-file ownership loss and no automatic reclaim are
checked against actual kernel mutations. Duplicate tests require both submit
and snapshot call counts to stay unchanged.

The fault-injecting backend distinguishes two uncertain cases:

1. The extracted kernel commits, but the callback reports UNKNOWN: a later exact
   state can establish the positive commit under the stated sole-mutator and
   same-backend lifetime assumptions.
2. The callback retains the command without invoking the extracted kernel:
   an older STATE remains UNKNOWN. Only after the fixture explicitly submits
   that retained command can an exact state reconcile it.

The second case models delayed backend delivery; it is not an actual pending
kernel syscall or a cancellation proof. Both FRAME and ROTATE are exercised.
Malformed callback results after a real extracted-kernel commit remain UNKNOWN.
Changing any expected state field prevents positive reconciliation. Errors do
not erase unresolved state. Positive reconciliation never reopens old admission.

A held backend callback and concurrent quiesce share the broker mutex. Twenty-four
concurrent exact duplicates produce one actual extracted-kernel write. A
callback attempting broker reentry receives the error-check mutex refusal.
Counter-boundary tests seed owned fixture state explicitly; no production
counter setter or overflow recovery path is added.

## Discriminating controls

| Deliberate broker defect | Required exact failing oracle |
| --- | --- |
| Omit epoch comparison | `stale_epoch_refused_before_backend` |
| Ignore duplicate payload | `duplicate_compares_all_frame_bytes` |
| Treat any valid STATE as positive reconciliation | `lower_state_is_not_cancellation` |
| Quiesce leaves admission open | `quiesce_blocks_new_request` |
| Reissue a grant in the closed epoch | `closed_epoch_cannot_reissue_grant` |
| Cached acknowledgment reopens admission | `cached_ack_does_not_reopen` |

Each control modifies only broker source, preserving the frozen extracted
kernel. The runner requires the exact assertion count, failure name, exit code
and absence of sanitizer diagnostics; compilation failure is not accepted as
a successful control.

## Preserved development runs and limits

An initial count-calibration run produced 203 passes and zero failures; the
runner deliberately rejected it against the provisional count zero. Its full
snapshot remains private. The first full matrix passed, then review requested
that duplicate checks also assert no STATE callback. That test-only refinement
preserved the assertion count and the complete final matrix above supersedes
the earlier result. Broker/header bytes did not change between those runs.

This does not test real VFS references, kernel initialization, entropy, process
crashes, durable receipts, broker IPC, actual preview queues, saved settings,
native execution, hardware release or stopped recovery. It does not integrate
the C broker with the Rust gate. Source review, isolated concurrency tests and
sanitizers do not prove whole-system lifetime or race freedom. There is no
module/guest install or runtime device action in this evidence.
