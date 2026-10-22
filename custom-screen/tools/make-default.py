#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Writes default_layout.h from assets/layout.txt, so the built-in layout matches the shipped one.
import pathlib
here = pathlib.Path(__file__).resolve().parent.parent
text = (here / "assets" / "layout.txt").read_text()
out = ["/* SPDX-License-Identifier: GPL-3.0-only",
       " * The layout used when the USB has no layout.txt: assets/layout.txt, built in.",
       " * Regenerate with: python3 tools/make-default.py */",
       "#ifndef CS_DEFAULT_LAYOUT_H", "#define CS_DEFAULT_LAYOUT_H", "static const char CS_DEFAULT_LAYOUT[] ="]
for line in text.splitlines():
    out.append('    "' + line.replace("\\", "\\\\").replace('"', '\\"') + '\\n"')
out[-1] += ";"
out.append("#endif")
(here / "default_layout.h").write_text("\n".join(out) + "\n")
