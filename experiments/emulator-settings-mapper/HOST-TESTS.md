# Host validation of the inactive mapper

The final primary run completed production `cargo check --locked --offline`
and **27 focused tests** in the actual pinned preview crate. The **162 inherited
tests** were compiled and listed, then filtered out. No application main,
window, settings file operation, worker, backend, emulator or physical device
was run.

The production build reported 81 warnings for unused/inactive code paths; this
is not a warning-free build. Execution used Homebrew Rust 1.98.1 / Cargo 1.98.1,
LLVM 22.1.8, on aarch64-apple-darwin. Linux/Windows execution, sanitizers, native
receipt authenticity, callback ordering and physical timing are outside scope.

## Focused cases and controls

The independent fixture author used a separate hardcoded 51-offset oracle,
the real producer envelope API and synthetic frames. Cases include:

- All 256 values for the complete analog/shared bytes, knob byte and selector;
  exact signed TIME byte patterns and all selection/saved-extra combinations.
- Every unsupported in-frame offset, oversized/overflowed keys, all supported
  duplicate keys, aliases, malformed values/objects and trailing content.
- Non-JSON whitespace, invalid UTF-8/BOM/NUL, exact input-size boundary,
  absence versus present-empty bytes and byte-value bounds.
- Complete ACK identity across keys, request IDs, sequences and all 152 bytes;
  stable serialization, input immutability and owned result copies.
- Compatibility with the actual pinned preview `mixer_levels` selection and
  `config::parse_byte_map` reader. The Python driver was neither imported nor run.

All **13 source mutation controls** compiled and failed only their selected
test at its designated assertion, with exit 101 and exactly one failure / 188
filtered tests. A compile error, wrong assertion, timeout or runtime diagnostic
does not count as a passing control. Controls remove or corrupt shared-bit
preservation, knob offset, selector masking, TIME byte order, explicit selection,
supported-key checks, duplicate rejection, input bounds, prior-selector safety,
TIME completeness, full ACK retention, complete parsing and numeric range checks.

The [inventory](test-inventory.json), [controls](controls.json) and
[primary receipt](host-result.json) bind the exact sources, toolchain, outcomes
and raw-log hashes. Full source exports, logs and binaries remain private.

## Initial harness failure retained

[The initial failure](initial-failure.json) is not a completed passing result.
Its candidate passed all 27 tests and the first 11 controls met the strict
criteria. The trailing-data mutation failed the intended assertion for
`{}{}`, but that custom `assert!(predicate, message)` printed only its custom
message. The runner requires an explicit assertion diagnostic and stopped
before the number-range control.

Only that fixture changed to `assert_eq!(predicate, true, same_message)`.
The exact predicate, production source, strict runner and all refusal policies
were unchanged. The final artifact was then validated in a fresh tree.
The original inputs and logs are retained; no completed result was retroactively
assigned to the interrupted matrix.

## Independent reproduction

A fresh independent reproduction passed the same production check, 27 tests
and 13 controls. The source/receipt audit reconstructed 28 variants with 22
files each and verified captured inputs and exact assertion locations. Every
result field agrees after excluding only 29 raw-log hashes (28 differ, one
agrees). The complete JSON files differ. Details are recorded in
[the independent receipt](host-result-independent.json) and
[comparison](comparison.json). The latter specifies the exact diagnostic-hash
exclusions used to compare functional results; whole JSON equality must not be
inferred from functional agreement.

These checks establish pure mapping and parser behavior in the host crate.
They do not establish real persistence, file-writer exclusion, restoration,
native event consumption or runtime readiness. All existing gates remain closed.
