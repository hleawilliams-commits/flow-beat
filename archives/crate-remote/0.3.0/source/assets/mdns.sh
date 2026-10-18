#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Crate Remote: publish crate-remote.local for each address given, and the page as a
# Bonjour web service, through the deck's own avahi-daemon. Stopped and restarted by the
# mod when the deck's addresses change.
trap 'kill 0' TERM INT
for ip in "$@"; do
    avahi-publish -a -R crate-remote.local "$ip" &
done
avahi-publish -s "Crate Remote" _http._tcp 8765 &
wait
