# Focused native construction graph evidence

Two fresh matrices pass: primary and one independent invocation. Each executes
13 new graph selectors in plain, ASan/UBSan and TSan modes, with one final
compound functional check per selector. Eleven strict compiling controls per
mode fail exactly their selected functional oracle. Both matrices and the
retained-file audit passed on their first invocations. No unchanged parent
suite was repeated.

[Primary](host-result.json) and [independent](host-result-independent.json)
results are byte-identical. The [comparison](comparison.json) records their
retained-source/log audit. Both result files have SHA256:
87bbcf835e82ddd6b21453e98ffbdb633e9187f76673d8a82039b8965c10194f.

Compiler: Apple clang 16.0.0 (clang-1600.0.26.6), arm64-apple-darwin25.2.0.
Each invocation records 54 clean compiles, 36 clean links and 72 executions.
Production translation units use -Wall -Wextra -Werror without warning
suppression. Only the fixture permits unused parameter/function/variable
warnings for inherited kernel shims. Controls require exit 1, their exact
named failure and exact check summary. Compiler/linker diagnostics, setup
assertions, sanitizer reports, timeouts and extra output cannot satisfy a
control. No Linux ARM64 build is claimed.

## New cases

| Selector | Focused observation |
|---|---|
| retain | Copied plan survives caller mutation; creation performs no I/O; fd0 transfers only after graph/claim retention; premature start and repeated transfer make no calls. Idle plans and zero session/producer are refused before transfer. |
| armed-chain | Synchronous native/sync entry inspection sees the retained graph, acquired children and exact armed stages. Child results retain their raw values; setup cannot repeat. Backend callbacks/context are absent from copied diagnostics. |
| preflight-stop | A partial 104-byte copy of the 208-byte pre-STATE followed by EFAULT keeps the original owner and stops before CLAIM or duplication. |
| claim-unknown | The actual extracted CLAIM commits before a scripted EIO return; Unknown remains retained with native ownership and no postflight or adapter promotion. |
| postflight-stop | An actual legacy write after preflight changes CLAIM's committed contents; postflight mismatch preserves committed evidence and stops before duplicate attachment. |
| duplicate-stop | Scripted EMFILE retains the already-stored adapter and CLAIM/original without cleanup or validation. |
| adapter-state-stop | Partial third-STATE copy followed by EFAULT keeps both original and duplicate descriptors and prevents broker setup. |
| broker-init-stop | Scripted mutex-init EAGAIN before pthread retains broker/attribute and both lower descriptors, without broker validation or dispatcher construction. |
| broker-state-stop | An actual same-file alias FRAME before the fourth STATE changes sequence and prevents dispatcher construction while retaining all lower resources. |
| ready-wire | Actual ADOPT and FRAME traverse the frozen dispatcher/broker/adapter and extracted driver. The full 200-byte native frame and canonical 668-byte response match expectations. Malformed input closes dispatcher admission; exact historical cached retrieval remains available through the graph. |
| dispose-order | Scripted mutex-destroy EBUSY, or attr-destroy EINVAL after actual mutex success, preserves descriptor dependencies. A separately justified later disposal skips released synchronization resources and closes duplicate before original. |
| close-uncertain | Duplicate or original model close takes effect before EINTR is reported. Raw reports and first/latest history survive; no old fd number is retried. Duplicate uncertainty stops original close. Later known cleanup leaves an uncertain shell requiring separate explicit finalization. |
| partial-dispose | Allocated-only and taken-before-start graphs dispose only their acquired resources. Caller ownership survives when no transfer occurred. |

The ready-wire case observes five STATE calls, one CLAIM, one duplication and
one FRAME, with seven native ioctl calls. A later cached response has historical
provenance and causes no additional native call. Construction stage evidence
does not change during exchange.

The fixture uses exact extracted driver methods and frozen child sources.
Descriptor/syscall behavior, usercopy, RNG and kernel locking are modeled.
Successful synchronization calls execute actual pthread methods; injected
refusals occur before pthread. The actual frozen broker operation lock/unlock
calls are never replaced. This does not reproduce libc/VFS behavior, native
concurrency, contention or live uncertain-close resolution.

Inspection during a callback is same-thread synchronous evidence. It does not
establish concurrent custody or quiescence. Graph first/latest disposal reports
reflect the last graph publication; live child evidence may be newer while a
child disposer is executing. Inspection is not an atomic cross-child snapshot.
Consumed-child diagnostics are archived evidence, not current resource ownership.

Fixture cleanup copies observations first, accounts for known modeled effects,
clears pre-call scripts and explicitly disposes remaining resources. An uncertain
close is modeled as having taken effect, so fixture accounting can determine
which child remains. This is not runtime recovery or retry authority.

No allocation substitution or private counter mutation is used. Allocation-error
branches and disposal-attempt exhaustion are source-reviewed only. The tests
target the new composition and do not exhaustively revalidate inherited policies.

## Discriminating controls

Every control alters only new graph.c in a private variant directory.

| Control | Oracle selector |
|---|---|
| omit-stage-arm | armed-chain |
| discard-raw-stage-result | preflight-stop |
| lose-retained-original-borrow | armed-chain |
| repeat-start-after-ready | armed-chain |
| hide-dispatcher-cache-after-error | ready-wire |
| omit-child-diagnostic-archive | partial-dispose |
| omit-duplicate-close-arm | dispose-order |
| continue-after-uncertain-duplicate | close-uncertain |
| replace-first-disposal-report | close-uncertain |
| omit-graph-slot-consume | partial-dispose |
| export-callable-backend-diagnostics | armed-chain |

[Controls](controls.json) preserve exact changes and failure labels;
[inventory](test-inventory.json) binds selectors and counts. The repeated-start
control reaches the frozen one-shot preflight refusal without issuing another
native call. The backend control removes both scrub sites but never calls the
exported pointers. The uncertain-close control permits early original close;
the fixture checks actual child presence before intermediate finalization, so
cleanup remains valid. The slot-consumption control frees normally; its selector
records the missing consumption and clears the local dangling slot from the
release report before cleanup. Controls must reach their final functional oracle
without sanitizer or setup failures.

## Source provenance and retained audit

Each private run captures seven new inputs, pins 24 frozen inputs and generates
24 source/header inputs. A fresh export of pinned emulator commit
06fc981707e69982c0554bb508e0c646c005a979 receives the three existing source patches.
No emulator module is built or installed. Seven exact driver methods are
extracted from that prepared source: mixer_instance_init, xput_bytes,
xget_bytes, ctrl_release, mixer_owned_next, ctrl_ioctl and ctrl_write.

The retained adapter reuses the exact 6,378-byte native adapter prefix before
mba_attach, SHA256:
385ad0b7b604c5525c10b33de4683b4a4bbde25519d114a0c8c52a8b12410420.
The retained broker reuses the exact 4,936-byte prefix before mb_create and
7,117-byte suffix from mb_adopt, combined SHA256:
4bfd81f15e656db6633c938556b013fdb55f27499960e6e26ceed8fd3cf3a2c3.
Frozen CLAIM, retained adapter/broker and wire/dispatcher files are copied
verbatim. Neither child policy nor any parent source is edited.

The private retained-file audit independently reconstructs the native/broker
cuts and driver functions by line boundaries, then checks prepared-source
receipts, captured/frozen/generated inventories, all 11 variants, every compile/
link/execution log and public result copies without rerunning tests.
Its SHA256 is 9f81e402b4f52fcc7501633ccc8eb0bef939c0edf5b1cd475cab258055ce528d.
The comparison counts all matrix and audit attempts.

Source/harness/control/runner/audit review and optimized-Python refusal checks
preceded matrix execution. AST/refusal checks created no matrix or audit attempt.
No failed matrix or audit attempt occurred in this checkpoint; historical parent
failures remain unchanged. Raw source exports, builds, logs and audit scripts
remain ignored under private/.

This is local construction and ordered dependency-disposal evidence under
external serialization, quiescence and uncertainty-accounting requirements.
The graph does not prove those requirements, exclude aliases, authenticate an
endpoint or retain itself across process death. Original and duplicate custody
do not establish exclusive native ownership. The first graph accepts injected
plans only; the standalone claimant's idle evidence remains separate.

Owned transport/supervisor binding, live libc/VFS, external writer exclusion,
authoritative originals/native stop, callback deadlines, complete overlay/
both-file recovery and durable/process-death custody remain open. No endpoint,
emulator restart, physical control, installation, core, package, catalog or
release changed. All existing runtime gates remain CLOSED.
