![C](https://img.shields.io/badge/language-C-blue)
[![MIT License](https://img.shields.io/badge/License-MIT-brightgreen.svg?)](https://github.com/kala13x/libxutils/blob/main/LICENSE)
[![Linux](https://github.com/kala13x/libxutils/actions/workflows/linux.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/linux.yml)
[![MacOS](https://github.com/kala13x/libxutils/actions/workflows/macos.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/macos.yml)
[![Windows](https://github.com/kala13x/libxutils/actions/workflows/windows.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/windows.yml)
[![CodeQL](https://github.com/kala13x/libxutils/actions/workflows/codeql.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/codeql.yml)

## libxutils - Cross-platform C library release 2.x

`libxutils` is a low-level cross-platform `C` library designed to eliminate the need for stitching together dozens of unrelated libraries.

It provides a unified, event-driven runtime with consistent APIs for networking, data structures, cryptography and system utilities, optimized for performance-critical native applications.

The library targets `Linux`, `Unix` and `Windows` and is released under the `MIT` license.

## What makes it strong

- One consistent low-level stack instead of multiple libraries
- Explicit memory and ownership model
- Event-driven architecture
- Minimal dependencies with optional `OpenSSL`
- Designed for high-performance native software where control and predictability matter

## Core strength: networking

The strongest side of `libxutils` is networking.

Instead of combining separate libraries for sockets, event loops, HTTP, WebSocket, SSL and protocol glue, `libxutils` keeps them inside one library with shared conventions for buffers, callbacks, ownership and runtime flow.

Built-in networking pieces include:

- Raw sockets, Unix sockets, `TCP` and `UDP`
- `SSL` (optional)
- `HTTP`
- `WebSocket`
- `MDTP`
- Cross-platform event loop integration

HTTP, WebSocket, events, timers and raw TCP share the same callback model, endpoint setup and event loop. Switch between protocols by changing a single enum — the rest of your code stays the same. More importantly, they can coexist: a single `xapi_t` instance can serve an HTTP API on one port, a WebSocket feed on another and a raw TCP control channel on a third, all multiplexed through one `XAPI_Service` loop with no threading required.

## Typical use cases

- High-performance network services such as `HTTP` and `WebSocket` servers, gateways and proxies
- CLI tools that need networking, JSON, crypto and filesystem support in one binary
- Embedded or system-level software with minimal dependency requirements
- Custom runtimes and protocol implementations
- Native backends exposed to higher-level languages through `FFI`

## Why libxutils?

libxutils started as a personal utility library in 2015 and has evolved over years of real-world use.

It reflects a design approach focused on performance, control, and avoiding unnecessary abstractions.

## Compared to other stacks

| Feature | libxutils | libuv | boost::asio | curl + ad-hoc stack |
|---|---|---|---|---|
| Integrated full stack | ✔️ | ❌ | ❌ | ❌ |
| Event loop | ✔️ | ✔️ | ✔️ | ❌ |
| Built-in HTTP/WebSocket | ✔️ | ❌ | ❌ | ❌ |
| Built-in crypto helpers | ✔️ | ❌ | ❌ | ❌ |
| Minimal external deps | ✔️ | ✔️ | ❌ | ❌ |

This comparison reflects what each library provides out of the box, without requiring additional libraries or integrations.

## Documentation

Full documentation is available in the [docs](docs/README.md) directory.

- Start with [docs/README.md](docs/README.md) for the full index
- Category indexes are available in [docs/crypt/README.md](docs/crypt/README.md), [docs/data/README.md](docs/data/README.md), [docs/net/README.md](docs/net/README.md) and [docs/sys/README.md](docs/sys/README.md)
- The `docs/` tree documents the real behavior from `src/`, including arguments, return values, callback contracts and known quirks

## Library modules

Each module below links to documentation with API contracts and behavior.

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

## Quick example

Minimal event-driven HTTP server example. For a more complete implementation with logging, timeouts, SSL and full connection lifecycle handling, see [`examples/http-server.c`](examples/http-server.c).

This example shows the core event-driven flow:

- accept a connection
- read HTTP request
- assemble a response
- send it back
- close the session

```c
#include <xutils/api.h>

static int on_request(xapi_session_t *s)
{
    xhttp_t *req = (xhttp_t*)s->pPacket;
    printf("Request: %s %s\n", XHTTP_GetMethodStr(req->eMethod), req->sUri);

    xhttp_t res;
    XHTTP_InitResponse(&res, 200, NULL);
    XHTTP_AddHeader(&res, "Content-Type", "text/plain");

    const char *body = "Hello from libxutils\n";
    XHTTP_Assemble(&res, (const uint8_t*)body, strlen(body));

    XByteBuffer_AddBuff(&s->txBuffer, &res.rawData);
    XHTTP_Clear(&res);

    return XAPI_EnableEvent(s, XPOLLOUT);
}

static int callback(xapi_ctx_t *ctx, xapi_session_t *s)
{
    switch (ctx->eCbType)
    {
        case XAPI_CB_ACCEPTED:
            return XAPI_SetEvents(s, XPOLLIN);

        case XAPI_CB_READ:
            return on_request(s);

        case XAPI_CB_WRITE:
            return XAPI_DISCONNECT;

        default:
            return XAPI_CONTINUE;
    }
}

int main(void)
{
    xapi_t api;
    XAPI_Init(&api, callback, NULL);

    xapi_endpoint_t ep;
    XAPI_InitEndpoint(&ep);

    ep.pAddr = "0.0.0.0";
    ep.nPort = 8080;
    ep.eType = XAPI_HTTP;
    ep.eRole = XAPI_SERVER;
    XAPI_AddEndpoint(&api, &ep);

    while (XAPI_Service(&api, 100) == XEVENTS_SUCCESS);

    XAPI_Destroy(&api);
    return 0;
}
```

## Installation

There are several ways to build and install the library.

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

Include the required `<xutils/*.h>` headers in your project and link with `-lxutils`.

For exact runtime behavior, use the documentation in [docs/](docs/README.md) instead of relying on symbol names alone.

## Examples and tools

The project contains two useful reference areas:

- `examples/` contains smaller and simpler examples focused on showing how to use specific parts of the library
- `tools/` contains larger, more advanced examples in the form of ready-made CLI applications built on top of the library

You can build both with:

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

## XTOP and included tools

<p align="center">
    <img src="https://raw.githubusercontent.com/kala13x/libxutils/main/examples/xtop.png" alt="xtop screenshot">
</p>

`XTOP` is an `HTOP`-style performance monitor that displays CPU, memory and network activity in a single CLI window. It is also a good example of how much can be built on top of the library without adding a large external stack.

After building the sources in `tools/`, run `sudo make install` to install:

- `xcrypt` - file and text encrypt/decrypt CLI tool
- `xpass` - secure password manager CLI tool
- `xjson` - JSON linter and minifier CLI tool
- `xhttp` - HTTP client CLI tool
- `xhost` - hosts-file search and modification CLI tool
- `xtop` - resource monitor CLI tool
- `xsrc` - advanced file search CLI tool

Run each tool with `-h` to see usage and version information.
