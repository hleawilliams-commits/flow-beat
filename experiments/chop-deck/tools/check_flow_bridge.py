#!/usr/bin/env python3
"""Check the existing FLOW -> Ableton Link bridge without joining any Link session.

Uses synthetic FLOW packets and aalink with networking explicitly disabled. No AZ
connection, audio, USB/config change, or Link-peer tempo change occurs. The optional
UDP check sends localhost-only packets on an ephemeral port, never FLOW's port 9100.
"""
import argparse
import asyncio
import contextlib
import importlib.metadata
import importlib.util
import io
import json
from pathlib import Path
import socket
import sys


def load_bridge():
    source = Path(__file__).resolve().parents[3] / "tools" / "flow_link_bridge.py"
    spec = importlib.util.spec_from_file_location("existing_flow_link_bridge", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, source


async def check_bridge():
    module, source = load_bridge()
    link = module.Link(124.0)
    link.enabled = False
    bridge = module.Bridge(link)
    checks = []

    def require(name, condition):
        if not condition:
            raise AssertionError(name)
        checks.append(name)

    def deliver(message):
        bridge.datagram_received(json.dumps(message).encode(), ("127.0.0.1", 49152))

    def deck(number, bpm, start, playing=1, loaded=1):
        return dict(d=number, bpm=bpm, start=start, play=playing, loaded=loaded)

    def state(*decks):
        deliver(dict(flowbeat=1, type="state", decks=list(decks)))

    with contextlib.redirect_stdout(io.StringIO()):
        state(deck(1, 124, 1), deck(2, 128, 2), deck(3, 130, 0, 0), deck(4, 0, 0, 0, 0))
        require("state packets set Link tempo", abs(link.tempo - 128) < 0.001)
        require("most recently started playing deck is selected", bridge.lead == 2)
        state(deck(1, 124, 1), deck(2, 128, 0, 0), deck(3, 130, 0, 0), deck(4, 0, 0, 0, 0))
        require("stopping lead deck falls back to another playing deck", bridge.lead == 1)
        require("fallback deck tempo reaches the real aalink object", abs(link.tempo - 124) < 0.001)
        deliver(dict(flowbeat=1, type="beat", d=1, bpm=124, n=1))
        require("beat packet accepted by current aalink API", link.beat >= 0)
        state(deck(1, 124, 0, 0), deck(2, 128, 0, 0), deck(3, 130, 0, 0), deck(4, 0, 0, 0, 0))
        require("all decks stopped holds the last tempo", abs(link.tempo - 124) < 0.001)
        require("Link networking stayed disabled throughout", not link.enabled)
        require("no remote play/stop synchronization enabled", not link.start_stop_sync_enabled)
        require("no peers joined", link.num_peers == 0)
    return dict(bridge=str(source), aalink=importlib.metadata.version("aalink"),
                checks=checks, link_networking=False, physical_deck_contacted=False)


def observe_local_udp_sharing():
    """Report, don't assume, how this OS distributes shared-port unicast packets."""
    listeners = []
    counts = [0, 0]
    try:
        for index in range(2):
            receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            listeners.append(receiver)
            receiver.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            if hasattr(socket, "SO_REUSEPORT"):
                receiver.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            receiver.bind(("127.0.0.1", 0 if index == 0 else listeners[0].getsockname()[1]))
            receiver.settimeout(0.2)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            for number in range(4):
                sender.sendto(("flow-local-check-%d" % number).encode(), listeners[0].getsockname())
        for index, receiver in enumerate(listeners):
            while True:
                try:
                    receiver.recvfrom(256)
                    counts[index] += 1
                except socket.timeout:
                    break
        return dict(local_unicast_packets_received=counts,
                    explanation="Shared sockets do not guarantee a copy of every unicast packet to both apps. This does not verify Flow VJ or LAN broadcast sharing.")
    finally:
        for receiver in listeners:
            receiver.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--udp-check", action="store_true", help="also observe shared-socket behavior on localhost only")
    args = parser.parse_args()
    result = asyncio.run(check_bridge())
    if args.udp_check:
        result["udp_observation"] = observe_local_udp_sharing()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, ImportError, OSError) as error:
        sys.exit("FLOW bridge check failed: %s" % error)
