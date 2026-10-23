# Set Clock

An AZ-MODS community mod for the XDJ-AZ. Shows SET and the time left in your set, counting down from the set length you pick (1 to 4 hours) once the first deck plays. Amber in the last 10 minutes, red once you run over. In the last minute of the playing track it adds END and that deck's time left, flashing red in the last 20 seconds.

Mod Menu rows: on/off, set length, track warning on/off, position, and Restart set timer.

Reads deck state through the AZ-MODS core only: no hooks, controls, LEDs, audio, files or network.

Test build: validated and run under the SDK harness (plain, ASan, TSan), not yet run on a deck. Services only, `"firmware": ["any"]`, no hooks. GPL-3.0-only.
