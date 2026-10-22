# Route observer binding

This observer uses the separate native encoder route at `0x1b01cf8`, identified
in [the static route review](../input-route/). The prior observer at `0x1adeee0`
accepted a hook but captured no entries in its bounded run. That result neither
proves this alternative was active nor explains why the earlier entry was
absent. This binding performs no installation, emulator access or native call.

The exact ELF must match the pinned SHA-256. Its FDE must begin at `0x1b01cf8`
and span `0x2e8` bytes. Both static table slots `0x2d453e8` and `0x2d45758` must
resolve to that entry. They belong respectively to the base
`HuiAdaptedViewDispatcher<DefaultGuiTaskBoxConf>` and derived
`gui::ViewTransitionManager` encoder-observer subobjects. The first four
instructions save FP/LR, set FP, and save x19–x22. The source's four expected
words must match all 16 bytes. The previous route review's pinned policy accepts
this prologue, but a live SDK claim still needs an available island and exclusive
ownership. Static slot reads are compatibility guards; no vslot is patched.

The candidate uses the existing five-input ARM64 tail wrapper byte for byte:
`x0`, `x1`, full-width `x2`, `w3`, `w4`, and original LR supplied to the helper
in `x5`. Its immutable source SHA-256 is
`a1f1cf42cc587c3222d299ac9dd31bfd0944f1ea51ec98a19d74ba40088a5034`.
[The earlier ABI fixture](../dispatcher-observer/abi/) exercised 55 assertions
and strict wrong-restore, wrong-LR and truncated-x2 controls. Those are retained
prior results, not new ABI executions. This binding validates the old fixture
source/evidence hashes, the identical candidate wrapper, its build wiring, and
the candidate's six-argument nonthrowing helper. Any wrapper change invalidates
this reuse and requires a separate ABI review.

Observe addresses only as opaque integers; retain no native pointer for later
reads or actions. The native event record at `0x2360f80` is a different,
pointer-taking interface and must not use this scalar binding. Its external
reader lifetime is unproven. Original LR can be inherited through tail calls
and therefore does not necessarily identify the immediate virtual-call site.

The static task route can forward to child handlers without entering either
previous generic wrapper. Entry does not establish enqueue success, a particular
thread, delayed execution, handler return, completion, cancellation or retained
object lifetime. The dispatcher-field/helper comparison is an opaque equality
gate, not a proven cancellation epoch. Live rotary registration, child identity,
physical-counter ordering and historical liveness at an input instant remain
open. Correlate any future distinct run with independently checked
[input-stage evidence](../input-stage/); RAW and normal callback observations
are not by themselves the final native counter.

The verifier fails closed on source or artifact mismatch. Meaningful controls
exercise a wrong native entry, incorrect or duplicated slots, changed prologue
words, and wrong LR/x2/restoration wrapper forms. No old passing ABI loops are
rerun. The stable candidate passes **34 binding checks and 18 strict rejection
controls**, recorded with exact source hashes in `binding-result.json`. Controls
also reject an omitted derived-slot comparison or a derived-slot read accidentally
using the base address. The exact prior ABI result is pinned to SHA-256
`d46963a16c5b3b71d6834a4e3792dc79fc45fdb81cb7d3a78d6f0d026ff3e0d7`.


Run from repository root with the existing private interpreter:

```sh
../../../work/qemu-python/bin/python \
  experiments/mods-shortcut-delivery/route-observer/verify-boundary.py \
  --elf ../../../work/picks-native-probe/EP147
```

Independent source review of the bound `observer.cpp` found no concrete blocker
within this passive scope. Native entry captures only its five input scalars and
LR; attempt/gate counters precede errno/TID work, and the active helper preserves
errno. The RAW callback reads six SDK-provided bytes, performs no frame writes,
and returns zero. Guarded queues, drop/overflow diagnostics and separate logging
budgets bound observation work. RAW and native records have independent sequence
spaces; matching counts or printed order do not establish causality. A dropped
RAW change is not replayed merely because space later becomes available, which
the new host fixture explicitly checks. These observations do not measure first
TLS access, hardware timing, native delivery or concurrent historical gate state.
The candidate's host results, completed ARM64 build and later runtime result are
separate evidence; this verifier itself performs no installation.
