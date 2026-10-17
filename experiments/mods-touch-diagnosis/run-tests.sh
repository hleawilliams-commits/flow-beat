#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")"
AZ_SDK="${AZ_SDK:-../../../az-mods-sdk}"
TEST_OUT="$(mktemp -d "${TMPDIR:-/tmp}/mods-touch-diagnosis.XXXXXX")"
trap 'rm -rf "$TEST_OUT"' EXIT HUP INT TERM
for san in plain asan; do
    flags=""
    if [ "$san" = asan ]; then flags="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
    ${TEST_CC:-clang} -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L $flags \
        -I"$AZ_SDK/include" test-diagnostic.c -o "$TEST_OUT/$san"
    "$TEST_OUT/$san"
done
# Verify the recorded patch applies and reproduces the full diagnostic helper.
mkdir -p "$TEST_OUT/mods-shortcut"
cp ../../mods-shortcut/shortcut.h "$TEST_OUT/mods-shortcut/shortcut.h"
patch -s -d "$TEST_OUT" -p1 < diagnostic.patch
cmp "$TEST_OUT/mods-shortcut/shortcut.h" shortcut-diagnostic.h
