# ARM64 task submission call/return fixture

This new standalone fixture tests a **call/return** observer for the known
TimerTaskBox submission ABI: two integer-register inputs (`x0` box, `x1` task)
and a full-width `x0` TaskNo result. It does not reuse entry-only tail-wrapper
evidence as return-wrapper proof. The intended native entry `0x237a440` and its
static limits are documented in [the task-box review](../route-task-ownership/TASKBOX.md)
and [boundary audit](../route-task-ownership/BOUNDARIES.md).

No hook, module, package, native firmware call, AZ emulator session, physical
device access or installation is included. Synthetic stubs execute the actual
assembly inside the existing local Linux/AArch64 build image. This result does
not establish live task/dispatcher lifetime, native queue ownership, task
completion, hardware timing or deployment readiness.

## Helper contract

[context.h](context.h) defines a 64-byte value-only context compatible with the
separate capture-contract layout:

| Byte range | Contents |
|---|---|
| `0..23` | TaskNo, ID, delta bits and cumulative bits |
| `24..47` | Run ID, invocation ID and thread observation |
| `48..55` | Phase and status |
| `56..63` | Reserved zero |

The assembly zeros all 64 bytes before each invocation. It supplies the original
inputs and caller LR separately to the before helper:

```cpp
void az_task_call_before(TaskCallContext *ctx,
                        uintptr_t box, uintptr_t task,
                        uintptr_t original_lr) noexcept;
void az_task_call_after(const TaskCallContext *ctx,
                       uint64_t native_result) noexcept;
```

The before helper may fill scalar values; it must not put box/task/LR addresses
in the context. The after helper receives only that context and the captured
full result. It must not read a task, dispatcher or other native object through
an address saved elsewhere, and must not retain the context's stack address.
The assembly performs no task or dispatcher dereference. The fixture's one
pre-copy memory read accesses an owned synthetic page, independently of any
firmware mapping or native lifetime claim.

After runs **only on normal native return**. A native C++ exception unwinds
through the assembly frame and bypasses after. Before must therefore hold no
resource, TLS reentry guard or other obligation that needs after to release it.
Missing after is an incomplete observation, not a native failure, cancellation
or completed-handler verdict. Nested calls have independent stack contexts;
this wrapper supplies no global ordering or correlation policy.

## Preserved state and ABI limits

[entry.S](entry.S) uses a 416-byte, 16-byte-aligned private stack frame. It saves
full `x0..x18`, original LR, full `q0..q7`, NZCV, FPCR and FPSR before calling the
helper. It restores those inputs before native execution, using IP0/`x16` for
the original target. Callee-saved `x19..x29` are not changed by the assembly;
the helpers and target must obey AAPCS64, including preservation of required
low halves of `v8..v15`.

The original target receives the known register inputs with a new call frame.
**Native SP is 416 bytes lower, and native LR is the wrapper's continuation.**
Original caller LR is captured before and restored when returning to the caller.
This is not transparent preservation of native caller identity, a generic
stack-argument adapter, or a tail call. No native stack arguments are forwarded.
Tests of pair, indirect, HFA and vector returns stress register preservation;
they do not change the intended two-input/full-`x0` target contract.

On return, the wrapper saves native `x0..x18`, `q0..q7` and FP/control state
before calling after, then restores them for the caller. Full `x0` reaches both
after and the caller. The private original-LR slot is never overwritten by the
native-return register capture. The context remains untouched by the assembly
between before and after.

The wrapper calls Linux `__errno_location()` once per invocation after saving
input registers. Its TLS address and temporary errno value occupy private
frame slots, outside the context. Entry errno is restored after before;
native-return errno is copied before after and restored afterward. Both fixture
helpers deliberately clobber errno. This adds a runtime/TLS call whose actual
firmware linkage, first-use cost and device timing are **unvalidated**. It is not
proof about arbitrary TLS state, signal handling, stack switching, SVE/SME,
foreign exceptions or a different platform ABI.

`az_task_call_original` must be non-null and release-published before the entry
can run; the assembly uses an acquire load. The fixture publishes it with a
release store. Original target, helpers, wrapper and unwind data must remain
mapped until no invocation can use them. This fixture has no production
shutdown protocol or fallback for a null/unmapped original target.

## Actual ARM64 evidence

The candidate passes **319 assertions**, with **83 before/native calls and 80
after calls**. Coverage includes:

- Full high-bit box/task inputs, original LR, native continuation LR, input and
  output GP/SIMD/control state, caller SP restoration and native SP alignment.
- Full-width TaskNo capture and forwarding, including high bits; supplemental
  two-register, indirect, HFA and vector result checks.
- Four nested native calls, reentry from before and after, and 64 invocations
  across four threads, checking each invocation's copied context and result.
- One exception thrown at the deepest of three nested native invocations: it
  unwinds all three wrappers to the original C++ caller, all four expected
  destructors run, no after record is emitted, native errno remains visible,
  and a later ordinary call still succeeds. The metadata field
  `native_exception_calls=3` counts native invocations on that unwind path;
  it does not count throw operations.
- Live `_Unwind_Backtrace` inside before, native and after, each reaching the
  assembly wrapper and its caller.
- A target that unmaps the synthetic task page before returning. After still
  reports the pre-copied values, including zero TaskNo, without reading that page.

The runner requires exact counts, exit status and failure-expression multisets.
A crash or unrelated assertion is not accepted as a successful control:

| Executed assembly control | Exact expected failures |
|---|---:|
| Substitute box bits for original LR supplied to before | 1 |
| Truncate the value supplied to after to 32 bits | 72 |
| Truncate the native result restored to the caller | 15 |
| Omit errno restoration after the after helper | 5 |
| Give before a nonzero initial context | 1 |

Two additional assembly objects remove the saved-LR rule or use the wrong CFA
offset. The CFI metadata audit rejects both. Those intentionally invalid unwind
objects are **not executed**; their rejection is not an exception-runtime test.
The real candidate's exception and backtrace checks above execute independently.

The candidate object has **107 instructions**, three `BL` calls
(`__errno_location`, before, after), one native `BLR x16`, and one `RET`.
Its FDE spans `0x1ac` bytes. CFA is SP+416 throughout helper/native calls; original
LR is CFA−264, restored before the final SP adjustment. The object audit checks
that every return-side assembly read uses the private stack or saved errno
address. [CFI](entry-cfi.txt), [disassembly](entry-disassembly.txt) and
[relocations](entry-relocations.txt) describe this generated synthetic wrapper,
not firmware. DWARF unwinding is tested; no frame-pointer-only backtrace claim
is made.

[Source/object/ELF metadata and exact controls](test-result.json) and
[sanitized results](test-results.txt) are retained. Raw process logs, immutable
input snapshots, generated binaries and mutants remain in ignored `out-history/`.
No sanitizer instrumentation was used for this assembly execution; it is not
whole-program race or lifetime validation.

The [independent review](REVIEW.md) reproduced all assertions and controls with
the same source and wrapper-object hashes. Its linked test ELF differs only in
six characters of one compiler-temporary filename in `.strtab`; all other bytes
are identical. The [comparison receipt](independent-result.json) preserves both
hashes and the exact comparison scope.

The first retained build stopped on two test-only issues: GNU assembler did not
expand the SIMD register macro suffix, and warnings-as-errors rejected a compact
C++ loop's misleading indentation. Both failed inputs/output are preserved.
The test syntax was corrected; the wrapper assembly did not change. The first
executable candidate passed all 319 assertions. Later runs added strict control
acceptance, metadata checks and release publication in the synthetic fixture.

## Reproduction

```sh
python3 experiments/mods-shortcut-delivery/task-call-return-abi/run-tests.py
```

The runner requires already installed image `azmod-build:18.04`, exact ID
`sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90`,
architecture `arm64`. It uses `--pull=never --network none`, GCC 7.5.0, `-O2`,
warnings as errors, exceptions, asynchronous unwind tables and pthread support.
It neither downloads dependencies nor executes EP147. This standalone ABI
result is one prerequisite for a future bounded observer; native capture,
claim/teardown, cancellation and object-lifetime gates remain separate.
