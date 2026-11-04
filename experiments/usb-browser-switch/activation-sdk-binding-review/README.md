# USB passive binding: slot type and installer lifetime

The frozen [offline entry observer](../activation-entry-observer/) is not directly
compatible with the SDK original-output parameter: its two original symbols are
`std::atomic<uintptr_t>` objects, while the pinned `claim_hook` takes `void **`.
The earlier size/alignment checks establish the selected target layout; they do
not make an atomic integer object a pointer object that the SDK may write through
`void **`. This review leaves those tested source bytes unchanged.

A separate compile-only probe checks this type boundary against the exact SDK
header and frozen observer header. The first compilation passes all three
expected outcomes: the plain pointer slot compiles without diagnostics, direct
atomic-slot passage is refused at its exact call, and an intentionally wrong
SDK signature fails its named static assertion. Both refused cases produce no
object. The one 1,504-byte AArch64 relocatable object is never linked or executed.
No shared library, loader descriptor, claim adapter or installation is supplied.

The [probe](type-probe.cpp) and [runner](run-compile.py) use the pinned, networkless
ARM64 GCC 7.5 image. [Sanitized results](result.json) retain five input hashes and
eight command receipts. An [independent retained-byte audit](verification.json)
passes all five inputs, eight commands, sixteen raw outputs and the exact
41-file compile inventory, plus both static extractor attempts and all 110
source blobs. It imports no runner and replays no compiler, test or ELF decode.
No original wrapper or installer suite was repeated.

## Existing eligibility, distinct live ownership

The [retained policy result](../activation-ownership/static-validation.json)
already accepts both `0x1be7e38` and `0x20a1550` at the one/four-word instruction
policy widths. Their complete extents and ABI are retained in
[activation-abi](../activation-abi/). Those passing checks are not replayed.
The Waveform entry and executable virtual-slot refusals remain unchanged.

Pinned `src/azmod/community.c:1226–1273` checks module ownership, init phase,
manifest hook permission and code range before locking. The loader checks the
exact firmware earlier at `community.c:1834–1838`; the claim also refuses an
any-firmware module. The overlap check and installation are serialized under
`g_cm_mu`. Its overlap interval is sixteen bytes even when the late installer
redirects one instruction.
Static eligibility is not evidence of an available island, a current unclaimed
entry, the actual loaded core revision, or a successful future claim. A source
literal search cannot establish exclusive live hook ownership. The bounded
search of 110 pinned production source blobs (3,359,025 bytes) found no matching
0x/EP_ address literals under that overlap relation; computed/pattern-derived
targets and independently loaded hooks remain outside that lexical result.

The [static receipt](static-evidence.json) adds only the service continuation row
at `0x20a1554`: CFA is SP+272, x29 is at CFA−272 and x30 at CFA−264. The action
row at `0x1be7e3c` is reused from retained metadata: SP+144, x29 at CFA−144 and
x30 at CFA−136. No full native function or eligibility suite is revalidated.
An initial extractor launch used the symlink-resolved base Python and failed
importing pyelftools before reading the ELF; the unchanged script succeeded
with the existing venv launcher. Both attempts remain retained privately.

## Exact slot publication and process lifetime

The source revision is `3e0c5cb010454228f4ee63226b60d060494eeaae`.
`azmod-mod.h:204` declares the `void **orig` parameter. The intended future
binding uses one permanent aligned `void *` object per hook, passes that exact
object address to the SDK, and has the assembly load that same symbol. C/C++
diagnostic reads would use `__atomic_load_n(&slot, __ATOMIC_ACQUIRE)` under the
selected compiler/target contract, not an aliasing cast to `std::atomic`. No temporary slot and post-claim copy is
acceptable: an entry may execute before `claim_hook` returns.

For the selected late island path, `inline_hooks.c:335–337` stores the trampoline
in `orig` with relaxed atomic ordering before the release entry-instruction
store and cache flush. A protection-restore warning after redirection remains a
successful installation. The existing [installer review](../../mods-shortcut-delivery/task-hook-lifetime/)
retains the detailed resource-failure and partial-claim evidence; its suite is
not replayed here. Instruction fetch is not a C++ acquire of the entry store.
The native cross-core code-patching/cache contract remains unproved by this
source review or by compiling a compatible output argument.

`azmod-mod.h:453–458` and `community.c:1804–1808,1853–1856` retain opened module
code and successful hooks even after init failure. Refusal of a second claim
must leave the first slot, wrapper and helper storage valid for the remainder
of the process. There is no retry, slot reset, unpatch or reclamation permission.
Closing capture does not prevent the original native call or drain admitted
helpers. No native object lifetime follows from retaining the observer itself.

## Capture close must remain terminal

The frozen helper exposes a reusable `set_ready(bool)` offline-helper interface.
A future installer must not confuse that interface with a terminal lifecycle
protocol. In particular, a separate state CAS followed by `set_ready(true)` has
a window: close can win after the CAS and then be overwritten by the later bool
store. This is a source-level schedule, not an observed runtime failure.

Use one authoritative capture state directly checked by the helper, with
initialization-to-ready CAS and terminal closed/failed states. Separate logical
installer progress may record which claims succeeded, but cannot reopen capture.
All other fallible preparation precedes claims, and successful original slots
remain independent of recording readiness. Repeated init must not reconstruct
storage or claim an entry again. A helper already admitted before close may
finish its bounded record publication; close still supplies no quiescence.

## Next offline implementation and closed gates

The next bounded change is a separately versioned SDK-compatible slot/capture
state composition. Preserve the 800-byte frame and independent register-only
records. Test exact permanent slot identity, callback-before-claim-return,
first/second refusal, repeated initialization, close during installation,
late admitted publication and no reopening. These schedules require their own
actual-source checks; no result is claimed for that unimplemented composition.

Action x2 stays opaque and service Source stays register-only. No native pointee
or post-return read, return interception, pending TLS or cross-entry pairing is
added. x16 tail scratch, installer x17 effects and anonymous island/trampoline
unwind remain separate limits. Even correct native +4 continuation metadata
would not supply metadata for an anonymous trampoline PC.

Actual live receiver/row/provider/argument provenance, hook ownership, queued and
reentrant owner scope, callback quiescence, source availability/readiness,
writer exclusion and stopped recovery remain open. Main/live dispatch and all
activation, stems, physical timing and release gates remain CLOSED. No emulator
or physical deck access, core/package/catalog change or release occurs here.
