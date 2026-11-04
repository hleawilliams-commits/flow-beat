# Guarded DeviceLibrary action: bounded ABI review

This is a static review of the exact EP147 action and its two known native callers. It narrows the entry, return and exception requirements for a future passive observer. It does not establish a complete wrapper ABI, hook ownership, successful installation, live receiver identity, permitted navigation or final browser completion.

No native code was executed, function was called, hook was claimed or device was contacted. The earlier instruction-policy and runtime checks were not repeated. The [value-route review](../activation-ownership/README.md) remains the source for the final Source construction and the timer route's ineligibility.

## Exact extent and coverage

The image SHA-256 is `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`. The ELF is little-endian AArch64 ET_EXEC.

| Object | FDE extent | Bytes SHA-256 |
| --- | --- | --- |
| Guarded action | `0x1be7e38`, size `0xbe4` | `42bdacce0bfc9da051348925fb949c9380de4ed4edee881d075784c7b0a59f0d` |
| Selected-row caller | `0x1be8a20`, size `0x98` | `eddbbc28effbea9b3ad5dd0ffbf325f4e9546c2a46cda7e90a32c743a416adf1` |
| Explicit-row caller | `0x1be8ab8`, size `0x198` | `aedaf2aafff768db7fc5117ae92f00b6cb430052c3b2c53d2ca4733c52d97eef` |
| First called provider helper | `0x1c12518`, size `0x250` | `6948b57c41258a42b34f74535a0b0c696948eb02550ddce14874336b73fb09f9` |

All 761 action instructions were decoded and reviewed, including its out-of-line cleanup code. There are 91 direct calls, 19 indirect calls, one RET at `0x1be7f20`, no BR and no direct unconditional branch outside the extent. A conservative control-flow walk follows both conditional successors, treats ordinary calls as returning, and stops at symbol-resolved `_Unwind_Resume` and `std::terminate`. Its ordinary entry reaches 703 instructions; its recorded exception landing roots reach the remaining 58, with no overlap or uncovered instruction. This is static coverage, not evidence that every branch executes or terminates.

## Incoming values and stack

The normal-entry register uses support three logical inputs:

- `x0` is copied to `x20` at `0x1be7e48` and used as the controller.
- The low 32 bits of `x1` are copied to `w21` at `0x1be7e50` and compared as the library-kind value. This does not establish a source-language enum name or signedness.
- `x2` is copied to `x24` at `0x1be7e58`. It remains a borrowed address: the native storage check receives it at `0x1be7ee4`, and the final Source construction reads eight bytes through it at `0x1be81c0`.

The action overwrites `x8` at `0x1be7e40` with its own `sp+0x80` before the first call. It never directly reads the caller's incoming `x8`. The first helper captures that outgoing `x8` at `0x1c1252c` and constructs a 16-byte provider pair there; this is a callee's hidden result destination, not evidence of an incoming aggregate-result argument to the action.

In the action's direct body, `x3/w3` and `x4/w4` are local loads/atomic temporaries, defined before their direct reads under the normal control-flow model. There are no direct operands using `x5..x7`, and no SIMD/FP instructions. All 14 direct `x8` references define outgoing local result addresses. The provider helper likewise has no direct consumption of incoming `x0..x7`: its working values are loaded or defined locally, and its `v0` constant is defined before use.

These are **direct-body facts**, not proof that arbitrary extra register values cannot reach transitive callees. The provider helper calls allocation, construction and virtual release code; the action has 19 indirect calls whose live targets and full contracts are not proved here. No unused-register conclusion authorizes a wrapper to clobber incoming argument registers or SIMD state.

The action reserves one fixed, 16-byte-aligned `0x90` stack frame at entry. It saves `x19..x30`; the local area occupies offsets `0x60..0x8f`. Direct stack accesses and stack-derived local addresses stay within that frame. No direct load from the incoming caller's stack-argument area is present, no dynamic stack adjustment appears, and `x29` is not used to fetch incoming arguments. This does not audit transitive callees or establish a reusable wrapper for arbitrary stack arguments.

## Known native callers and returned values

At `0x1be8a88`, the selected-row caller resolves the row's carrier; it stores the full returned `x0` at `sp+0x38`, restores the saved 32-bit kind into `w1`, and passes `x0=controller, x2=&carrier` to the action at `0x1be8a9c`. The resulting native return address is `0x1be8aa0`.

The explicit-row caller performs its native selection calls first. It then resolves the carrier at `0x1be8be8`, stores it at `sp+0x70`, supplies the same three logical inputs and calls at `0x1be8bfc`. Its native return address is `0x1be8c00`. Both carriers are addresses into their still-live caller frames; neither is a retained row pointer.

The action's only normal epilogue is `0x1be7f08..0x1be7f20`. It restores registers and SP but does not normalize `x0`, `x1` or an FP return register. Guard refusals and other branches can arrive with different residual register values. The final source dispatch at `0x1be81dc` returns to `0x1be81e0`, which branches directly to that epilogue.

Both known callers then immediately restore their frames and RET. They do not inspect the action's returned `x0`, but they also do not overwrite it before returning further up the call chain. This is not proof of a `void` return or of a boolean acceptance result. Source-language return type and wider caller consumption remain unresolved. A passive call/return wrapper must preserve the complete actual machine return; it must not manufacture success from reaching an epilogue or final-service entry.

## Exceptions and unwind requirements

The action has a `zPLR` CIE and a real LSDA at `0x3ec2b36`. Its 192-byte header/call-site region has SHA-256 `c90c14d902a5986542c0f7f136e9877b39cba93b33fce435bd4f74ac9500ccf6`. The table contains 32 entries: 25 have nonzero landing pads, covering 22 distinct landing addresses. Every action index is zero, and no type table is present. These are cleanup mappings, not typed catch handlers.

The CIE's indirect personality chain resolves through `0x3f85e90` to PLT `0x4283e0`, whose relocation names `__gxx_personality_v0`. Cleanup code preserves the exception pointer, destroys local strings or shared-provider state, and reaches the imported `_Unwind_Resume` through `0x426540` at `0x1be8964` and `0x1be89fc`. A separate cleanup path calls imported `std::terminate` through `0x4291d0` at `0x1be8990`.

The final service call's LSDA entry covers exactly `0x1be81dc..0x1be81df` with no local landing pad. An exception from that call can unwind past this action without running its normal epilogue. The selected-row caller has no LSDA; the explicit-row caller has cleanup metadata for earlier local work, but its action call lies in a no-local-landing range. Neither known return site guarantees a normal return.

CFI records the fixed CFA and saved registers, restores the ordinary-return state, then reinstates the frame state for out-of-line code. A wrapper introducing an additional frame or stack-local correlation context needs correct unwind metadata and cleanup across this actual path. Merely calling a C helper before/after the original does not establish exception-safe context removal. No native exception or synthetic wrapper execution occurred in this review.

## Remaining gate

The bounded logical entry shape, complete extent, caller packing and cleanup routes are now inspected. Full wrapper compatibility remains unproved: indirect/transitive contracts, exact caller-visible machine state, wrapper CFI and exception cleanup, LR-sensitive behavior, live hook ownership/island allocation, receiver provenance and native object lifetime remain separate requirements.

A later observer may copy the kind and eight-byte carrier only at an eligible, provenance-checked entry while the native caller still owns that storage, then correlate a nested service entry using values. It must preserve native checks and calls, leave a missing boundary UNKNOWN, and perform no post-original native reads. This review supplies no activation call, saved pointer, USB button or retention/cancellation authority.
