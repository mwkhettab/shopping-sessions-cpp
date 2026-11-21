#!/bin/sh
set -eu
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then echo "usage: sh build_pgo.sh PINNED_CHECKOUT NEW_OUTPUT [EXPECTED_FULL_SHA]" >&2; exit 2; fi
source_repo=$1
out=$2
rev=${3:-faf1047c6cb00cc8099572f5e067d18076375571}
case "$rev" in *[!0-9a-f]*|'') echo "expected a full lowercase Git commit SHA" >&2; exit 2;; esac
head=$(git -C "$source_repo" rev-parse --verify HEAD)
if [ "${#rev}" -ne 40 ] || [ "$head" != "$rev" ]; then
    echo "candidate checkout is $head, expected full SHA $rev" >&2
    exit 2
fi
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -- "$out"
out=$(CDPATH= cd -- "$out" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
git -C "$source_repo" archive "$rev" Dockerfile build.sh cpp/pipeline > "$tmp/source.tar"
mkdir "$tmp/context"
tar -xf "$tmp/source.tar" -C "$tmp/context"
cd "$tmp/context"
mkdir training
for f in train_build.sh train.py training_gate.cpp audit_profiles.py; do cp "$here/$f" training; done
python3 - "$rev" <<'PY'
from pathlib import Path
import sys
f=Path("Dockerfile")
source=f.read_text()
assert source.count("RUN STATIC=1 sh build.sh")==1 and source.count("FROM scratch")==1
release=source.replace(" python3-minimal "," python3 ")
assert " g++ python3 " in release, "release build stage needs g++ and the full Debian python3 package"
run = ("COPY training /training\n"
       "RUN python3 -c 'import datetime, hashlib, json, pathlib, random, resource, shutil, subprocess, tempfile' "
       "&& sh /training/train_build.sh "
       f"&& echo {sys.argv[1]} > /build-proof/source_revision")
release=release.replace("RUN STATIC=1 sh build.sh",run)
release=release.replace("FROM scratch\n","FROM scratch AS release\n")
f.write_text(release+"\nFROM scratch AS proof\nCOPY --from=build /build-proof/ /\n")
PY
docker buildx build --platform linux/amd64 --target release --output "type=local,dest=$out/release" .
docker buildx build --platform linux/amd64 --target proof --output "type=local,dest=$out/proof" .
