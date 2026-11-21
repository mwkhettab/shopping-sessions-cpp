#!/bin/sh
set -eu
mkdir -p build
compiler="${CXX:-c++}"
set -- -std=c++20 -O3 -DNDEBUG -pthread -Wall -Wextra
level=4
if [ "${DIAGNOSTICS:-0}" = 1 ]; then
  "$compiler" "$@" -DSHOPPING_DIAGNOSTICS cpp/main.cpp -o build/shopping-profile
  exit
fi
case "$("$compiler" -dumpmachine)" in
  x86_64*linux*) ;;
  *) "$compiler" "$@" cpp/main.cpp -o build/shopping; exit ;;
esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
profiled="${PGO:-0}"
if [ "$profiled" = 1 ]; then
  "${PYTHON:-python3}" perf/train.py "$work/input"
fi
compile() {
  name="$1"
  train="$2"
  shift 2
  if [ "$train" = 1 ]; then
    profile="$work/counters-$name"
    "$compiler" "$@" "-fprofile-generate=$profile" -fprofile-update=atomic cpp/main.cpp -o "$work/$name"
    NCPU=4 "$work/$name" "$work/input/short" "$work/out-short-$name"
    NCPU=4 "$work/$name" "$work/input/wide" "$work/out-wide-$name"
    SHOP_TRAIN_BASE_READER=1 NCPU=4 "$work/$name" "$work/input/short" "$work/base-short-$name"
    SHOP_TRAIN_BASE_READER=1 NCPU=4 "$work/$name" "$work/input/wide" "$work/base-wide-$name"
    NCPU=4 "$work/$name" "$work/input/indexed" "$work/indexed-$name"
    SHOP_TRAIN_BASE_READER=1 NCPU=4 "$work/$name" "$work/input/indexed" "$work/base-indexed-$name"
    SHOP_ORDINAL_BASE_MULTIPLY=1 NCPU=4 "$work/$name" "$work/input/indexed" "$work/dq-indexed-$name"

    "$compiler" "$@" "-fprofile-use=$profile" -fprofile-correction -Werror=missing-profile cpp/main.cpp -o "$work/$name"
  else
    "$compiler" "$@" cpp/main.cpp -o "$work/$name"
  fi
}
compile shopping "$profiled" "$@" -DSHOP_RELEASE_DISPATCH=1
cat > "$work/probe.cpp" <<'CPP'
int main(){return !(__builtin_cpu_supports("x86-64-v4") && __builtin_cpu_supports("pclmul"));}
CPP
"$compiler" -O2 "$work/probe.cpp" -o "$work/probe"
fast_profile=0
if ! "$work/probe"; then fast_profile=0; fi
compile shopping.opt "$fast_profile" "$@" -march=x86-64-v4 -mpclmul -mtune=icelake-server 
mv "$work/shopping" build/shopping
mv "$work/shopping.opt" build/shopping.opt
