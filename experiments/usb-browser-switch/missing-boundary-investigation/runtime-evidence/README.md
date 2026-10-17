# Bounded V4 native-path observation

On 2026-10-17, V4 0.0.1 ran only in the localhost emulator. All four SDK hook claims were accepted. The first USB1 and USB2 selections entered the native **empty-list request path**; returning to USB1 reached the existing nonempty-list commit observation. This reproduces the three-selection pattern from V3 and directly establishes the previously unobserved branch for the first two selections in this new run. Final transaction completion remains **UNKNOWN**.

[Sanitized logs](observer-sanitized.log) retain full supplied packed values, sequence correlations, statuses and native return codes. Process pointer values and numeric process/thread identifiers are omitted; `main-thread` means every observed TID equalled the process PID. [Structured results](runtime.json) pin the tested build and restoration checks. Raw screenshots, logs, exact backups and helper scripts remain ignored local files.

## Sequence and observations

Both existing emulator USB images were mounted before staging. After the intentional emulator process restart, the existing USB2 was reannounced through the documented emulator mechanism at its verified actual `/media/usb/sdb1` mount. No image, database or core was replaced. Native SOURCE, touch-row selection and browse-knob confirmation selected Local Library for USB1 → USB2 → USB1. There were no custom activation calls or LOAD/PLAY actions; all four decks remained empty.

| Outer event entry | Request | Added path observation | Separate helper result | Separate outer result |
|---|---|---|---|---|
| 3 | USB1 `1:0102` | Empty path, sequence 4, status 13; supplied `0001010200000001` | Sequence 5, rc 0 | Sequence 6, status 8, rc 0 |
| 9 | USB2 `1:0122` | Empty path, sequence 10, status 13; supplied `0001012200000001` | Sequence 11, rc 0 | Sequence 12, status 8, rc 0 |
| 15 | USB1 `1:0102` | Commit boundary, sequence 16, status 10; copied identity `1:0102`, busy 0 | No empty-path call observed | Sequence 17, status 8, rc 0 |

Both empty-path records compared only the complete MediaID and agent-pointer identity with their entry-time copies. Each supplied category byte was 1; its expected value and enum meaning remain unproven, so category matching remains **unknown**. Status 13 is explicitly partial and does not assert full packed-argument validity. Helper rc 0 records local native request admission/state only. Neither helper rc 0 nor outer rc 0 establishes asynchronous completion.

The exact caller gate for the empty-list method was `0x1ffbed0`; the additional claimed function entry was `0x1fd3a18`. The static equality edge and live scalar argument evidence distinguish these first selections from the nonempty path observed on the return. The result does not retroactively instrument the earlier V3 run or prove identical behavior for every restart/library.

Counts were six parent callbacks, three event-entry/result pairs, two empty-path entry/result pairs and one copied commit-entry record. All ran on the process main thread. No mismatch, wrong-thread refusal, queue-drop report or budget exhaustion appeared. Private screenshots independently showed native BROWSE identifying USB1, USB2 and USB1 with library rows. That visible UI evidence is separate from a generation-correlated completion contract.

## Restoration and limits

Before staging, 124 files and two absent paths were recorded with SHA-256 and tar-derived permissions. Atomic `.new` copies and rename staged only the probe, its enabled/approval entries and namespaced service opt-in. Logging was temporarily enabled. Neither core library was modified.

Afterward, the probe files, owner record and service opt-in were removed; baseline lists and flags were restored. Final verification checked all 124 file contents and modes, both absent paths, exact root inventories, both core hashes, the identical loaded-mod list, absence of the opt-in and absence of the probe mapping. The normal startup guard was allowed to clear naturally after restart.

The bounded run did not inject failure or cancellation, test missing media, issue already-current requests, or test alternate library formats. Static and host evidence show why helper failure can be hidden by outer rc 0, but this run observed no helper failure. Async reply ownership, cancellation and stale-result handling remain unresolved. No USB shortcut implementation, physical-device validation, final completion guarantee or new community release is claimed.
