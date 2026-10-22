# Owned diagnostic scalar layout

This offline planner binds **eight named atomic diagnostics, 56 bytes total**, in the exact unpublished `encoder-task-observer.so`. It reads supplied ELF, maps and auxv data, then returns addresses and widths for a separate caller to use. It does not open a transport, access a process, read a planned address, follow pointers, or inspect firmware/native objects.

The new tests pass **81 checks, including 54 strict rejection controls**. Four fabricated mapping configurations cover 4 KiB pages, a 4 KiB RELRO split with inaccessible reservation, 16 KiB pages and 64 KiB pages. None is a captured process mapping. No prior successful suite was rerun.

| Diagnostic | Link-time ELF value | Bytes | Frozen source type |
| --- | --- | --- | --- |
| `state` | `0x13070` | 4 | `std::atomic<unsigned>` |
| `heartbeat` | `0x130a8` | 8 | `std::atomic<uint64_t>` |
| `attempted` | `0x13068` | 8 | `std::atomic<uint64_t>` |
| `state_gated` | `0x13078` | 8 | `std::atomic<uint64_t>` |
| `caller_gated` | `0x13080` | 8 | `std::atomic<uint64_t>` |
| `tid_failed` | `0x13090` | 8 | `std::atomic<uint64_t>` |
| `worker_cycles` | `0x130a0` | 8 | `std::atomic<uint64_t>` |
| `worker_exit_reason` | `0x13098` | 4 | `std::atomic<unsigned>` |

Every selected symbol is a unique local defined `STT_OBJECT` of the exact size and value in allocated writable, non-executable NOBITS storage. There is no DWARF in this build: the C++ types above come from the exact source declarations and build receipt, while the symbol metadata independently establishes each compiled object's width and location. `STT_OBJECT` alone does not prove a C++ atomic type.

The exact binary SHA-256 is `71d7fc088f4def67d1307e4faeef301020aed04bfa3da7ae4bc6a422ece3e255`; adapter source SHA-256 is `538574af5f357131e0fa9ec6e26d54cfef0ec2a57f31baddff1e8e5bc7c134ed`. Both must match. The [existing offline maps planner](../../route-observer/status-map/layout.py) is reused by exact source hash `bb1182de30ce8d437e7b25d35723f901f1751b31bf59258dce3c35fc3a1e3c7c`; no private runtime module is imported. Its generic mapping checks are augmented with this module's fixed name, exact field allowlist, private mappings and private read/write permission at each scalar.

All `Transport` storage, embedded counters, record slots, `published`, `core`, and original-function pointer slots are excluded. The planner does not infer offsets within the 2,544-byte storage object. Worker stack locals such as polling/quiet counters and log-attempt counts are also excluded. No transport capacity, reservation count, phase-specific delivery count, record completeness, or coherent gate state can be inferred from the eight samples.

## API

Import this directory's `layout.py` under a distinct module name if another layout module is already loaded:

- `binary_layout(binary_path)` returns the hash-bound ELF model after checking the frozen source and scalar identities.
- `plan_reads(binary_path, maps_text, module_path, auxv_bytes)` repeats those guards and returns a plan containing `reads`, `load_bias`, `page_size`, and scope flags. `module_path` must be an absolute normalized path ending in exactly `encoder-task-observer.so`.
- `page_size(auxv_bytes)` requires AArch64 little-endian pairs, one `AT_PAGESZ`, a valid terminator and a supported 4/16/64 KiB page size. Only zero padding is accepted after termination.
- `decode_samples({name: bytes})` accepts exactly the eight names and exact widths and returns unsigned little-endian integers. It does not obtain those buffers or interpret their values as readiness.

Internal underscore-prefixed helpers are test seams, not alternate public entry points. In particular `_plan_model` does not repeat the binary identity check; a runtime caller should use `plan_reads` for both before/after map snapshots. Keep each individual read to the returned width; do not replace the allowlist with a contiguous read spanning the gaps, which contain excluded pointers.

A separate runtime caller remains responsible for exact loaded-file identity, unchanged process identity including start time, current same-process auxv/maps, validating the file hash before and after sampling, exact read lengths, and rechecking the resulting mapping plan and process identity. This artifact neither implements nor tests those process/transport operations. Two equal plans do not prevent an unobserved change between snapshots. `/proc` byte reads do not execute C++ acquire loads or create a coherent snapshot across these atomics.

The source enum maps `state` to Never=0, Installing=1, Ready=2, Closed=3, Failed=4; worker exit maps Running=0, InitFailed=1, SleepFailed=2, MissingStorage=3. Worker cycles can increase while Installing or Closed as well as Ready, and exit reason zero alone does not prove an active reader. Heartbeat progress is callback activity, not quarantine diagnosis. Aggregate attempts/gates have no phase-specific ownership or completion meaning. Native task admission, cancellation and execution remain outside this sampler's scope.

## Validation and reproduction

The 54 controls require their exact named rejection, covering source/binary drift; wrong ELF type/architecture and scalar type, binding, definition, width or location; duplicate/missing symbols; truncated segments; malformed, duplicate or unsupported auxv; changed file identity, file displacement, permissions, overlap or coverage; forbidden extra/missing fields; and short/oversized buffers. Structural ELF controls mutate only in-memory copies behind the public exact-hash gate. The public binary mismatch control uses a temporary local copy. No frozen source or binary is modified.

From the repository root:

```sh
../../../work/qemu-python/bin/python experiments/mods-shortcut-delivery/task-sdk-observer/status-layout/test-layout.py --binary experiments/mods-shortcut-delivery/task-sdk-observer/out-history/build-34bea645-2604-48e3-8094-7644561e1d42/output/encoder-task-observer.so --result /tmp/task-sdk-status-layout-review.json
```

Compare `/tmp/task-sdk-status-layout-review.json` with `result.json`. The existing private Python environment supplies pyelftools. The binary remains in ignored build history. Result metadata includes all tested source hashes and fabricated-map cases; it contains no observed process address, native pointer, raw log or binary. This is an offline planning result and supplies no installation, runtime, physical-timing or hardware validation.
