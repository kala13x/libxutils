[![MIT License](https://img.shields.io/badge/License-MIT-brightgreen.svg?)](https://github.com/kala13x/libxutils/blob/main/LICENSE)
[![Linux](https://github.com/kala13x/libxutils/actions/workflows/linux.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/linux.yml)
[![MacOS](https://github.com/kala13x/libxutils/actions/workflows/macos.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/macos.yml)
[![Windows](https://github.com/kala13x/libxutils/actions/workflows/windows.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/windows.yml)
[![CodeQL](https://github.com/kala13x/libxutils/actions/workflows/codeql.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/codeql.yml)

## libxutils - Cross-platform C library release 2.x

`libxutils` is a low-level cross-platform `C` library for building real native software without pulling in a large dependency stack. It combines containers, strings, JSON/JWT helpers, cryptography, sockets, HTTP/WebSocket, event loops, filesystems, logging, threads, synchronization and time utilities in one consistent codebase.

The library targets `Linux`, `Unix` and `Windows` and is released under the `MIT` license.

## Why libxutils

- It gives you one cohesive low-level stack instead of stitching together many unrelated libraries for buffers, strings, sockets, events, JSON, crypto and system utilities.
- It is designed around explicit ownership, explicit buffers and event-driven I/O, which makes it practical for performance-sensitive native applications.
- The networking side is unusually complete for a utility library: raw sockets, Unix sockets, TCP/UDP, SSL, HTTP, WebSocket, MDTP and a cross-platform event loop live in the same runtime.
- It stays close to C instead of hiding behavior behind heavy abstractions. That makes integration easier for `C`, `C++`, `Rust`, `Objective-C` and FFI-heavy projects.
- The dependency surface is small. `OpenSSL` is only needed for SSL/RSA-related functionality; most of the library builds without it.

## Documentation

Full documentation is available in the [docs](docs/README.md) directory.

- Start with [docs/README.md](docs/README.md) for the top-level index.
- Category indexes are available in [docs/crypt/README.md](docs/crypt/README.md), [docs/data/README.md](docs/data/README.md), [docs/net/README.md](docs/net/README.md) and [docs/sys/README.md](docs/sys/README.md).
- The `docs/` tree documents the real source behavior from `src/`, including argument meaning, return values, callback contracts and known quirks.

## Module map

### Data and containers

- [Dynamic array](docs/data/array.md)
- [Byte/data buffers](docs/data/buf.md)
- [Hash map](docs/data/hash.md)
- [Key/value map](docs/data/map.md)
- [Linked list](docs/data/list.md)
- [String utilities and dynamic string](docs/data/str.md)
- [JSON parser/writer](docs/data/json.md)
- [JWT helpers](docs/data/jwt.md)

### Networking

- [High-level event-driven API for HTTP, WebSocket, MDTP and raw sockets](docs/net/api.md)
- [Cross-platform socket layer](docs/net/sock.md)
- [Cross-platform event loop](docs/net/event.md)
- [HTTP parser/client helpers](docs/net/http.md)
- [WebSocket framing](docs/net/ws.md)
- [MDTP packet protocol](docs/net/mdtp.md)
- [RTP packet helpers](docs/net/rtp.md)
- [NTP helpers](docs/net/ntp.md)
- [Address/interface helpers](docs/net/addr.md)

### Cryptography and encoding

- [Cryptography dispatch helpers](docs/crypt/crypt.md)
- [Base64 and Base64Url](docs/crypt/base64.md)
- [AES helpers](docs/crypt/aes.md)
- [HMAC](docs/crypt/hmac.md)
- [RSA helpers](docs/crypt/rsa.md)
- [SHA-256](docs/crypt/sha256.md)
- [SHA-1](docs/crypt/sha1.md)
- [CRC32](docs/crypt/crc32.md)
- [MD5](docs/crypt/md5.md)

### System and runtime

- [File and directory operations](docs/sys/xfs.md)
- [CPU affinity helpers](docs/sys/cpu.md)
- [Recursive file/content search](docs/sys/srch.md)
- [Time helpers](docs/sys/xtime.md)
- [Resource monitoring](docs/sys/mon.md)
- [Memory pool](docs/sys/pool.md)
- [Logging](docs/sys/log.md)
- [Synchronization primitives](docs/sys/sync.md)
- [Threading and repeating tasks](docs/sys/thread.md)
- [CLI helpers and progress bars](docs/sys/cli.md)
- [Signal and daemon helpers](docs/sys/sig.md)
- [Small type/format helpers](docs/sys/type.md)

### Miscellaneous

- [Version helpers](docs/xver.md)

## Installation

There are several ways to build and install the project.

### Using the included script

This is the simplest path on Linux and macOS:

```bash
git clone https://github.com/kala13x/libxutils
./libxutils/build.sh --install
```

Supported build-script options:

- `--tool=<tool>` choose `cmake`, `smake` or the included `make`
- `--install` install the library and tools after building
- `--examples` include examples in the build
- `--tools` include tools in the build
- `--clean` remove object files after build/install

If `--tool` is omitted, the script uses `cmake` by default.

### Using CMake

```bash
git clone https://github.com/kala13x/libxutils
cd libxutils
cmake . && make
sudo make install
```

### Using SMake

[SMake](https://github.com/kala13x/smake) is a simple Makefile generator for Linux/Unix:

```bash
git clone https://github.com/kala13x/libxutils
cd libxutils
smake && make
sudo make install
```

### Using the included Makefile

The project can also be built with the pre-generated `Makefile` on Linux:

```bash
export XUTILS_USE_SSL=y # Enable SSL-related features
git clone https://github.com/kala13x/libxutils
cd libxutils
make
sudo make install
```

## Dependencies

`OpenSSL` is the only external dependency and is only required for `SSL` and `RSA` functionality. If the development package is missing, the library can still be built without SSL support.

### Install OpenSSL development package

Red Hat family: `sudo dnf install openssl-devel`  
Debian family: `sudo apt-get install libssl-dev`  
Windows: `choco install openssl`  
macOS: `brew install openssl`

## Usage

Include the required `<xutils/*.h>` headers in your program and link with `-lxutils`.

For API contracts and exact runtime behavior, use the documentation in [docs/](docs/README.md) instead of relying on symbol names alone.

## Tools and examples

Use the build script to include CLI tools from `tools/` and sample programs from `examples/`:

```bash
./libxutils/build.sh --tools --examples
```

You can also build examples manually:

```bash
export XUTILS_USE_SSL=y # Enable SSL-related features
cd examples
cmake .
make
```

## XTOP and more

<p align="center">
    <img src="https://raw.githubusercontent.com/kala13x/libxutils/main/examples/xtop.png" alt="xtop screenshot">
</p>

`XTOP` is an `HTOP`-style performance monitor that can display CPU, memory and network activity in a single CLI window. It also supports additional client/server and REST-oriented workflows.

After building the sources in `tools/`, run `sudo make install` to install:

- `xcrypt` - file and text encrypt/decrypt CLI tool
- `xpass` - secure password manager CLI tool
- `xjson` - JSON linter and minifier CLI tool
- `xhttp` - HTTP client CLI tool
- `xhost` - hosts-file search and modification CLI tool
- `xtop` - resource monitor CLI tool
- `xsrc` - advanced file search CLI tool

Run each tool with `-h` to see usage and version information.
