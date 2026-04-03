# rtp.c

## Purpose

Minimal RTP header/timestamp helpers.

## API Reference

### `uint32_t XRTP_GetTimestamp(float fRate)`

- Args:
  - frame rate.
- Does:
  - increments a static RTP timestamp by `90000 / fRate`.
- Returns:
  - next timestamp value.

### `int XRTP_ParseHeader(xrtp_header_t *pHeader, const uint8_t *pData, size_t nLength)`

- Args:
  - destination RTP header, raw packet bytes and total length.
- Does:
  - validates version 2 and parses header fields.
- Returns:
  - payload offset on success.
  - `XSTDERR` on invalid packet.

### `int XRTP_ParsePacket(xrtp_packet_t *pPacket, uint8_t *pData, size_t nLength)`

- Parses RTP header plus the project’s additional 4-byte payload header.
- Returns parsed payload length or `XSTDERR`.

### `uint8_t *XRTP_AssemblePacket(xrtp_header_t *pHeader, const uint8_t *pData, size_t nLength)`

- Builds one RTP packet into a newly allocated buffer.
- Returns buffer or `NULL`.
