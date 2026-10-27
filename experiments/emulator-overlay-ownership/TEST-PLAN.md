# Phase-1 final-mutation guard validation plan

This plan is offline and source-oriented. It does not authorize a module installation. Phase 1 has no producer handback API: after acquisition the mixer fence remains for the module lifetime, including explicit release and last owner-file close.

## Fixture boundary

Compile exact candidate functions and their state declarations, extracted from the separately applied pinned kernel patch. Preserve the full pinned source, candidate, patch, header, each extracted function, host shim, test source and immutable run results. A pthread mutex implements `miso_lock`; usercopy stubs copy owned buffers and can stop or fail at a selected call. Kernel capabilities, file modes, delay and parser services are explicitly controlled stand-ins. Snapshot all four overlays, presence flags, CRC values/count and owner/run/epoch/sequence state under the mutex before and after each refusal.

The fixture must execute the actual candidate control dispatch, legacy `ctrl_write`, `ctrl_release`, tagged mutation/claim/release/status, CRC setter/getter and `spi_read`. It must not replace these with a policy model. Functions outside the extracted scope, Linux VFS reference counting, syscall/ioctl encoding on other architectures, real usercopy, kernel scheduling and IRQ latency remain unproved. A same-pointer alias models duplicated file descriptors; it does not exercise `dup`, `fork` or last-reference release. Explicit RELEASE is the reachable concurrent transition; do not describe a direct fixture call to final `.release` during an in-flight operation as a VFS lifetime proof.

## Interface and serial matrix

The selected command header is 32 bytes: version 1, reserved 0, run ID, expected epoch and sequence. CLAIM is input-only with sequence 0; FRAME adds 152 bytes; RELEASE is input-only; STATE separately returns 192 bytes. Successful CLAIM has no response copy that could be rolled back. Failed input copy leaves state unchanged; an uncertain syscall return can only be diagnosed through STATE on the same retained file. Mutating ioctls require an enabled immutable module option, writable file and administrator capability. STATE is nonmutating and copies a coherent value snapshot after releasing the spinlock.

1. Before acquisition, preserve legacy whole-frame, one-bus clear and zero-byte clear behavior on all four buses, including existing short/oversized/error behavior. Preserve independent non-mixer operation after the fence.
2. Refuse disabled ownership, read-only files, absent capability, malformed header/reserved fields, zero/invalid identity, stale expected epoch and invalid sequence. Bind exact expected errors once source is supplied. No refused operation may alter any overlay, presence flag, CRC or ownership state.
3. Acquire only with mixer CRC 96. Do not silently normalize a different CRC. Successful claim establishes the requesting open-file description, run and new epoch, starting sequence 0, retaining the full prior mixer overlay. Another open cannot write with copied run/epoch/sequence. A same-file alias can.
4. Accept only the exact next tagged sequence. Check duplicates, gaps, wrong run/epoch/file, maximum values and overflow without wrap. A successful frame is one complete 152-byte replacement. Failed input copy, including partial copies, publishes no bytes and consumes no sequence.
5. Explicit release consumes its required next sequence, clears active ownership and retains the complete overlay and permanent fence. Final close only releases that file's ownership and also retains overlay/fence. Closing an unrelated file does nothing. A later claim rotates epoch; copied old tokens never regain authority, including a reused synthetic file address.
6. After the fence, reject all untagged bus 2 writes, its single-bus clear and zero-byte clear-all atomically. Clear-all refusal must preserve buses 0/1/3 too. Other-bus writes/clears stay independent. No public operation resets the fence.
7. CRC parse failures preserve both array and count. After fence, refuse changes away from mixer96 while permitting explicitly supported unrelated-bus changes. STATE and CRC getter snapshots must not expose a mixed update.
8. `spi_read` returns an entire owned snapshot and restamps CRC from the matching locked length. Preserve all bytes outside CRC locations, packed low fields and independent FX TIME values. Read usercopy failure does not consume or mutate an overlay. STATE returns copied values without modifying input/ownership and a copyout fault has no mutation effect.

## Forced schedules

Use deterministic barriers in stubs rather than repeated probabilistic loops. Never hold the source mutex while waiting for a peer that needs it.

- Pause legacy whole-frame copy before its final lock; acquire; resume the old write. It must refuse without replacing the retained mixer frame. Repeat for a single-bus clear and clear-all at their final lock boundary.
- Pause CRC parsing before commit; acquire; resume a CRC change away from 96. It must refuse with the complete original array/count retained. Reverse order: a completed incompatible CRC update makes acquire refuse.
- Start two claims with the same expected epoch; one owner wins, the other refuses. Start two tagged writes with the same next sequence; exactly one complete frame wins, the other cannot consume another sequence.
- Pause an old tagged request after input copy; release the owner and acquire a new epoch on a different file; resume. The stale request cannot mutate the new epoch's frame or sequence.
- Pause a reader after copying its locked frame/CRC into private storage; publish another frame; resume copyout. It must return the entire earlier snapshot with its matching CRC. A concurrent reader sees either complete frame, never a mixed payload.
- Pause STATE copyout after its locked snapshot; mutate/release/acquire; the returned snapshot remains internally consistent and does not imply current state afterward.
- Exercise repeated read/write publication with distinct deterministic payload patterns under TSan, consuming while producer threads are live. Joining producers before reading is not a concurrency test.

## Strict source mutations

Each mutant must compile and fail the named functional expectation, with sanitizer diagnostics treated as unexpected failures. Proposed discriminators: remove the final legacy mixer guard; allow clear-all after the fence; clear fence on owner release/close; remove file-identity check; admit stale epoch; remove exact-next-sequence check; mutate live CRC array before validation; read CRC length outside the frame lock; publish payload outside the lock. Select a small set that the actual candidate source and deterministic schedules can distinguish. A permissive mirror implementation or failure caused only by a compiler diagnostic is not an accepted negative control.

Run plain, ASan/UBSan and TSan against the new fixture. Preserve first failures and source snapshots; correct fixture mistakes explicitly rather than replacing failed receipts. Passing host checks establish only these extracted functions under the documented shims. They do not prove a built/loaded module, kernel compatibility, native input receipt, safe recovery, queued-generation handback or physical timing.
