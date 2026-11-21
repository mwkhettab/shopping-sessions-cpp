#!/bin/sh
set -eu
: "${SOURCE:?Set SOURCE to a checkout of 29ee0cc with constant-clock.patch applied}"
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
case "$(uname -s)-$(uname -m)" in Linux-x86_64) ;; *) exit 77;; esac
compiler=${CXX:-clang++-16}
out=${OUTPUT:-./clock-oracle}
set -- -std=c++20 -O3 -DNDEBUG -pthread -fno-pic -fno-pie -march=x86-64 -mtune=cascadelake \
    -fconstexpr-steps=100000000 -flto=full -DSHOP_KERNEL_ABSOLUTE_TABLES -DSHOP_CONSTANT_EVENT_CLOCK \
    -DREQUIRE_NATIVE_EVENT_KERNEL -Wno-return-type -I"$SOURCE/cpp/pipeline" -I"$SOURCE/tests/pipeline" \
    "$here/time_rows.cpp" "$SOURCE/cpp/pipeline/k_free.cpp" "$SOURCE/cpp/pipeline/k_free_x86.cpp" \
    "$SOURCE/cpp/pipeline/k_free_clicks.S" -no-pie -fuse-ld=lld-16 -Wl,--lto-O3 -Wl,--threads=1 -static -o "$out"
"$compiler" "$@"
if [ "${COMPILE_ONLY:-0}" != 1 ]; then "$out"; fi
