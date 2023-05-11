/*!
 *  @file libxutils/src/net/ws.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of Web Socket server and
 * client functionality based on xevents and xsock.
 */

#include "ws.h"

typedef struct xws_frame_code_ {
    const xweb_frame_type_t eType;
    const uint8_t nOpCode;
} xws_frame_code_t;

static const xws_frame_code_t g_wsFrameCodes[] =
{
    { 0x0, XWS_CONTINUATION },
    { 0x1, XWS_TEXT },
    { 0x2, XWS_BINARY },
    { 0x3, XWS_RESERVED1 },
    { 0x4, XWS_RESERVED2 },
    { 0x5, XWS_RESERVED3 },
    { 0x6, XWS_RESERVED4 },
    { 0x7, XWS_RESERVED5 },
    { 0x8, XWS_CLOSE },
    { 0x9, XWS_PING },
    { 0xA, XWS_PONG },
    { 0xB, XWS_RESERVED6 },
    { 0xC, XWS_RESERVED7 },
    { 0xD, XWS_RESERVED8 },
    { 0xE, XWS_RESERVED9 },
    { 0xF, XWS_RESERVED10 }
};

const char* XWebSock_GetStatusStr(xws_status_t eStatus)
{
    switch (eStatus)
    {
        case XWS_FRAME_COMPLETE:
            return "Successfully parsed WebSocket frame header and payload";
        case XWS_FRAME_PARSED:
            return "Successfully parsed WebSocket frame header";
        case XWS_ERR_ALLOC:
            return "Failed to allocate memory for WebSocket frame";
        case XWS_ERR_SIZE:
            return "Failed WebSocket frame size calculation";
        case XWS_FRAME_TOOBIG:
            return "Receiving WebSocket frame bigger than limit";
        case XWS_FRAME_INCOMPLETE:
            return "Invalid or incomplete WebSocket frame";
        case XWS_FRAME_INVALID:
            return "Invalid or unsupported WebSocket frame";
        case XWS_INVALID_REQUEST:
            return "Received invalid HTTP upgrade request";
        case XWS_INVALID_TYPE:
            return "Invalid or uninitialized frame type";
        case XWS_INVALID_ARGS:
            return "Invalid or uninitialized arguments";
        default:
            break;
    }

    return "Unknown status";
}

xweb_frame_type_t XWS_FrameType(uint8_t nOpCode)
{
    unsigned int i;

    for (i = 0; i < sizeof(g_wsFrameCodes) / sizeof(g_wsFrameCodes[0]); i++)
        if (g_wsFrameCodes[i].nOpCode == nOpCode) return g_wsFrameCodes[i].eType;

    return XWS_DUMMY;
}

uint8_t XWS_OpCode(xweb_frame_type_t eType)
{
    unsigned int i;

    for (i = 0; i < sizeof(g_wsFrameCodes) / sizeof(g_wsFrameCodes[0]); i++)
        if (g_wsFrameCodes[i].eType == eType) return g_wsFrameCodes[i].nOpCode;

    return 0;
}

uint8_t* XWS_CreateFrame(uint8_t *pPayload, size_t nLength, uint8_t nOpCode, xbool_t bFin, size_t *pFrameSize)
{
    if (pFrameSize != NULL) pFrameSize = 0;
    uint8_t nFIN = bFin ? XSTDOK : XSTDNON;
    uint8_t nStartByte = (nFIN << 7) | nOpCode;

    uint8_t nLengthByte = 0;
    size_t nHeaderSize = 2;

    if (nLength <= 125)
    {
        nLengthByte = nLength;
    }
    else if (nLength <= 65535)
    {
        nLengthByte = 126;
        nHeaderSize += 2;
    }
    else
    {
        nLengthByte = 127;
        nHeaderSize += 8;
    }

    size_t nFrameSize = nHeaderSize + nLength;
    uint8_t *pFrame = (uint8_t*)malloc(nFrameSize + 1);
    XASSERT((pFrame != NULL), NULL);

    pFrame[0] = nStartByte;
    pFrame[1] = nLengthByte;

    if (nLengthByte == 126)
    {
        uint16_t nLength16 = htons((uint16_t)nLength);
        memcpy(pFrame + 2, &nLength16, 2);
    }
    else if (nLengthByte == 127)
    {
        uint64_t nLength64 = htobe64(nLength);
        memcpy(pFrame + 2, &nLength64, 8);
    }

    if (pPayload != NULL && nLength)
        memcpy(pFrame + nHeaderSize, pPayload, nLength);

    pFrame[nFrameSize] = '\0';
    if (pFrameSize) *pFrameSize = nFrameSize;

    return pFrame;
}

void XWebFrame_Init(xweb_frame_t *pFrame)
{
    XASSERT_VOID(pFrame);
    pFrame->eType = XWS_DUMMY;
    pFrame->nPayloadLength = XSTDNON;
    pFrame->nHeaderSize = XSTDNON;
    pFrame->bComplete = XFALSE;
    pFrame->nOpCode = XSTDNON;
    pFrame->bAlloc = XFALSE;
    pFrame->bFin = XFALSE;

    xbyte_buffer_t *pbuffer = &pFrame->buffer;
    XByteBuffer_Init(pbuffer, XSTDNON, XFALSE);
}

void XWebFrame_Clear(xweb_frame_t *pFrame)
{
    XASSERT_VOID(pFrame);
    XByteBuffer_Clear(&pFrame->buffer);
}

void XWebFrame_Reset(xweb_frame_t *pFrame)
{
    XWebFrame_Clear(pFrame);
    XWebFrame_Init(pFrame);
}

void XWebFrame_Free(xweb_frame_t **pFrame)
{
    XASSERT_VOID((pFrame && *pFrame));
    xweb_frame_t *pWebFrame = *pFrame;

    XByteBuffer_Clear(&pWebFrame->buffer);
    if (pWebFrame->bAlloc)
    {
        free(pWebFrame);
        *pFrame = NULL;
    }
}

xws_status_t XWebFrame_Create(xweb_frame_t *pFrame, uint8_t *pPayload, size_t nLength, xweb_frame_type_t eType, xbool_t bFin)
{
    XASSERT(pFrame, XWS_INVALID_ARGS);
    uint8_t *pWsFrame = NULL;
    size_t nFrameSize = 0;

    XWebFrame_Init(pFrame);
    pFrame->eType = eType;
    pFrame->bFin = bFin;

    XASSERT((pFrame->eType != XWS_DUMMY), XWS_INVALID_TYPE);
    pFrame->nOpCode = XWS_OpCode(pFrame->eType);

    pWsFrame = XWS_CreateFrame(pPayload, nLength,
        pFrame->nOpCode, pFrame->bFin, &nFrameSize);

    XASSERT((pWsFrame != NULL), XWS_ERR_ALLOC);
    XASSERT_CALL((nFrameSize > nLength), free, pWsFrame, XWS_ERR_SIZE);

    XByteBuffer_OwnData(&pFrame->buffer, pWsFrame, nFrameSize);
    pFrame->nHeaderSize = nFrameSize - nLength;
    pFrame->nPayloadLength = nLength;

    return XWS_ERR_NONE;
}

xweb_frame_t* XWebFrame_Alloc(xweb_frame_type_t eType, size_t nBuffSize)
{
    xweb_frame_t *pFrame = (xweb_frame_t*)malloc(sizeof(xweb_frame_t));
    XASSERT((pFrame != NULL), NULL);
    
    XWebFrame_Init(pFrame);
    pFrame->bAlloc = XTRUE;
    pFrame->eType = eType;
    if (!nBuffSize) return pFrame;

    int nStatus = XByteBuffer_Resize(&pFrame->buffer, nBuffSize);
    XASSERT_CALL((nStatus > XSTDNON), free, pFrame, NULL);

    return pFrame;
}

xweb_frame_t* XWebFrame_New(uint8_t *pPayload, size_t nLength, xweb_frame_type_t eType, xbool_t bFin)
{
    xweb_frame_t *pFrame = XWebFrame_Alloc(eType, XSTDNON);
    XASSERT((pFrame != NULL), NULL);
    XSTATUS nStatus;

    nStatus = XWebFrame_Create(pFrame, pPayload, nLength, eType, bFin);
    XASSERT_CALL((nStatus == XWS_ERR_NONE), free, pFrame, NULL);

    pFrame->bAlloc = XTRUE;
    return pFrame;
}

xbool_t XWebFrame_CheckPayload(xweb_frame_t *pFrame)
{
    XASSERT_RET((pFrame &&
        pFrame->buffer.pData &&
        pFrame->buffer.nUsed &&
        pFrame->nPayloadLength &&
        pFrame->nHeaderSize), XFALSE);

    const size_t nDataSize = pFrame->buffer.nUsed;
    XASSERT_RET((nDataSize > pFrame->nHeaderSize), XFALSE);
    const size_t nPayloadSize = nDataSize - pFrame->nHeaderSize;

    if (nPayloadSize >= pFrame->nPayloadLength) pFrame->bComplete = XTRUE;
    return nDataSize > pFrame->nHeaderSize ? XTRUE : XFALSE;
}

const uint8_t* XWebFrame_GetPayload(xweb_frame_t *pFrame)
{
    XASSERT_RET(XWebFrame_CheckPayload(pFrame), NULL);
    return pFrame->buffer.pData + pFrame->nHeaderSize;
}

size_t XWebFrame_GetPayloadLength(xweb_frame_t *pFrame)
{
    XASSERT_RET(XWebFrame_CheckPayload(pFrame), XSTDNON);
    return pFrame->buffer.nUsed - pFrame->nHeaderSize;
}

size_t XWebFrame_GetFrameLength(xweb_frame_t *pFrame)
{
    XASSERT_RET((pFrame &&
        pFrame->buffer.pData &&
        pFrame->buffer.nUsed &&
        pFrame->nHeaderSize), XSTDNON);

    size_t nFrameSize = pFrame->nHeaderSize + pFrame->nPayloadLength;
    if (pFrame->buffer.nUsed < nFrameSize) return pFrame->buffer.nUsed;

    pFrame->bComplete = XTRUE;
    return nFrameSize;
}

size_t XWebFrame_GetExtraLength(xweb_frame_t *pFrame)
{
    size_t nFrameSize = XWebFrame_GetFrameLength(pFrame);
    XASSERT_RET((nFrameSize && pFrame->bComplete), XSTDNON);
    XASSERT_RET((nFrameSize > pFrame->buffer.nUsed), XSTDNON);
    return pFrame->buffer.nUsed - nFrameSize;
}

XSTATUS XWebFrame_CutExtraData(xweb_frame_t *pFrame)
{
    size_t nExtraLength = XWebFrame_GetExtraLength(pFrame);
    XASSERT_RET(nExtraLength, XSTDNON);

    size_t nFrameSize = pFrame->nHeaderSize + pFrame->nPayloadLength;
    return XByteBuffer_Terminate(&pFrame->buffer, nFrameSize);
}

XSTATUS XWebFrame_GetExtraData(xweb_frame_t *pFrame, xbyte_buffer_t *pBuffer, xbool_t bAppend)
{
    size_t nExtraLength = XWebFrame_GetExtraLength(pFrame);
    XASSERT_RET(nExtraLength, XSTDNON);

    size_t nFrameSize = pFrame->nHeaderSize + pFrame->nPayloadLength;
    const uint8_t *pData = pFrame->buffer.pData + nFrameSize;

    if (!bAppend) XByteBuffer_Init(pBuffer, XSTDNON, XFALSE);
    return XByteBuffer_Add(pBuffer, pData, nExtraLength);
}

xbyte_buffer_t* XWebFrame_GetBuffer(xweb_frame_t *pFrame)
{
    XASSERT_RET(pFrame, NULL);
    return &pFrame->buffer;
}

xws_status_t XWebFrame_Parse(xweb_frame_t *pFrame)
{
    XASSERT(pFrame, XWS_INVALID_ARGS);
    pFrame->bComplete = XFALSE;

    size_t nSize = pFrame->buffer.nUsed;
    const uint8_t *pData = pFrame->buffer.pData;
    if (pData == NULL || nSize < 2) return XWS_FRAME_INCOMPLETE;

    uint8_t nStartByte = pData[0];
    uint8_t nNextByte = pData[1];
    uint8_t nLengthByte = nNextByte & 0x7F;

    pFrame->bFin = (nStartByte & 0x80) >> 7;
    pFrame->nOpCode = nStartByte & 0x0F;
    pFrame->eType = XWS_FrameType(pFrame->nOpCode);

    if (nLengthByte <= 125)
    {
        pFrame->nPayloadLength = nLengthByte;
        pFrame->nHeaderSize = 2;
    }
    else if (nLengthByte == 126)
    {
        if (nSize < 4) return XWS_FRAME_INCOMPLETE;
        uint16_t nLength16 = 0;
        memcpy(&nLength16, pData + 2, 2);

        pFrame->nPayloadLength = ntohs(nLength16);
        pFrame->nHeaderSize = 4;
    }
    else
    {
        if (nSize < 10) return XWS_FRAME_INCOMPLETE;
        uint64_t nLength64 = 0;
        memcpy(&nLength64, pData + 2, 8);

        pFrame->nPayloadLength = be64toh(nLength64);
        pFrame->nHeaderSize = 10;
    }

    size_t nFrameLength = XWebFrame_GetFrameLength(pFrame);
    return nFrameLength && pFrame->bComplete ?
        XWS_FRAME_COMPLETE : XWS_FRAME_INCOMPLETE;
}

xws_status_t XWebFrame_AppendData(xweb_frame_t *pFrame, uint8_t* pData, size_t nSize)
{
    XASSERT((pFrame && pData && nSize), XWS_INVALID_ARGS);
    xbyte_buffer_t *pBuffer = &pFrame->buffer;

    int nStatus = XByteBuffer_Add(pBuffer, pData, nSize);
    return (nStatus > 0) ? XWS_ERR_NONE : XWS_ERR_ALLOC;
}

xws_status_t XWebFrame_TryParse(xweb_frame_t *pFrame, uint8_t* pData, size_t nSize)
{
    xws_status_t nStatus = XWebFrame_AppendData(pFrame, pData, nSize);
    return (nStatus > 0) ? XWebFrame_Parse(pFrame) : nStatus;
}

xws_status_t XWebFrame_ParseData(xweb_frame_t *pFrame, uint8_t* pData, size_t nSize)
{
    XWebFrame_Init(pFrame);
    return XWebFrame_TryParse(pFrame, pData, nSize);
}

xws_status_t XWebFrame_ParseBuff(xweb_frame_t *pFrame, xbyte_buffer_t *pBuffer)
{
    XWebFrame_Init(pFrame);
    XByteBuffer_Set(&pFrame->buffer, pBuffer);
    return XWebFrame_Parse(pFrame);
}