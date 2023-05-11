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

typedef enum {
    XWS_ERR_NONE,
    XWS_ERR_ALLOC,
    XWS_ERR_SIZE,
    XWS_INVALID_ARGS,
    XWS_INVALID_TYPE,
    XWS_INVALID_REQUEST,
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
    XWS_DUMMY
} xweb_frame_type_t;

typedef struct xweb_frame_ {
    xweb_frame_type_t eType;
    xbyte_buffer_t buffer;
    size_t nPayloadLength;
    size_t nHeaderSize;
    xbool_t bComplete;
    uint8_t nOpCode;
    xbool_t bAlloc;
    xbool_t bFin;
} xweb_frame_t;

const char* XWebSock_GetStatusStr(xws_status_t eStatus);
xweb_frame_type_t XWS_FrameType(uint8_t nOpCode);
uint8_t XWS_OpCode(xweb_frame_type_t eType);

void XWebFrame_Init(xweb_frame_t *pFrame);
void XWebFrame_Free(xweb_frame_t **pFrame);
void XWebFrame_Clear(xweb_frame_t *pFrame);
void XWebFrame_Reset(xweb_frame_t *pFrame);

uint8_t* XWS_CreateFrame(uint8_t *pPayload, size_t nLength, uint8_t nOpCode, xbool_t bFin, size_t *pFrameSize);
xweb_frame_t* XWebFrame_New(uint8_t *pPayload, size_t nLength, xweb_frame_type_t eType, xbool_t bFin);
xweb_frame_t* XWebFrame_Alloc(xweb_frame_type_t eType, size_t nBuffSize);

xws_status_t XWebFrame_Create(xweb_frame_t *pFrame, uint8_t *pPayload, size_t nLength, xweb_frame_type_t eType, xbool_t bFin);
xws_status_t XWebFrame_AppendData(xweb_frame_t *pFrame, uint8_t* pData, size_t nSize);
xws_status_t XWebFrame_ParseData(xweb_frame_t *pFrame, uint8_t* pData, size_t nSize);
xws_status_t XWebFrame_TryParse(xweb_frame_t *pFrame, uint8_t* pData, size_t nSize);
xws_status_t XWebFrame_ParseBuff(xweb_frame_t *pFrame, xbyte_buffer_t *pBuffer);
xws_status_t XWebFrame_Parse(xweb_frame_t *pFrame);

size_t XWebFrame_GetPayloadLength(xweb_frame_t *pFrame);
size_t XWebFrame_GetFrameLength(xweb_frame_t *pFrame);
size_t XWebFrame_GetExtraLength(xweb_frame_t *pFrame);

xbyte_buffer_t* XWebFrame_GetBuffer(xweb_frame_t *pFrame);
const uint8_t* XWebFrame_GetPayload(xweb_frame_t *pFrame);

XSTATUS XWebFrame_GetExtraData(xweb_frame_t *pFrame, xbyte_buffer_t *pBuffer, xbool_t bAppend);
XSTATUS XWebFrame_CutExtraData(xweb_frame_t *pFrame);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XWS_H__ */