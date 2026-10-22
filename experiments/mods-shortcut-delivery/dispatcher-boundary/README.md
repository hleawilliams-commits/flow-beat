# Encoder dispatch boundaries: static eligibility

October 22, 2026. Three **different function entries pass the pinned installer's instruction policy**. This supplies a possible next passive observation boundary after the refused Waveform handler; it does not show a successful SDK claim, live knob provenance, safe input consumption, or a working direct menu. No hook, runtime observer, emulator connection, physical access, core change or package is included.

The [verifier](verify-static.py) uses the exact private firmware SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925` and upstream commit `3e0c5cb010454228f4ee63226b60d060494eeaae`. [Captured results](static-result.json) contain hashes, addresses, semantic relationships and explicit limits. It checks 59 instruction anchors and executes 66 host assertions using the actual extracted `azm_pcrel_kind`, `azm_ends_fn`, `azm_patch_ok` and community overlap predicate. Firmware data and disassembly dumps are excluded.

## Candidate entries

| Function entry | Exact unwind extent | First four instruction classifications | Relationship |
|---|---|---|---|
| `0x1adea38` | `0x4a8` | position independent, position independent, position independent, relocatable ADRP | Waveform primary vtable `0x2d61ae0`, slot `+0x1b0`; dispatches synchronously or copies a task |
| `0x1adeee0` | `0x588` | position independent, position independent, position independent, relocatable ADRP | Waveform secondary vtable `0x2d61d80`, slot `+0x18`; offset-to-top is `-0x118`; adjusts `this` and follows the same dispatch choices |
| `0x1add3b8` | `0x1c` | four position-independent instructions | Task vtable `0x2d3dee0`, slot `+0x10`; loads captured scalar arguments and tail-dispatches the view's `+0x198` handler |

All three pass `azm_patch_ok` for both one-word and four-word coverage. This **does not authorize the four-word path at runtime**: the existing late-hook rule still requires an available island and refuses the fallback once EP147 is running. No policy is changed. A fixture places the historically refused TBZ into each of the first four positions and checks that both coverage modes reject it. Another fixture checks the short-function return guard. The actual community ownership predicate rejects overlaps at the same address and ±15 bytes and permits exact ±16-byte separation.

The earlier `0x1adeaac` and `0x1adef54` addresses are virtual **call sites inside these functions**, not their entries. Hooking those interior sites would need a different ABI/control-flow argument and is not proposed here. The refused `0x1b3ab60` function entry and SDK-ineligible `0x2d61c78` vtable slot remain unchanged.

## Five inputs before dispatch, four at the target

The task RTTI identifies a lambda inside `HuiAdaptedView<DefaultGuiTaskBoxConf>::didRotateEncoder`, with an encoder reference, `EasyID_Ex<false>` and two integer arguments. Machine-code relationships establish the following without assigning an unproven knob/delta meaning to the values:

| Dispatcher input | Native handling |
|---|---|
| `x0` | Primary view address at `0x1adea38`; at `0x1adeee0`, subtract `0x118` to obtain the primary view address |
| `x1` | Incoming encoder argument is overwritten before use in both inspected dispatcher bodies |
| `x2` | Copies 64 opaque bits to the target's `x1` or task `+0x18` |
| `w3`, `w4` | Copies 32-bit values to target `w2`, `w3` or task `+0x20`, `+0x24` |

The synchronous virtual calls at `0x1adeaac` / `0x1adef54` select the primary view's `+0x198` slot. Their epilogues leave downstream result registers unchanged. The queued branch constructs a different operation and returns after enqueue-related calls; **neither this static result nor a dispatcher return can be treated as accepted, executed or completed input**. The C return type is not established. Preserve the complete native register/return behavior, including any native unwinding, rather than choosing a guessed return type.

Both queued branches construct task vtable `0x2d3dee0`, copy the scalar payload, store the primary view at task `+0x28`, and call `0x237a440` with the task. Its executor `0x1add3b8` reloads those fields, loads the current primary vtable, and tail-branches through `+0x198`. Thus recording only dispatcher entry would not prove when or whether the target handler ran. The captured view address is not proof of retained view lifetime. Task sequence/liveness bookkeeping is not evidence for ordering of the mixer encoder counter.

## Ownership audit and next passive observation

A conservative audit of all 95 pinned `src/azmod` C/header files finds no hexadecimal literal within the 16-byte overlap range of these three entries, including firmware address-table values. The existing Browse handler at `0x1b10860` is separate and remains owned by the existing playlist machinery. This source audit cannot establish the current emulator's runtime hook table or discover addresses computed by another mod. A future observer must use the normal SDK claim and stop on refusal; no takeover, second owner, raw patch or retry around a guard is proposed.

Prefer **`0x1adeee0` for the next entry-only passive observer**. It has a concrete Waveform secondary-vtable relationship and preserves the earliest inspected dispatch inputs. Record `x0`, `x1`, `x2`, `w3`, `w4`, original LR, thread identity and an observer sequence as scalar values. No native pointer read is needed for that first record. The earlier four-field observer record is insufficient: the dispatcher consumes a fifth integer register, even though a register-transparent assembly tail wrapper can preserve it. The assembly ABI and bounded observer must be tested for the new capture shape before installation.

This dispatcher is shared by other views. A log record does not alone identify Waveform or a physical knob, and the dispatch predicate has not been observed live. Controlled emulator input/visibility, callback thread and caller lineage still need correlation. No post-return native reads should be added. The queued executor is a later, separate observation option; it takes a task object in `x0`, so blindly treating it as the four-argument Waveform handler would be wrong. Reading its payload would need a justified live-task ABI/lifetime gate and must occur while the native invocation owns that object.

Actual counter ordering, queued-event ownership, view cancellation, modal precedence, held-button release, focus handback and physical timing remain open. These anchors do not reopen stems staging, fix filter quarantine, or change the published shortcut.

## Reproduce

Use Python with pyelftools and Capstone, plus a host C compiler:

```sh
python3 experiments/mods-shortcut-delivery/dispatcher-boundary/verify-static.py \
  --elf /private/path/EP147 --upstream /path/to/xdj-az-mods
```

Only private input reads and a temporary host C fixture compilation/execution occur. The ELF and upstream checkout are never written. This run's host assertions are instruction-policy and ownership-range checks, not native ABI or runtime tests.
