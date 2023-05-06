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
    const xws_frame_type_t eType;
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

xws_frame_type_t XWS_FrameType(uint8_t nOpCode)
{
    unsigned int i;

    for (i = 0; sizeof(g_wsFrameCodes) / sizeof(g_wsFrameCodes[0]); i++)
        if (g_wsFrameCodes[i].nOpCode == nOpCode) return g_wsFrameCodes[i].eType;

    return XWS_INVALID;
}

uint8_t XWS_OpCode(xws_frame_type_t eType)
{
    unsigned int i;

    for (i = 0; sizeof(g_wsFrameCodes) / sizeof(g_wsFrameCodes[0]); i++)
        if (g_wsFrameCodes[i].eType == eType) return g_wsFrameCodes[i].nOpCode;

    return 0;
}

uint8_t* XWS_CreateFrame(uint8_t *pPayload, size_t nLength, uint8_t nOpCode, xbool_t bFin, size_t *pFrameSize)
{
    if (*pFrameSize) pFrameSize = 0;
    XASSERT((pPayload && nLength), NULL);

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

    memcpy(pFrame + nHeaderSize, pPayload, nLength);
    if (*pFrameSize) *pFrameSize = nFrameSize;

    pFrame[nFrameSize] = '\0';
    return pFrame;
}

void XWSFrame_Init(xws_frame_t *pFrame)
{
    XASSERT_VOID(pFrame);
    pFrame->eType = XWS_INVALID;
    pFrame->nPayloadLength = XSTDNON;
    pFrame->nHeaderSize = XSTDNON;
    pFrame->bComplete = XFALSE;
    pFrame->nOpCode = 0;
    pFrame->bFin = XFALSE;

    xbyte_buffer_t *pRawData = &pFrame->rawData;
    XByteBuffer_Init(pRawData, XSTDNON, XFALSE);
}

void XWSFrame_Clear(xws_frame_t *pFrame)
{
    XASSERT_VOID(pFrame);
    XByteBuffer_Clear(&pFrame->rawData);
}

void XWSFrame_Reset(xws_frame_t *pFrame)
{
    XWSFrame_Clear(pFrame);
    XWSFrame_Init(pFrame);
}

xbyte_buffer_t* XWSFrame_Create(xws_frame_t *pFrame, uint8_t *pPayload, size_t nPayloadLength)
{
    XASSERT(pFrame, NULL);
    size_t nFrameSize = 0;
    uint8_t *pWsFrame = NULL;

    XASSERT((pFrame->eType != XWS_INVALID), NULL);
    pFrame->nOpCode = XWS_OpCode(pFrame->eType);

    pWsFrame = XWS_CreateFrame(pPayload, nPayloadLength,
            pFrame->nOpCode, pFrame->bFin, &nFrameSize);

    XASSERT((pWsFrame != NULL), NULL);
    XASSERT_CALL((nFrameSize > nPayloadLength), free, pWsFrame, NULL);

    XByteBuffer_Own(&pFrame->rawData, pWsFrame, nFrameSize);
    pFrame->nHeaderSize = nFrameSize - nPayloadLength;
    pFrame->nPayloadLength = nPayloadLength;

    return &pFrame->rawData;
}

const uint8_t* XWSFrame_GetPayload(xws_frame_t *pFrame)
{
    XASSERT_RET((pFrame && pFrame->rawData.pData), NULL);
    XASSERT_RET((pFrame->nPayloadLength), NULL);
    const size_t nDataSize = pFrame->rawData.nUsed;
    XASSERT_RET((nDataSize > pFrame->nHeaderSize), NULL);
    return pFrame->rawData.pData + pFrame->nHeaderSize;
}

size_t XWSFrame_GetPayloadLength(xws_frame_t *pFrame)
{
    XASSERT_RET((pFrame && pFrame->rawData.pData), XSTDNON);
    XASSERT_RET((pFrame->nPayloadLength), XSTDNON);
    const size_t nDataSize = pFrame->rawData.nUsed;
    XASSERT_RET((nDataSize > pFrame->nHeaderSize), XSTDNON);
    return nDataSize - pFrame->nHeaderSize;
}

XSTATUS XWSFrame_AddData(xws_frame_t *pFrame, uint8_t* pData, size_t nSize)
{
    XASSERT((pFrame && pData && nSize), XSTDINV);
    xbyte_buffer_t *pBuffer = &pFrame->rawData;
    return XByteBuffer_Add(pBuffer, pData, nSize);
}

XSTATUS XWSFrame_Parse(xws_frame_t *pFrame)
{
    XASSERT(pFrame, XSTDINV);
    pFrame->bComplete = XFALSE;

    size_t nSize = pFrame->rawData.nUsed;
    const uint8_t *pData = pFrame->rawData.pData;
    if (pData == NULL || nSize < 2) return XSTDNON;

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
        if (nSize < 4) return XSTDNON;
        uint16_t nLength16 = 0;
        memcpy(&nLength16, pData + 2, 2);

        pFrame->nPayloadLength = ntohs(nLength16);
        pFrame->nHeaderSize = 4;
    }
    else
    {
        if (nSize < 10) return XSTDNON;
        uint64_t nLength64 = 0;
        memcpy(&nLength64, pData + 2, 8);

        pFrame->nPayloadLength = be64toh(nLength64);
        pFrame->nHeaderSize = 10;
    }

    size_t nFrameLength = pFrame->nHeaderSize + pFrame->nPayloadLength;
    if (pFrame->rawData.nUsed < nFrameLength) return XSTDNON;

    pFrame->bComplete = XTRUE;
    return XSTDOK;
}