# Review of inactive fixed-session construction

The implementation, constructor ordering, resource graph, preparer, strict runner,
25 focused tests and 16 controls received a separate agent review. Root reviewed
the final production and test source, parent route/producer APIs and control
anchors. No remaining production defect was found in this reviewed scope.

Two review improvements were applied before the first matrix: initialization now
checks retained settings-writer poison before Ready without acquiring that
writer's operation lock; the runner removes inherited encoded/build Rust flags
and compiler-wrapper variables. No frozen parent was edited.

## Decisions preserved

- Actual backend/directory custody exists before initialization. Failed
  construction never provides a new attempt; actual settings and route products
  are retained before later construction boundaries.
- The original directory remains retained and attachment uses a duplicate. A
  failed attachment does not authorize inventing complete original-file evidence.
- Route proxies own only separate backend/journal or settings cells, preventing
  a production ownership cycle. Test hooks use Weak owner captures where needed.
- The sink and initial grant share the full captured receipt. The private proxy
  refuses later baseline acquisition, while exact key/request/ACK admission
  remains the frozen producer's responsibility.
- First constructor cause and later refusal metadata are separate. A harmless
  repeat after Ready cannot erase the result or disable the first route.
- Journals are not held across underlying calls. Exact returned outcomes can be
  retained under poison only as diagnostics, while the route receives Unknown.
- Snapshot clones handles and copies journals without acquiring operation locks.
  It is neither a coherent instant nor filesystem/native freshness evidence.

The single-grant control would otherwise be masked by the frozen producer's
independent old-epoch rejection. The test therefore checks the wrapper's exact
BaselineAlreadyServed refusal, distinguishing the new boundary from that older
defense. Controls keep the designated final assertion strict; tests were not
relaxed to accept incidental failure.

## Limits and next boundary

The settings-sink parent's mismatch/rearming counterexamples remain frozen.
This successor closes those local construction gaps for this immutable route.
The backend remains injected/trusted: historical correspondence is not receipt
freshness, authentication or enforceable exclusion of aliases and independent
session constructions. A callback refusal can follow a known native commit.

The component performs no native acquisition, worker/supervisor startup, main
wiring, restore or release. Source review cannot establish external native or
file writer/reader exclusion, authoritative originals and stopped-process
identity, native dwell, cancellation/deadlines, durable custody or recovery.

The next integration boundary must establish actual backend acquisition and
exclusive resource identity, retain startup/worker/supervisor failures, and
connect owned stopped recovery without treating the final Rust Drop as safe
teardown. Do not activate this source to investigate those unproven conditions.
All existing runtime, staging, USB and release gates remain CLOSED.

## Verification review

The primary and independent fresh matrices both pass production compilation,
25 focused tests and all 16 strict compiling controls. The separate retained-file
audit reconstructs both complete matrices and compares all fields except the
35 enumerated raw-log hashes. See [host evidence](HOST-TESTS.md) and
[comparison](comparison.json). No inherited passing tests were re-executed,
no frozen source changed and no emulator or physical endpoint was contacted.
