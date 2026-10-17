#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")/.."
AZ_SDK="${AZ_SDK:-../../az-mods-sdk}"
TEST_CC="${TEST_CC:-clang}"
TEST_OUT="$(mktemp -d "${TMPDIR:-/tmp}/mods-shortcut-tests.XXXXXX")"
trap 'rm -rf "$TEST_OUT"' EXIT HUP INT TERM
for test in shortcut component concurrency; do
    "$TEST_CC" -pthread -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
        -I"$AZ_SDK/include" "tests/test_$test.c" -o "$TEST_OUT/$test"
    "$TEST_OUT/$test"
    "$TEST_CC" -pthread -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -I"$AZ_SDK/include" "tests/test_$test.c" -o "$TEST_OUT/$test-asan"
    "$TEST_OUT/$test-asan"
done

# This fixture invokes the real registered filter concurrently with UI requests.
"$TEST_CC" -pthread -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
    -fsanitize=thread -fno-omit-frame-pointer -I"$AZ_SDK/include" \
    tests/test_concurrency.c -o "$TEST_OUT/concurrency-tsan"
"$TEST_OUT/concurrency-tsan"
