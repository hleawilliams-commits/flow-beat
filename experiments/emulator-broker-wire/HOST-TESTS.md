# Focused offline validation

The new C codec/dispatcher and actual preview MixerBackend are tested with
synthetic state and private host streams/pipes. No emulator, controller endpoint,
native ioctl or physical deck is involved. Main remains unwired.

## Scope and inventories

- Production host crate compiles with 162 inactive-code warnings. It contains
  272 test names: 20 new wire tests execute; 252 inherited tests are listed and
  filtered. Dependencies and all prior implementation files remain pinned.
- The 11 new C cases perform 5,248 checks in each plain, ASan/UBSan and TSan mode.
  Five compiling C controls must produce exactly their declared FAIL labels,
  check counts and exit1 in every mode. A sanitizer diagnostic, crash, timeout
  or compiler error cannot count as a successful control.
- Seven compiling Rust controls must fail exactly one selected test at the
  declared functional assertion, with exit101 and all other names filtered.
  No compile error, incidental panic or deadlock can satisfy a control.
- The three production C translation units compile separately with
  -Wall -Wextra -Werror and empty diagnostics in every sanitizer mode.
  The private pipe fixture compiles without the unit-only MBW_TEST seam.

Both language implementations are checked against independent append-only byte
oracles, including full-width identities, signed fields and all 152 frame bytes.
The C cases exercise actual frozen mb_create, mb_adopt and mb_submit. The
dispatcher journals before its synchronous backend callback, distinguishes exact
cached requests from conflicts, and refuses insufficient output capacity before
a broker call. Every receipt identity field and raw frame/tail participates.

The Rust integration launches the real C dispatcher/broker fixture through
private stdin/stdout pipes. Its six modes cover commit, definite rejection,
unknown, EOF after a synthetic commit, cached receipt retrieval and a positive
receipt carrying a synthetic nonzero API result. The fixture process is joined;
that synthetic process lifecycle does not establish native adapter disposal.

## Counterexamples retained by the controls

The controls discriminate loss of full command correspondence, acceptance of
cached or API-error-positive replies, false rejection of an already committed
duplicate, loss of pre-I/O armed state, and loss of returned progress/error
evidence under journal poison. Guest controls additionally discriminate cached
provenance, API-error advancement, the final command byte, pre-callback journal
arming and output-capacity preflight.

The C unit-only return seam calls actual mb_submit first, then injects malformed
receipt fields or MB_ERR_MUTEX. The pipe api-error mode changes only result bytes
after a real synthetic dispatcher commit. Neither induces an actual OS mutex
unlock failure. The tests establish conservative classification of that possible
return, not its physical frequency or a live kernel outcome.

A reviewed source draft would have called a previously committed duplicate a
local definite rejection. That was corrected before the first matrix: all local
preflight refusals now remain Unknown without I/O. Review also required retaining
trait returns through journal poison. Successful progress and raw I/O errors are
preserved diagnostically while acceptance stays closed. The dedicated controls
make those distinctions observable.

## Custody and I/O limits

Invalid construction retains an inert stream owner. A caught write, flush or
read panic leaves an armed request, and subsequent calls cannot resend it.
Independent evidence remains available during blocked I/O. It does not retain
the stream after the backend itself is dropped, and copying evidence is never a
permission to dispose of native state.

A buffered stream is explicitly flushed once. Positive short I/O continues the
current record offset; error, zero progress, malformed/truncated data and cached
responses close the wrapper. Progress is measured at the trait boundary.
Counter boundary tests use synthetic state/local values, not prolonged live
operation. An unread suffix is not drained or authenticated. Rust tests are
nonsanitized host tests; C TSan cases do not prove whole-system concurrency.

## Reproduction and retained evidence

Run the public run-tests.py with --emu-source pointing at the pinned local
xdj-az-emu checkout. It uses locked, offline Cargo and creates a fresh private
export. Public candidate pins cover 30 actual-crate files; preparation captures
40 frozen parent inputs, 11 preparation inputs and eight C source files. The
runner captures 15 inputs. Only an inactive main module declaration changes
among the 28 parent files.

Both fresh matrix invocations passed first time. The retained-file audit
reconstructs all 16 Rust variants and 12 C variants (each C variant compiled/run
in three modes) across the two captures. It verifies source/input inventories,
exact assertion failures and logs. Exactly 63 enumerated raw log-hash fields are
excluded from functional comparison; 17 differ. Every other JSON field agrees;
whole JSON differs. The nine standalone C compilation entries per run are also
verified, along with the plain pipe-fixture build. Full exports,
binaries, command logs and the audit script stay in ignored private/. No
passing parent suite is rerun. The preliminary C smoke build also passed its
5,248 plain checks with no diagnostics; it is not an additional runtime claim.

The host toolchain is Rust/Cargo 1.98.1 (Homebrew), LLVM22.1.8,
aarch64-apple-darwin. C uses Apple clang16.0.0 on arm64-apple-darwin25.2.0.
These are host builds, not a Linux guest installation or native-driver run.

All runtime, stems staging, native ownership, USB and release gates remain
CLOSED. Authoritative acquisition/originals/stopped identity, endpoint and peer
binding, external exclusion, native consumption, recovery and durable custody
remain required before live activation.

Evidence digests:

- Primary result [host-result.json](host-result.json): 842fb0c751c4600b03f02a49c5aa703c4b0efff2eba14200c5f0673d4f10c629.
- Independent result [host-result-independent.json](host-result-independent.json): a37c395bbc0b9992330a1f9e74ce556fbfed06c18460e1618bc28befe8bd643a.
- Retained comparison [comparison.json](comparison.json): 3fefb7d50e236db7aee4b7f0c035c05f928545c979b5315ca9254c59f2b0bc50.
- Private retained-file audit script SHA-256: 5e0df5449205356da2c519711450777b5be9a95cf3cefded731a89b6a595f688.
