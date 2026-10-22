# Exact firmware and ABI binding

The unpublished `encoder-task-observer` adapter passes **96 static binding checks and 52 strict rejection controls** against the private, exact EP147 ELF. The verifier checks 44 selected instruction anchors, five data words, two RTTI identities, and five complete function bodies with exact FDE extents and hashes. Root independently reproduced byte-identical result JSON (`5517b9d2a96d50ae6efac3e6d03ad08ac0f3a133bf9a2a828d40526b902fbe84`). No native call, hook claim, emulator operation, or physical-device operation is part of this verification.

| Boundary | Function entry / FDE size | Static virtual slot | Selected original caller LR | Wrapper contract |
| --- | --- | --- | --- | --- |
| TimerTaskBox submission | `0x237a440` / `0x38` | `0x321b138` | `0x1b01e04` | Native `x0` box and `x1` task; copy before forwarding; observe full `x0` only after normal return. |
| Captured encoder task execution | `0x1b029c0` / `0x104` | `0x2d444e8` | `0x237d800` | Native `x0` task; observe entry and restore original SP/LR before tail forwarding. No native-result interpretation. |

The selected task vptr is `0x2d444d8`. The submission caller uses `BLR x23` at `0x1b01e00`, after selecting TimerTaskBox virtual slot `+0x30`; the drain caller uses `BLR x1` at `0x237d7fc`, after selecting task virtual slot `+0x10`. These exact static call sites bind the caller filters. Matching a caller and vptr rejects unrelated calls but does not establish native memory validity, retained object lifetime, current installed-hook ownership, or completion.

The four opening words are `a9bd7bfd 910003fd f9000bf3 aa0003f3` for submission and `f9400c03 90006fc1 9102d021 91067062` for execution. The verifier also binds constructor vptr selection, task construction fields, the submission forwarding path, and both caller instruction sequences. The complete firmware SHA-256 is `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.

## Reused evidence and new adapter checks

Both assembly wrappers and the three dependency headers are byte-identical to their existing sources at commit `5b0f5f3ee1b92682c3a3db51fc97f58050187ff2`. The verifier checks those bytes against the copied files, current parent files, the frozen-input declaration, and local Git objects. It binds the retained corrected ARM64 integration result and independent result by exact hash. It does not rerun that unchanged suite.

The selected prologues retain the earlier exact-policy acceptance recorded in [the task ownership audit](../route-task-ownership/BOUNDARIES.md). The verifier checks the immutable audit result, matching firmware, pinned upstream revision, prologue hashes, and first-one/first-four policy outcomes. It does not reexecute the old policy suite or claim that an island can be allocated or that a live SDK claim will succeed. The [task-box audit](../route-task-ownership/TASKBOX.md) supplies the bounded static scheduling and cleanup context.

New source checks bind the actual `observer.cpp`, `mod.c`, manifest, Makefile, and reused dependencies. They check both runtime slot/prologue comparisons, the exact opt-in and firmware gate, permanent aligned original-function slots passed directly to the two SDK claims, static transport publication before the claims, and its start after both claims. A release CAS prevents readiness from overwriting a failed installer; entry capture checks readiness with acquire before the caller filter and native reads. These are targeted source-shape checks, not a general parser or whole-program verification.

The submission before helper receives the private context plus the two native arguments and original LR. The executor helper receives the one native argument and original LR. The LR filters forward the actual supplied LR unchanged. The submission after helper receives only the copied 64-byte value context and full-width return bits, with no task pointer or post-return native field read. It can publish a reserved return after entry capture has closed. Exact helper signatures, phase selection, field offsets, errno guards, wrapper input setup, native forwarding, and selected CFI rules are bound. Complete assembly source identity carries the earlier synthetic ABI coverage; selected CFI string checks alone are not a general unwind proof.

The frozen adapter SHA-256 is `538574af5f357131e0fa9ec6e26d54cfef0ec2a57f31baddff1e8e5bc7c134ed`; `mod.c` is `051cf4243b0dde46552d55784feed337727094df065ee4c3efd551edbe1f46fc`. The result records every bound input hash and requires those inputs to remain unchanged during verification.

## Rejection controls and reproduction

All 52 mutations fail for their named reason: incorrect entries, slots, caller LRs, task vptr, FDE extents, each of the eight opening words, actual slot targets and caller BLRs; wrong wrapper argument/LR/frame/tail-transfer or CFI setup; altered copied capture constants; and adapter changes including temporary original slots, omitted prologue comparison, inverted caller filtering, narrowed helper arguments, wrong phases, readiness overwrite, suppressed late return, and truncated native return bits. Controls mutate in-memory inputs only.

Two preparatory verifier errors were corrected before the retained final result: an unmatched parenthesis prevented parsing, and the inverted-caller-filter control initially raised an unrelated string-lookup error. The latter control now reaches its dedicated comparator assertion and requires that exact rejection. These were verifier development failures, not adapter runtime observations.

From the repository root, using the existing private Python environment with Capstone and pyelftools:

```sh
../../../work/qemu-python/bin/python experiments/mods-shortcut-delivery/task-sdk-observer/verify-boundary.py --elf ../../../work/picks-native-probe/EP147 --result /tmp/task-sdk-binding-review.json
```

Compare the generated JSON with `binding-result.json`. The script reads the exact private firmware and local source/Git objects; it neither executes nor distributes the firmware. No earlier successful suite is invoked.

## Remaining limits

Static compatibility reads and caller/type filters do not prove that a native task is alive, sufficiently mapped, or stable during copying. The copied context carries scalar observations, not an owned native task or dispatcher. The submission wrapper changes the native callee's SP/LR while preserving the tested two-register contract; it is not a generic stack-argument wrapper. Executor helpers must remain `noexcept` and preserve errno. The prior synthetic exception checks cover synchronous native-body unwinding through the wrappers, not asynchronous unwinding while stopped inside an installed SDK island or original trampoline.

This result does not validate live module ownership, hook installation, original-pointer publication in a deployed core, teardown, hardware timing, compiled native read extents, or audio behavior. Submission return bits, executor entry, missing records, and observer invocation IDs do not by themselves establish admission, completion, cancellation, or native task identity. No runtime validation or deployment readiness is claimed by this binding note.
