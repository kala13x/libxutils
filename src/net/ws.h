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

typedef struct {
    xws_frame_type_t eType;
    xbyte_buffer_t rawData;
    size_t nPayloadLength;
    size_t nHeaderSize;
    xbool_t bComplete;
    uint8_t nOpCode;
    xbool_t bFin;
} xws_frame_t;

xws_frame_type_t XWS_FrameType(uint8_t nOpCode);
uint8_t XWS_OpCode(xws_frame_type_t eType);

void XWSFrame_Init(xws_frame_t *pFrame);
void XWSFrame_Clear(xws_frame_t *pFrame);
void XWSFrame_Reset(xws_frame_t *pFrame);

uint8_t* XWS_CreateFrame(uint8_t *pPayload, size_t nLength, uint8_t nOpCode, xbool_t bFin, size_t *pFrameSize);
xbyte_buffer_t* XWSFrame_Create(xws_frame_t *pFrame, uint8_t *pPayload, size_t nPayloadLength);

const uint8_t* XWSFrame_GetPayload(xws_frame_t *pFrame);
size_t XWSFrame_GetPayloadLength(xws_frame_t *pFrame);

XSTATUS XWSFrame_AddData(xws_frame_t *pFrame, uint8_t* pData, size_t nSize);
XSTATUS XWSFrame_Parse(xws_frame_t *pFrame);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XWS_H__ */