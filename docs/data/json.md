# json.c

## Purpose

JSON parser, mutable object builder and formatter.

## Core Types

- `xjson_t`: parser state and root object.
- `xjson_obj_t`: one JSON node.
- `xjson_writer_t`: output formatter/writer.
- `xjson_format_t`: pretty/color formatting palette.

## API Reference

### Constructors and object allocation

#### `xjson_obj_t *XJSON_FromStr(xpool_t *pPool, const char *pFmt, ...)`

- Formats input text, parses it as JSON and returns root object.
- Returns parsed root object or `NULL`.

#### `xjson_obj_t *XJSON_CreateObject(xpool_t *pPool, const char *pName, void *pValue, xjson_type_t nType)`

- Creates one node from already prepared payload storage.
- Returns node or `NULL`.

#### `xjson_obj_t *XJSON_NewObject / XJSON_NewArray(...)`

- Create object/array nodes backed by `xmap_t` / `xarray_t`.
- `nAllowUpdate` controls duplicate-key replacement on object insert.
- Return node or `NULL`.

#### `xjson_obj_t *XJSON_NewU64 / XJSON_NewU32 / XJSON_NewU16 / XJSON_NewInt / XJSON_NewFloat / XJSON_NewString / XJSON_NewBool / XJSON_NewNull(...)`

- Build primitive nodes from scalar input or string input.
- Number/bool/null values are stored as text internally.
- Return node or `NULL`.

#### `void XJSON_FreeObject(xjson_obj_t *pObj)`

- Frees one node recursively, including child map/array structures.

### Add / mutate helpers

#### `xjson_error_t XJSON_AddObject(xjson_obj_t *pDst, xjson_obj_t *pSrc)`

- For object destinations:
  - inserts by `pSrc->pName`.
  - replaces existing child only when `nAllowUpdate` is true.
- For array destinations:
  - appends child pointer without duplicating it.
- Returns:
  - `XJSON_ERR_NONE` on success.
  - `XJSON_ERR_EXITS`, `XJSON_ERR_ALLOC` or `XJSON_ERR_INVALID` on failure.

#### `xjson_error_t XJSON_AddU64 / AddU32 / AddU16 / AddInt / AddFloat / AddString / AddBool / AddNull(...)`

- Build a primitive child and insert it into `pObject`.
- Return `XJSON_ERR_NONE` or a specific error.

#### `xjson_error_t XJSON_AddStrIfUsed(xjson_obj_t *pObject, const char *pName, const char *pValue)`

- No-op success when `pValue` is null/empty.
- Otherwise behaves like `AddString`.

### Parser lifecycle

#### `void XJSON_Init(xjson_t *pJson)`

- Zeroes parser state and last-token info.

#### `int XJSON_Parse(xjson_t *pJson, xpool_t *pPool, const char *pData, size_t nSize)`

- Parses input whose first token must be `{` or `[`.
- Stores root object in `pJson->pRootObj`.
- Returns `XJSON_SUCCESS` (`1`) or `XJSON_FAILURE` (`0`).

#### `void XJSON_Destroy(xjson_t *pJson)`

- Frees root object and clears parser state.

#### `size_t XJSON_GetErrorStr(xjson_t *pJson, char *pOutput, size_t nSize)`

- Formats the current parser error and byte offset into `pOutput`.
- Returns written length.

### Object and array access

#### `xarray_t *XJSON_GetObjects(xjson_obj_t *pObj)`

- Collects object members into an array of `xmap_pair_t` payload pointers.
- Returns array or `NULL`.

#### `xjson_obj_t *XJSON_GetObject(xjson_obj_t *pObj, const char *pName)`

- Returns named child from an object or `NULL`.

#### `xjson_obj_t *XJSON_GetArrayItem(xjson_obj_t *pObj, size_t nIndex)`

- Returns array child at index or `NULL`.

#### `size_t XJSON_GetArrayLength(xjson_obj_t *pObj)`

- Returns array length or `0`.

#### `int XJSON_RemoveArrayItem(xjson_obj_t *pObj, size_t nIndex)`

- Removes array item and frees that child object.
- Always returns `0`.

#### `xjson_obj_t *XJSON_GetOrCreateObject(xjson_obj_t *pObj, const char *pName, uint8_t nAllowUpdate)`

#### `xjson_obj_t *XJSON_GetOrCreateArray(xjson_obj_t *pObj, const char *pName, uint8_t nAllowUpdate)`

- Return existing named child when found.
- Otherwise create and insert a new object/array child.
- Return child pointer or `NULL`.

### Value extraction

#### `const char *XJSON_GetString(xjson_obj_t *pObj)`

- Returns string payload.
- Returns `""` on type mismatch.

#### `int XJSON_GetInt(xjson_obj_t *pObj)`

#### `uint16_t XJSON_GetU16(xjson_obj_t *pObj)`

#### `uint32_t XJSON_GetU32(xjson_obj_t *pObj)`

#### `uint64_t XJSON_GetU64(xjson_obj_t *pObj)`

- Parse numeric text from a number node.
- Return `0` on type mismatch.

#### `double XJSON_GetFloat(xjson_obj_t *pObj)`

- Returns floating-point value or `0.0` on type mismatch.

#### `uint8_t XJSON_GetBool(xjson_obj_t *pObj)`

- Returns `1` when boolean text is `"true"`, else `0`.

### Writer / formatter

#### `void XJSON_FormatInit(xjson_format_t *pFormat)`

- Fills default ANSI/color formatting fields.

#### `int XJSON_InitWriter(xjson_writer_t *pWriter, xpool_t *pPool, char *pOutput, size_t nSize)`

- Initializes writer.
- When `pOutput == NULL` and `nSize > 0`, allocates output storage.
- Returns `1` on success, `0` on allocation failure.

#### `void XJSON_DestroyWriter(xjson_writer_t *pWriter)`

- Frees writer-owned output storage when allocated by `InitWriter`.

#### `int XJSON_WriteObject(xjson_obj_t *pObj, xjson_writer_t *pWriter)`

- Serializes one object/array/primitive into writer storage.
- Returns `XJSON_SUCCESS` or `XJSON_FAILURE`.

#### `int XJSON_Write(xjson_t *pJson, char *pOutput, size_t nSize)`

- Serializes `pJson->pRootObj` into caller buffer.
- Returns `XJSON_SUCCESS` or `XJSON_FAILURE`.

#### `char *XJSON_FormatObj / XJSON_Format(...)`

- Pretty-print object/root JSON with optional color formatting.
- Return allocated string or `NULL`.
- Store output length in optional `pLength`.

#### `char *XJSON_DumpObj / XJSON_Dump(...)`

- Compact-print object/root JSON.
- Return allocated string or `NULL`.

## Important Notes

- Objects use `xmap_t`; arrays use `xarray_t`.
- Strings are stored as copied text without a full JSON string-normalization layer.
- Writer return strings may be pool-backed when the source object/parser uses a pool.
