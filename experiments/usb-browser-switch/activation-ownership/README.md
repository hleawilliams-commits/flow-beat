# Native USB activation: value route found, owner still unresolved

October 22, 2026. Static follow-up to the [intent policy](../activation-contract/README.md). The final DeviceLibrary action can now be traced to a named **`BrowseSourceSelect::setSource(Source)`** interface: it receives a small value containing the native source discriminator and MediaID, rather than retaining the library row pointer. This narrows the argument question. It does **not** establish a safe place for a custom activation call, final browser readiness, or an owner that survives navigation/reentrant callbacks.

No native function was called, no emulator or physical device was contacted, and no hook, adapter, USB button or release was added. The preceding V2/V3/V4 observations and intent policy remain unchanged.

## Exact evidence

The [verifier](verify-static.py) reads the private EP147 image with SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`, firmware tag `ep147-9e9454e357389ece`. It checks ELF64/little-endian/AArch64/ET_EXEC identity, **87 instruction anchors**, named RTTI/slot relations and the actual executable-segment facts. FDE entries independently establish the proposed entries and extents: `0x1be7e38`/`0xbe4`, `0x20a1550`/`0x1024`, and timer no-op `0x1b2e538`/`4`. It also extracts the instruction predicates from pinned upstream `3e0c5cb010454228f4ee63226b60d060494eeaae` and executes **20 host assertions**, including fixture-only rejection controls. [Result and source hashes](static-validation.json). This executes host policy code, not the firmware. No firmware dump is included.

```sh
python3 verify-static.py --elf /private/path/EP147 \
  --upstream /path/to/xdj-az-mods --output static-validation.json
```

Capstone and pyelftools are required. The upstream working tree is never read as source: `git show` supplies the pinned policy files.

## Library row to final source value

| Boundary | Established argument or behavior |
|---|---|
| `0x1be8a70..0x1be8a9c` | Selected library rows have stride `0x20`. The caller saves row kind `+0x14`, calls resolver `0x1be5798`, stores its `x0` result as an eight-byte stack value, then supplies `x0=controller`, `w1=kind`, `x2=&value` to `0x1be7e38`. |
| `0x1be8bd0..0x1be8bfc` | The explicit-row entry selects the row and uses the same resolver and guarded action. This is another native call site, not authorization to fabricate or reuse a row index. |
| `0x1be5798` | Resolves the row's source descriptor at `+8` through a native provider. Kinds 0/1 use provider slot `+0x10`; kind 2 uses `+0x18`; kinds 3–6 use `+0x40` with index 0–3. Unsupported kinds return zero. The returned carrier is compared elsewhere by its 32-bit word and bytes 4/5 (`0x1be5a0c..0x1be5a34`), consistent with the existing MediaID observations. Upper carrier bytes are not treated as identity. |
| `0x1be31a8`, called at `0x1be908c` | Native row construction maps `MediaID.byte4 & 0x1f` to the row kind: 2→0, 3→1, 4→2, 6→4, 7→6, 8→3, 11→5. Other values take the diagnostic/default path. These are numeric facts; this investigation does not assign USB-port names or library names to the enum values. |
| `0x1be7ed4..0x1be7ef4` | The action runs a native storage check through controller `+0x58`, slot `+0xd0`, passing the supplied MediaID address. Later branches handle library-kind/service checks. It is not an unconditional forwarding stub. |
| `0x1be81b0..0x1be81dc` | On the final dispatch path, obtains controller `+0x48`, loads its slot `+0x10`, constructs a source value on the stack with requested discriminator 1 and the full eight-byte carrier, then invokes the slot with `x1=bytes[0..7]`, `w2=bytes[8..11]`. |

The constructor at **`0x1b00160`** writes a 32-bit discriminator at offset 0 and the eight-byte carrier at offset 4. It validates the requested discriminator 1 using the MediaID's low byte and `byte4 & 0x1f`; invalid combinations can normalize the discriminator to zero. A custom call must not assume that requesting 1 means a valid media source was accepted. Preserve the native validation and meaningful identity fields; do not equate carrier padding with MediaID identity.

The exact RTTI identifies **`gui::source_select::BrowseSourceSelect`**, with `IBrowseSourceSelect` as its zero-offset base. Its vtable `0x317de60`, slot `+0x10`, points to **`0x20a1550`**. The associated queued-lambda name explicitly identifies **`setSource(gui::source_select::Source)`**. Together with the caller's register packing, this resolves the final value's type and concrete target. It does not prove that a future live controller's registry lookup will return this same implementation: runtime receiver verification remains required.

Calling the final service directly would also bypass the guarded DeviceLibrary action above. The named service is therefore a useful research boundary, not an approved replacement for the native row/availability path. USB descriptor uniqueness, actual Local Library choice, ejection/missing-media behavior and the resulting MediaChangeEvent still require correlation.

## The value is copied; the owning object is still borrowed

`setSource` compares the current pthread with JUCE's message-thread singleton at `0x20a1598..0x20a15b0`.

- On the direct path, it compares/stores source fields in the service at `+0x24..+0x2d`, then invokes listeners. It may read the service's listener count again after a callback (`0x20a1884`, `0x20a1900`). These reads depend on remaining listeners; this is not a claim that every callback return performs one. Reentrant destruction is still a concern to resolve.
- On the queued path, it copies the source value into a task and also captures the **raw service pointer** at task `+0x18` (`0x20a1660..0x20a1674`). Task execute `0x20a2578` obtains that pointer and reads the service. The scalar value no longer borrows the caller's stack, but the task's service-pointer lifetime is not proved by that copy. No custom task is created here.

The SourceSelectView timer has the same unresolved distinction. Its task captures timer id at `+0x18` and a **raw timer pointer at `+0x20`**. The queued execute at `0x1ae8a40` can read the timer again after the listener returns (`0x1ae8b54`), just as the already identified direct route can at `0x1ae90b4`. These rereads are conditional on listener-loop progress. The task destructor at `0x1ae86c8` decrements a **global task counter** (`0x3fe9708`); that decrement is not a SourceSelectView retain/release. No independent view pin is acquired in the inspected capture or listener loop.

This does not prove a native lifetime bug. Native task-box teardown, factory ownership and permissible reentrancy may provide a broader contract. That contract has not been established for a new operation inside the callback. Do not add a reference-count write, carry a borrowed pointer to a later callback, or infer retention from the presence of a queue/global counter.

## Eligible instruction sequences are only a first gate

The SourceSelectView timer-listener slot **`0x2d5b030`** lies in the same executable `PT_LOAD` as the refused Waveform slot. The exact SDK `sdk_vslot.c` filter excludes `PF_X`. **Do not attempt this unchanged `claim_vslot` route.** The timer's original is a one-instruction RET; reading neighboring instructions does not turn it into a four-instruction function suitable for an ordinary inline wrapper. No SDK or core policy was changed.

Two larger ordinary entries provide a narrower next research route:

| Entry | What the host instruction-policy check establishes |
|---|---|
| Guarded DeviceLibrary action `0x1be7e38` | Its actual four opening instructions are position independent and pass both one-word and four-word `azm_patch_ok` checks. |
| Named `setSource` entry `0x20a1550` | Its actual four opening instructions also pass both widths. |

The 20 host checks include inserting the known refused TBZ instruction into each of the four positions in a temporary integer fixture: both patch widths must then reject it. The firmware stays unchanged. These results establish **instruction-policy eligibility only**. They do not establish exclusive runtime ownership, island availability, successful registration, a complete wrapper ABI, timing or input behavior. No claim was attempted. No reference to these addresses was found in the pinned core source by literal search; dynamically resolved ownership still cannot be excluded that way.

The next implementable step is a separately reviewed **entry-only observation** plan at these ordinary boundaries. First audit the full ABI, including return/exception behavior and any hidden arguments, and check hook ownership. At the guarded-action entry, copy the eight-byte supplied value while the native caller's stack is live, together with kind, original caller and an opaque controller identity. A synchronous stack-local context could then correlate the supplied Source registers at `setSource` entry. The exact native action return addresses are `0x1be8aa0` and `0x1be8c00`; the final interface dispatch return address is `0x1be81e0`. Gate and test those contexts before observation. Preserve every original call and argument, perform no post-original native reads, and leave missing boundaries UNKNOWN.

Such an observer could establish whether actual USB1/USB2 Local Library actions take this route and preserve the requested MediaID through native checks. It would still not activate anything, prove timer/service retention, or report switch completion. The activation adapter remains gated on a native owning/serialization contract or a different live scope explicitly permitting navigation. The value-only intent policy can remain unchanged while that proof is developed.
