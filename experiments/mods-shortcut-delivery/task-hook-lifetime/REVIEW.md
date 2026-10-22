# Independent review and repeat

Root independently ran the pinned-source extraction and host matrix on
October 22, 2026. The 56 assertions across ten serial scenarios passed in
plain and ASan/UBSan. The late-original and wrong-continuation controls
reproduced exactly four and three named failures respectively in each mode,
with no sanitizer diagnostics. The resulting JSON is byte-identical to
`validation.json`, SHA-256
`d0414f364dc8e1526ff8bcbfe7efa703fdf30ab11de29f4b992a52e7757f2e9b`.

The review distinguishes the SDK's relaxed original-pointer store followed by
the release entry-instruction store from the synthetic wrapper fixture's
release/acquire pointer setup. Instruction fetch is not a C++ acquire of that
entry store. This serial source fixture validates order and modeled resource
failure behavior, not the core's cross-core instruction-cache contract.

Both original slots and capture state must outlive a partially installed hook.
The pinned SDK/loader retains opened libraries and successful hooks until
restart, including after failed initialization; capture close does not unpatch
or wait for invocations. A native-body exception resuming in the original FDE
is distinct from an asynchronous interrupted PC inside anonymous trampoline
code. The latter has no demonstrated unwind metadata here. Actual installed
island/trampoline execution remains untested, including X17 scratch effects.

No core source was changed and no native instructions or devices were used.
The fixture models memory/protection/cache operations on owned buffers, and
does not provide a loader lifecycle or whole-engine concurrency proof.
