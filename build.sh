#!/bin/sh
set -eu
mkdir -p build
compiler=${CXX:-c++}
platform=$(uname -s)-$(uname -m)
set -- -std=c++20 -O3 -DNDEBUG -pthread

if [ "$platform" = Linux-x86_64 ] && [ "${compiler##*/}" = clang++-16 ]; then
    set -- "$@" -fno-pic -fno-pie -march=x86-64 -mtune=cascadelake \
        -fconstexpr-steps=100000000 -DSHOP_KERNEL_ABSOLUTE_TABLES -flto=full
    for unit in main k_free k_free_x86 sys_global_alloc; do
        "$compiler" "$@" -c "cpp/pipeline/$unit.cpp" -o "build/$unit.bc"
    done
    "$compiler" -fno-pie -DSHOP_KERNEL_ABSOLUTE_TABLES -c cpp/pipeline/k_free_clicks.S -o build/clicks.o
    set -- "$@" -no-pie -fuse-ld=lld-16 -Wl,--lto-O3 -Wl,--threads=1 \
        build/main.bc build/k_free.bc build/k_free_x86.bc build/sys_global_alloc.bc build/clicks.o
else
    if [ "$platform" = Linux-x86_64 ]; then
        set -- "$@" -fno-pie -no-pie -DSHOP_KERNEL_ABSOLUTE_TABLES cpp/pipeline/k_free_clicks.S
    fi
    set -- "$@" cpp/pipeline/main.cpp cpp/pipeline/k_free.cpp cpp/pipeline/k_free_x86.cpp \
        cpp/pipeline/sys_global_alloc.cpp
fi
if [ "${STATIC:-0}" = 1 ]; then
    set -- "$@" -static
fi
exec "$compiler" "$@" -o build/shopping
