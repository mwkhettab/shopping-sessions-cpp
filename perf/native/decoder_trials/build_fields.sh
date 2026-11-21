#!/bin/bash
set -euo pipefail
src=$(cd -- "$1" && pwd)
mkdir -p "$2"
out=$(cd -- "$2" && pwd)
here=$(cd -- "$(dirname -- "$0")" && pwd)
common="$here/../parser_trials"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
git -C "$src" archive HEAD .dockerignore Dockerfile build.sh cpp/pipeline tests/pipeline > "$work/source.tar"
cc=${CXX:-g++-12}
flags=(-std=c++20 -O3 -pthread -fno-pie -no-pie -DSHOP_KERNEL_ABSOLUTE_TABLES -Wno-ignored-attributes)
"$cc" "${flags[@]}" -DNDEBUG -c "$src/cpp/pipeline/k_free.cpp" -o "$work/dispatch.o"
"$cc" "${flags[@]}" -c "$src/cpp/pipeline/k_free_clicks.S" -o "$work/clicks.o"
"$cc" "${flags[@]}" -I"$src/cpp/pipeline" "$here/check_all.cpp" -o "$work/check-widths"
"$work/check-widths"
for variant in reverse-tail compact-ids row-ahead; do
    mkdir "$work/$variant"
    cd "$work/$variant"
    tar -xf "$work/source.tar"
    defs=()
    case "$variant" in
        reverse-tail) patch -p1 -i "$here/reverse-tail.patch" ;;
        compact-ids) patch -p1 -i "$here/compact-id-table.patch" ;;
        row-ahead)
            patch -p1 -i "$common/event-blocks.patch"
            patch -p1 -i "$common/window-shrd.patch"
            patch -p1 -i "$here/row-ahead.patch"
            sed -i 's/^RUN STATIC=1 sh build.sh$/RUN STATIC=1 EVENT_ROW_AHEAD=1 sh build.sh/' Dockerfile
            grep -q 'EVENT_ROW_AHEAD=1' Dockerfile
            defs=(-DSHOP_EVENT_BLOCK_MASKS -DSHOP_EVENT_ROW_AHEAD)
            ;;
    esac
    docker buildx build --platform linux/amd64 --output "type=local,dest=$out/$variant" .
    cpp="$PWD/cpp/pipeline"
    "$cc" "${flags[@]}" "${defs[@]}" -DNDEBUG -c "$cpp/k_free_x86.cpp" -o "$work/wide.o"
    objects=("$work/wide.o" "$work/dispatch.o" "$work/clicks.o")
    for test in "$common/extra_rows.cpp" "$here/pair_rows.cpp"; do
        "$cc" "${flags[@]}" "${defs[@]}" -I"$cpp" -Itests/pipeline "$test" "${objects[@]}" -o "$work/oracle"
        "$work/oracle"
    done
    sha256sum "$out/$variant/app/shopping"
done
