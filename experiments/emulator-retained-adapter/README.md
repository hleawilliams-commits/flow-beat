# Retained-before-validation native64 adapter

This separate, inactive successor keeps an allocated owner and every acquired
duplicate through failed STATE validation. It addresses the failed-attachment
custody gap identified in the [acquisition review](../emulator-wire-native64/ACQUISITION-REVIEW.md).
The frozen native64 adapter and all previous evidence remain unchanged.

The caller first stores the owner returned by mbra_create, which allocates and
copies a valid expected state without I/O. mbra_duplicate arms one attempt and
retains its exact return before interpreting it; a distinct nonnegative result,
including fd0, becomes held. mbra_validate arms a separate one-shot STATE,
then retains its raw result and all 208 initialized/returned wire bytes before
validation. A failed or partial copy remains diagnostic data. Version, reserved
fields, shape, identity, epoch, sequence, flags, CRC length and all 152 frame bytes
must match before a semantic state or backend is admitted.

Attempted-stage failures retain the owner and any duplicate without implicit
close/free/retry. Stage
repeats make no I/O and preserve the first evidence. Inspection is a bounded copy
under external serialization. It is not a concurrent snapshot or cancellation
API. The raw duplicate result and diagnostic descriptor number expose an actual
in-process descriptor: caller discipline and external exclusion are still needed.

The native64-core.inc include is generated privately as the exact frozen
adapter.c byte prefix before int mba_attach(. This retains the existing ABI
assertions, STATE callback, typed FRAME/ROTATE/RESTORE encoding and error
classification. Only the lifecycle interface changes. The legacy public
lifecycle is not linked. Copied backend callbacks check readiness before invoking
that private core; acquisition evidence stays unchanged by later operations.

[Host evidence](HOST-TESTS.md) describes focused synthetic checks and strict
controls. The runner takes a local emulator source checkout as --emu; it exports
pinned source files to a new ignored private directory. It never opens an
emulator endpoint. Raw prepared sources, binaries and logs remain private.

## Lifetime and authority limits

The original fd remains borrowed and must remain stable until duplication
returns. Creation copies its number; it does not acquire the original or issue
CLAIM. Other fd aliases can still mutate the shared file state. Matching expected
values do not establish authoritative originals, stopped identity or baseline
provenance. Owned idle flags 23 remain idle; no automatic FRAME converts them
to producer-admissible flags 31.

The owner must outlive every copied backend, broker and callback. Explicit
mbra_dispose retains the frozen external quiescence and uncertainty-resolution
preconditions. It consumes the caller's handle before one close when a duplicate
is held, preserves the raw close report, and frees storage. A zero API result
does not prove close success or recovery. This is not a checked graph teardown:
the frozen broker's void destroy cannot certify that all references disappeared.
Tests clean only their serial synthetic graph after observations are copied.

No process-death durability, blocked-I/O inspection, concurrent API safety,
original/CLAIM ownership, native authority, real VFS validation, endpoint binding
or native timing is provided. There is no runtime installation, community package
or physical-device change. All existing runtime, stems-staging, USB, recovery
and release gates remain closed.

Next compose retained original-fd/one-shot CLAIM custody and exact pre/poststate
with the adapter and broker, while keeping failed construction graphs inspectable.
Checked teardown, deadlines, peer/supervisor binding, external writer exclusion
and complete overlay/host-file recovery remain separate gates.
