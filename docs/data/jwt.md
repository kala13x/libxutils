# jwt.c

## Purpose

JWT creation, parsing and verification for HS256 and optionally RS256.

## API Reference

### Lifecycle

#### `void XJWT_Init(xjwt_t *pJWT, xjwt_alg_t eAlg)`

- Zeroes token fields and stores initial algorithm.

#### `void XJWT_Destroy(xjwt_t *pJWT)`

- Frees encoded header/payload/signature and parsed JSON objects.

### Algorithm helpers

#### `const char *XJWT_GetAlgStr(xjwt_alg_t eAlg)`

- Returns static `"HS256"` / `"RS256"` string or `NULL`.

#### `xjwt_alg_t XJWT_GetAlg(const char *pAlgStr)`

- Parses textual algorithm name.
- Returns enum or `XJWT_ALG_INVALID`.

#### `xjwt_alg_t XJWT_GetAlgorithm(xjwt_t *pJWT)`

- Returns cached algorithm if known.
- Otherwise parses the `alg` header field and caches it.

### Header / payload management

#### `XSTATUS XJWT_AddPayload(xjwt_t *pJWT, const char *pPayload, size_t nPayloadLen, xbool_t bIsEncoded)`

#### `XSTATUS XJWT_AddHeader(xjwt_t *pJWT, const char *pHeader, size_t nHeaderLen, xbool_t bIsEncoded)`

- Accept raw JSON when `bIsEncoded == XFALSE` and Base64Url-encode it.
- Accept already encoded JWT segment text when `bIsEncoded == XTRUE`.
- Return `XSTDOK`, `XSTDINV` or `XSTDERR`.

#### `char *XJWT_GetPayload(xjwt_t *pJWT, xbool_t bDecode, size_t *pPayloadLen)`

#### `char *XJWT_GetHeader(xjwt_t *pJWT, xbool_t bDecode, size_t *pHeaderLen)`

- Materialize cached segment text when missing.
- When `bDecode` is true, allocate and return decoded JSON text.
- Otherwise return internal encoded segment pointer.

#### `xjson_obj_t *XJWT_GetPayloadObj(xjwt_t *pJWT)`

#### `xjson_obj_t *XJWT_GetHeaderObj(xjwt_t *pJWT)`

- Lazily parse decoded JSON into `xjson_obj_t`.
- Return parsed object or `NULL`.

#### `xjson_obj_t *XJWT_CreateHeaderObj(xjwt_alg_t eAlg)`

- Builds a minimal header object containing `alg` and `typ=JWT`.
- Returns new object or `NULL`.

### Token build / verify

#### `char *XJWT_Create(xjwt_t *pJWT, const uint8_t *pSecret, size_t nSecretLen, size_t *pJWTLen)`

- Builds `header.payload.signature`.
- Returns allocated JWT string or `NULL`.

#### `XSTATUS XJWT_Verify(xjwt_t *pJWT, const char *pSignature, size_t nSignatureLen, const uint8_t *pSecret, size_t nSecretLen)`

- Dispatches to HS256 or RS256 verification.
- Returns `XSTDOK` on valid signature, `XSTDNON` on mismatch, or negative status on invalid args/failure.

#### `XSTATUS XJWT_Parse(xjwt_t *pJWT, const char *pJWTStr, size_t nLength, const uint8_t *pSecret, size_t nSecretLen)`

- Splits token into first three `.`-separated segments.
- Populates header/payload fields.
- Verifies immediately when secret/key is supplied.
- Returns `XSTDOK`, `XSTDNON` or `XSTDERR`.

## Important Runtime Semantics

- HS256 signing is `Base64Url(HMAC_SHA256(raw(header.payload)))`.
- RS256 delegates to the project’s manual `XCrypt_RS256` path.
- `GetHeader`/`GetPayload` may return internal pointers when `bDecode == XFALSE`; caller must not free those.
