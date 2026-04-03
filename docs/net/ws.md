# ws.c

## Purpose

WebSocket frame creation and parsing.

## API Reference

### Enum helpers

#### `const char *XWebSock_GetStatusStr(xws_status_t eStatus)`

#### `const char *XWS_FrameTypeStr(xws_frame_type_t eType)`

#### `xws_frame_type_t XWS_FrameType(uint8_t nOpCode)`

#### `uint8_t XWS_OpCode(xws_frame_type_t eType)`

- Convert between status/type enums and frame opcodes/text.

### Lifecycle

#### `void XWebFrame_Init(xws_frame_t *pFrame)`

#### `void XWebFrame_Clear(xws_frame_t *pFrame)`

#### `void XWebFrame_Reset(xws_frame_t *pFrame)`

#### `void XWebFrame_Free(xws_frame_t **pFrame)`

- Init, clear owned data, reset parse state, or free allocated frame.

### Creation

#### `uint8_t *XWS_CreateFrame(const uint8_t *pPayload, size_t nLength, uint8_t nOpCode, xbool_t bFin, size_t *pFrameSize)`

- Returns allocated raw frame bytes or `NULL`.

#### `xws_frame_t *XWebFrame_New(const uint8_t *pPayload, size_t nLength, xws_frame_type_t eType, xbool_t bMask, xbool_t bFin)`

#### `xws_frame_t *XWebFrame_Alloc(xws_frame_type_t eType, size_t nBuffSize)`

- Allocate frame object from payload or empty buffer.
- Return frame or `NULL`.

#### `xws_status_t XWebFrame_Create(xws_frame_t *pFrame, const uint8_t *pPayload, size_t nLength, xws_frame_type_t eType, xbool_t bMask, xbool_t bFin)`

- Fills frame buffer and metadata.
- Returns frame status.

### Parse / mask

#### `xws_status_t XWebFrame_AppendData(xws_frame_t *pFrame, uint8_t *pData, size_t nSize)`

#### `xws_status_t XWebFrame_ParseData(xws_frame_t *pFrame, uint8_t *pData, size_t nSize)`

#### `xws_status_t XWebFrame_TryParse(xws_frame_t *pFrame, uint8_t *pData, size_t nSize)`

#### `xws_status_t XWebFrame_ParseBuff(xws_frame_t *pFrame, xbyte_buffer_t *pBuffer)`

#### `xws_status_t XWebFrame_Parse(xws_frame_t *pFrame)`

- Append/attach bytes and parse header/payload state.
- Return `XWS_FRAME_COMPLETE`, `XWS_FRAME_INCOMPLETE` or error status.

#### `xws_status_t XWebFrame_Mask(xws_frame_t *pFrame)`

#### `xws_status_t XWebFrame_Unmask(xws_frame_t *pFrame)`

- Apply or remove mask in place.
- Return frame status.

### Access helpers

#### `size_t XWebFrame_GetPayloadLength / XWebFrame_GetFrameLength / XWebFrame_GetExtraLength(xws_frame_t *pFrame)`

- Return payload size, full frame size or trailing extra-data size.

#### `xbyte_buffer_t *XWebFrame_GetBuffer(xws_frame_t *pFrame)`

#### `const uint8_t *XWebFrame_GetPayload(xws_frame_t *pFrame)`

- Return internal frame buffer or payload pointer.

#### `XSTATUS XWebFrame_GetExtraData(xws_frame_t *pFrame, xbyte_buffer_t *pBuffer, xbool_t bAppend)`

#### `XSTATUS XWebFrame_CutExtraData(xws_frame_t *pFrame)`

- Copy or remove bytes following the first complete frame.
- Return status code.
