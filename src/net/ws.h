/*!
 *  @file libxutils/src/net/ws.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of Web Socket server and
 * client functionality based on xevents and xsock.
 */

#ifndef __XUTILS_XWS_H__
#define __XUTILS_XWS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"
#include "xbuf.h"

#define XWS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef enum {
    XWS_ERR_NONE,
    XWS_ERR_ALLOC,
    XWS_ERR_SIZE,
    XWS_INVALID_ARGS,
    XWS_INVALID_TYPE,
    XWS_INVALID_REQUEST,
    XWS_MISSING_SEC_KEY,
    XWS_MISSING_PAYLOAD,
    XWS_PARSED_SEC_KEY,
    XWS_FRAME_TOOBIG,
    XWS_FRAME_PARSED,
    XWS_FRAME_INVALID,
    XWS_FRAME_COMPLETE,
    XWS_FRAME_INCOMPLETE
} xws_status_t;

typedef enum {
    XWS_CONTINUATION,
    XWS_TEXT,
    XWS_BINARY,
    XWS_RESERVED1,
    XWS_RESERVED2,
    XWS_RESERVED3,
    XWS_RESERVED4,
    XWS_RESERVED5,
    XWS_CLOSE,
    XWS_PING,
    XWS_PONG,
    XWS_RESERVED6,
    XWS_RESERVED7,
    XWS_RESERVED8,
    XWS_RESERVED9,
    XWS_RESERVED10,
    XWS_INVALID
} xws_frame_type_t;

typedef struct xweb_frame_ {
    xws_frame_type_t eType;
    xbyte_buffer_t buffer;

    size_t nPayloadLength;
    size_t nHeaderSize;

    uint32_t nMaskKey;
    uint8_t nOpCode;

    xbool_t bComplete;
    xbool_t bAlloc;
    xbool_t bMask;
    xbool_t bFin;
} xws_frame_t;

const char* XWebSock_GetStatusStr(xws_status_t eStatus);
const char* XWS_FrameTypeStr(xws_frame_type_t eType);

xws_frame_type_t XWS_FrameType(uint8_t nOpCode);
uint8_t XWS_OpCode(xws_frame_type_t eType);

void XWebFrame_Init(xws_frame_t *pFrame);
void XWebFrame_Free(xws_frame_t **pFrame);
void XWebFrame_Clear(xws_frame_t *pFrame);
void XWebFrame_Reset(xws_frame_t *pFrame);

uint8_t* XWS_CreateFrame(uint8_t *pPayload, size_t nLength, uint8_t nOpCode, xbool_t bFin, size_t *pFrameSize);
xws_frame_t* XWebFrame_New(uint8_t *pPayload, size_t nLength, xws_frame_type_t eType, xbool_t bFin);
xws_frame_t* XWebFrame_Alloc(xws_frame_type_t eType, size_t nBuffSize);

xws_status_t XWebFrame_Create(xws_frame_t *pFrame, uint8_t *pPayload, size_t nLength, xws_frame_type_t eType, xbool_t bFin);
xws_status_t XWebFrame_AppendData(xws_frame_t *pFrame, uint8_t* pData, size_t nSize);
xws_status_t XWebFrame_ParseData(xws_frame_t *pFrame, uint8_t* pData, size_t nSize);
xws_status_t XWebFrame_TryParse(xws_frame_t *pFrame, uint8_t* pData, size_t nSize);
xws_status_t XWebFrame_ParseBuff(xws_frame_t *pFrame, xbyte_buffer_t *pBuffer);
xws_status_t XWebFrame_Parse(xws_frame_t *pFrame);
xws_status_t XWebFrame_Unmask(xws_frame_t *pFrame);

size_t XWebFrame_GetPayloadLength(xws_frame_t *pFrame);
size_t XWebFrame_GetFrameLength(xws_frame_t *pFrame);
size_t XWebFrame_GetExtraLength(xws_frame_t *pFrame);

xbyte_buffer_t* XWebFrame_GetBuffer(xws_frame_t *pFrame);
const uint8_t* XWebFrame_GetPayload(xws_frame_t *pFrame);

XSTATUS XWebFrame_GetExtraData(xws_frame_t *pFrame, xbyte_buffer_t *pBuffer, xbool_t bAppend);
XSTATUS XWebFrame_CutExtraData(xws_frame_t *pFrame);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XWS_H__ */