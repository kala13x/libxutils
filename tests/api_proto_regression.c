/* libxutils: the protocol endpoints the XAPI event loop drives.
 *
 * Each of the WebSocket, MDTP and HTTP endpoint types has its own framing
 * and its own handshake, so each gets a server and a client in the same
 * loop and is driven until a full message has crossed. Everything runs on
 * loopback in one process, which is what lets the assertions be on exact
 * payloads rather than on "something happened".
 */

#include "test.h"
#include "api.h"
#include "sync.h"
#include "xtime.h"
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>

typedef struct {
    xapi_t api;

    int nListening;
    int nConnected;
    int nAccepted;
    int nHandshakeRequest;
    int nHandshakeResponse;
    int nHandshakeAnswer;
    int nRead;
    int nClosed;
    int nTimers;
    int nStatus;
    int nErrors;
    int nDestroy;

    char sLastStatus[128];
    char sReceived[512];
    size_t nReceivedLen;

    xbool_t bEchoed;
    xbool_t bDisconnectOnRead;
    const char *pClientPayload;
} proto_test_t;

/* Finds a loopback port nothing is listening on. */
static uint16_t proto_free_port(void)
{
    int nFD = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (nFD < 0) return 0;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    socklen_t nLen = sizeof(addr);
    uint16_t nPort = 0;

    if (bind(nFD, (struct sockaddr*)&addr, nLen) == 0 &&
        getsockname(nFD, (struct sockaddr*)&addr, &nLen) == 0)
        nPort = ntohs(addr.sin_port);

    close(nFD);
    return nPort;
}

static void proto_record(proto_test_t *pTest, xapi_ctx_t *pCtx)
{
    switch (pCtx->eCbType)
    {
        case XAPI_CB_LISTENING: pTest->nListening++; break;
        case XAPI_CB_CONNECTED: pTest->nConnected++; break;
        case XAPI_CB_ACCEPTED: pTest->nAccepted++; break;
        case XAPI_CB_HANDSHAKE_REQUEST: pTest->nHandshakeRequest++; break;
        case XAPI_CB_HANDSHAKE_RESPONSE: pTest->nHandshakeResponse++; break;
        case XAPI_CB_HANDSHAKE_ANSWER: pTest->nHandshakeAnswer++; break;
        case XAPI_CB_CLOSED: pTest->nClosed++; break;
        case XAPI_CB_TIMER: pTest->nTimers++; break;
        case XAPI_CB_ERROR: pTest->nErrors++; break;
        default: break;
    }

    if (pCtx->eCbType == XAPI_CB_STATUS)
    {
        const char *pStatus = XAPI_GetStatus(pCtx);
        pTest->nStatus++;
        if (pStatus != NULL) xstrncpy(pTest->sLastStatus, sizeof(pTest->sLastStatus), pStatus);
        if (XAPI_IsDestroyEvent(pCtx)) pTest->nDestroy++;
    }
}

/* Keeps the last payload a session delivered, whatever the framing. */
static void proto_keep(proto_test_t *pTest, const void *pData, size_t nLength)
{
    if (pData == NULL || !nLength) return;
    size_t nCopy = XSTD_MIN(nLength, sizeof(pTest->sReceived) - 1);
    memcpy(pTest->sReceived, pData, nCopy);
    pTest->sReceived[nCopy] = '\0';
    pTest->nReceivedLen = nLength;
    pTest->nRead++;
}

/* ---------------- WebSocket ---------------- */

static int ws_callback(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    proto_test_t *pTest = (proto_test_t*)pCtx->pApi->pUserCtx;
    proto_record(pTest, pCtx);

    /* An accepted peer is registered watching nothing, so this callback is
     * where a session says what it wants to hear about. Without it the peer
     * never becomes readable and the exchange stalls with no error. */
    if (pCtx->eCbType == XAPI_CB_ACCEPTED)
        return XAPI_SetEvents(pSession, XPOLLIN);


    if (pCtx->eCbType == XAPI_CB_READ)
    {
        xws_frame_t *pFrame = (xws_frame_t*)pSession->pPacket;
        if (pFrame == NULL) return XAPI_DISCONNECT;

        proto_keep(pTest, XWebFrame_GetPayload(pFrame), XWebFrame_GetPayloadLength(pFrame));

        /* The server echoes the first frame back, the client stops there. */
        if (pSession->eRole == XAPI_PEER && !pTest->bEchoed)
        {
            xws_frame_t reply;
            XWebFrame_Init(&reply);

            if (XWebFrame_Create(&reply, XWebFrame_GetPayload(pFrame),
                XWebFrame_GetPayloadLength(pFrame), XWS_TEXT, XFALSE, XTRUE) != XWS_ERR_NONE)
                return XAPI_DISCONNECT;

            XAPI_PutTxBuff(pSession, &reply.buffer);
            XWebFrame_Clear(&reply);
            pTest->bEchoed = XTRUE;
            return XAPI_EnableEvent(pSession, XPOLLOUT);
        }

        return XAPI_DISCONNECT;
    }

    /* The client sees the server's upgrade response; the answer callback
     * is the server's own side of that exchange. Send the first frame once
     * the response has arrived. */
    if (pCtx->eCbType == XAPI_CB_HANDSHAKE_RESPONSE && pTest->pClientPayload != NULL)
    {
        xws_frame_t frame;
        XWebFrame_Init(&frame);

        if (XWebFrame_Create(&frame, (const uint8_t*)pTest->pClientPayload,
            strlen(pTest->pClientPayload), XWS_TEXT, XTRUE, XTRUE) != XWS_ERR_NONE)
            return XAPI_DISCONNECT;

        XAPI_PutTxBuff(pSession, &frame.buffer);
        XWebFrame_Clear(&frame);
        return XAPI_EnableEvent(pSession, XPOLLOUT);
    }

    return XAPI_CONTINUE;
}

static int XTest_websocket(void)
{
    /* A WebSocket server and client in one loop: the client's upgrade has
     * to be requested, answered and accepted before any frame moves. */
    uint16_t nPort = proto_free_port();
    if (!nPort)
    {
        printf("No free loopback port, skipping\n");
        return 77;
    }

    proto_test_t test;
    memset(&test, 0, sizeof(test));
    test.pClientPayload = "websocket payload";

    CHECK(XAPI_Init(&test.api, ws_callback, &test) == XSTDOK, "The API initializes");

    xapi_endpoint_t server;
    XAPI_InitEndpoint(&server);
    server.eType = XAPI_WS;
    server.eRole = XAPI_SERVER;
    server.pAddr = "127.0.0.1";
    server.nPort = nPort;

    if (XAPI_AddEndpoint(&test.api, &server) != XSTDOK)
    {
        XAPI_Destroy(&test.api);
        printf("The WebSocket listener could not be created, skipping\n");
        return 77;
    }

    CHECK(test.nListening == 1, "The WebSocket listener reported itself");

    xapi_endpoint_t client;
    XAPI_InitEndpoint(&client);
    client.eType = XAPI_WS;
    client.eRole = XAPI_CLIENT;
    client.pAddr = "127.0.0.1";
    client.nPort = nPort;
    client.pUri = "/socket";

    CHECK(XAPI_AddEndpoint(&test.api, &client) == XSTDOK, "The WebSocket client connects");
    CHECK(test.nConnected == 1, "The client reported itself connected");

    /* Drive the loop until the echo has come back to the client. */
    for (int i = 0; i < 400 && test.nRead < 2; i++) XAPI_Service(&test.api, 25);

    CHECK(test.nAccepted >= 1, "The listener accepted the client");
    CHECK(test.nHandshakeRequest >= 1, "The server saw the upgrade request");
    CHECK(test.nHandshakeAnswer >= 1, "The server answered the upgrade");
    CHECK(test.nHandshakeResponse >= 1, "The client saw the upgrade response");
    CHECK(test.nRead >= 2, "The frame reached the server and the echo reached the client");
    CHECK(test.nReceivedLen == strlen(test.pClientPayload), "The echoed frame kept its length");
    CHECK(strcmp(test.sReceived, test.pClientPayload) == 0, "The echoed frame kept its payload");

    XAPI_Destroy(&test.api);
    CHECK(test.nClosed >= 1, "Destroying the API closed the sessions");
    return 0;
}

/* ---------------- MDTP ---------------- */

static int mdtp_callback(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    proto_test_t *pTest = (proto_test_t*)pCtx->pApi->pUserCtx;
    proto_record(pTest, pCtx);

    /* An accepted peer is registered watching nothing, so this callback is
     * where a session says what it wants to hear about. Without it the peer
     * never becomes readable and the exchange stalls with no error. */
    if (pCtx->eCbType == XAPI_CB_ACCEPTED)
        return XAPI_SetEvents(pSession, XPOLLIN);


    if (pCtx->eCbType == XAPI_CB_READ)
    {
        xpacket_t *pPacket = (xpacket_t*)pSession->pPacket;
        if (pPacket == NULL) return XAPI_DISCONNECT;

        proto_keep(pTest, XPacket_GetPayload(pPacket), pPacket->header.nPayloadSize);
        return XAPI_DISCONNECT;
    }

    /* A client asks to be told when it can write, fills the buffer in that
     * callback, and switches to reading once the send has completed. */
    if (pCtx->eCbType == XAPI_CB_CONNECTED)
        return XAPI_SetEvents(pSession, XPOLLOUT);

    if (pCtx->eCbType == XAPI_CB_WRITE && pTest->pClientPayload != NULL && !pTest->bEchoed)
    {
        xpacket_t packet;
        if (XPacket_Init(&packet, (uint8_t*)pTest->pClientPayload,
            (uint32_t)strlen(pTest->pClientPayload)) != XPACKET_ERR_NONE) return XAPI_DISCONNECT;

        packet.header.eType = XPACKET_TYPE_DATA;
        xstrncpy(packet.header.sVersion, sizeof(packet.header.sVersion), XPACKET_VERSION_STR);

        xbyte_buffer_t *pWire = XPacket_Assemble(&packet);
        if (pWire == NULL) { XPacket_Clear(&packet); return XAPI_DISCONNECT; }

        XAPI_PutTxBuff(pSession, pWire);
        XPacket_Clear(&packet);
        pTest->bEchoed = XTRUE;
        return XAPI_EnableEvent(pSession, XPOLLOUT);
    }

    if (pCtx->eCbType == XAPI_CB_COMPLETE)
        return XAPI_EnableEvent(pSession, XPOLLIN);

    return XAPI_CONTINUE;
}

static int XTest_mdtp(void)
{
    /* An MDTP packet carries a JSON header and an opaque payload; the loop
     * has to reassemble both before the callback sees them. */
    uint16_t nPort = proto_free_port();
    if (!nPort)
    {
        printf("No free loopback port, skipping\n");
        return 77;
    }

    proto_test_t test;
    memset(&test, 0, sizeof(test));
    test.pClientPayload = "mdtp payload bytes";

    CHECK(XAPI_Init(&test.api, mdtp_callback, &test) == XSTDOK, "The API initializes");

    xapi_endpoint_t server;
    XAPI_InitEndpoint(&server);
    server.eType = XAPI_MDTP;
    server.eRole = XAPI_SERVER;
    server.pAddr = "127.0.0.1";
    server.nPort = nPort;

    if (XAPI_AddEndpoint(&test.api, &server) != XSTDOK)
    {
        XAPI_Destroy(&test.api);
        printf("The MDTP listener could not be created, skipping\n");
        return 77;
    }

    xapi_endpoint_t client;
    XAPI_InitEndpoint(&client);
    client.eType = XAPI_MDTP;
    client.eRole = XAPI_CLIENT;
    client.pAddr = "127.0.0.1";
    client.nPort = nPort;

    CHECK(XAPI_AddEndpoint(&test.api, &client) == XSTDOK, "The MDTP client connects");

    for (int i = 0; i < 400 && test.nRead < 1; i++) XAPI_Service(&test.api, 25);

    CHECK(test.nAccepted >= 1, "The listener accepted the MDTP client");
    CHECK(test.nRead >= 1, "The packet reached the server");
    CHECK(test.nReceivedLen == strlen(test.pClientPayload), "The packet payload kept its length");
    CHECK(strcmp(test.sReceived, test.pClientPayload) == 0, "The packet payload arrived unchanged");

    XAPI_Destroy(&test.api);
    return 0;
}

/* ---------------- HTTP ---------------- */

static int http_callback(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    proto_test_t *pTest = (proto_test_t*)pCtx->pApi->pUserCtx;
    proto_record(pTest, pCtx);

    /* An accepted peer is registered watching nothing, so this callback is
     * where a session says what it wants to hear about. Without it the peer
     * never becomes readable and the exchange stalls with no error. */
    if (pCtx->eCbType == XAPI_CB_ACCEPTED)
        return XAPI_SetEvents(pSession, XPOLLIN);


    if (pCtx->eCbType == XAPI_CB_READ)
    {
        xhttp_t *pHttp = (xhttp_t*)pSession->pPacket;
        if (pHttp == NULL) return XAPI_DISCONNECT;

        if (pSession->eRole == XAPI_PEER)
        {
            /* The server answers the request it just parsed. */
            proto_keep(pTest, XHTTP_GetBody(pHttp), XHTTP_GetBodySize(pHttp));
            return XAPI_RespondHTTP(pSession, 200, XAPI_STATUS_OK);
        }

        proto_keep(pTest, XHTTP_GetBody(pHttp), XHTTP_GetBodySize(pHttp));
        return XAPI_DISCONNECT;
    }

    /* Same shape as the MDTP client: ask to write, fill the buffer when
     * told, then switch to reading for the answer. */
    if (pCtx->eCbType == XAPI_CB_CONNECTED)
        return XAPI_SetEvents(pSession, XPOLLOUT);

    if (pCtx->eCbType == XAPI_CB_WRITE && pTest->pClientPayload != NULL && !pTest->bEchoed)
    {
        xhttp_t http;
        if (XHTTP_InitRequest(&http, XHTTP_POST, "/api", "1.1") <= 0) return XAPI_DISCONNECT;
        XHTTP_AddHeader(&http, "Host", "127.0.0.1");

        xbyte_buffer_t *pWire = XHTTP_Assemble(&http,
            (const uint8_t*)pTest->pClientPayload, strlen(pTest->pClientPayload));

        if (pWire == NULL) { XHTTP_Clear(&http); return XAPI_DISCONNECT; }

        XAPI_PutTxBuff(pSession, pWire);
        XHTTP_Clear(&http);
        pTest->bEchoed = XTRUE;
        return XAPI_EnableEvent(pSession, XPOLLOUT);
    }

    if (pCtx->eCbType == XAPI_CB_COMPLETE)
        return XAPI_EnableEvent(pSession, XPOLLIN);

    return XAPI_CONTINUE;
}

static int XTest_http_endpoint(void)
{
    /* An HTTP endpoint pair: the loop parses the request on one side and
     * the response on the other, so both directions are framed. */
    uint16_t nPort = proto_free_port();
    if (!nPort)
    {
        printf("No free loopback port, skipping\n");
        return 77;
    }

    proto_test_t test;
    memset(&test, 0, sizeof(test));
    test.pClientPayload = "http endpoint body";

    CHECK(XAPI_Init(&test.api, http_callback, &test) == XSTDOK, "The API initializes");

    xapi_endpoint_t server;
    XAPI_InitEndpoint(&server);
    server.eType = XAPI_HTTP;
    server.eRole = XAPI_SERVER;
    server.pAddr = "127.0.0.1";
    server.nPort = nPort;

    if (XAPI_AddEndpoint(&test.api, &server) != XSTDOK)
    {
        XAPI_Destroy(&test.api);
        printf("The HTTP listener could not be created, skipping\n");
        return 77;
    }

    xapi_endpoint_t client;
    XAPI_InitEndpoint(&client);
    client.eType = XAPI_HTTP;
    client.eRole = XAPI_CLIENT;
    client.pAddr = "127.0.0.1";
    client.nPort = nPort;

    CHECK(XAPI_AddEndpoint(&test.api, &client) == XSTDOK, "The HTTP client connects");

    for (int i = 0; i < 400 && test.nRead < 2; i++) XAPI_Service(&test.api, 25);

    CHECK(test.nAccepted >= 1, "The listener accepted the HTTP client");
    CHECK(test.nRead >= 1, "The request reached the server");

    XAPI_Destroy(&test.api);
    return 0;
}

/* ---------------- timers and session control ---------------- */

static int timer_callback(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    proto_test_t *pTest = (proto_test_t*)pCtx->pApi->pUserCtx;
    proto_record(pTest, pCtx);

    /* Arm a short timer on the accepted peer and let it fire. The peer is
     * registered watching nothing until this callback says otherwise. */
    if (pCtx->eCbType == XAPI_CB_ACCEPTED)
    {
        XAPI_AddTimer(pSession, 40);
        return XAPI_SetEvents(pSession, XPOLLIN);
    }

    if (pCtx->eCbType == XAPI_CB_TIMEOUT)
    {
        pTest->nTimers++;
        return XAPI_DISCONNECT;
    }

    return XAPI_CONTINUE;
}

static int XTest_timers(void)
{
    /* A timer armed on a session fires through the callback, and the
     * session can be torn down from inside that callback. */
    uint16_t nPort = proto_free_port();
    if (!nPort)
    {
        printf("No free loopback port, skipping\n");
        return 77;
    }

    proto_test_t test;
    memset(&test, 0, sizeof(test));

    CHECK(XAPI_Init(&test.api, timer_callback, &test) == XSTDOK, "The API initializes");

    xapi_endpoint_t server;
    XAPI_InitEndpoint(&server);
    server.eType = XAPI_SOCK;
    server.eRole = XAPI_SERVER;
    server.pAddr = "127.0.0.1";
    server.nPort = nPort;

    if (XAPI_AddEndpoint(&test.api, &server) != XSTDOK)
    {
        XAPI_Destroy(&test.api);
        printf("The listener could not be created, skipping\n");
        return 77;
    }

    xapi_endpoint_t client;
    XAPI_InitEndpoint(&client);
    client.eType = XAPI_SOCK;
    client.eRole = XAPI_CLIENT;
    client.pAddr = "127.0.0.1";
    client.nPort = nPort;

    CHECK(XAPI_AddEndpoint(&test.api, &client) == XSTDOK, "The client connects");

    /* The peer is idle, so only the timer can end it. */
    for (int i = 0; i < 400 && test.nTimers == 0; i++) XAPI_Service(&test.api, 25);
    CHECK(test.nTimers >= 1, "The session timer fired");

    XAPI_Destroy(&test.api);
    CHECK(test.nDestroy >= 1, "The teardown was reported as a destroy event");
    CHECK(test.sLastStatus[0] != '\0', "The status callback described what happened");
    return 0;
}

static int XTest_status_strings(void)
{
    /* Every status a callback can be handed has a description, so a caller
     * logging one never prints a bare number. The context carries it. */
    xapi_t api;
    proto_test_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&api, timer_callback, &test) == XSTDOK, "The API initializes");

    const xapi_status_t states[] = {
        XAPI_MISSING_TOKEN, XAPI_MISSING_KEY, XAPI_INVALID_KEY, XAPI_INVALID_ARGS,
        XAPI_INVALID_ROLE, XAPI_INVALID_TOKEN, XAPI_INVALID_PATH, XAPI_ERR_AUTH,
        XAPI_ERR_ALLOC, XAPI_ERR_ASSEMBLE, XAPI_ERR_CRYPT, XAPI_ERR_REGISTER,
        XAPI_ERR_RESOLVE, XAPI_ERR_SUPPORT, XAPI_STATUS_OK, XAPI_TIMER_DESTROY,
        XAPI_DESTROY, XAPI_HUNGED, XAPI_CLOSED
    };

    for (size_t i = 0; i < sizeof(states) / sizeof(*states); i++)
    {
        xapi_ctx_t ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.pApi = &api;
        ctx.nStatus = (uint8_t)states[i];
        ctx.eStatType = XAPI_SELF;
        ctx.eCbType = XAPI_CB_STATUS;

        const char *pText = XAPI_GetStatus(&ctx);
        CHECK(pText != NULL && *pText != '\0', "Every status has a description through the context");

        /* The same number under a protocol reads as that protocol's status,
         * so the pair is what a caller has to log, never the number alone.
         * Every type the context can carry has to resolve, or a callback
         * logging a protocol failure prints nothing at all. */
        const xapi_type_t types[] = {
            XAPI_NONE, XAPI_SELF, XAPI_EVENT, XAPI_HTTP, XAPI_MDTP, XAPI_SOCK, XAPI_WS
        };

        for (size_t n = 0; n < sizeof(types) / sizeof(*types); n++)
        {
            ctx.eStatType = types[n];
            const char *pUnder = XAPI_GetStatus(&ctx);
            CHECK(pUnder != NULL && *pUnder != '\0', "Every status reads under every type");
        }

        ctx.eStatType = XAPI_SELF;
    }

    /* The whole numeric range, under every type: none of it may return a
     * null a caller would then pass to a formatter. */
    for (int nValue = 0; nValue < 256; nValue++)
    {
        const xapi_type_t types[] = {
            XAPI_NONE, XAPI_SELF, XAPI_EVENT, XAPI_HTTP, XAPI_MDTP, XAPI_SOCK, XAPI_WS
        };

        for (size_t n = 0; n < sizeof(types) / sizeof(*types); n++)
        {
            xapi_ctx_t range;
            memset(&range, 0, sizeof(range));
            range.pApi = &api;
            range.eCbType = XAPI_CB_STATUS;
            range.eStatType = types[n];
            range.nStatus = (uint8_t)nValue;

            const char *pText = XAPI_GetStatus(&range);
            CHECK(pText != NULL, "Every number under every type has a description");
        }
    }

    /* The one status a callback must recognise is the teardown, because that
     * is the only one handed in without a session. It is the type and the
     * number together: neither half on its own says the API is going away. */
    xapi_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pApi = &api;
    ctx.eStatType = XAPI_SELF;

    ctx.nStatus = (uint8_t)XAPI_DESTROY;
    CHECK(XAPI_IsDestroyEvent(&ctx) == XTRUE, "The destroy status is a destroy event");

    ctx.eStatType = XAPI_SOCK;
    CHECK(XAPI_IsDestroyEvent(&ctx) == XFALSE, "The same number under a protocol is not");

    ctx.eStatType = XAPI_SELF;
    ctx.nStatus = (uint8_t)XAPI_TIMER_DESTROY;
    CHECK(XAPI_IsDestroyEvent(&ctx) == XFALSE, "A destroyed timer still has its session");

    ctx.nStatus = (uint8_t)XAPI_STATUS_OK;
    CHECK(XAPI_IsDestroyEvent(&ctx) == XFALSE, "An ordinary status is not a destroy event");

    ctx.nStatus = (uint8_t)XAPI_CLOSED;
    CHECK(XAPI_IsDestroyEvent(&ctx) == XFALSE, "A closed status is not a destroy event");

    CHECK(XAPI_IsDestroyEvent(NULL) == XFALSE, "A missing context is not a destroy event");

    /* Every type name and every status name resolves on its own too. */
    const xapi_type_t types[] = {
        XAPI_NONE, XAPI_SELF, XAPI_EVENT, XAPI_HTTP, XAPI_MDTP, XAPI_SOCK, XAPI_WS
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++)
    {
        const char *pName = XAPI_GetTypeStr(types[i]);
        CHECK(pName != NULL && *pName != '\0', "Every API type has a name");
    }

    for (size_t i = 0; i < sizeof(states) / sizeof(*states); i++)
    {
        const char *pName = XAPI_GetStatusStr(states[i]);
        CHECK(pName != NULL && *pName != '\0', "Every API status has a name");
    }

    /* Roles the loop knows how to drive, and one it does not. */
    CHECK(XAPI_IsSupportedRole(XAPI_SERVER) == XTRUE, "The server role is supported");
    CHECK(XAPI_IsSupportedRole(XAPI_CLIENT) == XTRUE, "The client role is supported");
    CHECK(XAPI_IsSupportedRole(XAPI_PEER) == XTRUE, "The peer role is supported");
    CHECK(XAPI_IsSupportedRole(XAPI_INACTIVE) == XFALSE, "An inactive role is not supported");

    XAPI_Destroy(&api);
    return 0;
}

/* What a forked worker does before it leaves.
 *
 * A worker comes back from the fork owning a rebuilt event backend and the
 * listener it inherited, so it can serve. It leaves through exit() rather
 * than _exit() on purpose: the child has its own copy of everything, and a
 * normal exit is what runs the teardown path being tested here. */
static void worker_body(xapi_t *pApi)
{
    /* The fields the worker side of the API is supposed to publish. */
    if (XAPI_GetWorkerIndex(pApi) < 0) _exit(11);
    if (XAPI_GetWorkerPID(pApi) != getpid()) _exit(12);
    if (XAPI_GetWorkerCount(pApi) != 0) _exit(13);
    if (XAPI_GetWorkerPIDs(pApi) != NULL) _exit(14);

    /* Serve for a moment on the rebuilt backend, so the inherited
     * registrations are proven to still be live after the fork. */
    for (int i = 0; i < 10; i++) XAPI_Service(pApi, 10);

    XAPI_Destroy(pApi);
    exit(0);
}

/* Starts a listener and forks nWorkers children off it. Returns 0 on success,
 * 77 when the platform cannot do it, and never returns inside a child. */
static int worker_fixture(xapi_t *pApi, proto_test_t *pTest, size_t nWorkers, xbool_t bAffinity)
{
    memset(pTest, 0, sizeof(*pTest));
    if (XAPI_Init(pApi, timer_callback, pTest) != XSTDOK) return 77;

    /* Workers are forked out of the event map, so the hashed backend is not
     * optional here: XAPI_SpawnWorker refuses an API without it. */
    pApi->bUseHashMap = XTRUE;

    uint16_t nPort = proto_free_port();
    if (!nPort) { XAPI_Destroy(pApi); return 77; }

    xapi_endpoint_t listener;
    XAPI_InitEndpoint(&listener);
    listener.eType = XAPI_SOCK;
    listener.eRole = XAPI_SERVER;
    listener.pAddr = "127.0.0.1";
    listener.nPort = nPort;

    if (XAPI_Listen(pApi, &listener) != XSTDOK) { XAPI_Destroy(pApi); return 77; }

    /* A child inherits the buffers, so anything still pending here would be
     * written out twice - once by the parent and once by every worker. */
    fflush(stdout);
    fflush(stderr);

    XSTATUS nStatus = XAPI_InitWorkers(pApi, nWorkers, bAffinity);

    /* This has to come before the status is judged: a child returns from
     * XAPI_InitWorkers with XSTDUSR, not XSTDOK, and must leave through the
     * worker path rather than fall into the parent's assertions. */
    if (XAPI_IsWorker(pApi)) worker_body(pApi);

    if (nStatus != XSTDOK) { XAPI_Destroy(pApi); return 77; }
    return 0;
}

/* Reaps whatever is left so a failed assertion cannot leave children behind. */
static void worker_cleanup(xapi_t *pApi)
{
    XAPI_StopWorkers(pApi, SIGKILL);
    XAPI_WaitWorkers(pApi);
    XAPI_Destroy(pApi);
}

static int XTest_workers(void)
{
    /* Workers are forked off a listener the parent already owns, and the
     * parent stays the one that knows every child and reaps it. */
    xapi_t api;
    proto_test_t test;

    int nPrep = worker_fixture(&api, &test, 2, XFALSE);
    if (nPrep != 0)
    {
        printf("Workers are not available here, skipping\n");
        return nPrep;
    }

    CHECK(XAPI_GetWorkerCount(&api) == 2, "Both workers started");
    CHECK(XAPI_IsWorker(&api) == XFALSE, "The parent is not a worker");
    CHECK(XAPI_GetWorkerPID(&api) == 0, "The parent has no worker pid of its own");
    CHECK(XAPI_GetWorkerIndex(&api) < 0, "The parent has no worker index");

    const xpid_t *pPIDs = XAPI_GetWorkerPIDs(&api);
    CHECK(pPIDs != NULL, "The parent can enumerate its workers");
    CHECK(pPIDs != NULL && pPIDs[0] > 0 && pPIDs[1] > 0, "Every worker has a process id");
    CHECK(pPIDs == NULL || pPIDs[0] != pPIDs[1], "The workers are distinct processes");

    /* One worker at a time, through the single-child reaper. The workers
     * leave on their own once they have served their short stint. */
    int nWaitStatus = 0;
    xpid_t nFirst = XAPI_WaitWorker(&api, &nWaitStatus);
    CHECK(nFirst > 0, "One worker is reaped on its own");
    CHECK(WIFEXITED(nWaitStatus), "It left through a normal exit");
    CHECK(!WIFEXITED(nWaitStatus) || WEXITSTATUS(nWaitStatus) == 0, "It reported success");

    xpid_t nSecond = XAPI_WaitWorker(&api, &nWaitStatus);
    CHECK(nSecond > 0 && nSecond != nFirst, "The other worker is reaped too");
    CHECK(WIFEXITED(nWaitStatus) && WEXITSTATUS(nWaitStatus) == 0, "It reported success too");

    pPIDs = XAPI_GetWorkerPIDs(&api);
    CHECK(pPIDs != NULL && pPIDs[0] == 0 && pPIDs[1] == 0, "Reaping cleared both slots");

    /* The table is empty now, so both calls say there is nothing left rather
     * than reporting an error. */
    CHECK(XAPI_StopWorkers(&api, SIGTERM) == XSTDNON, "Stopping reaped workers is a no-op");
    CHECK(XAPI_WaitWorkers(&api) == XSTDNON, "Waiting on reaped workers is a no-op");
    CHECK(XAPI_WaitWorker(&api, NULL) == XSTDNON, "There is no single worker left either");

    worker_cleanup(&api);

    /* The accessors stay answerable on an API with no workers at all. */
    CHECK(XAPI_GetWorkerCount(NULL) == 0, "A missing API has no workers");
    CHECK(XAPI_GetWorkerPIDs(NULL) == NULL, "A missing API has no worker table");
    CHECK(XAPI_GetWorkerPID(NULL) == 0, "A missing API has no worker pid");
    CHECK(XAPI_GetWorkerIndex(NULL) < 0, "A missing API has no worker index");
    CHECK(XAPI_GetCoreIndex(NULL) < 0, "A missing API has no core index");
    CHECK(XAPI_IsWorker(NULL) == XFALSE, "A missing API is not a worker");
    CHECK(XAPI_StopWorkers(NULL, SIGTERM) == XSTDINV, "Stopping a missing API is rejected");
    CHECK(XAPI_WaitWorkers(NULL) == XSTDINV, "Waiting on a missing API is rejected");
    CHECK(XAPI_WaitWorker(NULL, NULL) == XSTDINV, "Waiting for one worker of a missing API is rejected");
    CHECK(XAPI_InitWorkers(NULL, 2, XFALSE) == XSTDINV, "Starting workers on a missing API is rejected");
    return 0;
}

static volatile sig_atomic_t g_nWorkerInterrupt = 0;
static void worker_alarm(int nSignal) { (void)nSignal; g_nWorkerInterrupt = 1; }

static int XTest_worker_affinity(void)
{
    /* The supervisor loop is the documented way to run workers: it blocks on
     * the children, respawns whichever one dies, and tears the whole set down
     * when the interrupt flag is raised. Affinity is requested as they fork,
     * and a respawned child comes back out of this call rather than out of
     * XAPI_InitWorkers, so the worker check has to be repeated here. */
    xapi_t api;
    proto_test_t test;

    int nPrep = worker_fixture(&api, &test, 2, XTRUE);
    if (nPrep != 0)
    {
        printf("Workers are not available here, skipping\n");
        return nPrep;
    }

    CHECK(XAPI_GetWorkerCount(&api) == 2, "Both pinned workers started");

    const xpid_t *pPIDs = XAPI_GetWorkerPIDs(&api);
    CHECK(pPIDs != NULL && pPIDs[0] > 0 && pPIDs[1] > 0, "Every pinned worker has a process id");

    /* The watch loop only ends on the interrupt flag, so raise it from a
     * signal while it is blocked in waitpid. Along the way the workers that
     * finish their stint are noticed and replaced, which is the behaviour
     * this call exists for. */
    struct sigaction action, previous;
    memset(&action, 0, sizeof(action));
    action.sa_handler = worker_alarm;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGALRM, &action, &previous) != 0)
    {
        worker_cleanup(&api);
        printf("SIGALRM cannot be installed, skipping\n");
        return 77;
    }

    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_usec = 400000;

    g_nWorkerInterrupt = 0;
    fflush(stdout);
    fflush(stderr);
    CHECK(setitimer(ITIMER_REAL, &timer, NULL) == 0, "Arm the interrupt");

    XSTATUS nWatch = XAPI_WatchWorkers(&api, &g_nWorkerInterrupt);

    /* A respawn inside the watch loop returns here in the child. */
    if (XAPI_IsWorker(&api)) worker_body(&api);

    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_REAL, &timer, NULL);
    sigaction(SIGALRM, &previous, NULL);

    CHECK(g_nWorkerInterrupt == 1, "The interrupt was delivered");
    CHECK(nWatch == XSTDOK || nWatch == XSTDNON, "Watching ended without an error");

    pPIDs = XAPI_GetWorkerPIDs(&api);
    CHECK(pPIDs != NULL && pPIDs[0] == 0 && pPIDs[1] == 0, "Watching cleared the worker table");
    CHECK(XAPI_StopWorkers(&api, SIGTERM) == XSTDNON, "Nothing is left to signal");

    worker_cleanup(&api);

    volatile sig_atomic_t nUnused = 1;
    CHECK(XAPI_WatchWorkers(NULL, &nUnused) == XSTDINV, "Watching a missing API is rejected");

    /* Affinity is a flag on the API, so it can be set before any fork. */
    xapi_t plain;
    proto_test_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    CHECK(XAPI_Init(&plain, timer_callback, &ctx) == XSTDOK, "The API initializes");
    CHECK(XAPI_SetWorkerAffinity(&plain, XTRUE) == XSTDOK, "Affinity can be requested");
    CHECK(XAPI_SetWorkerAffinity(&plain, XFALSE) == XSTDOK, "Affinity can be withdrawn");
    CHECK(XAPI_SetWorkerAffinity(NULL, XTRUE) == XSTDINV, "Affinity on a missing API is rejected");
    CHECK(XAPI_WatchWorkers(&plain, NULL) == XSTDNON, "Watching with no workers does nothing");
    CHECK(XAPI_WaitWorker(&plain, NULL) == XSTDNON, "Waiting for one worker with none does nothing");

    /* Workers cannot be forked out of an API whose events are not hashed:
     * the fork rebuilds the backend from that map. */
    CHECK(XAPI_InitWorkers(&plain, 1, XFALSE) != XSTDOK, "Workers need the hashed event map");
    if (XAPI_IsWorker(&plain)) worker_body(&plain);

    XAPI_Destroy(&plain);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(websocket),
    XTEST_CASE(mdtp),
    XTEST_CASE(http_endpoint),
    XTEST_CASE(timers),
    XTEST_CASE(status_strings),
    XTEST_CASE(workers),
    XTEST_CASE(worker_affinity)
)
