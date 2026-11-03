#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 tools/write_build_info.py
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
for name in tft acc_display acc gps_nmea gps gps_display; do
    extra_source=""
    if [ "$name" = gps ]; then extra_source="LapTune/Core/Src/gps_nmea.c LapTune/Core/Src/gps_epoch.c LapTune/Core/Src/gps_time.c LapTune/Core/Src/log_clock.c tests/stubs/pps_source.c"; fi
    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DLAPTUNE_HOST_TEST \
       -Itests/stubs -ILapTune/Core/Inc \
       "tests/test_$name.c" "LapTune/Core/Src/$name.c" $extra_source -o "$test_build/$name"
    "$test_build/$name"
done
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DLAPTUNE_HOST_TEST \
   -Itests/stubs -ILapTune/Core/Inc tests/test_gps_config.c LapTune/Core/Src/gps.c \
   LapTune/Core/Src/gps_nmea.c LapTune/Core/Src/gps_epoch.c LapTune/Core/Src/gps_time.c LapTune/Core/Src/log_clock.c tests/stubs/pps_source.c -o "$test_build/gps_config"
"$test_build/gps_config"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DLAPTUNE_HOST_TEST \
   -Itests/stubs -ILapTune/Core/Inc tests/test_log_clock.c LapTune/Core/Src/log_clock.c LapTune/Core/Src/gps_time.c tests/stubs/pps_source.c -o "$test_build/log_clock"
"$test_build/log_clock"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DLAPTUNE_HOST_TEST \
   -Itests/stubs -ILapTune/Core/Inc tests/test_gps_time.c LapTune/Core/Src/gps_time.c LapTune/Core/Src/gps_epoch.c LapTune/Core/Src/log_clock.c -o "$test_build/gps_time"
"$test_build/gps_time"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DLAPTUNE_HOST_TEST \
   -Itests/stubs -ILapTune/Core/Inc -ILapTune/Core/FatFs \
   tests/test_sd_spi.c LapTune/Core/Src/sd_spi.c -o "$test_build/sd_spi"
"$test_build/sd_spi"

for name in timebase boot_identity imu_stream gps_epoch; do
    extra_source=""
    if [ "$name" = gps_epoch ]; then extra_source="LapTune/Core/Src/gps_time.c LapTune/Core/Src/log_clock.c tests/stubs/pps_source.c"; fi
    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DLAPTUNE_HOST_TEST \
       -Itests/stubs -ILapTune/Core/Inc "tests/test_$name.c" "LapTune/Core/Src/$name.c" $extra_source -o "$test_build/$name"
    "$test_build/$name"
done
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DLAPTUNE_HOST_TEST -DFF_USE_MKFS=1 \
   -Itests/stubs -ILapTune/Core/Inc -ILapTune/Core/FatFs \
   tests/test_sd_logger.c LapTune/Core/Src/sd_logger.c LapTune/Core/Src/session.c LapTune/Core/Src/log_clock.c \
   LapTune/Core/Src/gps_epoch.c LapTune/Core/Src/gps_time.c tests/stubs/pps_source.c LapTune/Core/Src/logger_pipeline.c LapTune/Core/Src/memory_diag.c tests/stubs/diagnostics.c LapTune/Core/FatFs/ff.c LapTune/Core/FatFs/ffunicode.c -o "$test_build/sd_logger"
export P0_ARTIFACT_DIR="${P0_ARTIFACT_DIR:-$test_build/p0-artifacts}"
mkdir -p "$P0_ARTIFACT_DIR"
"$test_build/sd_logger"
python3 tests/validate_p0_artifact.py "$P0_ARTIFACT_DIR"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DLAPTUNE_HOST_TEST -DLAPTUNE_PIPELINE_TEST -DFF_USE_MKFS=1 \
   -Itests/stubs -ILapTune/Core/Inc -ILapTune/Core/FatFs \
   tests/test_pipeline.c LapTune/Core/Src/sd_logger.c LapTune/Core/Src/session.c LapTune/Core/Src/log_clock.c \
   LapTune/Core/Src/gps_epoch.c LapTune/Core/Src/gps_time.c tests/stubs/pps_source.c LapTune/Core/Src/imu_stream.c LapTune/Core/Src/logger_pipeline.c \
   LapTune/Core/Src/memory_diag.c tests/stubs/diagnostics.c LapTune/Core/FatFs/ff.c LapTune/Core/FatFs/ffunicode.c -o "$test_build/pipeline"
"$test_build/pipeline"
