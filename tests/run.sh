#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
for name in tft acc_display acc; do
    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
       -Itests/stubs -ILapTune/Core/Inc \
       "tests/test_$name.c" "LapTune/Core/Src/$name.c" -o "$test_build/$name"
    "$test_build/$name"
done
