# Mix Bookmark 0.1.1

Emulator preview. **MIX BOOKMARK** sits above the waveforms next to the phase strip.
In two-deck view the button is labelled **MIX** and sits beside the compact **PICKS** button.
Both layouts keep the buttons above the waveforms, with separate hit areas.
Tap it to review two loaded decks. Select a recipe for each side, then **SAVE THIS PAIR**.
The panel is also under Mod Menu > Mix Bookmark. Close (or physical BACK) returns to the waveform.
**Next saved mix** cycles through saved pairs, newest first.

A bookmark saves title, artist, content ID, deck number, current BPM, playhead position,
aggregate stem availability/on state, timestamp, and a **manual recipe note** for each track.
Choices: Full track, Vocals, Instrumental, Drums, Bass. These notes do not read individual
pad states or change audio. Load tracks and set pads manually when recreating a pairing.
There is no automatic recall, cue movement, or key analysis. Matching BPM/key is a starting
point for listening, not a guarantee that vocals, phrasing or rhythm will work together.

The two identities must remain stable for a second; the save action rereads them and refuses
an empty deck, same deck twice, or a changed identity. SDK fields are individually sampled,
not a sample-accurate snapshot; positions are approximate and source USB identity is not
exposed. Title/artist help distinguish tracks with overlapping library IDs.

## Files and recovery

Only the mod's SDK `data_dir` is written. On the current emulator this is
`MODS/community-mods/mix-bookmark/data/`. The emulator push helper still mirrors packages
to legacy `community/`; this demo also places this mod in the current `community-mods/`
folder so the documented SDK storage service can find it. No core patch is needed.
Each `mix-NN.book` is a versioned fixed-size record with a checksum. Maximum 64 records.
The single writer scans and validates existing files before saving. It writes and fsyncs a
new temporary record, then renames it to the unused slot. Existing bookmarks are never
intentionally replaced or discarded. Full/corrupt storage and an interrupted pending file
refuse further saves; keep a backup before manual recovery. Checksums detect accidental
corruption, not hostile tampering. Directory fsync is attempted; filesystems that do not
support it cannot provide that additional durability guarantee.

The worker reacquires the current SDK data path on each pass and pins its directory with
an open file descriptor. A queued save is bound to the displayed directory's device/inode;
if that changes, save is refused. UI callbacks do no file I/O. Removing the mod can remove
its data folder: copy it elsewhere first. Binary format v1 is little-endian and uses the
current ARM64 record layout; future schema changes must preserve v1 reading or migrate it.

## Validation

```sh
python3 ../../az-mods-sdk/tools/azmod-pack build .
clang -std=c11 -D_DARWIN_C_SOURCE -Wall -Wextra -Werror \
  -fsanitize=address,undefined -g -I../../az-mods-sdk/include \
  tests/test_bookmark.c -lpthread -o /tmp/test-bookmark
/tmp/test-bookmark
```

164 focused checks cover snapshot stability, changed identity, invalid BPM, distinct decks,
recipe validation, checksums, truncated records, symlinks, pending-file refusal, persistence,
full storage, invalid descriptors, and preservation of old saves.

The pinned SDK harness incorrectly reads its global UI context from `data_dir` calls on
mod worker threads. This contradicts its API.md thread-safety table and also causes a TSan
race in the harness. `tests/harness-worker-directory.patch` fixes only that classification:
workers bypass the UI-context check while keeping ID and lifetime checks. Apply it to an
**isolated copy** of the SDK, then run `make test AZ_SDK=/path/to/that/copy`.
Plain, ASan/UBSan and TSan pass 8 gates each with that fix. Stock harness failure is retained
in the validation records. On macOS no-I/O/no-allocation gates are not measured. Physical
deck testing remains pending. See the parent handoff for emulator verification evidence.

This community package contains no official core or stems engine. Disable/remove this mod
through Community Mods to remove its UI. Keep bookmarks separately before uninstalling.
Follow the parent AGENTS.md recovery requirements before any physical USB installation.
