# mdtp.c

## Purpose

MDTP (Modern Data Transmit Protocol) packet framing and parsing.

## API Reference

### Enum/string helpers

#### `xpacket_type_t XPacket_GetType(const char *pType)`

#### `const char *XPacket_GetTypeStr(xpacket_type_t eType)`

#### `const char *XPacket_GetStatusStr(xpacket_status_t eStatus)`

- Convert between enum and static text.

### Lifecycle

#### `xpacket_status_t XPacket_Init(xpacket_t *pPacket, uint8_t *pData, uint32_t nSize)`

- Initializes packet state and optionally parses raw data.
- Returns packet status.

#### `xpacket_t *XPacket_New(uint8_t *pData, uint32_t nSize)`

- Allocates packet and initializes it.
- Returns packet or `NULL`.

#### `void XPacket_Clear(xpacket_t *pPacket)`

- Runs optional callback with `XPACKET_CB_CLEAR`, frees header object and clears raw storage.

#### `void XPacket_Free(xpacket_t **pPacket)`

- Clears and frees packet, then nulls caller pointer.

### Parse / assemble

#### `xpacket_status_t XPacket_Parse(xpacket_t *pPacket, const uint8_t *pData, size_t nSize)`

- Parses `4-byte header length + JSON header + payload`.
- Returns:
  - `XPACKET_INCOMPLETE` when even header is incomplete.
  - `XPACKET_PARSED` when header is complete but payload is incomplete.
  - `XPACKET_COMPLETE` when full packet is available.
  - error status otherwise.

#### `xpacket_status_t XPacket_UpdateHeader(xpacket_t *pPacket)`

- Rebuilds JSON header from `pPacket->header`.
- Returns packet status.

#### `void XPacket_ParseHeader(xpacket_header_t *pHeader, xjson_obj_t *pHeaderObj)`

- Extracts known fields from parsed header JSON.

#### `xpacket_status_t XPacket_Create(xbyte_buffer_t *pBuffer, const char *pHeader, size_t nHdrLen, uint8_t *pData, size_t nSize)`

- Appends one serialized packet into caller byte buffer.
- Returns packet status.

#### `xbyte_buffer_t *XPacket_Assemble(xpacket_t *pPacket)`

- Serializes packet header object plus payload into `rawData`.
- Returns pointer to `rawData` or `NULL`.

### Access helpers

#### `const uint8_t *XPacket_GetHeader(xpacket_t *pPacket)`

#### `const uint8_t *XPacket_GetPayload(xpacket_t *pPacket)`

#### `size_t XPacket_GetSize(xpacket_t *pPacket)`

- Return header bytes, payload pointer or full packet size.
- Return `NULL`/`0` when missing.
