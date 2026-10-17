# Selected Deck Highlight 0.1.0 verification

- Focused host suite: 299 assertions passed with AddressSanitizer and UndefinedBehaviorSanitizer; -Wall -Wextra -Werror.
- Official SDK harness: 9 checks passed in each plain, ASan/UBSan, and TSan run, including the custom functional scenario. macOS does not measure the harness no-I/O/no-allocation checks.
- ARM64 SDK build and package validation passed.
- Installed binary SHA-256 matches the built binary (see build-info.json).
- Emulator screenshots visually checked for all four selected pairs: 1+2, 3+2, 3+4, 1+4. Number badges and cyan panel edges follow the selected controls; stock MASTER remains distinct.
- 2 DECK view hides the overlay; returning to 4 DECK restores it.
- Wide waveform view checked with 3+2 selected; no waveform coverage.
- Core binary hashes unchanged. Harmonic Picks 0.3.0 and Phase Strip 0.1.2 remain loaded; Set Clock remains disabled.

Physical hardware is not tested or modified. The emulator core already contains a separate experimental stems patch and is not included in this package. Generated catalog URLs are future release destinations, not published links.

## Restore the previous appearance

Turn off Selected deck highlight in the mod settings, or disable/remove the independent deck-focus community mod. Other mods and the official core do not need replacement. Before any future physical USB installation, follow the recovery-kit backup and identity verification requirements in the workspace AGENTS.md; preserve the existing restore point until hardware testing succeeds.
