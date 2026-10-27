# Independent review and fresh offline repeat

No blocking source defect was found within the documented sole-port-family and trusted-backend contract. This review covers the retained in-memory owner and copied accounting; it does not approve runtime enablement.

## Ownership and accounting

`MixerWorker::join` takes the real thread handle, joins without holding mailbox or route locks, then constructs the private, non-Clone `JoinedMixerSession`. Its retained `Arc<Shared>` keeps the original route, backend and settings sink alive. The lifetime test removes all outside ports and the fixture's route reference before checking that neither endpoint object has been destroyed. Dropping the session subsequently permits their destructors to run, without new submit, baseline or settings calls.

The producer records a FRAME checkpoint only after matching the complete immutable envelope and the exact next kernel sequence. Recording precedes the settings callback, so a callback panic leaves the accepted ACK available with `CallbackArmed` and explicit poison. This is an accepted claim from the injected trusted backend, not independent broker authentication or proof of filesystem contents. Error, skipped, returned-success and armed-without-return dispositions remain distinct.

Diagnostic salvage does not clear mutex poison, mutate admission, query the backend or authorize recovery. An older raw `admission_open` value remains subordinate to the poison flag. `Pending.uncertain == false` also does not prove non-commit: a malformed positive response can leave the exact unresolved envelope without setting that bit. The new fixture and strict control preserve that case.

The baseline checkpoint is the latest exact acknowledged local handshake, including the caller-asserted release transition. It is not the original recovery baseline or current native state. It can belong to a newer generation than the bounded latest FRAME ACK; no history or automatic cross-generation association is provided. Terminal port clones cannot rearm the joined worker. External direct route clones remain outside the exclusive-use contract, so these diagnostics are not a general concurrent or cross-process freeze.

Review comments were addressed before freeze: backend-destructor effects on final Arc drop are explicit, admission/mutation failure is distinguished from diagnostic poison reads, and historical checkpoint getters warn against treating their values as current permission.

## Independent validation

A fresh export from pinned emulator commit `06fc981707e69982c0554bb508e0c646c005a979` applied the frozen parent patches and this successor. The independent run used its own private Cargo target with locked offline dependencies. Production configuration compiled successfully, with the existing visible unused-code warnings. The executable listed 126 tests; only the 11 new joined-session cases ran, and all passed. The 115 inherited tests were compiled/listed but not executed again.

All seven controls compiled and each failed its one selected test at the intended assertion with exit 101. The lifetime control makes the retained Arc weak while preserving actual join; the other controls remove pre-callback ACK recording, erase an earlier ACK on close, hide unmarked pending work, conceal poison, promote an unacknowledged baseline, or accept a foreign ACK payload. These are functional assertion failures, not accepted compiler errors, deadlocks, timeouts or unrelated panics.

The independent check reconstructed every mutant from each retained original tree and compared every resulting source file. It then independently bounded the selected test function, recalculated the assertion macro's line and column, and matched an actual panic header. All seven locations were checked in both primary and independent snapshots (14 checks); this did not import the runner's location routine.

All result fields agree except the explicitly enumerated raw compiler/test-log hashes in [comparison.json](comparison.json). All eight complete test outputs agree after replacing only each private run path, named panic-thread numeric IDs and libtest durations. Raw JSON and logs are not byte-identical, and full compiler-log text equality is not claimed. No source or runner change was made to normalize or rerun this result.

## Limits

The source remains inactive in `main`, with no application exit wiring. A real local join is not restoration, neutralization, cancellation, native-consumption evidence, a teardown permit or a callback deadline. A session preserves backend identity only while its in-memory owners survive; final Drop can close resources without restoring anything. Existing Unix supervisor EOF and Windows job-close behavior can bypass App cleanup on process death. [LIFECYCLE-LIMITS.md](LIFECYCLE-LIMITS.md) records those constraints and the separate host-file writer/recovery problem. No emulator, UI, process supervisor, network endpoint, device, firmware, settings file or native task was exercised by this repeat.

## Pins

- Successor patch: `4267249c94882120ad9b8e2b37e3894704db95876d51b5c42a10a5f244596cad`
- Runner: `1e3d3ba20b8bfa4721128196eb6c832751de061fa3061e3dd0610c1209185d69`
- az-preview/src/mixer_producer.rs: `ac4d1c2e0c03a3e3a50719c3fccd0ac08a056cfe77ac858bfc1e772accda1680`
- az-preview/src/mixer_route.rs: `6e2aad717deaed4407ced1648859f121c867810a973297f700de48791653d60e`
- az-preview/src/mixer_transport.rs: `7dcfa16de3eeecf90c729d37f8e48dffeb3887d9b9eaa6502b04498a3ce44d26`
- Primary result: `0daa7fe345d41eb5b63520bcde429bd9db06c36adb046bce407c3cb89c9536d5`
- Independent result: `c19a654b5a6e4e6a67724c38d19ec21bf9aaa8ea6a486f43cf1711185f08593e`
- Comparison: `cc27a625d4a90e43ec3543ece5cf29496a58aa1866b14eaf6462f38c78bfa4fc`
