# Independent offline broker/producer review

No blocking defect was found within the declared, cooperating single-broker lifetime contract. The implementation is an offline library and producer-gate candidate, not a complete producer handback. No device, emulator, physical deck, transport endpoint or installed module was accessed during this review.

The review read the actual C library, Rust gate, their contracts and test runners, then independently executed both new suites. It did not rerun the frozen standalone kernel suite. The C interaction fixture uses seven exact extracted functions from that frozen candidate and a single fixture-owned `struct file` for its kernel callbacks.

## Reviewed identities and independent results

| Reviewed input | SHA-256 |
|---|---|
| `broker.c` | `6d19795439378bd4c196b53e556bca2d67488a4ec9097177792d573541317cb8` |
| `broker.h` | `5d854db2b15d5130ce2c47c6e48f8b9c20508c9d0f3cd5de8ea2f990ca1fbc32` |
| `producer.rs` | `70ec0056c3f02ae24e7d2a90e8fd2310208b1092e54da4bc2469eab794f29a06` |
| Rust runner | `0cec1296a802f6138d5a706d853fe8ed34b91d7270a5c5d6b3d8ffa57ce2b01e` |

The independent C run passes **203 candidate assertions and six strict controls per mode** in plain, ASan/UBSan and TSan. Its complete JSON is byte-identical to [host-result.json](host-result.json), SHA `6a39c31dba05581c3930982e4f09181ac3fae2d078d3920eb02de1b0d856c96a`. Each control must fail its exact named assertion with the expected count and exit status; sanitizer diagnostics are rejected, including in expected-failure runs. The result pins the exact fixture, runner, prepared kernel, declarations, helper inputs and include shims.

The independent Rust run passes **19 tests and eight strict controls** using local `rustc`. Source, runner, compiler, complete test inventory, variant source hashes, test names, pass/fail/filtered counts and exit statuses match the author run. The original result is `6d8072a63016ad4ac4bba093bcf72651a24d9fd2cb98aebaed3774c1ff71d082`; the [independent result](producer-independent-result.json) is `a742ef4056add3c16bdfae9ea4a66fec1f7d9c504e7bd28e130ceef0cd4ac553`. They are **not byte-identical JSON**: expected-failure diagnostics contain varying numeric Rust panic-thread IDs, which the frozen runner does not normalize. All nine retained outputs, including the entire failure bodies, match after the existing path/duration normalization plus removal of only that numeric ID from the named panic-thread heading. The private comparison receipt hash is `ce1df20929be601589abf05c26cdb028b5501a6c55ef90fdd2ec3f36bc34e202`. Original outputs and both results remain intact; no runner change or repeat execution was used to hide the difference. Rust execution here is not a sanitizer run.

## What the source enforces

`mb_create` checks the complete declared owned state and publishes an opaque broker only after its snapshot callback agrees. `state_valid` requires exact flags 31, CRC prefix 96 and nonzero instance/run/epoch. The semantic state omits wire version/reserved fields; the actual-kernel fixture's translation validates those before producing it. A future real adapter must do the same. A matching snapshot does not prove absence of another broker or an earlier unresolved syscall.

`mb_adopt` issues only one grant per epoch after fresh exact baseline verification. `mb_submit` copies the supplied request before clearing output, checks the full grant before cache lookup, and admits only the next request ID. Its exact-last duplicate path compares the entire payload, returns retained receipt data without either backend callback and cannot reopen admission. Different bytes for that ID close admission without replacing the receipt. The final fixture checks both submit and snapshot call counts for cached committed and unknown requests.

`call_submit` treats malformed callback outcome/error combinations as UNKNOWN. An actual committed command with a lost or malformed reply therefore remains closed and retained. `mb_reconcile` accepts only the complete predicted post-state; a lower sequence, mismatching bytes, lost ownership or incomplete snapshot does not prove cancellation. Positive FRAME reconciliation does not reopen its grant. `mb_rotate` requires closed admission, no unresolved command and another exact current snapshot. A committed or positively reconciled rotation clears the old grant, but a new producer still requires separate fresh checked adoption. Cached success is a receipt, not permission to rearm.

The C mutex serializes callbacks and methods. The actual fixture forces an issued callback to remain pending while quiesce waits, and separately distinguishes a command that committed before its lost reply from a copied command that has not yet been applied. Twenty-four concurrent exact duplicates produce one kernel mutation. These are selected host schedules, not a proof of kernel/VFS lifetime or transport behavior. Destruction still requires externally established quiescence; the defensive busy-mutex branch does not establish it.

The Rust gate captures a key and contiguous request ID while reserving its canonical bounded queue entry. `admit` requires the exact immutable envelope at the front; it does not replace its key at dequeue. Queue-full refusal consumes no ID. Closure clears unsent queue/cache values while preserving an unresolved request. Exact positive acknowledgment or definite rejection can resolve that retained request, but neither reopens a closed generation. Baseline staging, full acknowledgment and the caller's release assertion are separate steps; reuse requires a newer epoch. Counter exhaustion closes admission. A source mutation that retags an old mpsc envelope is rejected by the strict stale-envelope control.

## Deliberate limits exposed by the tests

**Fresh tags cannot certify fresh bytes.** C `mb_stamp` accepts caller-provided frame bytes. Rust's private `Envelope` prevents mutation or retagging through that object API, but its public `create` still accepts a new frame value. Both fixtures deliberately demonstrate that a caller can copy old frame bytes into a new request under a fresh valid grant. Excluding old envelopes is useful; invalidating external UI caches and requiring fresh gestures still needs actual preview integration.

**Settings serialization is not acknowledgment ordering.** `with_host_settings` checks the captured generation key and shares the transition lock, so an old-epoch callback cannot run after a new generation opens. It does not bind a request ID or FRAME acknowledgment. The explicit same-generation counterexample executes two callbacks in reverse creation order and accepts the older value last. It does not establish saved-state ordering, equality with the committed overlay, rollback of partial callback effects or actual egui quiescence.

**Receipts and release observations need a trusted adapter.** Public semantic receipt values and `confirm_released(..., true)` are not self-authenticating capabilities or hardware observations. The C backend must retain the same open description, tell the truth about definite rejection versus unknown outcome, avoid retries and satisfy the sole-mutator contract. Rust's settings callback must not reenter its mutex; panic poisons the gate. Neither library authenticates producers, owns another process's queues, proves a disconnected producer died or prevents an arbitrary caller from constructing a replacement library instance with lost history.

**UNKNOWN is an ownership obligation.** A low STATE snapshot cannot cancel an outstanding command. Exact positive reconciliation is justified only by the retained single-command accounting, same owner and no intervening writer. In-memory receipts do not survive broker death. A new file or new object cannot silently recreate that history from STATE. No durable journal, session-authentication protocol, daemon lifecycle or reconnect wire adapter is supplied.

## Integration remains separate

The C/kernel interaction and Rust producer tests are separate executables. There is no C/Rust FFI, transport framing, actual forwarder patch, preview integration or real saved-counter write in this checkpoint. A neutral baseline policy is also a caller prerequisite: the kernel and C broker accept an opaque 152-byte frame, and the Rust release barrier only checks the supplied assertion. Already sampled native events remain outside the generation guarantee.

Before any runtime use, the actual producer path must preserve creation-time tags through its channel/cache/reconnect source, reconcile acknowledged baseline state, exclude old saved-state writes and distinguish definite guest results from socket success. The separately documented stopped recovery must restore full original neutral overlay content and injected mode plus exact host bytes before any guarded restart. Nothing in this review implements or opens those gates, changes the installed core or makes a community release ready.
