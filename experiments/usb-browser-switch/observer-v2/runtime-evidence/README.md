# Observer v2 native switching runtime check

On 2026-10-17, the unchanged observer 0.0.1 ran in Henry’s local emulator with its explicit environment opt-in. Both hook claims were accepted, and the native parent-select and MediaChangeEvent callbacks ran on Linux TID == PID. The observer copied entry state and scalar return values only. **This clears those runtime observation gates; it does not implement USB buttons or establish a correlated completion API.**

[Structured results](runtime.json) pin the exact community binary and both core hashes. [Sanitized logs](observer-sanitized.log) preserve sequence numbers, modes, descriptors, thread IDs and native returns; process addresses are omitted. Raw screenshots, library names, logs and backup files remain private outside the repository.

## What ran

The SDK container build/directory validator passed with the existing verified ARM64 binary unchanged. The observer was loaded through the emulator’s ordinary community-mod staging/approval helper, with normal logging temporarily enabled. Its two claims at `0x1b296c0` and `0x1ffb6c8` were accepted. Both existing test USB images were already mounted; USB2 needed its documented emulator announcement using the verified actual `/media/usb/sdb1` mount after EP147 restarted. No media image was created or replaced, and neither core was changed.

Native SOURCE controls selected USB1 and USB2. Source-row confirmation used the emulator helper’s normal 350 ms browse-knob press; 700 ms holds did not produce the short-select callback. A first USB1 Local Library selection used native touch. Later source selections and Local Library confirmations used short native knob presses. The mod exposed no activation operation and never called a discovered native function itself.

The bounded run produced:

- Eight valid parent snapshots: four in mode 0 and four in mode 1. Each contained both real USB descriptors, `0102` and `0122`, while the cloud and MOD MENU rows had no usable media identity.
- Five valid media-entry records, each followed by its separately correlated scalar result. All returned 0 with `post-read=none`. The initial request entered with no committed medium; subsequent entries alternated requested USB2/prior USB1 and requested USB1/prior USB2.
- PID and every recorded callback TID were 876423. No wrong-thread refusal, queue-drop report or observation-budget exhaustion appeared during the run.
- Native BROWSE visibly showed each USB’s library. After selecting different rows in the same test playlist, USB2 returned to row 3 and USB1 returned to row 2. Screenshots were inspected locally. No deck LOAD or PLAY request was sent; all four decks began empty.

An entry with a previously committed identity is an independently entered historical snapshot. The correlated return value describes the native call’s return only. Neither is labeled a verified completion of the requested switch. Visible browser retention is separate UI evidence and does not remove the lifetime/correlation gap described in [completion ownership](../COMPLETION-OWNERSHIP.md).

## Limits and restoration

Missing/unplugged media, disable/re-enable, already-current and error-return paths were not exercised in this bounded run. Both media use the same existing test export database. Different databases, scrolling and sort restoration remain untested. There was no physical-device connection, installation or control.

Before installation, 122 existing community, approval, settings and service files were backed up privately. The observer and its opt-in were removed, the two modified existing lists were restored, and all baseline file hashes, the original loaded-mod list, and both core hashes were verified after restart. The final process no longer mapped the observer. The core’s normal temporary startup marker was allowed to clear on its own; its protection was preserved. [Structured restoration results](runtime.json) record the completed checks.

The next implementation gate remains a safe, owned value snapshot that associates the requested medium with committed identity and handles stale, cancelled and failed switches. Do not progress directly from this observation run to custom activation.
