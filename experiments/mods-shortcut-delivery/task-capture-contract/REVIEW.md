# Independent host contract review

One independent invocation reproduced **67 passing assertions across six
scenarios in each of plain, ASan/UBSan and TSan**. The strict truncated-return
variant failed exactly `exact-values/full-width-return` in each mode. The
strict dispatcher-overread variant failed exactly the read-range and forbidden
read assertions in each mode. No sanitizer diagnostics occurred. The result is
byte-identical to `validation.json`.

| Reviewed artifact | SHA-256 |
|---|---|
| Independent result / validation | `7d6364090865e324e44d1521f06934de0f386c39d521439411e8f4afd2a8618d` |
| `capture.h` | `aa9b82311065fa3481cda29883f6dae40d1685c213d520271e1f4530039291ea` |
| `test-capture.c` | `2af588c0fa9cc8ee7abf1e141b455978c313e101788548795050b27bc15d5a98` |
| `run-tests.py` | `03ef2fe02aadc3c385e6473096618f5be11b6ee2815f2e6662430319798a4a02` |

A separate offline comparison passed 22 binding/snapshot checks. It derives
caller return addresses `0x1b01e04` and `0x237d800` from the pinned task-box
instruction blocks, checks the task vptr/allocation facts, and compares frozen
fixture inputs with source. The pin is
`70fb9d4d3aaffe04ceda5164d5bb2c4a93b759d9`; this reuses prior exact-ELF evidence
and does not constitute another firmware or runtime validation. All three
reviewed source files remained unchanged throughout the independent run.

## Findings and preconditions

No blocking correctness issue was found within the declared host-only value
contract. Source review and the tracked-read seam show five requested reads
covering 32 bytes: vptr validation plus 24 bytes of scalar payload. They exclude
the dispatcher pointer, helper tag and padding. The accounting tests requests
through that seam; it does not measure generated native memory instructions.
The native-task-unchanged assertion covers separate source and context storage.

The synthetic `munmap` scenario removes the task allocation before
`tc_return`; the return helper still operates solely on the pre-call context
and full result value. The context contains scalar fields only. Scripted inner
and outer contexts demonstrate independent copied values and per-call return
records even when their TaskNos match. They are not a native recursive-wrapper
or concurrency test. TSan exercises serial fixture execution.

Required caller preconditions remain explicit:

- Context/output storage is valid, caller-owned and disjoint from the task.
- The concrete task and its initialized fields stay valid and immutable for
  the entire entry copy. Caller LR, null, alignment, address-overflow and vptr
  filters do not establish mapped storage or native lifetime.
- The context remains private and unpublished until `tc_begin` returns. Its
  initial zeroing temporarily gives status the numeric `TC_COPIED` value; it
  is not an externally pollable concurrent state machine.
- Run/invocation identifiers are supplied by the caller. This code allocates
  neither unique identifiers nor a process/lifetime scope and detects no
  allocator wrap or logging loss.

`TC_SAME_VALUES_ONLY` is deliberately a tuple comparison, not native identity,
admission, uniqueness, execution success or completion. The comparator does
not inspect a submission return. Matching values may therefore coexist with a
missing, zero or mismatched return, or multiple equal execution candidates.
The tests preserve zero and wide TaskNos, duplicate values, differing payloads,
scope differences and wrap-like sequences without promoting them to stronger
claims. A decreasing TaskNo is not diagnosed as wrap. Missing execution is not
classified as cancellation.

No production observer, publication queue, automatic event join, native
memory validator, module or wrapper integration is supplied here. Standalone
ARM64 wrapper work is separate and cannot establish this contract's live
preconditions. No emulator, physical device, network or firmware execution
occurred during this review.

The ignored `out-history/independent-review-20261022T180500Z/` preserves the
invocation, before/after source fingerprints, full console, result and binding
receipt. The runner retains immutable source/build/output snapshots separately.
No capture, test or runner source was changed by the reviewer.
