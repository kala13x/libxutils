# list.c

## Purpose

Doubly linked list with optional per-node cleanup callback.

## API Reference

### Node-data helpers

#### `void XListData_Init(xlist_data_t *pListData, void *pData, size_t nSize, xlist_cb_t onClear, void *pCbCtx)`

- Fills one `xlist_data_t`.

#### `void XListData_MergeCtx(xlist_data_t *pNext, xlist_data_t *pPrev)`

- Copies cleanup callback/context from `pPrev` into `pNext` when missing.

### Lifecycle

#### `void XList_Init(xlist_t *pList, void *pData, size_t nSize, xlist_cb_t onClear, void *pCtx)`

#### `xlist_t *XList_New(void *pData, size_t nSize, xlist_cb_t onClear, void *pCtx)`

- Initialize stack node or allocate heap node.
- `New` returns node or `NULL`.

#### `void XList_Free(xlist_t *pList)`

- Runs `onClear` when present, else frees `pData` when `nSize > 0`.
- Frees the node when heap allocated, otherwise reinitializes it.

#### `void XList_Clear(xlist_t *pList)`

- Unlinks/frees all nodes in the chain.

### Structure editing

#### `void XList_Detach(xlist_t *pList)`

- Detaches one node from neighbors without freeing it.

#### `xlist_t *XList_Unlink(xlist_t *pList)`

- Detaches and frees one node.
- Returns `next` when available, else `prev`.

#### `xlist_t *XList_RemoveHead(xlist_t *pList)`

#### `xlist_t *XList_RemoveTail(xlist_t *pList)`

- Remove head/tail and return adjacent survivor.

#### `xlist_t *XList_GetHead(xlist_t *pList)`

#### `xlist_t *XList_GetTail(xlist_t *pList)`

- Return head/tail pointer or `NULL`.

#### `xlist_t *XList_InsertPrev/InsertNext/InsertHead/InsertTail(...)`

- Insert `pNode` relative to an existing chain.
- Return inserted node or neighboring node depending on exact helper.
- Return `NULL` on invalid args.

#### `xlist_t *XList_PushPrev/PushNext/PushFront/PushBack(...)`

- Allocate a new node and insert it in the requested position.
- Return new node or `NULL`.

### Search / remove

#### `xlist_t *XList_Search(xlist_t *pList, void *pUserPtr, xlist_cmp_t compare)`

- Calls comparator for each node from head.
- Returns first node where comparator returns `> 0`.
- Stops early if comparator returns `< 0`.
- Returns `NULL` when no match.

#### `xlist_t *XList_Remove(xlist_t *pList, void *pUserPtr, xlist_cmp_t compare)`

- Finds with `Search` and then unlinks that node.
- Returns adjacent node after removal or `NULL`.

### Ring helpers

#### `xlist_t *XList_MakeRing(xlist_t *pList)`

- Connects head.prev to tail and tail.next to head.
- Returns head.

#### `uint8_t XList_IsRing(xlist_t *pList)`

- Returns non-zero when traversal loops back to start.
