# hash.c

## Purpose

Fixed-bucket chained hash table keyed by integer key.

## API Reference

### `void XHash_Init(xhash_t *pHash, xhash_clearcb_t clearCb, void *pCtx)`

- Initializes all buckets and stores optional clear callback/context.
- No return value.

### `void XHash_Destroy(xhash_t *pHash)`

- Clears all buckets, runs clear callback for each pair when provided.

### `void XHash_Iterate(xhash_t *pHash, xhash_itfunc_t itfunc, void *pCtx)`

- Iterates all used pairs.
- Stops when iterator callback returns non-zero.

### `xhash_pair_t *XHash_NewPair(void *pData, size_t nSize, int nKey)`

- Creates one pair node without duplicating `pData`.
- Returns allocated pair or `NULL`.

### `int XHash_InsertPair(xhash_t *pHash, xhash_pair_t *pData)`

### `int XHash_Insert(xhash_t *pHash, void *pData, size_t nSize, int nKey)`

- Insert ready-made pair or raw payload pointer.
- Return `XSTDOK`/success value or `XSTDERR` on failure.

### `xhash_pair_t *XHash_GetPair(xhash_t *pHash, int nKey)`

### `xlist_t *XHash_GetNode(xhash_t *pHash, int nKey)`

### `void *XHash_GetData(xhash_t *pHash, int nKey)`

### `int XHash_GetSize(xhash_t *pHash, int nKey)`

- Lookup helpers returning pair node, list node, payload pointer or stored size.
- Return `NULL`/`0` when not found.

### `int XHash_Delete(xhash_t *pHash, int nKey)`

- Removes one pair immediately.
- Returns success/failure status.
