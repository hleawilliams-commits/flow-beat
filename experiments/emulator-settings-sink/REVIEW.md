# Independent source and integration review

Separate agents authored the bridge and fixtures. An independent reviewer read
the implementation, all 22 test bodies, all 14 control mutations, preparation
chain and runner before final evidence collection. The root reviewed the
composition and ran the primary matrix.

The bridge owns the pair writer and the evidence handle created with it, binds
an explicit selection and baseline, and checks callback order before pair I/O.
Accepted ACK/source/targets are retained before their corresponding operations.
Errors and armed phases close further work. Mutexes are not held across mapping,
pair I/O or test hooks. Evidence survives sink/route Drop without making a durable
custody or recovery claim. No actionable production defect remained in this review.

Two documentation clarifications were adopted: bridge counters track successful
host persistence and can lag a known native ACK; its completion record does not
prove that the producer observed a returned callback. The fixtures separately
check producer callback disposition after work completes.

The review identified the frozen route's later-epoch rearm as an explicit limit.
A failed settings callback consumes a known native ACK, but a future baseline
can reopen route admission. Its later FRAME commits before the permanently closed
bridge refuses. A mismatched constructed/backend baseline likewise refuses too
late to protect native admission. Both counterexamples are retained. The next
composition needs an external lifecycle owner that establishes baseline
correspondence and prevents failed-route rearming before submission.

## Validation tooling findings

The runner now checks captured compilation sources against the candidate pins,
rather than relying solely on the preceding preparer's check and a later
stability check. It also requires the final panic to be the exact designated
assertion, so earlier intentionally caught panic diagnostics cannot satisfy
that acceptance criterion. These improvements preceded the final matrix.

The first preparation attempt exposed a separate anchor ambiguity: the mapper
contains its own child anchor string as well as the redirect assignment.
The successor matches the complete indented assignment line. The interrupted
invocation and eight original inputs are retained; no candidate or test had run.
The frozen mapper and its parents remain unchanged.

## Independent final reproduction

The reviewer then ran one fresh matrix and audited both retained runs. Production
compilation, all 22 focused cases and all 14 exact assertion controls passed.
The audit reconstructs 30 variant trees, verifies source/input/log hashes and
all 211 listed test names, and compares complete result structures except exactly
31 declared raw-log hashes. All excluded hashes differ; all other fields agree.
The unchanged 189 inherited tests were not executed. See [comparison.json](comparison.json)
and [HOST-TESTS.md](HOST-TESTS.md) for pins and scope.

## Scope

This checkpoint connects the validated callback to real isolated host files;
it does not construct a live preview backend. Enforcement of external writer/
reader exclusion, authoritative originals, native receipt/stop authority,
durable custody, both-file/overlay restoration and guarded restart remain open.
Final Drop and process death can still lose ownership. No physical device,
emulator endpoint, core/package/catalog, recovery policy or runtime gate changed.
