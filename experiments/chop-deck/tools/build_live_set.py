#!/usr/bin/env python3
"""Build a private Chop Deck Session set from the user's installed Live templates.

No Ableton template or music is embedded in this source. Output is a separate
project and existing sets are never overwritten. Live's XML is undocumented;
structural validation is not an application/audio/hardware test.
"""
from __future__ import annotations
import argparse
import copy
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import wave
import xml.etree.ElementTree as ET


def value(parent, path, new):
    node = parent.find(path)
    if node is None:
        raise ValueError(f"Installed Live template lacks {path}")
    node.set("Value", str(new).lower() if isinstance(new, bool) else str(new))
    return node


def read_set(path):
    return ET.fromstring(gzip.decompress(path.read_bytes()))


def wav_info(path):
    with wave.open(str(path)) as wav:
        return wav.getnframes(), wav.getframerate(), wav.getnchannels()


def make_clip(template, path, project, name, bpm, number, looping):
    clip = copy.deepcopy(template)
    frames, rate, channels = wav_info(path)
    duration = frames / rate
    beats = duration * bpm / 60
    # Tiny sample-rounding differences must not accumulate in the backing loop.
    end = 16.0 if looping else beats
    clip.set("Id", str(1000 + number))
    clip.set("Time", "0")
    for key, val in {
        "CurrentStart": 0, "CurrentEnd": end, "Loop/LoopStart": 0,
        "Loop/LoopEnd": end, "Loop/StartRelative": 0,
        "Loop/LoopOn": looping, "Loop/OutMarker": end,
        "Loop/HiddenLoopStart": 0, "Loop/HiddenLoopEnd": end,
        "Name": name, "Color": 19 if looping else 25,
        "Annotation": "Four-bar instrumental loop" if looping else "One-shot vocal fragment; click triangle to retrigger. Keyboard and pad mapping are not installed.",
        "LaunchMode": 0, "LaunchQuantisation": 0,
        "Legato": False, "Ram": True, "Disabled": False,
        "VelocityAmount": 0, "FollowAction/FollowActionEnabled": False,
        "IsWarped": True, "PitchCoarse": 0, "PitchFine": 0,
        "SampleVolume": 1, "SampleRef/DefaultDuration": frames,
        "SampleRef/DefaultSampleRate": rate,
        "SampleRef/LastModDate": int(path.stat().st_mtime),
        "SampleRef/SamplesToAutoWarp": 0,
    }.items():
        value(clip, key, val)
    ref = clip.find("SampleRef/FileRef")
    for key, val in {
        "RelativePathType": 3, "RelativePath": path.relative_to(project).as_posix(),
        "Path": str(path.resolve()), "Type": 2,
        "LivePackName": "", "LivePackId": "",
        "OriginalFileSize": path.stat().st_size, "OriginalCrc": 0,
        "SourceHint": "",
    }.items():
        value(ref, key, val)
    # Discard the built-in library file's identity/provenance. The source remains
    # the local user-prepared stem, recorded in the separate asset manifest.
    clip.find("SampleRef/SourceContext").clear()
    markers = clip.find("WarpMarkers")
    markers.clear()
    ET.SubElement(markers, "WarpMarker", Id="0", SecTime="0", BeatTime="0")
    ET.SubElement(markers, "WarpMarker", Id="1", SecTime=str(duration), BeatTime=str(end))
    clip.find("SavedWarpMarkersForStretched").clear()
    value(clip, "MarkersGenerated", False)
    return clip


def configure_track(track, name, color, crossfade):
    value(track, "Name/EffectiveName", name)
    value(track, "Name/UserName", name)
    value(track, "Name/Annotation", "Private computer-first Chop Deck demo; physical XDJ-AZ routing is untested.")
    value(track, "Color", color)
    value(track, "SavedPlayingSlot", -1)
    value(track, "DeviceChain/Mixer/Volume/Manual", 0.7)
    value(track, "DeviceChain/Mixer/CrossFadeState/Manual", crossfade)
    value(track, "DeviceChain/Mixer/ViewStateSessionTrackWidth", 240)
    value(track, "DeviceChain/MainSequencer/MonitoringEnum", 2)
    value(track, "DeviceChain/MainSequencer/Recorder/IsArmed", False)
    track.find("DeviceChain/Mixer/Sends").clear()
    slots = track.findall("DeviceChain/MainSequencer/ClipSlotList/ClipSlot")
    if len(slots) != 8:
        raise ValueError("Expected eight Session slots in installed Live template")
    for slot in slots:
        slot.find("ClipSlot/Value").clear()
        value(slot, "HasStop", False)
    return slots


def validate(root, project, bpm):
    ls = root.find("LiveSet")
    tracks = ls.findall("Tracks/*")
    assert len(tracks) == 2 and all(t.tag == "AudioTrack" for t in tracks)
    assert len({t.get("Id") for t in tracks}) == 2
    assert len(ls.findall("Scenes/Scene")) == 8
    assert ls.find("GlobalQuantisation").get("Value") == "0"
    assert len(ls.findall(".//AudioClip")) == 9
    assert len(ls.findall("Tracks/AudioTrack/DeviceChain/MainSequencer/ClipSlotList/ClipSlot")) == 16
    ids = [e.get("Id") for e in ls.iter() if (e.tag in {"AutomationTarget", "ModulationTarget", "Pointee"} or e.tag.endswith("ModulationTarget")) and e.get("Id")]
    assert len(ids) == len(set(ids)), "Duplicate globally addressable IDs"
    assert int(ls.find("NextPointeeId").get("Value")) > max(map(int, ids))
    refs = []
    for clip in ls.iter("AudioClip"):
        assert clip.find("LaunchQuantisation").get("Value") == "0" # Follow global quantisation.
        assert clip.find("LaunchMode").get("Value") == "0"
        assert clip.find("FollowAction/FollowActionEnabled").get("Value") == "false"
        assert len(clip.find("SampleRef/SourceContext")) == 0, "SampleRef SourceContext is an object list, not a Value wrapper"
        ref = clip.find("SampleRef/FileRef")
        assert ref.find("Type").get("Value") == "2", "Expected native macOS FileRef type"
        assert ref.find("RelativePathType").get("Value") == "3"
        absolute = Path(ref.find("Path").get("Value"))
        relative = project / ref.find("RelativePath").get("Value")
        assert absolute.resolve() == relative.resolve() and absolute.is_file()
        frames, rate, channels = wav_info(absolute)
        assert channels == 2 and rate == 44100 and frames > 0
        refs.append({"file": absolute.name, "frames": frames, "sample_rate": rate, "sha256": hashlib.sha256(absolute.read_bytes()).hexdigest()})
    assert not ls.findall(".//PluginDevice") and not ls.findall(".//AuPluginDevice")
    assert not ls.findall(".//Devices/*")
    assert [t.find("DeviceChain/Mixer/CrossFadeState/Manual").get("Value") for t in tracks] == ["0", "2"]
    assert float(ls.find("MainTrack/DeviceChain/Mixer/Tempo/Manual").get("Value")) == bpm
    assert all(t.find("SavedPlayingSlot").get("Value") == "-1" for t in tracks)
    assert all(t.find("DeviceChain/MainSequencer/Recorder/IsArmed").get("Value") == "false" for t in tracks)
    return {"status": "structural checks passed; not opened or heard in Live", "track_count": 2, "session_clip_count": 9, "bpm": bpm, "crossfade_assignments": ["A", "B"], "launch_quantisation": "Clips follow global; global None", "crossfader_mapping": {"midi_channel": 5, "cc": 11, "hardware_tested": False}, "clip_launch_mappings": "None; use mouse or map manually in Live", "samples": refs}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--assets", type=Path, required=True, help="Private prepared-assets directory with manifest.json")
    p.add_argument("--project", type=Path, required=True, help="New private output project folder")
    p.add_argument("--app", type=Path, default=Path("/Applications/Ableton Live 12 Suite.app"))
    args = p.parse_args()
    project = args.project.expanduser().resolve()
    if project.exists():
        raise SystemExit(f"Refusing to overwrite existing project: {project}")
    manifest = json.loads((args.assets / "manifest.json").read_text())
    bpm = float(manifest["bpm"])
    if not 20 < bpm < 400 or len(manifest["slices"]) != 8 or manifest["backing_beats"] != 16:
        raise SystemExit("Expected eight slices and a 16-beat backing loop")
    resources = args.app / "Contents/App-Resources"
    default_path = resources / "Builtin/Templates/DefaultLiveSet.als"
    sample_path = resources / "Core Library/Ableton Folder Info/Sample Reference.als"
    root = read_set(default_path)
    clip_template = read_set(sample_path).find(".//AudioClip")
    if clip_template is None:
        raise SystemExit("Installed Core Library has no audio clip template")
    ls = root.find("LiveSet")
    tracks = ls.find("Tracks")
    audio = tracks.findall("AudioTrack")
    if len(audio) != 2:
        raise SystemExit("Expected two audio tracks in installed default set")
    for track in list(tracks):
        if track not in audio:
            tracks.remove(track)
    project.mkdir(parents=True)
    samples = project / "Samples/Imported"
    samples.mkdir(parents=True)
    entries = [manifest["backing"]] + manifest["slices"]
    copied = []
    for item in entries:
        source = Path(item["wav"])
        target = samples / source.name
        if target.exists():
            raise SystemExit("Duplicate sample filename")
        shutil.copy2(source, target)
        copied.append(target)
    backing_slots = configure_track(audio[0], "BEAT / A", 19, 0)
    chop_slots = configure_track(audio[1], "CHOPS / B", 25, 2)
    backing_slots[0].find("ClipSlot/Value").append(make_clip(clip_template, copied[0], project, "START BEAT / 4 bars", bpm, 0, True))
    for i, (slot, path) in enumerate(zip(chop_slots, copied[1:]), 1):
        slot.find("ClipSlot/Value").append(make_clip(clip_template, path, project, f"CHOP {i}", bpm, i, False))
    mixer = ls.find("MainTrack/DeviceChain/Mixer")
    value(mixer, "Tempo/Manual", bpm)
    value(mixer, "Volume/Manual", 0.7)
    value(mixer, "CrossFade/Manual", 0)
    tempo_id = mixer.find("Tempo/AutomationTarget").get("Id")
    for envelope in ls.findall("MainTrack/AutomationEnvelopes/Envelopes/AutomationEnvelope"):
        if envelope.find("EnvelopeTarget/PointeeId").get("Value") == tempo_id:
            for event in envelope.findall("Automation/Events/FloatEvent"):
                event.set("Value", str(bpm))
    # KeyMidi is the documented-by-diff Live parameter mapping shape. Its real
    # MIDI channels are zero-based; 4 means MIDI channel 5. No pad assignments
    # are guessed. See the experiment README for the native AZ message source.
    mapping = ET.Element("KeyMidi")
    for tag, val in [("PersistentKeyString", ""), ("IsNote", "false"), ("Channel", "4"), ("NoteOrController", "11"), ("LowerRangeNote", "-1"), ("UpperRangeNote", "-1"), ("ControllerMapMode", "0")]:
        ET.SubElement(mapping, tag, Value=val)
    mixer.find("CrossFade").insert(1, mapping)
    for i, scene in enumerate(ls.findall("Scenes/Scene")):
        value(scene, "Name", "START BEAT + CHOP 1" if i == 0 else f"CHOP {i + 1}")
        value(scene, "Tempo", bpm)
    value(ls, "GlobalQuantisation", 0) # No quantisation; default clips follow global.
    value(ls, "SelectedDocumentViewInMainWindow", 1) # Session, as in native APC demo templates.
    value(ls, "ViewStateMainWindowDeviceDetailOpen", False)
    value(ls, "ViewStateMainWindowClipDetailOpen", True)
    value(ls, "HighlightedTrackIndex", 1)
    value(ls, "Transport/CurrentTime", 0)
    value(ls, "Transport/LoopOn", False)
    value(ls, "Annotation", "CHOP DECK: click START BEAT, then CHOP 1-8 triangles. All chops share a track so they interrupt one another. Main crossfader blends A/B. Keyboard/pad launch mapping is manual; physical AZ audio and MIDI remain untested.")
    for key, val in {"SessionCrossFade": 1, "SessionIO": 0, "SessionSends": 0, "SessionReturns": 0}.items():
        value(ls, "ViewStates/" + key, val)
    ls.find("SendsPre").clear()
    report = validate(root, project, bpm)
    ET.indent(root, space="\t")
    output = project / "Chop Deck Demo.als"
    output.write_bytes(gzip.compress(ET.tostring(root, encoding="utf-8", xml_declaration=True), mtime=0))
    # Verify the actual compressed output, not only the in-memory document.
    validate(read_set(output), project, bpm)
    report["set_sha256"] = hashlib.sha256(output.read_bytes()).hexdigest()
    report["templates"] = [str(default_path), str(sample_path)]
    (project / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    (project / "READ ME.txt").write_text("CHOP DECK — private Ableton Session sketch\n\nOpen Chop Deck Demo.als in Live 12. Select a working audio output. Press Tab if Session clips are not visible.\nClick START BEAT / 4 bars, then click the CHOP 1–8 clip triangles.\nThe eight clips share one track: each new chop interrupts the previous one.\nDrag the main crossfader: left = backing, center = both, right = chops.\nFor keyboard controls: Cmd+K, click each CHOP launch triangle and press 1–8, then Cmd+K. Turn off the Computer MIDI Keyboard if needed.\nFor AZ controls, enable the AZ MIDI input Remote setting and map desired pads with Cmd+M. Crossfader mapping MIDI channel 5 / CC11 is already embedded but untested.\n\nThis separate set has passed file-structure checks only; Live loading, audio latency, pad mapping and physical routing have not been tested. No existing Live set, preferences, physical deck, USB or AZ-MODS core was changed.\nThe samples are private extracts of existing user-prepared stems, not redistributable example content.\n")
    print(json.dumps({"project": str(project), "set": str(output), **report}, indent=2))


if __name__ == "__main__":
    main()
