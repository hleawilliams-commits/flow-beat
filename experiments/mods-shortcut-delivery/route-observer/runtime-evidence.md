# Route observer: one native sample and complete restoration

On October 22, the localhost emulator accepted one exclusive hook at
`0x1b01cf8` from `encoder-route-observer` 0.0.1, binary SHA-256
`aff3fc9c36d1aa561a34c315dbe967de3a25e03409c3d950db5debf282ece356`.
The exact loaded row, positive ready message and core one-hook message all
passed. The existing cores were retained. This is unpublished observation work,
not a direct menu, USB activation or stems fix. No physical device was accessed.

[The sanitized result](runtime-result.json) records the single installation,
input sequence, scalar status and completed selective restoration. Raw scripts,
maps, process identities, runtime object/module addresses, logs, screenshots and
backups remain private. The published LR is an exact-firmware code return site,
checked separately against the pinned ELF; it is not an object address. The earlier refused Waveform and negative generic-dispatcher experiments
remain separate; their evidence was not replaced.

## Input and native entry

Before input, RAW sequence 1 showed mixer byte 34 equal to 128. The host driver's
saved counter was 133. Sending even a BROWSE frame would have included that stale
host value. A separately reviewed helper saved its exact bytes and mode, aligned
the **host file only** to 128, and verified completion before allowing input.
It sent no guest frame during alignment. This is an observed harness mismatch
in this run, not an explanation of the earlier negative experiment.

Two BROWSE pulses reached a visually confirmed empty four-deck Waveform screen.
One two-detent clockwise action and one two-detent counterclockwise action then
ran in the same process. No LOAD or playback input was sent. The host counter
went 128 → 130 → 128; RAW changes recorded 129, 130, 129, 128. The nine RAW
records comprise the initial baseline, four navigation changes and four knob
changes. They are checked SDK input **before core edits**, not the final native
counter.

The native helper's attempted count changed 0 → 2 → 4. Its one-shot off-thread
mailbox retained one scalar entry during the clockwise action: `w3=1`, `w4=1`,
full-width `x2`, and original LR `0x22c6e08`. The captured Linux TID differed from
the process PID. `x0` and `x1` were copied as opaque integers and never read as
objects. Three further off-thread entries were intentionally skipped after the
mailbox was occupied; their arguments were not captured. There were no main-ring
records, and the retained record's main sequence is zero. This does not prove
that every subsequent entry used the same thread or identify a native owner
thread. Mailbox `off_state=3` means consumed; it is unrelated to filter quarantine.

| Sample | SDK heartbeat | RAW heartbeat | Native attempts | Off-thread skipped | RAW sequence |
|---|---:|---:|---:|---:|---:|
| Before knob input | 15,436 | 17,620 | 0 | 0 | 5 |
| After clockwise | 16,321 | 18,318 | 2 | 1 | 7 |
| After counterclockwise | 17,197 | 18,998 | 4 | 3 | 9 |

State/live were 1 in each sample. Native and RAW state/live gates, queue drops,
reentry drops, invalid RAW input and TID failures were zero in the retained
samples. Capture stayed below both event budgets. The RAW queue drained after
each action. Sequential status, independent lane sequences and worker print
order do not establish a total event order or an input-to-entry bijection.
No native return, child-handler execution, completion, queue ownership or
cancellation was observed. The captured cumulative value 1 is distinct from
RAW byte 34 value 129; do not substitute either for the other.

## Status sampling and harness review

Unlike the historical inline sampler, this run retains the exact sampler,
binary/maps plan, auxiliary-vector page size, before/after maps and every scalar
read. The offline [status-map planner](status-map/) passes 110 checks, independently
reproduced, including three discriminating controls. Its original false-acceptance
findings and correction limits remain documented. These tests do not themselves
establish runtime mapping or transport correctness.

The actual run used a 4096-byte page size and read only 26 named objects from the
exact loaded observer binary: 24 four-byte values and two eight-byte sequences.
All were within file-backed mappings. Each sample checked the binary hash,
mapping plan, process/start identity, loaded status, unchanged cores and empty
decks before and after the reads. Maps and bytes were retained, not native queue
records or argument objects. Individual reads are **noncoherent**. Values such
as RAW attempted and heartbeat can differ because they were read at different
times. No pointer following or post-return native-object reads occurred.

The private sampler's retained AST/mock review passed 289 assertions across nine
scenarios, including short reads and changed hash/maps/process identity. The first
review was inline; its recovered script and transcribed result are labeled as
such. Root separately executed that saved script and retained fresh output.

An initial, never-executed host-counter helper had three review defects: an
attempt receipt could be mistaken for completed alignment, arbitrary observed
values could grant restore ownership, and a host-recovery failure could prevent
a safe retry after guest cleanup. The original sources and 14-assertion review
are retained privately. The corrected helper uses atomic replacement, a verified
completion receipt, exact driver-source binding and final ±2 targets only.
The driver writes the host counter once after both detents; intermediate guest
value 129 grants no host-file ownership. Guest restoration has its own receipt,
so a host-only recovery retry does not reconnect or repeat guest cleanup.
The corrected AST/mock suite passed **82 assertions across 25 cases**, plus a
guard-removal control, independently executed by root. These are offline tests,
not 82 additional emulator trials. The actual runtime sequence had no failed
action or status attempt and needed no recovery retry.

## Restoration and remaining work

The run took a fresh baseline of **147 regular files**, reflecting current
development. Selective cleanup removed only this probe's files, registration and
opt-in; no earlier snapshot was restored. All baseline bytes, tar modes,
inventory, settings, loaded-mod set, caches, both original cores, prior absences
and the shared logging flag were verified. A clean empty restart passed; the
probe was unmapped and its environment opt-in absent. The startup guard expired
naturally. The original host counter file, value 133 with exact bytes/mode, was
restored without sending input. The emulator is released.

The [observed-caller review](caller-review/) checks 71 instruction anchors, five
table words, one RTTI anchor, eight rejection controls and 14 actual pinned
policy assertions. The code return site follows a source-vtable `+0x10` call.
The known encoder notification path restores LR and tail-branches to observer
`+0x18`, so the saved LR is consistent with that route. A live source vtable was
not captured. Two static direct callers do not identify which ran. The earlier
caller has a policy-refused CBZ prologue; the notifier's ADRP is allowed by the
actual instruction policy, without proving full claim eligibility. Its initial
wrong rejection expectation and correction are retained separately.

The positive boundary narrows the next experiment to a separately tested,
bounded multi-record off-thread lane at the existing accepted scalar entry.
Do not rerun this unchanged one-shot sequence to collect values the design
intentionally skips. Keep copied addresses opaque, preserve native forwarding
and exclusive hook ownership, and establish receiver/child identity, ordering,
return behavior and event lifetime before a core-owned menu, focus handback or
view cancellation. A later executor observation needs its own eligibility and
ABI review. First-use TLS cost, hardware timing and whole-engine lifetime remain
unvalidated. The staging gate remains closed, and no community release or
physical installation follows.
