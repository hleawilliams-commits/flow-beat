# Full-frame action policy

These are pure rules for supplied host mixer settings and two copied emulator
mixer frames. They perform no I/O, read no clock and grant no input permission.
The passive task observer remains uninstalled.

`parse_mixer_state` accepts a missing file or bounded UTF-8 JSON with unique,
canonical decimal keys. Only the exact pinned driver's 28 mixer offsets are
allowed, and FX ON must be released. Booleans, fractional values, unknown
positions, button/counter/CRC/tail overrides and duplicate keys are rejected.
The constructor revalidates direct dictionaries and the exact supplied idle
frame, so callers cannot bypass the parser with unchecked settings.

`validate_frames` requires two immutable 152-byte frames, independently checks
each CRC over bytes 0–95, and requires the same counter in 1–254. Every byte
must match the planned neutral frame except counter byte 34 and CRC bytes
96–97. This includes all 24 bytes beyond the earlier 128-byte diagnostic.
The planned neutral shape independently checks protected template bytes,
released controls, X-PAD and FX. Matching corrupted plans do not make held
controls acceptable.

The initial kernel placeholder differs from the driver's neutral template.
A valid CRC and counter alone do not authorize overwriting that difference.
This policy rejects the mismatch; it never normalizes an observed frame by
sending a replacement. CRC does not cover the tail, and a matching full frame
is an observation of the offered controller state, not native consumption.

The [test result](result.json) has **245 checks, including 222 strict rejection
controls**. Controls cover all 149 compared positions, all 24 additional tail
bytes, held controls, malformed settings and both CRC bytes. The test extracts
the raw templates locally from exact pinned sources; those templates are not
copied into the repository. Independent execution produced identical JSON,
SHA-256 `6e979791eab73cd8b41b3ac7bca40f71c10fd179f329ba170f72b6ca527e7171`.

```sh
python3 test-policy.py --driver /private/path/az-drive.py \
  --frames /private/path/subucom_frames.h --result /tmp/action-policy-result.json
```

Freshness, same-process/run binding, serialization, host-file ownership,
readiness and release/recovery are separate coordinator obligations. Neither
this result nor saved diagnostic frames authorize a later input. No native
observer, input, service change, hardware test or release occurred here.
