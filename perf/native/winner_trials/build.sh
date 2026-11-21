#!/usr/bin/env bash
set -euo pipefail
source_root=$(cd -- "$1" && pwd)
out=$2
here=$(cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
mkdir -p "$out" "$scratch/src"
git -C "$source_root" archive HEAD | tar -xf - -C "$scratch/src"
cd "$scratch/src"
git apply "$here/event-primary.patch"
g++-12 -std=c++20 -O2 -pthread -Wno-ignored-attributes -Icpp/pipeline \
    tests/pipeline/event_primary.cpp -o "$scratch/test-event-primary"
"$scratch/test-event-primary"
for policy in staged bitmap cas; do
    docker buildx build --platform linux/amd64 --build-arg "SHOP_EVENT_PRIMARY_DEFAULT=$policy" \
        --output "type=local,dest=$out/$policy" .
    exe="$out/$policy/app/shopping"
    sha256sum "$exe"
    sample="$scratch/dev-$policy"
    mkdir -p "$sample"
    NCPU=4 XL_TIMING=1 "$exe" "$RUNNER_TEMP/shopping-trials/dev" "$sample" >"$out/$policy.log" 2>&1
    proof=$(grep -m1 -E "event_primary=$policy([[:space:]]|$)" "$out/$policy.log")
    echo "::notice title=Development event storage::$proof"
done
"$RUNNER_TEMP/shopping-python/bin/python" tests/pipeline/event_primary.py "$out/cas/app/shopping"
