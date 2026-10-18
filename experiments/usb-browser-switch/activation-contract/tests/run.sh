#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$here/private/test-output"
for variant in plain asan-ubsan tsan; do
  flags=''
  case "$variant" in
    asan-ubsan) flags='-fsanitize=address,undefined -fno-omit-frame-pointer' ;;
    tsan) flags='-fsanitize=thread' ;;
  esac
  c++ -std=c++17 -Wall -Wextra -Werror -O1 -g -pthread $flags "$here/tests/test_intent.cpp" -o "$here/private/test-output/$variant"
  "$here/private/test-output/$variant" > "$here/private/test-output/$variant.txt"
  cat "$here/private/test-output/$variant.txt"
done
