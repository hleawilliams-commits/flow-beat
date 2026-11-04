# Offline USB permanent-slot binding composition

This separately versioned candidate addresses the slot/type and capture-state
issues in [the SDK binding review](../activation-sdk-binding-review/). It has no
loader descriptor, installable package, live observer wiring or activation API.
The frozen [entry observer](../activation-entry-observer/) remains unchanged,
including its tested source bytes and recorded EOF whitespace findings.

## Binding contract

`binding.h` derives the claim function type from the exact pinned SDK header.
Both original targets are permanent, aligned plain `void *` objects. Their exact
addresses are passed as `void **`; no atomic-object cast, temporary output slot,
post-claim copy, reset, retry, unpatch or reclamation path exists. The selected
GNU/ARM64 atomic publication contract is explicit. SDK output must be nonnull
and stable before the replacement entry becomes reachable, including before
claim return. A post-return null check refuses readiness but cannot protect an
entry already reached through a malformed SDK implementation.

One atomic enum is the sole capture-readiness authority. Installation consumes
Dormant once, enters Installing, and can publish Ready only by a final CAS from
Installing after both claims succeed and both originals are present. Capture
checks that same enum directly. Failure transitions Installing to Failed by CAS;
close transitions a nonterminal state to Closed. Failed and Closed cannot reopen
or overwrite each other. The stage refusal result remains available to the
caller even when close wins the state race. A Ready result describes that
successful transition, not an ongoing guarantee against a later close.

The first successful claim remains forwardable if the second is refused. A close
after a pre-call state check can overlap the already admitted SDK call. A helper
admitted while Ready can publish after close; close neither drains it nor waits
for sink ownership. Code, original slots, sink and configuration therefore remain
for process lifetime after a successful claim, including partial installation.
No ownership or quiescence is inferred from a diagnostic snapshot.

The component assumes an externally validated, nonthrowing SDK function in the
permitted module/init context, after other fallible preparation. It does not
supply firmware/module preflight, current hook-ownership exclusion, instruction
cache synchronization, island lifetime or an installed-hook unwind contract.

## New fixture scope

The new fixture calls the actual copied assembly entries, actual helper and
binder with a mock SDK. It uses independent native entry-address literals and
checks the exact output-slot identities, replacement addresses, stable module ID
and alignment. Successful mock claims publish the original and invoke the
replacement before returning. Refusals occur before publishing or invoking the
refused slot, while prior successful slots remain callable. Missing-original
cases deliberately model malformed success and never invoke a null target.

Each selector starts a fresh process. There are no sink/slot resets. Selectors
cover success, first/second refusal, null claim, each missing original, reentrant
and later repeated initialization, close before initialization, close during
either claim, close before final readiness, close during either refusal, and an
already admitted helper that publishes after close. The last case uses one real
worker, a condition-variable barrier at the helper admission point, and a join
before semantic assertions. It establishes that selected ordering, not general
concurrent recording or native callback quiescence.

Two compile-time fixture hooks permit scheduling immediately before the final
Ready CAS and immediately after helper admission. They are absent from separately
compiled production helper/binder objects. The new assembly object is linked into
the executed fixture. No helper renaming, external state/backtrace interceptor,
old fixture execution or old register/return test matrix is used here.

The runner snapshots all inputs and receipts into a new ignored private directory,
uses the pinned local ARM64 build image with networking disabled, and accepts
only the reviewed check counts and exact named semantic failure sets. A failed
command, diagnostic, timeout or sanitizer runtime failure stays a failure with
retained output. Unavailable sanitizer libraries are reported as unavailable,
never silently treated as passing. Mutants that corrupt or substitute an original
slot are checked before entry invocation, so a null branch is not their oracle.

## First retained result

[result.json](result.json) records **39 checks across 14 selectors in each of
plain and ASan/UBSan modes**, plus five exact semantic controls in each mode.
The first complete matrix passed. The selected target sanitizer libraries were
available and a separate instrumented allocation/free smoke passed before the
fixture. All compiler and sanitizer diagnostics were empty. No assertion was
relaxed after execution, and no old suite was replayed.

| Control | Selected failure | Exact failing checks per mode |
|---|---|---:|
| Temporary first output slot | Wrong permanent-slot identity prevents safe entry | 4 |
| Unconditional final Ready store | Close before readiness is overwritten | 3 |
| Failed stage promotes Ready | Partial refusal opens capture | 3 |
| Reset first slot on second refusal | Earlier successful forwarding is lost | 2 |
| Unconditional failure store | Failure overwrites an earlier close | 2 |

Every control must exit with the exact complete named failure set; crashes,
unexpected output or sanitizer failures are rejected. The slot guards make the
first/reset controls semantic refusals, not deliberate null-target branches.

The new production helper/binder objects compile separately without either
fixture scheduling hook. Both permanent pointer symbols are eight-byte BSS
objects with at least eight-byte alignment. The newly compiled entry object is
byte-identical to the retained frozen object, including `.text`, `.eh_frame` and
relocations. That new object is used in the fixture; the retained old object is
only compared. This is not a repeat of the old ABI, CFI or return-channel suite.

The retained run contains 17 inputs, 106 commands, 56 artifacts and 550 files.
Its private result digest is
`f46484290ed161cd1642caebb0cac510b372ea2bc36c989ed617a85a7fe5cc00`.
An independent retained-byte audit passes across all 17 inputs, 106 commands,
212 output streams, 56 artifacts and the entire 550-file inventory, including
reconstructed mutations and the exact sanitized projection; it executes no
compiler or fixture. See [verification.json](verification.json).

Raw commands, outputs and binaries remain ignored. The new assembly is not
sanitizer-instrumented, production helper/binder configuration is compile-only,
and no TSan, generalized contention, asynchronous unwind, native instruction
publication or whole-engine lifetime proof is supplied.

## Remaining gates

This binding does not alter the 800-byte frame or the register-only record shape.
Action x2 is opaque bits; service Source comes only from registers. No native
pointee/post-return reads, return interception, pending TLS state or cross-entry
event pairing are introduced. The x16 tail-transfer exception and installer x17
and anonymous island/trampoline unwind limits remain. Source/object parity with
a previously tested entry is not installed-hook unwind proof.

A later separately scoped passive adapter still needs a permitted native Local
Library invocation tied to its actual live row/provider, arguments, owner scope
and execution context. Alternate destruction/replacement, queued work, callback
quiescence, final Local Library contract/readiness, current hook ownership,
writer exclusion and stopped recovery remain unresolved. Main/live dispatch,
USB activation, direct menu/focus/view cancellation, stems staging and product
release gates remain CLOSED. No emulator or physical deck is accessed by this
fixture. It changes no community package, catalog, official core or protection.
