# Bounded V3 native-entry observation

On 2026-10-17, V3 0.0.1 ran only in the localhost emulator. All three claims were accepted. A native USB1 → USB2 → USB1 sequence produced **one `COMMITTED_AT_ENTRY` record**, on the return to USB1. This verifies that the exact native caller gate and copied in-handler observation can execute. It does not establish final transaction completion, all-path coverage or custom activation.

[Sanitized logs](observer-sanitized.log) preserve request identities, sequence correlations and statuses while omitting process pointer values. [Structured results](runtime.json) pin the build, counts and restoration checks. Raw screenshots, logs and exact backups are ignored private local files and are not part of this repository.

## Bounded sequence and result

Both existing emulator USB images were mounted before the run. After the intentional emulator process restart, the existing USB2 was reannounced through the documented emulator mechanism using its verified actual `/media/usb/sdb1` mount. No image, database or core was replaced.

Native SOURCE, browse-knob and touch controls selected each USB and confirmed Local Library. One intermediate knob movement was still in the Local Library/Cloud pane and produced two parent callbacks without a media request; the visible native USB2 row was then selected and confirmed normally. No LOAD, PLAY or custom native-function invocation was issued. All four decks remained empty. Private screenshots showed native BROWSE identifying USB1, USB2 and then USB1 again with the existing library rows.

| Event entry | Requested media | Boundary observation | Separate native result |
|---|---|---|---|
| 3 | USB1 `1:0102` | No record; UNKNOWN | Sequence 4, status 8, rc 0 |
| 9 | USB2 `1:0122` | No record; UNKNOWN | Sequence 10, status 8, rc 0 |
| 13 | USB1 `1:0102` | Sequence 14, status 10, committed `1:0102`, busy 0 | Sequence 15, status 8, rc 0 |

The missing first two boundaries are consistent with the static unobserved paths, but this run did not instrument those branches and does not assign a cause. The matching third record is copied at helper entry before forwarding; native result and visible UI evidence are distinct observations. No state is read after either original returns.

All eight parent callbacks, three event entries, one commit entry and three scalar results used TID 901130, equal to the process PID. Parent modes were three mode-0 and five mode-1 snapshots. No wrong-thread refusal, queue-drop report or budget exhaustion appeared in the bounded log. Hook addresses were `0x1b296c0`, `0x1ffb6c8` and `0x2014a88`.

## Restoration and limits

Before staging, 124 existing files and two absent paths were recorded with SHA-256 and tar-derived permissions. Staging used atomic `.new` writes and rename for the probe, its enabled/approval entries and namespaced service opt-in. Normal logging was temporarily enabled. The official/core files were preserved throughout.

After the sequence, only the probe's files, owner record and service opt-in were removed. Three modified enabled/approval lists were restored. The probe was unmapped after returning to the original loaded-mod set. All 124 baseline files and modes, absent paths, root inventories and both core hashes were checked. The normal `.starting` marker cleared on its own after restart; it was not bypassed.

Missing media, already-current requests, asynchronous cancellation, final browser readiness, alternate libraries and physical-device behavior remain untested. Even a matching boundary followed by rc 0 is not a final completion contract. No new community release or USB shortcut activation follows from this result.
