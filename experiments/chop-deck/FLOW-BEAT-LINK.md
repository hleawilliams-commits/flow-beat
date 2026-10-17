# Use the existing FLOW Beat feed with Ableton

FLOW Beat already sends the information Ableton needs for a first tempo-sync experiment. The existing [`tools/flow_link_bridge.py`](../../tools/flow_link_bridge.py) receives the deck's UDP packets on port 9100 and publishes an Ableton Link session on the Mac. It sends no commands back to the XDJ-AZ. This is a timing bridge; it does not carry pad commands, sliced samples, stems, or audio.

The bridge follows the playing deck that started most recently. That is **not** the AZ's native MASTER selection. It sends that deck's BPM to Link and requests whole-beat alignment when a beat packet arrives. Packet timing is measured at arrival; the feed has no source timestamp, and the public SDK does not provide the bar's downbeat. Do not claim sample-accurate, bar-accurate, or hardware-verified sync from the local checks below.

## Local setup

Use a project virtual environment; do not install into Ableton's Python or modify its application bundle. From this repository root, with Python 3.11 available:

```sh
python3.11 -m venv ../flow-link-venv
../flow-link-venv/bin/python -m pip install aalink==0.2.3
../flow-link-venv/bin/python experiments/chop-deck/tools/check_flow_bridge.py --udp-check
```

The check uses synthetic packets and a real `aalink` object with Link networking disabled. It never contacts the deck, changes a USB, or joins a running Link session. The optional UDP check uses localhost and an ephemeral port, not port 9100. Keep virtual environments out of Git.

For Henry's local setup, the isolated dependency environment was prepared outside this repository at `work/ableton-link-venv` in the original task folder. The public repository does not depend on that machine-specific path.

## Run after choosing the reference deck

Start the existing bridge, then enable Link in Ableton:

```sh
../flow-link-venv/bin/python tools/flow_link_bridge.py
```

The bridge reports Link peers when they connect, and a lead deck only when it receives FLOW state packets. Check that the reported deck and BPM are the ones you intend before performing. Stop the bridge with Control-C. It does not start or stop Ableton's transport or any physical deck. With no fresh FLOW packets it holds its last tempo.

Link is a shared tempo session: running the bridge can change the tempo of participating Link apps, including Flow VJ if it has Link enabled. The bridge starts its local Link timeline at 120 BPM, so do not start it during a live set assuming existing peer timing will be untouched. Starting software and seeing a peer count is not proof that the AZ beat feed or audio is working.

## Flow VJ sharing

The mod's default destination is each local network's broadcast address on UDP 9100. A configured `flow-beat:host` can instead make it unicast to one computer. The current bridge requests shared sockets (`SO_REUSEADDR`/`SO_REUSEPORT`), but those flags **do not guarantee duplicate unicast delivery** to both the bridge and Flow VJ. A successful bind alone is insufficient.

Before using both apps, verify that Flow VJ continues receiving its normal state/beat packets while the bridge receives the same feed. If the receiver or packet mode prevents sharing, use an explicit local relay with separate receiver ports or add a forwarder inside Flow VJ; that is a separate integration change. Do not stop Flow VJ, change deck destinations, or replace its receiver settings merely to make the bridge bind. No such configuration change was made for this experiment.

## Checked in this build

- `aalink` 0.2.3 installed in an isolated Python 3.11 environment.
- Existing bridge imported with the installed API.
- Synthetic deck state changed the real disabled Link object's tempo.
- Latest-started deck selection and fallback after a stop were checked.
- Beat packet processing executed against the current Link API.
- The mounted FLOWMATIC USB has FLOW Beat 0.2.1 enabled and no namespaced host/port override, consistent with the default broadcast destination on UDP 9100. This read-only check does not prove the physical deck's current runtime configuration.
- A five-second passive check on UDP 9100 received no FLOW packets. No test packets were sent to the deck or LAN. The end-to-end connection is not yet verified.
- The optional localhost unicast check delivered four packets to only one of two shared listeners, so unicast duplication is not assumed. Loopback broadcast did not arrive on this Mac, so broadcast fanout with Flow VJ remains unverified.
- No Link peer, physical deck, audio path, or Flow VJ session participated in the synthetic compatibility checks.

Live pad chopping and the crossfader are separate from timing. The playable Chop Deck demo prepares that performance workflow; actual XDJ-AZ MIDI and audio routing still require a hardware check.
