# USB activation: authored-target SDK transfer fixture

This successor to the [binding composition](../activation-binding-composition/)
examines the exact SDK's late island/trampoline transfer in an isolated ARM64
process. It is not an installable observer. The targets are authored assembly
functions in a synthetic executable; no firmware, emulator, physical deck,
current native row/provider or application object is accessed.

The production entry, helper and binder are copied byte-for-byte from
`f6033a026737169c8476f988c9701a8625d4f702`. The SDK installer, protection helper
and interface header are privately extracted from
`3e0c5cb010454228f4ee63226b60d060494eeaae`. The new bridge substitutes only the
claim provider and diagnostics: it validates the requested identities and
passes authored target addresses to the pinned installer. It does not implement
the SDK community loader's module/init admission or establish loaded hook
ownership.

## Transfer contract

The pinned `inline_hooks.c` late route checks all four opening instructions.
It allocates a real nearby island, copies one opening instruction into an
anonymous trampoline, seals that trampoline RX, fills/relocks the island,
publishes the original slot, and then redirects the target with a single B.
If a nearby island is unavailable after late mode begins, the installer refuses;
this fixture must not relax that policy or use the four-word fallback.

Its absolute transfer uses LDR x17 followed by BR x17, with an eight-byte target
literal. There are two such transfers: island to wrapper and trampoline to
native continuation. The frozen wrapper restores its saved state, then uses
x16 to load and tail-transfer through the permanent original slot.

| Boundary | Stack and link register | Scratch / unwind limit |
| --- | --- | --- |
| Patched authored entry to island | Original SP/LR | B does not create a new return address. |
| Island to wrapper | Original SP/LR | x17 becomes the wrapper address; no anonymous island FDE is supplied. |
| Wrapper helper and restoration | 800-byte wrapper frame, then original SP/LR | The prior entry CFI review remains separate. |
| Wrapper tail | Original SP/LR | x16 becomes the trampoline address, the documented tail-transfer exception. |
| Trampoline opening STP | Authored native frame allocated; FP/LR saved | No anonymous trampoline FDE is supplied, including after this stack change. |
| Trampoline to native +4 | Native frame retained; original LR | x17 becomes native +4. Authored native CFI resumes at that address. |

The two authored openings use 144-byte and 272-byte frames, matching the
retained action/service opening shapes. Their +4 CFI uses SP-based CFA with
saved FP/LR offsets. This is suitable for their own relocated STP and following
MOV FP,SP. It neither decodes new firmware extents nor validates arbitrary
relocation, other targets, PAC/BTI or other installer modes.

An ordinary C++ exception raised in a body after the native continuation can
use that body's FDE and unwind to its original caller: the wrapper has already
tail-transferred and adds no return frame. That question differs from unwinding
an interrupted PC inside an island or trampoline. This fixture deliberately
does not register anonymous unwind metadata or test asynchronous unwinding.

## First new matrix

The first complete matrix passes **32 named checks across four fresh-process
selectors in each of plain and ASan/UBSan modes**. Each selector installs both
authored targets through the actual pinned late installer and invokes one:
action return, service return, action ordinary throw, or service ordinary throw.
No earlier suite is executed.

The unchanged production helper/binder configuration is now linked and executed
through this synthetic route, with no fixture scheduling macro, helper rename
or external interceptor. The claim-provider substitution described above is
the only activation seam. A new compilation of the frozen entry source is used;
no retained parent object is executed.

Before invocation, the fixture checks owned target/slot identities, nonzero
aligned originals, the generated B and transfer destinations, the copied STP,
unchanged following target words, and actual RX mappings. Checks then establish:

- one-word target patch, sixteen-byte island and twenty-byte trampoline;
- body x16 equal to its trampoline and x17 equal to target +4;
- preserved entry SP/LR and x0–x2, one register-only capture, and the selected
  synthetic result or exact body exception;
- one body and one caller cleanup, with forwarding slots retained after close.

The body records scratch registers before any BL can clobber them. It does not
directly observe wrapper-entry x17; that value follows from the emitted island
instructions and executed transfer path.

One safe semantic control changes both installer absolute-transfer instructions
from x17 to x16 while preserving each destination. It executes all four
selectors per mode and fails exactly **emitted-route** and **body-scratch**,
while forwarding, capture and ordinary exception cleanup still pass. Two
additional controls change only the authored action/service post-STP CFA.
They retain identical target machine code and code relocations, are rejected
for exactly the designated CFA row, and are never linked or executed.

The [sanitized result](result.json) retains 14 input identities, 82 commands,
164 output hashes, 72 artifact identities and a 462-file private run count.
Raw outputs, command arguments, synthetic runtime addresses/bytes, linked
executables and pinned upstream source copies remain private. Source review
caught and corrected a draft CFI parser escape, missing pre-execution CFI
failure gate, report-path mismatch and missing structural route preflight
before the first build. The first complete run has no failed branch.

Independent retained-byte verification is recorded separately in
[verification.json](verification.json); no successful runtime branch is
repeated for that audit. Two earlier auditor-only failures are preserved: the
first incorrectly rejected valid duplicate GCC SHT_GROUP section names; the
second required whole-object identity across different compile source paths.
The corrected audit permits only the named valid group case and the four
observed debug-only section differences, while requiring identical entry
code/CFI and relevant relocation bytes. It independently decodes raw .eh_frame
for both targets and callers. These corrections did not change or rerun the
fixture, and no whole-object identity across variant paths is claimed.

ASan/UBSan instruments the new C/C++ fixture, actual included installer/protection
code and production helper/binder. Assembly and executed generated instructions
are not sanitizer-instrumented. These selected serial cases supply no TSan,
general concurrency/lifetime or installed-native-hook unwind proof.

## Boundaries retained

The fixed-width helper continues to capture action x2 only as opaque pointer
bits and service Source only from entry registers. It does not dereference
native pointees or read after native return. There is no return interception,
TLS pending state, cross-entry pairing, native action, USB button or release.

Any successful synthetic route is limited to these authored targets, the
pinned source, compiler/image and selected ordinary return/exception paths.
It does not establish actual native arguments, current visible-label/action-kind
correlation, provider availability, view/service ownership, queued work,
alternate replacement/destruction paths, callback quiescence, current hook
ownership or instruction-publication synchronization on the application.

Closing capture still does not drain an admitted callback, unpatch the entry
or authorize slot/code/sink reclamation. The nonnull-before-reachability
contract remains necessary. Writer exclusion, actual application/supervisor/QEMU
topology, retained read outcomes, stopped recovery, USB activation, stems
staging and all unresolved runtime/product gates remain CLOSED. Kyle's core,
community packages, published archives and rollback are unchanged.
