# pool.c

## Purpose

Chained bump allocator with optional malloc/realloc/free wrappers.

## API Reference

#### `XSTATUS XPool_Init(xpool_t *pPool, size_t nSize)`

- Arguments:
  - `pPool`: pool object to initialize.
  - `nSize`: initial chunk size, defaults to `XPOOL_DEFAULT_SIZE` when zero.
- Does:
  - aligns the chunk size to 8 bytes.
  - allocates the first pool buffer.
  - clears usage counters and chain link.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` on allocation failure.

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
  - frees the current chunk.
  - recursively destroys `pNext`.
  - frees the pool struct itself when `bAlloc` is set.
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
  - allocates from the current chunk when space is available.
  - otherwise creates or reuses `pNext` and continues there.
- Returns:
  - pointer to pool-owned memory.
  - `NULL` on invalid args or allocation failure.

#### `void *XPool_Realloc(xpool_t *pPool, void *pData, size_t nDataSize, size_t nNewSize)`

- Arguments:
  - pool, old pointer, old size and new size.
- Does:
  - allocates a new region from the pool.
  - copies `min(nDataSize, nNewSize)` bytes.
  - calls `XPool_Free()` on the old region.
- Returns:
  - new pointer on success.
  - `NULL` on invalid args or allocation failure.

#### `void XPool_Free(xpool_t *pPool, void *pData, size_t nSize)`

- Arguments:
  - pool chain, pointer and allocation size.
- Does:
  - when `pPool == NULL`, falls back to `free(pData)`.
  - otherwise reclaims memory only in two cases:
    - the region spans the whole used prefix of a chunk
    - the region is exactly the last allocation in a chunk
  - middle-of-pool frees are ignored intentionally.
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
  - returns current chunk capacity or used bytes.
- Returns:
  - size/used count.
  - `0` when `pPool == NULL`.

## Important Notes

- This is not a general-purpose free-store allocator.
- `XPool_Free()` is intentionally conservative; many frees are no-ops.
