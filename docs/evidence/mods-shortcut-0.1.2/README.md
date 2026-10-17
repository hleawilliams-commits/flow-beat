# MODS Shortcut 0.1.2 validation

The exact ARM64 binary `c5d5e564b6e7006faf20dd372c077adba537105026850b6d04ee6d8294ed9aef` was exercised in the local XDJ-AZ emulator on October 17, 2026. The packaged binary matches those installed test bytes. The physical AZ was not changed, controlled, restarted or reloaded.

## Checks

- ARM64 SDK build and validation passed. Package validation passed; its only files are LICENSE, azmod.json, icon.png and mods-shortcut.so.
- 302 shortcut and 232 component assertions passed plain and ASan/UBSan. 35 concurrency/idle assertions passed plain, ASan/UBSan and TSan, including real simultaneous producer/filter activity. An independent reviewer reran the suite before the final six delayed-feedback assertions were added; the author reran the final suite.
- The official macOS SDK harness passed 9 measured checks each in plain, ASan/UBSan and TSan. Its missing frame_filter service means those passes do not exercise an active SOURCE pulse or measure the Linux no-I/O/no-allocation gates.
- A normal touchscreen tap opened native SOURCE with empty decks ([screen](source.png)).
- With only the emulator filter's dispatch flag deliberately set to off, repeated taps expired, cleared the request word and accepted a later request. This simulates quarantine; it did not generate eight actual overruns or change the protection budget. The SOURCE fallback was visually confirmed ([screen](timeout.png)). Initial 1.3-second screenshots preceded banner display; later screenshots showed it. Feedback depends on UI/message-thread scheduling.
- After a normal emulator restart, four copies of the prepared test track played. Three MODS taps opened native SOURCE across an 80.75-second scripted check; all four remaining-time displays advanced between captures. The filter was active with zero strikes at each snapshot in this run. Numeric observations are saved here; screenshots containing private library content remain local.
- An earlier empty-deck run accumulated one timing strike before the deliberate quarantine simulation. This update does **not** prove prevention of future overruns. There was no duration/performance comparison against 0.1.1 and no physical long-run test.

The emulator UI core SHA-256 was `f8d4ba67ccad9e3386c78dc2ca377bf3263221875d65578a1d893e9c2f3f5668`; its stems core was `cd95ce64bd1e4cbd4c7710aa77adc7546623e9782baf8fa9bd8d4acbe562a1d3`. This is the existing Flow Neon emulator baseline, not the exact physical UI binary. Core ELF allocated sections matched the local unstripped symbol copy before bounded state inspection. Neither core file changed during these tests.

The request protocol uses immutable timestamp tokens, atomic ownership and token-tagged completion. Host schedules check stale callbacks before/after claiming, disabled dispatch, fresh clock reads after a delayed claim, native-input precedence and a release edge before replacement pulses. These checks establish the tested protocol behavior, not strict deadlines under arbitrary thread suspension. Idle valid frames skip clock/view calls and still track the native knob.

## Restored baseline and physical limits

All 14 backed-up file paths were restored/verified (11 existed); prior MODS Shortcut 0.1.1 and the prior community set loaded again. Both cores match their original hashes. The emulator restart unloaded its test tracks. [Restoration result](restored.json).

On the physical board, 0.1.1's filter was already quarantined. UI timeout cannot reactivate it. A user-initiated normal Reload after stopping playback is needed to register an installed update. Further timing overruns remain possible; use physical SOURCE → MOD MENU if it times out. No stem-engine fix, USB shortcut, theme, direct-menu core service or logging-core patch is included.
