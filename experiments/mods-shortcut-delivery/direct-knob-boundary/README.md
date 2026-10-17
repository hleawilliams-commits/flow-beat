# Direct-menu knob boundary research

Source and static firmware review only, October 17, 2026. This narrows one unresolved part of the [direct-menu design](../DIRECT-MENU-DESIGN.md): consuming menu knob turns without freezing the native absolute-position byte. There is no new hook, menu implementation, emulator installation or physical-device test here.

## Existing mechanism and why it matters

Pinned upstream `3e0c5cb010454228f4ee63226b60d060494eeaae` already has two different input mechanisms:

- `src/azmod/bus_interpose.c`, `mixer_browse_knob`: while the menu owns input, holds mixer byte 34 at its anchor; on release, requests native SOURCE-row reselection. That handback depends on SOURCE and is unsuitable for a direct menu over the waveform view. The existing code explicitly rejects carrying a counter offset because it produced erratic native movement.
- `src/azmod/playlist_editor.c`, `knobturn_hook`: during playlist reordering, consumes a native Browse knob event after the raw counter has continued through the bus. The bus code documents that this avoids an accumulated movement on release. This existing hook is already claimed; a second hook must not replace it.

The second mechanism is a useful precedent for research. It does not establish a complete ownership contract for the waveform view or queued events.

## Exact image and ABI checks

[The verifier](verify-static.py) reads the owner's private ELF and pinned upstream address table, then emits only addresses, hashes and limitations. [The captured result](static-result.json) passes its assertions. Firmware bytes, library contents and disassembly dumps are excluded.

Source symbol names refer to an older address table. For the exact current firmware, `EP_19ac568` maps to Browse handler `0x1b10860`, not the hexadecimal suffix in its name. The verified Browse primary vtable at `0x2d4ea68` has that handler at slot `+0x198`. The Waveform RTTI identifies primary vtable `0x2d61ae0`; its known paint slot validates the table, and the same `+0x198` slot points to `0x1b3ab60`.

That Waveform candidate has a verified unwind extent of `0x84` bytes and preserves **four integer arguments**, including incoming `w3`. The existing three-argument Browse wrapper therefore cannot simply be copied. Two generic dispatch call sites use this virtual slot, at `0x1adeaac` and `0x1adef54`. The static check validates the candidate's forwarding instructions and the call sites; it does not prove the meaning of every argument or the return-value contract.

## Next gate

Before consuming any event, establish an observation-only ABI and lifetime contract at a live native entry. Prove that ordinary knob turns on the waveform screen reach this boundary, identify all arguments and return semantics, and show where the absolute counter is updated relative to event dispatch. The observer must forward original arguments and results exactly once and must not retain native pointers for later dereference.

Then test input ownership across queued turns, button release, navigation, modal precedence, disable, watchdog fail-open and menu close. Direct entry must not pin a SOURCE row on release. Until these checks exist, the published MODS Shortcut continues to use its documented SOURCE route. These static anchors are not an installable fix for its frame-filter quarantine.

To reproduce with private local inputs:

```sh
python3 experiments/mods-shortcut-delivery/direct-knob-boundary/verify-static.py --elf /private/path/EP147 --upstream /path/to/xdj-az-mods
```

Dependencies: Python, pyelftools and Capstone. The verifier rejects other firmware hashes and performs no mutation.
