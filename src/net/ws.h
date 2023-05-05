/*!
 *  @file libxutils/src/net/ws.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of Web Socket server
 * and client functionality based on xhttp_t.
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

void XWS_InitFrame(xws_frame_t *pFrame);
void XWS_ClearFrame(xws_frame_t *pFrame);
void XWS_ResetFrame(xws_frame_t *pFrame);

uint8_t* XWS_CreateFrame(uint8_t *pPayload, size_t nLength, uint8_t nOpCode, xbool_t bFin, size_t *pFrameSize);
xbyte_buffer_t* XWS_Create(xws_frame_t *pFrame, uint8_t *pPayload, size_t nPayloadLength);

XSTATUS XWS_Parse(xws_frame_t *pFrame);
const uint8_t* XWS_GetPayload(xws_frame_t *pFrame);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XWS_H__ */