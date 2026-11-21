FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY cpp cpp
COPY build.sh .
RUN sh build.sh

FROM debian:bookworm-slim
COPY --from=build /src/build/shopping /app/shopping
USER 1000:1000
CMD ["/app/shopping"]
