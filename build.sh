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
        if [ "${CUSTOM_MEMORY:-0}" = 1 ]; then
            # The static C runtime calls these symbols before main. Keep the
            # resolver at baseline ISA and outside the caller's LTO scope.
            "$compiler" -std=c++20 -O3 -march=x86-64 -ffreestanding -fno-builtin \
                -fno-stack-protector -fno-lto -fno-pie -c cpp/pipeline/sys_memory_x86.cpp \
                -o build/sys_memory.o
            set -- "$@" build/sys_memory.o
        fi
        ;;
esac
"$compiler" "$@" cpp/pipeline/main.cpp cpp/pipeline/k_free.cpp cpp/pipeline/k_free_x86.cpp \
    cpp/pipeline/sys_global_alloc.cpp -o build/shopping
