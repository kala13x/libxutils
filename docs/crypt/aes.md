# aes.c

## Purpose

AES helper layer with three modes:

- CBC with PKCS#7 padding
- XBC, a custom CBC-based framing mode with random prefix
- SIV, deterministic authenticated encryption using CMAC/S2V + CTR

## Types

- `xaes_key_t`: raw key/IV container. `nKeySize` is stored in bits.
- `xaes_ctx_t`: expanded round-key context.
- `xaes_t`: full runtime object containing mode, key and expanded context.

## API Reference

### `void XAES_InitSIVKey(xaes_key_t *pKey, const uint8_t *pMacKey, const uint8_t *pCtrKey, size_t nKeySize)`

- Arguments:
  - `pKey`: destination key struct.
  - `pMacKey`: CMAC/S2V key bytes.
  - `pCtrKey`: CTR encryption key bytes.
  - `nKeySize`: key size in bits, expected `128`, `192` or `256`.
- Does:
  - stores separate SIV MAC and CTR keys into `pKey`.
  - zeroes IV state.
- Returns:
  - no return value.

### `void XAES_InitKey(xaes_key_t *pKey, const uint8_t *pAESKey, size_t nKeySize, const uint8_t *pIV, uint8_t bContainIV)`

- Arguments:
  - `pKey`: destination key struct.
  - `pAESKey`: AES key bytes.
  - `nKeySize`: key size in bits.
  - `pIV`: optional IV pointer.
  - `bContainIV`: whether ciphertext should embed IV.
- Does:
  - copies key bytes and records key size.
  - copies `pIV` only if `pIV` exists and its first byte is non-zero.
  - otherwise generates a random IV when `bContainIV` is true, or uses all-zero IV when false.
- Returns:
  - no return value.

### `int XAES_Init(xaes_t *pAES, const xaes_key_t *pKey, xaes_mode_t eMode)`

- Arguments:
  - `pAES`: destination AES runtime object.
  - `pKey`: previously prepared key struct.
  - `eMode`: `XAES_MODE_CBC`, `XAES_MODE_XBC` or `XAES_MODE_SIV`.
- Does:
  - validates key size.
  - expands encryption round keys.
  - for SIV, expands both CMAC and CTR contexts.
- Returns:
  - `XSTDOK` on success.
  - `XSTDINV`/`XSTDERR` style negative status on invalid key size or init failure.

### `void XAES_ECB_Crypt(const xaes_t *pAES, uint8_t *pBuffer)`

### `void XAES_ECB_Decrypt(const xaes_t *pAES, uint8_t *pBuffer)`

- Arguments:
  - `pAES`: initialized AES context.
  - `pBuffer`: one 16-byte block to encrypt/decrypt in place.
- Does:
  - transforms exactly one AES block in ECB style.
- Returns:
  - no return value.

### `uint8_t *XAES_CBC_Crypt(xaes_t *pAES, const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - `pAES`: initialized CBC/XBC runtime object.
  - `pInput`: plaintext bytes.
  - `pLength`: in/out length pointer; input plaintext size, output ciphertext size.
- Does:
  - applies PKCS#7 padding.
  - encrypts in CBC mode.
  - prepends IV when `nContainIV` is set.
  - mutates stored IV to last ciphertext block when IV is not embedded.
- Returns:
  - newly allocated ciphertext buffer on success.
  - `NULL` on invalid args or allocation failure.

### `uint8_t *XAES_CBC_Decrypt(xaes_t *pAES, const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - same shape as `XAES_CBC_Crypt`.
- Does:
  - extracts embedded IV when present.
  - validates block size and PKCS#7 padding.
  - mutates stored IV to last ciphertext block when IV is external.
- Returns:
  - newly allocated plaintext buffer on success.
  - `NULL` on invalid input length, invalid padding or allocation failure.

### `uint8_t *XAES_SIV_Crypt(xaes_t *pAES, const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - `pAES`: initialized SIV runtime object.
  - `pInput`: plaintext bytes.
  - `pLength`: input plaintext size, then output size.
- Does:
  - computes S2V/CMAC synthetic IV over plaintext.
  - prepends the 16-byte SIV.
  - encrypts plaintext with CTR using the synthetic IV.
- Returns:
  - allocated `SIV || ciphertext` buffer on success.
  - `NULL` on failure.

### `uint8_t *XAES_SIV_Decrypt(xaes_t *pAES, const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - same shape as `XAES_SIV_Crypt`.
- Does:
  - splits SIV from ciphertext.
  - decrypts with CTR.
  - recomputes SIV and verifies it in constant time.
- Returns:
  - allocated plaintext buffer on success.
  - `NULL` when input is too short, authentication fails or allocation fails.

### `uint8_t *XAES_XBC_Crypt(xaes_t *pAES, const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - same shape as CBC helpers.
- Does:
  - creates a custom framed plaintext:
    - 4-byte big-endian random-prefix length
    - random prefix bytes
    - user plaintext
  - CBC-encrypts that framed buffer.
  - optionally embeds IV.
- Returns:
  - allocated ciphertext buffer or `NULL`.

### `uint8_t *XAES_XBC_Decrypt(xaes_t *pAES, const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - same shape as `XAES_XBC_Crypt`.
- Does:
  - CBC-decrypts.
  - validates the 4-byte random-prefix header.
  - strips the random prefix and returns only original plaintext.
- Returns:
  - allocated plaintext buffer or `NULL` when framing is invalid.

### `uint8_t *XAES_Encrypt(xaes_t *pAES, const uint8_t *pInput, size_t *pLength)`

### `uint8_t *XAES_Decrypt(xaes_t *pAES, const uint8_t *pInput, size_t *pLength)`

- Arguments:
  - generic AES runtime object plus input bytes and in/out length.
- Does:
  - dispatches by `pAES->mode` to CBC, XBC or SIV implementation.
- Returns:
  - allocated output buffer on success.
  - `NULL` on invalid mode or lower-level failure.

## Output Ownership

- Every encrypt/decrypt function returning `uint8_t *` allocates a new buffer.
- The caller owns that buffer and must free it.
- Binary outputs usually include a trailing `NUL`, but callers must trust `*pLength`, not `strlen()`.
