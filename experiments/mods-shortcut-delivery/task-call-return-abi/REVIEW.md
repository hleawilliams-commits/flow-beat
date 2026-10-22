# Independent call/return review

The root reviewer independently executed the frozen five-source fixture in the
pinned local ARM64 image, with networking disabled. The candidate again passed
**319 assertions**, with 83 before/native invocations and 80 normal-return after
invocations. All five executed controls produced their exact expected failure
multisets (1, 72, 15, 5 and 1); both compiled CFI controls were rejected by the
metadata audit and were not executed. No firmware, AZ emulator session, physical
deck or network service was accessed.

The [comparison receipt](independent-result.json) records identical source,
wrapper-object, output and validation fields except the unique run ID and linked
test-ELF hash. The two test ELFs differ in exactly six bytes: the compiler's
temporary basename in one local `STT_FILE` symbol's `.strtab` string. That symbol
is `SHN_ABS` with zero value and size. All other sections and the rest of the
complete ELF bytes are identical; replacing only that basename makes the entire
files equal. The executables are not claimed to have identical hashes. Both
originals and immutable run inputs remain privately retained.

The first root attempt was denied access to the local Docker socket by the
filesystem sandbox, before a container ran. The scoped escalation succeeded;
the completed run above is the sole root execution of this final matrix. That
access refusal is not a test failure or an emulator result.

## Source and unwind findings

Root and a separate reviewer inspected the assembly, fixture, context and CFI.
No blocking defect was found within the documented Linux AArch64 two-register
input/full-`x0` return contract. The 416-byte frame is aligned to 16; context
bytes 352..415 do not overlap original LR at 152 or the errno pointer/value at
320/328. The saved-LR rule at CFA−264 and restoration order match the emitted
instructions. The wrapper does not alter callee-saved general registers, and
requires all helpers and targets to obey the platform ABI.

Before receives original argument and LR bits. Native inputs are restored
before the original call, except that `x16` is branch scratch and SP/LR refer to
the wrapper frame/continuation. After receives only the stack-local value context
and full return bits. Return-side assembly reads are confined to the private
stack and saved libc errno address, with no task/dispatcher reads. The actual
synthetic unmap-before-return check supports that limited behavior.

One exception is thrown by the deepest of three nested native invocations. It
unwinds all three wrappers without after callbacks and runs four expected C++
destructors. The report's `native_exception_calls: 3` counts those invocations,
not three separately thrown exceptions. Before must not hold a resource or
reentry guard that needs after to release it. Actual backtraces from before,
native and after reach the wrapper and outer caller. The CFI audit itself checks
selected metadata facts; it is not a general interpreter or proof of every
unwind-rule state.

The concurrent fixture tests 64 invocations on four threads with its own atomic
IDs and locked synthetic event array. It does not implement or test a production
publication queue, dropped records, unique run scopes, teardown, or native
ownership. No sanitizer instrumentation was used for this ARM64 assembly run.
The separate serial capture-helper sanitizer results do not extend to this
fixture or an integration that has not been built.

## Integration limits

No stack-argument forwarding, original native SP/LR identity, arbitrary TLS,
signals, stack switching, SVE/SME or foreign-exception contract is established.
Linux `__errno_location` is an added runtime dependency with unmeasured device
cost. The original target must be non-null, release-published and mapped, and
wrapper/helper/unwind data must outlive all invocations.

The actual SDK hook island/trampoline, its scratch-register behavior and unwind
registration have not been exercised. Neither this wrapper nor the separate
capture helper has been composed into an observer; their compatible context
layout is not integration evidence. The executor needs its own one-input ABI.
Native storage validity, admission/execution/cancellation semantics, bounded
capture/loss accounting and safe shutdown remain gates before installation.
