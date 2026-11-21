#!/bin/sh
set -eu
mkdir -p build
compiler="${CXX:-c++}"
set -- -std=c++20 -O3 -DNDEBUG -pthread -Wall -Wextra
if [ "${DIAGNOSTICS:-0}" = 1 ]; then
  "$compiler" "$@" -DSHOPPING_DIAGNOSTICS cpp/main.cpp -o build/shopping-profile
elif [ "${PGO:-0}" = 1 ]; then
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  idx="$work/counters"
  "$compiler" "$@" "-fprofile-generate=$idx" -fprofile-update=atomic cpp/main.cpp -o build/shopping
  "${PYTHON:-python3}" perf/train.py "$work/input"
  NCPU=4 build/shopping "$work/input/short" "$work/output-short"
  NCPU=4 build/shopping "$work/input/wide" "$work/output-wide"
  SHOP_TRAIN_BASE_READER=1 NCPU=4 build/shopping "$work/input/short" "$work/output-avx2-short"
  SHOP_TRAIN_BASE_READER=1 NCPU=4 build/shopping "$work/input/wide" "$work/output-avx2-wide"
  SHOP_TRAIN_BW_READER=1 NCPU=4 build/shopping "$work/input/short" "$work/output-bw-short"
  SHOP_TRAIN_BW_READER=1 NCPU=4 build/shopping "$work/input/wide" "$work/output-bw-wide"
  "$compiler" "$@" "-fprofile-use=$idx" -fprofile-partial-training -fprofile-correction -Werror=missing-profile cpp/main.cpp -o build/shopping
else
  "$compiler" "$@" cpp/main.cpp -o build/shopping
fi
