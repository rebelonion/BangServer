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

## Custom Bangs

You can add custom bangs or override existing ones by creating a JSON file with your bangs.

### Usage

1. Create a `bangs.json` file in the same directory as the application, or
2. Set the `BANG_CONFIG_FILE` environment variable to the path of your custom bangs file

```bash
# Using environment variable
BANG_CONFIG_FILE=/path/to/your/custom-bangs.json ./cmake-build-release/bangserver
```

### File Format

The JSON file should contain an array of bang objects with the same format as the DuckDuckGo API:

```json
[
  {
    "t": "example",
    "u": "https://example.com/search?q={{{s}}}",
    "d": "example.com",
    "s": "Example Search"
  },
  {
    "t": "custom",
    "u": "https://customsearch.com/?q={{{s}}}"
  }
]
```

Required fields:
- `t`: Trigger (without the leading `!`)
- `u`: URL template (use `{{{s}}}` as placeholder for the search query)

Optional fields:
- `d`: Domain
- `s`: Short name
- `c`: Category
- `sc`: Subcategory
- `r`: Relevance

If a custom bang has the same trigger as an existing bang, it will override the original.

## Technical Details

- Written in C++23
- Uses Abseil flat_hash_map for optimal lookups
- Json parsing with simdjson
- Uses raw sockets and liburing for high-performance networking
- SIMD optimizations for performance

## License

This project is licensed under the MIT License. See the [LICENSE.md](LICENSE.md) file for details.