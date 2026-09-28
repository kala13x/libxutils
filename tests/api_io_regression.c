/* libxutils: real nonblocking streams, queued writes and callback lifetimes. */
#include "test.h"
#include "api.h"
#include "xtime.h"
#include "str.h"
#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

typedef struct xtest_api_ {
    xapi_session_t *pSession;
    xbyte_buffer_t received;
    int nClosed;
    int nComplete;
    int nRead;
    int nErrors;
    int nHandshake;
    int nPing;
    int nLastError;
    xapi_type_t eErrorType;
    xws_frame_type_t eLastFrame;
    xbool_t bDisconnect;
    xbool_t bReloop;
} xtest_api_t;

static int XTest_Callback(xapi_ctx_t *pContext, xapi_session_t *pSession)
{
    xtest_api_t *pTest = (xtest_api_t*)pContext->pApi->pUserCtx;
    if (pContext->eCbType == XAPI_CB_REGISTERED)
        pTest->pSession = pSession;
    else if (pContext->eCbType == XAPI_CB_COMPLETE)
        pTest->nComplete++;
    else if (pContext->eCbType == XAPI_CB_ERROR)
    {
        pTest->nLastError = pContext->nStatus;
        pTest->eErrorType = pContext->eStatType;
        pTest->nErrors++;
    }
    else if (pContext->eCbType == XAPI_CB_HANDSHAKE_RESPONSE)
        pTest->nHandshake++;
    else if (pContext->eCbType == XAPI_CB_CLOSED)
    {
        pTest->pSession = NULL;
        pTest->nClosed++;
    }
    else if (pContext->eCbType == XAPI_CB_READ)
    {
        if (pSession->eType == XAPI_HTTP)
        {
            xhttp_t *pHttp = (xhttp_t*)pSession->pPacket;
            if (pHttp == NULL) return XAPI_DISCONNECT;
            if (pHttp->nContentLength &&
                XByteBuffer_Add(&pTest->received, XHTTP_GetBody(pHttp), pHttp->nContentLength) <= 0)
                return XAPI_DISCONNECT;
        }
        else if (pSession->eType == XAPI_WS)
        {
            xws_frame_t *pFrame = (xws_frame_t*)pSession->pPacket;
            if (pFrame == NULL ||
                XByteBuffer_Add(&pTest->received, XWebFrame_GetPayload(pFrame), XWebFrame_GetPayloadLength(pFrame)) < 0)
                return XAPI_DISCONNECT;
            if (pFrame->eType == XWS_PING) pTest->nPing++;
            pTest->eLastFrame = pFrame->eType;
        }
        else
        {
            xbyte_buffer_t *pData = (xbyte_buffer_t*)pSession->pPacket;
            if (pData == NULL || XByteBuffer_AddBuff(&pTest->received, pData) <= 0) return XAPI_DISCONNECT;
        }
        pTest->nRead++;
        if (pTest->bDisconnect) return XAPI_DISCONNECT;
        if (pTest->bReloop) return XAPI_RELOOP;
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

/* A peer ends its stream while a forked child holds every descriptor, the way
   it does between a fork and its exec. The kernel keeps a socket - and its
   event registration - alive while any copy is open, so a session closed
   before it left the event set kept being reported to the loop, pointing at
   memory the loop had already freed. */
static int XTest_peer_eof_forked(void)
{
#ifdef _WIN32
    return 77;
#else
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a forked EOF fixture");

    pid_t nChild = fork();
    CHECK(nChild >= 0, "Fork a child that holds every descriptor");
    if (nChild == 0)
    {
        pause();
        _exit(0);
    }

    /* Closing shuts the stream down for every holder, so the session sees its end. */
    XSock_Close(&peer);
    for (int i = 0; i < 10 && test.nClosed == 0; i++) XAPI_Service(&api, 20);
    int nClosed = test.nClosed;
    size_t nEvents = XAPI_GetEventCount(&api);

    /* Nothing is left to wake the loop: each wait sleeps its whole timeout. */
    int nWoken = 0;
    for (int i = 0; i < 5; i++)
    {
        uint64_t nStart = XTime_GetMonoMs();
        XAPI_Service(&api, 40);
        if (XTime_GetMonoMs() - nStart < 20) nWoken++;
    }

    kill(nChild, SIGKILL);
    waitpid(nChild, NULL, 0);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);

    CHECK(nClosed == 1 && nEvents == 0, "Peer EOF removes its session while a child holds the descriptor");
    CHECK(nWoken == 0, "A closed session leaves nothing in the event set for the child's copy to wake");
    return 0;
#endif
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

/* Feeds pData to a server session one byte at a time and returns the result of the last dispatch */
static int XTest_Trickle(xapi_session_t *pSession, const char *pData, size_t nSize)
{
    int nStatus = XAPI_CONTINUE;
    for (size_t i = 0; i < nSize && nStatus == XAPI_CONTINUE; i++)
    {
        if (XByteBuffer_Add(&pSession->rxBuffer, (const uint8_t*)pData + i, 1) <= 0) return XAPI_DISCONNECT;
        nStatus = XAPI_ProcessBuffered(pSession);
    }

    return nStatus;
}

/* A request sent a byte at a time used to be searched from its first byte on every read, and a complete header
   waiting for its body parsed whole again: the work per read grew with everything sent so far, and one such peer
   kept a worker busy. The search resumes where it stopped, and a header waiting for a body waits for all of it. */
static int XTest_handshake_trickle(void)
{
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a trickled handshake fixture");
    xapi_session_t *pSession = test.pSession;
    pSession->eType = XAPI_WS;

    const char sHead[] = "GET / HTTP/1.1\r\nHost: local\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                         "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nX-Pad: ";
    CHECK(XTest_Trickle(pSession, sHead, sizeof(sHead) - 1) == XAPI_CONTINUE, "A partial header keeps the session");

    /* Blank-looking bytes that are not the end of the header, one at a time */
    for (int i = 0; i < 20000; i++)
        CHECK(XTest_Trickle(pSession, i % 2 ? "\r" : "x", 1) == XAPI_CONTINUE, "Keep waiting for the end of the header");
    CHECK(pSession->nHeaderScan == pSession->rxBuffer.nUsed && !pSession->bHandshakeStart,
        "Every byte is looked at once, and nothing is answered early");

    /* The end of the header split across reads still completes it */
    CHECK(XTest_Trickle(pSession, "\r\n\r", 3) == XAPI_CONTINUE && !pSession->bHandshakeStart, "Three of four");
    CHECK(XTest_Trickle(pSession, "\n", 1) == XAPI_CONTINUE && pSession->bHandshakeStart, "The last byte completes it");
    CHECK(pSession->txBuffer.nUsed > 0 && pSession->rxBuffer.nUsed == 0, "The upgrade is answered and consumed");
    XSock_Close(&peer);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);

    /* A header with a body is answered once the body has arrived, and not looked at in between */
    memset(&test, 0, sizeof(test));
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a handshake with a body");
    pSession = test.pSession;
    pSession->eType = XAPI_WS;
    const char sBody[] = "GET / HTTP/1.1\r\nUpgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                         "Content-Length: 6\r\n\r\nabcde";
    CHECK(XTest_Trickle(pSession, sBody, sizeof(sBody) - 1) == XAPI_CONTINUE && !pSession->bHandshakeStart,
        "A header waiting for its body waits");
    CHECK(pSession->nHeaderWait == pSession->rxBuffer.nUsed + 1, "It waits for exactly the rest of the body");
    CHECK(XTest_Trickle(pSession, "f", 1) == XAPI_CONTINUE && pSession->bHandshakeStart, "The whole body completes it");
    XSock_Close(&peer);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);

    /* A header that can never complete is bounded like a partial one; it used to be buffered without limit */
    memset(&test, 0, sizeof(test));
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create an endless handshake body");
    api.nRxSize = 4096;
    pSession = test.pSession;
    pSession->eType = XAPI_WS;
    const char sEndless[] = "GET / HTTP/1.1\r\nUpgrade: websocket\r\nContent-Type: text/plain\r\n\r\n";
    CHECK(XTest_Trickle(pSession, sEndless, sizeof(sEndless) - 1) == XAPI_CONTINUE, "An endless body starts");
    char sFill[4096];
    memset(sFill, 'b', sizeof(sFill));
    CHECK(XTest_Trickle(pSession, sFill, sizeof(sFill)) == XAPI_DISCONNECT, "An endless body ends at the limit");
    CHECK(pSession->rxBuffer.nUsed == api.nRxSize + 1, "Nothing is buffered past the limit");
    XSock_Close(&peer);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);

    /* So is a header that never ends */
    memset(&test, 0, sizeof(test));
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create an endless handshake header");
    api.nRxSize = 4096;
    pSession = test.pSession;
    pSession->eType = XAPI_WS;
    CHECK(XTest_Trickle(pSession, sFill, sizeof(sFill)) == XAPI_CONTINUE, "A header up to the limit waits");
    CHECK(XTest_Trickle(pSession, "b", 1) == XAPI_DISCONNECT, "A header past the limit ends the session");
    XSock_Close(&peer);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);
    return 0;
}

static int XTest_websocket_burst(void)
{
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a WebSocket burst fixture");
    test.pSession->eType = XAPI_WS;
    test.pSession->bHandshakeDone = XTRUE;

    /* Masked one byte client frames, seven bytes each on the wire. Handling
       them by recursion took one stack frame per frame and overflowed the
       stack; cutting each one off the front moved the whole rest every time. */
    enum
    {
        BURST_FRAMES = 100000
    };
    xws_frame_t frame, tail;
    CHECK(XWebFrame_Create(&frame, (const uint8_t*)"z", 1, XWS_BINARY, XTRUE, XTRUE) == XWS_ERR_NONE,
        "Create the smallest masked data frame");
    CHECK(XWebFrame_Create(&tail, (const uint8_t*)"end", 3, XWS_BINARY, XTRUE, XTRUE) == XWS_ERR_NONE,
        "Create the frame that arrives split across two reads");

    xbyte_buffer_t *pRx = &test.pSession->rxBuffer;
    CHECK(XByteBuffer_Reserve(pRx, frame.buffer.nUsed * BURST_FRAMES + tail.buffer.nUsed + 1) > 0,
        "Reserve the whole burst up front");
    for (int i = 0; i < BURST_FRAMES; i++)
        CHECK(XByteBuffer_Add(pRx, frame.buffer.pData, frame.buffer.nUsed) > 0, "Queue one burst frame");
    CHECK(XByteBuffer_Add(pRx, tail.buffer.pData, 3) > 0, "Queue only the head of the last frame");

    CHECK(XAPI_ProcessBuffered(test.pSession) == XAPI_CONTINUE, "A burst of valid frames keeps the session");
    CHECK(test.nRead == BURST_FRAMES && test.received.nUsed == BURST_FRAMES, "Deliver every frame of the burst exactly once");
    CHECK(pRx->nUsed == 3 && memcmp(pRx->pData, tail.buffer.pData, 3) == 0,
        "Keep only the unfinished frame, byte for byte, after consuming the burst");

    CHECK(XByteBuffer_Add(pRx, tail.buffer.pData + 3, tail.buffer.nUsed - 3) > 0, "Complete the split frame");
    CHECK(XAPI_ProcessBuffered(test.pSession) == XAPI_CONTINUE, "The completed frame is dispatched");
    CHECK(test.nRead == BURST_FRAMES + 1 && pRx->nUsed == 0, "The split frame arrives once and nothing is left over");
    CHECK(memcmp(test.received.pData + BURST_FRAMES, "end", 3) == 0, "The split frame keeps its payload");

    XWebFrame_Clear(&frame);
    XWebFrame_Clear(&tail);
    XSock_Close(&peer);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);
    return 0;
}

static int XTest_websocket_burst_invalid(void)
{
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a burst with an invalid tail fixture");
    test.pSession->eType = XAPI_WS;
    test.pSession->bHandshakeDone = XTRUE;

    xws_frame_t frame;
    CHECK(XWebFrame_Create(&frame, (const uint8_t*)"ok", 2, XWS_BINARY, XTRUE, XTRUE) == XWS_ERR_NONE,
        "Create a valid frame");

    xbyte_buffer_t *pRx = &test.pSession->rxBuffer;
    for (int i = 0; i < 3; i++)
        CHECK(XByteBuffer_Add(pRx, frame.buffer.pData, frame.buffer.nUsed) > 0, "Queue a valid frame");

    /* Reserved bits set: no extension was negotiated, so this is a protocol error */
    const uint8_t invalid[] = { 0xF2, 0x80, 0, 0, 0, 0 };
    CHECK(XByteBuffer_Add(pRx, invalid, sizeof(invalid)) > 0, "Queue an invalid frame after them");

    CHECK(XAPI_ProcessBuffered(test.pSession) == XAPI_DISCONNECT, "An invalid frame ends the session");
    CHECK(test.nRead == 3 && test.received.nUsed == 6 && test.nErrors == 1,
        "Frames before the invalid one are delivered in order and the error is reported once");

    XWebFrame_Clear(&frame);
    XSock_Close(&peer);
    XAPI_Destroy(&api);
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

static int XTest_read_chunk(void)
{
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer, second;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a large read fixture");
    xapi_session_t *pFirst = test.pSession;

    /* One wakeup takes everything the socket holds, not XAPI_RX_SIZE of it */
    enum
    {
        TEST_BYTES = 65536
    };
    uint8_t *pData = (uint8_t*)malloc(TEST_BYTES);
    CHECK(pData != NULL, "Allocate the payload");
    for (size_t i = 0; i < TEST_BYTES; i++) pData[i] = (uint8_t)(i % 253);
    CHECK(XSock_Write(&peer, pData, TEST_BYTES) == TEST_BYTES, "Queue the whole payload at once");
    CHECK(XAPI_Service(&api, 1000) == XEVENTS_SUCCESS, "Service one wakeup");
    CHECK(test.nRead == 1 && test.received.nUsed == TEST_BYTES, "A single read delivers the whole payload");
    CHECK(memcmp(test.received.pData, pData, TEST_BYTES) == 0, "The payload arrives byte for byte");

    /* A second session reads through the same buffer: each one gets its own bytes and nothing else */
    XSOCKET pair[2];
    CHECK(XSock_CreatePair(pair) == XSTDOK, "Create a second transport");
    CHECK(XSock_Init(&second, XSOCK_TCP_PEER, pair[1]) != XSOCK_ERROR, "Wrap the second remote endpoint");
    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_PEER;
    endpoint.nFD = pair[0];
    endpoint.nEvents = XPOLLIN;
    CHECK(XAPI_AddEndpoint(&api, &endpoint) == XSTDOK && test.pSession != pFirst, "Register the second peer");
    xapi_session_t *pSecond = test.pSession;
    CHECK(XSock_NonBlock(&pSecond->sock, XTRUE) != XSOCK_INVALID, "Force the second local endpoint nonblocking");

    XByteBuffer_Clear(&test.received);
    test.nRead = 0;
    CHECK(XSock_Write(&peer, pData, TEST_BYTES) == TEST_BYTES, "The first peer sends a large payload");
    CHECK(XSock_Write(&second, (const uint8_t*)"tail", 4) == 4, "The second peer sends a few bytes");
    for (int i = 0; i < 10 && test.nRead < 2; i++) XAPI_Service(&api, 100);
    CHECK(test.nRead == 2 && test.received.nUsed == TEST_BYTES + 4, "Each session reports exactly what its peer sent");
    xbool_t bLargeFirst = memcmp(test.received.pData, pData, TEST_BYTES) == 0;
    const uint8_t *pTail = bLargeFirst ? test.received.pData + TEST_BYTES : test.received.pData;
    const uint8_t *pLarge = bLargeFirst ? test.received.pData : test.received.pData + 4;
    CHECK(memcmp(pTail, "tail", 4) == 0 && memcmp(pLarge, pData, TEST_BYTES) == 0, "No bytes cross between sessions");

    /* A peer that trickles the start of a large frame holds what it sent, not a read's worth */
    pSecond->eType = XAPI_WS;
    pSecond->bHandshakeDone = XTRUE;
    const uint8_t head[] = { 0x82, 0xFF, 0, 0, 0, 0, 0, 0x01, 0x86, 0xA0, 1, 2, 3, 4, 'a', 'b', 'c', 'd', 'e', 'f' };
    CHECK(XSock_Write(&second, head, sizeof(head)) == sizeof(head), "Send the head of a 100000 byte frame");
    for (int i = 0; i < 10 && pSecond->rxBuffer.nUsed < sizeof(head); i++) XAPI_Service(&api, 100);
    CHECK(test.nRead == 2 && pSecond->rxBuffer.nUsed == sizeof(head), "The unfinished frame waits in the session");
    CHECK(pSecond->rxBuffer.nSize < 4096, "The session buffer holds only the bytes that arrived");

    free(pData);
    XSock_Close(&peer);
    XSock_Close(&second);
    XAPI_Destroy(&api);
    CHECK(test.nClosed == 2, "Teardown closes both peers");
    XByteBuffer_Clear(&test.received);
    return 0;
}

/* A WebSocket client session as it is once its upgrade request with the key of
   RFC 6455's example is on the wire, so the answer can be fed to it directly */
#define XTEST_WS_ACCEPT "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="

static void XTest_ClientSession(xapi_session_t *pSession)
{
    pSession->eType = XAPI_WS;
    pSession->eRole = XAPI_CLIENT;
    pSession->bHandshakeStart = XTRUE;
    xstrncpy(pSession->sKey, sizeof(pSession->sKey), "dGhlIHNhbXBsZSBub25jZQ==");
}

static int XTest_client_handshake(void)
{
    /* The accept key must be exactly the one derived from the key that was
       sent: anything around it, or a near miss, is a different key. Optional
       whitespace around a header value is not part of the value. */
    struct { const char *pAccept; xbool_t bAccepted; } cases[] = {
        { XTEST_WS_ACCEPT, XTRUE },
        { XTEST_WS_ACCEPT "  ", XTRUE },
        { "\t" XTEST_WS_ACCEPT "\t", XTRUE },
        { XTEST_WS_ACCEPT "x", XFALSE },
        { XTEST_WS_ACCEPT XTEST_WS_ACCEPT, XFALSE },
        { XTEST_WS_ACCEPT " junk", XFALSE },
        { "s3pPLMBiTxaQ9kYGzzhZRbK+xOo", XFALSE },
        { "S3pPLMBiTxaQ9kYGzzhZRbK+xOo=", XFALSE },
        { "x" XTEST_WS_ACCEPT, XFALSE }
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    {
        xtest_api_t test = {0};
        xapi_t api;
        xsock_t peer;
        CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a client handshake fixture");
        xapi_session_t *pSession = test.pSession;
        XTest_ClientSession(pSession);

        char sResponse[512];
        int nLength = snprintf(sResponse, sizeof(sResponse), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", cases[i].pAccept);
        CHECK(nLength > 0 && (size_t)nLength < sizeof(sResponse), "Build the upgrade response");

        /* All but the last byte, one at a time: nothing is decided early and each byte is looked at once */
        CHECK(XTest_Trickle(pSession, sResponse, (size_t)nLength - 1) == XAPI_CONTINUE, "A partial response waits");
        CHECK(!pSession->bHandshakeDone && test.nHandshake == 0, "Nothing is decided on a partial response");
        CHECK(pSession->nHeaderScan == pSession->rxBuffer.nUsed, "Every byte of the partial response is looked at once");

        int nStatus = XTest_Trickle(pSession, sResponse + nLength - 1, 1);
        if (cases[i].bAccepted)
        {
            CHECK(nStatus == XAPI_CONTINUE && pSession->bHandshakeDone, "The exact accept key completes the handshake");
            CHECK(test.nHandshake == 1 && pSession->rxBuffer.nUsed == 0, "The response is reported once and consumed");
        }
        else
        {
            CHECK(nStatus == XAPI_DISCONNECT && !pSession->bHandshakeDone, "Any other accept key ends the session");
            CHECK(test.nHandshake == 0 && test.nErrors > 0, "The refusal is reported as an error");
        }

        XSock_Close(&peer);
        XAPI_Destroy(&api);
        XByteBuffer_Clear(&test.received);
    }

    return 0;
}

static int XTest_client_handshake_edges(void)
{
    const char sHead[] = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n";

    /* The upgrade response and the first frame can arrive in one read */
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a combined response fixture");
    xapi_session_t *pSession = test.pSession;
    XTest_ClientSession(pSession);

    xbyte_buffer_t wire;
    XByteBuffer_Init(&wire, 0, XTRUE);
    CHECK(XByteBuffer_AddFmt(&wire, "%sSec-WebSocket-Accept: %s\r\n\r\n", sHead, XTEST_WS_ACCEPT) > 0, "Build the response");
    CHECK(XWS_AppendFrame(&wire, (const uint8_t*)"hello", 5, XWS_BINARY, XFALSE, XTRUE) == XWS_ERR_NONE, "Add a frame");
    CHECK(XByteBuffer_AddBuff(&pSession->rxBuffer, &wire) > 0, "Deliver both in one read");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && pSession->bHandshakeDone, "The handshake completes");
    CHECK(test.nRead == 1 && test.received.nUsed == 5 && !memcmp(test.received.pData, "hello", 5),
        "And the frame behind it is delivered");
    XByteBuffer_Clear(&wire);
    XSock_Close(&peer);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);

    /* A response header that waits for an endless body is bounded like a partial one */
    memset(&test, 0, sizeof(test));
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create an endless response fixture");
    api.nRxSize = 4096;
    pSession = test.pSession;
    XTest_ClientSession(pSession);

    char sEndless[512];
    int nLength = snprintf(sEndless, sizeof(sEndless), "%sSec-WebSocket-Accept: %s\r\nContent-Type: text/plain\r\n\r\n",
        sHead, XTEST_WS_ACCEPT);
    CHECK(XTest_Trickle(pSession, sEndless, (size_t)nLength) == XAPI_CONTINUE, "A response with an endless body starts");
    CHECK(!pSession->bHandshakeDone && pSession->nHeaderWait == SIZE_MAX, "It can never complete, and is not parsed again");

    char sFill[4096];
    memset(sFill, 'b', sizeof(sFill));
    CHECK(XTest_Trickle(pSession, sFill, sizeof(sFill)) == XAPI_DISCONNECT, "The endless body ends at the size limit");
    CHECK(pSession->rxBuffer.nUsed == api.nRxSize + 1, "Nothing is buffered past the limit");
    XSock_Close(&peer);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);

    /* So is a response header that never ends */
    memset(&test, 0, sizeof(test));
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create an endless header fixture");
    api.nRxSize = 4096;
    pSession = test.pSession;
    XTest_ClientSession(pSession);
    CHECK(XTest_Trickle(pSession, sFill, sizeof(sFill)) == XAPI_CONTINUE, "A header up to the limit waits");
    CHECK(XTest_Trickle(pSession, "b", 1) == XAPI_DISCONNECT, "A header past the limit ends the session");
    XSock_Close(&peer);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);

    /* A response without an accept key is taken only where that is allowed */
    for (int nAllow = 0; nAllow < 2; nAllow++)
    {
        memset(&test, 0, sizeof(test));
        CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a keyless response fixture");
        api.bAllowMissingKey = nAllow ? XTRUE : XFALSE;
        pSession = test.pSession;
        XTest_ClientSession(pSession);

        char sKeyless[256];
        nLength = snprintf(sKeyless, sizeof(sKeyless), "%s\r\n", sHead);
        int nStatus = XTest_Trickle(pSession, sKeyless, (size_t)nLength);
        CHECK(nAllow ? (nStatus == XAPI_CONTINUE && pSession->bHandshakeDone) : nStatus == XAPI_DISCONNECT,
            "A missing accept key is refused unless it is allowed");
        XSock_Close(&peer);
        XAPI_Destroy(&api);
        XByteBuffer_Clear(&test.received);
    }

    /* A response that is not an upgrade, or not HTTP at all, ends the session */
    const char *pRefused[] = {
        "HTTP/1.1 200 OK\r\nSec-WebSocket-Accept: " XTEST_WS_ACCEPT "\r\n\r\n",
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocketish\r\nSec-WebSocket-Accept: " XTEST_WS_ACCEPT "\r\n\r\n",
        "NOT HTTP AT ALL\r\n\r\n"
    };

    for (size_t i = 0; i < sizeof(pRefused) / sizeof(*pRefused); i++)
    {
        memset(&test, 0, sizeof(test));
        CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a refused response fixture");
        pSession = test.pSession;
        XTest_ClientSession(pSession);
        CHECK(XTest_Trickle(pSession, pRefused[i], strlen(pRefused[i])) == XAPI_DISCONNECT,
            "A response that is not a WebSocket upgrade ends the session");
        CHECK(!pSession->bHandshakeDone && test.nHandshake == 0, "And completes no handshake");
        XSock_Close(&peer);
        XAPI_Destroy(&api);
        XByteBuffer_Clear(&test.received);
    }

    return 0;
}

static int XTest_http_trickle(void)
{
    /* A plain HTTP request sent a byte at a time is looked at once per byte,
       a header waiting for its body is not parsed again until all of it can
       be there, and pipelined requests behind it are still separated. */
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a trickled HTTP fixture");
    xapi_session_t *pSession = test.pSession;
    pSession->eType = XAPI_HTTP;

    const char sHeader[] = "POST /upload HTTP/1.1\r\nHost: local\r\nContent-Length: 11\r\n\r\n";
    CHECK(XTest_Trickle(pSession, sHeader, sizeof(sHeader) - 2) == XAPI_CONTINUE, "A partial header waits");
    CHECK(pSession->nHeaderScan == pSession->rxBuffer.nUsed && test.nRead == 0, "Each byte is looked at once");
    CHECK(XTest_Trickle(pSession, "\n", 1) == XAPI_CONTINUE && test.nRead == 0, "A header waiting for its body waits");
    CHECK(pSession->nHeaderWait == pSession->rxBuffer.nUsed + 11, "It waits for exactly the rest of the body");

    CHECK(XTest_Trickle(pSession, "hello worl", 10) == XAPI_CONTINUE && test.nRead == 0, "The body arrives");
    CHECK(XTest_Trickle(pSession, "d", 1) == XAPI_CONTINUE && test.nRead == 1, "The last byte completes the request");
    CHECK(test.received.nUsed == 11 && !memcmp(test.received.pData, "hello world", 11), "The body is delivered whole");
    CHECK(pSession->rxBuffer.nUsed == 0 && !pSession->nHeaderWait && !pSession->nHeaderScan, "Nothing is left waiting");

    /* Two whole requests and the start of a third in one read */
    const char sPipeline[] = "GET /a HTTP/1.1\r\n\r\nPOST /b HTTP/1.1\r\nContent-Length: 2\r\n\r\nokGET /c HTTP/1.1\r\n";
    CHECK(XByteBuffer_Add(&pSession->rxBuffer, (const uint8_t*)sPipeline, sizeof(sPipeline) - 1) > 0, "Deliver them");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && test.nRead == 3, "Both whole requests are handled");
    CHECK(pSession->rxBuffer.nUsed == strlen("GET /c HTTP/1.1\r\n"), "The partial third one stays buffered");
    CHECK(pSession->nHeaderScan == pSession->rxBuffer.nUsed, "And is not looked at again until more arrives");
    CHECK(XTest_Trickle(pSession, "\r\n", 2) == XAPI_CONTINUE && test.nRead == 4, "The end of its header completes it");
    CHECK(pSession->rxBuffer.nUsed == 0, "Nothing is left over");

    /* A body larger than the receive size is still taken whole */
    api.nRxSize = 1024;
    char sBig[64];
    int nLength = snprintf(sBig, sizeof(sBig), "PUT /big HTTP/1.1\r\nContent-Length: %d\r\n\r\n", 4096);
    CHECK(XTest_Trickle(pSession, sBig, (size_t)nLength) == XAPI_CONTINUE, "A large body starts");

    char sBody[4096];
    memset(sBody, 'z', sizeof(sBody));
    CHECK(XTest_Trickle(pSession, sBody, sizeof(sBody) - 1) == XAPI_CONTINUE && test.nRead == 4, "It is waited for");
    CHECK(XTest_Trickle(pSession, sBody, 1) == XAPI_CONTINUE && test.nRead == 5, "And taken whole once it is all there");

    /* A header that never ends is still bounded by the receive size */
    CHECK(XTest_Trickle(pSession, sBody, api.nRxSize) == XAPI_CONTINUE, "A header up to the limit waits");
    CHECK(XTest_Trickle(pSession, sBody, 1) == XAPI_DISCONNECT, "A header past the limit ends the session");

    XSock_Close(&peer);
    XAPI_Destroy(&api);
    XByteBuffer_Clear(&test.received);
    return 0;
}

/* A WebSocket peer that completed its handshake, for frames fed straight to its receive buffer */
static int XTest_OpenWS(xapi_t *pApi, xtest_api_t *pTest, xsock_t *pPeer)
{
    memset(pTest, 0, sizeof(*pTest));
    CHECK(XTest_Open(pApi, pTest, pPeer) == 0, "Create a WebSocket fixture");
    pTest->pSession->eType = XAPI_WS;
    pTest->pSession->bHandshakeDone = XTRUE;
    return 0;
}

static void XTest_Close(xapi_t *pApi, xtest_api_t *pTest, xsock_t *pPeer)
{
    XSock_Close(pPeer);
    XAPI_Destroy(pApi);
    XByteBuffer_Clear(&pTest->received);
}

static int XTest_websocket_control(void)
{
    /* A control frame can arrive between the fragments of a message. It is delivered at once, on
       its own, and the message around it is delivered whole once its last fragment arrives. */
    xtest_api_t test;
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_OpenWS(&api, &test, &peer) == 0, "Create a control frame fixture");
    xapi_session_t *pSession = test.pSession;
    xbyte_buffer_t *pRx = &pSession->rxBuffer;

    CHECK(XWS_AppendFrame(pRx, (const uint8_t*)"ab", 2, XWS_TEXT, XTRUE, XFALSE) == XWS_ERR_NONE, "Open a message");
    CHECK(XWS_AppendFrame(pRx, (const uint8_t*)"p", 1, XWS_PING, XTRUE, XTRUE) == XWS_ERR_NONE, "Ping inside it");
    CHECK(XWS_AppendFrame(pRx, (const uint8_t*)"cd", 2, XWS_CONTINUATION, XTRUE, XTRUE) == XWS_ERR_NONE, "Finish it");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && pRx->nUsed == 0, "Every frame is taken");
    CHECK(test.nRead == 2 && test.nPing == 1 && test.eLastFrame == XWS_TEXT, "The ping and the message, in that order");
    CHECK(test.received.nUsed == 5 && !memcmp(test.received.pData, "pabcd", 5), "Neither one's bytes leak into the other");
    CHECK(!pSession->bWSFragStart && pSession->wsBuffer.nUsed == 0 && !test.nErrors, "Nothing is left of the message");

    /* Empty fragments are fragments too, and a message of nothing but them is an empty message */
    CHECK(XWS_AppendFrame(pRx, NULL, 0, XWS_BINARY, XTRUE, XFALSE) == XWS_ERR_NONE, "Open an empty message");
    CHECK(XWS_AppendFrame(pRx, NULL, 0, XWS_CONTINUATION, XTRUE, XFALSE) == XWS_ERR_NONE, "Continue it with nothing");
    CHECK(XWS_AppendFrame(pRx, NULL, 0, XWS_CONTINUATION, XTRUE, XTRUE) == XWS_ERR_NONE, "End it with nothing");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && test.nRead == 3, "The empty message is delivered once");
    CHECK(test.eLastFrame == XWS_BINARY && test.received.nUsed == 5 && !test.nErrors, "As what it was opened as");

    CHECK(XWS_AppendFrame(pRx, NULL, 0, XWS_TEXT, XTRUE, XFALSE) == XWS_ERR_NONE, "Open with an empty fragment");
    CHECK(XWS_AppendFrame(pRx, (const uint8_t*)"xyz", 3, XWS_CONTINUATION, XTRUE, XTRUE) == XWS_ERR_NONE, "End it");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && test.nRead == 4, "The message is delivered");
    CHECK(test.received.nUsed == 8 && !memcmp(test.received.pData + 5, "xyz", 3), "With the bytes of its last fragment");

    XTest_Close(&api, &test, &peer);
    return 0;
}

static int XTest_websocket_sequence_errors(void)
{
    /* Every way a fragmented message can be broken ends the session before anything of it is delivered */
    struct {
        const char *pName;
        xws_frame_type_t eFirst;
        xbool_t bFirstFin;
        xws_frame_type_t eSecond;
        xbool_t bSecondFin;
    } cases[] = {
        { "A new message inside an unfinished one", XWS_BINARY, XFALSE, XWS_BINARY, XTRUE },
        { "A new fragmented message inside an unfinished one", XWS_TEXT, XFALSE, XWS_TEXT, XFALSE },
        { "A message of another type inside an unfinished one", XWS_TEXT, XFALSE, XWS_BINARY, XTRUE },
        { "A continuation that is not final, with nothing to continue", XWS_CONTINUATION, XFALSE, XWS_BINARY, XTRUE },
        { "A final continuation, with nothing to continue", XWS_CONTINUATION, XTRUE, XWS_BINARY, XTRUE }
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    {
        xtest_api_t test;
        xapi_t api;
        xsock_t peer;
        CHECK(XTest_OpenWS(&api, &test, &peer) == 0, cases[i].pName);
        xapi_session_t *pSession = test.pSession;
        xbyte_buffer_t *pRx = &pSession->rxBuffer;

        CHECK(XWS_AppendFrame(pRx, (const uint8_t*)"one", 3, cases[i].eFirst, XTRUE, cases[i].bFirstFin) == XWS_ERR_NONE,
            "Queue the first frame");
        CHECK(XWS_AppendFrame(pRx, (const uint8_t*)"two", 3, cases[i].eSecond, XTRUE, cases[i].bSecondFin) == XWS_ERR_NONE,
            "Queue the second frame");
        CHECK(XAPI_ProcessBuffered(pSession) == XAPI_DISCONNECT, cases[i].pName);
        CHECK(test.nRead == 0 && test.nErrors == 1, "Nothing is delivered and the error is reported once");
        CHECK(test.eErrorType == XAPI_WS && test.nLastError == XWS_FRAME_INVALID, "It is reported as an invalid frame");
        CHECK(!pSession->bWSFragStart && pSession->wsBuffer.nUsed == 0, "The unfinished message is dropped");
        XTest_Close(&api, &test, &peer);
    }

    return 0;
}

static int XTest_websocket_limits(void)
{
    /* A message is bounded by the receive size however it is fragmented */
    xtest_api_t test;
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_OpenWS(&api, &test, &peer) == 0, "Create a fragmented size fixture");
    api.nRxSize = 64;
    xapi_session_t *pSession = test.pSession;
    xbyte_buffer_t *pRx = &pSession->rxBuffer;

    uint8_t payload[64];
    memset(payload, 'f', sizeof(payload));
    CHECK(XWS_AppendFrame(pRx, payload, 40, XWS_BINARY, XTRUE, XFALSE) == XWS_ERR_NONE, "Open a message");
    CHECK(XWS_AppendFrame(pRx, payload, 24, XWS_CONTINUATION, XTRUE, XFALSE) == XWS_ERR_NONE, "Grow it to the limit");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && pSession->wsBuffer.nUsed == 64, "A message up to the limit waits");
    CHECK(XWS_AppendFrame(pRx, payload, 1, XWS_CONTINUATION, XTRUE, XTRUE) == XWS_ERR_NONE, "Grow it past the limit");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_DISCONNECT && test.nRead == 0, "A message past the limit ends the session");
    CHECK(test.nLastError == XWS_FRAME_TOOBIG && !pSession->bWSFragStart, "It is reported as too big and dropped");
    XTest_Close(&api, &test, &peer);

    /* A message of exactly the limit, fragmented, is delivered */
    CHECK(XTest_OpenWS(&api, &test, &peer) == 0, "Create an exact limit fixture");
    api.nRxSize = 64;
    pSession = test.pSession;
    pRx = &pSession->rxBuffer;
    for (int i = 0; i < 4; i++)
        CHECK(XWS_AppendFrame(pRx, payload, 16, i ? XWS_CONTINUATION : XWS_TEXT, XTRUE, i == 3) == XWS_ERR_NONE,
            "Queue a quarter of the message");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && test.nRead == 1, "A message of the limit is delivered");
    CHECK(test.received.nUsed == 64 && !memcmp(test.received.pData, payload, 64), "Whole");
    XTest_Close(&api, &test, &peer);

    /* A frame that announces more than the limit is refused as soon as more than the limit is buffered */
    CHECK(XTest_OpenWS(&api, &test, &peer) == 0, "Create an oversized frame fixture");
    api.nRxSize = 64;
    pSession = test.pSession;
    pRx = &pSession->rxBuffer;
    xws_frame_t frame;
    uint8_t large[1000];
    memset(large, 'L', sizeof(large));
    CHECK(XWebFrame_Create(&frame, large, sizeof(large), XWS_BINARY, XTRUE, XTRUE) == XWS_ERR_NONE, "Create a large frame");
    CHECK(XByteBuffer_Add(pRx, frame.buffer.pData, 64) > 0, "Buffer up to the limit");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && pRx->nUsed == 64, "Its start waits");
    CHECK(XByteBuffer_Add(pRx, frame.buffer.pData + 64, 1) > 0, "Buffer one byte more");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_DISCONNECT && test.nRead == 0, "It ends the session");
    CHECK(test.eErrorType == XAPI_WS && test.nLastError == XPACKET_BIGDATA, "As data that is too big");
    XWebFrame_Clear(&frame);
    XTest_Close(&api, &test, &peer);

    /* A frame that can not be parsed ends the session */
    CHECK(XTest_OpenWS(&api, &test, &peer) == 0, "Create an unparsable frame fixture");
    const uint8_t fragmentedPing[] = { 0x09, 0x80, 0, 0, 0, 0 };
    CHECK(XByteBuffer_Add(&test.pSession->rxBuffer, fragmentedPing, sizeof(fragmentedPing)) > 0, "Buffer a fragmented ping");
    CHECK(XAPI_ProcessBuffered(test.pSession) == XAPI_DISCONNECT && test.nRead == 0, "A fragmented control frame is refused");
    CHECK(test.nLastError == XWS_FRAME_INVALID, "As an invalid frame");
    XTest_Close(&api, &test, &peer);
    return 0;
}

static int XTest_websocket_tls_hello(void)
{
    /* A TLS record where a plain upgrade request is expected is refused at once, from its first three bytes */
    const uint8_t hello[][3] = { { 0x16, 0x03, 0x01 }, { 0x16, 0x03, 0x03 }, { 0x16, 0x03, 0x04 }, { 0x16, 0x03, 0x00 } };
    for (size_t i = 0; i < sizeof(hello) / sizeof(*hello); i++)
    {
        xtest_api_t test = {0};
        xapi_t api;
        xsock_t peer;
        CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a TLS hello fixture");
        test.pSession->eType = XAPI_WS;
        CHECK(XByteBuffer_Add(&test.pSession->rxBuffer, hello[i], 2) > 0, "Buffer two bytes");
        CHECK(XAPI_ProcessBuffered(test.pSession) == XAPI_CONTINUE, "Two bytes decide nothing");
        CHECK(XByteBuffer_Add(&test.pSession->rxBuffer, hello[i] + 2, 1) > 0, "Buffer the third");
        CHECK(XAPI_ProcessBuffered(test.pSession) == XAPI_DISCONNECT, "A TLS record ends the session");
        CHECK(test.eErrorType == XAPI_WS && test.nLastError == XWS_INVALID_REQUEST && !test.nHandshake,
            "As an invalid request, before any handshake");
        XTest_Close(&api, &test, &peer);
    }

    /* Bytes that only look like the start of one are an HTTP request like any other */
    const char *pOther[] = { "\x16\x03\x05", "\x16\x02\x01", "\x17\x03\x01" };
    for (size_t i = 0; i < sizeof(pOther) / sizeof(*pOther); i++)
    {
        xtest_api_t test = {0};
        xapi_t api;
        xsock_t peer;
        CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a lookalike fixture");
        test.pSession->eType = XAPI_WS;
        CHECK(XTest_Trickle(test.pSession, pOther[i], 3) == XAPI_CONTINUE && !test.nErrors, "They wait like a partial header");
        XTest_Close(&api, &test, &peer);
    }

    return 0;
}

static int XTest_real_ip(void)
{
    /* The address a proxy reports for the peer is taken from the first header that has one, in this order */
    struct { const char *pHeaders; const char *pExpected; } cases[] = {
        { "X-Client-IP: 10.0.0.1\r\nX-Forwarded-For: 10.0.0.2\r\nX-Real-IP: 10.0.0.3\r\n", "10.0.0.1" },
        { "X-Real-IP: 10.0.0.3\r\nX-Forwarded-For: 10.0.0.2\r\n", "10.0.0.2" },
        { "X-Forwarded-For: 10.0.0.2, 10.0.0.9, 10.0.0.8\r\n", "10.0.0.2" },
        { "X-Forwarded-For: 10.0.0.2,10.0.0.9\r\n", "10.0.0.2" },
        { "X-Forwarded-For: \t 10.0.0.2 \r\n", "10.0.0.2" },
        { "X-Forwarded-For: ,10.0.0.9\r\nX-Real-IP: 10.0.0.3\r\n", "10.0.0.3" },
        { "X-Client-IP:\r\nX-Real-IP: 2001:db8::1\r\n", "2001:db8::1" },
        { "x-real-ip: 10.0.0.3\r\n", "10.0.0.3" },
        { "", "" }
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    {
        for (int nType = 0; nType < 2; nType++)
        {
            xtest_api_t test = {0};
            xapi_t api;
            xsock_t peer;
            CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a real address fixture");
            xapi_session_t *pSession = test.pSession;
            pSession->eType = nType ? XAPI_WS : XAPI_HTTP;

            char sRequest[512];
            int nLength = snprintf(sRequest, sizeof(sRequest), "GET / HTTP/1.1\r\nHost: local\r\nUpgrade: websocket\r\n"
                "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n%s\r\n", cases[i].pHeaders);
            CHECK(nLength > 0 && (size_t)nLength < sizeof(sRequest), "Build the request");
            CHECK(XByteBuffer_Add(&pSession->rxBuffer, (const uint8_t*)sRequest, (size_t)nLength) > 0, "Buffer it");
            CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && !test.nErrors, "The request is accepted");
            CHECK(nType ? pSession->bHandshakeStart : test.nRead == 1, "And handled");
            CHECK(!strcmp(pSession->sRealIP, cases[i].pExpected), "The reported address is the expected one");
            XTest_Close(&api, &test, &peer);
        }
    }

    /* An address longer than the field is cut to fit, and the first one found is kept for the session */
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a long address fixture");
    xapi_session_t *pSession = test.pSession;
    pSession->eType = XAPI_HTTP;

    char sLong[XSOCK_ADDR_MAX * 2];
    memset(sLong, '7', sizeof(sLong) - 1);
    sLong[sizeof(sLong) - 1] = '\0';
    char sRequest[XSOCK_ADDR_MAX * 4];
    int nLength = snprintf(sRequest, sizeof(sRequest), "GET / HTTP/1.1\r\nX-Real-IP: %s\r\n\r\n", sLong);
    CHECK(nLength > 0 && (size_t)nLength < sizeof(sRequest), "Build the request");
    CHECK(XByteBuffer_Add(&pSession->rxBuffer, (const uint8_t*)sRequest, (size_t)nLength) > 0, "Buffer it");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && test.nRead == 1, "The request is handled");
    CHECK(strlen(pSession->sRealIP) == sizeof(pSession->sRealIP) - 1, "The address is cut to the field");
    CHECK(!strncmp(pSession->sRealIP, sLong, sizeof(pSession->sRealIP) - 1), "From its start");

    const char sNext[] = "GET / HTTP/1.1\r\nX-Real-IP: 10.1.1.1\r\n\r\n";
    CHECK(XByteBuffer_Add(&pSession->rxBuffer, (const uint8_t*)sNext, sizeof(sNext) - 1) > 0, "Buffer another request");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && test.nRead == 2, "It is handled");
    CHECK(pSession->sRealIP[0] == '7', "The address found first stays");
    XTest_Close(&api, &test, &peer);
    return 0;
}

static int XTest_http_endless(void)
{
    /* A header without a Content-Length but with a type has a body that never ends, and is bounded like a header */
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create an endless body fixture");
    api.nRxSize = 1024;
    xapi_session_t *pSession = test.pSession;
    pSession->eType = XAPI_HTTP;

    const char sHeader[] = "POST /stream HTTP/1.1\r\nContent-Type: application/octet-stream\r\n\r\n";
    CHECK(XTest_Trickle(pSession, sHeader, sizeof(sHeader) - 1) == XAPI_CONTINUE, "The header is taken");
    CHECK(pSession->nHeaderWait == SIZE_MAX && test.nRead == 0, "It can never complete");

    char sFill[1024];
    memset(sFill, 'e', sizeof(sFill));
    size_t nRoom = api.nRxSize - pSession->rxBuffer.nUsed;
    CHECK(XTest_Trickle(pSession, sFill, nRoom) == XAPI_CONTINUE, "A body up to the limit waits");
    CHECK(XTest_Trickle(pSession, sFill, 1) == XAPI_DISCONNECT, "A body past the limit ends the session");
    CHECK(pSession->rxBuffer.nUsed == api.nRxSize + 1 && test.nRead == 0, "Nothing past the limit is buffered");
    CHECK(test.eErrorType == XAPI_HTTP && test.nLastError == XHTTP_BIGCNT, "It is reported as too big");
    XTest_Close(&api, &test, &peer);

    /* The same in one read */
    memset(&test, 0, sizeof(test));
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a one read endless body fixture");
    api.nRxSize = 1024;
    pSession = test.pSession;
    pSession->eType = XAPI_HTTP;
    CHECK(XByteBuffer_Add(&pSession->rxBuffer, (const uint8_t*)sHeader, sizeof(sHeader) - 1) > 0, "Buffer the header");
    CHECK(XByteBuffer_Add(&pSession->rxBuffer, (const uint8_t*)sFill, sizeof(sFill)) > 0, "And a body past the limit");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_DISCONNECT && test.nRead == 0, "It ends the session at once");
    CHECK(test.nLastError == XHTTP_BIGCNT, "As too big");
    XTest_Close(&api, &test, &peer);

    /* A request behind a complete one is bounded the same way */
    memset(&test, 0, sizeof(test));
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a pipelined partial fixture");
    api.nRxSize = 1024;
    pSession = test.pSession;
    pSession->eType = XAPI_HTTP;
    const char sFirst[] = "GET /first HTTP/1.1\r\n\r\nGET /second HTTP/1.1\r\nX-Pad: ";
    CHECK(XByteBuffer_Add(&pSession->rxBuffer, (const uint8_t*)sFirst, sizeof(sFirst) - 1) > 0, "Buffer the requests");
    CHECK(XByteBuffer_Add(&pSession->rxBuffer, (const uint8_t*)sFill, sizeof(sFill)) > 0, "And an endless header");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_DISCONNECT, "The endless header ends the session");
    CHECK(test.nRead == 1 && test.nLastError == XHTTP_BIGCNT, "After the complete request is handled");
    XTest_Close(&api, &test, &peer);

    /* A Content-Length body past the limit is still waited for: it is known to end */
    memset(&test, 0, sizeof(test));
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a known length fixture");
    api.nRxSize = 1024;
    pSession = test.pSession;
    pSession->eType = XAPI_HTTP;
    char sKnown[128];
    int nLength = snprintf(sKnown, sizeof(sKnown), "PUT / HTTP/1.1\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n",
        sizeof(sFill) * 2);
    CHECK(XByteBuffer_Add(&pSession->rxBuffer, (const uint8_t*)sKnown, (size_t)nLength) > 0, "Buffer the header");
    CHECK(XByteBuffer_Add(&pSession->rxBuffer, (const uint8_t*)sFill, sizeof(sFill)) > 0, "And half the body");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && test.nRead == 0, "Half the body waits");
    CHECK(XTest_Trickle(pSession, sFill, sizeof(sFill) - 1) == XAPI_CONTINUE && test.nRead == 0, "So does all but a byte");
    CHECK(XTest_Trickle(pSession, sFill, 1) == XAPI_CONTINUE && test.nRead == 1, "The last byte completes it");
    CHECK(test.received.nUsed == sizeof(sFill) * 2 && !test.nErrors, "The body is delivered whole");
    XTest_Close(&api, &test, &peer);
    return 0;
}

static int XTest_handshake_body_limit(void)
{
    /* An upgrade request that announces a body waits for it, but never past the receive size */
    const char sHeader[] = "GET / HTTP/1.1\r\nUpgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                           "Content-Length: 4096\r\n\r\n";
    char sFill[2048];
    memset(sFill, 'h', sizeof(sFill));

    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create an upgrade body fixture");
    api.nRxSize = 1024;
    test.pSession->eType = XAPI_WS;
    CHECK(XByteBuffer_Add(&test.pSession->rxBuffer, (const uint8_t*)sHeader, sizeof(sHeader) - 1) > 0, "Buffer the header");
    CHECK(XByteBuffer_Add(&test.pSession->rxBuffer, (const uint8_t*)sFill, sizeof(sFill)) > 0, "And part of its body");
    CHECK(XAPI_ProcessBuffered(test.pSession) == XAPI_DISCONNECT && !test.nHandshake, "A body past the limit is refused");
    CHECK(test.eErrorType == XAPI_HTTP && test.nLastError == XHTTP_BIGCNT, "As too big");
    XTest_Close(&api, &test, &peer);

    /* The same for the response a client waits for */
    memset(&test, 0, sizeof(test));
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a response body fixture");
    api.nRxSize = 1024;
    XTest_ClientSession(test.pSession);
    const char sResponse[] = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nSec-WebSocket-Accept: " XTEST_WS_ACCEPT
                             "\r\nContent-Length: 4096\r\n\r\n";
    CHECK(XByteBuffer_Add(&test.pSession->rxBuffer, (const uint8_t*)sResponse, sizeof(sResponse) - 1) > 0, "Buffer it");
    CHECK(XByteBuffer_Add(&test.pSession->rxBuffer, (const uint8_t*)sFill, sizeof(sFill)) > 0, "And part of its body");
    CHECK(XAPI_ProcessBuffered(test.pSession) == XAPI_DISCONNECT && !test.nHandshake, "A body past the limit is refused");
    CHECK(test.nLastError == XHTTP_BIGCNT, "As too big");
    XTest_Close(&api, &test, &peer);
    return 0;
}

static int XTest_mdtp_limit(void)
{
    /* A packet that announces more than the receive size is refused once more than that is buffered */
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create an oversized packet fixture");
    api.nRxSize = 128;
    xapi_session_t *pSession = test.pSession;
    pSession->eType = XAPI_MDTP;

    xpacket_t packet;
    uint8_t payload[512];
    memset(payload, 'm', sizeof(payload));
    CHECK(XPacket_Init(&packet, payload, sizeof(payload)) == XPACKET_ERR_NONE, "Create a large packet");
    xbyte_buffer_t *pWire = XPacket_Assemble(&packet);
    CHECK(pWire != NULL && pWire->nUsed > api.nRxSize + 1, "Assemble it");
    CHECK(XByteBuffer_Add(&pSession->rxBuffer, pWire->pData, api.nRxSize) > 0, "Buffer up to the limit");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_CONTINUE && test.nRead == 0, "Its start waits");
    CHECK(XByteBuffer_Add(&pSession->rxBuffer, pWire->pData + api.nRxSize, 1) > 0, "Buffer one byte more");
    CHECK(XAPI_ProcessBuffered(pSession) == XAPI_DISCONNECT && test.nRead == 0, "It ends the session");
    CHECK(test.eErrorType == XAPI_MDTP && test.nLastError == XPACKET_BIGDATA, "As data that is too big");
    XPacket_Clear(&packet);
    XTest_Close(&api, &test, &peer);
    return 0;
}

static int XTest_crossed_events(void)
{
    /* A read that has to wait for writability is finished by the next writable event, and a write that has to
       wait for readability by the next readable one. The events watched before are restored either way. */
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a crossed events fixture");
    xapi_session_t *pSession = test.pSession;

    CHECK(XSock_Write(&peer, (const uint8_t*)"late read", 9) == 9, "Queue data for a read");
    pSession->bReadOnWrite = XTRUE;
    CHECK(XAPI_SetEvents(pSession, XPOLLOUT) == XSTDOK, "Watch writability, as a waiting read does");
    pSession->nEvents = XPOLLIN;
    for (int i = 0; i < 10 && !test.nRead; i++) CHECK(XAPI_Service(&api, 20) == XEVENTS_SUCCESS, "Service the session");
    CHECK(test.nRead == 1 && test.received.nUsed == 9 && !memcmp(test.received.pData, "late read", 9),
        "The writable event reads");
    CHECK(!pSession->bReadOnWrite && pSession->nEvents == XPOLLIN, "And restores what was watched before");

    const char sWrite[] = "late write";
    xbyte_buffer_t out;
    XByteBuffer_Init(&out, 0, XFALSE);
    XByteBuffer_SetData(&out, (uint8_t*)sWrite, sizeof(sWrite) - 1);
    CHECK(XAPI_PutTxBuff(pSession, &out) > 0, "Queue data for a write");
    pSession->bWriteOnRead = XTRUE;
    CHECK(XSock_Write(&peer, (const uint8_t*)"x", 1) == 1, "Make the session readable");
    for (int i = 0; i < 10 && pSession->txBuffer.nUsed; i++) CHECK(XAPI_Service(&api, 20) == XEVENTS_SUCCESS, "Service");
    CHECK(!pSession->bWriteOnRead && pSession->txBuffer.nUsed == 0, "The readable event writes");

    char sRead[32] = {0};
    int nRead = 0;
    for (int i = 0; i < 50 && nRead < (int)sizeof(sWrite) - 1; i++)
    {
        int nBytes = XSock_Read(&peer, (uint8_t*)sRead + nRead, sizeof(sRead) - 1 - (size_t)nRead);
        if (nBytes > 0) nRead += nBytes;
        else XAPI_Service(&api, 10);
    }

    CHECK(nRead == (int)sizeof(sWrite) - 1 && !strcmp(sRead, sWrite), "Everything queued reaches the peer");
    XTest_Close(&api, &test, &peer);
    return 0;
}

static int XTest_reloop(void)
{
    /* A callback can ask the loop to start over, which keeps the session */
    xtest_api_t test = {0};
    xapi_t api;
    xsock_t peer;
    CHECK(XTest_Open(&api, &test, &peer) == 0, "Create a reloop fixture");
    test.bReloop = XTRUE;
    CHECK(XSock_Write(&peer, (const uint8_t*)"again", 5) == 5, "Queue data");
    for (int i = 0; i < 10 && !test.nRead; i++) XAPI_Service(&api, 20);
    CHECK(test.nRead == 1 && test.pSession != NULL && !test.nClosed, "The session is kept");
    test.bReloop = XFALSE;
    CHECK(XSock_Write(&peer, (const uint8_t*)"more", 4) == 4, "Queue more data");
    for (int i = 0; i < 10 && test.nRead < 2; i++) XAPI_Service(&api, 20);
    CHECK(test.nRead == 2 && test.received.nUsed == 9 && !memcmp(test.received.pData, "againmore", 9), "And keeps working");
    XTest_Close(&api, &test, &peer);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(partial_io),
    XTEST_CASE(callback_disconnect),
    XTEST_CASE(peer_eof),
    XTEST_CASE(peer_eof_forked),
    XTEST_CASE(websocket_fragments),
    XTEST_CASE(unexpected_continuation),
    XTEST_CASE(buffered_upgrade),
    XTEST_CASE(websocket_burst),
    XTEST_CASE(websocket_burst_invalid),
    XTEST_CASE(eof_with_data),
    XTEST_CASE(read_chunk),
    XTEST_CASE(handshake_trickle),
    XTEST_CASE(client_handshake),
    XTEST_CASE(client_handshake_edges),
    XTEST_CASE(http_trickle),
    XTEST_CASE(websocket_control),
    XTEST_CASE(websocket_sequence_errors),
    XTEST_CASE(websocket_limits),
    XTEST_CASE(websocket_tls_hello),
    XTEST_CASE(real_ip),
    XTEST_CASE(http_endless),
    XTEST_CASE(handshake_body_limit),
    XTEST_CASE(mdtp_limit),
    XTEST_CASE(crossed_events),
    XTEST_CASE(reloop)
)
