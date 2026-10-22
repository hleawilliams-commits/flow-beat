# Observed encoder caller — static review

The recorded LR **0x22c6e08** follows the indirect call at **0x22c6e04**.
Its enclosing FDE is **0x22c6c48**, extent **0x260**. This function calls
the source object's virtual slot **+0x10**, passing the source in x0, an
observer in x1, and a pointer to a stack copy of a 12-byte event in x2.
These are static register roles; no live pointer was read in this review.

The existing `HuiEncoder` RTTI/vtable anchors place **0x2360f80** at that
source slot. Its rotation path loads the observer's **+0x18** entry, copies
the event fields into w3/w4, passes a full-width ID in x2 and an encoder
interface address in x1, restores x30, and uses **BR x5** at **0x2361088**.
That tail call preserves 0x22c6e08. The observed LR therefore fits this
available route; it is not the address of the immediate scalar-dispatch
instruction. A live source vtable/class was not captured, so the static
chain must not be presented as an independently observed object identity.

Two direct B/BL references reach 0x22c6c48: 0x22c1a64 and 0x22c1ad8, in
separate FDEs. Their source pointers and input formatting differ. Neither
the saved LR nor this static inventory selects one as the runtime source.
The copied event storage exists during the native call, but this establishes
no external-reader lifetime contract or post-return ownership.

The earlier caller entry 0x22c6c48 fails the unchanged installer policy because
CBZ occurs at +0xc. The notifier 0x2360f80 begins with ADRP, which the actual
policy explicitly treats as relocatable and allows. Its overlap, claim
ownership and remaining installer conditions have not been audited here.
The initial host fixture wrongly expected ADRP rejection; that failed
expectation is preserved privately and corrected in this result. The verifier
extracts the actual pinned predicates, checks caller rejection before island
allocation, and distinguishes ADRP from forbidden ADR. Instruction substitutions
are host-only controls and are never applied to firmware. Do not install at an
interior instruction or relax policy.

The bounded next step is a separate, tested change to the existing accepted
**0x1b01cf8** observer's off-thread capture lane. The current one-shot lane
intentionally saves only one off-thread scalar record and counts later skips.
Additional bounded records could establish delta/cumulative ordering, caller
and thread consistency for both directions. They should copy only x0/x1/full
x2/w3/w4/LR/TID, keep addresses opaque, report contention or capacity drops,
preserve every native argument/return register, and avoid imposing a global
order across threads. No new hook or event-pointer read is needed for that step.

This review establishes no native return type, accepted/completed handling,
successful enqueue, task lifetime, cancellation, active child identity, or
executor thread. The reported runtime observation and its complete evidence
belong to the parent runtime report; this verifier contains no runtime object
addresses or opaque ID value and does not repeat any runtime action.

## Reproduce

From repository root with the existing private interpreter and firmware:

~~~sh
../../../work/qemu-python/bin/python \
  experiments/mods-shortcut-delivery/route-observer/caller-review/verify-caller.py \
  --elf ../../../work/picks-native-probe/EP147 \
  --upstream ../../../work/xdj-az-mods
~~~

The result binds exact firmware/source/function hashes, FDEs, instruction and
RTTI/table anchors, semantic rejection controls, and actual policy assertions.
It does not execute firmware, import runtime transport, or contact any device.
See the earlier [input route](../../input-route/) for the task-executor route;
that evidence remains unchanged.
