#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
phase_test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
phase_sdk=${AZ_SDK:-"$phase_test_dir/../../../az-mods-sdk"}
phase_tmp=$(mktemp -d "${TMPDIR:-/tmp}/phase-strip-tests.XXXXXX")
trap 'rm -rf "$phase_tmp"' EXIT HUP INT TERM
for phase_san in plain asan; do
    case "$phase_san" in plain) set -- ;; asan) set -- -fsanitize=address,undefined ;; esac
    "${CC:-clang}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
        -fno-omit-frame-pointer -g "$@" -I"$phase_sdk/include" \
        "$phase_test_dir/test_component.c" -o "$phase_tmp/test-$phase_san"
    printf '%s\n' "phase-strip focused test ($phase_san)"
    "$phase_tmp/test-$phase_san"
done
