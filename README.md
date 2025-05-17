# BangServer

A high-performance C++ server for DuckDuckGo bang command processing.

[![Docker Build and Publish](https://github.com/rebelonion/BangServer/actions/workflows/docker-build.yml/badge.svg)](https://github.com/YOUR_USERNAME/BangServer/actions/workflows/docker-build.yml)
[![Release](https://github.com/rebelonion/BangServer/actions/workflows/release.yml/badge.svg)](https://github.com/YOUR_USERNAME/BangServer/actions/workflows/release.yml)

## Overview

BangServer is an ultra-fast, multi-threaded C++ application designed to process DuckDuckGo bang commands with minimal
latency.
This project aims to provide the fastest possible lookup and redirection for bang commands.

## Performance

Benchmarks:

| CPU              | Threads | Queries/second | Time per query (µs) |
|------------------|---------|----------------|---------------------|
| AMD EPYC 9754    | 256     | 831,665,000    | 0.0012              |
| Intel i9-12900KF | 24      | 86,000,900     | 0.0116              |
| Intel i9-12900KF | 8       | 42,907,400     | 0.0233              |
| Intel i9-12900KF | 1       | 8,148,490      | 0.1227              |

## Running

### Docker

```bash
# Run with default configuration
docker run -p 8080:8080 ghcr.io/rebelonion/bangserver:latest

# Run with custom configuration
docker run -p 8080:8080 -v $(pwd)/config:/etc/bangserver:ro ghcr.io/rebelonion/bangserver:latest
```

### Native

```bash
# Run the server
./cmake-build-debug/bangserver

# Run benchmarks
./cmake-build-release/bangbenchmark -t <threads>

# More options can be found with --help
```

## Building

### Native Build

```bash
# Debug build
cmake -B cmake-build-debug && cmake --build cmake-build-debug

# Release build (recommended for performance)
cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release
```

### Docker Build

```bash
# Build the Docker image
docker build -t bangserver .

# Or using Docker Compose
docker-compose build
```

## Configuration

BangServer supports configuration through TOML files and environment variables.

### Server Configuration

You can configure server settings using a TOML file or environment variables.

#### Using TOML Config File

Create a `bangserver.toml` file in one of these locations:

- Current directory
- User's config directory (`~/.config/bangserver/` on Linux)
- System-wide config directory (`/etc/bangserver/` on Linux)

Example `bangserver.toml`:

```toml
# Server settings
[server]
port = 3000                   # Port to listen on
backlog = 5                   # Connection backlog
queue_depth = 256             # I/O queue depth for liburing
request_buffer_size = 4096    # Maximum request size
num_threads = 8               # Optional: number of threads to use (auto if not specified)

# Search settings
[search]
default_url = "https://www.google.com/search?q="   # Default search engine URL

# Bang providers configuration
[[providers]]
source = "https://duckduckgo.com/bang.js"  # DuckDuckGo public API
format = "json"
required = true

[[providers]]
source = "bangs.json"         # Custom bangs in JSON format (local file)
format = "json"
required = false

[[bangs]]                   # Bangs can be added in the server config as well
trigger = "custom"
url_template = "https://customsearch.com/?q={{{s}}}"
```

#### Using Environment Variables

You can also set configuration with these environment variables:

- `BANG_PORT` - Server port
- `BANG_BACKLOG` - Connection backlog
- `BANG_DEFAULT_SEARCH` - Default search URL
- `BANG_THREADS` - Number of worker threads
- `BANG_CONFIG_FILE` - Path to custom config file

Example:

```bash
BANG_PORT=8080 BANG_DEFAULT_SEARCH="https://duckduckgo.com/?q=" ./cmake-build-release/bangserver
```

#### Docker Configuration

When using Docker, you can configure the server in several ways:

1. Creating a config directory with all your configuration files:

```bash
mkdir -p config
cp bangserver.example.toml config/bangserver.toml
cp bangs.example.json config/bangs.json
```

2. Using environment variables alongside your config directory:

```bash
docker run -p 8080:8080 \
  -v $(pwd)/config:/etc/bangserver:ro \
  -e BANG_PORT=8080 \
  -e BANG_DEFAULT_SEARCH="https://duckduckgo.com/?q=" \
  bangserver
```

3. With Docker Compose (edit docker-compose.yml):

```yaml
services:
  bangserver:
    # ...
    environment:
      - BANG_PORT=8080
      - BANG_DEFAULT_SEARCH=https://duckduckgo.com/?q=
    volumes:
      - ./config:/etc/bangserver:ro
```

### Configuration Locations

The application searches for configuration files in the following locations (in order of priority):

1. Path specified by the `BANG_CONFIG_FILE` environment variable
2. Current directory
3. Platform-specific user config directories:
    - **Linux**: `~/.config/bangserver/`
    - **Windows**: `%APPDATA%\BangServer\` and `%LOCALAPPDATA%\BangServer\`
    - **macOS**: `~/Library/Application Support/BangServer/` and `~/.config/bangserver/`
4. System-wide (Linux only): `/etc/bangserver/`

If no configuration file is found or no providers are configured, the application falls back to:

1. Always load from DuckDuckGo API
2. Check for `BANG_CONFIG_FILE` environment variable

### Custom Bangs

You can add custom bangs or override existing ones in either JSON or TOML format.

#### JSON Format

Bang definition file in JSON format:

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

#### TOML Format

Bang definition file in TOML format:

```toml
[[bangs]]
trigger = "example"
url_template = "https://example.com/search?q={{{s}}}"
domain = "example.com"
short_name = "Example Search"
category = "Research"
subcategory = "Reference"

[[bangs]]
trigger = "custom"
url_template = "https://customsearch.com/?q={{{s}}}"
```

#### Required Fields

- JSON: `t` (trigger), `u` (URL template)
- TOML: `trigger`, `url_template`

Use `{{{s}}}` as placeholder for the search query in either format.

#### Optional Fields

- `d`/`domain`: Website domain
- `s`/`short_name`: Display name
- `c`/`category`: Category (Entertainment, Multimedia, News, Online Services, Research, Shopping, Tech, Translation)
- `sc`/`subcategory`: Subcategory
- `r`/`relevance`: Relevance ranking

If a custom bang has the same trigger as an existing bang, it will override the original.

## Technical Details

- Written in C++23
- Modular design with source/format separation
- Uses Abseil flat_hash_map for optimal lookups
- JSON parsing with simdjson
- TOML parsing with toml11
- Uses raw sockets and liburing for high-performance networking
- SIMD optimizations for performance

## License

This project is licensed under the MIT License. See the [LICENSE.md](LICENSE.md) file for details.