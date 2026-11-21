FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ python3-minimal && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY cpp/pipeline cpp/pipeline
COPY build.sh .
RUN STATIC=1 sh build.sh

FROM scratch
COPY --from=build /src/build/shopping /app/shopping
COPY cpp/pipeline/LICENSE /app/LICENSE
USER 1000:1000
CMD ["/app/shopping"]
