# Native owner graph for USB activation

The new static review identifies real native owners of both SourceSelectView
and BrowseSourceSelect. It also distinguishes their task-allocation pools from
the shared execution queue. Ordinary view selection is not itself deletion of
the stored view owners. These results narrow the retention question; **native
activation and complete wrapper gates remain CLOSED**.

This successor starts from development commit
`76ff130dedb44518e71f08b1ff8d4527a0a943ac`. It reads the private exact image;
it does not execute firmware, contact the emulator/device, install a hook or
change a core/package/catalog. The preceding ABI, timer and runtime evidence
remain separate and unchanged.

| Relationship | New bounded evidence |
|---|---|
| View storage | [View ownership](VIEW-OWNER.md): ViewObjectManager `+0x90` and dispatcher registered array `+0x138` each have paired increments/releases of view `+0xd8`. |
| Selection | The native `goToView` body changes a distinct pointer at dispatcher `+0x150` and sends deselect/select notifications. SourceSelectView stops/starts timer IDs 0/3; this does not establish ID-1 scheduling. |
| Dispatcher synchronization | A concrete MessageManagerLock handshake exists. The selected null-thread constructor retries until acquisition succeeds, but the dispatcher releases the guard before registered-view teardown. Same-message-thread reentry is a separate concern. |
| Embedded timer | [Timer ownership](TIMER-OWNER.md): its shared control owns an in-place DefaultMemPool, not the timer, view or execution queue. |
| Source service | [Service ownership](SERVICE-OWNER.md): GuiObjectManager `+0x178` owns the allocated service. The queued task still carries a raw service capture and supplies no independently acquired service lifetime. |
| Shared execution | [Shared queue](SHARED-QUEUE.md): TimerTaskBox::Impl is cached separately and registers a DeletedAtShutdown base; its own destruction reaches pending-task cleanup. Originating-pool release is a different operation. |

The reviews are deliberately local. They do not assert a native use-after-free,
immortal managers, an exhaustive alias/writer inventory or transitive shutdown
safety. None authorizes count mutation, direct setSource invocation, queue drain,
custom cancellation or post-return native reads.

## Verification and reproduction

The first and only new-pin check passed: **42 complete extents, 328 selected
instructions, 71 pointer relations, 19 names and six imported symbols**. An
independent byte/structure audit matched the result and all 197 supplied fact
fields without rerunning the verifier.

[review-pins.json](review-pins.json) binds complete code/FDE extents, selected
instructions, pointer relations, names and imported symbols for this new
ownership graph. [result.json](result.json) contains the exact-image check;
[verification.json](verification.json) records the attempt, retained diagnostic
hashes and independent review. Full disassembly, scripts and diagnostics remain
in the ignored private directory. No new exception tables are decoded here;
unparsed LSDA pointers remain explicitly unparsed.

The unchanged [static verifier](../activation-abi/verify-static.py) is reused
with **these new pins**. Its generic result scope label still says ABI/timer
teardown; this directory's pinned owner graph is the bounded subject of this
run. It does not replay the preceding directory's pins or any old test suite.
To reproduce with an authorized local copy of the exact image, from this
experiment directory and with a fresh output path:

```sh
python3 ../activation-abi/verify-static.py \
  --elf "$AZ_PRIVATE_EP147" \
  --pins review-pins.json \
  --output /absolute/private/fresh-lifetime-result.json
```

Dependencies and exact tool versions are recorded in the result. Matching code
bytes and native owner operations establish static facts, not a live lifetime
lease, hook eligibility, wrapper ABI/unwind transparency, completion or
cancellation. Do not rerun the passing check unchanged merely to increase a
count.

## Next distinct proof

Connect the proposed invocation to a live manager/registered owner that spans
that exact operation. Account for reentrant notifications, already selected
callbacks, queued raw captures, exceptions and the shared implementation's
actual shutdown order. The service manager's earlier teardown participants
remain a useful bounded next review; alternatively establish a native-owned
action scope with a valid retention contract.

The prior action's final Local Library contract and native completion remain
open. Missing media, lifecycle, alternate databases and scroll/sort remain
unproven; no USB buttons or native adapter exist. All stems, observer, recovery,
audio-quality and physical-timing limits continue to apply. A UI milestone is
not a stems fix or a release-ready product.
