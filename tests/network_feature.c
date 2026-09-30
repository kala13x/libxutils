/* Complete request/response exchanges through each public networking layer. */
#include "test.h"
#include "api.h"
#include "thread.h"
#include "xtime.h"
#include "sha1.h"
#include "base64.h"
#include <unistd.h>
#ifdef XSOCK_USE_SSL
#include "tls_fixture.h"
#endif

#ifdef FEATURE_WRAP_SEND
static _Thread_local int g_nWriteFD = -1;
static _Thread_local size_t g_nWriteLimit;
ssize_t __real_send(int nFD, const void *pData, size_t nSize, int nFlags);
ssize_t __real_write(int nFD, const void *pData, size_t nSize);

ssize_t __wrap_send(int nFD, const void *pData, size_t nSize, int nFlags)
{
    if (nFD == g_nWriteFD && g_nWriteLimit) nSize = XSTD_MIN(nSize, g_nWriteLimit);
    return __real_send(nFD, pData, nSize, nFlags);
}

ssize_t __wrap_write(int nFD, const void *pData, size_t nSize)
{
    if (nFD == g_nWriteFD && g_nWriteLimit) nSize = XSTD_MIN(nSize, g_nWriteLimit);
    return __real_write(nFD, pData, nSize);
}
#endif

typedef struct feature_ feature_t;

enum {
    FEATURE_VALID, FEATURE_REQUEST_BODY, FEATURE_RESPONSE_BODY, FEATURE_REQUEST_ORDER,
    FEATURE_RESPONSE_ORDER, FEATURE_REQUEST_TYPE, FEATURE_RESPONSE_TYPE
};

typedef struct {
    feature_t *pTest;
    xsock_t sock;
    xbyte_buffer_t rx;
    xbyte_buffer_t tx;
    xapi_session_t *pSession;
    xevent_data_t *pEvent;
    size_t nSeen;
    size_t nDelivered;
    size_t nControls;
    int nCloseFrames;
    size_t nRead;
    size_t nWritten;
    int nSide;
    int nClosed;
    xbool_t bUpgrade;
    xbool_t bTLSReady;
} feature_peer_t;

struct feature_ {
    feature_t *pServer;
    feature_t *pNext;
    uint16_t nPort;
    xapi_t api[2];
    xevents_t events[2];
    feature_peer_t peers[2];
    xsock_t listener;
    xapi_session_t *pListener;
    xevent_data_t *pListenEvent;
    xapi_type_t eType;
    int nLayer;
    int nFault;
    int nAccepted;
    int nHandshakes;
    int nUpgradeRejected;
    int nUnexpectedUpgrade;
    uint16_t nUpgradeStatus;
    const char *pUpgradeConnection;
    xbool_t bRejectUpgrade;
    xbool_t bRejectRequest;
    xhttp_method_t eUpgradeMethod;
    xatomic_t nErrors;
    int nInitialized;
    size_t nChunk;
    size_t nMessages;
    size_t nClientID;
    xbool_t bHash;
    xbool_t bTLS;
    xsock_cert_t serverCert;
    xsock_cert_t clientCert;
    xbool_t bFragment;
    xbool_t bControls;
    xatomic_t nStop;
    char sError[160];
    char sUpgradeKey[64];
};

static const size_t g_sizes[] = {0, 1, 2, 125, 126, 127, 1023, 4096, 65535, 65536, 65537};

static int feature_fail(feature_t *pTest, const char *pError)
{
    if (XSYNC_ATOMIC_ADD(&pTest->nErrors, 1) == 1) xstrncpy(pTest->sError, sizeof(pTest->sError), pError);
    return XSTDERR;
}

static uint8_t feature_byte(size_t nClientID, size_t nMessage, size_t nOffset, xbool_t bResponse)
{
    uint8_t nByte = (uint8_t)(nClientID * 47 + nMessage * 29 + nOffset * 131 + (nOffset >> 8));
    return bResponse ? (uint8_t)(nByte ^ 0xa7) : nByte;
}

static xws_status_t feature_ping(feature_peer_t *pPeer, size_t nMessage)
{
    uint8_t payload[] = {'p', 'i', 'n', 'g', 0, 0, 0, (uint8_t)nMessage};
    return XWS_AppendFrame(&pPeer->tx, payload, sizeof(payload), XWS_PING, XTRUE, XTRUE);
}

static int feature_message(feature_peer_t *pPeer, size_t nMessage, const uint8_t *pRequest)
{
    feature_t *pTest = pPeer->pTest;
    size_t nLength = g_sizes[nMessage];
    uint8_t *pPayload = (uint8_t*)malloc(nLength + 1);
    if (pPayload == NULL) return feature_fail(pTest, "Allocate message payload");
    for (size_t i = 0; i < nLength; i++)
        pPayload[i] = pPeer->nSide ? feature_byte(pTest->nClientID, nMessage, i, XFALSE) : (uint8_t)(pRequest[i] ^ 0xa7);
    xbool_t bMutate = nMessage == 2 && (pPeer->nSide ? pTest->nFault & 1 : pTest->nFault && !(pTest->nFault & 1));
    xbool_t bBody = bMutate && pTest->nFault <= FEATURE_RESPONSE_BODY;
    xbool_t bOrder = bMutate && pTest->nFault >= FEATURE_REQUEST_ORDER && pTest->nFault <= FEATURE_RESPONSE_ORDER;
    xbool_t bType = bMutate && pTest->nFault >= FEATURE_REQUEST_TYPE;
    if (bBody) pPayload[nLength / 2] ^= 1;
    size_t nSequence = bOrder ? nMessage - 1 : nMessage;

    int nStatus = XSTDERR;
    if (pTest->eType == XAPI_HTTP)
    {
        xhttp_t http;
        if (pPeer->nSide) XHTTP_InitRequest(&http, bType ? XHTTP_PUT : XHTTP_POST, "/feature", "1.1");
        else XHTTP_InitResponse(&http, bType ? 500 : 201, "1.1");
        XHTTP_AddHeader(&http, "Connection", "keep-alive");
        XHTTP_AddHeader(&http, "X-Sequence", "%zu", nSequence);
        XHTTP_AddHeader(&http, "X-Client-ID", "%zu", pTest->nClientID);
        XHTTP_AddHeader(&http, "Content-Type", "application/octet-stream");
        xbyte_buffer_t *pWire = XHTTP_Assemble(&http, pPayload, nLength);
        if (pWire != NULL) nStatus = XByteBuffer_AddBuff(&pPeer->tx, pWire);
        XHTTP_Clear(&http);
    }
    else if (pTest->eType == XAPI_WS)
    {
        if (pTest->bFragment && pPeer->nSide && nLength > 1)
        {
            size_t nFirst = nLength / 2;
            nStatus = XWS_AppendFrame(&pPeer->tx, pPayload, nFirst, XWS_BINARY, XTRUE, XFALSE);
            if (nStatus == XWS_ERR_NONE && pTest->bControls) nStatus = feature_ping(pPeer, nMessage);
            if (nStatus == XWS_ERR_NONE)
                nStatus = XWS_AppendFrame(&pPeer->tx, pPayload + nFirst, nLength - nFirst, XWS_CONTINUATION, XTRUE, XTRUE);
        }
        else
        {
            nStatus = XWS_AppendFrame(&pPeer->tx, pPayload, nLength, bType ? XWS_TEXT : XWS_BINARY, pPeer->nSide, XTRUE);
            if (nStatus == XWS_ERR_NONE && pPeer->nSide && pTest->bControls) nStatus = feature_ping(pPeer, nMessage);
        }
        nStatus = nStatus == XWS_ERR_NONE ? XSTDOK : XSTDERR;
    }
    else if (pTest->eType == XAPI_MDTP)
    {
        xpacket_t packet;
        if (XPacket_Init(&packet, pPayload, (uint32_t)nLength) == XPACKET_ERR_NONE)
        {
            packet.header.eType = pPeer->nSide ? XPACKET_TYPE_DATA : XPACKET_TYPE_ACK;
            if (bType) packet.header.eType = pPeer->nSide ? XPACKET_TYPE_ACK : XPACKET_TYPE_DATA;
            packet.header.nPacketID = (uint32_t)nSequence;
            packet.header.nSessionID = 0x12345678 + (uint32_t)pTest->nClientID;
            packet.header.nTimeStamp = 1700000000 + (uint32_t)nMessage;
            xstrncpy(packet.header.sPayloadType, sizeof(packet.header.sPayloadType), "application/octet-stream");
            xbyte_buffer_t *pWire = XPacket_Assemble(&packet);
            if (pWire != NULL) nStatus = XByteBuffer_AddBuff(&pPeer->tx, pWire);
        }
        XPacket_Clear(&packet);
    }
    else
    {
        uint32_t header[] = {htonl((uint32_t)nSequence), htonl((uint32_t)nLength)};
        nStatus = XByteBuffer_Add(&pPeer->tx, (uint8_t*)header, sizeof(header));
        if (nStatus > 0 && nLength) nStatus = XByteBuffer_Add(&pPeer->tx, pPayload, nLength);
    }

    free(pPayload);
    return nStatus > 0 ? XSTDOK : feature_fail(pTest, "Assemble the complete message");
}

static int feature_requests(feature_peer_t *pPeer)
{
    for (size_t i = 0; i < pPeer->pTest->nMessages; i++)
        if (feature_message(pPeer, i, NULL) < 0) return XSTDERR;
    return XSTDOK;
}

static int feature_verify(feature_peer_t *pPeer, const uint8_t *pData, size_t nLength, size_t nSequence)
{
    feature_t *pTest = pPeer->pTest;
    if (pPeer->nSeen >= sizeof(g_sizes) / sizeof(*g_sizes) || nSequence != pPeer->nSeen)
        return feature_fail(pTest, "Each message arrives once and in order");
    if (nLength != g_sizes[pPeer->nSeen]) return feature_fail(pTest, "The complete payload length is preserved");
    for (size_t i = 0; i < nLength; i++)
        if (pData[i] != feature_byte(pTest->nClientID, nSequence, i, pPeer->nSide))
            return feature_fail(pTest, "Every request and response byte matches the independent model");

    pPeer->nSeen++;
    if (!pPeer->nSide) return feature_message(pPeer, nSequence, pData);
    return XSTDOK;
}

static int feature_http(feature_peer_t *pPeer, xhttp_t *pHttp)
{
    pPeer->nDelivered++;
    const char *pSequence = XHTTP_GetHeader(pHttp, "X-Sequence");
    char sSequence[32];
    snprintf(sSequence, sizeof(sSequence), "%zu", pPeer->nSeen);
    if (pSequence == NULL || strcmp(pSequence, sSequence)) return feature_fail(pPeer->pTest, "HTTP sequence header");
    const char *pClientID = XHTTP_GetHeader(pHttp, "X-Client-ID");
    char sClientID[32];
    snprintf(sClientID, sizeof(sClientID), "%zu", pPeer->pTest->nClientID);
    if (pClientID == NULL || strcmp(pClientID, sClientID)) return feature_fail(pPeer->pTest, "HTTP client identity");
    if ((pPeer->nSide ? pHttp->eType != XHTTP_RESPONSE || pHttp->nStatusCode != 201 :
        pHttp->eType != XHTTP_REQUEST || pHttp->eMethod != XHTTP_POST || strcmp(pHttp->sUri, "/feature")) ||
        strcmp(pHttp->sVersion, "1.1"))
        return feature_fail(pPeer->pTest, "HTTP method, target and response status");
    const char *pType = XHTTP_GetHeader(pHttp, "Content-Type");
    if (pType == NULL || strcmp(pType, "application/octet-stream")) return feature_fail(pPeer->pTest, "HTTP content type");
    if (XHTTP_GetBodySize(pHttp) < pHttp->nContentLength) return feature_fail(pPeer->pTest, "HTTP body is complete");
    return feature_verify(pPeer, XHTTP_GetBody(pHttp), pHttp->nContentLength, pPeer->nSeen);
}

static int feature_mdtp(feature_peer_t *pPeer, xpacket_t *pPacket)
{
    pPeer->nDelivered++;
    xpacket_type_t eExpected = pPeer->nSide ? XPACKET_TYPE_ACK : XPACKET_TYPE_DATA;
    if (pPacket->header.eType != eExpected || pPacket->header.nSessionID != 0x12345678 + pPeer->pTest->nClientID ||
        pPacket->header.nTimeStamp != 1700000000 + pPeer->nSeen || strcmp(pPacket->header.sVersion, "1.0") ||
        (pPacket->header.nPayloadSize && strcmp(pPacket->header.sPayloadType, "application/octet-stream")))
        return feature_fail(pPeer->pTest, "MDTP type, session and content type survive the exchange");
    return feature_verify(pPeer, XPacket_GetPayload(pPacket), pPacket->header.nPayloadSize, pPacket->header.nPacketID);
}

static int feature_ws(feature_peer_t *pPeer, xws_frame_t *pFrame)
{
    feature_t *pTest = pPeer->pTest;
    const uint8_t *pPayload = XWebFrame_GetPayload(pFrame);
    const uint8_t closeBody[] = {3, 232, 'd', 'o', 'n', 'e'};
    if (pTest->bControls && pFrame->eType != XWS_BINARY)
    {
        if (!pFrame->bFin || pFrame->bMask || !!(pFrame->buffer.pData[1] & 0x80) != !pPeer->nSide)
            return feature_fail(pTest, "Control frames preserve their final bit and client/server masking");
        if (pFrame->eType == XWS_CLOSE)
        {
            if (pFrame->nPayloadLength != sizeof(closeBody) || memcmp(pPayload, closeBody, sizeof(closeBody)) ||
                pPeer->nSeen != pTest->nMessages || pPeer->nControls != pTest->nMessages || pPeer->nCloseFrames)
                return feature_fail(pTest, "Close code 1000 and reason arrive after all data and control replies");
            pPeer->nCloseFrames++;
            if (!pPeer->nSide &&
                XWS_AppendFrame(&pPeer->tx, closeBody, sizeof(closeBody), XWS_CLOSE, XFALSE, XTRUE) != XWS_ERR_NONE)
                return feature_fail(pTest, "Send the server close acknowledgment");
            return XSTDOK;
        }
        uint8_t expected[] = {'p', 'i', 'n', 'g', 0, 0, 0, (uint8_t)pPeer->nControls};
        if (pFrame->eType != (pPeer->nSide ? XWS_PONG : XWS_PING) || pFrame->nPayloadLength != sizeof(expected) ||
            memcmp(pPayload, expected, sizeof(expected))) return feature_fail(pTest, "Each pong echoes its exact ping payload");
        pPeer->nControls++;
        if (!pPeer->nSide &&
            XWS_AppendFrame(&pPeer->tx, pPayload, sizeof(expected), XWS_PONG, XFALSE, XTRUE) != XWS_ERR_NONE)
            return feature_fail(pTest, "Queue the matching pong");
    }
    else
    {
        pPeer->nDelivered++;
        if (pFrame->eType != XWS_BINARY || !pFrame->bFin)
            return feature_fail(pTest, "WebSocket message type and final frame");
        if (feature_verify(pPeer, pPayload, pFrame->nPayloadLength, pPeer->nSeen) < 0) return XSTDERR;
    }
    if (pTest->bControls && pPeer->nSide && pPeer->nSeen == pTest->nMessages && pPeer->nControls == pTest->nMessages &&
        XWS_AppendFrame(&pPeer->tx, closeBody, sizeof(closeBody), XWS_CLOSE, XTRUE, XTRUE) != XWS_ERR_NONE)
        return feature_fail(pTest, "Send the client close code and reason");
    return XSTDOK;
}

static int feature_ws_headers(feature_peer_t *pPeer, xhttp_t *pHttp)
{
    const char *pUpgrade = XHTTP_GetHeader(pHttp, "Upgrade");
    const char *pConnection = XHTTP_GetHeader(pHttp, "Connection");
    if (!pUpgrade || strcmp(pUpgrade, "websocket") || !pConnection || strcmp(pConnection, "Upgrade") ||
        strcmp(pHttp->sVersion, "1.1") || pHttp->nContentLength)
        return feature_fail(pPeer->pTest, "The upgrade preserves HTTP version, connection and upgrade headers");
    if (pHttp->eType == XHTTP_REQUEST)
    {
        const char *pVersion = XHTTP_GetHeader(pHttp, "Sec-WebSocket-Version");
        const char *pKey = XHTTP_GetHeader(pHttp, "Sec-WebSocket-Key");
        if (pHttp->eMethod != XHTTP_GET || strcmp(pHttp->sUri, "/feature") || !pVersion || strcmp(pVersion, "13") ||
            !pKey || strlen(pKey) != 24) return feature_fail(pPeer->pTest, "WebSocket request target, version and nonce");
        if (pPeer->nSide) xstrncpy(pPeer->pTest->sUpgradeKey, sizeof(pPeer->pTest->sUpgradeKey), pKey);
        else if (strcmp(pKey, pPeer->pTest->sUpgradeKey)) return feature_fail(pPeer->pTest, "Server received the exact nonce");
    }
    else
    {
        const char *pAccept = XHTTP_GetHeader(pHttp, "Sec-WebSocket-Accept");
        char key[128];
        int nLength = snprintf(key, sizeof(key), "%s%s", pPeer->pTest->sUpgradeKey, XWS_GUID);
        uint8_t digest[XSHA1_DIGEST_SIZE];
        if (XSHA1_Compute(digest, sizeof(digest), (const uint8_t*)key, (size_t)nLength) < 0) return XSTDERR;
        size_t nSize = sizeof(digest);
        char *pExpected = XBase64_Encrypt(digest, &nSize);
        xbool_t bMatches = pHttp->eType == XHTTP_RESPONSE && pHttp->nStatusCode == 101 &&
            pAccept && pExpected && !strcmp(pAccept, pExpected);
        free(pExpected);
        if (!bMatches) return feature_fail(pPeer->pTest, "The 101 response matches the exact request nonce");
    }
    return XSTDOK;
}

static int feature_upgrade(feature_peer_t *pPeer)
{
    xhttp_t http;
    xhttp_status_t eStatus = XHTTP_ParseData(&http, pPeer->rx.pData, pPeer->rx.nUsed);
    if (eStatus != XHTTP_COMPLETE)
    {
        XHTTP_Clear(&http);
        return eStatus == XHTTP_INCOMPLETE || eStatus == XHTTP_PARSED ? XSTDNON :
            feature_fail(pPeer->pTest, "A WebSocket upgrade parses as HTTP");
    }

    const char *pHeader = XHTTP_GetHeader(&http, pPeer->nSide ? "Sec-WebSocket-Accept" : "Sec-WebSocket-Key");
    const char *pExpected = pPeer->nSide ? "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" : "dGhlIHNhbXBsZSBub25jZQ==";
    int nStatus = feature_ws_headers(pPeer, &http);
    if (pHeader == NULL || strcmp(pHeader, pExpected)) nStatus = XSTDERR;
    if (pPeer->nSide && http.nStatusCode != 101) nStatus = XSTDERR;
    if (!pPeer->nSide && (http.eMethod != XHTTP_GET || strcmp(http.sUri, "/feature"))) nStatus = XSTDERR;
    XByteBuffer_Advance(&pPeer->rx, XHTTP_GetPacketSize(&http));
    XHTTP_Clear(&http);
    if (nStatus < 0) return feature_fail(pPeer->pTest, "Both sides validate the WebSocket upgrade");

    pPeer->bUpgrade = XTRUE;
    pPeer->pTest->nHandshakes++;
    if (pPeer->nSide) return feature_requests(pPeer);
    const char reply[] = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
    return XByteBuffer_Add(&pPeer->tx, (const uint8_t*)reply, sizeof(reply) - 1) > 0 ? XSTDOK : XSTDERR;
}

static int feature_parse(feature_peer_t *pPeer)
{
    feature_t *pTest = pPeer->pTest;
    if (pTest->eType == XAPI_WS && !pPeer->bUpgrade && feature_upgrade(pPeer) <= 0)
        return pTest->nErrors ? XSTDERR : XSTDNON;

    while (pPeer->rx.nUsed)
    {
        size_t nUsed = 0;
        int nStatus = XSTDOK;
        if (pTest->eType == XAPI_HTTP)
        {
            xhttp_t http;
            xhttp_status_t eStatus = XHTTP_ParseData(&http, pPeer->rx.pData, pPeer->rx.nUsed);
            if (eStatus == XHTTP_COMPLETE)
            {
                nUsed = XHTTP_GetPacketSize(&http);
                nStatus = feature_http(pPeer, &http);
            }
            else if (eStatus != XHTTP_INCOMPLETE && eStatus != XHTTP_PARSED) nStatus = XSTDERR;
            XHTTP_Clear(&http);
        }
        else if (pTest->eType == XAPI_WS)
        {
            xws_frame_t frame;
            XWebFrame_Init(&frame);
            xws_status_t eStatus = XWebFrame_ParseData(&frame, pPeer->rx.pData, pPeer->rx.nUsed);
            if (eStatus == XWS_FRAME_COMPLETE)
            {
                nUsed = XWebFrame_GetFrameLength(&frame);
                nStatus = feature_ws(pPeer, &frame);
            }
            else if (eStatus != XWS_FRAME_INCOMPLETE && eStatus != XWS_FRAME_PARSED) nStatus = XSTDERR;
            XWebFrame_Clear(&frame);
        }
        else if (pTest->eType == XAPI_MDTP)
        {
            xpacket_t packet;
            xpacket_status_t eStatus = XPacket_Parse(&packet, pPeer->rx.pData, pPeer->rx.nUsed);
            if (eStatus == XPACKET_COMPLETE)
            {
                nUsed = XPacket_GetSize(&packet);
                nStatus = feature_mdtp(pPeer, &packet);
            }
            else if (eStatus != XPACKET_INCOMPLETE && eStatus != XPACKET_PARSED) nStatus = XSTDERR;
            XPacket_Clear(&packet);
        }
        else
        {
            uint32_t header[2];
            if (pPeer->rx.nUsed < sizeof(header)) break;
            memcpy(header, pPeer->rx.pData, sizeof(header));
            size_t nLength = ntohl(header[1]);
            if (nLength > 65537) return feature_fail(pTest, "Raw stream length prefix");
            if (pPeer->rx.nUsed < sizeof(header) + nLength) break;
            nUsed = sizeof(header) + nLength;
            pPeer->nDelivered++;
            nStatus = feature_verify(pPeer, pPeer->rx.pData + sizeof(header), nLength, ntohl(header[0]));
        }

        if (nStatus < 0) return feature_fail(pTest, "A complete message is valid at both ends");
        if (!nUsed) break;
        XByteBuffer_Advance(&pPeer->rx, nUsed);
    }
    return XSTDOK;
}

static int feature_api_flush(feature_peer_t *pPeer)
{
    if (!pPeer->tx.nUsed) return XAPI_CONTINUE;
    size_t nSize = XSTD_MIN(pPeer->tx.nUsed, pPeer->pTest->nChunk);
    if (XByteBuffer_Add(&pPeer->pSession->txBuffer, pPeer->tx.pData, nSize) <= 0)
        return feature_fail(pPeer->pTest, "Queue complete API output");
    XByteBuffer_Advance(&pPeer->tx, nSize);
    return XAPI_EnableEvent(pPeer->pSession, XPOLLOUT);
}

static int feature_api_cb(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    feature_peer_t *pPeer = (feature_peer_t*)pCtx->pApi->pUserCtx;
    if (pSession && pSession->pSessionData) pPeer = pSession->pSessionData;
    if (pCtx->eCbType == XAPI_CB_ACCEPTED)
    {
        while (pPeer->pSession && pPeer->pTest->pNext) pPeer = &pPeer->pTest->pNext->peers[0];
        if (pPeer->pSession) return feature_fail(pPeer->pTest, "Every accepted connection has its own state");
    }
    feature_t *pTest = pPeer->pTest;
    if (pCtx->eCbType == XAPI_CB_ERROR)
    {
        if (pCtx->eStatType == XAPI_WS &&
            ((pTest->bRejectUpgrade && pPeer->nSide && pCtx->nStatus == XWS_INVALID_RESPONSE) ||
             (pTest->bRejectRequest && !pPeer->nSide && pCtx->nStatus == XWS_INVALID_REQUEST)))
        {
            pTest->nUpgradeRejected++;
            return XAPI_CONTINUE;
        }
        return feature_fail(pTest, XAPI_GetStatus(pCtx));
    }
    if (pCtx->eCbType == XAPI_CB_REGISTERED || pCtx->eCbType == XAPI_CB_LISTENING)
    {
        if (pSession->eRole == XAPI_SERVER) pTest->pListener = pSession;
        else pPeer->pSession = pSession;
    }
    if (pCtx->eCbType == XAPI_CB_CLOSED)
    {
        if (pTest->pListener == pSession) pTest->pListener = NULL;
        if (pPeer->pSession == pSession) { pPeer->pSession = NULL; pPeer->nClosed++; }
        return XAPI_CONTINUE;
    }
    if (pCtx->eCbType == XAPI_CB_ACCEPTED || pCtx->eCbType == XAPI_CB_CONNECTED)
    {
        pPeer->pSession = pSession;
        pSession->pSessionData = pPeer;
        if (!pPeer->nSide) pTest->nAccepted++;
        if (pSession->sock.nDomain == AF_INET) XSock_NoDelay(&pSession->sock, XTRUE);
        if (pPeer->nSide && pTest->eType != XAPI_WS && feature_requests(pPeer) < 0) return XAPI_DISCONNECT;
        if (XAPI_SetEvents(pSession, XPOLLIN | (pPeer->tx.nUsed || pTest->eType == XAPI_WS ? XPOLLOUT : 0)) < 0)
            return feature_fail(pTest, "Register API peer events");
    }
    if (pCtx->eCbType == XAPI_CB_HANDSHAKE_REQUEST)
    {
        if (pTest->bRejectRequest && !pPeer->nSide) { pTest->nUnexpectedUpgrade++; return XAPI_DISCONNECT; }
        if (feature_ws_headers(pPeer, pSession->pPacket) < 0) return XAPI_DISCONNECT;
        if (!pPeer->nSide) pTest->nHandshakes++;
        if (pTest->bRejectRequest && pPeer->nSide)
        {
            xhttp_t *pHttp = pSession->pPacket;
            pHttp->eMethod = pTest->eUpgradeMethod;
            pHttp->nAllowUpdate = XTRUE;
            if (pTest->pUpgradeConnection) XHTTP_AddHeader(pHttp, "Connection", "%s", pTest->pUpgradeConnection);
            else XMap_Remove(&pHttp->headerMap, "Connection");
            pHttp->nComplete = XFALSE;
            if (!XHTTP_Assemble(pHttp, NULL, 0)) return feature_fail(pTest, "Assemble the deliberately invalid request");
        }
    }
    if (pCtx->eCbType == XAPI_CB_HANDSHAKE_RESPONSE)
    {
        if (pTest->bRejectUpgrade) { pTest->nUnexpectedUpgrade++; return XAPI_DISCONNECT; }
        if (feature_ws_headers(pPeer, pSession->pPacket) < 0) return XAPI_DISCONNECT;
        pTest->nHandshakes++;
        if (feature_requests(pPeer) < 0) return XAPI_DISCONNECT;
    }
    if (pCtx->eCbType == XAPI_CB_HANDSHAKE_ANSWER && pTest->bRejectUpgrade)
    {
        xhttp_t *pHttp = pSession->pPacket;
        pHttp->nStatusCode = pTest->nUpgradeStatus;
        pHttp->nAllowUpdate = XTRUE;
        if (pTest->pUpgradeConnection) XHTTP_AddHeader(pHttp, "Connection", "%s", pTest->pUpgradeConnection);
        else XMap_Remove(&pHttp->headerMap, "Connection");
        pHttp->nComplete = XFALSE;
        if (!XHTTP_Assemble(pHttp, NULL, 0)) return feature_fail(pTest, "Assemble the deliberately invalid upgrade");
    }
    if (pCtx->eCbType == XAPI_CB_READ)
    {
        int nStatus;
        if (pTest->eType == XAPI_HTTP) nStatus = feature_http(pPeer, (xhttp_t*)pSession->pPacket);
        else if (pTest->eType == XAPI_MDTP) nStatus = feature_mdtp(pPeer, (xpacket_t*)pSession->pPacket);
        else if (pTest->eType == XAPI_WS) nStatus = feature_ws(pPeer, pSession->pPacket);
        else
        {
            xbyte_buffer_t *pBuffer = (xbyte_buffer_t*)pSession->pPacket;
            nStatus = XByteBuffer_AddBuff(&pPeer->rx, pBuffer);
            if (nStatus > 0) nStatus = feature_parse(pPeer);
        }
        if (nStatus < 0) return XAPI_DISCONNECT;
    }
    return pPeer->pSession != NULL ? feature_api_flush(pPeer) : XAPI_CONTINUE;
}

static int feature_read(feature_peer_t *pPeer)
{
    uint8_t buffer[4093];
    size_t nSize = XSTD_MIN(sizeof(buffer), pPeer->pTest->nChunk);
    int nRead = XSock_Read(&pPeer->sock, buffer, nSize);
    if (nRead <= 0)
    {
        if (pPeer->sock.eStatus == XSOCK_WANT_READ || pPeer->sock.eStatus == XSOCK_WANT_WRITE) return XSTDOK;
        return feature_fail(pPeer->pTest, "The connection remains open until every response arrives");
    }
    pPeer->nRead += (size_t)nRead;
    if (XByteBuffer_Add(&pPeer->rx, buffer, (size_t)nRead) <= 0) return feature_fail(pPeer->pTest, "Buffer received bytes");
    return feature_parse(pPeer);
}

static int feature_write(feature_peer_t *pPeer)
{
    if (!pPeer->tx.nUsed) return XSTDOK;
    size_t nSize = XSTD_MIN(pPeer->tx.nUsed, pPeer->pTest->nChunk);
    int nSent = XSock_Write(&pPeer->sock, pPeer->tx.pData, nSize);
    if (nSent <= 0)
    {
        if (pPeer->sock.eStatus == XSOCK_WANT_READ || pPeer->sock.eStatus == XSOCK_WANT_WRITE) return XSTDOK;
        return feature_fail(pPeer->pTest, "Send queued bytes without losing a partial write");
    }
    pPeer->nWritten += (size_t)nSent;
    XByteBuffer_Advance(&pPeer->tx, (size_t)nSent);
    return XSTDOK;
}

static int feature_ready(feature_peer_t *pPeer)
{
    if (!pPeer->pTest->bTLS || pPeer->bTLSReady) return XSTDOK;
    XSOCKET nFD = pPeer->nSide ? XSock_SSLConnect(&pPeer->sock) : XSock_SSLAccept(&pPeer->sock);
    if (nFD == XSOCK_INVALID) return feature_fail(pPeer->pTest, "Complete the TLS handshake");
    if (pPeer->sock.eStatus == XSOCK_WANT_READ || pPeer->sock.eStatus == XSOCK_WANT_WRITE) return XSTDNON;
    pPeer->bTLSReady = XTRUE;
    return XSTDOK;
}

static int feature_accept(feature_t *pTest)
{
    feature_peer_t *pPeer = &pTest->peers[0];
    if (XSock_Accept(&pTest->listener, &pPeer->sock) == XSOCK_INVALID)
        return feature_fail(pTest, "Accept the actual loopback client");
    pTest->nAccepted++;
    XSock_NoDelay(&pPeer->sock, XTRUE);
    if (pTest->nLayer == 1)
    {
        int nEvents = pTest->bTLS ? XPOLLIO : XPOLLIN;
        pPeer->pEvent = XEvents_RegisterEvent(&pTest->events[0], pPeer, pPeer->sock.nFD, nEvents, XEVENT_TYPE_PEER);
        if (pPeer->pEvent == NULL) return feature_fail(pTest, "Register the accepted peer");
    }
    return XSTDOK;
}

static int feature_event_cb(void *pLoop, void *pData, XSOCKET nFD, xevent_cb_type_t eReason)
{
    xevents_t *pEvents = (xevents_t*)pLoop;
    feature_t *pTest = (feature_t*)pEvents->pUserSpace;
    xevent_data_t *pEvent = (xevent_data_t*)pData;
    (void)nFD;
    if (pEvent == NULL) return XEVENTS_CONTINUE;
    feature_peer_t *pPeer = (feature_peer_t*)pEvent->pContext;
    if (eReason == XEVENT_CB_CLEAR)
    {
        if (pPeer != NULL)
        {
            XSock_Close(&pPeer->sock);
            pPeer->pEvent = NULL;
            pPeer->nClosed++;
        }
        else
        {
            XSock_Close(&pTest->listener);
            pTest->pListenEvent = NULL;
        }
        return XEVENTS_CONTINUE;
    }
    if (eReason == XEVENT_CB_READ && pEvent->nType == XEVENT_TYPE_SERVER)
        return feature_accept(pTest) > 0 ? XEVENTS_ACCEPT : XEVENTS_DISCONNECT;
    if (pPeer == NULL) return XEVENTS_CONTINUE;
    if (eReason == XEVENT_CB_ERROR || eReason == XEVENT_CB_CLOSED || eReason == XEVENT_CB_HUNGED)
        return feature_fail(pTest, "Event connection stays open during the exchange");
    int nReady = feature_ready(pPeer);
    if (nReady < 0) return XEVENTS_DISCONNECT;
    if (!nReady)
    {
        int nEvents = pPeer->sock.eStatus == XSOCK_WANT_WRITE ? XPOLLOUT : XPOLLIN;
        if (XEvents_Modify(pEvents, pEvent, nEvents) != XEVENTS_SUCCESS) return XEVENTS_DISCONNECT;
        return XEVENTS_CONTINUE;
    }
    if (eReason == XEVENT_CB_READ)
    {
        do
        {
            if (feature_read(pPeer) < 0) return XEVENTS_DISCONNECT;
        } while (XSock_Pending(&pPeer->sock) > 0);
    }
    if (eReason == XEVENT_CB_WRITE && feature_write(pPeer) < 0) return XEVENTS_DISCONNECT;
    int nEvents = XPOLLIN | (pPeer->tx.nUsed ? XPOLLOUT : 0);
    if (nEvents != (int)pEvent->nEvents && XEvents_Modify(pEvents, pEvent, nEvents) != XEVENTS_SUCCESS)
        return feature_fail(pTest, "Update readiness after reading or writing");
    return XEVENTS_CONTINUE;
}

static void feature_clear(feature_t *pTest)
{
    for (int i = 0; i < 2; i++)
    {
        if (i < pTest->nInitialized)
        {
            if (pTest->nLayer == 0)
            {
                if (!pTest->api[i].bUseHashMap)
                {
                    if (pTest->peers[i].pSession) XAPI_Disconnect(pTest->peers[i].pSession);
                    if (!i && pTest->pListener) XAPI_Disconnect(pTest->pListener);
                }
                XAPI_Destroy(&pTest->api[i]);
            }
            else if (pTest->nLayer == 1)
            {
                if (!pTest->bHash)
                {
                    if (pTest->peers[i].pEvent) XEvents_Delete(&pTest->events[i], pTest->peers[i].pEvent);
                    if (!i && pTest->pListenEvent) XEvents_Delete(&pTest->events[i], pTest->pListenEvent);
                }
                XEvents_Destroy(&pTest->events[i]);
            }
        }
        XSock_Close(&pTest->peers[i].sock);
        XByteBuffer_Clear(&pTest->peers[i].rx);
        XByteBuffer_Clear(&pTest->peers[i].tx);
    }
    XSock_Close(&pTest->listener);
}

static void feature_init(feature_t *pTest)
{
    XSock_Init(&pTest->listener, 0, XSOCK_INVALID);
    for (int i = 0; i < 2; i++)
    {
        feature_peer_t *pPeer = &pTest->peers[i];
        pPeer->pTest = pTest;
        pPeer->nSide = i;
        XSock_Init(&pPeer->sock, 0, XSOCK_INVALID);
        XByteBuffer_Init(&pPeer->rx, 0, XTRUE);
        XByteBuffer_Init(&pPeer->tx, 0, XTRUE);
    }
}

static int feature_open(feature_t *pTest)
{
    feature_init(pTest);
    if (pTest->pServer) pTest->nPort = pTest->pServer->nPort;
    else
    {
        int nFD = socket(AF_INET, SOCK_STREAM, 0);
        if (nFD < 0) return feature_fail(pTest, "Create an ephemeral loopback listener");
        uint32_t nListenFlags = XSOCK_TCP_SERVER | (pTest->bTLS && pTest->nLayer != 0 ? XSOCK_SSL : 0);
        if (XSock_Init(&pTest->listener, nListenFlags, nFD) == XSOCK_ERROR) { close(nFD); return XSTDERR; }
        if (pTest->bTLS && pTest->nLayer != 0 &&
            (XSock_InitSSLServer(&pTest->listener, 0) == XSOCK_INVALID ||
            XSock_SetSSLCert(&pTest->listener, &pTest->serverCert) == XSOCK_INVALID)) return XSTDERR;
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(nFD, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(nFD, 8) < 0 ||
            XSock_NonBlock(&pTest->listener, XTRUE) == XSOCK_INVALID)
            return feature_fail(pTest, "Bind an ephemeral loopback listener");
        socklen_t nLength = sizeof(addr);
        if (getsockname(pTest->listener.nFD, (struct sockaddr*)&addr, &nLength) < 0)
            return feature_fail(pTest, "Read the bound port without releasing it");
        pTest->nPort = ntohs(addr.sin_port);
    }
    uint16_t nPort = pTest->nPort;

    for (int i = 0; i < 2; i++)
    {
        if (pTest->nLayer == 0)
        {
            if (XAPI_Init(&pTest->api[i], feature_api_cb, &pTest->peers[i]) != XSTDOK) return XSTDERR;
            pTest->api[i].bUseHashMap = pTest->bHash;
            if (XAPI_SetRxSize(&pTest->api[i], 512 * 1024) != XSTDOK) return XSTDERR;
        }
        else if (pTest->nLayer == 1 &&
            XEvents_Create(&pTest->events[i], 16, pTest, feature_event_cb, pTest->bHash) != XEVENTS_SUCCESS) return XSTDERR;
        pTest->nInitialized++;
    }

    if (pTest->nLayer == 0)
    {
        xapi_endpoint_t endpoint;
        XAPI_InitEndpoint(&endpoint);
        endpoint.eType = pTest->eType;
        endpoint.nPort = nPort;
        if (!pTest->pServer)
        {
            endpoint.eRole = XAPI_SERVER;
            endpoint.nFD = pTest->listener.nFD;
            endpoint.nEvents = XPOLLIN;
            pTest->listener.nFD = XSOCK_INVALID;
            if (XAPI_AddEvent(&pTest->api[0], &endpoint) != XSTDOK) return feature_fail(pTest, "Attach the API listener");
            pTest->api[0].events.nEventMax = 16;
            if (pTest->bTLS)
            {
                xsock_t *pSock = &pTest->pListener->sock;
                XSOCKET nListenFD = pSock->nFD;
                if (XSock_Init(pSock, pSock->nFlags | XSOCK_SSL, nListenFD) == XSOCK_ERROR)
                {
                    close(nListenFD);
                    return XSTDERR;
                }
                if (XSock_InitSSLServer(pSock, 0) == XSOCK_INVALID ||
                    XSock_SetSSLCert(pSock, &pTest->serverCert) == XSOCK_INVALID) return XSTDERR;
            }
        }
        endpoint.bTLS = pTest->bTLS;
        endpoint.certs = pTest->clientCert;
        endpoint.eRole = XAPI_CLIENT;
        endpoint.pAddr = "127.0.0.1";
        endpoint.pUri = "/feature";
        endpoint.bAsync = XTRUE;
        endpoint.nFD = XSOCK_INVALID;
        int nStatus = XAPI_Connect(&pTest->api[1], &endpoint);
        if (pTest->api[1].bHaveEvents) pTest->api[1].events.nEventMax = 16;
        return nStatus;
    }

    feature_peer_t *pClient = &pTest->peers[1];
    uint32_t nClientFlags = XSOCK_TCP_CLIENT | XSOCK_NB | (pTest->bTLS ? XSOCK_SSL | XSOCK_ASYNC : 0);
    if (XSock_Create(&pClient->sock, nClientFlags, "127.0.0.1", nPort) == XSOCK_INVALID)
        return feature_fail(pTest, "Connect the socket client");
    if (pTest->bTLS && XSock_SetSSLCert(&pClient->sock, &pTest->clientCert) == XSOCK_INVALID) return XSTDERR;
    XSock_NoDelay(&pClient->sock, XTRUE);
    if (pTest->eType == XAPI_WS)
    {
        xstrncpy(pTest->sUpgradeKey, sizeof(pTest->sUpgradeKey), "dGhlIHNhbXBsZSBub25jZQ==");
        const char upgrade[] = "GET /feature HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
        if (XByteBuffer_Add(&pClient->tx, (const uint8_t*)upgrade, sizeof(upgrade) - 1) <= 0) return XSTDERR;
    }
    else if (feature_requests(pClient) < 0) return XSTDERR;

    if (pTest->nLayer == 1)
    {
        pTest->pListenEvent = XEvents_RegisterEvent(&pTest->events[0], NULL, pTest->listener.nFD, XPOLLIN, XEVENT_TYPE_SERVER);
        if (pTest->pListenEvent == NULL)
            return feature_fail(pTest, "Register the event listener");
        pClient->pEvent = XEvents_RegisterEvent(&pTest->events[1], pClient, pClient->sock.nFD, XPOLLIO, XEVENT_TYPE_CLIENT);
        if (pClient->pEvent == NULL) return feature_fail(pTest, "Register the event client");
    }
    else if (feature_accept(pTest) < 0) return XSTDERR;
    return XSTDOK;
}

static int feature_run_case(int nLayer, xapi_type_t eType, size_t nChunk, xbool_t bHash,
    xbool_t bFragment, xbool_t bTLS, int nFault, xbool_t bControls)
{
    feature_t test = {0};
    test.nLayer = nLayer;
    test.nFault = nFault;
    test.bControls = bControls;
    test.eType = eType;
    test.nChunk = nChunk;
    test.bHash = bHash;
    test.bTLS = bTLS;
#ifdef XSOCK_USE_SSL
    tls_fixture_t identity = {0};
    if (bTLS)
    {
        int nCreated = tls_fixture_begin(&identity);
        if (nCreated != XSTDOK) { tls_fixture_end(&identity); return 1; }
        XSock_InitCert(&test.serverCert);
        XSock_InitCert(&test.clientCert);
        test.serverCert.pCertPath = identity.sCert;
        test.serverCert.pKeyPath = identity.sKey;
        test.clientCert.pCaPath = identity.sCert;
        test.clientCert.pHostName = "localhost";
        test.clientCert.nVerifyFlags = SSL_VERIFY_PEER;
    }
#else
    if (bTLS) return 77;
#endif
    test.bFragment = bFragment;
    test.nMessages = nChunk == 1 ? 7 : sizeof(g_sizes) / sizeof(*g_sizes);
    if (nFault) test.nMessages = 4;
    int nStatus = feature_open(&test);
    size_t nMessages = test.nMessages;
    uint64_t nDeadline = XTime_GetMonoMs() + 60000;
    while (nStatus > 0 && !test.nErrors && XTime_GetMonoMs() < nDeadline &&
        (test.peers[1].nSeen < nMessages || (bControls && !test.peers[1].nCloseFrames)))
    {
        for (int i = 0; i < 2 && !test.nErrors; i++)
        {
            if (nLayer == 0 && XAPI_Service(&test.api[i], 0) != XEVENTS_SUCCESS) nStatus = XSTDERR;
            else if (nLayer == 1 && XEvents_Service(&test.events[i], 0) != XEVENTS_SUCCESS) nStatus = XSTDERR;
            else if (nLayer == 2)
            {
                int nReady = feature_ready(&test.peers[i]);
                if (nReady < 0 || (nReady > 0 &&
                    (feature_write(&test.peers[i]) < 0 || feature_read(&test.peers[i]) < 0))) nStatus = XSTDERR;
            }
        }
    }
#ifdef XSOCK_USE_SSL
    if (bTLS && nStatus > 0 && test.peers[1].nSeen == nMessages)
    {
        xsock_t *pSock = nLayer == 0 ? &test.peers[1].pSession->sock : &test.peers[1].sock;
        SSL *pSSL = XSock_GetSSL(pSock);
        X509 *pCert = pSSL ? SSL_get_peer_certificate(pSSL) : NULL;
        if (pCert == NULL || SSL_get_verify_result(pSSL) != X509_V_OK)
            nStatus = feature_fail(&test, "The client authenticated the server certificate");
        X509_free(pCert);
    }
#endif
    feature_clear(&test);
#ifdef XSOCK_USE_SSL
    tls_fixture_end(&identity);
#endif
    if (nFault)
    {
        const char *pExpected = "Every request and response byte matches the independent model";
        if (nFault == FEATURE_REQUEST_ORDER || nFault == FEATURE_RESPONSE_ORDER)
            pExpected = eType == XAPI_HTTP ? "HTTP sequence header" : "Each message arrives once and in order";
        else if (nFault == FEATURE_REQUEST_TYPE || nFault == FEATURE_RESPONSE_TYPE)
        {
            if (eType == XAPI_HTTP) pExpected = "HTTP method, target and response status";
            else if (eType == XAPI_WS) pExpected = "WebSocket message type and final frame";
            else pExpected = "MDTP type, session and content type survive the exchange";
        }
        feature_peer_t *pReceiver = &test.peers[!(nFault & 1)];
        CHECK(test.nErrors && !strcmp(test.sError, pExpected), "The receiving endpoint rejected the specific corruption");
        CHECK(pReceiver->nDelivered == 3 && pReceiver->nSeen == 2, "A complete third message failed semantic validation");
        CHECK(test.nAccepted == 1, "Corruption was delivered over an actual client/server connection");
        if (nLayer != 2)
            CHECK(test.peers[0].nClosed == 1 && test.peers[1].nClosed == 1, "Both connections were released after rejection");
        return 0;
    }
    if (test.nErrors) fprintf(stderr, "layer=%d protocol=%d: %s\n", nLayer, eType, test.sError);
    CHECK(nStatus > 0 && !test.nErrors, "Both endpoints complete without network or protocol errors");
    CHECK(test.nAccepted == 1, "Exactly one real connection was accepted");
    CHECK(test.peers[0].nSeen == nMessages, "The server validated every complete request");
    CHECK(test.peers[1].nSeen == nMessages, "The client validated every complete response");
    if (eType == XAPI_WS) CHECK(test.nHandshakes == 2, "Both ends completed the WebSocket upgrade");
    if (bControls)
    {
        CHECK(test.peers[0].nControls == nMessages && test.peers[1].nControls == nMessages, "Every ping has its matching pong");
        CHECK(test.peers[0].nCloseFrames == 1 && test.peers[1].nCloseFrames == 1, "Both peers validated the closing handshake");
    }
    if (nLayer != 2)
        CHECK(test.peers[0].nClosed == 1 && test.peers[1].nClosed == 1, "Each connection was released exactly once");
    return 0;
}

static int feature_run(int nLayer, xapi_type_t eType, size_t nChunk, xbool_t bHash, xbool_t bFragment, xbool_t bTLS)
{
    return feature_run_case(nLayer, eType, nChunk, bHash, bFragment, bTLS, FEATURE_VALID, XFALSE);
}

static int feature_reject(int nFault)
{
    const xapi_type_t types[] = {XAPI_HTTP, XAPI_WS, XAPI_MDTP, XAPI_SOCK};
    for (int nLayer = 0; nLayer < 3; nLayer++)
        for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++)
        {
            if (types[i] == XAPI_WS && (nFault == FEATURE_REQUEST_ORDER || nFault == FEATURE_RESPONSE_ORDER)) continue;
            if (types[i] == XAPI_SOCK && nFault >= FEATURE_REQUEST_TYPE) continue;
            CHECK(feature_run_case(nLayer, types[i], 17, XTRUE, XFALSE, XFALSE, nFault, XFALSE) == 0,
                "Each networking layer validates the message instead of accepting transport completion");
        }
    return 0;
}

static int XTest_reject_request_body(void) { return feature_reject(FEATURE_REQUEST_BODY); }
static int XTest_reject_response_body(void) { return feature_reject(FEATURE_RESPONSE_BODY); }
static int XTest_reject_request_order(void) { return feature_reject(FEATURE_REQUEST_ORDER); }
static int XTest_reject_response_order(void) { return feature_reject(FEATURE_RESPONSE_ORDER); }
static int XTest_reject_request_type(void) { return feature_reject(FEATURE_REQUEST_TYPE); }
static int XTest_reject_response_type(void) { return feature_reject(FEATURE_RESPONSE_TYPE); }

static int XTest_ws_controls(void)
{
    for (int nLayer = 0; nLayer < 3; nLayer++)
        CHECK(feature_run_case(nLayer, XAPI_WS, 257, XTRUE, nLayer == 0, XFALSE, FEATURE_VALID, XTRUE) == 0,
            "Upgrade, binary messages, ping/pong and close complete through every networking layer");
    return 0;
}

static int feature_reject_upgrade(uint16_t nStatusCode, const char *pConnection)
{
    feature_t test = {0};
    test.eType = XAPI_WS;
    test.bHash = XTRUE;
    test.bRejectUpgrade = XTRUE;
    test.nUpgradeStatus = nStatusCode;
    test.pUpgradeConnection = pConnection;
    test.nChunk = 17;
    test.nMessages = sizeof(g_sizes) / sizeof(*g_sizes);
    int nStatus = feature_open(&test);
    uint64_t nDeadline = XTime_GetMonoMs() + 10000;
    while (nStatus > 0 && !test.nErrors && !test.peers[1].nClosed && XTime_GetMonoMs() < nDeadline)
        for (int i = 0; i < 2; i++)
            if (XAPI_Service(&test.api[i], 1) != XEVENTS_SUCCESS) nStatus = XSTDERR;
    xbool_t bDisconnected = test.peers[1].nClosed == 1;
    feature_clear(&test);
    CHECK(nStatus > 0 && !test.nErrors && test.nAccepted == 1, "The complete upgrade request reached the actual server");
    CHECK(test.nUpgradeRejected == 1 && !test.nUnexpectedUpgrade && bDisconnected,
        "XAPI rejected the response before reporting a successful handshake to the application");
    CHECK(test.nHandshakes == 1 && !test.peers[0].nSeen && !test.peers[1].nSeen, "No data flows after a refused upgrade");
    CHECK(test.peers[0].nClosed == 1 && test.peers[1].nClosed == 1, "Both upgrade connections were released exactly once");
    return 0;
}

static int XTest_ws_reject_status(void)
{
    const uint16_t statuses[] = {200, 204, 301, 400, 401, 403, 500, 503};
    for (size_t i = 0; i < sizeof(statuses) / sizeof(*statuses); i++)
        CHECK(feature_reject_upgrade(statuses[i], "Upgrade") == 0, "The accept key cannot turn an HTTP error into an upgrade");
    return 0;
}

static int XTest_ws_reject_connection(void)
{
    const char *connections[] = {NULL, "keep-alive", "notupgrade", "upgradeish", "up grade"};
    for (size_t i = 0; i < sizeof(connections) / sizeof(*connections); i++)
        CHECK(feature_reject_upgrade(101, connections[i]) == 0, "A 101 response needs the actual Connection upgrade token");
    return 0;
}

static int XTest_ws_reject_request(void)
{
    struct { xhttp_method_t eMethod; const char *pConnection; } cases[] = {
        {XHTTP_POST, "Upgrade"}, {XHTTP_PUT, "Upgrade"}, {XHTTP_DELETE, "Upgrade"},
        {XHTTP_OPTIONS, "Upgrade"}, {XHTTP_GET, NULL},
        {XHTTP_GET, "keep-alive"}, {XHTTP_GET, "notupgrade"}, {XHTTP_GET, "upgradeish"}, {XHTTP_GET, "up grade"}
    };
    for (size_t j = 0; j < sizeof(cases) / sizeof(*cases); j++)
    {
        feature_t test = {0};
        test.eType = XAPI_WS;
        test.bHash = XTRUE;
        test.bRejectRequest = XTRUE;
        test.eUpgradeMethod = cases[j].eMethod;
        test.pUpgradeConnection = cases[j].pConnection;
        test.nChunk = 17;
        test.nMessages = sizeof(g_sizes) / sizeof(*g_sizes);
        int nStatus = feature_open(&test);
        uint64_t nDeadline = XTime_GetMonoMs() + 10000;
        while (nStatus > 0 && !test.nErrors && !test.peers[0].nClosed && XTime_GetMonoMs() < nDeadline)
            for (int i = 0; i < 2; i++)
                if (XAPI_Service(&test.api[i], 1) != XEVENTS_SUCCESS) nStatus = XSTDERR;
        xbool_t bDisconnected = test.peers[0].nClosed == 1;
        feature_clear(&test);
        CHECK(nStatus > 0 && !test.nErrors && test.nAccepted == 1, "The client connects to the actual API server");
        CHECK(test.nUpgradeRejected == 1 && !test.nUnexpectedUpgrade && bDisconnected,
            "The server rejects the invalid request before its application handshake callback");
        CHECK(!test.nHandshakes && !test.peers[0].nSeen && !test.peers[1].nSeen, "A refused request cannot carry data");
        CHECK(test.peers[0].nClosed == 1 && test.peers[1].nClosed == 1, "Both request connections close exactly once");
    }
    return 0;
}

static void *feature_serve(void *pArg)
{
    feature_t *pTest = (feature_t*)pArg;
    while (!XSYNC_ATOMIC_GET(&pTest->nStop) && !XSYNC_ATOMIC_GET(&pTest->nErrors))
    {
        if (XAPI_Service(&pTest->api[0], 10) != XEVENTS_SUCCESS)
        {
            feature_fail(pTest, "Service the HTTP server thread");
            break;
        }
    }
    return NULL;
}

static int feature_http_client(size_t nWriteLimit, xbool_t bPerform)
{
#ifndef FEATURE_WRAP_SEND
    if (nWriteLimit) return 77;
#endif
    feature_t test = {0};
    test.eType = XAPI_HTTP;
    test.nChunk = 257;
    test.nMessages = sizeof(g_sizes) / sizeof(*g_sizes);
    feature_init(&test);
    char sRoot[] = "/tmp/xutils-feature-XXXXXX";
    CHECK(mkdtemp(sRoot) != NULL, "Create a private Unix listener directory");
    char sPath[108];
    snprintf(sPath, sizeof(sPath), "%s/http.sock", sRoot);
    xthread_t thread;
    xbool_t bStarted = XFALSE;
    int nStatus = XSTDERR;

    do
    {
        if (XAPI_Init(&test.api[0], feature_api_cb, &test.peers[0]) != XSTDOK) break;
        test.nInitialized = 1;
        xapi_endpoint_t endpoint;
        XAPI_InitEndpoint(&endpoint);
        endpoint.eType = XAPI_HTTP;
        endpoint.eRole = XAPI_SERVER;
        endpoint.bUnix = XTRUE;
        endpoint.pAddr = sPath;
        if (XAPI_AddEndpoint(&test.api[0], &endpoint) != XSTDOK) break;
        test.api[0].events.nEventMax = 16;
        if (XThread_Create(&thread, feature_serve, &test, 0) != XSTDOK) break;
        bStarted = XTRUE;
        xsock_t *pSock = &test.peers[1].sock;
        if (XSock_Create(pSock, XSOCK_UNIX_CLIENT, sPath, 0) == XSOCK_INVALID) break;
        XSock_TimeOutR(pSock, nWriteLimit ? 2 : 30, 0);
        XSock_TimeOutS(pSock, 30, 0);
#ifdef FEATURE_WRAP_SEND
        g_nWriteFD = pSock->nFD;
        g_nWriteLimit = nWriteLimit;
#endif

        nStatus = XSTDOK;
        for (size_t i = 0; i < test.nMessages && nStatus > 0; i++)
        {
            size_t nLength = g_sizes[i];
            uint8_t *pBody = (uint8_t*)malloc(nLength + 1);
            if (pBody == NULL) { nStatus = XSTDERR; break; }
            for (size_t j = 0; j < nLength; j++) pBody[j] = feature_byte(test.nClientID, i, j, XFALSE);
            xhttp_t request, response;
            XHTTP_InitRequest(&request, XHTTP_POST, "/feature", "1.1");
            XHTTP_Init(&response, XHTTP_DUMMY, 0);
            XHTTP_AddHeader(&request, "Connection", "keep-alive");
            XHTTP_AddHeader(&request, "X-Sequence", "%zu", i);
            XHTTP_AddHeader(&request, "X-Client-ID", "%zu", test.nClientID);
            XHTTP_AddHeader(&request, "Content-Type", "application/octet-stream");
            if (bPerform)
            {
                if (XHTTP_Perform(&request, pSock, pBody, nLength) != XHTTP_COMPLETE) nStatus = XSTDERR;
                else nStatus = feature_http(&test.peers[1], &request);
            }
            else if (XHTTP_Assemble(&request, pBody, nLength) == NULL ||
                XHTTP_Exchange(&request, &response, pSock) != XHTTP_COMPLETE) nStatus = XSTDERR;
            else nStatus = feature_http(&test.peers[1], &response);
            XHTTP_Clear(&response);
            XHTTP_Clear(&request);
            free(pBody);
        }
    } while (0);

#ifdef FEATURE_WRAP_SEND
    g_nWriteFD = -1;
    g_nWriteLimit = 0;
#endif
    XSYNC_ATOMIC_SET(&test.nStop, 1);
    if (bStarted) XThread_Join(&thread);
    feature_clear(&test);
    unlink(sPath);
    rmdir(sRoot);
    if (test.nErrors) fprintf(stderr, "%s\n", test.sError);
    CHECK(nStatus > 0 && !test.nErrors, "The HTTP client and API server finish every exchange");
    CHECK(test.nAccepted == 1 && test.peers[0].nClosed == 1, "One persistent server connection is opened and released");
    CHECK(test.peers[0].nSeen == test.nMessages, "The API server checked every request body");
    CHECK(test.peers[1].nSeen == test.nMessages, "The HTTP client checked every response body");
    return 0;
}

static int XTest_http_client(void) { return feature_http_client(0, XFALSE); }
static int XTest_http_short_exchange(void) { return feature_http_client(17, XFALSE); }
static int XTest_http_short_perform(void) { return feature_http_client(17, XTRUE); }

static int XTest_one_byte(void)
{
    const xapi_type_t types[] = {XAPI_HTTP, XAPI_WS, XAPI_MDTP, XAPI_SOCK};
    for (int nLayer = 0; nLayer < 3; nLayer++)
        for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++)
            CHECK(feature_run(nLayer, types[i], 1, XTRUE, XFALSE, XFALSE) == 0, "Every header and body survives bytewise I/O");
    return 0;
}

static int XTest_without_hash(void)
{
    const xapi_type_t types[] = {XAPI_HTTP, XAPI_WS, XAPI_MDTP, XAPI_SOCK};
    for (int nLayer = 0; nLayer < 2; nLayer++)
        for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++)
            CHECK(feature_run(nLayer, types[i], 4093, XFALSE, XFALSE, XFALSE) == 0, "Caller-owned registrations are released");
    return 0;
}

static int XTest_api_http(void) { return feature_run(0, XAPI_HTTP, 4093, XTRUE, XFALSE, XFALSE); }
static int XTest_api_ws(void) { return feature_run(0, XAPI_WS, 4093, XTRUE, XFALSE, XFALSE); }
static int XTest_api_ws_fragments(void) { return feature_run(0, XAPI_WS, 4093, XTRUE, XTRUE, XFALSE); }
static int XTest_api_mdtp(void) { return feature_run(0, XAPI_MDTP, 4093, XTRUE, XFALSE, XFALSE); }
static int XTest_api_socket(void) { return feature_run(0, XAPI_SOCK, 4093, XTRUE, XFALSE, XFALSE); }
static int XTest_event_http(void) { return feature_run(1, XAPI_HTTP, 257, XTRUE, XFALSE, XFALSE); }
static int XTest_event_ws(void) { return feature_run(1, XAPI_WS, 257, XTRUE, XFALSE, XFALSE); }
static int XTest_event_mdtp(void) { return feature_run(1, XAPI_MDTP, 257, XTRUE, XFALSE, XFALSE); }
static int XTest_event_socket(void) { return feature_run(1, XAPI_SOCK, 257, XTRUE, XFALSE, XFALSE); }
static int XTest_socket_http(void) { return feature_run(2, XAPI_HTTP, 4093, XTRUE, XFALSE, XFALSE); }
static int XTest_socket_ws(void) { return feature_run(2, XAPI_WS, 4093, XTRUE, XFALSE, XFALSE); }
static int XTest_socket_mdtp(void) { return feature_run(2, XAPI_MDTP, 4093, XTRUE, XFALSE, XFALSE); }
static int XTest_socket_stream(void) { return feature_run(2, XAPI_SOCK, 4093, XTRUE, XFALSE, XFALSE); }

static int XTest_api_tls(void)
{
    const xapi_type_t types[] = {XAPI_HTTP, XAPI_WS, XAPI_MDTP, XAPI_SOCK};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++)
    {
        int nStatus = feature_run(0, types[i], 4093, XTRUE, XFALSE, XTRUE);
        if (nStatus != 0) return nStatus;
    }
    return 0;
}

static int XTest_event_tls(void)
{
    const xapi_type_t types[] = {XAPI_HTTP, XAPI_WS, XAPI_MDTP, XAPI_SOCK};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++)
    {
        int nStatus = feature_run(1, types[i], 257, XTRUE, XFALSE, XTRUE);
        if (nStatus != 0) return nStatus;
    }
    return 0;
}

static int XTest_socket_tls(void)
{
    const xapi_type_t types[] = {XAPI_HTTP, XAPI_WS, XAPI_MDTP, XAPI_SOCK};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++)
    {
        int nStatus = feature_run(2, types[i], 257, XTRUE, XFALSE, XTRUE);
        if (nStatus != 0) return nStatus;
    }
    return 0;
}

static int feature_concurrent(xapi_type_t eType, xbool_t bTLS)
{
    feature_t tests[8] = {0};
#ifdef XSOCK_USE_SSL
    tls_fixture_t identity = {0};
    if (bTLS && tls_fixture_begin(&identity) != XSTDOK) { tls_fixture_end(&identity); return 1; }
#else
    if (bTLS) return 77;
#endif
    int nStatus = XSTDOK;
    size_t nCount = sizeof(tests) / sizeof(*tests), nOpened = 0;
    for (size_t i = 0; i < nCount; i++)
    {
        feature_t *pTest = &tests[i];
        pTest->pServer = i ? tests : NULL;
        pTest->pNext = i + 1 < nCount ? &tests[i + 1] : NULL;
        pTest->eType = eType;
        pTest->bHash = XTRUE;
        pTest->bTLS = bTLS;
        pTest->nChunk = 257 + i * 128;
        pTest->nMessages = sizeof(g_sizes) / sizeof(*g_sizes);
        pTest->nClientID = i + 1;
#ifdef XSOCK_USE_SSL
        XSock_InitCert(&pTest->serverCert);
        XSock_InitCert(&pTest->clientCert);
        pTest->serverCert.pCertPath = identity.sCert;
        pTest->serverCert.pKeyPath = identity.sKey;
        pTest->clientCert.pCaPath = identity.sCert;
        pTest->clientCert.pHostName = "localhost";
        pTest->clientCert.nVerifyFlags = SSL_VERIFY_PEER;
#endif
        nOpened++;
        nStatus = feature_open(pTest);
        if (nStatus < 0) break;
    }
    size_t nDone = 0;
    uint64_t nDeadline = XTime_GetMonoMs() + 60000;
    while (nStatus > 0 && nDone != nCount && XTime_GetMonoMs() < nDeadline)
    {
        if (XAPI_Service(&tests[0].api[0], 0) != XEVENTS_SUCCESS) nStatus = XSTDERR;
        nDone = 0;
        for (size_t i = 0; i < nOpened; i++)
        {
            if (XAPI_Service(&tests[i].api[1], 0) != XEVENTS_SUCCESS || tests[i].nErrors) nStatus = XSTDERR;
            if (tests[i].peers[1].nSeen == tests[i].nMessages) nDone++;
        }
    }
    for (size_t i = 0; i < nOpened; i++) feature_clear(&tests[i]);
#ifdef XSOCK_USE_SSL
    tls_fixture_end(&identity);
#endif
    CHECK(nStatus > 0 && nDone == nCount, "One server services eight simultaneous clients to completion");
    for (size_t i = 0; i < nCount; i++)
    {
        CHECK(tests[i].nAccepted == 1 && !tests[i].nErrors, "Each accepted connection has an independent lifetime");
        CHECK(tests[i].peers[0].nSeen == tests[i].nMessages, "Every client request reached the server once");
        CHECK(tests[i].peers[1].nSeen == tests[i].nMessages, "Every client received its complete response sequence");
        CHECK(tests[i].peers[0].nClosed == 1 && tests[i].peers[1].nClosed == 1, "All client and server sessions are released");
    }
    return 0;
}

static int XTest_concurrent(void)
{
    const xapi_type_t types[] = {XAPI_HTTP, XAPI_WS, XAPI_MDTP, XAPI_SOCK};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++)
        CHECK(feature_concurrent(types[i], XFALSE) == 0, "Each protocol supports concurrent clients");
    return 0;
}

static int XTest_concurrent_tls(void)
{
    const xapi_type_t types[] = {XAPI_HTTP, XAPI_WS, XAPI_MDTP, XAPI_SOCK};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++)
    {
        int nStatus = feature_concurrent(types[i], XTRUE);
        if (nStatus != 0) return nStatus;
    }
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(api_http),
    XTEST_CASE(api_ws),
    XTEST_CASE(api_ws_fragments),
    XTEST_CASE(ws_controls),
    XTEST_CASE(ws_reject_status),
    XTEST_CASE(ws_reject_connection),
    XTEST_CASE(ws_reject_request),
    XTEST_CASE(api_mdtp),
    XTEST_CASE(api_socket),
    XTEST_CASE(event_http),
    XTEST_CASE(event_ws),
    XTEST_CASE(event_mdtp),
    XTEST_CASE(event_socket),
    XTEST_CASE(socket_http),
    XTEST_CASE(socket_ws),
    XTEST_CASE(socket_mdtp),
    XTEST_CASE(socket_stream),
    XTEST_CASE(http_client),
    XTEST_CASE(reject_request_body),
    XTEST_CASE(reject_response_body),
    XTEST_CASE(reject_request_order),
    XTEST_CASE(reject_response_order),
    XTEST_CASE(reject_request_type),
    XTEST_CASE(reject_response_type),
    XTEST_CASE(concurrent),
    XTEST_CASE(concurrent_tls),
    XTEST_CASE(http_short_exchange),
    XTEST_CASE(http_short_perform),
    XTEST_CASE(api_tls),
    XTEST_CASE(event_tls),
    XTEST_CASE(socket_tls),
    XTEST_CASE(one_byte),
    XTEST_CASE(without_hash)
)
