# Independent source review and corrected ARM64 repeat

Root found a data race in the first integration fixture: each native submission
constructed a Guard whose destructor incremented a shared plain `unsigned`.
The concurrent scenario ran that increment on four threads. A passing result
does not make that C++ execution defined. Those earlier passing runs are
preserved and superseded; the only C++ correction makes this bookkeeping
counter `std::atomic<unsigned>`. Capture, transport and all assembly remained
unchanged. This was a source-review finding, not a sanitizer diagnostic.

The corrected author's run `5643311b-7a07-4025-95cd-0307a610f904` and root's
independent run `d2e79bb4-5059-40a5-bfa4-fbafb7484d1a` both pass **239 assertions
across 16 scenarios**. Both reproduce exact executed control failures
12/3/14/2 and reject two compiled CFI controls without executing them. Each
candidate run captured all eight concurrent native submissions. The pinned
local ARM64 image ran with networking disabled; no sanitizers, firmware,
emulator or device calls were used.

The independent wrong-LR control captured seven concurrent records instead of
the author's eight. The one-CAS transport permits this contention drop. Its
conservation and native forwarding checks pass, and all twelve intended
wrong-LR failures remain identical. The sanitized output differs only in that
control's concurrent-record count. Neither an unchanged passing rerun nor an
expectation change was needed to accept this documented scheduling variation.

[Independent metadata](independent-result.json) binds both result sets and
binary hashes. The executor object is byte-identical. The complete test ELFs
differ by 18 characters in three local `STT_FILE` compiler-temporary names in
`.strtab` only. All section headers, symbol metadata, allocated code/data and
unwind sections match. Normalizing only those three filename strings makes
the whole ELF equal. Initial comparison assumptions of identical contention
counts and only one temporary filename were rejected; those comparison
receipts remain private. They were not build or execution failures.

Source review additionally checked exact synthetic LR filtering, per-invocation
context layout, executor register restoration and tail SP/LR, nonthrowing
helpers, errno ownership, normal-return-only publication, native forwarding
on capture rejection and absence of post-return native reads. The concurrent
fixture's other writes are atomic, thread-local, assigned distinct event
indices, or limited to serial scenarios; shared configuration is fixed before
thread creation and read only until joins. This review does not claim a
whole-process race proof.

The original pointers here are release-published before synthetic calls. The
SDK's relaxed original-slot store and entry-patch/cache behavior remain a
different contract, described in the [hook-lifetime audit](../task-hook-lifetime/).
Live firmware provenance, entry borrowing validity, dispatcher/child retention,
installed trampoline execution, asynchronous unwind, output ownership and
physical timing remain open. These helpers still have no SDK adapter or
runtime hook claim, and matching or missing records never establish completion
or cancellation.
