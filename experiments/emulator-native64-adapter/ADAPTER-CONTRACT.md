# Native64 adapter contract

This is a separate C adapter for the frozen RESTORE-capable broker and Linux
native64 ownership UAPI. It adds no device discovery, `open`, CLAIM, daemon,
transport, preview factory, service action or runtime enablement. The API is
`adapter.h`; `native.c` supplies the three direct libc calls declared in
`syscalls.h`. This document describes source behavior, not executed validation.

## Attachment and retained identity

`mba_attach(original_fd, expected, out, report)` returns zero or a positive
errno. The caller supplies an already-claimed controller descriptor and a
complete expected owned state. That descriptor must remain open and stable
through attachment. The adapter copies the expected value, clears supplied
outputs, duplicates the descriptor once with `F_DUPFD_CLOEXEC`, and obtains one
fresh STATE through the duplicate. A successful duplicate may be descriptor
zero. A purported duplicate equal to the still-open original is refused without
closing the original.

The fresh state must match every expected semantic field and all 152 raw bytes.
Both instance words are compared; the instance pair, run and epoch must be
nonzero. Flags must be exactly 23 (owned idle) or 31 (owned injected), and the CRC
prefix must be 96. Wire version must be 2 and reserved must be zero. A malformed
state is a protocol error; valid-shaped disagreement is stale state. This does
not establish that the caller's expected value is an authoritative recovery
baseline or that another controller cannot write afterward.

Success retains a private reference to the same kernel file object. Closing or
reusing the caller's descriptor number afterward cannot redirect that private
reference. It is not exclusive ownership of every reference: other original,
duplicated, inherited or same-capability writers remain outside the adapter's
control. The adapter pins the accepted instance and run for its lifetime; it does
not silently attach to a replacement module or run.

If attachment fails after duplication, it attempts one close of the duplicate.
The primary attachment error is preserved in `report.error`; the independent
cleanup attempt and raw result are retained separately. A failed or uncertain
cleanup is not successful release. No retry or alternate descriptor is used.
The returned adapter handle is null on failure.

## Backend and wire translation

`mba_get_backend` returns the immutable callback/context pair for one broker.
The adapter must outlive all such copied pairs, brokers and callbacks. Calls
must be serialized externally by that broker. The adapter has no internal lock,
deadline, cancellation, reentrancy accommodation or cross-process exclusion.

Semantic broker structs are not cast to ioctl structs. Each request uses a
separate zero-initialized native64 wire object with explicit version, reserved,
instance, run, epoch, sequence and frame assignments:

- FRAME and ROTATE use the 200-byte frame structure and require semantic mode 1.
- RESTORE uses the 208-byte structure, with explicit mode 0 or 1 and reserved 0.
- STATE uses a separate zero-initialized 208-byte output structure.

The reviewed sizes, offsets, ioctl encodings and flag relationships are compile
assertions. All 152 frame bytes, including raw CRC positions and tail, are copied
unchanged. The adapter does not calculate a CRC, synthesize idle bytes, normalize
controls or infer target mode from equal bytes. The kernel separately validates
the exact idle template for mode 0.

Invalid operations, modes, null arguments and zero epoch/sequence are locally
rejected with EINVAL before any ioctl. Instance/run disagreement is locally
rejected with ESTALE. Local validation does not assert that a nonzero epoch or
sequence is current; the kernel remains the final check.

## One-call result classification

Each valid submission calls libc `ioctl` once. The native wrapper captures errno
immediately only when libc returns -1. There is no EINTR retry, fallback, implicit
STATE query, reopen, retagging or automatic disposal.

A result of zero is COMMITTED, regardless of stale errno. A result of -1 is
REJECTED only for the following errors, under the exact pinned direct-driver
contract: EBADF, EPERM, EOPNOTSUPP, EFAULT, EINVAL, ESTALE, EACCES, EOVERFLOW and
ENOTTY. The reviewed input-only handler returns these errors before mutation.
This list is not a general transport classifier and must not be applied to SSH,
sockets, another driver, a retrying wrapper or an unverified remote response.

All other returns are UNKNOWN. This includes EINTR, EIO, timeout, EBUSY,
unrecognized errors, missing/nonpositive errno and unexpected positive or other
negative return values. A usable positive errno from a -1 result is preserved;
otherwise EPROTO is reported. UNKNOWN can describe an operation that committed.
Only the broker's retained exact request and positive reconciliation rules can
change that accounting; a lower STATE is not cancellation.

STATE is read once and published only after a zero return and complete shape and
identity validation. On every failure, the caller's semantic output is zeroed;
partially copied wire fields are not published. Success relies on the direct
syscall's complete-output contract and does not authenticate an arbitrary
replacement syscall implementation. Epoch, sequence, mode and raw frame may
change within the retained instance/run; broker comparison decides whether a
snapshot matches its expected state. A successful read is a snapshot, not a
freeze on later writes or proof of native consumption.

## Explicit terminal disposal

`mba_dispose(adapter_slot, report)` requires caller-established quiescence: no
broker or callback may still reference the context. The caller must separately
resolve operation uncertainty under an authoritative lifecycle policy and
retain that resolution and the required accounting. Retaining an UNKNOWN
receipt alone does not permit disposal. A valid call consumes the adapter
handle before one close attempt, then frees its local
storage. Its zero return means inspect the report, not that close or recovery
succeeded. A repeated call with a null handle records no attempt. Null required
arguments are refused without consuming a live handle.

The raw close result is retained even when uncertain; close is never retried.
The adapter no longer provides a usable handle after that attempt. Neither an
attachment cleanup report nor a disposal report proves whether an uncertain
close released its kernel reference. Closing the final reference may orphan a
sticky selected overlay while preserving the permanent legacy fence. It does
not restore an overlay, cancel native events, restore files, authorize restart
or complete recovery. Callback errors and UNKNOWN results never trigger this
operation automatically.

## Remaining composition and custody gates

The actual Rust preview still has no production owned backend. Its injected
`MixerBackend` accepts typed receipts that are not self-authenticating. A future
bridge must bind the same adapter/broker session, validate complete request and
result identity, and preserve uncertainty without turning a transport write or
historical cache answer into a current ACK. The changed C semantic structures
also require separately compiled consumers; they are not an FFI wire format.

The owned `SettingsSink` remains injected. Its retained checkpoint records the
exact validated ACK and whether a callback was armed, skipped or returned; it
does not record which of `knob.ctr` and `mixer.state` was replaced, synchronized or
restored. The legacy writes and older private counter helper are not a two-file
transaction. A real sink needs exact original presence/bytes/modes, ordered
ACK-bound pair intents, per-file partial/uncertain replacement receipts and
explicit exclusion of preview, driver and boot-script writers. Two separate
renames are not an atomic pair for uncoordinated readers. A coordinator-only lock
cannot supply that exclusion, and kernel input fencing does not fence host-file
writes.

Joined-session and lifecycle objects preserve bounded local accounting and
backend/supervisor references while held. They do not supply native-stop
identity, an authoritative original overlay, durable recovery custody, a
host-file restoration permit or a guarded restart decision. Final Drop,
process death, platform termination and remaining external references still
need a separate policy. Acquisition of the original claimed descriptor and
baseline, real native64 execution, Rust integration, native event dwell and
stopped restoration remain unimplemented gates. No API here opens them.
