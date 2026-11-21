#!/bin/bash
set -euo pipefail
source_root=$(cd -- "$1" && pwd)
out=$2
here=$(cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
mkdir -p "$out"
git -C "$source_root" archive HEAD .dockerignore Dockerfile build.sh cpp/pipeline tests/pipeline > "$scratch/source.tar"
for variant in direct-events cached-lines click-index click-partitions; do
    checkout="$scratch/$variant"
    mkdir "$checkout"
    tar -xf "$scratch/source.tar" -C "$checkout"
    cd "$checkout"
    if test "$variant" = direct-events; then
        patch -p1 -i "$here/direct-event-pages.patch"
        gcc_flags=(-std=c++20 -O3 -pthread -Icpp/pipeline -Wno-ignored-attributes)
        for test in routing_part routing_packets routing_sinks; do
            g++-12 "${gcc_flags[@]}" "tests/pipeline/$test.cpp" cpp/pipeline/sys_global_alloc.cpp -o "$scratch/$test"
            "$scratch/$test"
        done
    else
        python3 - "$variant" <<'PY'
from pathlib import Path
import sys
kind = sys.argv[1]
if kind == "cached-lines":
    path = Path("cpp/pipeline/main.cpp")
    before, after = 'toggle("XL_NT",true)', 'toggle("XL_NT",false)'
else:
    path = Path("cpp/pipeline/setup.hpp")
    mode = {"click-index": 1, "click-partitions": 2}[kind]
    before, after = 'env_size("XL_CKIX",0)', f'env_size("XL_CKIX",{mode})'
source = path.read_text()
if source.count(before) != 1:
    raise ValueError(f"expected one default in {path}")
path.write_text(source.replace(before, after))
PY
    fi
    docker buildx build --platform linux/amd64 --output "type=local,dest=$out/$variant" .
    sha256sum "$out/$variant/app/shopping"
    sample="$scratch/dev-output-$variant"
    mkdir "$sample"
    NCPU=4 XL_CHECK=1 XL_TIMING=1 "$out/$variant/app/shopping" "$RUNNER_TEMP/shopping-trials/dev" "$sample" >"$out/$variant.log" 2>&1
    if grep -q '\[pipeline\] user key' "$out/$variant.log"; then
        capacity=$(awk '/\[pipeline\] user key/ {print $NF; exit}' "$out/$variant.log")
        echo "::notice title=1M $variant path::Compact reader, click ordinal capacity $capacity."
    else
        echo "::notice title=1M $variant path::Used the general reader."
    fi
    rm -r -- "$sample"
done
