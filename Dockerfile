FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ python3-minimal && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY cpp/pipeline cpp/pipeline
COPY build.sh .
ARG PAIRED_CLICK_SORT=0
ARG CUSTOM_MEMORY=0
ARG DIRECT_DICT_LOOPS=0
ARG INTEL_LAYOUT=0
ARG V4_SIDECAR=0
RUN STATIC=1 sh build.sh

FROM scratch
COPY --from=build /src/build/shopping* /app/
COPY cpp/pipeline/LICENSE /app/LICENSE
USER 1000:1000
CMD ["/app/shopping"]
