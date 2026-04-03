# base64.c

## Purpose

Standard and URL-safe Base64 encode/decode helpers.

## API Reference

### `char *XBase64_Encrypt(const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - `pInput`: source bytes.
  - `pLength`: in/out length pointer; input size on entry, encoded length on success.
- Does:
  - encodes input with standard Base64 alphabet and `=` padding.
- Returns:
  - newly allocated NUL-terminated string on success.
  - `NULL` when `pInput == NULL`, `pLength == NULL`, `*pLength == 0` or allocation fails.

### `char *XBase64_Decrypt(const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - `pInput`: Base64 text.
  - `pLength`: in/out length pointer; encoded size on entry, decoded byte count on success.
- Does:
  - accepts both properly padded input and missing-padding input.
  - internally rounds the length to a multiple of 4 when necessary.
- Returns:
  - newly allocated decoded buffer on success.
  - `NULL` on invalid args or allocation failure.

### `char *XBase64_UrlEncrypt(const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - same shape as `XBase64_Encrypt`.
- Does:
  - uses URL-safe alphabet `-` and `_`.
  - strips trailing `=` padding from the textual result.
- Returns:
  - newly allocated encoded string or `NULL`.

### `char *XBase64_UrlDecrypt(const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - same shape as `XBase64_Decrypt`.
- Does:
  - converts URL-safe alphabet back to standard Base64.
  - then decodes through the standard decoder.
- Returns:
  - newly allocated decoded buffer or `NULL`.

## Notes

- Decoded buffers are convenient for text because the implementation appends a trailing NUL, but the authoritative size is always `*pLength`.
