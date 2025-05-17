FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    liburing-dev \
    pkg-config \
    g++-13 \
    && rm -rf /var/lib/apt/lists/*

# Use GCC 13 which has full C++23 support
ENV CC=gcc-13
ENV CXX=g++-13

WORKDIR /app

COPY . .

RUN cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=23 \
    && cmake --build cmake-build-release --parallel

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    liburing2 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/cmake-build-release/bangserver /app/

RUN mkdir -p /etc/bangserver

EXPOSE 8080

VOLUME ["/etc/bangserver"]

CMD ["./bangserver"]