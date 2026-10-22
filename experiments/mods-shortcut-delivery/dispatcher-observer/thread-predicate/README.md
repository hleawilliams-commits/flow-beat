# Dispatcher owner-thread predicate

The exact firmware's encoder dispatcher chooses queued versus synchronous dispatch by comparing `pthread_self()` with a native owner's stored thread identity. It does **not** compare Linux PID and TID. The owner's live identity and the thread that delivers panel input have not been observed.

The [read-only verifier](verify-static.py) checks the private firmware SHA-256, ELF64/little-endian/AArch64 format, exact instruction blocks, a vtable value and the `pthread_self` PLT relocation. [Sanitized results](static-result.json) retain hashes and relationships. It performs no native calls or process-memory reads, accesses no emulator/device, and does not repeat hook-policy checks.

Both encoder dispatchers use the shared taskbox at `0x3fe8f88`, calling its virtual slot `+0x28`. The adjusted dispatcher's initialization code stores vptr `0x321b108`; that table's slot resolves to predicate `0x1e61cd8`. The predicate calls wrapper `0x249b0f0`, which tail-branches to PLT `0x425670`. Its GOT cell `0x3f84038` has an `R_AARCH64_JUMP_SLOT` relocation for the undefined dynamic symbol `pthread_self`.

The predicate then obtains the pointer at `0x40443a8` through getter `0x2503090`. For a non-null pointer it loads the 64-bit field at `+0x10`; for a null pointer the comparison operand remains zero. It returns `1` when that operand differs from `pthread_self()` and `0` when equal. The dispatcher takes the queue branch for a nonzero result and the synchronous native virtual-dispatch branch for zero. These are static code relationships; no claim is made that a live object retains this vptr indefinitely or that the owner pointer has a proven lifetime.

An observer that only accepts `TID == PID` can therefore miss the intended input. A separate one-shot off-thread scalar mailbox can establish that at least one entry occurred off the process main thread while preserving the main-thread queue's single-producer contract. Such a record cannot establish the native predicate's live result, identify the view as Waveform, prove queued execution or ordering, or establish view/owner retention. No native object read is needed for that limited record.

Reproduce with Python, pyelftools and Capstone:

```sh
python3 experiments/mods-shortcut-delivery/dispatcher-observer/thread-predicate/verify-static.py \
  --elf /private/path/EP147
```

Raw firmware and disassembly remain private. This proof supplies no hook claim, runtime readiness, menu activation, focus handback, cancellation semantics or physical-timing validation.
