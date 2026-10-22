# Complete saved-overlay source audit

This is an offline binding of the local preview and driver sources to a proposed **54-offset preservation schema**. It does not change the earlier 28-offset policy, authorize input or reopen the refused runtime attempt. The verifier reads only four pinned source files; it never imports the driver, opens saved user settings, contacts the emulator or reads firmware.

`audit.py` passes **147 checks and 29 strict rejection controls**. Root independently reran the final verifier and reproduced byte-identical JSON (SHA-256 `0c56b9ea78627ff1c99cfe40cfea4548691f36cc168b9341c1fb93da6ab5196d`). Its selected Python `_mixer` and `_mixer_state` AST functions execute only against synthetic maps, in-memory text and mocked counter callbacks. Rust function fingerprints bind the manually reviewed bodies; this run does not execute Rust or validate hardware. `audit-result.json` contains exact source/function hashes, extracted field meanings and control results. No source body, idle frame, user values or firmware is included.

From the repository root:

```sh
python3 experiments/mods-shortcut-delivery/task-sdk-observer/overlay-policy-v2/source-audit/audit.py \
  --source-root ../xdj-az-emu \
  --result /tmp/overlay-source-audit.json
```

The verifier requires the exact local source hashes listed in its result. It rejects whole-source drift, missing/changed selected functions, analog field aliasing/displacement, changed packed-bit shifts, expansion into reserved bits, swapped FX TIME bytes, changed selector/counter/latch/X-PAD offsets, duplicate button bits and button overlap with the selector. The negative controls exercise extraction without using the whole-source hash as a substitute for every semantic check. Function-body controls remain identity checks rather than proofs for arbitrary replacement code.

## Derived analog fields

`MX_AT` has 17 controls and `MX10` has 23. The union has **40 distinct high bytes and 11 packed-low bytes**, all disjoint. The preview's `mixer_levels` sorts and deduplicates exactly these 51 offsets before persistence.

For controls with a low byte, the source value is `high * 4 + ((low >> shift) & 3)`. The setter clamps to 0–1023 and replaces only that pair of low bits. Sound Color FX PARAMETER has only its eight-bit high byte in this mapping; its source getter yields `high * 4`. MIC 1/2 LEVEL are explicitly excluded by the source as analog gain controls outside the bus.

| Control | High byte | Packed-low byte | Low-bit positions |
|---|---:|---:|---|
| CH1 fader | 10 | 14 | 6–7 |
| CH2 fader | 11 | 14 | 4–5 |
| CH1 trim | 36 | 40 | 6–7 |
| CH2 trim | 41 | 45 | 6–7 |
| CH1 HI | 37 | 40 | 4–5 |
| CH1 MID | 38 | 40 | 2–3 |
| CH1 LOW | 39 | 40 | 0–1 |
| CH2 HI | 42 | 45 | 4–5 |
| CH2 MID | 43 | 45 | 2–3 |
| CH2 LOW | 44 | 45 | 0–1 |
| CH1 COLOR | 61 | 65 | 6–7 |
| CH2 COLOR | 62 | 65 | 4–5 |
| Sound Color FX PARAMETER | 78 | — | — |
| crossfader | 15 | 16 | 0–1 |
| CH3 fader | 12 | 14 | 2–3 |
| CH4 fader | 13 | 14 | 0–1 |
| master LEVEL | 56 | 60 | 6–7 |
| CH3 TRIM | 46 | 50 | 6–7 |
| CH3 HI | 47 | 50 | 4–5 |
| CH3 MID | 48 | 50 | 2–3 |
| CH3 LOW | 49 | 50 | 0–1 |
| CH4 TRIM | 51 | 55 | 6–7 |
| CH4 HI | 52 | 55 | 4–5 |
| CH4 MID | 53 | 55 | 2–3 |
| CH4 LOW | 54 | 55 | 0–1 |
| MASTER HI | 57 | 60 | 4–5 |
| MASTER MID | 58 | 60 | 2–3 |
| MASTER LOW | 59 | 60 | 0–1 |
| CH3 COLOR | 63 | 65 | 2–3 |
| CH4 COLOR | 64 | 65 | 0–1 |
| MIC 1 HI | 66 | 70 | 6–7 |
| MIC 1 MID | 67 | 70 | 4–5 |
| MIC 1 LOW | 68 | 70 | 2–3 |
| BEAT FX LEVEL/DEPTH | 69 | 70 | 0–1 |
| MIC 2 HI | 71 | 75 | 6–7 |
| MIC 2 MID | 72 | 75 | 4–5 |
| MIC 2 LOW | 73 | 75 | 2–3 |
| BOOTH LEVEL | 74 | 75 | 0–1 |
| HEADPHONES MIX | 76 | 80 | 6–7 |
| HEADPHONES LEVEL | 77 | 80 | 4–5 |

All high bytes admit integer values 0–255. Packed-low bytes 14, 40, 45, 50, 55, 60, 65, 70 and 75 use all eight bits. **Byte 16 uses only mask `0x03`; byte 80 only `0xf0`.** The complementary bits are unmapped by these tables and zero in both pinned idle definitions. A strict preservation policy should require those bits to remain zero; it must not clear or reinterpret them silently. “Unmapped” does not establish that firmware never reads them.

## FX controls are distinct fields

Byte **22** holds the four-bit Gray-coded FX selector in its high nibble. All sixteen high-nibble patterns correspond to the source's sixteen selector positions. Its low nibble contains four buttons: ON/OFF plus LOW/MID/HI frequency controls. A neutral policy therefore requires **the entire low nibble `0x0f` to be zero**, preserving the high nibble unchanged. Checking only ON/OFF bit 0 is insufficient. The source notes that FX selector/ON-OFF behavior was measured on a deck but unconfirmed by the emulator's FX panel; this audit adds no runtime validation.

Bytes **26 and 27** are a separate FX TIME endless encoder, represented by the preview as a wrapping signed 16-bit little-endian counter. Every 16-bit pattern can be represented. They are neither analog level bytes nor the BROWSE counter at byte 34. Preserving them means copying both bytes exactly; no new increment, wrap or inferred semantic reset is appropriate. A proposed strict saved-map contract should accept the pair only when **both are present or both absent**. This paired-presence rule is a new safety restriction, not a check already present in the driver.

The proposed set is the 51 analog offsets plus **22, 26, 27**, totaling 54. This matches the count reported by the refused runtime attempt; the verifier does not read that private file or prove each of its values acceptable.

## Active fields and neutral baseline

The source lists 14 panel buttons and 31 mixer buttons. Three separately defined controls add BROWSE PUSH, MIXER SETTINGS and FX ON/OFF. The extracted bit map is:

| Byte | Source control and bit | Combined active mask |
|---|---|---|
| 20 | CH1 CUE `0x08`, CH2 CUE `0x04`, CH3 CUE `0x02`, CH4 CUE `0x01`, BEAT > `0x10`, BEAT < `0x20`, TAP `0x80` | `0xbf` |
| 21 | SPACE `0x02`, DUB ECHO `0x10`, CRUSH `0x01`, SWEEP `0x08`, NOISE `0x20`, FILTER `0x04`, MASTER CUE `0x40`, LINK CUE `0x80` | `0xff` |
| 22 | FX FREQUENCY LOW `0x02`, FX FREQUENCY MID `0x04`, FX FREQUENCY HI `0x08`, FX ON/OFF `0x01` | `0x0f` |
| 23 | BEAT FX CH 3 `0x20`, BEAT FX CH 1 `0x80`, BEAT FX CH 2 `0x40`, BEAT FX CH 4 `0x10`, BEAT FX MIC `0x01`, BEAT FX CF.A `0x08`, BEAT FX CF.B `0x04`, BEAT FX MASTER `0x02` | `0xff` |
| 24 | FX QUANTIZE `0x04`, AUTO/TAP `0x08`, TALK OVER `0x20`, MIC 2 ON `0x40`, MIC 1 ON `0x80`, MIXER SETTINGS `0x10` | `0xfc` |
| 31 | BROWSE PUSH `0x01` | `0x01` |
| 32 | SOURCE `0x80`, BROWSE `0x40`, TAG LIST `0x20`, PLAYLIST `0x10`, SEARCH `0x08`, MENU `0x04` | `0xfc` |
| 33 | BACK `0x80`, TAG TRACK `0x40`, FILTER/EDIT `0x20`, SHORTCUT `0x10`, LOAD 1 `0x08`, LOAD 2 `0x04`, LOAD 3 `0x02`, LOAD 4 `0x01` | `0xff` |

Neutrality requires bytes 20, 21, 23, 24, 31, 32 and 33 to be zero, plus byte 22's low nibble zero. Unlisted bits in these bytes are protected by exact idle equality, rather than admitted as safe controls. The shared latch at byte **6** is **8** when released and **0** when pressed. X-PAD neutral values are **byte 29 = 255 and byte 30 = 0**; the source's touched position is seven-bit and its touched zone bitmap is nonzero. Neither X-PAD byte is admitted to the saved-map schema. Byte 35 has no new semantic assignment here and remains exactly its pinned idle value (zero).

The new policy should require every other non-overlay byte to equal the pinned idle, apart from the separately validated BROWSE counter and CRC bytes. The full **152 bytes**, including the tail, must participate in validation. Do not drop extra keys just because their current values happen to equal idle. Strict decimal keys, duplicate-key rejection and true integer values 0–255 are additional policy requirements; the upstream parsers do not supply them.

## Saved state versus defaults

The preview's `initial_mixer_frame` starts from idle and puts selected faders, master level, crossfader and all trims at midpoint 512. Those are startup choices, not observations of the current controller. Subsequent `mixer_levels` persistence includes all 51 analog bytes. `mixer_state_store` merges those bytes into the existing map and retains older keys, so saved-file presence does not prove preview-analog provenance.

The driver builds `_mixer` from its pinned 152-byte idle, overlays any in-range numeric saved offsets and then overwrites byte 34 with the separate saved BROWSE counter. It also permits active controls and tail overrides; the synthetic AST tests demonstrate this explicitly. `_mixer_state` coerces and wraps values, collapses canonicalized key collisions and JSON duplicates, and falls back to an empty map on malformed input. The new policy must reject those cases before considering a baseline.

A recognized absent field means the driver's planned frame uses idle for that field. It does **not** establish that the guest currently has idle there. Both missing FX TIME bytes can likewise be synthesized only as a planned default that still must match freshly acquired full frames. Never synthesize the preview's midpoint startup choices or one missing half of the FX TIME pair as current state.

## What preservation can establish

Given a freshly validated neutral frame and unchanged guard conditions, copying every byte except the explicitly intended latch/BROWSE/counter changes introduces **no other byte changes relative to that frame**. This includes the packed-low analog fields, FX selector, FX TIME pair, frame tail and copied CRC bytes; CRC handling by a later transport remains a separate contract. Requiring all button bits released prevents an unchanged held button from being coupled to a newly active latch.

This byte comparison does not prove that the observed baseline still equals the overlay at send time, that no external writer changes it, that a send reaches native code, or that repeated identical values have no native side effects. The host lock cannot establish ownership over all preview/native writers. A future runtime coordinator still needs fresh same-process identity and full-frame observations, bounded age, mixer drift checks, one-use action evidence and its reviewed transport semantics. None of those gates is granted by this source audit.

The preparatory extraction probe initially treated Rust character-literal braces as structural braces and stopped before producing a report. The final extractor handles the pinned character literals; the completed audit above used that corrected extraction. No earlier policy, helper, candidate binary or runtime state changed.
