#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
for name in tft acc_display acc gps_nmea gps gps_display; do
    extra_source=""
    if [ "$name" = gps ]; then extra_source=LapTune/Core/Src/gps_nmea.c; fi
    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
       -Itests/stubs -ILapTune/Core/Inc \
       "tests/test_$name.c" "LapTune/Core/Src/$name.c" $extra_source -o "$test_build/$name"
    "$test_build/$name"
done
