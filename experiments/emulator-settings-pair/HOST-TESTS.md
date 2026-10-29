# Focused host verification

The final primary build and focused matrix pass on `aarch64-apple-darwin`
with rustc 1.98.1 (Homebrew, LLVM 22.1.8) and Cargo 1.98.1. This is a new,
inactive Unix file-pair component in the actual pinned preview crate. No
application main, window, socket/controller backend, emulator or device endpoint
runs. All filesystem operations use synthetic files under an explicit fresh
private test directory. There is no sanitizer, Linux/Windows execution, native
consumption, crash recovery, physical timing or power-loss durability claim.

## Exact preparation and execution

The preparer verifies the frozen owned-exit parent and its three earlier patch
stages, including the pinned preview dependency lock. It verifies 18 inherited
candidate files before applying only the inactive module declaration and pinned
direct Unix libc dependency. The resulting 20-file inventory includes the two
new Rust files. Fifteen inherited files are byte-identical; only main.rs,
Cargo.toml and Cargo.lock change. All actual application routes, settings helpers
and main's absent owned pair remain inherited.

The runner first performs `cargo check --locked --offline` for production cfg.
It then compiles/list-checks each candidate/control test binary against the
reviewed inventory. Production compilation succeeds with expected dead-code
warnings for inactive modules; it is not a warning-free build. The binary lists
**162 tests**: the **24 new tests** run, while **138 inherited tests** remain
filtered. Each of **eight strict controls** compiles and executes one selected
test, with exit 101, exactly one failure, 161 filtered and the precise reviewed
assertion file/line/column. Build failure, timeout, wrong assertion, unexpected
runtime diagnostic or a different count does not count as control success.

[host-result.json](host-result.json) records the final primary matrix, including
all source pins and private log hashes. Its SHA-256 is
`7d27671d43767c86b0e5db85768d567fdc025dcef645785a6f7112d8828be6c0`.
Independent reproduction and raw-artifact comparison are recorded separately
in [REVIEW.md](REVIEW.md) and [comparison.json](comparison.json).
The fresh independent run passes the same 24 cases and eight controls. Every
source/compiler/inventory/outcome field agrees; only the 19 explicitly listed
raw log hashes are excluded. All nine complete paired test logs also agree
after the separately enumerated private-root/thread-ID/elapsed normalizations.
This is functional agreement, not byte-identical result files or binaries.

## Cases and controls

The tests use the actual `Producer` to construct typed envelopes and complete
ACKs. They do not authenticate a live ACK source or map frame bytes into settings.
The target strings are deliberately opaque synthetic data.

Coverage includes original presence/absence/empty distinctions; preserved bytes,
modes and replaced inode identities; sequential ACKs and read-only duplicates;
full key/order/sequence validation; changed duplicate payloads and target bytes;
file and target size bounds; finite sequence and request-ID admission; exclusive
temporary collisions; content/identity/mode/presence drift; both replacements
and final pair readback; partial second-file failure; uncertain visible rename;
panic/poison; diagnostic inspection without lock reentry; evidence after writer
Drop; and retained directory identity after pathname replacement.

Fourteen before/after boundaries on the first file cover create, write, mode,
file sync, rename, directory sync and readback. These inject errors around real
operations. They do not substitute actual kernel partial-write/fsync failure
returns. A separate second-file failure demonstrates the retained mixed pair;
no rollback or native FRAME replay is performed. Panic after rename retains an
armed Pending transaction and later writes remain closed.

Controls remove or corrupt: completed-duplicate drift checking, exact ACK kernel
sequence, duplicate target binding, per-file mode preservation, exclusive
creation, replacement readback matching, final pair verification and terminal
admission closure. The collision control demonstrates a mutated file even when
a later operation refuses it. The readback control demonstrates that accepting
external bytes as verified current state would incorrectly permit completion.

## Retained fixture failures and narrower permission claim

The first full run passed 22/23 and failed a special-mode refusal assertion.
The requested `04600` fixture was later observed as ordinary `0100600`. A strengthened
second full run also passed 22/23, now failing an explicit actual-mode check
**before production attach**: requested `04600`, observed `0600`. Neither run reached
the controls. Both source snapshots, binaries, logs and synthetic directories
remain private; [initial-failure.json](initial-failure.json) preserves the
sanitized evidence instead of replacing it with a later passing result.

A separate-agent Python probe and actual Rust permission probe retained all
requested bits. One bounded repeat of the original selected test passed there;
[permission-review.json](permission-review.json) preserves that observation.
It did not explain the failure. Using that exact Rust probe binary under the
root launcher reproduced masking of `06000` with both inherited and runner-filtered
environments, while `01000` remained. This establishes a launch-context difference;
its underlying mechanism is unproven and it is not a general macOS claim.
No escalation or alternate-context full matrix bypassed the observed restriction.

The final fixtures separate two claims explicitly. Five actual filesystem
refusal cases cover symlink, hardlink, directory, oversized file and sticky mode,
with exact pre/post mode assertions. One additional test calls the actual
`validate_identity` on copies of ordinary metadata with `04000`, `02000` and `01000`
individually set; the real file stays `0640`. **Real attach rejection of files with
setuid/setgid bits is not proved.** No production code or permission policy was
changed, and no runtime assertion, skip, capability fallback or retry hides a
missing fixture precondition. The final matrix has 24 selected tests because of
this explicit separation. The underlying launch-context mechanism remains open.

## Reproduction and limits

From the repository root, with the pinned emulator source and cached host Cargo
dependencies available:

```sh
python3 experiments/emulator-settings-pair/run-tests.py --emu-source ../xdj-az-emu
```

The preparer always creates a new ignored private export. The runner requires an
explicit isolated filesystem root for each test variant; there is no default
live settings directory. Tests retain their temporary files and evidence rather
than erasing partial outcomes. Raw logs, binaries, generated source, scripts and
synthetic filesystem trees remain ignored.

The final request-ID MAX-minus-one operation, real partial syscall failures,
cross-UID/GID filesystem ownership, concurrent outside writers, ACL/xattrs,
physical storage behavior and whole-process death are not demonstrated. The
same-process journal cannot survive the last handle or process disappearing.
Identity checks do not make ordinary rename conditional on an observed inode,
and two successful renames are not atomic as a pair. External writer/reader
exclusion, validated serialization, the actual SettingsSink bridge, both-file
restoration, authoritative native stop/baseline and guarded restart remain open.
All runtime, stems staging and release gates stay CLOSED.
