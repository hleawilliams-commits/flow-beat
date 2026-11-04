# Offline USB entry snapshot adapter

This separate candidate implements the [entry-only plan](../activation-label-contract/NEXT-OBSERVER.md).
Two AArch64 assembly entries save the fixed-width state they receive, call a
bounded capture helper, restore the original caller stack and LR, and tail-branch
to a preinitialized original slot. There is no native activation call, return
interceptor or cross-entry correlation scope.

The 800-byte private frame contains x0–x30, full q0–q31, NZCV, FPCR and FPSR;
reserved bytes are zeroed. The final transfer uses x16 as scratch. Existing
installer x17 use, anonymous island/trampoline unwind, SVE/SME, PAC/BTI, signals,
asynchronous cancellation, foreign exceptions and hardware latency remain
outside this source contract. This candidate has no installer or runtime binding.

## Value capture and storage

[observer.cpp](observer.cpp) copies action x0/x1/x2 as raw bits and reconstructs
the service's twelve Source bytes from x1 and low w2. It never dereferences the
native-looking argument values. The upper x2 bits remain part of forwarded state.
The helper borrows only the assembly's private frame and retains no frame pointer.

A separate ready flag controls capture. A single try-only atomic flag protects
an append-only sixteen-record sink; disabled, full or contended capture still
returns to the assembly forwarding path. Snapshot reads use the same flag and
copy only fixed owned values. Closing ready does not drain admitted helpers or
permit reclamation. Successful-record ordinals are neither attempt IDs nor proof
that action and service entries belong to one native operation.

Original slots must be aligned, nonnull, initialized before entry reachability,
and stable while code or callers remain reachable. The zero-initialized offline
objects are not an installable configuration. Target compiler/platform checks
bind their atomic size and alignment; C++ source alone is not a universal atomic
representation guarantee. No ready-state observation supplies a target lifetime.

## Synthetic fixture scope

The new [fixture](test_entry.cpp) and [assembly caller/interceptor](fixture.S)
exercise this candidate independently of earlier task-executor suites. The actual
helper body is symbol-renamed for the fixture, and an external interceptor adds
state clobbering and a live stack walk while the entry frame exists. A fixture
macro exposes only a try-hold/release sink seam. Those additional fixture APIs
are absent from the separately compiled production object.

A stack walk through the entry/interceptor frame and a target-thrown exception
after tail transfer establish different things. Neither is an installed-hook
unwind test. The fixture owns its inaccessible action-pointer storage and its
synthetic targets; it supplies no invalid pointer to firmware. Helper/interceptor
instrumentation has no production timing or allocation claim.

The first matrix passes **31 checks across eight fresh processes/selectors**:
full register/state forwarding, value records, return channels, target exception,
live entry/interceptor stack walk, disabled capture, capacity and contention.
Five compiled semantic controls each finish cleanup and fail only their named
value oracle. They corrupt observed LR/SP, omit x1 or q31 restoration, or
explicitly clobber errno. The last control is not a mere restore omission.
Two malformed-CFI controls compile and are rejected at exact metadata PCs;
neither malformed variant is executed.

Each entry compiles to 87 instructions with one helper call and one terminal BR,
no RET and no return callback. The metadata check compares 88 CFI states per
entry against actual save/restore PCs, including CFA 800 and LR at CFA−560.
The production configuration compiles separately without fixture functions and
has two eight-byte original slots in an eight-byte-aligned BSS. This is a target
compiler result, not universal atomic-layout proof.

Both entries receive the complete synthetic register/state checks. Typed scalar,
pair, HFA, vector and x8 aggregate returns, actual stack arguments and the
nested throwing target exercise the action entry. The exception unwinds three
target guards and a C++ caller guard; two later normal calls still work. This
covers synthetic target/caller cleanup after tail transfer, not firmware unwind.

Contention uses two real threads while a fixture-owned flag hold forces refusal;
it does not establish general concurrent recording or race-sanitizer coverage.
No sanitizer was used. No earlier suite or native experiment was replayed.
An independent retained-byte audit verifies all six source inputs, 60 commands,
44 artifacts and 330 retained files, without importing the runner or replaying a
test. It independently checks the complete entry CFI text at all 176 PCs.
[Sanitized results](result.json) and [verification](verification.json) preserve
source/object fingerprints and private command/log receipts. All raw logs,
intermediate objects and executables remain ignored in private storage.

A pre-build review found and repaired an unencodable STP offset in the synthetic
SAVE_ALL macro, using separate STR instructions. It never reached a compiler or
execution. Independent review also clarified FP/control stress, added explicit
atomic slot-layout assertions, and replaced a mirrored service-byte oracle with
an independent twelve-byte literal. The first completed build/test matrix then
passed without a failed runtime or weakened assertion.

Next review exact SDK eligibility and original-slot publication/lifetime for a
separately scoped passive binding. This candidate supplies no permission to
install an observer, reopen device capture, or add a USB button. Actual writer,
recovery, owner and installed-path gates must be resolved independently.

## Gates retained

Current live receiver/row/provider identity, action-kind and carrier provenance,
accepted intent, operation-scoped retention, queued and reentrant ownership,
callback quiescence, readiness and cancellation remain unproved. Static label
resource associations and historical V2–V4 port observations retain their bounded
scopes. Entry records do not establish event ownership, acceptance or completion.

No emulator or physical deck is contacted by this fixture. Main/live dispatch,
writer exclusion and stopped recovery remain unresolved. All runtime, stems,
activation, physical-timing and release gates remain **CLOSED**.
