# Shopping Sessions in C++

A high-throughput C++20 solution for the Shopping Sessions Buildathon challenge. It reads the challenge CSV dataset, resolves linked identities, deduplicates and validates events and clicks, sessionizes activity, attributes campaigns, and writes the two required Parquet tables.

The complete challenge contract is in [`rules.md`](rules.md).

## Build the submission image

The scorer runs `linux/amd64`. Docker Buildx compiles the C++20 pipeline with Clang 16 and link-time optimization, then copies the static executable into the runtime image.

```sh
docker buildx build --platform linux/amd64 --load \
  -t shopping-sessions-cpp:latest .
```

On Apple silicon, use `--platform linux/arm64` for faster development builds, then validate an `amd64` image before submitting.

## Run

The image reads `/data` and writes `/output` by default. The scorer supplies eight CPUs and 24 GB of memory, with no network access at runtime.

```sh
mkdir -p output scratch
docker run --rm \
  --cpus=8 --memory=24g --network=none --read-only \
  -v "$PWD/dev:/data:ro" \
  -v "$PWD/output:/output" \
  -v "$PWD/scratch:/scratch" \
  -e DATA_DIR=/data \
  -e OUTPUT_DIR=/output \
  -e SCRATCH_DIR=/scratch \
  -e HOME=/scratch \
  -e NCPU=8 \
  -e MEM_GB=24 \
  shopping-sessions-cpp:latest
```

The result is written as Parquet files under:

- `output/user_daily/`
- `output/campaign_daily/`

## Native development build

The executable builds with a C++20 compiler on Linux or macOS. The test suite uses Polars and PyArrow to compare and independently read the Parquet files.

```sh
sh build.sh
DATA_DIR=dev OUTPUT_DIR=output NCPU=8 ./build/shopping
```

## Implementation notes

Workers parse events, clicks and identity links into partitions. Every CSV-valid event revision with an ID enters the global earliest-ingest comparison, including revisions that fail semantic validation. A losing revision invalidates its own raw event slot. Link endpoints and surviving records share the same exact byte-key groups. A parallel union finds the smallest alias in each connected component, including link-only aliases.

Short person timelines use a chronological sort. For dense timelines, half-hour bins keep their first and last milliseconds, event counts and purchases. The gap between adjacent occupied bins decides whether their events belong to the same session. Wide manifest windows and large catalogs retain full time and category widths. The writer emits the two flat Parquet tables directly.

Short IDs use a reversible 63-bit representation. Its bits select the same shard and hash-table slot on each lookup. Comparisons for a canonical person name or tied click recover the original bytes. Other IDs retain their complete input string and a cached hash.

The native comparisons, resource limits and correctness checks are recorded in [BENCHMARKS.md](BENCHMARKS.md).
