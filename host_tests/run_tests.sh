#!/bin/sh
# Developer tool (Linux, needs gcc): compiles the REAL plugin sources against
# fake PS4/USB stubs and runs the unit + system tests, including the
# AddressSanitizer/UBSan and ThreadSanitizer builds.
# You do NOT need this to build or use the plugin on Windows.
set -e
cd "$(dirname "$0")"
OUT=/tmp/xbs_host_build
mkdir -p "$OUT" /tmp/xbs_host
CF="-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -pthread -Istubs -I../include -I."
SRCS="../src/xbox_series.c ../src/translator.c ../src/settings.c ../src/process_filter.c ../src/debug_log.c ../src/usb_manager.c ../src/virtual_pad.c ../src/hooks.c fake_platform.c"
export LSAN_OPTIONS="suppressions=$PWD/lsan.supp"

echo "== core unit tests"
gcc $CF -o $OUT/test_core test_core.c ../src/xbox_series.c ../src/translator.c ../src/settings.c ../src/process_filter.c fake_platform.c
$OUT/test_core
echo "== system tests (AddressSanitizer + UBSan)"
gcc $CF -fsanitize=address,undefined -o $OUT/test_system_asan test_system.c $SRCS
$OUT/test_system_asan
echo "== system tests (ThreadSanitizer)"
gcc $CF -fsanitize=thread -o $OUT/test_system_tsan test_system.c $SRCS
$OUT/test_system_tsan
echo "== exclusion tests (plugin_load, AddressSanitizer + UBSan)"
gcc $CF -fsanitize=address,undefined -o $OUT/test_exclusion_asan test_exclusion.c ../src/main.c $SRCS
$OUT/test_exclusion_asan
echo "== exclusion tests (ThreadSanitizer)"
gcc $CF -fsanitize=thread -o $OUT/test_exclusion_tsan test_exclusion.c ../src/main.c $SRCS
$OUT/test_exclusion_tsan
echo "ALL HOST TESTS PASSED"
