# md5.c

## Purpose

One-shot MD5 helpers.

## API Reference

### `XSTATUS XMD5_Compute(uint8_t *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength)`

- Arguments:
  - `pOutput`: destination digest buffer.
  - `nSize`: destination size, must be at least `16`.
  - `pInput`: input bytes.
  - `nLength`: input length.
- Does:
  - computes raw 16-byte MD5 digest.
- Returns:
  - `XSTDOK` on success.
  - `XSTDINV` on invalid args such as zero length or too-small output.
  - `XSTDERR` on allocation/internal failure.

### `uint8_t *XMD5_Encrypt(const uint8_t *pInput, size_t nLength)`

- Arguments:
  - `pInput`: input bytes.
  - `nLength`: input length.
- Does:
  - computes MD5 and returns raw digest bytes.
- Returns:
  - allocated `16 + 1` buffer or `NULL`.

### `char *XMD5_Sum(const uint8_t *pInput, size_t nLength)`

- Arguments:
  - input bytes and length.
- Does:
  - computes MD5 and renders lowercase 32-character hex.
- Returns:
  - allocated string or `NULL`.
