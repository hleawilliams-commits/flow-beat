# V2 handback candidate: offline host checks

The extracted V2 candidate passes **228 assertions per mode** in plain, ASan/UBSan and TSan. Ten separate source mutations each fail exactly their required functional assertion in all three modes; sanitizer diagnostics are rejected even for expected-failure controls. This is a source-level check of instance binding and atomic same-owner generation rotation. No module was loaded, no device was contacted and no producer adapter was exercised.

## Reproduction and provenance

From the repository root:

```sh
python3 experiments/emulator-overlay-handback/run-tests.py --emu ../xdj-az-emu --result experiments/emulator-overlay-handback/private/reproduction.json
```

The runner invokes `prepare.py` into a new private UUID directory. Preparation checks the pinned emulator checkout, applies captured verified phase-one and successor patch bytes, and verifies the exact resulting file inventory and hashes. The runner also compares both applied patches with its retained input snapshots. It extracts 14 complete actual functions, their needed declarations, the unedited V2 UAPI and the pinned frame header. Source bodies, full frames, compiler output and executables remain private. The public result contains fingerprints and assertion summaries.

The host is Apple clang 16.0.0 (`clang-1600.0.26.6`), target `arm64-apple-darwin25.2.0`, with `CC` unset. This runs a host executable; it is not an ARM64 Linux kernel build. Kernel integer conversion, usercopy, capability, RNG and scheduler services are controlled host shims. `miso_lock` uses a pthread mutex. Native64 ioctl encodings are represented by explicit Linux include shims; compat execution is not tested.

| Input or result | SHA-256 |
|---|---|
| V2 patch | `fa800f32c5e9227cc1953028d35b896e711a40b0d4fd6f2e03dcdf2ac6eaf0dd` |
| V2 candidate C | `bab273a308dde0ba696394d2383461af10413f1975e836b2058456dba2833717` |
| V2 UAPI | `355316f5047509ae4c787210fddf0bd8b659c600ac1e153adf0543e28cf35aec` |
| Preparation helper | `6a35c4ac9c3d9671d1d652bbba57ea6a49f2e4f8865427c1d58751b340ffad84` |
| Runner | `1a8030db691cc6df52bf5afc7d94fd460360875f3c535a43070c859ed2745f83` |
| Fixture | `37882354077f05ee87a5f9655a8325380d02c4a43b292081d8ecb8fd49ead9a4` |
| Sanitized result | `112935e5fded38c073e40b1fb46f9b24c890f1cb187cff2ab2d1d6d3d94c7217` |

`host-result.json` binds the base commit and file hashes, phase-one inputs, per-function extraction hashes, declarations, module-init body, include shims and each tested variant. Source inputs are checked against retained snapshots again at the end. An independent parent run rebuilt all 33 mode/variant combinations from a fresh prepared source copy and produced a byte-identical result (`112935e5fded38c073e40b1fb46f9b24c890f1cb187cff2ab2d1d6d3d94c7217`).

## Exercised boundaries

- Disabled instance initialization performs no RNG call. Enabled initialization propagates a failed wait without filling bytes; one successful wait and fill is required, and an all-zero identity is refused. Either 64-bit identity word can carry the nonzero value. These are deterministic RNG stubs, not an entropy or uniqueness test.
- All four mutations bind both instance words. An equal file/run/epoch/sequence with a different simulated module identity is refused. The wrong-instance check precedes file authorization. Exact V1 ioctl constants are refused before usercopy. V2 sizes, offsets and command numbers are checked.
- ROTATE requires the existing owner, capability, write mode, opt-in, valid header, current run/epoch and exact next sequence. Partial input copy and malformed headers preserve all overlay and generation state. Last allowed counters and subsequent exhaustion are checked without wrap; close can still orphan an exhausted owner while preserving the fence.
- Successful ROTATE commits all 152 opaque frame bytes, increments epoch, resets sequence and retains file owner, run, instance and fence. New-generation sequence 1 works; its duplicate fails.
- A request paused after its actual input copy but before its commit is tested three ways: old FRAME, RELEASE and ROTATE each fail after a successful same-file ROTATE, even though old numerical sequence 1 equals the new epoch's next sequence. A copied ROTATE also loses to a FRAME that consumes its sequence first.
- Concurrent same-header ROTATE/FRAME and ROTATE/ROTATE contenders have exactly one success and retain that winner's complete frame/generation state. A separately forced snapshot at the unlock boundary distinguishes one atomic transition from a deliberately split frame/epoch commit.
- A STATE request whose snapshot is already copied under the lock retains the whole historical pre-rotation result even if its usercopy finishes later. A sampled SPI frame similarly survives rotation with its original full payload and CRC. Rotation does not retract a frame already sampled by a caller.
- The inherited legacy-write, clear, CRC, file identity and ordinary-frame tests execute against the new V2 source as interaction regression. The 1,000-write/read stress case uses ordinary FRAME commands, with a midstream acknowledgment forcing an actual reader observation while the producer is live. Rotation interleavings use the explicit schedules above; the stress case is not a rotation stress test.

Two intentionally successful counterexamples constrain the interpretation. STATE can report sequence 0 while a copied FRAME remains pending and later commits sequence 1, so the snapshot cannot classify that request as cancelled. After ROTATE, a producer that replaces an old payload's header with a valid current-generation header can submit those old bytes successfully. The kernel treats the payload as opaque; producer queue ownership and a no-retag rule need their own implementation and validation.

The 22 source-integration checks bind handler registration, absent compat handler, default-off immutable opt-in, CRC setter/getter and array metadata, V2 ABI assertions, random header, module-init registration and an error-returning identity initialization immediately before the first device allocation. The complete module-init body is retained and hashed, but its allocator/cdev lifecycle is not executed by the fixture.

## Strict negative controls

| Actual-source mutation | Required failed assertion |
|---|---|
| Ignore RNG wait failure | `random_wait_failure_before_fill` |
| Accept all-zero identity | `zero_instance_refused` |
| Remove instance comparison | `instance_identity_required` |
| Remove file identity check | `file_identity_required` |
| Keep epoch during ROTATE | `rotation_commits_frame_epoch_and_zero_sequence` |
| Keep submitted sequence during ROTATE | `rotation_commits_frame_epoch_and_zero_sequence` |
| Suppress only ROTATE's payload copy | `rotation_commits_frame_epoch_and_zero_sequence` |
| Remove ROTATE epoch-overflow check | `rotation_epoch_exhaustion_unchanged` |
| Ignore epoch in owned-next check | `copied_old_generation_refused_after_rotation` |
| Unlock between frame and epoch commit | `rotation_is_one_locked_state_transition` |

The first eight controls have exactly one assertion and one expected failure. The last two have three assertions (including thread creation/join), exactly one expected failure and exit code 1. Candidate execution must have exactly 228 assertions, no failed labels and exit code 0. No sanitizer warning is accepted as a substitute for the named functional failure.

## Retained development receipts and limits

The first count-discovery execution produced 228 passes; the runner rejected its intentional expected-count placeholder of zero. That source snapshot and output remain private. The next plain matrix passed all ten controls (result `94c7f00d211d03503b3e94472c57e3110a01537101c27f3e9f14efb072cd17d0`). Before the final matrix, the omitted-frame control was narrowed to suppress only the payload copy while retaining the other ROTATE updates, making the frame assertion independently discriminating. Both earlier snapshots are preserved. No candidate function was changed for these fixture refinements.

The previous phase-one runner, fixture, result and host-test note retain their recorded hashes. This successor neither replaces those results nor grants their runtime prerequisites.

The tests do not establish real VFS reference retention or final-close timing, interrupt/spinlock behavior, real kernel parser behavior, crypto readiness, identity uniqueness after VM snapshot replay, kernel/module lifecycle, transport acknowledgement, producer generation retention, queued native-event cancellation or safe handback. STATE remains a historical value snapshot. An accepted rotation is an opaque overlay commit, not proof that native software consumed it or that a producer has safely resumed.
