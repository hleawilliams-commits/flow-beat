# Independent adapter checkpoint

The new adapter completes offline SDK integration of the frozen capture,
transport and wrappers. It remains unpublished and uninstalled. The prior
route-burst emulator result and its fresh restoration are separate evidence;
no device was contacted for this checkpoint.

## Accepted source and independent executions

| Item | SHA-256 |
| --- | --- |
| `observer.cpp` | `538574af5f357131e0fa9ec6e26d54cfef0ec2a57f31baddff1e8e5bc7c134ed` |
| `mod.c` | `051cf4243b0dde46552d55784feed337727094df065ee4c3efd551edbe1f46fc` |
| Actual-source host result, both runs | `53c8770200bdccfece5dc9b1c08ef3b88c9dfaa42b3f59e015669599cfb7a227` |
| Static binding result, both runs | `5517b9d2a96d50ae6efac3e6d03ad08ac0f3a133bf9a2a828d40526b902fbe84` |
| Private ARM64 module | `71d7fc088f4def67d1307e4faeef301020aed04bfa3da7ae4bc6a422ece3e255` |

The strict host author run `399daf44-ac0e-4fd6-80e4-52ffd65e176c`
and independent run `9b17fb55-cc14-49a0-b80b-1df185f56802` retain
separate immutable source snapshots, compiler outputs, executables and logs
under ignored `out-history/`. Their public JSON is byte-identical. Every mode
passes 660 assertions across 41 fresh processes. Five deliberate source faults
produce exactly 1/2/2/1/1 named failures in plain, ASan/UBSan and TSan, without
sanitizer or crash diagnostics. This is an independent repeat of the new
adapter matrix, not another run of unchanged earlier suites.

The binding author and independent runs pass the same 96 checks and 52 strict
controls. The checker verifies 44 instruction anchors, five data words and two
RTTI names, complete selected body hashes/FDE extents and exact copied wrapper
and dependency identities. Source-shape checks are deliberately identified as
such. Preparatory verifier-only failures are disclosed in [BINDING.md](BINDING.md);
they are not adapter executions or native failures.

## Review focus

Review followed permanent storage through every init outcome: worker creation
before claims, helpers entered while a claim is in progress, first/second
claim rejection, independent worker failure during installation and late
normal returns after closure. The actual SDK uses a detached worker, supplies
no stop/join handle, keeps opened module libraries mapped and leaves claimed
hooks installed after failed initialization. The adapter does not use a
stack-local original slot or reconstruct storage on retry. The direct claim
arguments and unchanged wrappers bind forwarding to those permanent slots.

Root and a separate reviewer inspected the actual test source. Mock log
vectors/counters are accessed on the reader/main path; producer read tracking
is thread-local and shared diagnostics are atomic. Concurrent consumers read
published records before producer acknowledgement/join. Reservations are
intentionally ordered to distinguish invocation identity from publication
order. Pending-init worker steps are scripted within mock services, not proof
of native scheduling. Dummy wrapper symbols satisfy the host link: this suite
does not execute native forwarding or ARM64 instructions.

Tests distinguish a hole from an already copied record, a log attempt from
delivery, quiet observation from quarantine, and reader exit from producer
quiescence. No native task pointer is retained in output. The return helper is
used after a real synthetic allocation is unmapped, but native entry borrowing
still requires valid, immutable storage for the complete copy. Neither caller
LR nor vptr checks establish mapping validity or retained dispatcher lifetime.

## Build and remaining runtime gate

[build.py](build.py) snapshots its inputs before compiling inside the existing
pinned ARM64 SDK image with networking disabled. SDK/header/tool and image
identities are recorded in [build-result.json](build-result.json). The SDK
validator passes; no package archive is produced. The build snapshot is
`build-34bea645-2604-48e3-8094-7644561e1d42`. Raw disassembly, symbol tables,
CFI, binary, SDK validation output and source snapshot stay private.
[BUILD-REVIEW.md](BUILD-REVIEW.md) records the separate compiled-load and
wrapper-metadata review. A build and offline metadata review are not installed
trampoline execution, hardware latency or asynchronous-unwind evidence.

Before a new passive emulator run, bind an own-module scalar sampler to this
exact binary and review fresh loaded-file/process/mapping readiness, exclusive
hook ownership, baseline backup and selective cleanup. Use the current emulator
state; never restore an earlier whole snapshot. Native task data may be copied
only inside the synchronous pre-forwarding helper. Do not sample native objects
after return. A new bounded input sequence must address the still-unobserved
submission/normal-return/execution relationship, not repeat the successful
route-burst sequence as though it supplied these boundaries.

Keep a missing boundary UNKNOWN. Post return is not handler completion,
executor entry is not successful handling, and matching tuples are not event
identity. No direct menu, focus handback, view cancellation, recovery activation
or USB buttons are justified by this checkpoint. No community release or
physical-installation recommendation is made. The stems staging gate remains
closed and the MODS 0.1.3 lane-diagnosis counterexample remains unresolved.
