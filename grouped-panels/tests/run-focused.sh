#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Host-only checks. No emulator, network, USB or device writes.
set -eu

gp_test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
gp_mod_dir=$(CDPATH= cd -- "$gp_test_dir/.." && pwd)
gp_sdk=${AZ_SDK:-"$gp_mod_dir/../../az-mods-sdk"}
gp_cc=${CC:-clang}
if [ ! -f "$gp_sdk/include/azmod-mod.h" ]; then
    printf '%s\n' 'Set AZ_SDK to the az-mods-sdk checkout containing include/azmod-mod.h.' >&2
    exit 2
fi
command -v "$gp_cc" >/dev/null 2>&1 || {
    printf '%s\n' "Compiler not found: $gp_cc" >&2
    exit 2
}
gp_tmp=$(mktemp -d "${TMPDIR:-/tmp}/grouped-panels-tests.XXXXXX")
trap 'rm -rf "$gp_tmp"' EXIT HUP INT TERM

gp_run() {
    gp_suite=$1
    gp_mode=$2
    case "$gp_mode" in
        plain) set -- ;;
        asan) set -- -fsanitize=address,undefined ;;
        tsan) set -- -fsanitize=thread ;;
        *) exit 2 ;;
    esac
    printf '%s\n' "grouped-panels: $gp_suite ($gp_mode)"
    "$gp_cc" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
        -fno-omit-frame-pointer -g "$@" -I"$gp_sdk/include" -pthread \
        "$gp_test_dir/test_$gp_suite.c" -lm -o "$gp_tmp/$gp_suite-$gp_mode"
    "$gp_tmp/$gp_suite-$gp_mode"
}

for gp_mode in plain asan; do
    for gp_suite in layout hud data paint; do
        gp_run "$gp_suite" "$gp_mode"
    done
done
# The data suite exercises actual concurrent reader, counter and UI operations.
gp_run data tsan
