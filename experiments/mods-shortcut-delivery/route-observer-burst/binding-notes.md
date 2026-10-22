# Burst observer binding and review scope

This separate candidate keeps the accepted native entry **0x1b01cf8** and adds
a bounded off-thread scalar capture pool. Its identity is
`encoder-route-burst-observer`, namespace `erb`, log prefix
`ERB`, with opt-in `AZ_ENCODER_ROUTE_BURST_OBSERVER_EMULATOR=1`.
The prior [one-shot observer](../route-observer/) and its positive native sample
remain separate evidence. This binding performs no installation or runtime action.

The exact ELF SHA-256, FDE entry/extent (`0x2e8`), slots
`0x2d453e8` / `0x2d45758`, first four instructions and complete function
hash must match the pinned [input-route evidence](../input-route/). Both source
slot comparisons must remain in the compatibility guard before the sole hook
claim. Static slots are read for compatibility; no vslot is patched. A later
claim still requires live ownership and all unchanged SDK installation checks.

The five-input ARM64 tail wrapper is unchanged:
`x0`, `x1`, full `x2`, `w3`, `w4` plus original LR passed in `x5`.
Its full source SHA-256 is
`a1f1cf42cc587c3222d299ac9dd31bfd0944f1ea51ec98a19d74ba40088a5034`.
The prior [55-assertion ABI fixture](../dispatcher-observer/abi/) and all its
source hashes are immutable references. The new binding checks copied wrapper
identity, build wiring and exact helper shape; it does not rerun or relabel
those previous ARM64 assertions. Any wrapper change invalidates this reuse.

## Bounded slot contract

The stable candidate passes **47 binding/source-shape checks and 30 strict
rejection controls**, preserving the previous 34/18 checks and adding 13/12
for the bounded slot design. The source-shape checks supplement independent
review and actual-source host tests. They are not a C++ parser or a complete
concurrency proof.

The off-thread pool has 16 immutable slots. Each attempt reads the saturated
reservation count, refuses capacity exhaustion, and makes at most one strong
CAS. Failed reservation counts contention and returns; there is no source
retry or spin. One source-level strong CAS and lock-free atomics do not
prove bounded ARM64 contention latency or physical timing. A successful
reservation alone identifies a unique slot. Record fields must be filled before release-publication of READY. A single worker scans all
slot indexes, acquires READY before copying that slot, then release-publishes
CONSUMED. It skips holes so an unpublished earlier reservation cannot block
later READY slots. Consumed slots are never made reusable.

Capacity and contention counters have different meanings. A slot ordinal is a
reservation index, not native input order, execution order or a global sequence
across threads. Off-thread records retain main sequence zero. The main and RAW
lanes have their own sequencing; logging or scan order does not correlate those
lanes into a single causal history.

Capture remains entry-only: copy x0/x1/full x2/w3/w4/LR/TID as scalar values,
keep addresses opaque, and tail-forward all native registers and unknown
returns through the unchanged wrapper. No source, observer, event or task
pointer is dereferenced for this observation. Additional slots do not establish
native return types, accepted/completed handling, queue ownership, cancellation,
child identity, native object lifetime, first TLS cost or physical timing.

## Reproduce

From repository root with the existing private interpreter and exact firmware:

~~~sh
../../../work/qemu-python/bin/python \
  experiments/mods-shortcut-delivery/route-observer-burst/verify-boundary.py \
  --elf ../../../work/picks-native-probe/EP147
~~~

The final `binding-result.json` binds the candidate source files and contains
exact counts and rejection labels. Wrong entry/slot/prologue/LR/x2 forms remain
strictly rejected. Burst-specific controls distinguish lost publication
ordering, retry or reuse, capacity changes and head-of-line blocking. Host,
ARM64 build, and any future emulator evidence must be reported separately.

Independent read-only source review found no concrete blocker in the bounded
slot design. Relaxed reservation ordering is used only for unique index
ownership; record visibility depends on each slot’s release/acquire READY
publication. The single worker consumes each immutable record at most once and
continues scanning past reservations that are not READY. The reviewed source
is bound in the result as SHA-256
`e59eeadda2497467e5b01dac02d17d1932964878a85d58cc4f035c2ca9567e80`.
This approval covers this passive source scope and does not replace the host
concurrency, ARM64 build or future runtime gates.
