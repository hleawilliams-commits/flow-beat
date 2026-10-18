#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")/.."
SDK=${AZ_SDK:-../../../../az-mods-sdk}
CXX=${TEST_CXX:-clang++}
mkdir -p out-tests
for san in plain asan tsan; do
    flags=''
    case "$san" in
        asan) flags='-fsanitize=address,undefined -fno-sanitize-recover=all';;
        tsan) flags='-fsanitize=thread';;
    esac
    "$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-function \
        -fno-omit-frame-pointer -fexceptions -fasynchronous-unwind-tables \
        $flags -I"$SDK/include" tests/test_observer.cpp -pthread -o "out-tests/test-$san"
    for scenario in env sdk firmware prologue worker claim empty normal thread heartbeat budget concurrent; do
        "out-tests/test-$san" "$scenario"
    done
done
