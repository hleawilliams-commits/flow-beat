# Set Log

An AZ-MODS community mod for the XDJ-AZ. Writes every track you play to a CSV on your USB stick: your tracklist, ready after the gig.

A track counts once it has played for 30 seconds in one go, so cueing and previewing don't. Each one becomes a line in a CSV in MODS/community-mods/set-log/data on the stick: number, time into the set, clock time, deck, artist, title, BPM, length and rekordbox id. A new file starts after a reboot, after 30 minutes with nothing playing, or from Start a new set in the Mod Menu.

Mod Menu rows: on/off, a short note on screen when a track is logged, and Start a new set.

Reads deck state through the AZ-MODS core and writes only its own data folder: no hooks, controls, LEDs, audio or network.

Test build: validated and run under the SDK harness (plain, ASan, TSan), not yet run on a deck. Services only, `"firmware": ["any"]`, no hooks. GPL-3.0-only.
