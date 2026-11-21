#!/bin/sh
# Build the unmodified shipping ELF and compile-time sampling ELF with the same release Dockerfile.
set -eu
if [ "$#" != 2 ]; then echo "usage: sh build_parse_sampling.sh SOURCE_GIT NEW_OUTPUT_DIR" >&2; exit 2; fi
src=$1
dest=$2
ref=faf1047c6cb00cc8099572f5e067d18076375571
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -- "$dest"
dest=$(CDPATH= cd -- "$dest" && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
git -C "$src" archive "$ref" .dockerignore Dockerfile build.sh cpp/pipeline > "$work/source.tar"
mkdir "$work/source"
tar -xf "$work/source.tar" -C "$work/source"
cd "$work/source"
docker buildx build --platform linux/amd64 --output "type=local,dest=$dest/shipping" .
patch -p1 -i "$here/parse-sampling.patch"
python3 - <<'PY'
from pathlib import Path
p = Path("build.sh")
t = p.read_text()
assert t.count("set -- -std=c++20") == 1
p.write_text(t.replace("set -- -std=c++20", "set -- -DSHOP_PARSE_SAMPLING -std=c++20"))
PY
docker buildx build --platform linux/amd64 --output "type=local,dest=$dest/probe" .
sha256sum "$dest/shipping/app/shopping" "$dest/probe/app/shopping" > "$dest/SHA256SUMS"
