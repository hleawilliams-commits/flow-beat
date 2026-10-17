# Separate header shortcut slots

Stem Picks 0.4.1 and Mix Bookmark 0.1.1, emulator only.

The two shortcuts now have distinct, adjacent hit areas in a dedicated header row. STEM PICKS / MIX BOOKMARK have a 20px gap in 4 DECK. Compact PICKS / MIX have a 10px gap in 2 DECK and 2/4 DECK, leaving the native phase display clear. Buttons stop at y80 above the waveform rows. The top-left WAVEFORM title is restored.

Validation: ARM64 build and package validation; existing SDK harness passes plain, ASan/UBSan and TSan, eight gates per pass for each mod. Mix Bookmark uses its previously documented isolated harness fix. macOS no-I/O/no-allocation gates remain unmeasured.

Screenshots check all three native layouts. Tapping MIX opens the bookmark panel, closing it restores the appropriate compact/full button, and tapping PICKS opens the native browser in both two- and four-deck modes. Installed-state hashes confirm the UI core, stems engine and three existing saved bookmarks were preserved.

No physical installation, catalog update or release promotion. The prior source/packages are retained under archives/harmonic-picks/0.4.0 and archives/mix-bookmark/0.1.0.
