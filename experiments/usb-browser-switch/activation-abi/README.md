# USB activation: full function extents and concrete timer teardown

The SourceSelectView timer's previously unidentified entry destructor is now
resolved. It removes the registered timer from its schedule and deletes the
entry, while the timer message invokes callbacks outside that same mutex.
Schedule removal therefore supplies no proof that an already selected callback
has finished. The separately queued raw timer capture still needs its own
retention/cancellation contract.

The guarded DeviceLibrary action and named `setSource(Source)` service also
have complete static extent and exception reviews. Their known native callers
pack the expected values, but normal return registers do not provide a proven
success status. Both functions have real cleanup/unwind paths; the final source
dispatch can unwind past the action's normal epilogue. A wrapper cannot treat
entry or ordinary return as accepted activation or final completion.

This successor adds exact-image static evidence only. The frozen
[activation ownership evidence](../activation-ownership/) is unchanged. No
wrapper, native adapter, hook, USB button or runtime enabling change is added.

| Review | What it resolves | What remains open |
|---|---|---|
| [Guarded action](ACTION-REVIEW.md) | Complete 761-instruction extent; two caller frames; local hidden-result destinations; normal return and cleanup routes | Transitive/indirect contracts, machine-state forwarding, wrapper CFI, live event ownership |
| [Named service](SERVICE-REVIEW.md) | Complete 1,033-instruction extent; register/stack packing; 36-row LSDA; known caller continuation | Live receiver identity, direct/queued service retention, actual source choice and completion |
| [Timer teardown](TIMER-REVIEW.md) | Concrete entry constructor, RTTI, virtual deleting destructor and callback unlock/call/relock order | View retention/serialization, queued-task cancellation and permissible listener reentry |

The [AAPCS64 register and result rules](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)
provide the review vocabulary: x0–x7 carry arguments/results, x8 can carry an
indirect result address, and callee preservation includes x19–x29 and the low
64 bits of v8–v15. Those rules do not determine the full signature or forwarding
requirements of these particular native functions. Direct absence of a register
operand is not proof that opaque transitive callees cannot observe its value.

## Reproducible bounded evidence

[verify-static.py](verify-static.py) passed its first focused run with
[review-pins.json](review-pins.json). It binds the complete private ELF hash,
14 exact FDE/code extents and full fixed-width decoding, 79 selected semantic
anchors, 13 pointer relations, four RTTI strings and eight PLT import identities.
It records FDE/CIE hashes for all selected functions and decodes only the three
explicitly reviewed cleanup LSDAs: action, explicit caller and source service.
Other LSDA pointers are recorded, not interpreted. Counts and anchors are static
facts, not path coverage, lifetime tests or an automatic ABI proof.

The [result](result.json) has SHA-256
`3ec0d934b971b3f2b4733df8416309102c98c8a3edf3e98e9db067c676cbd6fe`.
[Verification receipt](verification.json) binds the exact verifier, pins,
review documents and retained private run logs. Python 3.11.16, Capstone 5.0.7
and pyelftools 0.33 were used. Two independent source-only reviews checked the
verifier and selected pins; the full four-byte call-site interval check and an
explicit `lsda_decoded` flag were added before the run. A subsequent read-only
receipt audit is recorded there. No unchanged suite or runtime branch was
repeated, and no sanitizer or native-exception test is claimed.

For a genuinely changed review, provide a private copy of the exact image and
a fresh output path; the verifier refuses replacement:

```sh
python verify-static.py --elf /private/path/to/EP147 --output /private/fresh-result.json
```

Do not rerun the unchanged passing check. The parser deliberately rejects other
LSDA encodings rather than approximating them. Raw disassembly, firmware,
private analysis scripts and logs remain ignored under `private/`.

Two preparation-only bookkeeping failures are preserved in the receipt: a
private timer anchor filename glob was too narrow, and initial pin assembly
used a nonexistent `extra` key. Both failed before writing their intended
outputs and were corrected from the inspected inputs before the focused run.
They were not native or verifier failures.

## Next bounded work

Resolve the queued task-box owner's lifetime/cancellation and native
message-thread/destruction serialization before using an embedded timer listener
to navigate. Alternatively, review an independently owned activation route.
For any passive action/service observer, first establish full entry/return
state preservation, actual wrapper/trampoline unwind behavior, live receiver
provenance and existing hook ownership. Copy values only within the native
entry scope; no post-original native reads or saved borrowed pointers.

Final Local Library/USB readiness, asynchronous cancellation, missing media,
lifecycle, alternate databases and scroll/sort remain open. Timer schedule
removal is not a new activation permit or proof of a native use-after-free.
All observer, stems staging and product runtime gates remain CLOSED. No emulator
or physical deck, core, settings, package, catalog, backup or snapshot was changed.
