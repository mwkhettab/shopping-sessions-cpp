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
  set -- "$@" -flto=thin -fuse-ld=lld-16
  "$compiler" "$@" -fprofile-instr-generate cpp/main.cpp -o build/shopping
  "${PYTHON:-python3}" perf/train.py "$work/input"
  LLVM_PROFILE_FILE="$work/short.profraw" NCPU=4 build/shopping "$work/input/short" "$work/output-short"
  LLVM_PROFILE_FILE="$work/wide.profraw" NCPU=4 build/shopping "$work/input/wide" "$work/output-wide"
  "${LLVM_PROFDATA:-llvm-profdata-16}" merge "$work/short.profraw" "$work/wide.profraw" -o "$work/use.profdata"
  "$compiler" "$@" "-fprofile-instr-use=$work/use.profdata" -Werror=profile-instr-out-of-date cpp/main.cpp -o build/shopping
else
  "$compiler" "$@" cpp/main.cpp -o build/shopping
fi
