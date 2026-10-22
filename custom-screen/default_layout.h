/* SPDX-License-Identifier: GPL-3.0-only
 * The layout used when the USB has no layout.txt: assets/layout.txt, built in.
 * Regenerate with: python3 tools/make-default.py */
#ifndef CS_DEFAULT_LAYOUT_H
#define CS_DEFAULT_LAYOUT_H
static const char CS_DEFAULT_LAYOUT[] =
    "# Custom Screen layout, made in Deck Screen Designer\n"
    "# Coordinates are deck pixels (1280 x 800). Widget x/y are inside their panel.\n"
    "version 1\n"
    "name Big name + key on the left\n"
    "\n"
    "panel 1 x=0 y=80 w=140 h=77 views=4 bg=180d25 border=none bw=0 edge=all\n"
    "widget 1 title deck=3 x=8 y=2 w=130 h=28 size=18 color=auto align=l\n"
    "widget 1 key deck=3 x=8 y=34 w=62 h=40 size=28 color=auto align=l\n"
    "widget 1 bpm deck=3 x=72 y=34 w=66 h=20 size=14 color=dim align=r\n"
    "widget 1 time_left deck=3 x=72 y=54 w=66 h=20 size=14 color=auto align=r\n"
    "\n"
    "panel 2 x=0 y=202 w=140 h=77 views=4 bg=180d25 border=none bw=0 edge=all\n"
    "widget 2 title deck=1 x=8 y=2 w=130 h=28 size=18 color=auto align=l\n"
    "widget 2 key deck=1 x=8 y=34 w=62 h=40 size=28 color=auto align=l\n"
    "widget 2 bpm deck=1 x=72 y=34 w=66 h=20 size=14 color=dim align=r\n"
    "widget 2 time_left deck=1 x=72 y=54 w=66 h=20 size=14 color=auto align=r\n"
    "\n"
    "panel 3 x=0 y=324 w=140 h=77 views=4 bg=180d25 border=none bw=0 edge=all\n"
    "widget 3 title deck=2 x=8 y=2 w=130 h=28 size=18 color=auto align=l\n"
    "widget 3 key deck=2 x=8 y=34 w=62 h=40 size=28 color=auto align=l\n"
    "widget 3 bpm deck=2 x=72 y=34 w=66 h=20 size=14 color=dim align=r\n"
    "widget 3 time_left deck=2 x=72 y=54 w=66 h=20 size=14 color=auto align=r\n"
    "\n"
    "panel 4 x=0 y=446 w=140 h=77 views=4 bg=180d25 border=none bw=0 edge=all\n"
    "widget 4 title deck=4 x=8 y=2 w=130 h=28 size=18 color=auto align=l\n"
    "widget 4 key deck=4 x=8 y=34 w=62 h=40 size=28 color=auto align=l\n"
    "widget 4 bpm deck=4 x=72 y=34 w=66 h=20 size=14 color=dim align=r\n"
    "widget 4 time_left deck=4 x=72 y=54 w=66 h=20 size=14 color=auto align=r\n";
#endif
