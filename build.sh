#!/bin/sh
set -eu
mkdir -p build
compiler=${CXX:-c++}
set -- -std=c++20 -O3 -DNDEBUG -pthread
if [ "${STATIC:-0}" = 1 ]; then
    set -- "$@" -static
fi
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64)
        set -- "$@" -fno-pie -no-pie -DSHOP_KERNEL_ABSOLUTE_TABLES cpp/pipeline/k_free_clicks.S
        if [ "${PAIRED_CLICK_SORT:-0}" = 1 ]; then
            set -- "$@" -DSHOP_PAIRED_CLICK_SORT
        fi
        ;;
esac
"$compiler" "$@" cpp/pipeline/main.cpp cpp/pipeline/k_free.cpp cpp/pipeline/k_free_x86.cpp \
    cpp/pipeline/sys_global_alloc.cpp -o build/shopping
