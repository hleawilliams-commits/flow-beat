flow-beat 0.5.0 — TEST on the AZ before it goes public (not in the repo yet)
New in 0.5.0: FLOW Mixer Link (faders, EQ, crossfader, Color FX, Beat FX, X-PAD to FLOW)
and Cue Map (hot/memory cues ride with the track analysis). Includes everything in 0.4.0.

1. Plug your AZ USB (the one with MODS/ on it) into the Mac.
2. Copy this folder's  MODS/community-mods/flow-beat  over the one on the USB (replace it).
   Leave MODS/azmods.cfg on the USB as it is.
3. Eject, put the USB in the AZ, power it on.
4. Mod Menu > Community Mods > Installed: "New mod found: allow?" -> Allow -> Reload now.
5. Mod Menu now has 4 FLOW rows: Send beat to FLOW (on), Send mixer to FLOW (on),
   Send track analysis to FLOW (on), Send mixer data (test) (off).
6. On the Mac, in Terminal:
       python3 ~/Desktop/Claude/dj-tools/az-beat/test/mixer-watch.py
   One line updates as you move things. Move, one at a time: each channel fader 1-4, the
   crossfader, master level, one EQ knob, press FILTER (Color FX) and turn its knob, press a
   Beat FX channel button and Beat FX ON, touch the X-PAD. Then Ctrl+C: it lists what moved.
   Tell Claude the list (or paste it). Check the crossfader direction: hard left should read 0%.
7. Load and play a track: beat, phrases and cues should still reach FLOW as before.
   If the deck hiccups at all, turn "Send mixer to FLOW" off and tell Claude.
