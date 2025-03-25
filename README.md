# BangServer

A high-performance C++ server for DuckDuckGo bang command processing.

## Overview

BangServer is an ultra-fast, multi-threaded C++ application designed to process DuckDuckGo bang commands with minimal latency. 
This project aims to provide the fastest possible lookup and redirection for bang commands.

## Performance

Benchmarks on Intel i9-12900KF:

| Threads | Queries/second | Time per query (µs) |
|---------|-----------------|---------------------|
| 24      | 86,000,900      | 0.0116              |
| 8       | 42,907,400      | 0.0233              |
| 1       | 8,148,490       | 0.1227              |

## Building

```bash
# Debug build
cmake -B cmake-build-debug && cmake --build cmake-build-debug

# Release build (recommended for performance)
cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release
```

## Running

```bash
# Run the server
./cmake-build-debug/bangserver

# Run benchmarks
./cmake-build-release/bangbenchmark -t <threads>

# More options can be found with --help
```

## Technical Details

- Written in C++23
- Uses Abseil flat_hash_map for optimal lookups
- Json parsing with simdjson
- Uses raw sockets and liburing for high-performance networking
- SIMD optimizations for performance

## License

This project is licensed under the MIT License. See the [LICENSE.md](LICENSE.md) file for details.