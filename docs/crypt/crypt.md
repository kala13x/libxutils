# crypt.c

## Purpose

Generic transform layer over codecs, hashes and symmetric/asymmetric crypto.

## Direct Helper APIs

### `uint8_t *XCrypt_AES(const uint8_t *pInput, size_t *pLength, const uint8_t *pKey, size_t nKeyLen, const uint8_t *pIV)`

### `uint8_t *XDecrypt_AES(const uint8_t *pInput, size_t *pLength, const uint8_t *pKey, size_t nKeyLen, const uint8_t *pIV)`

- Arguments:
  - `pInput`: input bytes.
  - `pLength`: in/out length.
  - `pKey`: AES key.
  - `nKeyLen`: key length in bytes.
  - `pIV`: IV bytes, may be `NULL`.
- Does:
  - initializes AES-CBC helper with embedded-IV behavior and encrypts/decrypts one payload.
- Returns:
  - allocated output buffer or `NULL`.

### `uint8_t *XCrypt_HEX(const uint8_t *pInput, size_t *pLength, const char *pSpace, size_t nColumns, xbool_t bLowCase)`

- Arguments:
  - `pInput`: input bytes.
  - `pLength`: in/out length.
  - `pSpace`: optional separator inserted between byte pairs.
  - `nColumns`: optional wrap width in bytes.
  - `bLowCase`: lower- or upper-case hex digits.
- Does:
  - renders input bytes as printable hex text.
- Returns:
  - allocated text buffer or `NULL`.

### `uint8_t *XDecrypt_HEX(const uint8_t *pInput, size_t *pLength, xbool_t bLowCase)`

- Arguments:
  - `pInput`: hex text.
  - `pLength`: in/out length.
  - `bLowCase`: accepted case preference.
- Does:
  - parses hex pairs until parsing fails.
- Returns:
  - allocated decoded bytes or `NULL`.

### `uint8_t *XCrypt_XOR(const uint8_t *pInput, size_t nLength, const uint8_t *pKey, size_t nKeyLen)`

- Arguments:
  - input bytes and explicit key bytes/length.
- Does:
  - XORs each input byte with a repeating key sequence.
- Returns:
  - allocated output buffer of `nLength + 1` or `NULL`.

### `char *XCrypt_Reverse(const char *pInput, size_t nLength)`

- Arguments:
  - `pInput`: source text.
  - `nLength`: byte count to reverse.
- Does:
  - creates a reversed copy.
- Returns:
  - allocated string or `NULL`.

### `char *XCrypt_Casear(const char *pInput, size_t nLength, size_t nKey)`

### `char *XDecrypt_Casear(const char *pInput, size_t nLength, size_t nKey)`

- Arguments:
  - `pInput`: source text.
  - `nLength`: text length.
  - `nKey`: rotation amount.
- Does:
  - rotates alphabetic ASCII only.
  - non-alphabetic bytes are copied unchanged.
- Returns:
  - allocated string or `NULL`.

## Cipher Identification APIs

### `xcrypt_chipher_t XCrypt_GetCipher(const char *pCipher)`

- Arguments:
  - `pCipher`: textual cipher name.
- Does:
  - maps text such as `aes`, `hex`, `base64`, `sha256`, `rs256` into enum values.
- Returns:
  - matching enum or `XC_INVALID`.

### `const char *XCrypt_GetCipherStr(xcrypt_chipher_t eCipher)`

- Arguments:
  - `eCipher`: cipher enum.
- Does:
  - returns the library’s textual name for that enum.
- Returns:
  - static string or `NULL`.
- Caveat:
  - `XC_HS256` maps to `"h256"` in current code, not the more expected `"hs256"`.

## Context / Chain APIs

### `void XCrypt_Init(xcrypt_ctx_t *pCtx, xbool_t bDecrypt, char *pCiphers, xcrypt_cb_t callback, void *pUser)`

- Arguments:
  - `pCtx`: destination chain context.
  - `bDecrypt`: whether caller intends decrypt path.
  - `pCiphers`: colon-separated cipher chain string.
  - `callback`: key/IV callback.
  - `pUser`: user context passed to callback.
- Does:
  - stores configuration into the context.
- Returns:
  - no return value.

### `uint8_t *XCrypt_Single(xcrypt_ctx_t *pCtx, xcrypt_chipher_t eCipher, const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - one configured context, one cipher enum, input bytes and in/out length.
- Does:
  - applies exactly one stage.
  - fetches key/IV via callback when the stage requires them.
  - for hashes and CRC, rewrites `*pLength` to digest/text length.
- Returns:
  - allocated output buffer or `NULL`.

### `uint8_t *XDecrypt_Single(xcrypt_ctx_t *pCtx, xcrypt_chipher_t eCipher, const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - same shape as `XCrypt_Single`.
- Does:
  - runs one reversible stage in decrypt mode.
- Returns:
  - allocated output buffer or `NULL`.
- Caveat:
  - hash-style ciphers are not reversible and are not supported here.

### `uint8_t *XCrypt_Multy(xcrypt_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - `pCtx`: initialized context with `pCiphers`.
  - `pInput`: source bytes.
  - `pLength`: in/out length.
- Does:
  - tokenizes `pCiphers` on `:`.
  - pipes the output of each stage into the next stage.
  - frees intermediate stage outputs along the way.
- Returns:
  - final allocated output buffer or `NULL`.

## Notes

- Generic hash stages may return printable text rather than raw binary.
- Generic CRC32 output is decimal text, not raw 32-bit binary.
