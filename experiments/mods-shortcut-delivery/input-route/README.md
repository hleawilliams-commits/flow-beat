# Native encoder route — static evidence only

The exact firmware contains a separate `HuiAdaptedViewDispatcher` encoder
observer at **0x1b01cf8**. Its native task route can call a Waveform handler directly
through virtual slot `+0x198`, bypassing the generic wrappers at `0x1adea38` and
`0x1adeee0`. This is a discriminating alternative for the next passive observer;
it does **not** explain the earlier accepted-hook/zero-entry runtime result.
No hook, emulator access, device access or firmware execution occurred here.

## Verified route

| Stage | Exact static evidence |
| --- | --- |
| Encoder ingestion | `0x235c270` reads a delta, adds it to the encoder's counter at `+0xd8`, and appends delta/cumulative fields at event offsets `+0`/`+4`. This identifies native HUI semantics, not the physical packet's byte-34 ordering. |
| Observer notification | `HuiEncoder` primary vptr `0x3216938`, slot `+0x10`, resolves to `0x2360f80`. The nonzero-delta path sends `x0=observer`, `x1=encoder+0xc0`, `x2=full-width ID`, `w3=delta`, `w4=cumulative` through observer slot `+0x18`. A distinct `UiOperationDetector` callback has a special branch. |
| Dispatcher observer | RTTI identifies `HuiAdaptedViewDispatcher<DefaultGuiTaskBoxConf>`. Its secondary vptr `0x2d453d0`, offset-to-top `-0x118`, slot `+0x18` resolves to `0x1b01cf8`. The derived `gui::ViewTransitionManager` secondary vptr `0x2d45740` uses the same entry. Constructor `0x1afe1f0` installs these base and derived vptrs at object `+0x118`. |
| Queued capture | `0x1b01cf8` adjusts incoming `x0` by `-0x118` and copies primary dispatcher, ID, delta and cumulative to task offsets `+0x18`, `+0x20`, `+0x28`, `+0x2c`. Task vptr `0x2d444d8` and RTTI identify `HuiAdaptedViewDispatcher::didRotateEncoder`; execute slot `+0x10` resolves to `0x1b029c0`. Entry does not establish successful enqueue or eventual execution. |
| Handler dispatch | Executor `0x1b029c0` compares dispatcher field `+0x19c` with helper `0x1b02858`'s return, then tries child pointer `+0x1a0`, child pointer `+0x150`, and the dispatcher itself through slot `+0x198`. Null children/default handler are skipped; a child's nonzero low-byte result stops fallback. ID is forwarded in `x1`, delta/cumulative in `w2`/`w3`. Current child identities are unknown. |
| Waveform destination | Waveform primary vptr `0x2d61ae0` slot `+0x198` resolves to `0x1b3ab60`. Its primary `+0x1b0` and secondary `+0x18` wrappers remain `0x1adea38`/`0x1adeee0`; this separate dispatcher can reach its handler without entering either wrapper. |

The current live registration recipient for `rotarySelector`, active dispatcher,
and child identities remain **unproven**. The constructor/vtable/call-chain
anchors establish an available route, not a registration that was observed at
runtime. Counter equality, opaque ID values and missing logs are insufficient
to fill that gap. Native task submission through task-box slot `+0x30` does not establish
asynchronous execution, a particular thread, or delayed execution. Immediate
execution by the owner remains possible. Queue cancellation, object lifetime
and final handling remain unproven. The field/helper comparison is an integer equality gate; its generation semantics
and lifetime contract are not proven here. It establishes no cancellation epoch.

## Next observation boundary and policy

Prefer entry-only, scalar-only observation at **0x1b01cf8**: preserve all native
registers/return classes and capture `x0`, `x1`, full `x2`, `w3`, `w4` and the
original LR. The existing five-input ABI tail wrapper is structurally suitable;
a new claim/binding would still need independent checks. Keep addresses opaque:
no native pointer dereference or post-return read is necessary. Do not interpret
the native return as accepted/completed input. Because the upstream notification
can tail-branch, LR need not identify the immediate virtual dispatch instruction.

The enclosing FDE starts exactly at `0x1b01cf8` and spans `0x2e8` bytes. The first
four instructions save FP/LR, establish FP, and save x19–x22. Actual extracted
`azm_pcrel_kind` returns zero for all four; actual `azm_patch_ok` accepts both
one-word and four-word modes. The pinned 95-file native-source numeric-literal
audit finds no overlapping hook address. This proves conditional static
eligibility only: an actual SDK claim must still satisfy live ownership, code
range/init state, island availability and late-install constraints. No claim was
attempted. Existing occupied hooks and claim policies must remain unchanged.

`0x2360f80` is an earlier native notification boundary but accepts an event
**pointer**, not five scalar event arguments. Its record's lifetime and caller
ownership have not been established for an external observer. Do not reuse this
scalar ABI or add event-pointer reads there based only on these static offsets.
The scalar dispatcher entry is the narrower next step.

Pair any later native observation with the independently tested
[SDK input-stage evidence](../input-stage/): host counter mutation does not prove native delivery. Core menu/focus
freezing, earlier EATEN skipping a normal observer, and later frame rewrites
remain separate possible blind spots. Mixer EATEN stops that filter chain; it
does not drop the entire frame. Do not repeat the unchanged secondary-dispatcher trial.

## Reproduce

From repository root, using the existing private interpreter and exact ELF:

```sh
../../../work/qemu-python/bin/python \
  experiments/mods-shortcut-delivery/input-route/verify-static.py \
  --elf ../../../work/picks-native-probe/EP147 \
  --upstream ../../../work/xdj-az-mods
```

`static-result.json` records **88 instruction anchors, 13 data words, 3 RTTI
anchors, 6 discriminating semantic controls and 22 actual policy/overlap
assertions**. Controls distinguish swapped delta/cumulative captures, wrong
task offsets, wrapper-versus-handler slot, reversed child-result branch and
wrong observer identity. Policy controls reject TBZ in each opening position,
exercise RET overwrite length, and enforce ±15-byte overlap versus ±16-byte
separation. The script reads policy from pinned Git objects and compiles only
the extracted host predicates; it does not execute firmware. Firmware/source
hashes and function hashes are in the result. Raw disassembly and scratch
scripts stay in ignored `private/`.

An independent execution reproduced the final verifier result byte-for-byte
(SHA-256 `5ebdee5b9261a6cfc1fbf1577f05d8d1ff80b7be7d194a291fc4683354058f48`).
Review narrowed the initially proposed “generation” label to the proved opaque
integer equality gate and added exact argument reload anchors after each child
callback. Those refinements preserve the unproven lifetime/cancellation limits.
