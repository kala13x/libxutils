# type.c

## Purpose

Small conversion helpers for float bits, printable checks and human-readable size formatting.

## API Reference

#### `uint32_t XFloatToU32(float fValue)`

- Arguments:
  - floating-point value.
- Does:
  - reinterprets the float bits through a shared global union.
- Returns:
  - raw `uint32_t` bit pattern.

#### `float XU32ToFloat(uint32_t nValue)`

- Arguments:
  - raw 32-bit bit pattern.
- Does:
  - reinterprets the bits as `float`.
- Returns:
  - floating-point value.

#### `xbool_t XTypeIsPrint(const uint8_t *pData, size_t nSize)`

- Arguments:
  - byte buffer and max length.
- Does:
  - checks bytes until either `nSize` is exhausted or the first NUL byte is seen.
  - fails on the first non-printable byte.
- Returns:
  - `XTRUE` when all examined bytes are printable.
  - `XFALSE` otherwise.

#### `size_t XBytesToUnit(char *pDst, size_t nSize, size_t nBytes, xbool_t bShort)`

- Arguments:
  - destination buffer, size, byte count and short-format flag.
- Does:
  - formats bytes as `B`, `KB`, `MB` or `GB`.
  - uses one decimal in short mode, two decimals in long mode, except GB which is formatted without decimals.
- Returns:
  - written length.

#### `size_t XKBToUnit(char *pDst, size_t nSize, size_t nKB, xbool_t bShort)`

- Arguments:
  - destination buffer, size, kilobyte count and short-format flag.
- Does:
  - formats values as `K`, `M`, `G` or `T` / `KB`, `MB`, `GB`, `TB`.
- Returns:
  - written length.

## Important Notes

- `XFloatToU32()` / `XU32ToFloat()` use one shared global union, so they are not independently thread-safe storage primitives.
- These helpers are for display and quick conversions, not stable serialization formats.
