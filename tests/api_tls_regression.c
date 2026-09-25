/* libxutils: the XAPI event loop driving TLS sessions.
 *
 * TLS on a non-blocking socket is where the loop's event bookkeeping earns
 * its keep: a read can need the socket to become writable and a write can
 * need it to become readable, so the loop has to flip what it is watching
 * and put it back afterwards. A plain TCP session never does that, which is
 * why these cases exist separately from the rest of the loop's tests.
 *
 * The server is built out of XAPI, the client out of the socket layer in a
 * thread, against a throwaway identity generated into a temporary directory.
 */

#include "test.h"
#include "tls_fixture.h"
#include "api.h"
#include "sock.h"
#include "thread.h"
#include "sync.h"
#include "xtime.h"
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    xapi_t api;
    uint16_t nPort;

    int nListening;
    int nAccepted;
    int nRead;
    int nWrite;
    int nComplete;
    int nErrors;
    int nClosed;

    size_t nReceived;       /* Total bytes the server has read */
    size_t nSent;           /* Total bytes the server has queued */
    size_t nEcho;           /* How many bytes to answer with */

    char sFirst[256];
    size_t nFirstLen;

    /* An XAPI client in the same loop (the async cases) */
    const char *pClientRequest;
    int nClientConnected;
    int nClientErrors;
    int nClientClosed;
} api_tls_t;

static uint16_t api_tls_port(void)
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

/* The loop's own client, used by the async cases: it queues its request as soon as it is registered, before the
 * connect or the handshake have finished, and leaves both to the loop. */
static int api_tls_client_callback(api_tls_t *pTest, xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    switch (pCtx->eCbType)
    {
        case XAPI_CB_CONNECTED:
        {
            pTest->nClientConnected++;
            if (pTest->pClientRequest == NULL) return XAPI_CONTINUE;

            xbyte_buffer_t *pTx = XAPI_GetTxBuff(pSession);
            size_t nLength = strlen(pTest->pClientRequest);

            if (pTx == NULL || XByteBuffer_Add(pTx, (const uint8_t*)pTest->pClientRequest, nLength) <= 0)
                return XAPI_DISCONNECT;

            return XAPI_EnableEvent(pSession, XPOLLOUT);
        }
        case XAPI_CB_ERROR: pTest->nClientErrors++; break;
        case XAPI_CB_CLOSED: pTest->nClientClosed++; break;
        case XAPI_CB_COMPLETE: return XAPI_EnableEvent(pSession, XPOLLIN);
        default: break;
    }

    return XAPI_CONTINUE;
}

static int api_tls_callback(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    api_tls_t *pTest = (api_tls_t*)pCtx->pApi->pUserCtx;
    if (pSession != NULL && pSession->eRole == XAPI_CLIENT) return api_tls_client_callback(pTest, pCtx, pSession);

    switch (pCtx->eCbType)
    {
        case XAPI_CB_LISTENING: pTest->nListening++; break;
        case XAPI_CB_ERROR: pTest->nErrors++; break;
        case XAPI_CB_CLOSED: pTest->nClosed++; break;
        default: break;
    }

    if (pCtx->eCbType == XAPI_CB_ACCEPTED)
    {
        /* The handshake has already run inside the accept, so this peer is
         * an established session by the time the callback sees it. */
        pTest->nAccepted++;
        return XAPI_SetEvents(pSession, XPOLLIN);
    }

    if (pCtx->eCbType == XAPI_CB_READ)
    {
        xbyte_buffer_t *pBuffer = (xbyte_buffer_t*)pSession->pPacket;
        if (pBuffer == NULL) return XAPI_DISCONNECT;

        pTest->nRead++;
        pTest->nReceived += pBuffer->nUsed;

        if (!pTest->nFirstLen && pBuffer->nUsed)
        {
            size_t nCopy = XSTD_MIN(pBuffer->nUsed, sizeof(pTest->sFirst) - 1);
            memcpy(pTest->sFirst, pBuffer->pData, nCopy);
            pTest->sFirst[nCopy] = '\0';
            pTest->nFirstLen = nCopy;
        }

        /* Answer with a body large enough that a single TLS write cannot
         * push it all into the socket, which is the case the loop has to
         * finish across several writable events rather than in one. */
        if (pTest->nEcho && !pTest->nSent)
        {
            xbyte_buffer_t *pTx = XAPI_GetTxBuff(pSession);
            if (pTx == NULL) return XAPI_DISCONNECT;

            char sChunk[4096];
            memset(sChunk, 'R', sizeof(sChunk));

            size_t nLeft = pTest->nEcho;
            while (nLeft > 0)
            {
                size_t nAdd = XSTD_MIN(nLeft, sizeof(sChunk));
                if (XByteBuffer_Add(pTx, (uint8_t*)sChunk, nAdd) <= 0) return XAPI_DISCONNECT;
                nLeft -= nAdd;
            }

            pTest->nSent = pTest->nEcho;
            return XAPI_EnableEvent(pSession, XPOLLOUT);
        }

        return XAPI_CONTINUE;
    }

    if (pCtx->eCbType == XAPI_CB_WRITE) { pTest->nWrite++; return XAPI_CONTINUE; }

    if (pCtx->eCbType == XAPI_CB_COMPLETE)
    {
        pTest->nComplete++;
        return XAPI_EnableEvent(pSession, XPOLLIN);
    }

    return XAPI_CONTINUE;
}

/* ---------------- the client side, in a thread ---------------- */

typedef struct {
    tls_fixture_t *pFixture;
    uint16_t nPort;

    const char *pRequest;
    size_t nExpectEcho;     /* Bytes to read back before closing */

    xatomic_t nConnected;
    xatomic_t nFailed;
    xatomic_t nArmed;       /* 1 once the private anchor is installed */
    xatomic_t nWhere;       /* Which step gave up, for the failure message */
    xatomic_t nStatus;      /* The socket status at that point */
    size_t nReceived;
} api_tls_client_t;

/* A TLS client built on the socket layer.
 *
 * XSock_InitSSLClient() does not just build a context: it runs the first
 * SSL_connect() itself, with verification already switched on against the
 * system trust store. The private anchor can therefore only be installed
 * afterwards, in whatever window that first call leaves - and it leaves one
 * only while the server has not answered yet. The client signals nArmed
 * once the anchor is in, and the test does not service the loop until then,
 * so the server cannot answer early and the window is not a race.
 *
 * Nothing outside a test can arrange that, which is what makes the ordering
 * a defect rather than a quirk: an ordinary client cannot use a private CA
 * with this entry point at all. */
static void *api_tls_client(void *pContext)
{
    api_tls_client_t *pClient = (api_tls_client_t*)pContext;
    xsock_t sock;

    if (XSock_Create(&sock, XSOCK_TCP_CLIENT, "127.0.0.1", pClient->nPort) == XSOCK_INVALID ||
        XSock_NonBlock(&sock, XTRUE) == XSOCK_INVALID)
    {
        XSYNC_ATOMIC_SET(&pClient->nWhere, 1);
        XSYNC_ATOMIC_SET(&pClient->nArmed, 1);
        XSYNC_ATOMIC_SET(&pClient->nStatus, XSock_Status(&sock));
        XSock_Close(&sock);
        XSYNC_ATOMIC_SET(&pClient->nFailed, 1);
        return NULL;
    }

    sock.nFlags |= XSOCK_SSL;

    xsock_cert_t cert;
    XSock_InitCert(&cert);
    cert.pCaPath = pClient->pFixture->sCert;

    if (XSock_InitSSLClient(&sock, "localhost") == XSOCK_INVALID ||
        XSock_SetSSLCert(&sock, &cert) == XSOCK_INVALID)
    {
        XSYNC_ATOMIC_SET(&pClient->nWhere, 2);
        XSYNC_ATOMIC_SET(&pClient->nArmed, 1);
        XSYNC_ATOMIC_SET(&pClient->nStatus, XSock_Status(&sock));
        XSock_Close(&sock);
        XSYNC_ATOMIC_SET(&pClient->nFailed, 1);
        return NULL;
    }

    /* The anchor is in. From here the server may answer. */
    XSYNC_ATOMIC_SET(&pClient->nArmed, 1);

    xbool_t bUp = XFALSE;
    for (int i = 0; i < 4000 && !bUp; i++)
    {
        if (XSock_SSLConnect(&sock) == XSOCK_INVALID) break;

        xsock_status_t eStatus = XSock_Status(&sock);
        if (eStatus != XSOCK_WANT_READ && eStatus != XSOCK_WANT_WRITE) bUp = XTRUE;
        else xusleep(1000);
    }

    if (!bUp || XSock_NonBlock(&sock, XFALSE) == XSOCK_INVALID)
    {
        XSYNC_ATOMIC_SET(&pClient->nWhere, bUp ? 4 : 3);
        XSYNC_ATOMIC_SET(&pClient->nStatus, XSock_Status(&sock));
        XSock_Close(&sock);
        XSYNC_ATOMIC_SET(&pClient->nFailed, 1);
        return NULL;
    }

    XSYNC_ATOMIC_SET(&pClient->nConnected, 1);
    XSock_TimeOutR(&sock, 10, 0);
    XSock_TimeOutS(&sock, 10, 0);

    if (pClient->pRequest != NULL)
        XSock_SSLWrite(&sock, pClient->pRequest, strlen(pClient->pRequest));

    /* Read the answer back slowly enough that the server cannot push all
     * of it in one go, which is what leaves a partial write behind. */
    char sBuffer[1024];
    while (pClient->nReceived < pClient->nExpectEcho)
    {
        int nRead = XSock_SSLRead(&sock, sBuffer, sizeof(sBuffer), XFALSE);
        if (nRead <= 0) break;
        pClient->nReceived += (size_t)nRead;
    }

    XSock_Close(&sock);
    return NULL;
}

/* Waits until the client has installed its anchor, so servicing the loop
 * cannot let the server answer before the client is ready for it. */
static void api_tls_wait_armed(api_tls_client_t *pClient)
{
    for (int i = 0; i < 4000 && !XSYNC_ATOMIC_GET(&pClient->nArmed); i++) xusleep(1000);
}

/* ---------------- cases ---------------- */

/* Brings up a TLS listener inside the loop. Returns 0, or 77 to skip. */
static int api_tls_listen(api_tls_t *pTest, tls_fixture_t *pFixture, xbool_t bUseP12)
{
    memset(pTest, 0, sizeof(*pTest));
    if (XAPI_Init(&pTest->api, api_tls_callback, pTest) != XSTDOK) return 77;

    pTest->nPort = api_tls_port();
    if (!pTest->nPort) { XAPI_Destroy(&pTest->api); return 77; }

    xapi_endpoint_t listener;
    XAPI_InitEndpoint(&listener);
    listener.eType = XAPI_SOCK;
    listener.eRole = XAPI_SERVER;
    listener.pAddr = "127.0.0.1";
    listener.nPort = pTest->nPort;
    listener.bTLS = XTRUE;

    if (bUseP12)
    {
        listener.certs.p12Path = pFixture->sP12;
        listener.certs.p12Pass = "regression";
    }
    else
    {
        listener.certs.pCertPath = pFixture->sCert;
        listener.certs.pKeyPath = pFixture->sKey;
    }

    if (XAPI_Listen(&pTest->api, &listener) != XSTDOK)
    {
        XAPI_Destroy(&pTest->api);
        return 77;
    }

    return 0;
}

static int XTest_handshake(void)
{
    /* A TLS listener inside the loop: the accept runs the handshake, and
     * the peer that reaches the callback is already an encrypted session. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No TLS fixture could be built, skipping\n");
        return 77;
    }

    api_tls_t test;
    int nPrep = api_tls_listen(&test, &fixture, XFALSE);
    if (nPrep != 0)
    {
        tls_fixture_end(&fixture);
        printf("No TLS listener could be created, skipping\n");
        return nPrep;
    }

    CHECK(test.nListening == 1, "The TLS listener reported itself");

    api_tls_client_t client;
    memset(&client, 0, sizeof(client));
    client.pFixture = &fixture;
    client.nPort = test.nPort;
    client.pRequest = "a request over tls";
    client.nExpectEcho = 0;

    xthread_t thread;
    if (XThread_Create(&thread, api_tls_client, &client, XFALSE) != XSTDOK)
    {
        XAPI_Destroy(&test.api);
        tls_fixture_end(&fixture);
        printf("No client thread could be started, skipping\n");
        return 77;
    }

    api_tls_wait_armed(&client);

    for (int i = 0; i < 400 && test.nReceived < strlen(client.pRequest); i++)
        XAPI_Service(&test.api, 25);

    XThread_Join(&thread);

    if (XSYNC_ATOMIC_GET(&client.nFailed))
        printf("client gave up at step %d with status %d (%s)\n",
            (int)XSYNC_ATOMIC_GET(&client.nWhere), (int)XSYNC_ATOMIC_GET(&client.nStatus),
            XSock_GetStatusStr((xsock_status_t)XSYNC_ATOMIC_GET(&client.nStatus)));

    CHECK(XSYNC_ATOMIC_GET(&client.nFailed) == 0, "The client handshake succeeded");
    CHECK(XSYNC_ATOMIC_GET(&client.nConnected) == 1, "The client reported itself connected");
    CHECK(test.nAccepted == 1, "The listener accepted one TLS peer");
    CHECK(test.nReceived == strlen(client.pRequest), "The whole request was decrypted");
    CHECK(strcmp(test.sFirst, client.pRequest) == 0, "The request arrived unchanged");
    CHECK(test.nErrors == 0, "Nothing was reported as an error");

    XAPI_Destroy(&test.api);
    tls_fixture_end(&fixture);
    return 0;
}

static int XTest_pkcs12_listener(void)
{
    /* The same listener, with its identity loaded from a PKCS#12 bundle
     * rather than a PEM pair. Whatever the loader took, the session is
     * indistinguishable on the wire. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No TLS fixture could be built, skipping\n");
        return 77;
    }

    api_tls_t test;
    int nPrep = api_tls_listen(&test, &fixture, XTRUE);
    if (nPrep != 0)
    {
        tls_fixture_end(&fixture);
        printf("No TLS listener could be created, skipping\n");
        return nPrep;
    }

    api_tls_client_t client;
    memset(&client, 0, sizeof(client));
    client.pFixture = &fixture;
    client.nPort = test.nPort;
    client.pRequest = "bundled identity";

    xthread_t thread;
    if (XThread_Create(&thread, api_tls_client, &client, XFALSE) != XSTDOK)
    {
        XAPI_Destroy(&test.api);
        tls_fixture_end(&fixture);
        printf("No client thread could be started, skipping\n");
        return 77;
    }

    api_tls_wait_armed(&client);

    for (int i = 0; i < 400 && test.nReceived < strlen(client.pRequest); i++)
        XAPI_Service(&test.api, 25);

    XThread_Join(&thread);

    CHECK(XSYNC_ATOMIC_GET(&client.nFailed) == 0, "The client handshake succeeded");
    CHECK(test.nAccepted == 1, "The bundled identity accepted a peer");
    CHECK(strcmp(test.sFirst, client.pRequest) == 0, "The request arrived unchanged");

    XAPI_Destroy(&test.api);
    tls_fixture_end(&fixture);
    return 0;
}

static int XTest_partial_writes(void)
{
    /* An answer larger than the socket can take at once has to be finished
     * across several writable events. TLS makes that harder than plain TCP,
     * because a write can report that it needs a read first: the loop has
     * to switch what it is watching and switch back once it has it. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No TLS fixture could be built, skipping\n");
        return 77;
    }

    api_tls_t test;
    int nPrep = api_tls_listen(&test, &fixture, XFALSE);
    if (nPrep != 0)
    {
        tls_fixture_end(&fixture);
        printf("No TLS listener could be created, skipping\n");
        return nPrep;
    }

    /* Comfortably past any default socket buffer, so the answer cannot go
     * out in one write, while still small enough to move under valgrind. */
    test.nEcho = 512 * 1024;

    api_tls_client_t client;
    memset(&client, 0, sizeof(client));
    client.pFixture = &fixture;
    client.nPort = test.nPort;
    client.pRequest = "send me a lot";
    client.nExpectEcho = test.nEcho;

    xthread_t thread;
    if (XThread_Create(&thread, api_tls_client, &client, XFALSE) != XSTDOK)
    {
        XAPI_Destroy(&test.api);
        tls_fixture_end(&fixture);
        printf("No client thread could be started, skipping\n");
        return 77;
    }

    api_tls_wait_armed(&client);

    for (int i = 0; i < 2000 && test.nComplete == 0; i++) XAPI_Service(&test.api, 10);

    XThread_Join(&thread);

    CHECK(XSYNC_ATOMIC_GET(&client.nFailed) == 0, "The client handshake succeeded");
    CHECK(test.nAccepted == 1, "The listener accepted the peer");
    CHECK(test.nComplete >= 1, "The whole answer was written out");
    CHECK(client.nReceived == test.nEcho, "Every byte of the answer reached the client");

    /* The write callback is the loop asking for something to send, so a
     * session that queued its answer up front is never asked. */
    CHECK(test.nWrite == 0, "A session with a full buffer is not asked for more");
    CHECK(test.nErrors == 0, "Nothing was reported as an error");

    XAPI_Destroy(&test.api);
    tls_fixture_end(&fixture);
    return 0;
}

static int XTest_plaintext_client(void)
{
    /* A client that speaks plain TCP to a TLS listener is dropped during the
     * handshake rather than being handed to the callback as a session. What
     * it sent must never reach the application as decrypted data. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No TLS fixture could be built, skipping\n");
        return 77;
    }

    api_tls_t test;
    int nPrep = api_tls_listen(&test, &fixture, XFALSE);
    if (nPrep != 0)
    {
        tls_fixture_end(&fixture);
        printf("No TLS listener could be created, skipping\n");
        return nPrep;
    }

    int nFD = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (nFD < 0)
    {
        XAPI_Destroy(&test.api);
        tls_fixture_end(&fixture);
        printf("No client socket, skipping\n");
        return 77;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(test.nPort);

    CHECK(connect(nFD, (struct sockaddr*)&addr, sizeof(addr)) == 0, "The plain client connects");

    const char *pPlain = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    send(nFD, pPlain, strlen(pPlain), MSG_NOSIGNAL);

    for (int i = 0; i < 200; i++) XAPI_Service(&test.api, 10);

    CHECK(test.nRead == 0, "No plaintext reached the application");
    CHECK(test.nReceived == 0, "No plaintext bytes were counted");
    CHECK(test.nAccepted == 0, "The peer never became a session");
    CHECK(XAPI_GetEventCount(&test.api) == 1, "Only the listener is left in the loop");

    close(nFD);
    XAPI_Destroy(&test.api);
    tls_fixture_end(&fixture);
    return 0;
}

static int XTest_missing_identity(void)
{
    /* A TLS listener with no usable identity must fail to come up rather
     * than listen and fail every handshake later, and a certificate path
     * that does not exist is not an identity. */
    api_tls_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&test.api, api_tls_callback, &test) == XSTDOK, "The API initializes");

    uint16_t nPort = api_tls_port();
    if (!nPort)
    {
        XAPI_Destroy(&test.api);
        printf("No free loopback port, skipping\n");
        return 77;
    }

    xapi_endpoint_t listener;
    XAPI_InitEndpoint(&listener);
    listener.eType = XAPI_SOCK;
    listener.eRole = XAPI_SERVER;
    listener.pAddr = "127.0.0.1";
    listener.nPort = nPort;
    listener.bTLS = XTRUE;
    listener.certs.pCertPath = "/nonexistent/path/cert.pem";
    listener.certs.pKeyPath = "/nonexistent/path/key.pem";

    XSTATUS nStatus = XAPI_Listen(&test.api, &listener);
    CHECK(nStatus != XSTDOK, "A listener with no identity does not come up");
    CHECK(test.nErrors >= 1, "The failure was reported");
    CHECK(test.nListening == 0, "Nothing reported itself as listening");

    XAPI_Destroy(&test.api);
    return 0;
}

/* Every deadline in the async cases stretches with the runner: XUTILS_TEST_TIMEOUT_MS, as in api_io_regression.
 * Under valgrind creating the TLS client context alone - loading the system trust store - takes seconds. */
static unsigned long api_tls_timeout_ms(void)
{
    const char *pTimeout = getenv("XUTILS_TEST_TIMEOUT_MS");
    unsigned long nTimeout = pTimeout ? strtoul(pTimeout, NULL, 10) : 10000;

    if (nTimeout < 10000) nTimeout = 10000;
    if (nTimeout > 90000) nTimeout = 90000;
    return nTimeout;
}

static xapi_endpoint_t api_tls_async_endpoint(uint16_t nPort, const char *pCaPath)
{
    xapi_endpoint_t client;
    XAPI_InitEndpoint(&client);
    client.eType = XAPI_SOCK;
    client.eRole = XAPI_CLIENT;
    client.pAddr = "127.0.0.1";
    client.nPort = nPort;
    client.nEvents = XPOLLIN;
    client.bTLS = XTRUE;
    client.bAsync = XTRUE;
    client.certs.pCaPath = pCaPath;
    client.certs.nVerifyFlags = SSL_VERIFY_PEER;
    return client;
}

static int XTest_async_client(void)
{
    /* A client endpoint that neither connects nor handshakes inside XAPI_AddEndpoint: both finish on the loop,
     * against a server in the same loop. The server's certificate is only trusted through the private anchor
     * handed in with the endpoint, which a blocking client installs too late - its handshake has already run
     * against the system store by the time the endpoint's certificates are applied. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No TLS fixture could be built, skipping\n");
        return 77;
    }

    api_tls_t test;
    int nPrep = api_tls_listen(&test, &fixture, XFALSE);
    if (nPrep != 0)
    {
        tls_fixture_end(&fixture);
        printf("No TLS listener could be created, skipping\n");
        return nPrep;
    }

    test.pClientRequest = "an async request over tls";
    xapi_endpoint_t client = api_tls_async_endpoint(test.nPort, fixture.sCert);
    unsigned long nTimeoutMs = api_tls_timeout_ms();

    /* A blocking client would sit in its handshake waiting for a server that only this same loop can run */
    alarm((unsigned int)(nTimeoutMs / 1000U) * 2U);
    XSTATUS nStatus = XAPI_AddEndpoint(&test.api, &client);

    uint64_t nDeadlineMs = XTime_GetMs() + nTimeoutMs;
    while (nStatus == XSTDOK && test.nReceived < strlen(test.pClientRequest) && XTime_GetMs() < nDeadlineMs)
        XAPI_Service(&test.api, 25);

    alarm(0);

    /* Checked after the teardown, so a failure reads as the failed check rather than as the leaks of a session
     * the early return would have skipped. */
    api_tls_t result = test;
    XAPI_Destroy(&test.api);
    tls_fixture_end(&fixture);

    CHECK(nStatus == XSTDOK, "The async client endpoint is registered");
    CHECK(result.nClientConnected == 1, "The client was handed to the loop at once");
    CHECK(result.nAccepted == 1, "The listener accepted the client");
    CHECK(result.nReceived == strlen(result.pClientRequest), "The request crossed the handshake the loop completed");
    CHECK(strcmp(result.sFirst, result.pClientRequest) == 0, "The request arrived unchanged");
    CHECK(result.nClientErrors == 0 && result.nClientClosed == 0, "The client saw no error");
    return 0;
}

static int XTest_async_stalled(void)
{
    /* A server that accepts TCP and never answers TLS. A blocking client waits in SSL_connect for as long as it
     * stays silent - the caller's whole loop with it; the async one registers at once and leaves the verdict to
     * its own timeout. The alarm turns a hang into a failure rather than a stuck run. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No TLS fixture could be built, skipping\n");
        return 77;
    }

    xsock_t silent;
    uint16_t nPort = api_tls_port();
    if (!nPort || XSock_Create(&silent, XSOCK_TCP_SERVER, "127.0.0.1", nPort) == XSOCK_INVALID)
    {
        tls_fixture_end(&fixture);
        printf("No silent listener could be created, skipping\n");
        return 77;
    }

    api_tls_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&test.api, api_tls_callback, &test) == XSTDOK, "The API initializes");

    xapi_endpoint_t client = api_tls_async_endpoint(nPort, fixture.sCert);

    /* The server never answers, so a client that waited for the handshake would never return: the alarm is the
     * check. A stopwatch is not - under valgrind the TLS context alone can take longer than any fixed budget. */
    alarm((unsigned int)(api_tls_timeout_ms() / 1000U));
    XSTATUS nStatus = XAPI_AddEndpoint(&test.api, &client);
    alarm(0);

    XAPI_Service(&test.api, 50);
    int nClientErrors = test.nClientErrors;

    /* Torn down before the checks, so a failure cannot also leave the pending session behind as leaks */
    XAPI_Destroy(&test.api);
    XSock_Close(&silent);
    tls_fixture_end(&fixture);

    CHECK(nStatus == XSTDOK, "The client endpoint is registered while the server stays silent");
    CHECK(nClientErrors == 0, "A handshake still pending is not an error");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(handshake),
    XTEST_CASE(async_client),
    XTEST_CASE(async_stalled),
    XTEST_CASE(pkcs12_listener),
    XTEST_CASE(partial_writes),
    XTEST_CASE(plaintext_client),
    XTEST_CASE(missing_identity)
)
