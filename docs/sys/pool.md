# pool.c

## Purpose

Chained bump allocator with optional malloc/realloc/free wrappers.

## API Reference

#### `XSTATUS XPool_Init(xpool_t *pPool, size_t nSize)`

- Arguments:
  - `pPool`: pool object to initialize.
  - `nSize`: initial chunk size, defaults to `XPOOL_DEFAULT_SIZE` when zero.
- Does:
  - aligns the chunk size to `XPOOL_ALIGNMENT`.
  - allocates the first pool buffer.
  - clears usage counters, chain link and tail hint.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` on `NULL` pool, allocation failure or when the
    size can not be aligned without overflowing `size_t`.

#### `xpool_t *XPool_Create(size_t nSize)`

- Arguments:
  - `nSize`: initial chunk size.
- Does:
  - heap-allocates an `xpool_t` and initializes it.
- Returns:
  - allocated pool on success.
  - `NULL` on allocation/init failure.

#### `void XPool_Destroy(xpool_t *pPool)`

- Arguments:
  - `pPool`: pool chain root.
- Does:
  - walks the chain and frees every chunk buffer.
  - frees each pool struct itself when its `bAlloc` is set.
- Returns:
  - no return value.

#### `void XPool_Reset(xpool_t *pPool)`

- Arguments:
  - `pPool`: pool chain root.
- Does:
  - resets `nUsed` to zero in the current chunk and all chained chunks.
- Returns:
  - no return value.

#### `void *XPool_Alloc(xpool_t *pPool, size_t nSize)`

- Arguments:
  - `pPool`: pool chain root.
  - `nSize`: requested allocation size.
- Does:
  - rounds the size up to `XPOOL_ALIGNMENT`, so every returned
    pointer is properly aligned for any pool-allocated structure.
  - allocates from the tail chunk when space is available.
  - otherwise walks the chain looking for a leftover space.
  - appends a new chunk to the chain as a last resort.
- Returns:
  - pointer to pool-owned memory.
  - `NULL` on invalid args, allocation failure or when the
    requested size overflows while being aligned.

#### `void *XPool_Realloc(xpool_t *pPool, void *pData, size_t nDataSize, size_t nNewSize)`

- Arguments:
  - pool, old pointer, old size and new size.
- Does:
  - resizes in place and returns the same pointer when the region
    is the last allocation of its chunk and the chunk has room.
  - otherwise allocates a new region from the pool, copies
    `min(nDataSize, nNewSize)` bytes and calls `XPool_Free()`
    on the old region.
- Returns:
  - old or new pointer on success.
  - `NULL` on invalid args or allocation failure.

#### `void XPool_Free(xpool_t *pPool, void *pData, size_t nSize)`

- Arguments:
  - pool chain, pointer and allocation size.
- Does:
  - when `pPool == NULL`, falls back to `free(pData)`.
  - rounds the size up the same way `XPool_Alloc()` does, so the
    caller can pass the size it originally asked for.
  - otherwise reclaims memory only in two cases:
    - the region spans the whole used prefix of a chunk
    - the region is exactly the last allocation in a chunk
  - middle-of-pool frees are ignored intentionally.
  - pointers that belong to no chunk of the chain are ignored,
    they are never passed to `free()`.
- Returns:
  - no return value.

#### `void *xalloc(xpool_t *pPool, size_t nSize)`

#### `void *xrealloc(xpool_t *pPool, void *pData, size_t nDataSize, size_t nNewSize)`

- Arguments:
  - optional pool plus allocation parameters.
- Does:
  - dispatches to `malloc/realloc` when `pPool == NULL`.
  - otherwise dispatches to `XPool_Alloc()` / `XPool_Realloc()`.
- Returns:
  - allocated pointer or `NULL`.

#### `void xfree(xpool_t *pPool, void *pData)`

#### `void xfreen(xpool_t *pPool, void *pData, size_t nSize)`

- Arguments:
  - optional pool, pointer and optional known size.
- Does:
  - `xfree()` calls `free()` only when `pPool == NULL`.
  - `xfreen()` forwards to `xfree()` when `nSize == 0`, otherwise uses `XPool_Free()`.
- Returns:
  - no return value.

#### `size_t XPool_GetSize(xpool_t *pPool)`

#### `size_t XPool_GetUsed(xpool_t *pPool)`

- Arguments:
  - pool object.
- Does:
  - sums the capacity or the used bytes of every chunk in the chain.
- Returns:
  - size/used count.
  - `0` when `pPool == NULL`.

## Important Notes

- This is not a general-purpose free-store allocator.
- `XPool_Free()` is intentionally conservative; many frees are no-ops.
- Every returned block is aligned to `XPOOL_ALIGNMENT` (8 bytes), which
  also means a chunk holds slightly fewer objects than its raw size.
- The pool is not thread safe, external synchronization is required.
- `XPool_Init()` expects an uninitialized object, re-initializing a pool
  that is already in use leaks its buffer and the rest of the chain.
- Sizes passed to `XPool_Free()`/`XPool_Realloc()` must match the size
  the block was allocated with. An oversized value can reset a chunk
  that still holds live blocks, and freeing a stale pointer twice can
  hand the same memory out twice.
