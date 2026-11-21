#!/bin/bash
set -euo pipefail
src=$1
out=$2
here=$(cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$out"
out=$(cd -- "$out" && pwd)
git -C "$src" archive HEAD .dockerignore Dockerfile build.sh cpp/pipeline tests/pipeline > "$work/source.tar"
mkdir "$work/source"
tar -xf "$work/source.tar" -C "$work/source"
cd "$work/source"
patch -p1 -i "$here/event-blocks.patch"
python3 - <<'PY'
from pathlib import Path
p = Path("Dockerfile")
original = p.read_text()
line = "RUN STATIC=1 sh build.sh"
if original.count(line) != 1:
    raise ValueError("the shipping Docker build invocation has changed")
p.write_text(original.replace(line, "RUN STATIC=1 EVENT_BLOCK_MASKS=1 sh build.sh"))
PY
cpp="$work/source/cpp/pipeline"
flags=(-std=c++20 -O3 -pthread -fno-pie -no-pie -DSHOP_KERNEL_ABSOLUTE_TABLES -DSHOP_EVENT_BLOCK_MASKS -Wno-ignored-attributes)
compiler=${CXX:-g++-12}
"$compiler" "${flags[@]}" -DNDEBUG -c "$cpp/k_free.cpp" -o "$work/dispatch.o"
"$compiler" "${flags[@]}" -c "$cpp/k_free_clicks.S" -o "$work/clicks.o"
objects=("$work/wide.o" "$work/dispatch.o" "$work/clicks.o")
for variant in blocks blocks-shift; do
    if [[ "$variant" == blocks-shift ]]; then patch -p1 -i "$here/window-shrd.patch"; fi
    docker buildx build --platform linux/amd64 --output "type=local,dest=$out/$variant" .
    "$compiler" "${flags[@]}" -DNDEBUG -c "$cpp/k_free_x86.cpp" -o "$work/wide.o"
    "$compiler" "${flags[@]}" -I"$cpp" tests/pipeline/kernel_rows.cpp "${objects[@]}" -o "$work/kernel"
    "$work/kernel" | tee "$out/$variant-kernel.log"
    "$compiler" "${flags[@]}" -I"$cpp" -Itests/pipeline "$here/extra_rows.cpp" "${objects[@]}" -o "$work/edges"
    "$work/edges" | tee "$out/$variant-edges.log"
    sha256sum "$out/$variant/app/shopping" >> "$out/scanner.sha256"
done
