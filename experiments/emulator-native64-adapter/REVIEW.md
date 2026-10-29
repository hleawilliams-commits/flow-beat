# Independent native64 adapter review

No blocking defect was found in the final adapter under its documented native64
Linux, exact-driver, serialized-caller and externally controlled lifetime
preconditions. This covers source and offline fixture behavior. It does not
approve endpoint access, installation, restoration or native teardown.

| Reviewed source | SHA-256 |
| --- | --- |
| `adapter.c` | `0f7313437ece7cef07fd69df9f63bc4b167a7a47e6205a226779b02bb45fac92` |
| `adapter.h` | `b6b01712383904b0d438e5ca1fbe20b0604a98831fe13b2e7951bf1f3b1f2d3d` |
| `syscalls.h` | `bbc885c14c1d468a057d5aa40ee537ec20533390453b1abe53fad2257828fd6c` |
| `native.c` | `10269247d44f43af38d312018fb5325e1101f96ef08e06c2002bf0caffa8609b` |
| `test-adapter.c` | `43b4cbc15520c2bd444639e52691724242466097789a63ee62c39cd67c861acf` |
| `run-tests.py` | `6e7d4cc5181d991442d81a29399ac30dc18691dec0ee983caf1bf373b73fdee5` |

## API, wire and fd review

Attachment copies the expected semantic state before clearing outputs, creates
one private duplicate, then compares a fresh owned snapshot with every expected
field and all 152 raw bytes. The public contract requires handle/report output
storage to be disjoint. It also requires the original fd to remain open and
stable throughout attach, including failed-attach cleanup. A duplicate returning
the original descriptor is refused without closing that descriptor. Fd0 remains
a valid distinct duplicate. Failure closes only the new duplicate once and
preserves the primary error separately from the cleanup result.

The adapter binds instance and run without retaining an epoch that would prevent
legitimate generation changes. STATE uses a separate zeroed 208-byte wire object.
It publishes no semantic output on syscall, shape or binding failure. Version2,
reserved0, flags exactly23 or31, CRC96, a nonzero instance pair, nonzero run and
nonzero epoch are required. Extra flags and missing OWNER/ENABLED bits are
refused. Frame bytes, including raw CRC positions and the tail, are copied without
normalization or CRC restamping. Successful full copy relies on the pinned
kernel's return contract; ioctl does not return a copied-byte count.

Submission copies its input before constructing zeroed, typed wire objects. It
permits only FRAME, ROTATE and RESTORE, with explicit request selection and mode
rules, matching bound instance/run and nonzero epoch/sequence. Compile assertions
bind the native64 sizes, relevant offsets, flag composition and exact Linux ioctl
encodings. Semantic broker structs are not cast to ioctl wire structs. The
kernel retains responsibility for current epoch/sequence, ownership and exact
mode0 template validation.

The exact pinned input-only mutation handler returns0 after the locked commit
and has no later copyout failure path. Thus direct return0 maps to COMMITTED,
ignoring stale errno. The reviewed precommit errno whitelist maps to REJECTED;
unexpected errno or malformed results map to UNKNOWN. This classification is
conditional on the exact driver and direct non-retrying syscall boundary, not a
generic network/transport error rule. The three native wrappers each call libc
once and capture errno immediately only for return-1. Their internal hidden
visibility does not authenticate a driver or exclude libc interposition.

Snapshot/submission failure does not close, reopen, query again or retry. Explicit
disposal consumes the handle before one close call, retains its raw result and
frees adapter storage. The caller must first eliminate every broker/callback
reference, resolve operation uncertainty under an authoritative lifecycle policy,
and retain that resolution and its accounting. Retaining an UNKNOWN receipt
alone does not permit disposal. The documented disjoint pointer/report storage
precondition is necessary. A successful API return means
inspect the close report; failed or uncertain close never causes retry.

A private duplicate prevents reuse of the original integer from redirecting
adapter calls, but shares its open-file description. Original, duplicated and
fork-inherited writers can still exist. Closing the final reference can orphan
ownership while retaining the sticky overlay and fence. Neither attachment nor
disposal establishes sole custody, restoration or safe process shutdown.

## Fixture and result review

The fixture executes the actual adapter, unchanged frozen broker and seven exact
kernel methods. Its fd table models references to the same file object and calls
the actual extracted `ctrl_release` only on the final modeled reference. It also
uses explicit usercopy/syscall faults and owned-memory kernel shims. It never
executes `native.c`, opens a real fd, or establishes actual VFS/fork/cancellation
semantics. This is a serial fixture; TSan coverage is not a concurrency proof.

Manual review confirms 137 assertions: attach26, STATE21, wire/local validation24,
syscall outcomes27, descriptor lifecycle12, broker integration14, plus thirteen
single-oracle selectors. The three final STATE flag cases were added before the
first matrix. No production defect or test failure was needed to explain a later
pass. Setup failures, compile diagnostics, sanitizer reports and timeouts are
refused rather than counted as expected control failures.

All thirteen controls use unique source anchors and target a named single
functional assertion. They cover mode, full payload tail, stale errno, EINTR,
unexpected positive return, precommit rejection, failed STATE publication,
reserved STATE, run binding, full attach comparison, original-fd reuse,
consume-before-close and local sequence refusal. Cleanup remains valid for each
mutant. Broker integration uses mutator-only faults, preserving the real
pre-submit STATE query, and checks UNKNOWN both before and after actual modeled
commit. Synthetic cleanup after uncertainty is not permission to close a real
uncertain session.

One fresh independent matrix, after the first complete primary pass, passed all
137 candidate checks in plain, ASan/UBSan and TSan, all thirteen exact one-failure
controls per mode, and three clean standalone adapter compiles. Parent passing
suites were not rerun. [test-result-independent.json](test-result-independent.json)
is completely byte-identical to [test-result.json](test-result.json), SHA-256
`9b7000366cd33b664948d7a75cd102379ce78e03ca43acda0383c8319b6ccf32`.
There are no excluded fields or normalizations.

A separate audit imported no runner/extraction functions. Across the two retained
snapshots it reconstructed fourteen kernel-function instances, declarations and
shims; checked six unchanged frozen-broker copies and all 28 adapter variants;
and verified all 84 exact test outputs, 84 empty fixture compile logs and six
empty standalone compile logs. It also checked source/frozen pins, prepared
kernel files and applied-patch hashes. [comparison.json](comparison.json) records
the sanitized counts and private audit-script hash. Raw source exports, binaries,
scripts and logs stay ignored under `private/`. Linux native-wrapper build/link
evidence is separate from these host tests; it is not syscall execution evidence.

## Remaining limits

The adapter neither opens nor claims an endpoint. It provides no original
baseline authority, native process stop proof, native consumption, writer
exclusion, two-file host restoration, durable journal, recovery coordinator or
restart permission. Driver/module identity, same-open-description custody,
callback deadlines, caller memory ownership and final process lifetime remain
external. A recreated broker cannot recover lost UNKNOWN accounting from STATE
alone. All runtime gates remain closed.
