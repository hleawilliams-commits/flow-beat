# Focused retained CLAIM-owner host evidence

Two fresh matrices pass: primary and one independent invocation. Each exercises
14 positive selectors, with one final compound functional check per selector,
and 12 strict compiling controls in each plain, ASan/UBSan and TSan mode.
Both matrices and the retained-file audit passed on their first invocations.
No passing parent suite was repeated.

[Primary](host-result.json) and [independent](host-result-independent.json)
results are byte-identical. The [comparison](comparison.json) records their
retained-source/log audit. Both result files have SHA256:
33494de42b49aaaff89b1b3e3cf19aa41be5b83dd453da59e9e7e79a9bb668b9.

Compiler: Apple clang 16.0.0 (clang-1600.0.26.6), arm64-apple-darwin25.2.0.
Each invocation records 42 clean compiles, 39 clean links and 78 executions.
The new production translation unit uses -Wall -Wextra -Werror; unused
declarations are suppressed only in the harness including extracted kernel
methods. Every control must exit 1 with exactly its selected functional failure
and expected summary. Compile/link diagnostics, setup assertions, sanitizer
reports, timeouts and extra output cannot satisfy a control. This checkpoint
does not include a Linux ARM64 build.

## New cases

| Selector | Focused observation |
|---|---|
| transfer | Copy the plan before transfer; invalid fd refused; fd0 retained before caller slot consumption; repeated take preserves the other slot; 12 invalid plans return null without I/O. |
| pre-arm | Synchronous STATE-entry inspection sees the transferred original, consumed caller slot and preflight intent; output starts zero. |
| pre-failure | Actual partial STATE copy keeps 104 returned bytes and 104 initialized zero bytes with EFAULT; valid-shape expected-tail disagreement is ESTALE; neither attempts CLAIM or closes. |
| claim-arm | Synchronous CLAIM-entry inspection sees the exact authored 48 bytes and prior raw STATE; actual commit plus scripted stale errno remains Committed; subsequent scripted argument overwrite leaves immutable intent unchanged. |
| claim-input-fault | Actual extracted input-copy failure after 24 bytes produces EFAULT/Rejected without acquisition. |
| busy | Actual intervening CLAIM from another file or a same-file alias makes this attempt EBUSY/Rejected; winning state and retained original remain intact. |
| legacy-race | Actual legacy write between preflight and CLAIM changes content; CLAIM commits, postflight rejects the changed bytes, and Committed history remains independent of readiness. |
| unknown | Actual commit followed by scripted EIO stays Unknown; even exact matching fixture STATE cannot admit postflight or promote it. |
| classification | EACCES, positive and abnormal negative results scripted before or after actual commit stay Unknown; raw returns are preserved. |
| post-fault | Actual partial post-STATE copy keeps all 208 initialized/returned bytes, EFAULT and independent Committed outcome. |
| post-mismatch | Twelve scripted changes cover both instance halves, run, epoch, sequence, mode, CRC, version, reserved and raw bytes 96/97/151; all retain Committed evidence without Ready. |
| idle | Fresh idle flags16 become owned/fenced idle flags23 with unchanged bytes; no automatic FRAME or injection. |
| fenced | Unowned fenced idle/injected flags17/25 accept a seeded old maximum sequence; new CLAIM advances epoch, resets sequence and preserves selected mode/content. |
| disposal | Allocated, held, preflight-failed, Unknown and Ready branches consume the owner slot before one held-fd close; raw uncertain close is retained and never retried. |

The invalid-plan oracle observes null/no-I/O behavior, not allocator calls.
Static review confirms validation precedes calloc; no allocator counter or
allocation-failure injection is present. In-flight inspection is synchronous
same-thread observation only. The prior UINT64_MAX sequence is seeded directly
in quiescent fixture state after actual CLAIM/RELEASE, not reached by executing
2^64 operations.

Counts cover owner-mediated syscalls. Direct extracted-driver setup and
diagnostic calls are outside those counters. Partial usercopy failures and
legacy/competing CLAIM transitions execute exact extracted methods; abnormal
syscall results, overwritten command arguments and corrupted STATE outputs are
explicit wrapper scripts. The real input-only CLAIM handler does not overwrite
its command. The disposal fixture removes the modeled original before returning
(-1,EINTR); that tests report/count behavior, not real close resolution. Fallback
synthetic cleanup happens after copied observations and grants no teardown
permission.

## Discriminating controls

Every control mutates only new claim.c in a private variant directory. Frozen
parents and prepared driver sources remain unchanged.

| Control | Oracle selector |
|---|---|
| caller-slot-not-consumed | transfer |
| pre-arm-is-late | pre-arm |
| claim-arm-is-late | claim-arm |
| discard-failed-prestate | pre-failure |
| discard-failed-poststate | post-fault |
| busy-is-unknown | busy |
| access-denied-is-rejected | classification |
| unknown-can-postflight | unknown |
| terminal-erases-committed-claim | legacy-race |
| poststate-tail-not-compared | post-mismatch |
| omit-original-close | disposal |
| mutable-claim-journal | claim-arm |

[Controls](controls.json) retain exact old/new text and failure labels.
[Inventory](test-inventory.json) binds all selectors and check counts. The
Unknown control really permits a further STATE; the mutable-journal control
passes authored storage to the scripted overwrite; the tail control omits only
the last comparison byte. The disposal control omits close while freeing owner
storage normally, then fails before fallback fd cleanup.

## Source provenance and retained audit

Each fresh private run captures seven new inputs and ten frozen parent inputs.
The same pinned local emulator source is freshly exported and the three frozen
patches applied without installing or running a module. Seven driver methods
are extracted exactly: instance initialization, input/output usercopy, final
release, owned-next checking, ioctl and legacy write. Ten generated inputs
include the exact Linux UAPI and explicit host Linux ioctl/type shims.

The private retained-file audit independently reconstructs method/declaration
extraction, shims, copied headers and all 12 variants. It verifies exact
frozen/generated/prepared-module file inventories, patch receipts, every
recorded compile/link/run log and both public result copies. Its SHA256 is:
c306795049074884c29678ce6db8046d3c1f61a646a2553f72be890c1048371c.
It reads retained files only; no test is rerun.

Before either matrix, independent source review narrowed current-ownership
wording, added invalid-prestate and mutable-command coverage, and checked all
12 control paths. A separate optimized-Python refusal check confirmed runner
and audit stop before creating a run directory. These were pre-run checks,
not failed matrix attempts. Historical parent failures remain untouched.

Raw exports, binaries, logs and the audit script remain ignored under private/.
No emulator/device endpoint, runtime scripts, firmware, credentials or music
are published. This standalone component is not yet composed with the retained
adapter, broker or dispatcher. Matching state and historical Committed evidence
do not establish current exclusive ownership, authoritative original baseline,
native stop or recovery. Same-file aliases may change ownership after CLAIM.
Concurrent/durable custody, process-death recovery, checked graph destruction,
native timing and product release gates remain unresolved.
