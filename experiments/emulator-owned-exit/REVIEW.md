# Independent review and reproduction

No remaining blocker was found for this **inactive, offline lifecycle-owner
candidate**. This review does not approve live owned-mode enablement, native
restoration, or guest/backend teardown. Main still selects a literal absent
owned pair. The four-file successor patch is
`b278c09b4db47e341ebc5542386dfa79d185586c905b8c4904c430f2aef51a3b`;
[candidate pins](candidate-pins.json) bind all 18 prepared files.

## Source and lifetime findings

The constructor checks actual shared-Arc identity between the port and unique
worker. A rejected pair returns the port, worker and optional supervisor value
without performing stop/join. Main's owned branch partitions the supervisor
value out of the App's legacy slot and keeps the lifecycle owner outside the
AppCreator closure. The returning helper preserves the runner's original result
and independently invokes completion, including when no App was created or an
App was dropped without `on_exit`.

The first admitted finish changes Running to Joining and takes the unique
worker under the owner mutex. Stop and actual join run after releasing that
mutex. A stop error does not skip join. Concurrent completion observes Joining;
completed calls remain idempotent. The direct-worker-thread check refuses a
first finish before taking the worker. It prevents direct self-join, not every
callback dependency cycle. Backend/settings nonreentry remains a contract, and
there is no bounded callback or join deadline.

The exact typed stop result and joined session remain held in the owner. A
review finding in the initial source was corrected before the frozen matrix:
poison acquired while joining must not cause the just-completed local session
and stop result to be dropped. The final code salvages that mutex only to retain
those results, keeps the poison flag set and returns `OwnerPoisoned`. The
pre-take poison path is deliberately different: it refuses before stop/join and
reports unjoined state. It must not be described as having closed admission;
the worker may remain active. `StopStatus::NotRecorded` also avoids asserting
that no stop call has happened while Joining is still in progress.

Actual App `on_exit` returns before legacy Quit/`boot::stop` for both a lifecycle
owner and an unpaired owned port. A completed empty or clean worker still
returns `RecoveryRequired`. The unpaired branch requests stop but cannot claim
a join. Legacy None behavior remains in the original branch.

## Independent execution

One fresh offline export ran the frozen runner after the primary pass. The
production configuration compiled successfully. Its test binary listed 138
cases; only the 12 new cases ran, and all passed. The 126 inherited tests were
filtered out, including existing supervisor tests. Each of seven separately
compiled strict controls executed one selected case and exited 101 at its
intended functional assertion. Compile failures, timeouts and unrelated panic
locations were not accepted as control success.

The eight owner cases exercise returned resources on pairing rejection,
returning creator failure, concurrent Joining, stop failure followed by actual
join, poison during/preceding finish, direct-worker refusal and legacy
partitioning. The four headless App cases exercise repeated exit plus returning
fallback, App Drop without `on_exit`, missing-owner refusal and the legacy
no-supervisor branch. Deterministic callback barriers establish the tested
interleavings; their timeout paths fail rather than count as successful
synchronization. Mutants retain actual joins and fixture cleanup.

App tests supply `None` for the actual `Launched` type. Owner lifetime tests use
a counted fake resource. They run actual App callbacks and actual dedicated
Rust workers with synthetic backend/settings implementations, without spawning
or stopping a child process. Thus absence of queued Quit is directly tested;
real supervisor `boot::stop`, framework termination and live resource behavior
are not dynamically tested.

Results are [primary](test-result.json),
[independent](test-result-independent.json), and
[comparison](comparison.json). The result hashes are respectively:

- Primary: `e9e1fafe9ac95cc677e9e582db49ab19c0690e87b5ec1268a1379d1eb23ee9d5`.
- Independent: `5c651e081a5fd7e4ed8e644642bb91d10666a1fec1562c02cd8fcc831dcd67e6`.
- Comparison: `583f18f034b9cd00e8d3676baf5283638fa9e599efe792adca30c4b1bf4eaa3c`.

Every result field agrees after excluding exactly the 17 enumerated raw
compile/test-log hash fields; 16 of those hashes actually differ. This is not a
byte-identical JSON or binary claim. All eight complete test logs agree after
replacing only each private run path, named panic-thread numeric IDs and
libtest's finished-in duration. Compiler logs were not normalized for full-body
equality.

An independent private comparison program reconstructed each declared mutation
from each retained base tree and checked all 18 files in every variant: 288
variant file comparisons across the two runs. It located each selected
function by its declaration and indentation-matched closing brace, independently
calculated the exact assertion macro line and column, and matched a real panic
header in the corresponding log. All 14 checks (seven controls in each run)
agree with the published locations. It did not import the runner's location
routine. Its hash is retained in the comparison; source snapshots, complete logs
and executables remain private.

## Unresolved enablement boundaries

[Platform limits](PLATFORM-LIMITS.md) remain material. Returning from `on_exit`
cannot cancel a close already underway. The returning helper is not a
panic/unwind guard; macOS termination need not return through it. At the final
main branch Drop, a future owned owner or rejected pair would still release its
retained resources. On Unix that can close the supervisor's stdin pipe. Process
death independently closes descriptors. No native termination interception,
final-custody transfer or recovery protocol exists here. Windows retains only a
PID in `Launched`, not the job handle; real owned construction is unavailable
there in this candidate.

The in-memory joined session is not neutralization, restoration, native event
cancellation, persistent custody or teardown permission. This pass performed no
application-main, window, framework-event-loop, supervisor-process, endpoint,
kernel, firmware, emulator or physical-device execution, and no sanitizer run.
The inactive candidate's local lifetime evidence leaves the live gate closed.
