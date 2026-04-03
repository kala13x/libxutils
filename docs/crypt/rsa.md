# rsa.c

## Purpose

OpenSSL-backed RSA utilities. Present only when `_XUTILS_USE_SSL` is enabled.

## API Reference

### `int XRSA_HaveSSL(void)`

- Does:
  - reports whether RSA/SSL support is built in.
- Returns:
  - non-zero when SSL-backed RSA is available.
  - `0` otherwise.

## Context Lifecycle

### `void XRSA_Init(xrsa_ctx_t *pCtx)`

- Arguments:
  - `pCtx`: destination RSA context.
- Does:
  - zeroes the context and sets default padding to `RSA_PKCS1_PADDING`.
- Returns:
  - no return value.

### `void XRSA_Destroy(xrsa_ctx_t *pCtx)`

- Arguments:
  - `pCtx`: context to clean.
- Does:
  - frees loaded/generated keys, stored PEM strings and last error string.
- Returns:
  - no return value.

### `char *XSSL_LastErrors(size_t *pOutLen)`

- Arguments:
  - `pOutLen`: optional output length pointer.
- Does:
  - collects OpenSSL error text into a newly allocated string.
- Returns:
  - allocated string or `NULL`.

## Key Loading / Generation

### `XSTATUS XRSA_GenerateKeys(xrsa_ctx_t *pCtx, size_t nKeyLength, size_t nPubKeyExp)`

- Arguments:
  - `pCtx`: destination context.
  - `nKeyLength`: RSA modulus size in bits.
  - `nPubKeyExp`: public exponent.
- Does:
  - generates a keypair and stores PEM private/public strings and parsed keys in the context.
- Returns:
  - `XSTDOK` on success, `XSTDERR` on failure.

### `XSTATUS XRSA_LoadKeyFiles(xrsa_ctx_t *pCtx, const char *pPrivPath, const char *pPubPath)`

### `XSTATUS XRSA_LoadPrivKeyFile(xrsa_ctx_t *pCtx, const char *pPath)`

### `XSTATUS XRSA_LoadPubKeyFile(xrsa_ctx_t *pCtx, const char *pPath)`

- Arguments:
  - context plus PEM file path(s).
- Does:
  - reads PEM text via `XPath_Load()` and loads parsed key objects.
- Returns:
  - `XSTDOK` or `XSTDERR`.

### `XSTATUS XRSA_LoadPrivKey(xrsa_ctx_t *pCtx)`

### `XSTATUS XRSA_LoadPubKey(xrsa_ctx_t *pCtx)`

- Arguments:
  - context whose PEM text fields are already filled.
- Does:
  - parses PEM text already stored in the context.
- Returns:
  - `XSTDOK` or `XSTDERR`.

### `XSTATUS XRSA_SetPubKey(xrsa_ctx_t *pCtx, const char *pPubKey, size_t nLength)`

### `XSTATUS XRSA_SetPrivKey(xrsa_ctx_t *pCtx, const char *pPrivKey, size_t nLength)`

- Arguments:
  - context, PEM text pointer and PEM length.
- Does:
  - copies PEM text into context-owned memory and immediately loads it.
- Returns:
  - `XSTDOK` or `XSTDERR`.

## RSA Operations

### `uint8_t *XRSA_Crypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength)`

- Does:
  - encrypts with public key.
- Returns:
  - allocated ciphertext or `NULL`.

### `uint8_t *XRSA_Decrypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength)`

- Does:
  - decrypts with private key.
- Returns:
  - allocated plaintext or `NULL`.

### `uint8_t *XRSA_PrivCrypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength)`

- Does:
  - private-key RSA operation used for signature-style flows.
- Returns:
  - allocated output or `NULL`.

### `uint8_t *XRSA_PubDecrypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength)`

- Does:
  - inverse public-key decrypt for private-key-encrypted data.
- Returns:
  - allocated output or `NULL`.

## One-Shot Wrappers

### `uint8_t *XCrypt_RSA(...)`

### `uint8_t *XCrypt_PrivRSA(...)`

### `uint8_t *XDecrypt_RSA(...)`

### `uint8_t *XDecrypt_PubRSA(...)`

- Arguments:
  - input bytes, input length, PEM text pointer, PEM length, output length pointer.
- Does:
  - creates a temporary RSA context and performs one encrypt/decrypt operation.
- Returns:
  - allocated output or `NULL`.

## RS256 Helpers

### `uint8_t *XCrypt_RS256(const uint8_t *pInput, size_t nLength, const char *pPrivKey, size_t nKeyLen, size_t *pOutLen)`

- Does:
  - computes SHA-256 of input.
  - prepends ASN.1 DigestInfo padding.
  - applies private-key RSA operation.
- Returns:
  - allocated signature bytes or `NULL`.

### `XSTATUS XCrypt_VerifyRS256(const uint8_t *pSignature, size_t nSignatureLen, const uint8_t *pData, size_t nLength, const char *pPubKey, size_t nKeyLen)`

- Does:
  - decrypts signature with public key.
  - rebuilds local padded SHA-256 digest.
  - byte-compares the two.
- Returns:
  - `XSTDOK` on match.
  - `XSTDNON`/`XSTDERR` on mismatch or failure.
