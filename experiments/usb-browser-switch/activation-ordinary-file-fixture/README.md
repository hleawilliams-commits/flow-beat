# Offline ordinary-file witness

This separately versioned C11 witness runs ordinary file callbacks through the unchanged terminal engine in a pinned isolated Linux AArch64 process. Every live gate remains **CLOSED**.

The first GCC 7.5 matrix passes **126 summed assertions (22 distinct names) across eight ordinary selectors and six control baselines per mode**, plus six exact singleton controls per mode: **40 fresh exec witness children** across plain and ASan/UBSan. Leak detection is enabled. The same-Linux parent supervisors are separate from those child counts.

## Source and native custody

The [retained plan](../activation-ordinary-file-plan/README.md), [source ledger](source-ledger.json), [transformation map](source-transform.json), [contract](test-contract.json), [source review](source-review.json) and [review lock](review-lock.json) bind the reviewed prebuild inputs. The entire engine/header, six reader/type fragments (two layout/storage fragments and four functions), sole terminal admission and five setup-only functions remain unchanged. All owner/module/frame inputs remain authored; the file-generated case uses synthetic ranges, not installed code.

A parent supervisor creates a fresh private directory and explicitly inherits its directory and report descriptors into one fresh exec child. It opens the evidence files outside the disposable directory before child execution. A fixed complete typed provider table, context and request/model storage stay alive through synchronous return. The provider creates only the fixed name `witness.dat` using exclusive creation, nofollow and close-on-exec flags. It holds a successful native `int` descriptor before identity checks, validates file/directory ownership, and checks the full signed 64-bit token range, exact descriptor equality and held custody before every narrowing or follow-up operation. No corrupt token reaches native I/O.

Native raw returns and valid failure errno are captured before explicit provider instrumentation. The native pair, supplied callback value, prepared-return journal, actual ordinary return, authoritative engine receipt and public acquire-gated completion remain separate. A prepared journal alone does not establish outer raw capture. The frozen engine still validates only reached callback functions; this witness supplies a stable complete table.

Actual close entry moves custody to unknown before the call. A returned close error would remain unknown, with no retry or further operation through that token; this path was not exercised. External cleanup happens after the immutable report is frozen and may close only a held descriptor whose actual close never entered. Parent observation requires confirmed child termination, exact-size report reading, nofollow identity checks and bounded file readback. Fixed-name unlink and empty-directory removal have separate receipts. Neither engine completion nor observed bytes proves durability or recovery.

## First cases and exact controls

The ordinary cases cover direct/generated authored ownership, real exclusive-create collision, real write failure on a read-only descriptor, a supplied close refusal with separate held cleanup, an actual 32-byte write followed by an invalid supplied result of 33, opaque no-owner work, and a terminal repeat after actual caller mutation. In both modes, the read-only write returned `-1/EBADF`, its subsequent actual fsync returned zero and close returned zero. All successful provider opens observed descriptor 3; native fd zero was allowed by the source but was not exercised.

The supplied-close case made no native close inside the attempt. The invalid-write case returned incomplete with no public payload and made no sync/close call inside the attempt. Both retained held custody for separate cleanup. No-owner calls only the opaque authored unblamed callback and creates no witness file. The repeat preserves the copied owner/completion despite changed caller ID and PC/LR/FP values.

| Provider-only control | Exact singleton failure |
| --- | --- |
| Consume instrumented ambient errno | `native-failure-pair-survives-instrumentation` |
| Report open token plus 2^32 | `native-open-token-and-custody-correspond` |
| Write different owned bytes | `owned-write-preserves-copied-bytes` |
| Omit native sync | `sync-receipt-has-native-call-evidence` |
| Omit native close | `close-receipt-has-native-call-evidence` |
| Perform a second bounded owned write | `terminal-provider-writes-once` |

Controls reuse the same candidate core, native-leaf and harness objects; only the provider object changes. Corrupt-token follow-ups are refused before native calls, leaving an empty file and a held cleanup obligation. Omitted operations are reported as supplied values, not native outcomes. The second-write control records two full owned writes and 64 observed bytes; it never retries a failed write. Each composite oracle owns its corresponding downstream effects, while shared checks still reject unrelated native failures.

## Build, review and retained evidence

The first build-only preflight completed before any witness invocation. A separate [method/dependency review](codegen-review.json) checks 351 selected method extents against retained ELF bytes and every disassembled instruction word. The [target layout review](native-layout-review.json) confirms native `nlink_t` is four bytes and the same-build child report is 4,760 bytes. The earlier unexecuted eight-byte guess was corrected before compilation.

Each native leaf has one selected direct libc invocation; fstat/fstatat lower to `__fxstat`/`__fxstatat`. This is not a kernel syscall count or proof of transitive libc/interposition behavior. Sanitizer checks, stack-canary dependencies, fortified formatting and memcpy/memset/memcmp remain. The terminal CAS lowers to a retrying LL/SC loop; bounded or wait-free admission is not established. Standalone core configurations are compile-only.

[Sanitized results](result.json) and [independent verification](verification.json) retain 35 inputs, 120 commands, 240 output streams, 204 build artifacts within 295 work files, and 40 JSON/binary report pairs. The complete packet has 816 files. Independent source/control reviews and the separately frozen corrected byte audit pass. Its **463,495 structural audit assertions are not runtime cases**. The corrected auditor passed on its first invocation and did not invoke the runner, compiler, disassembler or any witness.

All implementation and oracle corrections preceded the first build. Retained drafts cover complete source custody, native header inclusion, attempted-versus-completed process counts, read-only fsync's accepted native result domain, actual FP mutation, shared outcome checks that prevent unrelated native failures from imitating controls, and the native-width correction. No built source changed, no compiler/runtime attempt failed, and no completed predecessor matrix, capture or audit was replayed. The first frozen byte auditor incorrectly equated sorted JSON selector keys with the separately authored C selector enum order and refused the packet. Its unchanged source and failure streams remain retained. A separately reviewed one-line correction binds the sorted contract order while preserving enum-index and actual command-order checks; the witness sources and runtime outcomes were unchanged. Both executed auditor versions were copied before invocation and retained unchanged. A separate publication writer initially named nonexistent `.bin` review streams and stopped before writing public files; the retained review uses `.txt` streams and stores its exit in `invocation.json`. The failed writer/streams and corrected metadata writer are retained; no review or matrix was replayed.

Raw paths, file identities, binary reports, command streams and build artifacts remain private. Public files contain source, authored-value summaries, reviewed code metadata and hashes. No device logs, firmware, credentials or music are published.

## Limits and next boundary

Actual native close failure, native short write, EINTR, post-open query refusal, missing-return/nonlocal exit and failed external cleanup remain unexecuted. Preparation/report failures have explicit infrastructure handling but this passing matrix does not establish those failure paths. The child cannot acknowledge its own report write; the parent supplies separate exit/read/size evidence. Pre-exec descriptor enumeration is not a complete per-call receipt journal, and isolated-process termination is not acknowledged cleanup or recovery.

No private stepping/inspection/diagnostic API, second admission, callback nesting, concurrent publication, native frame/crash-file provider, CM_BLAME/CM_CRASHES, native quarantine/startup policy, signal handler, DSO/installer, firmware/emulator/device action, production integration, live observer or release occurred. Arbitrary callback/descriptor lifetime, external writers, native visibility, loader/interposition, signals/nonlocal exits, quiescence and recovery remain unresolved. Quarantine and official protections are unchanged; ownership is not fault causation.

Next make a separately scoped source-only ordinary-file failure-custody review. Reuse the frozen ordinary-file witness, exact engine/readers, native-provider review and retained first matrix without replay. Map each setup, post-open validation, returned failure, missing-return, report, observer and cleanup boundary to the evidence actually obtained, separating real native failures from supplied callback refusals and declared safe controls. Identify the smallest justified future extension for close uncertainty or incomplete resource custody, with explicit external ownership and exact safe controls before any implementation. Do not manufacture a real close failure by reusing or operating on a possibly closed token. Preserve the sole terminal admission, immutable completion, setup-only surface and opaque unblamed boundary. This slice is source-only: no new adapter, compiler, native I/O, callback/runtime matrix, nesting, concurrency, handler/signal/nonlocal exit, DSO/installer, firmware/emulator/device, production or live work. Keep all prior sources, failure packets, LL/SC/libc/canary limits, quarantine and every live gate unchanged; ownership is not causation.
