# sha1.c

## Purpose

SHA-1 stateful and one-shot helpers.

## API Reference

### `void XSHA1_Init(xsha1_ctx_t *pCtx)`

- Arguments:
  - `pCtx`: destination SHA-1 context.
- Does:
  - initializes state words, counters and internal buffer.
- Returns:
  - no return value.

### `void XSHA1_Update(xsha1_ctx_t *pCtx, const uint8_t *pData, uint32_t nLength)`

- Arguments:
  - context plus input bytes and length.
- Does:
  - appends bytes into the running SHA-1 state.
  - ignores null or zero-length input.
- Returns:
  - no return value.

### `void XSHA1_Final(xsha1_ctx_t *pCtx, uint8_t uDigest[XSHA1_DIGEST_SIZE])`

- Arguments:
  - context and 20-byte destination digest buffer.
- Does:
  - finalizes the running hash and writes raw digest.
- Returns:
  - no return value.

### `void XSHA1_Transform(uint32_t uState[5], const uint8_t uBuffer[64])`

- Arguments:
  - raw state words and one 64-byte block.
- Does:
  - processes one SHA-1 compression block.
- Returns:
  - no return value.

### `XSTATUS XSHA1_Compute(uint8_t *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength)`

- Does:
  - one-shot SHA-1 raw digest.
- Returns:
  - `XSTDOK` on success.
  - failure status when output buffer is invalid.

### `XSTATUS XSHA1_ComputeSum(char *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength)`

- Does:
  - one-shot lowercase hex SHA-1.
- Returns:
  - `XSTDOK` or failure status.

### `char *XSHA1_Sum(const uint8_t *pInput, size_t nLength)`

- Returns:
  - allocated lowercase hex string or `NULL`.

### `uint8_t *XSHA1_Encrypt(const uint8_t *pInput, size_t nLength)`

- Returns:
  - allocated raw `20 + 1` digest buffer or `NULL`.
