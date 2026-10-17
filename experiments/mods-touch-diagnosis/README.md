# MODS touch failure: diagnosis only

The physical deck loaded the exact published MODS Shortcut 0.1.1 binary, but Henry reported that touching MODS did nothing. This experiment adds specific failure feedback and retained diagnostic words to that version. It is **not a confirmed fix, release or physical install**. Production `mods-shortcut/` and its version remain unchanged.

## What was established on October 17, 2026

- Read-only inspection verified the loaded binary's SHA-256: `48159c3165c07f66c9ace13f1cbb75939cae589527b375aa982b9ba7a45262ca`.
- The touch handler had accepted a request. The shortcut was available and registered; there was no logged filter-budget disable.
- After that request, pending and sent were zero and the pulse deadline was still zero. Therefore the request was cancelled before this instance ever began a SOURCE pulse. The retained state does **not** identify which guard cancelled it.
- The pinned core delivers `AZ_EV_DOWN` followed by `AZ_EV_TAP` on a surface's X ButtonPress, with screen-absolute coordinates. The current component uses those semantics correctly.
- Root's emulator checks opened SOURCE for both an 80 ms contact in 4 DECK and an 800 ms contact in 2 DECK. They did not reproduce the physical failure.
- Three passive 128-byte reads of the physical firmware's existing mixer input buffer passed its CRC-16/X25 check over bytes 0–95. Bytes 31, 32 and 33 were zero in those samples, and the browse counter was steady. Byte 6 was zero. These samples occurred **after** the reported tap and cannot establish the guard values during it.
- Exact-firmware parser review found that screen and corner button bits are read without conditioning them on byte 6. Do not weaken the native-input cancellation guards by treating byte 6 as an idle validity flag.
- A later report that MODS had disappeared coincided with the native BROWSE screen. This button intentionally appears on the deck/waveform screen only; that observation is separate from its failed action.
- A subsequent capture confirmed the button was absent on the actual 4 DECK waveform screen. The mod remained loaded and available; exact-core `row_value` cache inspection showed its **MODS source shortcut** row was 0/OFF while **Hide WAVEFORM heading** remained 1/ON. This explains that missing-button report. The user can turn **MODS source shortcut** ON through SOURCE → MOD MENU without a Reload. Whole-mod enable and this per-button toggle are separate; the inspection did not change either setting. The earlier accepted-but-cancelled tap is still unresolved.

The possibilities still include native input during the request, a transient SDK view change, or no mixer frame arriving before the one-second deadline. No core, physical USB, running process, diagnostics flag or control was changed during this investigation. Reading process memory used bounded reads from known mod variables and a vtable-validated native object chain; no ptrace, process pause, signal, heap scan or direct device read was used.

## Proposed diagnostic build

`diagnostic.patch` applies to the existing `mods-shortcut/shortcut.h`. `shortcut-diagnostic.h` is the resulting complete helper for review and host tests. The production file has not had the patch applied.

On the normal UI tick, an unsuccessful request displays one bounded banner naming the guard:

- Screen button active.
- LOAD/BACK or a corner button active.
- Browse knob pressed or moved.
- Clock unavailable or a request expired before mixer input arrived.
- Deck view changed before any SOURCE press was sent.
- SOURCE press sent, followed by another SDK view. This does not claim failure: the emulator sometimes briefly reported BROWSE while the native SOURCE screen was opening.
- No mixer input received after the tap (distinct from cancellation).

The bus callback only publishes atomic numeric state. It performs no logging, formatting, filesystem access, allocation or UI call. Successful SOURCE arrival retains the existing banner and pulse bounds. Native input still cancels the request without changing any input byte. The UI does not clear a pending request itself; it remains an observation of the existing behavior, so this diagnostic patch does not repair a request that has received no mixer frame yet.

The retained words are `gp_shortcut_cancel_reason`, `gp_shortcut_observed_input`, `gp_shortcut_observed_delay`, `gp_shortcut_observed_view` and `gp_shortcut_saw_frame`. The packed input word contains bytes 6, 31, 32, 33, 34 and 35, in that order from least significant byte. The observed delay is milliseconds since the request. The fields are individually atomic and are diagnostic snapshots, not an atomic multi-field transaction. Capture them after the request completes and before another tap for a coherent record.

## Validation

Run from this folder:

```sh
./run-tests.sh
```

183 assertions pass in plain and AddressSanitizer/UndefinedBehaviorSanitizer runs. The tests exercise all cancellation guards, preserve native LOAD and navigation frames byte-for-byte, ensure no banner is called from the frame callback, distinguish a delayed frame from no frame, verify one-shot feedback, and retain successful SOURCE behavior. The script also applies the patch to a temporary copy and compares it with the reviewed helper.

A separate emulator transport-gap experiment now builds this diagnostic helper: see [the reproduction](../mods-shortcut-delivery/README.md). The saved screenshots precede the shorter feedback wording and distinction between a view change before/after sending SOURCE. Current wording has host regression coverage; it has not been visually rerun. No physical diagnostic installation or test has been performed. Before a diagnostic physical build, preserve the current package and the user's recovery backup, assign a distinct version, build/validate it and exercise it in the emulator. Any hardware test must happen when the user is ready to reload; the current practice session was left running.
