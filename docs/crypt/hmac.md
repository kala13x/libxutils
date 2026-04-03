# hmac.c

## Purpose

HMAC helpers for SHA-256 and MD5.

## API Reference

### `XSTATUS XHMAC_SHA256(uint8_t *pOutput, size_t nSize, const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen)`

- Arguments:
  - `pOutput`: destination digest buffer.
  - `nSize`: destination size, must be at least `32`.
  - `pData`/`nLength`: message bytes and length.
  - `pKey`/`nKeyLen`: key bytes and length.
- Does:
  - computes raw 32-byte HMAC-SHA256.
- Returns:
  - `XSTDOK` on success.
  - `XSTDINV`/`XSTDERR` on invalid args or internal failure.

### `XSTATUS XHMAC_SHA256_HEX(char *pOutput, size_t nSize, const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen)`

- Arguments:
  - same data/key inputs as raw SHA256 HMAC.
  - `pOutput`: hex destination buffer.
  - `nSize`: must fit lowercase hex digest plus terminator.
- Does:
  - computes HMAC-SHA256 and renders lowercase hex.
- Returns:
  - `XSTDOK` or `XSTDERR`.

### `char *XHMAC_SHA256_B64(const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen, size_t *pOutLen)`

- Arguments:
  - message and key inputs.
  - `pOutLen`: optional output length pointer.
- Does:
  - computes raw SHA256 HMAC and Base64Url-encodes it.
- Returns:
  - allocated encoded string or `NULL`.

### `char *XHMAC_SHA256_NEW(const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen)`

- Arguments:
  - message and key inputs.
- Does:
  - computes SHA256 HMAC and allocates lowercase hex text.
- Returns:
  - allocated string or `NULL`.

### `XSTATUS XHMAC_MD5(char *pOutput, size_t nSize, const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen)`

- Arguments:
  - message and key inputs.
  - `pOutput`: destination buffer for lowercase MD5 hex.
  - `nSize`: output buffer length.
- Does:
  - computes HMAC-MD5 and writes lowercase hex text.
- Returns:
  - `XSTDOK` or `XSTDERR`.

### `char *XHMAC_MD5_NEW(const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen)`

- Arguments:
  - message and key inputs.
- Does:
  - computes HMAC-MD5 and allocates lowercase hex text.
- Returns:
  - allocated string or `NULL`.

## Caveat

- Keys longer than the HMAC block size are not processed strictly per standard HMAC because the long-key branch hashes the key but then still builds ipad/opad from the original key bytes.
