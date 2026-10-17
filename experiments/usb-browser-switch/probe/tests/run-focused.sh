#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")/.."
SDK=${AZ_SDK:-../../../../az-mods-sdk}
CC=${TEST_CC:-clang}
mkdir -p out-tests
for san in plain asan tsan; do
    flags=''
    case "$san" in
        asan) flags='-fsanitize=address,undefined -fno-sanitize-recover=all';;
        tsan) flags='-fsanitize=thread';;
    esac
    "$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer \
        $flags -I"$SDK/include" tests/test_backend.c -pthread -o "out-tests/test-$san"
    for test in normal bounds thread concurrent firmware prologue worker claim0 claim1 claim2 claim3; do
        "out-tests/test-$san" "$test"
    done
done
