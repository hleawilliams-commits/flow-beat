# Dispatcher entry-only AArch64 ABI

`entry.S` adapts the earlier [four-input observer wrapper](../../direct-knob-observer/abi/) for the five inputs at dispatcher `0x1adeee0`. It exports `az_dispatch_entry`, calls a nonthrowing helper once, restores the native entry registers and tail-branches to `az_dispatch_original`. Native return values and exceptions never pass through observer code afterward. The helper performs no native call or pointer dereference in this ABI fixture.

Required definitions in the same shared object:

```cpp
extern "C" void *az_dispatch_original;
extern "C" void az_dispatch_observe_entry(
    uintptr_t x0, uintptr_t x1, uintptr_t x2,
    uint32_t w3, uint32_t w4, uintptr_t original_lr) noexcept;
```

The helper receives full-width `x0`, `x1`, **`x2`**, low `w3` and `w4`, and original LR in **`x5`**. Incoming `x5` is saved before that substitution and restored before native execution. Reusing the old `mov x4, x30` would overwrite the fifth captured input and give the helper an unrelated sixth argument. The tests discriminate that mistake, separately from corruption of native forwarding.

The native dispatcher is generic and may enqueue an event. These six observation fields are scalar copies; the fixture neither interprets addresses nor proves delivery, Waveform identity, knob provenance, counter ordering or event ownership. The [static boundary evidence](../../dispatcher-boundary/) establishes the inspected machine-code inputs; it does not establish a C return type or successful runtime claim.

## Preservation and lifetime contract

The 320-byte frame keeps SP aligned to 16 bytes and saves/restores full `x0`–`x18`, incoming LR, full `q0`–`q7`, NZCV, FPCR and FPSR. Once restored, `x16` is used for the tail target, as permitted for IP0 by AAPCS64. The SDK island may use IP1/`x17`; this wrapper does not promise to preserve those scratch registers across the complete installed hook. It leaves `x19`–`x29` untouched and relies on the helper's normal AAPCS64 preservation of callee-saved registers and required low halves of `v8`–`v15`. It does not preserve all CPU state, SVE/SME state, arbitrary TLS effects or a different platform ABI.

Full incoming `x3`/`x4` are restored even though the helper accepts their low 32 bits. `x8` is restored for an indirect result, and original SP/LR are restored before the branch so native stack arguments and returns use the original caller's frame. The original target must be non-null, published before entry becomes reachable, and remain mapped along with the wrapper until stopped-process removal. An acquire load obtains it. There is no null-target fallback. Observation disablement must still forward the call.

The helper and original-pointer symbols are hidden within the shared object. The helper must be nonthrowing and obey AAPCS64. Any required `errno` preservation belongs to the helper; this assembly itself does not access TLS. The ABI test replaces the real helper with a recorder that deliberately clobbers all tested caller-saved input registers and FP/control state. Its tests do not bound execution time or validate the real helper's first-use TLS or system calls.

## Actual ARM64 results

[The runner](run-tests.py) uses installed image `azmod-build:18.04`, ID `sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90`, with `--pull=never --network none`. GCC 7.5.0 compiles at `-O2`, with warnings as errors, exceptions and asynchronous unwind tables. It executes the actual ELF assembly on Linux/AArch64. No AZ emulator or physical deck is accessed.

The candidate passes **55 assertions**, with 12 helper calls and 11 counted native C++ calls plus the assembly register target. Coverage includes full input registers, high bits of `x2`, distinct `w3`/`w4`, original LR, SP, indirect-result `x8`, SIMD inputs, FP/control state, eleven integer arguments including stack arguments, a two-register integer return, a 40-byte indirect return with a stack argument, HFA/vector/double returns, four nested native invocations, a native C++ exception and both expected destructors. A live `_Unwind_Backtrace` inside the helper must reach the wrapper and its assembly invoker.

Three mutated assembly controls run the same fixture. The runner checks the exact failure multiset, total assertion count, native/helper call counts and exit status; a crash or unrelated failure is not accepted:

| Control | Expected failures | What it distinguishes |
|---|---:|---|
| Omit `x2`/`x3` restoration | 8 | Native register, integer/large result and nested-call corruption |
| Put LR in `x4` rather than `x5` | 7 | Wrong LR and fifth observation field, including nested records; native values still restore correctly |
| Truncate `x2` before helper capture | 2 | Lost high bits in captured data even when full native `x2` is restored afterward |

[Results and expected failures](test-results.txt) and [source/object/ELF hashes](test-result.json) are retained. Input snapshots, binaries, mutant assembly and raw process outputs stay in ignored `out-history/` directories. No firmware is included.

The emitted object has 46 instructions, one `BL` to the helper and a final `BR x16`, with no `BLR` or return-side observer. Its FDE covers the `0xb8`-byte wrapper: CFA becomes SP+320, saved LR is CFA−168, then LR and CFA rules are restored before tail transfer. [Object disassembly](entry-disassembly.txt), [CFI](entry-cfi.txt) and [wide relocation output](entry-relocations.txt) are generated from this source, not the firmware.

An initial run passed all 55 candidate assertions but stopped at the relocation-name audit because ordinary `readelf -r` truncated `az_dispatch_observe_entry` to `az_dispatch_observe_en`. That run's inputs and outputs remain in its private snapshot. The runner was corrected to `readelf --wide -r`; no assembly or C++ fixture change was needed. The final run passed the audit and all three exact negative controls.

Reproduce from the repository root:

```sh
python3 experiments/mods-shortcut-delivery/dispatcher-observer/abi/run-tests.py
```

This does not execute the SDK island/trampoline or real native dispatcher. Runtime claim acceptance, loaded status, caller/thread provenance, native argument meanings, queued delivery/cancellation and device timing remain separate gates.
