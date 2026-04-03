# crc32.c

## Purpose

Two CRC32 helpers with different initialization/finalization rules.

## API Reference

### `uint32_t XCRC32_Compute(const uint8_t *pInput, size_t nLength)`

- Arguments:
  - `pInput`: source bytes.
  - `nLength`: byte count.
- Does:
  - computes CRC32 starting from `0`.
- Returns:
  - computed CRC value.
  - `0` when `pInput == NULL` or `nLength == 0`.

### `uint32_t XCRC32_ComputeB(const uint8_t *pInput, size_t nLength)`

- Arguments:
  - same shape as `XCRC32_Compute`.
- Does:
  - computes CRC32 starting from `0xFFFFFFFF` and returns bitwise complement at the end.
- Returns:
  - computed CRC value.
  - `0` when `pInput == NULL` or `nLength == 0`.
