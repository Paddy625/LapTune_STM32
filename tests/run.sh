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
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
   -Itests/stubs -ILapTune/Core/Inc tests/test_log_clock.c LapTune/Core/Src/log_clock.c -o "$test_build/log_clock"
"$test_build/log_clock"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DFF_USE_MKFS=1 \
   -Itests/stubs -ILapTune/Core/Inc -ILapTune/Core/FatFs \
   tests/test_sd_logger.c LapTune/Core/Src/sd_logger.c LapTune/Core/Src/log_clock.c LapTune/Core/Src/gps_nmea.c \
   LapTune/Core/FatFs/ff.c -o "$test_build/sd_logger"
"$test_build/sd_logger"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
   -Itests/stubs -ILapTune/Core/Inc -ILapTune/Core/FatFs \
   tests/test_sd_spi.c LapTune/Core/Src/sd_spi.c -o "$test_build/sd_spi"
"$test_build/sd_spi"
