FROM debian:bookworm-slim@sha256:734a72b3d6142a71993b4bc580ca01d8ca687f77a2c422d96a961d3376b179bc AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ clang-16 lld-16 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY cpp cpp
COPY build.sh .
RUN CXX=clang++-16 STATIC=1 sh build.sh

FROM scratch
COPY --from=build /src/build/shopping /app/shopping
COPY cpp/pipeline/LICENSE /app/LICENSE
USER 1000:1000
CMD ["/app/shopping"]
