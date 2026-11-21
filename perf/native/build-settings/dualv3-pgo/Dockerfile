FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ python3-minimal && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY cpp cpp
COPY build.sh .
COPY perf/train.py perf/train.py
RUN PGO=1 sh build.sh

FROM debian:bookworm-slim
COPY --from=build /src/build/shopping /src/build/shopping.opt /app/
USER 1000:1000
CMD ["/app/shopping"]
