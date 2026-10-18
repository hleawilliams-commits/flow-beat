# Entry-only AArch64 observer ABI

`entry.S` exports `az_knob_entry`. It calls the externally supplied nonthrowing helper once, restores the native entry state, and tail-branches to `az_knob_original`. It does not call native code from the helper, interpret a native result, catch a native exception, consume an input event, or dereference any native object. This is a Linux AArch64 ELF wrapper, not a claim about the firmware handler's still-unknown result type or argument meanings.

Required definitions in the same shared object:

```cpp
extern "C" void *az_knob_original;
extern "C" void az_knob_observe_entry(
    uintptr_t x0, uintptr_t x1, uint32_t w2, uint32_t w3,
    uintptr_t original_lr) noexcept;
```

The helper and target-pointer symbols are hidden to bind inside this shared object. The hook installer must publish a valid, non-null original target before redirecting execution to this entry. The wrapper loads that pointer with `LDAR`. It has no null-target fallback and must not become reachable during partially completed installation. The original target and wrapper must remain mapped until a stopped-process removal; observation disablement does not remove the forwarding path.

## Preserved state and boundaries

The 320-byte frame retains 16-byte stack alignment. It saves and restores `x0`–`x18`, the incoming LR, full `q0`–`q7`, and NZCV/FPCR/FPSR. After restoration, `x16` becomes the tail-transfer address. AAPCS64 permits that IP0 scratch use. The SDK branch island itself can use IP1/`x17`, so this is not a promise to preserve IP0/IP1 across the entire installed hook. The wrapper leaves `x19`–`x29` alone and relies on the helper's ordinary AAPCS64 preservation of callee-saved registers and the required halves of `v8`–`v15`.

The helper receives only the low 32 bits of arguments 2 and 3, but the complete incoming `x2`/`x3` registers are restored for the native target. Original `x8` is restored for an indirect result. Original SP and LR are restored before the tail branch, so native stack arguments and return paths use the original caller's frame. Return registers never pass through observer code afterward. This does not cover SVE/SME calling conventions, arm64e pointer authentication or a different platform ABI.

A helper must be nonthrowing and obey the stated ABI. Register preservation does not preserve arbitrary TLS state: a helper using libc should separately preserve `errno` when required. The current observer uses first-entry TLS initialization and a `gettid` syscall and does not explicitly save/restore `errno`. No zero-first-call-syscall/allocation or full-native-state transparency claim is made. Its first-use TLS, allocation and execution-time properties are outside this assembly contract.

## Unwind audit

The emitted object has 46 instructions, exactly one `BL` to the helper, and one final `BR x16`; there is no `BLR` or return-side observer. The load of the target is `LDAR` after SP restoration. The [disassembly](entry-disassembly.txt), [relocations](entry-relocations.txt) and [CFI](entry-cfi.txt) are from the actual compiled object, not firmware disassembly.

The FDE covers the entire `0xb8`-byte wrapper. After the first instruction, CFA is SP+320. Once LR is stored, it is described at CFA−168. When LR is restored, its CFI register rule is restored; after SP restoration, CFA returns to SP+0. The helper is called only within the covered saved-LR region. The fixture also obtains a live `_Unwind_Backtrace` inside the helper and checks that it reaches both this wrapper and the assembly invoker above it. Native exceptions occur after the wrapper frame is gone and unwind directly through the native target and original caller.

## Actual instruction tests

Run from the repository root:

```sh
python3 experiments/mods-shortcut-delivery/direct-knob-observer/abi/run-tests.py
```

The runner requires the already installed ARM64 image recorded in [test-result.json](test-result.json); it never pulls an image and uses `--network none`. It compiles and executes the ELF assembly under Linux/AArch64 with GCC 7.5.0, `-O2`, warnings-as-errors, exceptions and asynchronous unwind tables. Build artifacts stay under ignored `out-history/` and are not source-control deliverables. No AZ emulator or physical device is accessed.

The recorded candidate result is **54 passed, 0 failed**. The fixture helper deliberately clobbers `x0`–`x18`, `q0`–`q7`, flags and FP state after recording its arguments. Tests verify full-width registers (except documented `x16` scratch), SP/LR, original `x8`, vector inputs, and flags. Correctly declared assembly entry thunks then exercise eleven integer arguments including stack arguments, a two-register integer result, a 40-byte indirect result with a stack argument, a four-double homogeneous aggregate, a 128-bit vector, a scalar double, four nested native calls, and a native C++ exception with both expected destructors running. Native execution counts distinguish one helper observation per forwarded invocation. There is no persistent TLS context spanning native execution to restore.

The negative control deliberately omits restoration of `x2`/`x3`. The same actual assembly fixture rejects it with **eight failures**, including native integer/large-result and nested-call corruption. Its failures are expected and retained in [test-results.txt](test-results.txt). The production wrapper is unchanged by that control.

These tests do not execute the SDK island/trampoline, establish handler provenance, prove input ownership or cancellation, infer the actual native return type, or validate emulator/device performance. Those are separate gates.

## Runner setup corrections

Initial candidate executions passed 54/0, but the runner could not retrieve audit files from temporary bind mounts under macOS `/var/folders` or `/private/tmp`; the Docker daemon did not share those host directories. A stdout-transfer attempt also stopped because the minimal build image has no Python interpreter. No assembly change was made for either issue. The final runner uses an explicitly ignored workspace output directory and host Python for the audit. The earlier complete process outputs were not retained as files; this paragraph records observed setup failures rather than representing a reconstructed complete log. The final run's process output, disassembly, unwind data, relocations and source/binary hashes are retained.
