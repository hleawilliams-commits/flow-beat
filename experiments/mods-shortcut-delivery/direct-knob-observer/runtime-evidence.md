# Refused Waveform function-entry observer

The emulator attempt used the unpublished ARM64 binary with SHA-256
`0e2e847a5f9ea3b0c92ef5122b2ed02482514e36a1968f4e9730272eb381a661`.
It staged community files and an emulator-only opt-in, then restarted the
localhost emulator. The physical deck was not accessed or changed. Both original
emulator core hashes matched the saved baseline during installation.

The saved probe log contains these three diagnostic lines:

```text
claim_hook 0x1b3ab60 refused: the hook installer declined it
WKO disabled: hook refused; no retry or takeover
not loaded: Its init() returned -20
```

There are no native entry records in that log. No native knob, navigation or
playback test sequence was sent after the refused initialization. This attempt
therefore provides no argument/return observations, live knob provenance,
counter-order evidence, queued-event ownership or focus-handback proof.

The [corrected static verification](install-boundary-result.json) explains the
refusal in the pinned source: `azm_patch_ok(func, 1)` checks all four initial
instructions before attempting island allocation. The exact third instruction
is a disallowed `TBZ`, so the function-entry installer returns `AZM_DECLINED`.
The one-instruction relocation path is never reached. Neither an island shortage
nor ownership conflict is required to explain this result; no hook-policy change
or retry was made.

## Readiness error retained

The private installation harness accepted a status file merely containing the
mod ID. A failed-mod row also contains that ID, so the script printed “Probe
installed” and saved an installation record despite `init()` failing. Those
records establish that staging and restart occurred; they do not establish a
loaded probe or accepted hook. Future readiness checks must require the exact
successful loaded row and positive hook acceptance, and must reject this saved
failure pattern before permitting any test inputs.

The build result's `emulator_installation: false` describes its earlier build-time
state. It must not be used to deny this later failed staging attempt. The original
raw log and installation records remain private and have not been overwritten to
make the attempt look successful.

## Historical cleanup interruption

Baseline restoration was **unconfirmed** when the refusal correction began. The attempted
restore command was not executed because automatic approval review could not
complete when workspace credits were exhausted; that was a review failure, not a
determination that restoration was unsafe. The local record set had no completed
restoration receipt at that point. Staged files, registration entries and opt-in
removal could not then be assumed. Cleanup through the approved path was required
before any further emulator experiment.

The later preflight found legitimate development changes, requiring a fresh
snapshot and selective cleanup instead of restoring the obsolete October 17
community set. The verified result follows below. Further source review also
found that the pinned SDK's virtual-slot service rejects this exact executable
segment; simply retrying through `claim_vslot` is not an eligible follow-up.
Neither claim policy was changed.

## Selective cleanup verified — October 22

The approved localhost preflight succeeded on this continuation. It found newer
legitimate emulator packages and settings, so the October 17 snapshot was not
restored. A fresh private backup bound current bytes, modes, inventory, core
hashes, loaded mods and envelope caches before any mutation. An independent
review checked the selective cleanup script and its failure/restart handling.

Cleanup removed ten exact probe-owned paths (two package directories and their
six files, the owner record, and the service opt-in) and removed matching tokens
from two registration lists. Verification preserved **146 remaining regular
files**, with only those registration-line edits permitted. The current loaded
set, both original cores, configs, caches, prior absent paths and shared logging
flag matched the fresh snapshot. The probe is unmapped and its environment
opt-in is absent. A clean empty-deck restart completed and the normal startup
guard expired without intervention. The emulator was released after verification.

The obsolete restore entry now refuses execution, and the private install
harness rejects failed loaded rows. The earlier failed installation and blocked
cleanup remain historical evidence; no successful native knob observation is
claimed. The sanitized [cleanup result](cleanup-result.json) records scope.
Private backups, raw logs and scripts remain excluded from Git. The physical
deck was not accessed, restarted, reloaded or controlled in this continuation.
