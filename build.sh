#!/bin/sh
set -eu
mkdir -p build
compiler=${CXX:-c++}
set -- -std=c++20 -O3 -DNDEBUG -DSHOP_SPARSE_USER_OUTPUT=1 -pthread
if [ "$(uname -s)-$(uname -m)" = Linux-x86_64 ] && [ "${compiler##*/}" = clang++-16 ]; then
    set -- "$@" -march=x86-64 -mtune=cascadelake -fno-pic -fno-pie -no-pie \
        -fconstexpr-steps=100000000 -flto=full -fuse-ld=lld-16 -Wl,--lto-O3 -Wl,--threads=1
fi
if [ "${STATIC:-0}" = 1 ]; then
    set -- "$@" -static
fi
exec "$compiler" "$@" cpp/main.cpp -o build/shopping
