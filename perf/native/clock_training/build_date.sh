#!/bin/bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
llvm=$(cd -- "$1" && pwd)
gcc=$(cd -- "$2" && pwd)
out=$3
mkdir -p "$out"
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
for label in clang-date gcc-date; do
    checkout=$llvm
    if test "$label" = gcc-date; then checkout=$gcc; fi
    work="$scratch/$label"
    mkdir "$work"
    git -C "$checkout" archive HEAD .dockerignore Dockerfile build.sh cpp/pipeline tests/pipeline > "$scratch/tree.tar"
    tar -xf "$scratch/tree.tar" -C "$work"
    cd "$work"
    git apply --no-index --include=cpp/pipeline/k_free_x86.cpp "$here/shared-event-date.patch"
    grep -q '^#ifdef SHOP_SHARED_EVENT_DATE' cpp/pipeline/k_free_x86.cpp
    python3 - <<'PY'
from pathlib import Path
p = Path("build.sh")
s = p.read_text()
start = "set -- -std=c++20 -O3"
assert s.count(start) == 1
p.write_text(s.replace(start, start + " -DSHOP_SHARED_EVENT_DATE"))
PY
    if test "$label" = clang-date; then
        flags=(-std=c++20 -O3 -pthread -Wno-return-type -Wno-ignored-attributes -fno-pie -no-pie
                -DSHOP_KERNEL_ABSOLUTE_TABLES -DSHOP_SHARED_EVENT_DATE -DREQUIRE_NATIVE_EVENT_KERNEL)
        g++-12 "${flags[@]}" -Icpp/pipeline -Itests/pipeline "$here/field_rows.cpp" \
            cpp/pipeline/k_free.cpp cpp/pipeline/k_free_x86.cpp cpp/pipeline/k_free_clicks.S -o "$scratch/date-oracle"
        "$scratch/date-oracle"
    fi
    docker buildx build --platform linux/amd64 --output "type=local,dest=$out/$label" .
    sha256sum "$out/$label/app/shopping"
done
