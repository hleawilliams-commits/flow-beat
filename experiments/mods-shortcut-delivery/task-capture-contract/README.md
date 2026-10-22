# Entry-local task values and return relations

This offline prototype separates submission-entry values, the submission's
full-width return, and executor-entry values. It never interprets a matching
TaskNo as accepted input, completed handling or cancellation. It contains no
native hook, capture queue, logger, identity allocator, navigation operation or
community package.

The [lifetime review](LIFETIME.md) explains the conditional native-storage
contract and exact fields. The [independent review](REVIEW.md) checks the source
and reproduces the final host matrix. The separately scoped
[ARM64 call/return fixture](../task-call-return-abi/) tests an assembly wrapper;
these two artifacts have **not been integrated or installed**.

## Contract and preconditions

`tc_begin` performs a synchronous pre-forwarding copy into one private,
caller-owned 64-byte context. Context/output storage must be valid, writable
where appropriate, and disjoint from the native task. The task fields must
remain valid and immutable for the entire copy. The context must remain private
and unpublished until `tc_begin` returns: initialization is not an atomic
publication protocol, and the zeroed status temporarily equals `TC_COPIED`.
Any later cross-thread publication requires its own synchronization and loss
contract.

The exact caller filters are submission LR `0x1b01e04` and executor LR
`0x237d800`. Both precede any observer helper call. Phase, caller, nonzero
observer identifiers, null, alignment and arithmetic-range filters precede the
vptr read. The expected task vptr is `0x2d444d8`. These are rejection filters,
not memory-mapping, lifetime or authenticity checks; even reading a plausible
vptr requires valid task storage.

Successful capture reads five disjoint ranges: eight bytes of vptr, eight of
TaskNo at `+0x10`, eight of opaque ID at `+0x20`, and four each of delta and
cumulative bits at `+0x28` and `+0x2c`. That is 24 payload bytes and 32 total
native bytes. The dispatcher field at `+0x18`, tag, padding and unused tail are
never read. Task bytes are unchanged. The context holds values and observer
metadata only; no task, box, dispatcher or child address is retained.

Run and invocation IDs are caller-provided, nonzero observer values. The caller
must guarantee their uniqueness in the intended scope, including teardown and
reuse; this header does not allocate or validate uniqueness. A local invocation
ID pairs an entry with its own return. Native TaskNo is not that ID. Thread
observation is opaque metadata and does not prove native ownership. Contexts
must remain separate across nesting and reentry.

`tc_return` accepts only the entry context and full 64-bit return value. It has
no task-pointer parameter and performs no native read. Its possible relations
are unqualified, zero, different value, or same nonzero value. A task may already
have been consumed and freed before this function runs. Missing return records
remain unknown; exception handling, capture capacity and teardown are not
implemented here.

`tc_candidate` compares a submission-entry tuple with an executor-entry tuple
in the same observer run. Execution may precede the corresponding submission
return. Equal nonzero TaskNo, ID and both integer bit patterns yield only
`TC_SAME_VALUES_ONLY`. Two identical executor records can both be candidates;
the helper never chooses a unique match. It intentionally does not consult a
submission return, so this value relation can coexist with a missing, zero or
mismatched return history. No admission claim follows. Zero TaskNo, wrap/reuse,
different runs, missing records, rejection and potential capture loss remain
ambiguous. There is no numeric ordering, automatic correlation, accepted state,
cancelled state or completed state.

## Executed checks

The final [validation](validation.json) passes **67 assertions in six scenarios**
in plain, ASan/UBSan and TSan modes. An independent execution reproduced the
same JSON and source hashes. It covers early rejection without reads, exact
read ranges and unchanged input, full-width values/returns, separate scripted
nested contexts, duplicate/wrap/missing-boundary ambiguity, and a real `munmap`
of owned synthetic task storage before return recording.

Two broken controls run in each mode. Truncating the stored return to 32 bits
fails exactly the full-width-return assertion. Adding a dispatcher-field read
fails exactly the read-range and forbidden-field assertions. No sanitizer
diagnostics occurred. These controls discriminate the tested behavior; they do
not prove native mapping or all possible races.

All tests are serial host fixtures with owned synthetic storage and instrumented
read accounting. Scripted context nesting is not native recursive integration.
TSan is not a concurrency proof here, and the host checks establish no ARM64
timing or native lifetime. The runner derives caller LRs from the previously
verified instruction blocks pinned at `70fb9d4`; it reuses those static facts
without executing firmware or rerunning the unchanged firmware verifier.

Run from the repository root:

```sh
python3 experiments/mods-shortcut-delivery/task-capture-contract/run-tests.py \
  --result experiments/mods-shortcut-delivery/task-capture-contract/validation.json
```

Immutable inputs, binaries and raw process output are kept in ignored
`out-history/` directories. The earlier 66-check pass remains there; the final
67-check pass adds the unchanged-native-task assertion. No historical failure
or test output was overwritten.

## Next integration gates

Compose the capture helper with the separately tested ARM64 wrapper and a
bounded observer-owned publication protocol. Test original-LR forwarding,
disabled/rejected/full/reentrant paths, unique invocation allocation, losses,
native exception paths and teardown with the actual combined source. The
executor is a different one-input ABI; the submission wrapper is not its
implementation. Establish safe original-function publication, hook ownership,
module lifetime and unwind registration before installation. Native task
borrowing remains conditional on the documented entry lifetime.

Only then can a fresh, selective emulator experiment attempt submission-return
and executor-entry observations. It must keep missing observations UNKNOWN and
all post-return native reads absent. Live admission/execution/cancellation,
dispatcher/child retention, counter ownership, direct menu and focus handback
remain unresolved. This work changes no core, package, catalog, device or
staging gate.
