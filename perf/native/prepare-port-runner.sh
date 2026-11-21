#!/bin/bash
set -euo pipefail
mkdir -p "$RUNNER_TEMP/shopping-trials"
cd "$RUNNER_TEMP/shopping-trials"
base=https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files
for archive in generator starter-pyspark dev-answer; do
    curl --fail --location --retry 3 --silent --show-error "$base/$archive.zip" -o "$archive.zip"
done
sha256sum --check "$GITHUB_WORKSPACE/perf/native/archives.sha256"
for archive in generator starter-pyspark dev-answer; do unzip -q "$archive.zip"; done
cargo build --manifest-path generator/Cargo.toml --release --offline
generator/target/release/generate --seed 1 --events 1000000 --clicks 250000 --threads 4 --output dev
generator/target/release/generate --seed 23 --events 20000000 --clicks 5000000 --threads 4 --output practice
python3 -m venv "$RUNNER_TEMP/shopping-python"
"$RUNNER_TEMP/shopping-python/bin/pip" install --quiet pyarrow==21.0.0 polars==2.1.0
if ! command -v g++-12; then
    sudo apt-get -qq update
    sudo apt-get install -y g++-12
fi
lscpu
df -h .
