# Data Modules

This section documents `xutils/src/data/*`.

## Files

- [array.md](array.md): dynamic array with optional pool-backed ownership
- [buf.md](buf.md): byte buffers, pointer buffers and ring buffers
- [hash.md](hash.md): chained hash table keyed by integer hash
- [json.md](json.md): JSON parser, builder and formatter
- [jwt.md](jwt.md): JSON Web Token creation, parsing and verification
- [list.md](list.md): doubly linked list utilities
- [map.md](map.md): open-addressing key/value map
- [str.md](str.md): string helpers, tokenization, formatting and dynamic string routines

## Common Rules

- Several containers can either copy payloads or take ownership of an external pointer. The docs call out the difference because it is not uniform across the API surface.
- Pool-backed storage is common. Resetting or destroying a pool-backed container invalidates every pointer allocated from that pool.
- JSON, JWT and HTTP-related code heavily reuse `buf`, `array`, `map` and `str`.
