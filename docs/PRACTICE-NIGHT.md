# Henry + Jackson: practice-night check

Grouped Deck Panels **0.1.0** is staged on the physical **FLOWMATIC** USB. Its four package files were verified, but the deck still needs to approve and load it. A fresh backup was verified before installation, and the original restore point is preserved. The official cores and unrelated files were left unchanged.

The original Key next to BPM and Phrase Coach packages are retained but disabled to make room for the grouped display. Set Clock is off. FLOW Beat is unchanged; Phase Strip remains **0.1.1** and Harmonic Picks remains **0.1.0**, without a header shortcut.

## Start the new display

Safely eject FLOWMATIC from the Mac, then start the AZ with the USB inserted so its loader sees the new package. With playback stopped, open **SOURCE → MOD MENU → Community Mods → Installed → Grouped Deck Panels → Allow**, then choose **Reload now**. Reload restarts the deck application; do this before practice, never during a mix.

Open **4 DECK** view. Each lower deck header should show its track, key and phrase beside the native BPM. The grouped headers appear only in 4 DECK view.

## Ten-minute check

1. **Deck selection — 2 minutes.** Switch the left side between decks **1 and 3**, and the right side between **2 and 4**. The cyan number/border and **LEFT/RIGHT** label should follow the deck that side controls. This indicates control selection, independently of MASTER or playback. Check that titles remain readable and native BPM, time and waveforms remain clear.
2. **Phrase and key — 2 minutes.** Start a track from a known downbeat. PHRASE counts a repeating **eight bars of four beats**, anchored near the first play after loading. It does not detect the song's musical phrases. Use **Deck N phrase starts now** in Mod Menu to reset its anchor on a known downbeat. `?` means uncertain; `--` means unavailable. KEY shows the original analyzed key, not the key after Key Shift. Missing library metadata can take about 20 seconds plus scan time to appear.
3. **Stems on four decks — 4 minutes.** Use tracks whose stems are already complete. Load the same prepared track onto decks **1, 2, 3 and 4**, leaving the earlier decks loaded. Once native loading finishes, check the pads and briefly isolate a stem on each deck. Then replace tracks in reverse deck order. If stems disappear on one deck, compare that exact track from the same USB on another deck. Note the track, USB, failing deck, working deck and load order. Save logs with **Mod Menu → System Info → Save Logs to USB** before rebooting; they go in `MODS/logs`. The physical stems issue is **not confirmed fixed**. The experimental retry engine remains in the emulator.
4. **B2B browsing — 2 minutes.** Henry selects a playlist and track on his USB; Jackson selects a different playlist and track on his. Switch through the native **SOURCE** screen twice each. Check whether each USB returns to its own playlist and selection. Report any reset or unexpected destination.

## If the grouped display causes trouble

Stop playback. In Community Mods, disable **Grouped Deck Panels**, re-enable the retained **Key next to BPM** and **Phrase Coach**, then **Reload now**. Set Clock can stay off. This restores the earlier display combination; it is not a stems repair.

If the deck cannot reach its menu, use Henry's existing private recovery kit to restore **community files and their settings only**. Preserve the current official core and selected known-good restore point. The Git repository contains [recovery-tool source](../tools/recovery/README.md), not the private restore point. Follow the [recovery requirements](../AGENTS.md).

**Not installed on the physical USB:** the new USB buttons, Phase Strip 0.1.2, MODS Shortcut 0.1.1, or the simplified Save Combo flow. Do not expect those controls during this test.
