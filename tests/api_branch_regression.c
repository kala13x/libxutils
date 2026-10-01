/* Callback results must preserve session, buffer and timer ownership. */
#include "test.h"
#include "api.h"
#include "xtime.h"
#include <fcntl.h>
#include <unistd.h>

typedef struct {
    xapi_t api;
    xapi_session_t *pSession;
    xapi_cb_type_t eAction;
    int nAction;
    int nClosed;
    int nTimers;
    int nTimeouts;
    int nReads;
    int nWrites;
    int nComplete;
    int nErrors;
    int nLastError;
    int nUsers;
    xbool_t bUser;
    xbool_t bBadContext;
    xbool_t bBadHTTP;
    uint8_t received[128];
    size_t nReceived;
} branch_api_t;

static int branch_callback(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    branch_api_t *pTest = pCtx->pApi->pUserCtx;
    if (pCtx->eCbType == XAPI_CB_REGISTERED) pTest->pSession = pSession;
    if (pCtx->eCbType == XAPI_CB_CLOSED) { pTest->nClosed++; pTest->pSession = NULL; }
    if (pCtx->eCbType == XAPI_CB_ERROR) { pTest->nErrors++; pTest->nLastError = pCtx->nStatus; }
    if (pCtx->eCbType == XAPI_CB_STATUS && pCtx->eStatType == XAPI_SELF && pCtx->nStatus == XAPI_TIMER_DESTROY) pTest->nTimers++;
    if (pCtx->eCbType == XAPI_CB_TIMEOUT) pTest->nTimeouts++;
    if (pCtx->eCbType == XAPI_CB_WRITE) pTest->nWrites++;
    if (pCtx->eCbType == XAPI_CB_COMPLETE) pTest->nComplete++;
    if ((pCtx->eCbType == XAPI_CB_TICK || pCtx->eCbType == XAPI_CB_USER) && pSession) pTest->bBadContext = XTRUE;
    if (pCtx->eCbType == XAPI_CB_READ)
    {
        xbyte_buffer_t *pData = pSession->pPacket;
        if (!pData) return XAPI_DISCONNECT;
        const uint8_t *pBytes;
        size_t nLength;
        if (pSession->eType == XAPI_HTTP)
        {
            xhttp_t *pHTTP = pSession->pPacket;
            pBytes = XHTTP_GetBody(pHTTP);
            nLength = XHTTP_GetBodySize(pHTTP);
            if (pHTTP->eType != XHTTP_REQUEST || pHTTP->eMethod != XHTTP_POST || strcmp(pHTTP->sUri, "/proxy") ||
                strcmp(pHTTP->sVersion, "1.1")) pTest->bBadHTTP = XTRUE;
        }
        else
        {
            pBytes = pSession->eType == XAPI_WS ? XWebFrame_GetPayload(pSession->pPacket) : pData->pData;
            nLength = pSession->eType == XAPI_WS ? XWebFrame_GetPayloadLength(pSession->pPacket) : pData->nUsed;
        }
        if (nLength > sizeof(pTest->received) - pTest->nReceived) return XAPI_DISCONNECT;
        if (nLength) memcpy(pTest->received + pTest->nReceived, pBytes, nLength);
        pTest->nReceived += nLength;
        pTest->nReads++;
    }
    if (pCtx->eCbType == XAPI_CB_USER) { pTest->nUsers++; return pTest->nAction; }
    if (pCtx->eCbType == pTest->eAction) return pTest->bUser ? XAPI_USER_CB : pTest->nAction;
    return XAPI_CONTINUE;
}

static int branch_open(branch_api_t *pTest, int *pPeer)
{
    int pair[2];
    CHECK(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair), "Create the private callback transport");
    CHECK(XAPI_Init(&pTest->api, branch_callback, pTest) == XSTDOK, "Initialize the callback fixture");
    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_PEER;
    endpoint.nEvents = XPOLLIN;
    endpoint.nFD = pair[0];
    CHECK(XAPI_AddEvent(&pTest->api, &endpoint) == XSTDOK && pTest->pSession, "Register a callback-owned session");
    CHECK(XSock_NonBlock(&pTest->pSession->sock, XTRUE) >= 0, "Callback service never blocks on an empty stream");
    pTest->api.events.nEventMax = 8;
    *pPeer = pair[1];
    return 0;
}

static int XTest_timer_actions(void)
{
    const int actions[] = {XAPI_CONTINUE, XAPI_NO_ACTION, XAPI_RELOOP, XAPI_DISCONNECT};
    for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); i++)
        for (int nUser = 0; nUser < 2; nUser++)
        {
            branch_api_t test = {.eAction = XAPI_CB_TIMEOUT, .nAction = actions[i], .bUser = nUser};
            int nPeer;
            CHECK(!branch_open(&test, &nPeer), "Open the timer callback case");
            CHECK(XAPI_AddTimer(test.pSession, 1) == XSTDOK, "Arm the session timer");
            int nTimer = test.pSession->pTimer->nFD;
            uint64_t nDeadline = XTime_GetMonoMs() + 5000;
            while (!test.nTimeouts && XTime_GetMonoMs() < nDeadline) XAPI_Service(&test.api, 20);
            CHECK(test.nTimeouts == 1 && !test.nErrors && !test.bBadContext && test.nUsers == nUser,
                "Exactly one timeout reaches the selected direct or deferred callback");
            xbool_t bRetain = !nUser && actions[i] == XAPI_CONTINUE;
            CHECK(test.nClosed == (actions[i] == XAPI_DISCONNECT) && test.nTimers == !bRetain,
                "Timeout results retain the timer, remove only the timer, or disconnect its session");
            if (test.pSession)
            {
                CHECK((test.pSession->pTimer != NULL) == bRetain, "The surviving session retains only an owned timer");
                const uint8_t data[] = {0, 't', 0xff};
                CHECK(write(nPeer, data, sizeof(data)) == sizeof(data), "Send exact data after timeout handling");
                CHECK(XAPI_Service(&test.api, 100) == XEVENTS_SUCCESS && test.nReceived == sizeof(data) &&
                    !memcmp(test.received, data, sizeof(data)), "Timer-only removal leaves the transport usable");
            }
            XAPI_Destroy(&test.api);
            close(nPeer);
            CHECK(test.nClosed == 1 && test.nTimers == 1 && fcntl(nTimer, F_GETFD) < 0 && errno == EBADF,
                "Every timer and session is released exactly once after every callback result");
        }
    return 0;
}

static int XTest_buffered_actions(void)
{
    const int actions[] = {XAPI_CONTINUE, XAPI_RELOOP, XAPI_DISCONNECT};
    const uint8_t bytes[] = {'b', 0, 0x81, 0xff};
    for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); i++)
        for (int nUser = 0; nUser < 2; nUser++)
        {
            branch_api_t test = {.eAction = XAPI_CB_READ, .nAction = actions[i], .bUser = nUser};
            int nPeer;
            CHECK(!branch_open(&test, &nPeer), "Open the buffered callback case");
            CHECK(XAPI_ProcessBuffered(test.pSession) == XAPI_CONTINUE && !test.nReads,
                "An empty receive buffer never fabricates a read callback");
            CHECK(XByteBuffer_Add(&test.pSession->rxBuffer, bytes, sizeof(bytes)) == sizeof(bytes), "Queue exact buffered input");
            CHECK(XAPI_ProcessBuffered(test.pSession) == actions[i] && test.nReads == 1 && test.nUsers == nUser,
                "Buffered dispatch preserves continue, reloop and disconnect through direct and deferred callbacks");
            CHECK(test.nReceived == sizeof(bytes) && !memcmp(test.received, bytes, sizeof(bytes)) &&
                !test.pSession->rxBuffer.nUsed && !test.pSession->pPacket && !test.bBadContext,
                "Each callback sees the complete payload once and retains no borrowed packet pointer");
            CHECK(XByteBuffer_Add(&test.pSession->rxBuffer, bytes, sizeof(bytes)) == sizeof(bytes),
                "Queue input before cancellation");
            test.pSession->bCancel = XTRUE;
            CHECK(XAPI_ProcessBuffered(test.pSession) == XAPI_DISCONNECT && test.nReads == 1 &&
                test.pSession->rxBuffer.nUsed == sizeof(bytes), "A cancelled session never dispatches or consumes pending input");
            XAPI_Destroy(&test.api);
            close(nPeer);
            CHECK(test.nClosed == 1 && !test.nErrors, "The caller closes the buffered session exactly once");
        }
    return 0;
}

static int XTest_tick_actions(void)
{
    const int actions[] = {XAPI_CONTINUE, XAPI_NO_ACTION, XAPI_RELOOP, XAPI_DISCONNECT};
    const uint8_t bytes[] = {'t', 0, 0xff};
    for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); i++)
        for (int nUser = 0; nUser < 2; nUser++)
        {
            branch_api_t test = {.eAction = XAPI_CB_TICK, .nAction = actions[i], .bUser = nUser};
            int nPeer;
            CHECK(!branch_open(&test, &nPeer), "Open the tick callback case");
            CHECK(write(nPeer, bytes, sizeof(bytes)) == sizeof(bytes), "Send exact data before the loop tick");
            int nStatus = XAPI_Service(&test.api, 100);
            CHECK(nStatus == (actions[i] == XAPI_DISCONNECT ? XEVENTS_EBREAK : XEVENTS_SUCCESS) && test.nUsers == nUser,
                "Only a disconnect tick stops service, including a result supplied by the user callback");
            CHECK(!test.nClosed && test.pSession && !test.bBadContext && test.nReceived == sizeof(bytes) &&
                !memcmp(test.received, bytes, sizeof(bytes)), "Stopping the loop retains its live session and exact data");
            test.nAction = XAPI_CONTINUE;
            CHECK(write(nPeer, bytes, sizeof(bytes)) == sizeof(bytes) && XAPI_Service(&test.api, 100) == XEVENTS_SUCCESS &&
                test.nReceived == 2 * sizeof(bytes) && !memcmp(test.received + sizeof(bytes), bytes, sizeof(bytes)),
                "Service resumes on the same session without duplicating or losing stream bytes");
            XAPI_Destroy(&test.api);
            close(nPeer);
            CHECK(test.nClosed == 1 && !test.nErrors, "Tick results never transfer or duplicate session ownership");
        }
    return 0;
}

static int XTest_write_actions(void)
{
    const int actions[] = {XAPI_CONTINUE, XAPI_NO_ACTION, XAPI_RELOOP, XAPI_DISCONNECT};
    for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); i++)
        for (int nUser = 0; nUser < 2; nUser++)
            for (int nCustom = 0; nCustom < 2; nCustom++)
            {
                branch_api_t test = {.eAction = XAPI_CB_WRITE, .nAction = actions[i], .bUser = nUser};
                int nPeer;
                CHECK(!branch_open(&test, &nPeer), "Open the empty outbound-buffer callback case");
                if (nCustom) test.pSession->eRole = XAPI_CUSTOM;
                CHECK(XAPI_EnableEvent(test.pSession, XPOLLOUT) == XSTDOK, "Request one writable notification");
                CHECK(XAPI_Service(&test.api, 100) == XEVENTS_SUCCESS && test.nWrites == 1 && test.nUsers == nUser &&
                    !test.nComplete && !test.nReads && !test.nErrors && !test.bBadContext,
                    "An empty buffer dispatches exactly one write callback without fabricating a completed send");
                uint8_t byte;
                if (actions[i] == XAPI_DISCONNECT)
                    CHECK(test.nClosed == 1 && !test.pSession && recv(nPeer, &byte, 1, MSG_DONTWAIT) == 0,
                        "A disconnect result closes the stream without sending any bytes");
                else
                {
                    CHECK(test.pSession && !test.nClosed &&
                        !!(test.pSession->nEvents & XPOLLOUT) == (nCustom || actions[i] == XAPI_RELOOP),
                        "Normal empty writes disable writable interest; custom and reloop results retain caller control");
                    CHECK(recv(nPeer, &byte, 1, MSG_DONTWAIT) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
                        "A surviving empty write neither sends garbage nor closes the transport");
                }
                XAPI_Destroy(&test.api);
                close(nPeer);
                CHECK(test.nClosed == 1, "Every write callback result releases the session exactly once");
            }
    return 0;
}

static int XTest_complete_actions(void)
{
    const int actions[] = {XAPI_CONTINUE, XAPI_NO_ACTION, XAPI_RELOOP, XAPI_DISCONNECT};
    const uint8_t bytes[] = {0, 0xff, 'd', 'o', 'n', 'e', 0};
    for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); i++)
        for (int nUser = 0; nUser < 2; nUser++)
        {
            branch_api_t test = {.eAction = XAPI_CB_COMPLETE, .nAction = actions[i], .bUser = nUser};
            int nPeer;
            CHECK(!branch_open(&test, &nPeer), "Open the completed-write callback case");
            CHECK(XByteBuffer_Add(&test.pSession->txBuffer, bytes, sizeof(bytes)) == sizeof(bytes) &&
                XAPI_EnableEvent(test.pSession, XPOLLOUT) == XSTDOK, "Queue a binary response including embedded zero bytes");
            CHECK(XAPI_Service(&test.api, 100) == XEVENTS_SUCCESS && test.nComplete == 1 && test.nUsers == nUser &&
                !test.nWrites && !test.nErrors && !test.bBadContext, "Completion fires once after sending the queued response");
            uint8_t received[sizeof(bytes) + 1];
            CHECK(recv(nPeer, received, sizeof(received), MSG_DONTWAIT) == sizeof(bytes) &&
                !memcmp(received, bytes, sizeof(bytes)),
                "Even a disconnecting completion callback leaves the peer with the complete exact response");
            if (actions[i] == XAPI_DISCONNECT)
                CHECK(test.nClosed == 1 && !test.pSession && recv(nPeer, received, sizeof(received), MSG_DONTWAIT) == 0,
                    "The peer sees EOF only after the entire response");
            else
            {
                CHECK(test.pSession && !test.nClosed && !test.pSession->txBuffer.nUsed &&
                    !(test.pSession->nEvents & XPOLLOUT), "Completion drains the output and disables writable interest");
                CHECK(XAPI_Service(&test.api, 0) == XEVENTS_SUCCESS && test.nComplete == 1 && !test.nWrites,
                    "An idle loop does not resend the response or repeat its completion callback");
            }
            XAPI_Destroy(&test.api);
            close(nPeer);
            CHECK(test.nClosed == 1, "Completion and destruction release the session exactly once");
        }
    return 0;
}

static int XTest_ws_key_policy(void)
{
    const char *pHeaders[] = {"", "Sec-WebSocket-Key: \r\n", "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"};
    for (size_t i = 0; i < sizeof(pHeaders) / sizeof(*pHeaders); i++)
        for (int nAllow = 0; nAllow < 2; nAllow++)
            for (int nPreset = 0; nPreset < 2; nPreset++)
            {
                branch_api_t test = {0};
                int nPeer;
                CHECK(!branch_open(&test, &nPeer), "Open a server transport for the websocket key policy");
                test.pSession->eType = XAPI_WS;
                test.api.bAllowMissingKey = nAllow;
                if (nPreset) strcpy(test.pSession->sKey, "dGhlIHNhbXBsZSBub25jZQ==");
                char request[512];
                int nLength = snprintf(request, sizeof(request), "GET /keys HTTP/1.1\r\nHost: example.test\r\n"
                    "Upgrade: h2c, \tWebSocket\t, other\r\nConnection: keep-alive, \tUpGrAdE\t, \r\n%s\r\n", pHeaders[i]);
                CHECK(nLength > 0 && nLength < (int)sizeof(request) && write(nPeer, request, nLength) == nLength,
                    "Send the exact key variant with mixed-case comma-separated upgrade tokens");
                uint64_t nDeadline = XTime_GetMonoMs() + 5000;
                while (test.pSession && !test.pSession->bHandshakeDone && XTime_GetMonoMs() < nDeadline)
                    CHECK(XAPI_Service(&test.api, 20) == XEVENTS_SUCCESS, "Service the complete upgrade exchange");
                uint8_t wire[2048];
                ssize_t nRead = recv(nPeer, wire, sizeof(wire), MSG_DONTWAIT);
                if (i < 2 && !nAllow && !nPreset)
                    CHECK(test.nClosed == 1 && !test.pSession && nRead == 0 && test.nErrors == 1 &&
                        test.nLastError == XWS_MISSING_SEC_KEY, "A missing or empty required key is refused without an answer");
                else
                {
                    CHECK(test.pSession && test.pSession->bHandshakeDone && !test.nErrors && nRead > 0,
                        "An allowed or configured key completes the handshake");
                    xhttp_t response;
                    CHECK(XHTTP_ParseData(&response, wire, nRead) == XHTTP_COMPLETE && response.nStatusCode == 101 &&
                        !strcmp(response.sVersion, "1.1") && !XHTTP_GetBodySize(&response), "The peer receives a 101 upgrade");
                    const char *pAccept = XHTTP_GetHeader(&response, "Sec-WebSocket-Accept");
                    const char *pExpected = i == 2 || nPreset ? "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" : "Kfh9QIsMVZcl6xEPYxPHzW8SZ8w=";
                    CHECK(pAccept && !strcmp(pAccept, pExpected), "The digest matches the effective key, including an empty key");
                    XHTTP_Clear(&response);
                    const uint8_t payload[] = {0, 'k', 0xff};
                    xbyte_buffer_t frame;
                    XByteBuffer_Init(&frame, 0, XFALSE);
                    CHECK(XWS_AppendFrame(&frame, payload, sizeof(payload), XWS_BINARY, XTRUE, XTRUE) == XWS_ERR_NONE &&
                        write(nPeer, frame.pData, frame.nUsed) == (ssize_t)frame.nUsed, "Send masked binary data after upgrade");
                    CHECK(XAPI_Service(&test.api, 100) == XEVENTS_SUCCESS && test.nReads == 1 &&
                        test.nReceived == sizeof(payload) && !memcmp(test.received, payload, sizeof(payload)),
                        "Every accepted policy delivers the complete exact application payload");
                    XByteBuffer_Clear(&frame);
                }
                XAPI_Destroy(&test.api);
                close(nPeer);
                CHECK(test.nClosed == 1, "Every key policy releases its session once");
            }
    return 0;
}

static int XTest_ws_token_refusals(void)
{
    const struct { const char *pUpgrade; const char *pConnection; } cases[] = {
        {", ,\t,", "Upgrade"}, {"websockeX", "Upgrade"}, {"h2c,\tother, \t", "Upgrade"},
        {"websocket", ", ,\t,"}, {"websocket", "UpgradX"}, {"websocket", "keep-alive, \t"},
        {"websocket", "UpgradeMore"}, {"websocket", "notUpgrade"}, {"websocket", "Up grade"}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    {
        branch_api_t test = {0};
        int nPeer;
        CHECK(!branch_open(&test, &nPeer), "Open the invalid upgrade token case");
        test.pSession->eType = XAPI_WS;
        char request[512];
        int nLength = snprintf(request, sizeof(request), "GET /tokens HTTP/1.1\r\nHost: example.test\r\n"
            "Upgrade: %s\r\nConnection: %s\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n",
            cases[i].pUpgrade, cases[i].pConnection);
        CHECK(nLength > 0 && nLength < (int)sizeof(request) && write(nPeer, request, nLength) == nLength,
            "Send the complete request with exactly one invalid upgrade header");
        CHECK(XAPI_Service(&test.api, 100) == XEVENTS_SUCCESS && test.nErrors == 1 &&
            test.nLastError == XWS_INVALID_REQUEST && test.nClosed == 1 && !test.pSession && !test.nReads,
            "Separators, trailing whitespace and similar words cannot be mistaken for a valid upgrade token");
        uint8_t byte;
        CHECK(recv(nPeer, &byte, 1, MSG_DONTWAIT) == 0, "The refused request receives no success response or application bytes");
        XAPI_Destroy(&test.api);
        close(nPeer);
        CHECK(test.nClosed == 1, "Refusal and destruction do not close a session twice");
    }
    return 0;
}

static int XTest_http_client_ip(void)
{
    const struct { const char *pHeaders; const char *pExpected; } cases[] = {
        {"X-Client-IP: 203.0.113.1 ignored\r\nX-Forwarded-For: 203.0.113.2\r\nX-Real-IP: 203.0.113.3\r\n", "203.0.113.1"},
        {"X-Client-IP: ,ignored\r\nX-Forwarded-For: 203.0.113.2\t, 203.0.113.4\r\nX-Real-IP: 203.0.113.3\r\n", "203.0.113.2"},
        {"X-Client-IP: \t\r\nX-Forwarded-For: ,ignored\r\nX-Real-IP: 2001:db8::3\r\n", "2001:db8::3"},
        {"X-Forwarded-For: 203.0.113.4, 203.0.113.5\r\n", "203.0.113.4"},
        {"X-Real-IP: ,ignored\r\n", "192.0.2.8"}, {"", "192.0.2.8"}
    };
    const uint8_t body[] = {0, 0x81, 'i'};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    {
        branch_api_t test = {0};
        int nPeer;
        CHECK(!branch_open(&test, &nPeer), "Open the HTTP client-address selection case");
        test.pSession->eType = XAPI_HTTP;
        strcpy(test.pSession->sAddr, "192.0.2.8");
        for (int nRequest = 0; nRequest < 2; nRequest++)
        {
            char request[512];
            int nLength = snprintf(request, sizeof(request), "POST /proxy HTTP/1.1\r\nHost: example.test\r\n"
                "Content-Length: 3\r\n%s\r\n", nRequest ? "X-Client-IP: 198.51.100.99\r\n" : cases[i].pHeaders);
            CHECK(nLength > 0 && nLength + sizeof(body) < sizeof(request), "The complete header and binary body fit the fixture");
            memcpy(request + nLength, body, sizeof(body));
            nLength += sizeof(body);
            CHECK(write(nPeer, request, nLength) == nLength && XAPI_Service(&test.api, 100) == XEVENTS_SUCCESS,
                "Deliver the complete HTTP request through the real event backend");
            CHECK(test.pSession && !strcmp(test.pSession->sRealIP, cases[i].pExpected) && !test.bBadHTTP && !test.nErrors &&
                test.nReads == nRequest + 1 && test.nReceived == (nRequest + 1) * sizeof(body) &&
                !memcmp(test.received + nRequest * sizeof(body), body, sizeof(body)),
                "Header precedence and trimming select the exact address while preserving method, URI, version and binary body");
            CHECK(!test.pSession->rxBuffer.nUsed && !test.pSession->pPacket,
                "The next request cannot retain a borrowed packet or change an already established client address");
        }
        XAPI_Destroy(&test.api);
        close(nPeer);
        CHECK(test.nClosed == 1, "Both requests share one session that is released once");
    }
    return 0;
}

static int XTest_cancel_ready(void)
{
    const uint8_t bytes[] = {0, 'c', 0xff};
    for (int nWrite = 0; nWrite < 2; nWrite++)
    {
        branch_api_t test = {0};
        int nPeer;
        CHECK(!branch_open(&test, &nPeer), "Open the cancelled ready-event case");
        if (nWrite)
            CHECK(XByteBuffer_Add(&test.pSession->txBuffer, bytes, sizeof(bytes)) == sizeof(bytes) &&
                XAPI_SetEvents(test.pSession, XPOLLOUT) == XSTDOK, "Queue output before cancellation");
        else CHECK(write(nPeer, bytes, sizeof(bytes)) == sizeof(bytes), "Make input readable before cancellation");
        test.pSession->bCancel = XTRUE;
        CHECK(XAPI_Service(&test.api, 100) == XEVENTS_SUCCESS && test.nClosed == 1 && !test.pSession &&
            !test.nReads && !test.nWrites && !test.nComplete && !test.nErrors,
            "An already cancelled session closes without dispatching application callbacks or sending queued output");
        uint8_t byte;
        ssize_t nRead = recv(nPeer, &byte, 1, MSG_DONTWAIT);
        CHECK(nRead == 0 || (!nWrite && nRead < 0 && errno == ECONNRESET),
            "The peer receives no bytes from a cancelled session, including previously queued output");
        CHECK(!XAPI_GetEventCount(&test.api), "Cancellation removes the ready event from the owner");
        XAPI_Destroy(&test.api);
        close(nPeer);
        CHECK(test.nClosed == 1, "A cancelled event cannot be destroyed twice");
    }
    return 0;
}

XTEST_MAIN(XTEST_CASE(timer_actions),
    XTEST_CASE(buffered_actions),
    XTEST_CASE(tick_actions),
    XTEST_CASE(write_actions),
    XTEST_CASE(complete_actions),
    XTEST_CASE(ws_key_policy),
    XTEST_CASE(ws_token_refusals),
    XTEST_CASE(http_client_ip),
    XTEST_CASE(cancel_ready)
)
