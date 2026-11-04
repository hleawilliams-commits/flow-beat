# USB activation: ordinary shutdown ordering

This static review resolves two earlier GuiObjectManager participants and the
selected application's normal shutdown chain. It extends the
[owner graph](../activation-lifetime/) without changing an implementation,
calling native code, or reopening activation.

The manager's two earlier objects are SubDisplay instances. Their immediate
destructors perform local cleanup, including mutex destruction, but supply no
established service-task drain or cancellation receipt. Separately, the normal
application runner returns from its outer message loop before shutdown. EP147's
ordinary shutdown resets the manager-owning member; application deletion follows,
then registry cleanup when the shared GUI initialization count reaches zero.
This is a concrete normal-path ordering result. It does not establish a live
operation-scoped owner for a proposed USB action or complete callback quiescence.

## Evidence

- [Manager participants](MANAGER-ORDER.md): concrete types, virtual slots,
  immediate cleanup and unresolved transitive boundaries.
- [Application and registry ordering](SHUTDOWN-REGISTRY.md): registry snapshot
  deletion, selected runner, actual normal shutdown reset and manager ownership.
- [New exact-image pins](review-pins.json), [check result](result.json) and
  [verification receipt](verification.json).

One first-time check with the unchanged
[static verifier](../activation-abi/verify-static.py) passed:

| Checked subject | Count |
|---|---:|
| New complete function/FDE extents | 27 |
| Selected instruction relations | 296 |
| Pointer relations | 30 |
| RTTI names | 7 |
| Imported symbol relations | 11 |
| Newly decoded LSDA tables | 0 |

All 27 full function extents are new relative to the preceding 42-extent owner
review. Selected new relation anchors also refer to the already-pinned manager
constructor/destructor; those old full bodies were not replayed as function
checks. Independent source and retained-result audits are recorded in the
receipt. Runtime suites and previously successful native branches were not
repeated.

The verifier's inherited result scope label remains
"static exact-ELF ABI and concrete timer teardown only". Its subject here is
this directory's new shutdown pins; it does not confer ABI, runtime retention,
or cancellation proof. FDE/CIE fingerprints and selected metadata are checked,
but no new LSDA table or alternate exception execution path is established.

The private image remains SHA-256
6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925.
Raw disassembly, local paths, scripts and diagnostic logs stay in the ignored
private directory. Public pins contain selected relationships and fingerprints,
not a firmware copy.

## What remains open

The outer runner cannot advance to its post-loop shutdown while its own
synchronously invoked callback remains on the stack. A nested loop alone does
not overturn that ordering. Actual alternate destructive/replacement paths,
other producers and dispatch contexts, exceptions, and pending raw captures
still need their own evidence. The normal shutdown reset is not evidence that
a custom activation can retain or destroy the same objects.

Next connect a permitted native invocation to its actual live owner scope and
execution context. The existing native Local Library caller and copied-value
contracts still need exact final source/readiness and wrapper ABI/unwind proof.
No reference-count mutation, direct source call, post-return native read, view
cancellation, native adapter or USB button follows from this review.

All activation, observer, stems staging and physical gates remain **CLOSED**.
No emulator/device access, installation, package/catalog change or release
occurred. Preserve the completed publication reconciliation and earlier
failures. Do not rerun this passing pin set unchanged.
