FROM debian:bookworm-slim@sha256:734a72b3d6142a71993b4bc580ca01d8ca687f77a2c422d96a961d3376b179bc AS builder
RUN apt-get update && apt-get install -y --no-install-recommends g++ clang-16 lld-16 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN SOURCE=/src OUTPUT=/tmp/test-clock COMPILE_ONLY=1 sh native-oracle.sh
FROM scratch
COPY --from=builder /tmp/test-clock /test-clock
