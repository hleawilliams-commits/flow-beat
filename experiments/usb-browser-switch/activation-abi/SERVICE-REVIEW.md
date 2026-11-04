# Named source service: complete static extent and exception boundary

This review narrows the machine ABI of the previously identified
`gui::source_select::BrowseSourceSelect::setSource(Source)` at
`0x20a1550`. It does **not** approve a typed wrapper, a hook, a native
activation call, or a borrowed-object lifetime. The full wrapper gate remains
**CLOSED**.

The [preceding ownership review](../activation-ownership/README.md) supplies
the named interface and value-route evidence. Here the complete service extent,
its unwind metadata and the known final-dispatch continuation were read from
the private ELF. Nothing was executed from that ELF. No native call, hook
claim, runtime access, device operation, or replay of the previous 87-anchor /
20-policy-check suite occurred. Firmware bytes and private paths are omitted.

## Exact extent and identity

The input is the same ELF64 little-endian AArch64 ET_EXEC image,
SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`,
tag `ep147-9e9454e357389ece`.

| Region | Extent | SHA-256 |
|---|---|---|
| Complete service | `0x20a1550..0x20a2574`, end exclusive; `0x1024` bytes | `d15679efc5e257fe31e7efcc89866a0d379828fc9cce201e5c420c1226c2b79d` |
| Service FDE | `0x3c546c8`; length field 160, total 164 bytes | `0ca15b0e1ebd17d41819e52c57682e52571df71c12697bf683bf6f364c064a72` |
| Shared CIE | `0x38ff588`; length field 24, total 28 bytes | `bf4098afb666e641cd42f2ab2d43c80828447519107aefcf1508efb6bba15baa` |
| Service LSDA | `0x3f16392..0x3f1646c`, end exclusive; 218 bytes | `2e14f4e4b6b57b4f67c2f27b8334ef8750659c308c843fb5f7c5838eb075c53f` |

The FDE extent decodes completely into **1,033 four-byte instructions**:
**153 direct calls, 10 indirect calls and one normal RET instruction**.
There is no BR instruction or plain unconditional B leaving this extent.
These are static instruction counts, not executed-path counts. In particular,
the single RET does not mean every path returns normally.

## Incoming register and stack use

At the known final interface call, the caller constructs the 12-byte Source
on its own stack, loads bytes 0–7 into `x1`, loads bytes 8–11 into `w2`,
sets `x0` to the interface receiver and calls the vtable target. This agrees
with the complete service body's explicit uses:

| Entry value | Explicit service-body evidence |
|---|---|
| `x0` | Copied into `x19` at `0x20a1560`; subsequent service fields and the queued raw receiver capture use that saved value. |
| `x1` | Low 32 bits become the discriminator in `w24` at `0x20a1578`; the upper 32 bits become the MediaID word in `x23` at `0x20a157c`. Full `x1` is also stored in local Source copies at `0x20a1588` and `0x20a1590`. |
| `x2` | Its low 32 bits replace all of `w3` at `0x20a1564`, supplying Source bytes 8–11. The low two bytes are separately extracted at `0x20a156c/1570`. The complete four-byte value is retained in local Source copies. The body does not use incoming `x2[63:32]`. Padding is not thereby promoted to MediaID identity. |
| `x3` | Incoming contents are overwritten by the local load at `0x20a1558`. The following full-width `bfxil w3,w2,#0,#32` replaces that loaded word before it contributes to the source value. |
| `x4` | All explicit uses follow a local definition: exclusive-store status outputs or immediate assignments. The first occurrence is the status destination at `0x20a1ac0`. |
| `x5..x7` | No explicit occurrence in the complete body. This alone does not establish a transitive helper ABI. |
| `x8` | All 21 explicit occurrences assign local addresses for outgoing aggregate-result calls. The first is `0x20a1960`; there is no explicit read of the incoming value or incoming result-buffer pointer. |
| SIMD/FP | Only `d0`, `d1` and `d8` appear explicitly. `d0` is first assigned a constant; `d1` is loaded from local timing storage. Incoming `d8` is saved before local timing arithmetic and restored on the relevant normal paths. The body provides no incoming FP/SIMD Source argument use. |

The prologue at `0x20a1550` allocates **0x110 bytes**, saves frame/link
registers and then saves `x19..x28`. The only explicit changes to SP are
that prologue and the restoring instruction at `0x20a1758`.
All 83 SP-based memory operands belong to this frame; the largest positive
offset is `0x108` for an eight-byte access, ending exactly at the original
SP. No explicit access reads an incoming stack-argument slot at or above that
original SP. Local pointers passed to callees are not incoming arguments.

The early load at frame offset `0x88` is therefore a local scratch read,
not evidence of a fourth stack argument: the next relevant instruction replaces
its entire 32-bit value from `w2`. Do not infer a hidden argument from this
load or from the outgoing `x8` assignments.

These findings concern explicit instructions in this extent. They do not prove
every opaque/dynamically selected helper's full contract or authorize dropping
otherwise live machine state before forwarding. The initial thread-id helper
tail-branches to the imported `pthread_self`; the next singleton helper
loads a global pointer. Those observations explain the initial thread comparison,
not a service-retention guarantee.

## Normal return and the known caller

The only normal epilogue is `0x20a1744..0x20a175c`: restore saved general
registers, restore SP/link and RET. It supplies no normalized success/status
value. Different routes can leave different incidental values from loads or
callee returns in volatile return registers. A service return is not evidence
of a switch, final browser readiness, accepted navigation, or listener delivery.

The final interface call is `0x1be81dc`, with LR **`0x1be81e0`**.
That continuation immediately branches to `0x1be7f08`; the target restores
the guarded action's frame and returns at `0x1be7f20`. There is no intervening
branch, store, or other consumption of the service's scalar/SIMD return
registers. This establishes non-consumption at this specific continuation,
not the complete source-level return type of every caller or implementation.
The caller's epilogue can still pass incidental volatile values onward.

The service has two materially different routes already identified in the
ownership review. The direct route invokes listener slots at
`0x20a1878` and `0x20a18f4`, with conditional service/listener rereads
afterward. The queued route copies the scalar Source and a **raw service
pointer**, then enqueues work. The full extent review adds no owner pin,
cancellation guarantee, or later completion event to either route.

## Exception and unwind obligations

The CIE uses augmentation `zPLR`, return-address register 30, and
PC-relative signed-four-byte encodings for FDE/LSDA references. Its personality
encoding is `0x9b` (indirect, PC-relative signed four-byte). The encoded
reference resolves through cell `0x3f85e90` to PLT entry `0x4283e0`;
that entry's dynamic relocation at `0x3f856f0` names
`__gxx_personality_v0`. The FDE has **32 decoded CFI rows**, including
the 0x110-byte CFA adjustment, saved `x19..x30`, and conditional saved
`d8` rules at frame offset `0x60`.

The LSDA is fully bounded by its own decoded table: omitted LPStart
(`0xff`, so offsets are relative to the function), omitted type table
(`0xff`), ULEB128 call-site encoding (`0x01`), five header bytes and
a **213-byte call-site table** ending at `0x3f1646c`. It contains:

- **36 call-site rows**, all with action index zero;
- **32 rows with a nonzero landing pad**, naming **28 distinct landing PCs**;
- four rows with no landing pad, beginning at `0x20a1598`,
  `0x20a16c4`, `0x20a18f4` and `0x20a241c`;
- no action chain or type-filter table after the call-site table.

Nonzero landing/action-zero rows describe cleanup paths, not typed catch
handlers. The table does not establish that every call is nonthrowing; nor
does a zero/absent landing justify swallowing an exception in a wrapper.

Concrete boundaries include the allocation virtual call at `0x20a15f8`
leading to cleanup `0x20a2530`, the wake/notification virtual call at
`0x20a171c` leading to `0x20a2548`, and the first listener call at
`0x20a1878` leading to `0x20a24bc`. The second listener call at
`0x20a18f4` has a zero landing pad. Calls and their cleanups must not be
collapsed into an ordinary returned receipt.

The complete EH tail contains three imported **`_Unwind_Resume`** calls,
at `0x20a241c`, `0x20a24a0` and `0x20a255c`, all targeting
PLT `0x426540`. Another cleanup path calls imported **`std::terminate`**
at `0x20a245c` through `0x4291d0`. The two bad-function calls at
`0x20a232c` and `0x20a23e4` target imported
`std::__throw_bad_function_call` through `0x4271c0`.
These symbol identities come from the corresponding ELF relocations.
Unwind-resume/termination paths are not normal returns to the known LR.

## What remains before any wrapper or observation

The value packing, full extent and EH inventory are substantially narrower
than an approved wrapper. A later implementation still needs a reviewed
forwarding mechanism that preserves all required entry/return state and
native exception propagation; correct unwind metadata for any trampoline or
wrapper frame; cleanup of any thread-local correlation scope on unwinding;
and an explicit policy for missing returns rather than a fabricated success.

A logger or observer must not add a throwing operation, replace the native
exception path, infer a return type solely from an unused result, or perform
post-original native reads. Live receiver provenance, existing hook ownership,
successful installation, timing and event ownership remain unobserved here.
Most importantly, none of this proves service/view retention through reentry,
timer cancellation, actual USB/Local Library choice, missing-media behavior,
final list readiness, or native switch completion. The activation and full
wrapper gates remain closed.
