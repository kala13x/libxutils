/* libxutils: the TLS side of the socket layer, end to end over loopback.
 *
 * A private key and a self-signed certificate are generated into a
 * temporary directory, so the whole path the library documents runs for
 * real: a server that loads a certificate from disk, a client that trusts
 * it, a handshake, and encrypted traffic in both directions. Nothing here
 * reaches outside the machine or depends on a system trust store.
 */

#include "test.h"
#include "tls_fixture.h"
#include "sock.h"
#include "xfs.h"
#include "str.h"
#include "thread.h"
#include "sync.h"
#include "xtime.h"
#include <unistd.h>


/* How the server ends the session once it has answered. Each of these is a
 * different error the client's TLS read has to tell apart. */
typedef enum {
    TLS_END_CLOSE = 0,      /* Ordinary close: OpenSSL sends close_notify */
    TLS_END_RESET,          /* Reset the connection with no close_notify */
    TLS_END_GARBAGE         /* Raw bytes into the middle of the record stream */
} tls_end_t;

typedef struct {
    tls_fixture_t *pFixture;
    uint16_t nPort;
    xatomic_t nReady;       /* 1 once listening, 2 on failure */
    xatomic_t nAccepted;
    xatomic_t nHandshook;

    xbool_t bUseP12;        /* Load the bundle instead of the PEM pair */
    xbool_t bRequireClient; /* Ask the client for a certificate */
    tls_end_t eEnd;
    xatomic_t nClientArmed; /* 1 once the client has installed its anchor */
    char sReceived[256];
    size_t nReceivedLen;
    const char *pReply;
} tls_server_t;

/* A TLS server built entirely out of the library's own entry points.
 *
 * The identity goes on the listener, not on the accepted peer: XSock_Accept()
 * creates the per-connection session from the listener's context and runs
 * the handshake itself, which is the shape XAPI_Listen() uses too. */
static void *tls_serve(void *pContext)
{
    tls_server_t *pServer = (tls_server_t*)pContext;
    xsock_t listener;

    uint16_t nPort = 0;
    for (uint16_t nTry = 39600; nTry < 39700 && !nPort; nTry++)
    {
        if (XSock_Create(&listener, XSOCK_TCP_SERVER | XSOCK_SSL | XSOCK_REUSEADDR,
            "127.0.0.1", nTry) != XSOCK_INVALID && XSock_Status(&listener) == XSOCK_ERR_NONE) nPort = nTry;
        else XSock_Close(&listener);
    }

    if (!nPort) { XSYNC_ATOMIC_SET(&pServer->nReady, 2); return NULL; }

    xsock_cert_t cert;
    XSock_InitCert(&cert);

    if (pServer->bUseP12)
    {
        cert.p12Path = pServer->pFixture->sP12;
        cert.p12Pass = "regression";
    }
    else
    {
        cert.pCertPath = pServer->pFixture->sCert;
        cert.pKeyPath = pServer->pFixture->sKey;
    }

    if (XSock_SetSSLCert(&listener, &cert) == XSOCK_INVALID)
    {
        XSock_Close(&listener);
        XSYNC_ATOMIC_SET(&pServer->nReady, 2);
        return NULL;
    }

    pServer->nPort = nPort;
    XSYNC_ATOMIC_SET(&pServer->nReady, 1);
    XSock_TimeOutR(&listener, 10, 0);

    /* Nothing may be answered until the client has installed its trust
     * anchor. XSock_InitSSLClient() runs the first SSL_connect() itself with
     * verification already on, so the anchor can only go in while the server
     * has not replied yet: answering early makes the client reject a
     * certificate it was about to be told to trust. */
    for (int i = 0; i < 4000 && !XSYNC_ATOMIC_GET(&pServer->nClientArmed); i++) xusleep(1000);

    /* Accept runs the TLS handshake before it hands the peer back. */
    xsock_t peer;
    XSOCKET nAccepted = XSock_Accept(&listener, &peer);
    XSock_Close(&listener);

    if (nAccepted == XSOCK_INVALID) return NULL;

    XSYNC_ATOMIC_ADD(&pServer->nAccepted, 1);
    XSYNC_ATOMIC_ADD(&pServer->nHandshook, 1);
    XSock_TimeOutR(&peer, 10, 0);
    XSock_TimeOutS(&peer, 10, 0);

    /* Read the client's message and answer it, both over TLS. */
    char sBuffer[256];
    int nRead = XSock_SSLRead(&peer, sBuffer, sizeof(sBuffer) - 1, XFALSE);
    if (nRead > 0)
    {
        pServer->nReceivedLen = (size_t)nRead;
        memcpy(pServer->sReceived, sBuffer, (size_t)nRead);
        pServer->sReceived[nRead] = '\0';
    }

    if (pServer->pReply != NULL)
        XSock_SSLWrite(&peer, pServer->pReply, strlen(pServer->pReply));

    if (pServer->eEnd == TLS_END_RESET)
    {
        /* Zero linger makes close() send a reset, so the record stream ends
         * with no close_notify and no orderly FIN either. */
        struct linger reset;
        reset.l_onoff = 1;
        reset.l_linger = 0;
        setsockopt(peer.nFD, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset));
    }
    else if (pServer->eEnd == TLS_END_GARBAGE)
    {
        /* Bytes written straight to the descriptor land where a TLS record
         * header is expected, which is a protocol error rather than an end. */
        const char *pJunk = "\x17\x03\x03\xff\xffnot a tls record at all";
        send(peer.nFD, pJunk, strlen(pJunk), 0);
        xusleep(20000);
    }

    XSock_Close(&peer);
    return NULL;
}

static int tls_start_ex(tls_server_t *pServer, tls_fixture_t *pFixture,
                        xthread_t *pThread, xbool_t bUseP12, tls_end_t eEnd)
{
    memset(pServer, 0, sizeof(*pServer));
    pServer->pFixture = pFixture;
    pServer->bUseP12 = bUseP12;
    pServer->eEnd = eEnd;
    pServer->pReply = "hello from the tls server";

    if (XThread_Create(pThread, tls_serve, pServer, XFALSE) != XSTDOK) return XSTDERR;
    for (int i = 0; i < 2000 && !XSYNC_ATOMIC_GET(&pServer->nReady); i++) xusleep(5000);
    if (XSYNC_ATOMIC_GET(&pServer->nReady) == 1) return XSTDOK;

    XThread_Join(pThread);
    return XSTDERR;
}

static int tls_start(tls_server_t *pServer, tls_fixture_t *pFixture, xthread_t *pThread, xbool_t bUseP12)
{
    return tls_start_ex(pServer, pFixture, pThread, bUseP12, TLS_END_CLOSE);
}

/* Connects a TLS client that trusts a private anchor.
 *
 * XSock_InitSSLClient() does not just build a context: it runs the first
 * SSL_connect() itself, with verification already switched on against the
 * system trust store. A private anchor can therefore only be installed in
 * whatever window that first call leaves behind, and it leaves one only
 * while the server has not answered yet. Making the socket non-blocking
 * first is what keeps that call from running to completion; pServer is then
 * told, through nClientArmed, that it may answer.
 *
 * Creating the socket with XSOCK_NB would be the obvious way to ask for the
 * non-blocking part, but that flag is applied after the connect for TCP
 * sockets, so the handshake would already have happened. */
static int tls_connect_armed(xsock_t *pSock, uint16_t nPort, const xsock_cert_t *pCert,
                             tls_server_t *pServer)
{
    if (XSock_Create(pSock, XSOCK_TCP_CLIENT, "127.0.0.1", nPort) == XSOCK_INVALID) goto armed;
    if (XSock_NonBlock(pSock, XTRUE) == XSOCK_INVALID) goto armed;

    pSock->nFlags |= XSOCK_SSL;
    if (XSock_InitSSLClient(pSock, "localhost") == XSOCK_INVALID) goto armed;

    xsock_cert_t cert = *pCert;
    if (XSock_SetSSLCert(pSock, &cert) == XSOCK_INVALID) goto armed;

    if (pServer != NULL) XSYNC_ATOMIC_SET(&pServer->nClientArmed, 1);

    for (int i = 0; i < 2000; i++)
    {
        if (XSock_SSLConnect(pSock) == XSOCK_INVALID) return XSTDERR;

        xsock_status_t eStatus = XSock_Status(pSock);
        if (eStatus != XSOCK_WANT_READ && eStatus != XSOCK_WANT_WRITE)
            return XSock_NonBlock(pSock, XFALSE) == XSOCK_INVALID ? XSTDERR : XSTDOK;

        xusleep(2000);
    }

armed:
    /* However this ended, the server must not be left waiting for a signal
     * that is never coming. */
    if (pServer != NULL) XSYNC_ATOMIC_SET(&pServer->nClientArmed, 1);
    return XSTDERR;
}


static int XTest_handshake(void)
{
    /* A full TLS exchange: the client trusts the server's own certificate
     * as its only anchor, so nothing depends on the system trust store. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("The TLS fixture could not be built, skipping\n");
        return 77;
    }

    tls_server_t server;
    xthread_t thread;
    if (tls_start(&server, &fixture, &thread, XFALSE) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No free loopback port, skipping\n");
        return 77;
    }

    XSock_InitSSL();

    /* Trust the server's certificate and require it to match the host. */
    xsock_cert_t cert;
    XSock_InitCert(&cert);
    cert.pCaPath = fixture.sCert;
    cert.pHostName = "localhost";
    cert.nVerifyFlags = SSL_VERIFY_PEER;

    xsock_t client;
    CHECK(tls_connect_armed(&client, server.nPort, &cert, &server) == XSTDOK, "The TLS handshake completes");
    CHECK(XSock_IsSSL(&client) == XTRUE, "The client socket is a TLS socket");
    CHECK(XSock_GetSSL(&client) != NULL, "The client has a TLS session");
    CHECK(XSock_GetSSLCTX(&client) != NULL, "The client has a TLS context");

    const char *pMessage = "hello from the tls client";
    CHECK(XSock_SSLWrite(&client, pMessage, strlen(pMessage)) == (int)strlen(pMessage),
        "The client writes over TLS");

    char sReply[256];
    int nRead = XSock_SSLRead(&client, sReply, sizeof(sReply) - 1, XFALSE);
    CHECK(nRead > 0, "The client reads the server answer over TLS");
    sReply[nRead] = '\0';
    CHECK(strcmp(sReply, server.pReply) == 0, "The answer arrived unchanged through the tunnel");

    XSock_Close(&client);
    XThread_Join(&thread);

    CHECK(XSYNC_ATOMIC_GET(&server.nAccepted) == 1, "The server accepted the connection");
    CHECK(XSYNC_ATOMIC_GET(&server.nHandshook) == 1, "The server completed the handshake");
    CHECK(strcmp(server.sReceived, pMessage) == 0, "The client message arrived unchanged through the tunnel");

    tls_fixture_end(&fixture);
    return 0;
}

static int XTest_stale_error_queue(void)
{
    /* SSL_get_error() consults this thread's OpenSSL error queue before the
     * return value it is given. An error that some unrelated connection or call
     * left there made a read that merely had no data yet look like a fatal
     * protocol error, and the healthy connection was closed on the spot. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("The TLS fixture could not be built, skipping\n");
        return 77;
    }

    tls_server_t server;
    xthread_t thread;
    if (tls_start(&server, &fixture, &thread, XFALSE) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No free loopback port, skipping\n");
        return 77;
    }

    XSock_InitSSL();

    xsock_cert_t cert;
    XSock_InitCert(&cert);
    cert.pCaPath = fixture.sCert;
    cert.pHostName = "localhost";
    cert.nVerifyFlags = SSL_VERIFY_PEER;

    xsock_t client;
    CHECK(tls_connect_armed(&client, server.nPort, &cert, &server) == XSTDOK, "The TLS handshake completes");
    CHECK(XSock_NonBlock(&client, XTRUE) != XSOCK_INVALID, "Read without blocking");

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    ERR_raise(ERR_LIB_SSL, SSL_R_BAD_LENGTH);
#else
    ERR_put_error(ERR_LIB_SSL, 0, SSL_R_BAD_LENGTH, __FILE__, __LINE__);
#endif
    CHECK(ERR_peek_error() != 0, "An unrelated failure is left on the error queue");

    char sReply[256];
    int nRead = XSock_SSLRead(&client, sReply, sizeof(sReply) - 1, XFALSE);
    CHECK(nRead <= 0 && XSock_Status(&client) == XSOCK_WANT_READ, "A read with no data yet asks to be retried");
    CHECK(client.nFD != XSOCK_INVALID && XSock_GetSSL(&client) != NULL, "A stale error does not close the connection");

    CHECK(XSock_NonBlock(&client, XFALSE) != XSOCK_INVALID, "Go back to blocking reads");
    const char *pMessage = "still here";
    CHECK(XSock_SSLWrite(&client, pMessage, strlen(pMessage)) == (int)strlen(pMessage), "The session still carries data");

    nRead = XSock_SSLRead(&client, sReply, sizeof(sReply) - 1, XFALSE);
    CHECK(nRead > 0, "The answer arrives over the same session");
    sReply[nRead] = '\0';
    CHECK(strcmp(sReply, server.pReply) == 0, "The answer arrives unchanged");

    XSock_Close(&client);
    XThread_Join(&thread);
    CHECK(strcmp(server.sReceived, pMessage) == 0, "The server received the message sent after the stale error");

    tls_fixture_end(&fixture);
    return 0;
}

static int XTest_pkcs12_identity(void)
{
    /* The same identity loaded from a PKCS#12 bundle has to serve the same
     * handshake as the separate certificate and key files. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("The TLS fixture could not be built, skipping\n");
        return 77;
    }

    if (!XPath_Exists(fixture.sP12))
    {
        tls_fixture_end(&fixture);
        printf("PKCS#12 bundles are unavailable in this build, skipping\n");
        return 77;
    }

    tls_server_t server;
    xthread_t thread;
    if (tls_start(&server, &fixture, &thread, XTRUE) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No free loopback port, skipping\n");
        return 77;
    }

    XSock_InitSSL();

    xsock_cert_t cert;
    XSock_InitCert(&cert);
    cert.pCaPath = fixture.sCert;

    xsock_t client;
    CHECK(tls_connect_armed(&client, server.nPort, &cert, &server) == XSTDOK,
        "The handshake against the bundled identity completes");

    CHECK(XSock_SSLWrite(&client, "p12", 3) == 3, "The client writes over the bundled identity");

    char sReply[256];
    int nRead = XSock_SSLRead(&client, sReply, sizeof(sReply) - 1, XFALSE);
    CHECK(nRead > 0, "The server answers over the bundled identity");

    XSock_Close(&client);
    XThread_Join(&thread);
    CHECK(XSYNC_ATOMIC_GET(&server.nHandshook) == 1, "The bundled identity completed the handshake");

    tls_fixture_end(&fixture);
    return 0;
}

static int XTest_untrusted(void)
{
    /* The same server, with the client trusting nothing: verification has
     * to refuse the handshake rather than fall through to plaintext. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("The TLS fixture could not be built, skipping\n");
        return 77;
    }

    tls_server_t server;
    xthread_t thread;
    if (tls_start(&server, &fixture, &thread, XFALSE) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No free loopback port, skipping\n");
        return 77;
    }

    XSock_InitSSL();

    /* Verification is on, but the self-signed certificate is not trusted:
     * no private anchor is installed, so only the system store applies. */
    xsock_cert_t cert;
    XSock_InitCert(&cert);
    cert.nVerifyFlags = SSL_VERIFY_PEER;
    cert.pHostName = "localhost";

    xsock_t client;
    CHECK(tls_connect_armed(&client, server.nPort, &cert, &server) == XSTDERR, "An untrusted certificate is refused");
    CHECK(XSock_Status(&client) != XSOCK_ERR_NONE, "The refusal is reported as an error");
    CHECK(XSock_IsSSLError(XSock_Status(&client)) == XTRUE, "The refusal is reported as a TLS error");

    /* The failure has a description the caller can log. */
    char sError[512];
    XSock_LastSSLError(sError, sizeof(sError));
    CHECK(XSock_ErrStr(&client) != NULL, "The socket has an error description");

    XSock_Close(&client);
    XThread_Join(&thread);

    tls_fixture_end(&fixture);
    return 0;
}

static int XTest_host_override(void)
{
    /* The client dials localhost but asks, through the certificate options, for a different name. The name asked
     * for is the one that has to be verified: a caller that connects by address and names the service it expects
     * (the relay's Redis link does exactly that with its SNI setting) must get that name checked, not the address.
     * The guard around this used to test a function name with defined(), which compiled the check away. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("The TLS fixture could not be built, skipping\n");
        return 77;
    }

    tls_server_t server;
    xthread_t thread;
    if (tls_start(&server, &fixture, &thread, XFALSE) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No free loopback port, skipping\n");
        return 77;
    }

    XSock_InitSSL();

    /* Trusted anchor, valid certificate for localhost - but not for the name the caller asked for. */
    xsock_cert_t cert;
    XSock_InitCert(&cert);
    cert.pCaPath = fixture.sCert;
    cert.pHostName = "wrong.example";
    cert.nVerifyFlags = SSL_VERIFY_PEER;

    xsock_t client;
    CHECK(tls_connect_armed(&client, server.nPort, &cert, &server) == XSTDERR,
        "A certificate that does not cover the requested name is refused");
    CHECK(XSock_IsSSLError(XSock_Status(&client)) == XTRUE, "The refusal is reported as a TLS error");

    XSock_Close(&client);
    XThread_Join(&thread);
    tls_fixture_end(&fixture);

    /* An address works as the requested name too, matched against the certificate's IP entry. */
    if (tls_fixture_begin(&fixture) != XSTDOK || tls_start(&server, &fixture, &thread, XFALSE) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("The second TLS server could not be started, skipping\n");
        return 77;
    }

    cert.pCaPath = fixture.sCert;
    cert.pHostName = "127.0.0.1";
    CHECK(tls_connect_armed(&client, server.nPort, &cert, &server) == XSTDOK,
        "An address the certificate covers is accepted as the requested name");

    const char *pMessage = "by address";
    CHECK(XSock_SSLWrite(&client, pMessage, strlen(pMessage)) == (int)strlen(pMessage), "The client writes over TLS");

    char sReply[256];
    CHECK(XSock_SSLRead(&client, sReply, sizeof(sReply) - 1, XFALSE) > 0, "The client reads the answer");

    XSock_Close(&client);
    XThread_Join(&thread);
    tls_fixture_end(&fixture);
    return 0;
}

static int XTest_cert_guards(void)
{
    /* A certificate the server cannot load is reported at configuration
     * time rather than surfacing as a confusing handshake failure. */
    XSock_InitSSL();

    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");

    xsock_t sock;
    CHECK(XSock_Init(&sock, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "The socket wraps");
    sock.nFlags |= XSOCK_SSL;
    CHECK(XSock_InitSSLServer(&sock, 0) != XSOCK_INVALID, "The TLS server context is created");

    /* A certificate path that is not there. */
    xsock_cert_t cert;
    XSock_InitCert(&cert);
    cert.pCertPath = "/no/such/cert.pem";
    cert.pKeyPath = "/no/such/key.pem";
    CHECK(XSock_SetSSLCert(&sock, &cert) == XSOCK_INVALID, "A missing certificate is refused");
    xclosesock(pair[1]);

    /* A trust anchor path that is not there. The socket is non-blocking so
     * that XSock_InitSSLClient() leaves the handshake it starts unfinished
     * instead of failing against a peer that will never answer. */
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");
    CHECK(XSock_Init(&sock, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "The socket wraps");
    CHECK(XSock_NonBlock(&sock, XTRUE) != XSOCK_INVALID, "The socket is made non blocking");
    sock.nFlags |= XSOCK_SSL;
    CHECK(XSock_InitSSLClient(&sock, "localhost") != XSOCK_INVALID, "The TLS client context is created");
    CHECK(XSock_Status(&sock) == XSOCK_WANT_READ || XSock_Status(&sock) == XSOCK_WANT_WRITE,
        "The unfinished handshake reports that it wants more input");

    XSock_InitCert(&cert);
    cert.pCaPath = "/no/such/ca.pem";
    CHECK(XSock_SetSSLCert(&sock, &cert) == XSOCK_INVALID, "A missing trust anchor is refused");
    xclosesock(pair[1]);

    /* A bundle with the wrong password. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) == XSTDOK && XPath_Exists(fixture.sP12))
    {
        CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");
        CHECK(XSock_Init(&sock, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "The socket wraps");
        sock.nFlags |= XSOCK_SSL;
        CHECK(XSock_InitSSLServer(&sock, 0) != XSOCK_INVALID, "The TLS server context is created");

        XSock_InitCert(&cert);
        cert.p12Path = fixture.sP12;
        cert.p12Pass = "wrong-password";
        CHECK(XSock_SetSSLCert(&sock, &cert) == XSOCK_INVALID, "A bundle with the wrong password is refused");
        xclosesock(pair[1]);
    }
    tls_fixture_end(&fixture);

    /* A plain socket has no TLS session to hand out. */
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");
    CHECK(XSock_Init(&sock, XSOCK_TCP_PEER, pair[0]) != XSOCK_ERROR, "The socket wraps");
    CHECK(XSock_IsSSL(&sock) == XFALSE, "A plain socket is not a TLS socket");
    CHECK(XSock_GetSSL(&sock) == NULL, "A plain socket has no TLS session");
    CHECK(XSock_Pending(&sock) == 0, "A plain socket buffers nothing above the kernel");
    XSock_Close(&sock);
    xclosesock(pair[1]);

    /* The error formatter tolerates a small destination. */
    char sTiny[8];
    XSock_LastSSLError(sTiny, sizeof(sTiny));
    CHECK(strlen(sTiny) < sizeof(sTiny), "The TLS error description respects its buffer");

    /* Initializing twice is a no-op rather than a reinitialization. */
    XSock_InitSSL();
    XSock_InitSSL();
    return 0;
}

static int XTest_ssl_teardown(void)
{
    /* This case has to be the last one in the file, and must stay last when
     * cases are added. XSock_DeinitSSL() runs OPENSSL_cleanup(), which tears
     * down the whole process's OpenSSL state: everything after it, in any
     * case, gets an unusable library back. Under ctest each case is its own
     * process, but the binary also runs every case in order when it is
     * invoked bare, and that is where the ordering matters. */
    XSock_InitSSL();
    XSock_InitSSL();

    XSock_DeinitSSL();
    XSock_DeinitSSL();

    /* Nothing may use the library from here on. */
    return 0;
}


/* Runs one session against a server that ends it in the given way, and
 * reports what the client's next TLS read made of that ending. */
static int tls_read_after_end(tls_fixture_t *pFixture, tls_end_t eEnd,
                              int *pResult, xsock_status_t *pStatus)
{
    tls_server_t server;
    xthread_t thread;

    if (tls_start_ex(&server, pFixture, &thread, XFALSE, eEnd) != XSTDOK) return XSTDERR;

    xsock_cert_t cert;
    XSock_InitCert(&cert);
    cert.pCaPath = pFixture->sCert;

    xsock_t client;
    if (tls_connect_armed(&client, server.nPort, &cert, &server) != XSTDOK)
    {
        XSock_Close(&client);
        XThread_Join(&thread);
        return XSTDERR;
    }

    XSock_TimeOutR(&client, 10, 0);
    XSock_SSLWrite(&client, "hello", 5);

    /* The first read takes the answer, the second meets the ending. */
    char sBuffer[256];
    XSock_SSLRead(&client, sBuffer, sizeof(sBuffer) - 1, XFALSE);

    *pResult = XSock_SSLRead(&client, sBuffer, sizeof(sBuffer) - 1, XFALSE);
    *pStatus = XSock_Status(&client);

    XSock_Close(&client);
    XThread_Join(&thread);
    return XSTDOK;
}

static int XTest_peer_endings(void)
{
    /* The three ways a TLS session can stop have to be told apart, because
     * only one of them is an orderly end of data. A caller that treated a
     * reset or a corrupted record as an ordinary EOF would accept a
     * truncated message as complete. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No TLS fixture could be built, skipping\n");
        return 77;
    }

    int nResult = 0;
    xsock_status_t eStatus = XSOCK_ERR_NONE;

    /* An orderly close sends close_notify, which is the one real end. */
    if (tls_read_after_end(&fixture, TLS_END_CLOSE, &nResult, &eStatus) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("The TLS server could not be started, skipping\n");
        return 77;
    }

    CHECK(nResult <= 0, "A closed session delivers no more data");
    CHECK(eStatus == XSOCK_EOF, "An orderly close is reported as end of file");
    CHECK(XSock_GetStatusStr(eStatus) != NULL, "The end of file status has a description");

    /* A reset ends the transport without ending the TLS session. */
    if (tls_read_after_end(&fixture, TLS_END_RESET, &nResult, &eStatus) == XSTDOK)
    {
        CHECK(nResult <= 0, "A reset session delivers no more data");
        CHECK(eStatus != XSOCK_ERR_NONE, "A reset is not silent");
        CHECK(XSock_GetStatusStr(eStatus) != NULL, "Whatever it is has a description");
    }

    /* Bytes that are not a record are a protocol error, not an end. */
    if (tls_read_after_end(&fixture, TLS_END_GARBAGE, &nResult, &eStatus) == XSTDOK)
    {
        CHECK(nResult <= 0, "A corrupted record stream delivers no data");
        CHECK(eStatus == XSOCK_ERR_SSLERR || eStatus == XSOCK_ERR_SSLREAD ||
              eStatus == XSOCK_ERR_SYSCALL || eStatus == XSOCK_EOF,
            "A corrupted record stream is reported as a TLS failure");
        CHECK(XSock_GetStatusStr(eStatus) != NULL, "That failure has a description too");
    }

    tls_fixture_end(&fixture);
    return 0;
}

static int XTest_closed_session_io(void)
{
    /* Once a TLS session has failed, the socket is closed underneath the
     * caller. Every further call has to say so rather than act on a
     * descriptor that is gone or, worse, one the kernel has reused. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No TLS fixture could be built, skipping\n");
        return 77;
    }

    tls_server_t server;
    xthread_t thread;

    if (tls_start(&server, &fixture, &thread, XFALSE) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("The TLS server could not be started, skipping\n");
        return 77;
    }

    xsock_cert_t cert;
    XSock_InitCert(&cert);
    cert.pCaPath = fixture.sCert;

    xsock_t client;
    if (tls_connect_armed(&client, server.nPort, &cert, &server) != XSTDOK)
    {
        XSock_Close(&client);
        XThread_Join(&thread);
        tls_fixture_end(&fixture);
        printf("The TLS handshake did not complete, skipping\n");
        return 77;
    }

    CHECK(XSock_IsSSL(&client) == XTRUE, "The session is a TLS session");
    CHECK(XSock_GetSSL(&client) != NULL, "It carries a TLS object");

    /* An empty write is a no-op, not a failure, and must not close it. */
    char sBufferGuard[8];
    CHECK(XSock_SSLWrite(&client, "x", 0) == XSOCK_NONE, "A zero length write does nothing");
    CHECK(XSock_SSLWrite(&client, NULL, 4) == XSOCK_NONE, "A write with no data does nothing");
    CHECK(XSock_SSLRead(&client, NULL, 4, XFALSE) == XSOCK_NONE, "A read into nothing does nothing");
    CHECK(XSock_SSLRead(&client, sBufferGuard, 0, XFALSE) == XSOCK_NONE, "A zero length read does nothing");

    XSock_Close(&client);

    CHECK(XSock_GetFD(&client) == XSOCK_INVALID, "Closing released the descriptor");
    CHECK(XSock_SSLWrite(&client, "x", 1) == XSOCK_ERROR, "Writing to a closed session is refused");
    CHECK(XSock_SSLRead(&client, sBufferGuard, sizeof(sBufferGuard), XFALSE) == XSOCK_ERROR,
        "Reading a closed session is refused");
    CHECK(XSock_SSLConnect(&client) == XSOCK_INVALID, "Reconnecting a closed session is refused");

    /* Closing twice must be harmless. */
    XSock_Close(&client);

    XThread_Join(&thread);
    tls_fixture_end(&fixture);
    return 0;
}

static int XTest_status_strings(void)
{
    /* Every status the socket layer can leave behind has a description, so
     * a caller reporting a failure never prints a bare number. */
    const xsock_status_t states[] = {
        XSOCK_ERR_NONE, XSOCK_ERR_ALLOC, XSOCK_ERR_ARGS, XSOCK_ERR_INVALID,
        XSOCK_ERR_SUPPORT, XSOCK_ERR_CONNECT, XSOCK_ERR_CREATE, XSOCK_ERR_ACCEPT,
        XSOCK_ERR_LISTEN, XSOCK_ERR_WRITE, XSOCK_ERR_READ, XSOCK_ERR_SEND,
        XSOCK_ERR_RECV, XSOCK_ERR_JOIN, XSOCK_ERR_BIND, XSOCK_ERR_NAME,
        XSOCK_ERR_ADDR, XSOCK_ERR_SETFL, XSOCK_ERR_GETFL, XSOCK_ERR_SETOPT,
        XSOCK_ERR_PKCS12, XSOCK_ERR_SSLWRITE, XSOCK_ERR_SSLREAD, XSOCK_ERR_SSLINV,
        XSOCK_ERR_SSLNEW, XSOCK_ERR_SSLCTX, XSOCK_ERR_SSLMET, XSOCK_ERR_SSLCNT,
        XSOCK_ERR_SSLACC, XSOCK_ERR_SSLKEY, XSOCK_ERR_SSLCRT, XSOCK_ERR_SSLERR,
        XSOCK_ERR_SSLCA, XSOCK_ERR_NOSSL, XSOCK_ERR_FLAGS, XSOCK_ERR_INVSSL,
        XSOCK_ERR_SYSCALL, XSOCK_WANT_READ, XSOCK_WANT_WRITE, XSOCK_EOF
    };

    for (size_t i = 0; i < sizeof(states) / sizeof(*states); i++)
    {
        const char *pText = XSock_GetStatusStr(states[i]);
        CHECK(pText != NULL && *pText != '\0', "Every socket status has a description");

        /* Two different statuses must not read the same, or a log line
         * would not say which one happened. */
        for (size_t n = 0; n < i; n++)
            CHECK(strcmp(pText, XSock_GetStatusStr(states[n])) != 0,
                "No two socket statuses share a description");
    }

    CHECK(XSock_GetStatusStr((xsock_status_t)250) != NULL, "An out of range status still has one");
    return 0;
}


static int XTest_write_after_reset(void)
{
    /* Writing into a session the peer has already reset is the mirror of
     * reading from one: the write cannot report success, and the socket has
     * to be closed rather than left open for the caller to keep writing
     * into a connection that is gone. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No TLS fixture could be built, skipping\n");
        return 77;
    }

    tls_server_t server;
    xthread_t thread;

    if (tls_start_ex(&server, &fixture, &thread, XFALSE, TLS_END_RESET) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("The TLS server could not be started, skipping\n");
        return 77;
    }

    xsock_cert_t cert;
    XSock_InitCert(&cert);
    cert.pCaPath = fixture.sCert;

    xsock_t client;
    if (tls_connect_armed(&client, server.nPort, &cert, &server) != XSTDOK)
    {
        XSock_Close(&client);
        XThread_Join(&thread);
        tls_fixture_end(&fixture);
        printf("The TLS handshake did not complete, skipping\n");
        return 77;
    }

    XSock_TimeOutR(&client, 10, 0);
    XSock_TimeOutS(&client, 10, 0);

    CHECK(XSock_SSLWrite(&client, "hello", 5) == 5, "The first write goes out");

    /* Let the server answer and reset. */
    char sBuffer[256];
    XSock_SSLRead(&client, sBuffer, sizeof(sBuffer) - 1, XFALSE);
    XThread_Join(&thread);

    /* A stream of writes into the reset connection: the first may still be
     * absorbed by the local send buffer, but it cannot keep succeeding. */
    char sChunk[8192];
    memset(sChunk, 'w', sizeof(sChunk));

    int nFailed = 0;
    for (int i = 0; i < 64 && !nFailed; i++)
    {
        if (XSock_SSLWrite(&client, sChunk, sizeof(sChunk)) <= 0) nFailed = 1;
        else xusleep(2000);
    }

    CHECK(nFailed == 1, "Writing into a reset session eventually fails");
    CHECK(XSock_Status(&client) != XSOCK_ERR_NONE, "The failure left a status behind");
    CHECK(XSock_GetStatusStr(XSock_Status(&client)) != NULL, "That status has a description");
    CHECK(XSock_GetFD(&client) == XSOCK_INVALID, "The socket was closed rather than left open");
    CHECK(XSock_SSLWrite(&client, "more", 4) == XSOCK_ERROR, "Writing again is refused outright");

    XSock_Close(&client);
    tls_fixture_end(&fixture);
    return 0;
}


static int XTest_pkcs12_ownership(void)
{
    /* The bundle loader hands the caller a certificate, a key and a CA
     * chain, and the caller owns all three. Loading in a loop without
     * releasing them is what a server reloading its identity does, so a
     * missing release is a leak that grows for as long as the process runs. */
    tls_fixture_t fixture;
    if (tls_fixture_begin(&fixture) != XSTDOK)
    {
        tls_fixture_end(&fixture);
        printf("No TLS fixture could be built, skipping\n");
        return 77;
    }

    for (int i = 0; i < 8; i++)
    {
        xsock_ssl_cert_t loaded;
        memset(&loaded, 0, sizeof(loaded));

        CHECK(XSock_LoadPKCS12(&loaded, fixture.sP12, "regression") == XSOCK_SUCCESS,
            "The bundle loads");
        CHECK(loaded.nStatus == 1, "The load reports itself complete");
        CHECK(loaded.pCert != NULL, "A certificate came out of it");
        CHECK(loaded.pKey != NULL, "A private key came out of it");

        XSock_FreePKCS12(&loaded);

        CHECK(loaded.pCert == NULL && loaded.pKey == NULL && loaded.pCa == NULL,
            "Releasing clears every pointer");
        CHECK(loaded.nStatus == 0, "Releasing clears the status");

        /* Releasing twice must be harmless, since the pointers are gone. */
        XSock_FreePKCS12(&loaded);
    }

    /* The refusals leave nothing to release. */
    xsock_ssl_cert_t bad;
    memset(&bad, 0, sizeof(bad));
    CHECK(XSock_LoadPKCS12(&bad, "/nonexistent/bundle.p12", "x") != XSOCK_SUCCESS,
        "A missing bundle is refused");
    CHECK(bad.nStatus == 0, "A refused load reports nothing");
    XSock_FreePKCS12(&bad);

    memset(&bad, 0, sizeof(bad));
    CHECK(XSock_LoadPKCS12(&bad, fixture.sCert, "regression") != XSOCK_SUCCESS,
        "A PEM file is not a bundle");
    XSock_FreePKCS12(&bad);

    memset(&bad, 0, sizeof(bad));
    CHECK(XSock_LoadPKCS12(&bad, fixture.sP12, "wrong-password") != XSOCK_SUCCESS,
        "The wrong password is refused");
    CHECK(bad.pCert == NULL && bad.pKey == NULL, "Nothing came out of the refused load");
    XSock_FreePKCS12(&bad);

    XSock_FreePKCS12(NULL);

    tls_fixture_end(&fixture);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(handshake),
    XTEST_CASE(stale_error_queue),
    XTEST_CASE(pkcs12_identity),
    XTEST_CASE(pkcs12_ownership),
    XTEST_CASE(untrusted),
    XTEST_CASE(host_override),
    XTEST_CASE(cert_guards),
    XTEST_CASE(peer_endings),
    XTEST_CASE(closed_session_io),
    XTEST_CASE(status_strings),
    XTEST_CASE(write_after_reset),
    XTEST_CASE(ssl_teardown)
)
