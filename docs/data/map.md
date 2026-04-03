# map.c

## Purpose

Open-addressing string-key map with linear probing and tombstones.

## API Reference

### Lifecycle

#### `int XMap_Init(xmap_t *pMap, xpool_t *pPool, uint32_t nSize)`

- Initializes map storage and metadata.
- Default hash type becomes `XMAP_HASH_FNV`.
- Returns `XMAP_OK` or negative error.

#### `xmap_t *XMap_New(xpool_t *pPool, uint32_t nSize)`

- Allocates and initializes a map.
- Returns map or `NULL`.

#### `int XMap_Realloc(xmap_t *pMap)`

- Grows table size, rehashes active entries.
- Returns `XMAP_OK` or negative error.

#### `void XMap_Reset(xmap_t *pMap)`

- Clears used pairs, converts deleted tombstones back to unused, resets counts.

#### `void XMap_Destroy(xmap_t *pMap)`

#### `void XMap_Free(xmap_t *pMap)`

- `Destroy` resets then frees.
- `Free` only frees structure/storage state.

### Hash selection

#### `int XMap_HashFNV(xmap_t *pMap, const char *pStr)`

#### `int XMap_Hash(xmap_t *pMap, const char *pStr)`

- Return bucket index or negative error.

#### `int XMap_HashMIX / XMap_HashCRC32 / XMap_HashSHA256(...)`

- Available only with `_XMAP_USE_CRYPT`.
- Return bucket index or negative error.

#### `int XMap_GetHash(xmap_t *pMap, const char *pKey)`

- Returns:
  - existing key slot index,
  - first reusable tombstone/empty slot,
  - or negative error / `XMAP_FULL`.

### Insert / update

#### `int XMap_Put(xmap_t *pMap, char *pKey, void *pValue)`

- Auto-grows on init or load >= 70%.
- Stores raw key pointer and value pointer without deep copy.
- Returns `XMAP_OK`, `XMAP_EEXIST` when updates are disabled, or negative error.

#### `int XMap_PutPair(xmap_t *pMap, xmap_pair_t *pPair)`

- Inserts pair key/value through `XMap_Put`.

#### `int XMap_Update(xmap_t *pMap, int nHash, void *pValue)`

- Replaces value at a known slot index.
- Returns `XMAP_OK` or negative error.

### Lookup / remove

#### `xmap_pair_t *XMap_GetPair(xmap_t *pMap, const char *pKey)`

#### `void *XMap_GetIndex(xmap_t *pMap, const char *pKey, int *pIndex)`

#### `void *XMap_Get(xmap_t *pMap, const char *pKey)`

- Return pair pointer, value plus slot index, or value only.
- Return `NULL` when missing.

#### `int XMap_Remove(xmap_t *pMap, const char *pKey)`

- Marks slot as deleted tombstone and runs `clearCb` if configured.
- Returns `XMAP_OK`, `XMAP_MISSING` or negative error.

### Iteration / stats

#### `int XMap_Iterate(xmap_t *pMap, xmap_iterator_t itfunc, void *pCtx)`

- Calls iterator for every used pair.
- Stops on first iterator status not equal to `XMAP_OK`.

#### `int XMap_UsedSize(xmap_t *pMap)`

- Returns active pair count or negative error.
