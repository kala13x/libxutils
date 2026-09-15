/* libxutils: bounded parser fuzzing with cleanup on both success and rejection. */
#include "xstd.h"
#include "log.h"
#include "fuzz_parsers.h"
#include "json.h"
#include "http.h"
#include "ws.h"
#include "jwt.h"
#include "base64.h"
#include "addr.h"
#include "mdtp.h"
#include "rtp.h"
#include "str.h"
#include "array.h"
#include "buf.h"
#include "crypt.h"
#include "xfs.h"
#include "xtime.h"


/* A cursor over the input, so a target can take a few parameters off the
 * front and treat the rest as the payload. Everything it hands back is
 * bounded by what is actually there. */
typedef struct {
    const uint8_t *pData;
    size_t nSize;
    size_t nAt;
} xfuzz_reader_t;

static void XFuzz_ReaderInit(xfuzz_reader_t *pReader, const uint8_t *pData, size_t nSize)
{
    pReader->pData = pData;
    pReader->nSize = nSize;
    pReader->nAt = 0;
}

static uint8_t XFuzz_Byte(xfuzz_reader_t *pReader)
{
    if (pReader->nAt >= pReader->nSize) return 0;
    return pReader->pData[pReader->nAt++];
}

static size_t XFuzz_Left(const xfuzz_reader_t *pReader)
{
    return pReader->nSize - pReader->nAt;
}

static const uint8_t *XFuzz_Rest(const xfuzz_reader_t *pReader)
{
    return &pReader->pData[pReader->nAt];
}

/* A NUL terminated copy of what is left, for the entry points that take a
 * C string. The caller frees it. */
static char *XFuzz_RestString(const xfuzz_reader_t *pReader)
{
    size_t nLeft = XFuzz_Left(pReader);
    char *pStr = (char*)malloc(nLeft + 1);
    if (pStr == NULL) return NULL;

    if (nLeft) memcpy(pStr, XFuzz_Rest(pReader), nLeft);
    pStr[nLeft] = 0;
    return pStr;
}

/* Feeds a byte buffer to a parser the way a socket would: in chunks whose
 * sizes come from the input, so the split points are fuzzed too. This is
 * where incremental parsers go wrong - a length taken from the buffer's
 * capacity rather than what has arrived, a packet declared complete on a
 * partial read - and none of it shows up when the whole message is handed
 * over at once. */
typedef int (*xfuzz_stream_cb_t)(xbyte_buffer_t *pBuffer);

static void XFuzz_Stream(xfuzz_reader_t *pReader, xfuzz_stream_cb_t consume)
{
    /* Up to eight chunk sizes, cycled if the payload outlasts them. */
    uint8_t sChunks[8];
    for (size_t i = 0; i < sizeof(sChunks); i++) sChunks[i] = XFuzz_Byte(pReader);

    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, 0, XFALSE);

    const uint8_t *pRest = XFuzz_Rest(pReader);
    size_t nLeft = XFuzz_Left(pReader);
    size_t nAt = 0, nRound = 0;

    while (nAt < nLeft)
    {
        size_t nChunk = (size_t)sChunks[nRound++ % sizeof(sChunks)] + 1;
        if (nChunk > nLeft - nAt) nChunk = nLeft - nAt;

        if (XByteBuffer_Add(&buffer, &pRest[nAt], nChunk) <= 0) break;
        nAt += nChunk;

        if (consume(&buffer) < 0) break;
    }

    XByteBuffer_Clear(&buffer);
}

/* Consumes as many whole HTTP messages as the buffer holds. */
static int XFuzz_HttpConsume(xbyte_buffer_t *pBuffer)
{
    for (;;)
    {
        xhttp_t http;
        xhttp_status_t eStatus = XHTTP_ParseBuff(&http, pBuffer);

        if (eStatus != XHTTP_COMPLETE) { XHTTP_Clear(&http); return 0; }

        size_t nPacket = XHTTP_GetPacketSize(&http);

        /* A complete message cannot claim more bytes than were parsed. */
        if (nPacket > pBuffer->nUsed) abort();

        /* Its body has to lie inside its own storage. */
        const uint8_t *pBody = XHTTP_GetBody(&http);
        size_t nBody = XHTTP_GetBodySize(&http);

        if (pBody != NULL && nBody)
        {
            uintptr_t nStart = (uintptr_t)http.rawData.pData;
            uintptr_t nOffset = (uintptr_t)pBody - nStart;

            if ((uintptr_t)pBody < nStart ||
                nOffset > http.rawData.nUsed ||
                nBody > http.rawData.nUsed - nOffset) abort();
        }

        XHTTP_Clear(&http);

        if (!nPacket) return 0;              /* No progress, stop */
        XByteBuffer_Advance(pBuffer, nPacket);
        if (!pBuffer->nUsed) return 0;
    }
}

/* Consumes as many whole MDTP packets as the buffer holds. */
static int XFuzz_MdtpConsume(xbyte_buffer_t *pBuffer)
{
    for (;;)
    {
        xpacket_t packet;
        xpacket_status_t eStatus = XPacket_Parse(&packet, pBuffer->pData, pBuffer->nUsed);

        if (eStatus != XPACKET_COMPLETE)
        {
            XPacket_Clear(&packet);
            return (eStatus == XPACKET_INCOMPLETE || eStatus == XPACKET_PARSED) ? 0 : -1;
        }

        size_t nPacket = XPacket_GetSize(&packet);
        if (nPacket > pBuffer->nUsed) abort();

        const uint8_t *pPayload = packet.pPayload;
        if (pPayload != NULL)
        {
            uintptr_t nOffset = (uintptr_t)pPayload - (uintptr_t)pBuffer->pData;

            if (pPayload < pBuffer->pData ||
                nOffset > pBuffer->nUsed ||
                packet.header.nPayloadSize > pBuffer->nUsed - nOffset) abort();
        }

        XPacket_Clear(&packet);

        if (!nPacket) return 0;
        XByteBuffer_Advance(pBuffer, nPacket);
        if (!pBuffer->nUsed) return 0;
    }
}

/* Consumes as many whole WebSocket frames as the buffer holds. */
static int XFuzz_WsConsume(xbyte_buffer_t *pBuffer)
{
    for (;;)
    {
        xws_frame_t frame;
        xws_status_t eStatus = XWebFrame_ParseData(&frame, pBuffer->pData, pBuffer->nUsed);

        if (eStatus != XWS_FRAME_COMPLETE)
        {
            XWebFrame_Clear(&frame);
            return (eStatus == XWS_FRAME_INCOMPLETE || eStatus == XWS_FRAME_PARSED) ? 0 : -1;
        }

        size_t nFrame = XWebFrame_GetFrameLength(&frame);
        if (nFrame > pBuffer->nUsed) abort();

        const uint8_t *pPayload = XWebFrame_GetPayload(&frame);
        size_t nPayload = XWebFrame_GetPayloadLength(&frame);

        if (pPayload != NULL && nPayload)
        {
            uintptr_t nStart = (uintptr_t)frame.buffer.pData;
            uintptr_t nOffset = (uintptr_t)pPayload - nStart;

            if ((uintptr_t)pPayload < nStart ||
                nOffset > frame.buffer.nUsed ||
                nPayload > frame.buffer.nUsed - nOffset) abort();
        }

        XWebFrame_Clear(&frame);

        if (!nFrame) return 0;
        XByteBuffer_Advance(pBuffer, nFrame);
        if (!pBuffer->nUsed) return 0;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *pData, size_t nSize)
{
    xlog_setfl(0);
    if (nSize == 0 || nSize > 65536) return 0;
    uint8_t nTarget = pData[0] % XFUZZ_TARGET_COUNT;
    pData++;
    nSize--;
    if (nTarget == XFUZZ_TARGET_JSON)
    {
        xjson_t json;
        XJSON_Parse(&json, NULL, (const char*)pData, nSize);
        XJSON_Destroy(&json);
    }
    else if (nTarget == XFUZZ_TARGET_HTTP)
    {
        xhttp_t http;
        XHTTP_ParseData(&http, (uint8_t*)pData, nSize);
        size_t nExtra = XHTTP_GetExtraSize(&http);
        const uint8_t *pExtra = XHTTP_GetExtraData(&http);
        if (pExtra && nExtra)
        {
            uintptr_t nStart = (uintptr_t)http.rawData.pData;
            if ((uintptr_t)pExtra < nStart || (uintptr_t)pExtra - nStart > http.rawData.nUsed ||
                nExtra > http.rawData.nUsed - ((uintptr_t)pExtra - nStart))
                abort();
        }
        XHTTP_Clear(&http);
    }
    else if (nTarget == XFUZZ_TARGET_WS)
    {
        xws_frame_t frame;
        if (XWebFrame_ParseData(&frame, (uint8_t*)pData, nSize) == XWS_FRAME_COMPLETE)
        {
            /* Everything a completed frame reports has to lie inside what
               was handed in, the same invariant the other framers hold. */
            const uint8_t *pPayload = XWebFrame_GetPayload(&frame);
            size_t nPayload = XWebFrame_GetPayloadLength(&frame);

            if (pPayload != NULL && nPayload)
            {
                uintptr_t nStart = (uintptr_t)frame.buffer.pData;
                uintptr_t nOffset = (uintptr_t)pPayload - nStart;

                if ((uintptr_t)pPayload < nStart ||
                    nOffset > frame.buffer.nUsed ||
                    nPayload > frame.buffer.nUsed - nOffset) abort();
            }
        }

        XWebFrame_Clear(&frame);
    }
    else if (nTarget == XFUZZ_TARGET_BASE64)
    {
        size_t nLength = nSize;
        char *pDecoded = XBase64_Decrypt(pData, &nLength);
        free(pDecoded);
        nLength = nSize;
        pDecoded = XBase64_UrlDecrypt(pData, &nLength);
        free(pDecoded);
    }
    else if (nTarget == XFUZZ_TARGET_JWT)
    {
        xjwt_t jwt;
        XJWT_Init(&jwt, XJWT_ALG_HS256);
        XJWT_Parse(&jwt, (const char*)pData, nSize, NULL, 0);
        XJWT_Destroy(&jwt);
    }
    else if (nTarget == XFUZZ_TARGET_MDTP)
    {
        xpacket_t packet;
        memset(&packet, 0, sizeof(packet));

        if (XPacket_Parse(&packet, pData, nSize) == XPACKET_COMPLETE)
        {
            /* A completed parse must point its payload inside the input. */
            const uint8_t *pPayload = packet.pPayload;
            if (pPayload != NULL)
            {
                uintptr_t nOffset = (uintptr_t)pPayload - (uintptr_t)pData;
                if (pPayload < pData || nOffset > nSize ||
                    packet.header.nPayloadSize > nSize - nOffset) abort();
            }
        }

        XPacket_Clear(&packet);
    }
    else if (nTarget == XFUZZ_TARGET_RTP)
    {
        xrtp_packet_t packet;
        memset(&packet, 0, sizeof(packet));

        int nParsed = XRTP_ParsePacket(&packet, (uint8_t*)pData, nSize);
        if (nParsed > 0)
        {
            /* Everything the parser reports has to lie inside the input. */
            if ((size_t)nParsed > nSize) abort();
            if (packet.nPayloadSize < 0 || (size_t)packet.nPayloadSize > nSize) abort();
            if (packet.nUnusedBytes < 0 || (size_t)packet.nUnusedBytes > nSize) abort();

            if (packet.pPayload != NULL)
            {
                uintptr_t nOffset = (uintptr_t)packet.pPayload - (uintptr_t)pData;
                if (packet.pPayload < pData || nOffset > nSize) abort();
            }
        }

        xrtp_header_t header;
        XRTP_ParseHeader(&header, pData, nSize);
    }
    else if (nTarget == XFUZZ_TARGET_HTTP_STREAM)
    {
        xfuzz_reader_t reader;
        XFuzz_ReaderInit(&reader, pData, nSize);
        XFuzz_Stream(&reader, XFuzz_HttpConsume);
    }
    else if (nTarget == XFUZZ_TARGET_MDTP_STREAM)
    {
        xfuzz_reader_t reader;
        XFuzz_ReaderInit(&reader, pData, nSize);
        XFuzz_Stream(&reader, XFuzz_MdtpConsume);
    }
    else if (nTarget == XFUZZ_TARGET_WS_STREAM)
    {
        xfuzz_reader_t reader;
        XFuzz_ReaderInit(&reader, pData, nSize);
        XFuzz_Stream(&reader, XFuzz_WsConsume);
    }
    else if (nTarget == XFUZZ_TARGET_STR_OPS)
    {
        /* The string helpers, on bytes that are not necessarily text. Each
           one writes into a fixed buffer, so what is checked is that the
           result stays inside it and stays terminated. */
        xfuzz_reader_t reader;
        XFuzz_ReaderInit(&reader, pData, nSize);

        uint8_t nOp = XFuzz_Byte(&reader);
        uint8_t nWidth = XFuzz_Byte(&reader);

        char *pInput = XFuzz_RestString(&reader);
        if (pInput == NULL) return 0;

        char sOut[256];
        memset(sOut, 0, sizeof(sOut));

        switch (nOp % 8)
        {
            case 0: xstrncpyf(sOut, sizeof(sOut), "%s", pInput); break;
            case 1: xstrncpyfl(sOut, sizeof(sOut), (size_t)nWidth, '.', "%s", pInput); break;
            case 2: xstrnlcpyf(sOut, sizeof(sOut), (size_t)nWidth, ' ', "%s", pInput); break;
            case 3: xstrnfill(sOut, sizeof(sOut), (size_t)nWidth, pInput[0] ? pInput[0] : 'x'); break;
            case 4:
            {
                xarray_t *pParts = xstrsplit(pInput, ",");
                XArray_Destroy(pParts);
                break;
            }
            case 5:
            {
                char *pRep = xstrrep(pInput, ",", ";");
                free(pRep);
                break;
            }
            case 6:
            {
                xstrncpy(sOut, sizeof(sOut), pInput);
                xstrcase(sOut, XSTR_UPPER);
                break;
            }
            default:
            {
                xstring_t str;
                if (XString_InitFrom(&str, "%s", pInput) >= 0) XString_Clear(&str);
                break;
            }
        }

        /* Whatever ran, the destination is still a bounded C string. */
        if (strnlen(sOut, sizeof(sOut)) >= sizeof(sOut)) abort();
        free(pInput);
    }
    else if (nTarget == XFUZZ_TARGET_TIME)
    {
        /* Timestamp parsing, and the round trip for anything accepted: a
           timestamp this library writes has to read back as itself. */
        xfuzz_reader_t reader;
        XFuzz_ReaderInit(&reader, pData, nSize);

        uint8_t nForm = XFuzz_Byte(&reader);
        char *pText = XFuzz_RestString(&reader);
        if (pText == NULL) return 0;

        xtime_t parsed;
        memset(&parsed, 0, sizeof(parsed));
        int nFields = 0;

        switch (nForm % 5)
        {
            case 0: nFields = XTime_FromStr(&parsed, pText); break;
            case 1: nFields = XTime_FromHstr(&parsed, pText); break;
            case 2: nFields = XTime_FromLstr(&parsed, pText); break;
            case 3: nFields = XTime_FromRstr(&parsed, pText); break;
            default: nFields = XTime_FromISO(&parsed, pText); break;
        }

        if (nFields > 0)
        {
            /* An accepted timestamp is a real date, and its fraction fits
               the two digit field every format writes. */
            if (!parsed.nYear || parsed.nMonth < 1 || parsed.nMonth > 12) abort();
            if (parsed.nDay < 1 || parsed.nDay > 31) abort();
            if (parsed.nHour > 23 || parsed.nMin > 59 || parsed.nSec > 59) abort();
            if (parsed.nFraq > XTIME_FRAQ_MAX) abort();

            char sBack[XTIME_MAX];
            size_t nWritten = XTime_ToStr(&parsed, sBack, sizeof(sBack));
            if (nWritten != 16) abort();

            xtime_t again;
            memset(&again, 0, sizeof(again));

            if (XTime_FromStr(&again, sBack) == 7 &&
                memcmp(&again, &parsed, sizeof(again)) != 0) abort();
        }

        free(pText);
    }
    else if (nTarget == XFUZZ_TARGET_PATH)
    {
        /* Filesystem path splitting, without touching the filesystem. */
        xfuzz_reader_t reader;
        XFuzz_ReaderInit(&reader, pData, nSize);

        char *pPathStr = XFuzz_RestString(&reader);
        if (pPathStr == NULL) return 0;

        xpath_t path;
        if (XPath_Parse(&path, pPathStr, XFALSE) == XSTDOK)
        {
            /* Each part is a bounded, terminated string in its own field. */
            if (strnlen(path.sPath, sizeof(path.sPath)) >= sizeof(path.sPath)) abort();
            if (strnlen(path.sFile, sizeof(path.sFile)) >= sizeof(path.sFile)) abort();
        }

        free(pPathStr);
    }
    else if (nTarget == XFUZZ_TARGET_HEX)
    {
        /* Hex encoding and back, which is used for checksums and keys. */
        xfuzz_reader_t reader;
        XFuzz_ReaderInit(&reader, pData, nSize);

        uint8_t nColumns = XFuzz_Byte(&reader);
        size_t nLeft = XFuzz_Left(&reader);
        if (!nLeft) return 0;

        size_t nHexLength = nLeft;
        uint8_t *pHex = XCrypt_HEX(XFuzz_Rest(&reader), &nHexLength, " ", nColumns % 32, XFALSE);

        if (pHex != NULL)
        {
            size_t nBack = nHexLength;
            uint8_t *pPlain = XDecrypt_HEX(pHex, &nBack, XFALSE);

            /* Whatever comes back out has to be no longer than what went
               in, or the decoder invented bytes. */
            if (pPlain != NULL && nBack > nLeft) abort();
            free(pPlain);
        }

        free(pHex);
    }
    else if (nTarget == XFUZZ_TARGET_JSON_ROUNDTRIP)
    {
        /* Anything the JSON parser accepts, it has to be able to write out
           again and re-read to the same thing. A dump that cannot be
           parsed back means one of the two is wrong about the grammar. */
        xjson_t json;
        if (XJSON_Parse(&json, NULL, (const char*)pData, nSize))
        {
            size_t nDumped = 0;
            char *pDumped = XJSON_DumpObj(json.pRootObj, 0, &nDumped);

            if (pDumped != NULL && nDumped)
            {
                xjson_t again;
                if (!XJSON_Parse(&again, NULL, pDumped, nDumped)) abort();
                XJSON_Destroy(&again);
            }

            free(pDumped);
        }

        XJSON_Destroy(&json);
    }
    else if (nTarget == XFUZZ_TARGET_UNIX_URL)
    {
        /* The unix form splits a socket path from a request path on the
           first separator, so the split offsets come straight from input. */
        char *pURL = (char*)malloc(nSize + 1);
        if (pURL == NULL) return 0;
        memcpy(pURL, pData, nSize);
        pURL[nSize] = 0;

        xlink_t link;
        if (XLink_ParseUnix(&link, pURL) == XSTDOK)
        {
            /* Neither half may run past the buffer each was copied into. */
            if (strlen(link.sAddr) >= sizeof(link.sAddr)) abort();
            if (strlen(link.sUri) >= sizeof(link.sUri)) abort();
            if (strlen(link.sFile) >= sizeof(link.sFile)) abort();
        }

        free(pURL);
    }
    else if (nTarget == XFUZZ_TARGET_GLOB)
    {
        /* Both sides of the match come from input: the first byte says how
           much of it is the pattern, the rest is the subject. */
        if (nSize < 2) return 0;

        const uint8_t *pBody = &pData[1];
        size_t nBody = nSize - 1;
        size_t nSplit = (size_t)pData[0] % (nBody + 1);

        char *pPattern = (char*)malloc(nSplit + 1);
        char *pSubject = (char*)malloc(nBody - nSplit + 1);

        if (pPattern == NULL || pSubject == NULL)
        {
            free(pPattern);
            free(pSubject);
            return 0;
        }

        memcpy(pPattern, pBody, nSplit);
        pPattern[nSplit] = 0;

        memcpy(pSubject, &pBody[nSplit], nBody - nSplit);
        pSubject[nBody - nSplit] = 0;

        xstrregex(pSubject, strlen(pSubject), pPattern);

        free(pPattern);
        free(pSubject);
    }
    else
    {
        char *pURL = (char*)malloc(nSize + 1);
        if (pURL == NULL) return 0;
        memcpy(pURL, pData, nSize);
        pURL[nSize] = 0;
        xlink_t link;
        XLink_Parse(&link, pURL);
        free(pURL);
    }
    return 0;
}
