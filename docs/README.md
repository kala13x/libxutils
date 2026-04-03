# xutils Documentation

`xutils` is a low-level C runtime/framework used by the rest of this project for:

- cryptography and encoding helpers
- dynamic containers and JSON/JWT handling
- sockets, HTTP, WebSocket, MDTP and event loops
- threads, synchronization, filesystem, logging and time utilities

This documentation is written from the actual source in `xutils/src`, not from symbol names alone so callers can rely on real semantics.

## Directory Map

- [crypt/README.md](crypt/README.md): hashing, HMAC, AES, RSA, codec and cipher-chain helpers
- [data/README.md](data/README.md): arrays, buffers, maps, hashes, strings, JSON and JWT
- [net/README.md](net/README.md): sockets, event loop, HTTP, WebSocket, MDTP, RTP, NTP and address helpers
- [sys/README.md](sys/README.md): CLI, CPU, monitoring, memory pools, sync, files, time and logging
- [xver.md](xver.md): library version helpers

## Reading Conventions

- `XSTDOK`, `XSTDNON`, `XSTDERR`, `XSTDINV` and similar values come from `xstd.h`. Most modules use them instead of raw `0/-1`.
- Many binary-producing helpers append an extra trailing `NUL` byte for convenience, but the real payload length is still the returned length or `*pLength`.
- Several modules use ownership-transfer APIs. When a function says it "attaches" or "owns" a buffer, the caller must not free the same pointer separately.
- Some modules are Linux-first. The docs note when behavior changes on Windows or when a feature is effectively unavailable outside Linux.

## Documentation Scope

Each category contains:

- a category `README.md` with an overview and link index
- one `.md` file per `.c` file in the matching source directory

The intent is operational correctness: help a new engineer use the library safely, understand callback/return conventions, and avoid the sharp edges already present in the codebase.
