#!/usr/bin/env bash
set -euo pipefail
source_root=$(cd -- "$1" && pwd)
out=$2
here=$(cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
mkdir -p "$out"
git -C "$source_root" archive HEAD .dockerignore Dockerfile build.sh cpp/pipeline tests/pipeline > "$scratch/tree.tar"
for shape in calendar epoch combined; do
    mkdir "$scratch/$shape"
    cd "$scratch/$shape"
    tar -xf "$scratch/tree.tar"
    git apply --no-index "$here/constant-clock.patch"
    python3 - "$shape" <<'PY'
from pathlib import Path
import sys
shape = sys.argv[1]
docker = Path("Dockerfile")
command = "RUN CXX=clang++-16 STATIC=1 sh build.sh"
s = docker.read_text()
assert s.count(command) == 1
docker.write_text(s.replace(command, command.replace("RUN ", "RUN CONSTANT_EVENT_CLOCK=1 ")))
if shape != "combined":
    kernel = Path("cpp/pipeline/k_free_x86.cpp")
    remove = {"calendar": 13, "epoch": 20}[shape]
    line = f"    if (st.tj==&T.tj[2*{remove}]) return fixed_event_clock<2*{remove}>(st);\n"
    s = kernel.read_text()
    assert s.count(line) == 1
    kernel.write_text(s.replace(line, ""))
PY
    flags=(-std=c++20 -O3 -pthread -Wno-return-type -Wno-ignored-attributes -fno-pie -no-pie
           -DSHOP_KERNEL_ABSOLUTE_TABLES -DSHOP_CONSTANT_EVENT_CLOCK -DREQUIRE_NATIVE_EVENT_KERNEL)
    g++-12 "${flags[@]}" -Icpp/pipeline -Itests/pipeline "$here/../clock_training/field_rows.cpp" \
        cpp/pipeline/k_free.cpp cpp/pipeline/k_free_x86.cpp cpp/pipeline/k_free_clicks.S -o "$scratch/$shape-oracle"
    "$scratch/$shape-oracle" > "$out/$shape-rows.log"
    cat "$out/$shape-rows.log"
    docker buildx build --platform linux/amd64 --output "type=local,dest=$out/$shape" .
    sha256sum "$out/$shape/app/shopping"
done
