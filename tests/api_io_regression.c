/* libxutils: real nonblocking streams, queued writes and callback lifetimes. */
#include "test.h"
#include "api.h"
#include "xtime.h"

typedef struct xtest_api_ {
    xapi_session_t *pSession;
    xbyte_buffer_t received;
    int nClosed;
    int nComplete;
    int nRead;
    int nErrors;
    xbool_t bDisconnect;
} xtest_api_t;

static int XTest_Callback(xapi_ctx_t *pContext, xapi_session_t *pSession)
{
    xtest_api_t *pTest = (xtest_api_t*)pContext->pApi->pUserCtx;
    if (pContext->eCbType == XAPI_CB_REGISTERED)
        pTest->pSession = pSession;
    else if (pContext->eCbType == XAPI_CB_COMPLETE)
        pTest->nComplete++;
    else if (pContext->eCbType == XAPI_CB_ERROR)
        pTest->nErrors++;
    else if (pContext->eCbType == XAPI_CB_CLOSED)
    {
        pTest->pSession = NULL;
        pTest->nClosed++;
    }
    else if (pContext->eCbType == XAPI_CB_READ)
    {
        if (pSession->eType == XAPI_WS)
        {
            xws_frame_t *pFrame = (xws_frame_t*)pSession->pPacket;
            if (pFrame == NULL ||
                XByteBuffer_Add(&pTest->received, XWebFrame_GetPayload(pFrame), XWebFrame_GetPayloadLength(pFrame)) <= 0)
                return XAPI_DISCONNECT;
        }
        else
        {
            xbyte_buffer_t *pData = (xbyte_buffer_t*)pSession->pPacket;
            if (pData == NULL || XByteBuffer_AddBuff(&pTest->received, pData) <= 0) return XAPI_DISCONNECT;
        }
        pTest->nRead++;
        if (pTest->bDisconnect) return XAPI_DISCONNECT;
    }
    return XAPI_CONTINUE;
}

static int XTest_Open(xapi_t *pApi, xtest_api_t *pTest, xsock_t *pPeer)
{
    XSOCKET pair[2];
    CHECK(XSock_CreatePair(pair) == XSTDOK, "Create a private full duplex transport");
    CHECK(XSock_Init(pPeer, XSOCK_TCP_PEER, pair[1]) != XSOCK_ERROR, "Wrap remote endpoint");
    CHECK(XSock_NonBlock(pPeer, XTRUE) != XSOCK_INVALID, "Remote endpoint must never block the test loop");
    CHECK(XAPI_Init(pApi, XTest_Callback, pTest) == XSTDOK, "Initialize API");
    XByteBuffer_Init(&pTest->received, 0, XTRUE);
    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_PEER;
    endpoint.nFD = pair[0];
    endpoint.nEvents = XPOLLIN;
    CHECK(XAPI_AddEndpoint(pApi, &endpoint) == XSTDOK && pTest->pSession, "Register stream peer");
    CHECK(XSock_NonBlock(&pTest->pSession->sock, XTRUE) != XSOCK_INVALID, "Force nonblocking local endpoint");
    return 0;
}

static int XTest_partial_io(void)
{
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create API fixture");
    enum
    {
        TEST_BYTES = 524311
    };
    uint8_t *pData = (uint8_t*)malloc(TEST_BYTES), readBuffer[4093];
    CHECK(pData != NULL, "Allocate payload larger than socket queues");
    for (size_t i = 0; i < TEST_BYTES; i++) pData[i] = (uint8_t)(i % 251);
    int nQueueSize = 4096;
    CHECK(setsockopt(test.pSession->sock.nFD, SOL_SOCKET, SO_SNDBUF, (const char*)&nQueueSize, sizeof(nQueueSize)) == 0,
        "Constrain sender queue to exercise partial writes");
    xbyte_buffer_t source;
    XByteBuffer_Init(&source, 0, XFALSE);
    XByteBuffer_SetData(&source, pData, TEST_BYTES);
    CHECK(XAPI_PutTxBuff(test.pSession, &source) > 0, "Queue the complete outbound payload");
    CHECK(XAPI_EnableEvent(test.pSession, XPOLLOUT) > 0, "Enable outbound service");
    for (int i = 0; i < 5; i++) CHECK(XAPI_Service(&api, 0) == XEVENTS_SUCCESS, "Fill the constrained sender queue");
    CHECK(test.pSession->txBuffer.nUsed > 0 && test.nComplete == 0, "Backpressure retains unsent bytes without completion");
    size_t nSent = 0, nReceived = 0;
    const char *pTimeout = getenv("XUTILS_TEST_TIMEOUT_MS");
    unsigned long nTimeout = pTimeout ? strtoul(pTimeout, NULL, 10) : 10000;
    CHECK(nTimeout > 0 && nTimeout <= 90000, "The I/O deadline must remain bounded");
    uint64_t nDeadline = XTime_GetMs() + nTimeout;
    while ((nReceived < TEST_BYTES || test.received.nUsed < TEST_BYTES) && XTime_GetMs() < nDeadline)
    {
        if (nSent < TEST_BYTES)
        {
            size_t nChunk = XSTD_MIN((size_t)331, TEST_BYTES - nSent);
            int nBytes = XSock_Write(&peer, pData + nSent, nChunk);
            if (nBytes > 0)
                nSent += (size_t)nBytes;
            else
                CHECK(peer.eStatus == XSOCK_WANT_WRITE, "Backpressure is retryable");
        }
        CHECK(XAPI_Service(&api, 0) == XEVENTS_SUCCESS, "Service simultaneous inbound and outbound traffic");
        CHECK(test.pSession && !test.nErrors, "Backpressure must not disconnect the peer");
        int nBytes = XSock_Read(&peer, readBuffer, sizeof(readBuffer));
        if (nBytes > 0)
        {
            CHECK((size_t)nBytes <= TEST_BYTES - nReceived, "Sender must not duplicate bytes");
            CHECK(memcmp(readBuffer, pData + nReceived, (size_t)nBytes) == 0, "Partial writes retain byte order");
            nReceived += (size_t)nBytes;
        }
        else
            CHECK(peer.eStatus == XSOCK_WANT_READ, "Empty nonblocking read is retryable");
    }
    CHECK(nReceived == TEST_BYTES && test.received.nUsed == TEST_BYTES, "Both streams finish within the deadline");
    CHECK(memcmp(test.received.pData, pData, TEST_BYTES) == 0, "Fragmented reads preserve every byte");
    CHECK(test.nComplete == 1 && test.pSession->txBuffer.nUsed == 0, "Report completion exactly once after the queue drains");
    CHECK(!(test.pSession->nEvents & XPOLLOUT), "Disable writable interest after completion");
    XByteBuffer_Clear(&source);
    free(pData);
    XSock_Close(&peer);
    XAPI_Destroy(&api);
    CHECK(test.nClosed == 1, "API teardown closes its peer once");
    XByteBuffer_Clear(&test.received);
    return 0;
}

static int XTest_callback_disconnect(void)
{
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create callback fixture");
    test.bDisconnect = XTRUE;
    CHECK(XSock_Write(&peer, "abc", 3) == 3, "Make peer readable");
    for (int i = 0; i < 10 && test.nClosed == 0; i++) XAPI_Service(&api, 20);
    CHECK(test.nRead == 1 && test.nClosed == 1 && !test.pSession, "Read callback disconnect cleans up exactly once");
    CHECK(XAPI_GetEventCount(&api) == 0, "Disconnect removes the registered descriptor");
    XAPI_Destroy(&api);
    CHECK(test.nClosed == 1, "Destroy must not repeat a prior close callback");
    XSock_Close(&peer);
    XByteBuffer_Clear(&test.received);
    return 0;
}

static int XTest_peer_eof(void)
{
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create EOF fixture");
    XSock_Close(&peer);
    for (int i = 0; i < 10 && test.nClosed == 0; i++) XAPI_Service(&api, 20);
    CHECK(test.nClosed == 1 && XAPI_GetEventCount(&api) == 0, "Peer EOF removes its session");
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);
    return 0;
}

static int XTest_websocket_fragments(void)
{
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create fragmented WebSocket fixture");
    test.pSession->eType = XAPI_WS;
    test.pSession->bHandshakeDone = XTRUE;
    xws_frame_t first, last;
    CHECK(XWebFrame_Create(&first, (const uint8_t*)"abc", 3, XWS_BINARY, XTRUE, XFALSE) == XWS_ERR_NONE,
        "Create opening fragment");
    CHECK(XWebFrame_Create(&last, (const uint8_t*)"de", 2, XWS_CONTINUATION, XTRUE, XTRUE) == XWS_ERR_NONE,
        "Create final continuation");
    for (size_t i = 0; i < first.buffer.nUsed; i++)
    {
        CHECK(XSock_Write(&peer, first.buffer.pData + i, 1) == 1, "Send a fragment one wire byte at a time");
        CHECK(XAPI_Service(&api, 20) == XEVENTS_SUCCESS && test.nRead == 0, "An unfinished message is not delivered");
    }
    CHECK(test.pSession && test.pSession->bWSFragStart, "Retain the fragmented message state");
    CHECK(XSock_Write(&peer, last.buffer.pData, last.buffer.nUsed) == (int)last.buffer.nUsed, "Send final continuation");
    for (int i = 0; i < 10 && test.nRead == 0; i++) XAPI_Service(&api, 20);
    CHECK(test.nRead == 1 && test.received.nUsed == 5 && memcmp(test.received.pData, "abcde", 5) == 0,
        "Deliver the concatenated message once, preserving all fragment bytes");
    CHECK(test.pSession && !test.pSession->bWSFragStart && !test.nErrors, "Completion resets fragmentation state");
    XWebFrame_Clear(&first);
    XWebFrame_Clear(&last);
    XSock_Close(&peer);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);
    return 0;
}

static int XTest_unexpected_continuation(void)
{
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create invalid continuation fixture");
    test.pSession->eType = XAPI_WS;
    test.pSession->bHandshakeDone = XTRUE;
    xws_frame_t frame;
    CHECK(XWebFrame_Create(&frame, (const uint8_t*)"x", 1, XWS_CONTINUATION, XTRUE, XTRUE) == XWS_ERR_NONE,
        "Create a syntactically valid continuation without an opening message");
    CHECK(XSock_Write(&peer, frame.buffer.pData, frame.buffer.nUsed) == (int)frame.buffer.nUsed, "Send unexpected continuation");
    for (int i = 0; i < 10 && !test.nClosed; i++) XAPI_Service(&api, 20);
    CHECK(test.nClosed == 1 && test.nRead == 0, "Reject invalid message sequencing before application delivery");
    XWebFrame_Clear(&frame);
    XSock_Close(&peer);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);
    return 0;
}

static int XTest_buffered_upgrade(void)
{
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a protocol selection fixture");
    test.pSession->eType = XAPI_WS;
    const char request[] = "GET / HTTP/1.1\r\nHost: local\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    CHECK(XByteBuffer_Add(&test.pSession->rxBuffer, (const uint8_t*)request, sizeof(request) - 1) > 0,
        "Supply a handshake already read by the protocol selector");
    xws_frame_t frame;
    CHECK(XWebFrame_Create(&frame, (const uint8_t*)"first", 5, XWS_BINARY, XTRUE, XTRUE) == XWS_ERR_NONE,
        "Create an optimistic first frame");
    CHECK(XByteBuffer_AddBuff(&test.pSession->rxBuffer, &frame.buffer) > 0, "Coalesce the handshake and first frame");
    CHECK(XAPI_ProcessBuffered(test.pSession) == XAPI_CONTINUE, "Process the handshake without reading the socket");
    CHECK(test.pSession->txBuffer.nUsed > 0 && test.nRead == 0, "Wait until the handshake answer is sent");
    CHECK(XAPI_Service(&api, 20) == XEVENTS_SUCCESS, "Send the answer and dispatch the already buffered frame");
    CHECK(test.nRead == 1 && test.received.nUsed == 5 && memcmp(test.received.pData, "first", 5) == 0,
        "Deliver the first frame without requiring another readable event");
    XWebFrame_Clear(&frame);
    XAPI_Destroy(&api);
    XSock_Close(&peer);
    XByteBuffer_Clear(&test.received);
    return 0;
}

static int XTest_eof_with_data(void)
{
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a final-data fixture");
    uint8_t data[8193];
    memset(data, 0x5a, sizeof(data));
    CHECK(XSock_Write(&peer, data, sizeof(data)) == sizeof(data), "Queue a final message larger than one API read");
    XSock_Close(&peer);
    for (int i = 0; i < 10 && !test.nClosed; i++) XAPI_Service(&api, 20);
    CHECK(test.received.nUsed == sizeof(data) && memcmp(test.received.pData, data, sizeof(data)) == 0,
        "Hangup must not discard unread bytes that arrived before EOF");
    CHECK(test.nClosed == 1, "Close once after consuming the final bytes");
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);
    return 0;
}

XTEST_MAIN(XTEST_CASE(partial_io), XTEST_CASE(callback_disconnect), XTEST_CASE(peer_eof), XTEST_CASE(websocket_fragments),
    XTEST_CASE(unexpected_continuation), XTEST_CASE(buffered_upgrade), XTEST_CASE(eof_with_data))
