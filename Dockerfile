FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    liburing-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release \
    && cmake --build cmake-build-release --parallel

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    liburing2 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/cmake-build-release/bangserver /app/

RUN mkdir -p /etc/bangserver

EXPOSE 8080

VOLUME ["/etc/bangserver"]

CMD ["./bangserver"]