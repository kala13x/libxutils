/* libxutils: the HTTP client exchange surface, against a local server.
 *
 * Every entry point from the raw socket exchange up to the one-line
 * convenience wrapper ends in the same place, so each is driven against a
 * server thread that answers on loopback. That keeps the test hermetic and
 * lets it assert on the exact status line and body the server sent.
 */

#include "test.h"
#include "http.h"
#include "sock.h"
#include "addr.h"
#include "thread.h"
#include "sync.h"
#include "xtime.h"
#include <unistd.h>

typedef struct {
    uint16_t nPort;
    xatomic_t nReady;       /* 1 once bound and listening, 2 on failure */
    xatomic_t nServed;      /* Number of requests answered */

    int nStatusCode;        /* Status to answer with */
    const char *pBody;      /* Body to answer with */
    xbool_t bDropRequest;   /* Close without answering at all */
    xbool_t bHalfAnswer;    /* Send a header with no body it promised */
    xbool_t bStream;        /* Answer with no length and end by closing */
    xbool_t bWithLength;    /* Announce the generated body's length */
    size_t nFillBody;       /* Answer with this many generated body bytes */
    int nRequests;          /* How many requests to serve before exiting */

    /* What the last request looked like on the wire. */
    char sMethod[16];
    char sUri[256];
    char sBody[256];
    size_t nBodyLen;
    char sAuth[256];
} http_server_t;

/* Serves a fixed number of requests on an ephemeral loopback port. */
static void *http_serve(void *pContext)
{
    http_server_t *pServer = (http_server_t*)pContext;
    int nListen = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (nListen < 0) { XSYNC_ATOMIC_SET(&pServer->nReady, 2); return NULL; }

    int nReuse = 1;
    setsockopt(nListen, SOL_SOCKET, SO_REUSEADDR, &nReuse, sizeof(nReuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    socklen_t nLen = sizeof(addr);
    if (bind(nListen, (struct sockaddr*)&addr, nLen) < 0 ||
        getsockname(nListen, (struct sockaddr*)&addr, &nLen) < 0 ||
        listen(nListen, 8) < 0)
    {
        close(nListen);
        XSYNC_ATOMIC_SET(&pServer->nReady, 2);
        return NULL;
    }

    pServer->nPort = ntohs(addr.sin_port);
    XSYNC_ATOMIC_SET(&pServer->nReady, 1);

    for (int nServed = 0; nServed < pServer->nRequests; nServed++)
    {
        struct timeval timeout = {10, 0};
        setsockopt(nListen, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        int nPeer = (int)accept(nListen, NULL, NULL);
        if (nPeer < 0) break;

        setsockopt(nPeer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        /* Read until the header terminator, then whatever body was
         * announced, so the request can be asserted on. */
        char sRequest[8192];
        size_t nUsed = 0;
        int nHeaderEnd = -1;

        while (nUsed < sizeof(sRequest) - 1)
        {
            ssize_t nRead = recv(nPeer, &sRequest[nUsed], sizeof(sRequest) - 1 - nUsed, 0);
            if (nRead <= 0) break;
            nUsed += (size_t)nRead;
            sRequest[nUsed] = '\0';

            char *pEnd = strstr(sRequest, "\r\n\r\n");
            if (pEnd == NULL) continue;
            nHeaderEnd = (int)(pEnd - sRequest) + 4;

            const char *pLen = strcasestr(sRequest, "content-length:");
            size_t nExpect = pLen ? (size_t)atol(pLen + 15) : 0;
            if (nUsed >= (size_t)nHeaderEnd + nExpect) break;
        }

        if (nHeaderEnd > 0)
        {
            sscanf(sRequest, "%15s %255s", pServer->sMethod, pServer->sUri);

            const char *pAuth = strcasestr(sRequest, "authorization:");
            if (pAuth != NULL)
            {
                pAuth += 14;
                while (*pAuth == ' ') pAuth++;
                size_t i = 0;
                while (i < sizeof(pServer->sAuth) - 1 && pAuth[i] != '\r' && pAuth[i] != '\0')
                { pServer->sAuth[i] = pAuth[i]; i++; }
                pServer->sAuth[i] = '\0';
            }

            pServer->nBodyLen = nUsed - (size_t)nHeaderEnd;
            if (pServer->nBodyLen >= sizeof(pServer->sBody)) pServer->nBodyLen = sizeof(pServer->sBody) - 1;
            memcpy(pServer->sBody, &sRequest[nHeaderEnd], pServer->nBodyLen);
            pServer->sBody[pServer->nBodyLen] = '\0';
        }

        if (pServer->bDropRequest) { close(nPeer); continue; }

        char sResponse[1024];
        size_t nBodyLen = pServer->pBody ? strlen(pServer->pBody) : 0;
        int nLength;

        if (pServer->nFillBody && pServer->bWithLength)
        {
            /* The length delimited form: the reader knows in advance how
             * much body to expect, which is a different loop in the client
             * from the one that reads until the connection ends. */
            nLength = snprintf(sResponse, sizeof(sResponse),
                "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n"
                "X-Served-By: regression\r\nConnection: close\r\n\r\n",
                pServer->nStatusCode, XHTTP_GetCodeStr(pServer->nStatusCode), pServer->nFillBody);

            if (nLength > 0) send(nPeer, sResponse, (size_t)nLength, MSG_NOSIGNAL);

            char sChunk[1024];
            memset(sChunk, 'L', sizeof(sChunk));

            size_t nLeft = pServer->nFillBody;
            while (nLeft > 0)
            {
                size_t nSend = nLeft < sizeof(sChunk) ? nLeft : sizeof(sChunk);
                if (send(nPeer, sChunk, nSend, MSG_NOSIGNAL) <= 0) break;
                nLeft -= nSend;
            }

            XSYNC_ATOMIC_ADD(&pServer->nServed, 1);
            close(nPeer);
            continue;
        }

        if (pServer->bStream || pServer->nFillBody)
        {
            /* No length at all: the end of the body is the end of the
             * connection, which is what HTTP/1.0 style answers do. */
            nLength = snprintf(sResponse, sizeof(sResponse),
                "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\n"
                "X-Served-By: regression\r\nConnection: close\r\n\r\n",
                pServer->nStatusCode, XHTTP_GetCodeStr(pServer->nStatusCode));

            if (nLength > 0) send(nPeer, sResponse, (size_t)nLength, MSG_NOSIGNAL);

            if (pServer->nFillBody)
            {
                char sChunk[1024];
                memset(sChunk, 'z', sizeof(sChunk));

                size_t nLeft = pServer->nFillBody;
                while (nLeft > 0)
                {
                    size_t nSend = nLeft < sizeof(sChunk) ? nLeft : sizeof(sChunk);
                    if (send(nPeer, sChunk, nSend, MSG_NOSIGNAL) <= 0) break;
                    nLeft -= nSend;
                }
            }
            else if (nBodyLen)
            {
                send(nPeer, pServer->pBody, nBodyLen, MSG_NOSIGNAL);
            }

            XSYNC_ATOMIC_ADD(&pServer->nServed, 1);
            close(nPeer);
            continue;
        }

        nLength = snprintf(sResponse, sizeof(sResponse),
            "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n"
            "X-Served-By: regression\r\nConnection: close\r\n\r\n",
            pServer->nStatusCode, XHTTP_GetCodeStr(pServer->nStatusCode),
            pServer->bHalfAnswer ? nBodyLen + 64 : nBodyLen);

        if (nLength > 0) send(nPeer, sResponse, (size_t)nLength, MSG_NOSIGNAL);
        if (nBodyLen && !pServer->bHalfAnswer) send(nPeer, pServer->pBody, nBodyLen, MSG_NOSIGNAL);

        XSYNC_ATOMIC_ADD(&pServer->nServed, 1);
        close(nPeer);
    }

    close(nListen);
    return NULL;
}

/* Configures and starts the server, waiting for it to publish its port. */
static int http_start(http_server_t *pServer, xthread_t *pThread, int nRequests)
{
    memset(pServer, 0, sizeof(*pServer));
    pServer->nStatusCode = 200;
    pServer->pBody = "hello from the regression server";
    pServer->nRequests = nRequests;

    if (XThread_Create(pThread, http_serve, pServer, XFALSE) != XSTDOK) return XSTDERR;
    for (int i = 0; i < 2000 && !XSYNC_ATOMIC_GET(&pServer->nReady); i++) xusleep(5000);
    if (XSYNC_ATOMIC_GET(&pServer->nReady) == 1) return XSTDOK;

    XThread_Join(pThread);
    return XSTDERR;
}

static int XTest_connect(void)
{
    /* The connect helper resolves a parsed link and leaves an open socket
     * the caller can exchange on. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/connect", server.nPort);

    xlink_t link;
    CHECK(XLink_Parse(&link, sUrl) == XSTDOK, "The link parses");
    CHECK(link.nPort == server.nPort, "The link carries the server port");

    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, link.sUri, "1.1") > 0, "The request initializes");

    xsock_t sock;
    CHECK(XHTTP_Connect(&http, &sock, &link) == XHTTP_CONNECTED, "The client connects to the link");
    CHECK(XSock_IsOpen(&sock) == XTRUE, "The connection is open");
    CHECK(XSock_GetPort(&sock) == server.nPort, "The socket is connected to the server port");

    /* The open socket can then carry an exchange. Exchange writes the bytes
     * the request already holds, so unlike Perform it needs the request to
     * have been assembled first. */
    CHECK(XHTTP_Assemble(&http, NULL, 0) != NULL, "The request assembles before it is sent");

    xhttp_t response;
    CHECK(XHTTP_Init(&response, XHTTP_DUMMY, 0) >= 0, "The response initializes");
    CHECK(XHTTP_Exchange(&http, &response, &sock) == XHTTP_COMPLETE, "The exchange completes");
    CHECK(response.nStatusCode == 200, "The server answered with its status");
    CHECK(XHTTP_GetBodySize(&response) == strlen(server.pBody), "The body arrived whole");
    CHECK(memcmp(XHTTP_GetBody(&response), server.pBody, XHTTP_GetBodySize(&response)) == 0,
        "The body arrived unchanged");
    CHECK(strcmp(XHTTP_GetHeader(&response, "X-Served-By"), "regression") == 0,
        "The response headers arrived");

    XSock_Close(&sock);
    XHTTP_Clear(&response);
    XHTTP_Clear(&http);
    XThread_Join(&thread);

    CHECK(strcmp(server.sMethod, "GET") == 0, "The server saw the method that was sent");
    CHECK(strcmp(server.sUri, "/connect") == 0, "The server saw the target that was sent");
    return 0;
}

static int XTest_link_exchange(void)
{
    /* The link form connects, exchanges and closes in one call. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }
    server.pBody = "link exchange body";

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/link", server.nPort);

    xlink_t link;
    CHECK(XLink_Parse(&link, sUrl) == XSTDOK, "The link parses");

    xhttp_t request, response;
    CHECK(XHTTP_InitRequest(&request, XHTTP_GET, link.sUri, "1.1") > 0, "The request initializes");
    CHECK(XHTTP_AddHeader(&request, "Host", "127.0.0.1") > 0, "A host header is added");
    CHECK(XHTTP_Assemble(&request, NULL, 0) != NULL, "The request assembles");
    CHECK(XHTTP_Init(&response, XHTTP_DUMMY, 0) >= 0, "The response initializes");

    CHECK(XHTTP_LinkExchange(&request, &response, &link) == XHTTP_COMPLETE, "The link exchange completes");
    CHECK(response.nStatusCode == 200, "The server answered with its status");
    CHECK(XHTTP_GetBodySize(&response) == strlen(server.pBody), "The body arrived whole");
    CHECK(memcmp(XHTTP_GetBody(&response), "link exchange body", strlen("link exchange body")) == 0,
        "The link response contains the exact expected bytes");

    XHTTP_Clear(&response);
    XHTTP_Clear(&request);
    XThread_Join(&thread);

    CHECK(strcmp(server.sUri, "/link") == 0, "The server saw the link target");
    CHECK(strcmp(server.sMethod, "GET") == 0 && !server.nBodyLen, "The server received the expected method and empty body");
    return 0;
}

static int XTest_easy_exchange(void)
{
    /* The easy form takes the URL as a string and parses it itself. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }
    server.pBody = "easy exchange body";

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/easy?q=1", server.nPort);

    xhttp_t request, response;
    CHECK(XHTTP_InitRequest(&request, XHTTP_GET, "/easy?q=1", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_Assemble(&request, NULL, 0) != NULL, "The request assembles");
    CHECK(XHTTP_Init(&response, XHTTP_DUMMY, 0) >= 0, "The response initializes");

    CHECK(XHTTP_EasyExchange(&request, &response, sUrl) == XHTTP_COMPLETE, "The easy exchange completes");
    CHECK(response.nStatusCode == 200, "The server answered with its status");
    CHECK(XHTTP_GetBodySize(&response) == strlen(server.pBody), "The easy response has the exact expected size");
    CHECK(memcmp(XHTTP_GetBody(&response), server.pBody, strlen(server.pBody)) == 0,
        "The easy exchange body arrived unchanged");

    XHTTP_Clear(&response);
    XHTTP_Clear(&request);
    XThread_Join(&thread);

    CHECK(strcmp(server.sUri, "/easy?q=1") == 0, "The query string reached the server");
    CHECK(strcmp(server.sMethod, "GET") == 0 && !server.nBodyLen, "The easy request has the expected method and empty body");
    return 0;
}

static int XTest_perform(void)
{
    /* Perform sends a body and reuses the same handle for the answer, so
     * the request fields are replaced by the response fields in place. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }
    server.nStatusCode = 201;
    server.pBody = "created";

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/perform", server.nPort);

    xlink_t link;
    CHECK(XLink_Parse(&link, sUrl) == XSTDOK, "The link parses");

    const uint8_t body[] = {'p', 'o', 's', 't', 'e', 'd', 0x00, 0x7f};
    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_POST, link.sUri, "1.1") > 0, "The request initializes");
    CHECK(XHTTP_AddHeader(&http, "Content-Type", "application/octet-stream") > 0, "A content type is added");

    CHECK(XHTTP_LinkPerform(&http, &link, body, sizeof(body)) == XHTTP_COMPLETE, "The perform completes");
    CHECK(http.nStatusCode == 201, "The handle now carries the response status");
    CHECK(http.eType == XHTTP_RESPONSE, "The handle is now a response");
    CHECK(XHTTP_GetBodySize(&http) == strlen(server.pBody), "The response body arrived whole");
    CHECK(memcmp(XHTTP_GetBody(&http), "created", 7) == 0, "The creation response has the exact expected bytes");

    XHTTP_Clear(&http);
    XThread_Join(&thread);

    CHECK(strcmp(server.sMethod, "POST") == 0, "The server saw a post");
    CHECK(strcmp(server.sUri, "/perform") == 0, "The post reached the expected target");
    CHECK(server.nBodyLen == sizeof(body), "The server received the whole body");
    CHECK(memcmp(server.sBody, body, sizeof(body)) == 0, "Binary body bytes reached the server unchanged");
    return 0;
}

static int XTest_easy_perform(void)
{
    /* The easy and solo forms take the URL directly; solo also takes the
     * method, so the caller never builds a request at all. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 2) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }
    server.pBody = "easy perform body";

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/easy-perform", server.nPort);

    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_PUT, "/easy-perform", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_EasyPerform(&http, sUrl, (const uint8_t*)"put-body", 8) == XHTTP_COMPLETE,
        "The easy perform completes");
    CHECK(http.nStatusCode == 200, "The easy perform carries the response status");
    CHECK(XHTTP_GetBodySize(&http) == strlen("easy perform body") &&
        !memcmp(XHTTP_GetBody(&http), "easy perform body", strlen("easy perform body")), "The PUT response bytes are exact");
    XHTTP_Clear(&http);

    CHECK(strcmp(server.sMethod, "PUT") == 0, "The server saw a put");
    CHECK(server.nBodyLen == 8, "The server received the put body");
    CHECK(!memcmp(server.sBody, "put-body", 8) && !strcmp(server.sUri, "/easy-perform"), "The PUT target and body are exact");

    /* The solo form builds the request from the method and URL alone. */
    xhttp_t solo;
    CHECK(XHTTP_SoloPerform(&solo, XHTTP_DELETE, sUrl, NULL, 0) == XHTTP_COMPLETE,
        "The solo perform completes");
    CHECK(solo.nStatusCode == 200, "The solo perform carries the response status");
    CHECK(XHTTP_GetBodySize(&solo) == strlen(server.pBody), "The solo perform received the body");
    CHECK(!memcmp(XHTTP_GetBody(&solo), "easy perform body", strlen("easy perform body")), "The DELETE response bytes are exact");
    XHTTP_Clear(&solo);

    XThread_Join(&thread);
    CHECK(strcmp(server.sMethod, "DELETE") == 0, "The server saw a delete");
    CHECK(!strcmp(server.sUri, "/easy-perform") && !server.nBodyLen, "The DELETE target and empty body are exact");
    CHECK(XSYNC_ATOMIC_GET(&server.nServed) == 2, "Both requests were answered");
    return 0;
}

static int XTest_auth_and_status(void)
{
    /* Credentials set on the request reach the server, and a non-2xx
     * status is delivered to the caller rather than turned into an error. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }
    server.nStatusCode = 404;
    server.pBody = "not here";

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/secret", server.nPort);

    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/secret", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_SetAuthBasic(&http, "user", "pass") > 0, "Basic auth is set");

    CHECK(XHTTP_EasyPerform(&http, sUrl, NULL, 0) == XHTTP_COMPLETE, "The exchange completes");
    CHECK(http.nStatusCode == 404, "A not found status reaches the caller");
    CHECK(XHTTP_IsSuccessCode(&http) == XFALSE, "A not found status is not a success");
    CHECK(XHTTP_GetBodySize(&http) == strlen(server.pBody), "The error body reaches the caller");
    CHECK(!memcmp(XHTTP_GetBody(&http), "not here", 8), "The error response contains the expected explanation");
    XHTTP_Clear(&http);

    XThread_Join(&thread);
    CHECK(strncmp(server.sAuth, "Basic ", 6) == 0, "The server saw the basic scheme");
    CHECK(strcmp(&server.sAuth[6], "dXNlcjpwYXNz") == 0, "The server saw the encoded credentials");
    CHECK(!strcmp(server.sMethod, "GET") && !strcmp(server.sUri, "/secret") && !server.nBodyLen,
        "Authentication accompanied the expected request");
    return 0;
}

static int XTest_client_failures(void)
{
    /* A URL that cannot be reached is reported rather than hanging, and a
     * server that answers nothing is reported too. */
    xhttp_t http;

    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_EasyPerform(&http, "http://127.0.0.1:1/", NULL, 0) != XHTTP_COMPLETE,
        "A closed port is reported");
    XHTTP_Clear(&http);

    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_EasyPerform(&http, "http://no.such.host.invalid/", NULL, 0) != XHTTP_COMPLETE,
        "An unresolvable host is reported");
    XHTTP_Clear(&http);

    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_EasyPerform(&http, "not a url at all", NULL, 0) != XHTTP_COMPLETE,
        "An unparsable link is reported");
    XHTTP_Clear(&http);

    /* A server that accepts and then closes without answering. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }
    server.bDropRequest = XTRUE;

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/dropped", server.nPort);

    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/dropped", "1.1") > 0, "The request initializes");
    uint64_t nStart = XTime_GetMs();
    CHECK(XHTTP_EasyPerform(&http, sUrl, NULL, 0) != XHTTP_COMPLETE, "A dropped request is reported");
    CHECK(XTime_GetMs() - nStart < 60000, "A dropped request does not hang the client");
    XHTTP_Clear(&http);

    XThread_Join(&thread);
    return 0;
}

static int XTest_truncated_answer(void)
{
    /* A server that promises more body than it sends must not leave the
     * client claiming a complete message. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }
    server.bHalfAnswer = XTRUE;
    server.pBody = "short";

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/truncated", server.nPort);

    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/truncated", "1.1") > 0, "The request initializes");

    uint64_t nStart = XTime_GetMs();
    xhttp_status_t eStatus = XHTTP_EasyPerform(&http, sUrl, NULL, 0);
    CHECK(XTime_GetMs() - nStart < 60000, "A truncated answer does not hang the client");
    CHECK(eStatus != XHTTP_COMPLETE || XHTTP_GetBodySize(&http) < strlen(server.pBody) + 64,
        "A truncated answer is never reported as a complete body");
    XHTTP_Clear(&http);

    XThread_Join(&thread);
    return 0;
}


/* ---------------- callbacks and length-less answers ---------------- */

typedef struct {
    int nStatusCbs;
    int nHeaderCbs;
    int nContentCbs;
    int nWriteCbs;
    int nErrorCbs;
    size_t nStreamed;
    size_t nHeaderLen;      /* Length the header callback was handed */
    size_t nHeaderSeen;     /* Bytes that had actually arrived by then */
    int nAnswer;            /* What every content callback returns */
    char sLastStatus[128];
    xhttp_status_t eLastStatus;
} http_cb_t;

static int http_client_cb(xhttp_t *pHttp, xhttp_ctx_t *pCtx)
{
    http_cb_t *pTest = (http_cb_t*)pHttp->pUserCtx;

    if (pCtx->eCbType == XHTTP_STATUS)
    {
        pTest->nStatusCbs++;
        pTest->eLastStatus = pCtx->eStatus;
        if (pCtx->pData != NULL && pCtx->nLength < sizeof(pTest->sLastStatus))
        {
            memcpy(pTest->sLastStatus, pCtx->pData, pCtx->nLength);
            pTest->sLastStatus[pCtx->nLength] = '\0';
        }
        return XSTDUSR;
    }

    if (pCtx->eCbType == XHTTP_ERROR) { pTest->nErrorCbs++; return XSTDUSR; }
    if (pCtx->eCbType == XHTTP_WRITE) { pTest->nWriteCbs++; return XSTDUSR; }
    if (pCtx->eCbType == XHTTP_READ_HDR)
    {
        /* A callback that wrote this out verbatim would publish whatever
         * the length covers, so it must not cover more than arrived. */
        pTest->nHeaderCbs++;
        pTest->nHeaderLen = pCtx->nLength;
        pTest->nHeaderSeen = pHttp->rawData.nUsed;
        return XSTDUSR;
    }

    if (pCtx->eCbType == XHTTP_READ_CNT)
    {
        pTest->nContentCbs++;
        pTest->nStreamed += pCtx->nLength;
        return pTest->nAnswer;
    }

    return XSTDUSR;
}

static int XTest_stream_callback(void)
{
    /* A content callback that answers XSTDOK takes the bytes itself, so the
     * handle must not also buffer them. That is what lets a caller stream a
     * large download straight to disk instead of into memory. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }

    server.nFillBody = 40000;

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/stream", server.nPort);

    http_cb_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.nAnswer = XSTDOK;

    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/stream", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_SetCallback(&http, http_client_cb, &cb,
        XHTTP_STATUS | XHTTP_ERROR | XHTTP_READ_HDR | XHTTP_READ_CNT | XHTTP_WRITE) > 0,
        "The callbacks are installed");

    xhttp_status_t eStatus = XHTTP_EasyPerform(&http, sUrl, NULL, 0);
    CHECK(eStatus == XHTTP_COMPLETE, "A length-less answer completes when the peer closes");
    CHECK(http.nStatusCode == 200, "The status line was parsed");

    CHECK(cb.nStatusCbs > 0, "The status callback ran");
    CHECK(cb.nHeaderCbs > 0, "The header callback ran");
    CHECK(cb.nContentCbs > 0, "The content callback ran");
    CHECK(cb.nStreamed == server.nFillBody, "Every body byte reached the content callback");

    /* The header read takes a whole socket buffer, so the first body bytes
     * usually arrive with it and stay in the handle. Everything read after
     * that is handed over and dropped, which is the point of claiming it:
     * the handle must not grow with the download. */
    CHECK(XHTTP_GetBodySize(&http) < server.nFillBody / 2,
        "A claimed body is not accumulated in the handle");
    CHECK(cb.nHeaderLen <= cb.nHeaderSeen,
        "The header callback is given only bytes that arrived");

    XHTTP_Clear(&http);
    XThread_Join(&thread);
    return 0;
}

static int XTest_buffered_stream(void)
{
    /* The same answer with a callback that only observes: the bytes are
     * still buffered, so the handle holds the whole body at the end. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }

    server.bStream = XTRUE;
    server.pBody = "a body with no announced length at all";

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/buffered", server.nPort);

    http_cb_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.nAnswer = XSTDUSR;

    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/buffered", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_SetCallback(&http, http_client_cb, &cb, XHTTP_READ_CNT | XHTTP_STATUS) > 0,
        "The callbacks are installed");

    CHECK(XHTTP_EasyPerform(&http, sUrl, NULL, 0) == XHTTP_COMPLETE, "The exchange completes");
    CHECK(XHTTP_GetBodySize(&http) == strlen(server.pBody), "The whole body was buffered");
    CHECK(memcmp(XHTTP_GetBody(&http), server.pBody, strlen(server.pBody)) == 0,
        "The buffered body is the one that was sent");
    CHECK(http.nComplete == XTRUE, "The handle is marked complete");

    XHTTP_Clear(&http);
    XThread_Join(&thread);
    return 0;
}

static int XTest_stopped_stream(void)
{
    /* A content callback answering XSTDNON says it has everything it wants.
     * The exchange completes there rather than reading to the end. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }

    server.nFillBody = 200000;

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/stop", server.nPort);

    http_cb_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.nAnswer = XSTDNON;

    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/stop", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_SetCallback(&http, http_client_cb, &cb, XHTTP_READ_CNT) > 0, "The callback is installed");

    CHECK(XHTTP_EasyPerform(&http, sUrl, NULL, 0) == XHTTP_COMPLETE, "Stopping early still completes");
    CHECK(cb.nContentCbs == 1, "The callback stopped after its first answer");
    CHECK(cb.nStreamed < server.nFillBody, "It did not have to read the whole body first");

    XHTTP_Clear(&http);
    XThread_Join(&thread);
    return 0;
}

static int XTest_terminated_stream(void)
{
    /* A content callback answering XSTDERR aborts the exchange, and the
     * caller is told it was terminated rather than that it completed. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }

    server.nFillBody = 200000;

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/abort", server.nPort);

    http_cb_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.nAnswer = XSTDERR;

    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/abort", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_SetCallback(&http, http_client_cb, &cb, XHTTP_READ_CNT) > 0, "The callback is installed");

    CHECK(XHTTP_EasyPerform(&http, sUrl, NULL, 0) == XHTTP_TERMINATED, "A refusing callback terminates it");
    CHECK(cb.nContentCbs == 1, "It stopped on the first answer");

    XHTTP_Clear(&http);
    XThread_Join(&thread);
    return 0;
}

static int XTest_content_limit(void)
{
    /* A body larger than the caller's limit is refused rather than
     * buffered, which is what stops a hostile answer from being an
     * unbounded allocation. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }

    server.nFillBody = 300000;

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/big", server.nPort);

    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/big", "1.1") > 0, "The request initializes");
    http.nContentMax = 8192;

    xhttp_status_t eStatus = XHTTP_EasyPerform(&http, sUrl, NULL, 0);
    CHECK(eStatus == XHTTP_BIGCNT, "A body past the limit is refused");
    CHECK(XHTTP_GetBodySize(&http) <= 300000, "Nothing past what was read was kept");
    CHECK(XHTTP_GetStatusStr(XHTTP_BIGCNT) != NULL, "The refusal has a description");

    XHTTP_Clear(&http);
    XThread_Join(&thread);
    return 0;
}

static int XTest_callback_guards(void)
{
    /* Installing callbacks has to answer on a handle that is not there, and
     * a handle with no callback must not be called back into. */
    http_cb_t cb;
    memset(&cb, 0, sizeof(cb));

    CHECK(XHTTP_SetCallback(NULL, http_client_cb, &cb, XHTTP_READ_CNT) < 0,
        "Installing on a missing handle is rejected");

    xhttp_t http;
    CHECK(XHTTP_Init(&http, XHTTP_DUMMY, 0) >= 0, "The handle initializes");
    CHECK(http.callback == NULL, "A fresh handle has no callback");
    CHECK(http.nCbTypes == 0, "A fresh handle subscribes to nothing");

    CHECK(XHTTP_SetCallback(&http, http_client_cb, &cb, XHTTP_READ_CNT) > 0, "A callback is installed");
    CHECK(http.callback == http_client_cb, "The callback is the one installed");
    CHECK(http.pUserCtx == &cb, "The context is the one installed");
    CHECK(http.nCbTypes == XHTTP_READ_CNT, "Only the requested types are subscribed");

    /* Clearing a handle drops the callback with it, so a reused handle
     * never calls into a context that has gone out of scope. */
    XHTTP_Clear(&http);
    return 0;
}


/* Drives one length delimited download with the given callback answer, and
 * reports what the exchange came back as. */
static xhttp_status_t http_length_body(http_cb_t *pCb, int nAnswer, size_t nBody,
                                       size_t nContentMax, size_t *pBuffered)
{
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK) return XHTTP_NONE;

    server.nFillBody = nBody;
    server.bWithLength = XTRUE;

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/length", server.nPort);

    memset(pCb, 0, sizeof(*pCb));
    pCb->nAnswer = nAnswer;

    xhttp_t http;
    if (XHTTP_InitRequest(&http, XHTTP_GET, "/length", "1.1") <= 0)
    {
        XThread_Join(&thread);
        return XHTTP_NONE;
    }

    XHTTP_SetCallback(&http, http_client_cb, pCb, XHTTP_READ_CNT | XHTTP_READ_HDR | XHTTP_STATUS);
    if (nContentMax) http.nContentMax = nContentMax;

    xhttp_status_t eStatus = XHTTP_EasyPerform(&http, sUrl, NULL, 0);
    if (pBuffered != NULL) *pBuffered = XHTTP_GetBodySize(&http);

    XHTTP_Clear(&http);
    XThread_Join(&thread);
    return eStatus;
}

static int XTest_length_stream(void)
{
    /* A body whose length is announced takes a different read loop from one
     * that ends with the connection, and it is the one a real server uses.
     * A callback that claims the bytes must still see all of them. */
    http_cb_t cb;
    size_t nBuffered = 0;
    const size_t nBody = 40000;

    xhttp_status_t eStatus = http_length_body(&cb, XSTDOK, nBody, 0, &nBuffered);
    if (eStatus == XHTTP_NONE) { printf("No loopback listener, skipping\n"); return 77; }

    CHECK(eStatus == XHTTP_COMPLETE, "The announced body completes");
    CHECK(cb.nContentCbs > 0, "The content callback ran");
    CHECK(cb.nStreamed == nBody, "Every announced byte reached the callback");
    CHECK(nBuffered < nBody / 2, "A claimed body is not accumulated in the handle");
    return 0;
}

static int XTest_length_buffered(void)
{
    /* The same download with an observing callback: the handle keeps the
     * whole body, and its length is exactly what was announced. */
    http_cb_t cb;
    size_t nBuffered = 0;
    const size_t nBody = 20000;

    xhttp_status_t eStatus = http_length_body(&cb, XSTDUSR, nBody, 0, &nBuffered);
    if (eStatus == XHTTP_NONE) { printf("No loopback listener, skipping\n"); return 77; }

    CHECK(eStatus == XHTTP_COMPLETE, "The announced body completes");
    CHECK(nBuffered == nBody, "The whole announced body is buffered");
    CHECK(cb.nStreamed == nBody, "And the callback saw all of it too");
    return 0;
}

static int XTest_length_stopped(void)
{
    /* A callback that says it has enough stops the download there, even
     * though the announced length has not been reached. */
    http_cb_t cb;
    const size_t nBody = 200000;

    xhttp_status_t eStatus = http_length_body(&cb, XSTDNON, nBody, 0, NULL);
    if (eStatus == XHTTP_NONE) { printf("No loopback listener, skipping\n"); return 77; }

    CHECK(eStatus == XHTTP_COMPLETE, "Stopping early still completes");
    CHECK(cb.nContentCbs == 1, "It stopped on the first answer");
    CHECK(cb.nStreamed < nBody, "Without reading the announced remainder");
    return 0;
}

static int XTest_length_terminated(void)
{
    /* And one that refuses aborts the exchange. */
    http_cb_t cb;
    xhttp_status_t eStatus = http_length_body(&cb, XSTDERR, 200000, 0, NULL);
    if (eStatus == XHTTP_NONE) { printf("No loopback listener, skipping\n"); return 77; }

    CHECK(eStatus == XHTTP_TERMINATED, "A refusing callback terminates it");
    CHECK(cb.nContentCbs == 1, "It stopped on the first answer");
    return 0;
}

static int XTest_length_limit(void)
{
    /* An announced body larger than the caller's limit is refused while it
     * is being read, not after: the point of the limit is that the memory
     * is never allocated. */
    http_cb_t cb;
    size_t nBuffered = 0;

    xhttp_status_t eStatus = http_length_body(&cb, XSTDUSR, 300000, 8192, &nBuffered);
    if (eStatus == XHTTP_NONE) { printf("No loopback listener, skipping\n"); return 77; }

    CHECK(eStatus == XHTTP_BIGCNT, "An announced body past the limit is refused");
    CHECK(nBuffered < 300000, "The whole body was never buffered");
    return 0;
}

static int XTest_length_truncated(void)
{
    /* A server that announces more than it sends and then goes away is a
     * read error, not a complete message: accepting it would hand the
     * caller a body shorter than the one it was promised. */
    http_server_t server;
    xthread_t thread;
    if (http_start(&server, &thread, 1) != XSTDOK)
    {
        printf("No loopback listener, skipping\n");
        return 77;
    }

    server.bHalfAnswer = XTRUE;
    server.pBody = "short";

    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/truncated", server.nPort);

    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/truncated", "1.1") > 0, "The request initializes");

    xhttp_status_t eStatus = XHTTP_EasyPerform(&http, sUrl, NULL, 0);
    CHECK(eStatus != XHTTP_COMPLETE, "A short answer is not reported as complete");
    CHECK(XHTTP_GetStatusStr(eStatus) != NULL, "Whatever it is has a description");

    XHTTP_Clear(&http);
    XThread_Join(&thread);
    return 0;
}

typedef struct {
    char sPath[96];
    xatomic_t nReady;
    const char *pAnswer;
} http_unix_server_t;

/* Answers one request on a unix socket with a fixed response */
static void *http_unix_serve(void *pContext)
{
    http_unix_server_t *pServer = (http_unix_server_t*)pContext;
    int nListen = (int)socket(AF_UNIX, SOCK_STREAM, 0);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    xstrncpy(addr.sun_path, sizeof(addr.sun_path), pServer->sPath);

    if (nListen < 0 || bind(nListen, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(nListen, 1) < 0)
    {
        if (nListen >= 0) close(nListen);
        XSYNC_ATOMIC_SET(&pServer->nReady, 2);
        return NULL;
    }

    XSYNC_ATOMIC_SET(&pServer->nReady, 1);
    int nPeer = (int)accept(nListen, NULL, NULL);

    if (nPeer >= 0)
    {
        char sRequest[2048];
        size_t nUsed = 0;
        while (nUsed < sizeof(sRequest) - 1)
        {
            ssize_t nRead = recv(nPeer, &sRequest[nUsed], sizeof(sRequest) - 1 - nUsed, 0);
            if (nRead <= 0) break;
            nUsed += (size_t)nRead;
            sRequest[nUsed] = '\0';
            if (strstr(sRequest, "\r\n\r\n") != NULL) break;
        }

        send(nPeer, pServer->pAnswer, strlen(pServer->pAnswer), MSG_NOSIGNAL);
        close(nPeer);
    }

    close(nListen);
    return NULL;
}

static int http_answer_cb(xhttp_t *pHttp, xhttp_ctx_t *pCtx)
{
    http_cb_t *pTest = (http_cb_t*)pHttp->pUserCtx;
    if (pCtx->eCbType == XHTTP_STATUS) pTest->nStatusCbs++;
    if (pCtx->eCbType == XHTTP_WRITE) pTest->nWriteCbs++;
    if (pCtx->eCbType == XHTTP_READ_HDR) pTest->nHeaderCbs++;
    if (pCtx->eCbType == XHTTP_READ_CNT) pTest->nContentCbs++;
    if (pCtx->eCbType == XHTTP_STATUS && pCtx->eStatus == XHTTP_RESOLVED)
        xstrncpy(pTest->sLastStatus, sizeof(pTest->sLastStatus), (const char*)pCtx->pData);
    return (int)pCtx->eCbType == pTest->nAnswer ? XSTDNON : XSTDUSR;
}

static int XTest_client_paths(void)
{
    http_cb_t cb;
    xhttp_t http, response;
    xlink_t link;

    /* A unix socket target is reached through the same exchange, with a read timeout and a status callback */
    http_unix_server_t unixServer;
    memset(&unixServer, 0, sizeof(unixServer));
    snprintf(unixServer.sPath, sizeof(unixServer.sPath), "/tmp/xutils-http-%d.sock", (int)getpid());
    unixServer.pAnswer = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    unlink(unixServer.sPath);

    xthread_t thread;
    CHECK(XThread_Create(&thread, http_unix_serve, &unixServer, XFALSE) == XSTDOK, "Start a unix server");
    for (int i = 0; i < 2000 && !XSYNC_ATOMIC_GET(&unixServer.nReady); i++) xusleep(1000);
    CHECK(XSYNC_ATOMIC_GET(&unixServer.nReady) == 1, "The unix server listens");

    memset(&cb, 0, sizeof(cb));
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/unix", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_SetUnixAddr(&http, unixServer.sPath) > 0, "It goes to the unix socket");
    CHECK(XHTTP_SetCallback(&http, http_answer_cb, &cb, XHTTP_STATUS) == XSTDOK, "It reports its status");
    http.nTimeout = 5;

    CHECK(XHTTP_EasyPerform(&http, "http://localhost/unix", NULL, 0) == XHTTP_COMPLETE, "The exchange completes");
    CHECK(http.nStatusCode == 200 && XHTTP_GetBodySize(&http) == 2, "The answer is the server's");
    CHECK(!strncmp(cb.sLastStatus, "Connecting to unix server: ", 27), "The connection named the unix server");
    XHTTP_Clear(&http);
    XThread_Join(&thread);
    unlink(unixServer.sPath);

    /* A protocol that is not HTTP is refused, a TLS one is tried, a link that is no link is reported */
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_EasyPerform(&http, "ftp://127.0.0.1/file", NULL, 0) == XHTTP_EPROTO, "FTP is not HTTP");
    CHECK(XHTTP_EasyPerform(&http, "https://127.0.0.1:1/", NULL, 0) == XHTTP_ECONNECT, "A closed TLS port is reported");
    XHTTP_Clear(&http);

    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_EasyExchange(&http, &response, "") == XHTTP_ELINK, "An empty link is no link");
    XHTTP_Clear(&http);

    /* A link without a port gets the default one of its protocol */
    XLink_Init(&link);
    xstrncpy(link.sProtocol, sizeof(link.sProtocol), "https");
    xstrncpy(link.sHost, sizeof(link.sHost), "127.0.0.1");
    xsock_t sock;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/", "1.1") > 0, "The request initializes");
    XHTTP_Connect(&http, &sock, &link);
    CHECK(link.nPort == XHTTP_SSL_PORT && !strcmp(link.sHost, "127.0.0.1:443"), "TLS defaults to its port");
    XSock_Close(&sock);
    XHTTP_Clear(&http);

    /* Neither the exchange nor the request runs on a socket that does not block */
    XSOCKET pair[2];
    CHECK(XSock_CreatePair(pair) == XSTDOK, "Create a pair");
    CHECK(XSock_Init(&sock, XSOCK_UNIX | XSOCK_PEER | XSOCK_NB, pair[0]) == XSOCK_SUCCESS, "Flag one end non-blocking");
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_Exchange(&http, &response, &sock) == XHTTP_EFDMODE, "The exchange refuses it");
    CHECK(XHTTP_Perform(&http, &sock, NULL, 0) == XHTTP_EFDMODE, "And so does the request");

    /* A request that was never assembled has nothing to send */
    sock.nFlags &= ~XSOCK_NB;
    CHECK(XHTTP_Exchange(&http, &response, &sock) == XHTTP_EWRITE, "Nothing assembled is nothing to write");
    XHTTP_Clear(&response);
    XHTTP_Clear(&http);
    XSock_Close(&sock);
    close(pair[1]);

    /* A callback can end the exchange after the request is written, or at the header of the answer */
    http_server_t server;
    CHECK(http_start(&server, &thread, 2) == XSTDOK, "Start a server");
    char sUrl[128];
    snprintf(sUrl, sizeof(sUrl), "http://127.0.0.1:%u/early", server.nPort);

    memset(&cb, 0, sizeof(cb));
    cb.nAnswer = XHTTP_READ_HDR;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/early", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_SetCallback(&http, http_answer_cb, &cb, XHTTP_READ_HDR) == XSTDOK, "Watch the header");
    CHECK(XHTTP_EasyPerform(&http, sUrl, NULL, 0) == XHTTP_COMPLETE && cb.nHeaderCbs == 1, "The header ends it");
    CHECK(http.nComplete, "As complete");
    XHTTP_Clear(&http);

    server.bStream = XTRUE;
    memset(&cb, 0, sizeof(cb));
    cb.nAnswer = XHTTP_READ_CNT;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/early", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_SetCallback(&http, http_answer_cb, &cb, XHTTP_READ_CNT) == XSTDOK, "Watch the body");
    xhttp_status_t eStatus = XHTTP_EasyPerform(&http, sUrl, NULL, 0);
    CHECK(eStatus == XHTTP_COMPLETE && http.nComplete, "A body without a length is ended by the callback");
    XHTTP_Clear(&http);
    XThread_Join(&thread);

    /* Read without blocking, an answer that ends with its connection completes at its end. The whole answer is
       waited for first: read before its body arrives, it completes with no body (see XHTTP_ReadContent) */
    CHECK(http_start(&server, &thread, 1) == XSTDOK, "Start a server");
    server.bStream = XTRUE;
    CHECK(XSock_Create(&sock, XSOCK_TCP_CLIENT, "127.0.0.1", server.nPort) != XSOCK_INVALID, "Connect");
    const char *pRequest = "GET /nonblocking HTTP/1.1\r\nHost: x\r\n\r\n";
    CHECK(XSock_Write(&sock, pRequest, strlen(pRequest)) == (int)strlen(pRequest), "Send a request");
    XThread_Join(&thread);
    CHECK(XSock_NonBlock(&sock, XTRUE) != XSOCK_INVALID, "Stop blocking");

    XHTTP_Init(&http, XHTTP_DUMMY, XSTDNON);
    eStatus = XHTTP_INCOMPLETE;
    for (int i = 0; i < 2000 && eStatus != XHTTP_COMPLETE; i++)
    {
        eStatus = http.nHeaderLength ? XHTTP_ReadContent(&http, &sock) : XHTTP_Receive(&http, &sock);
        if (eStatus != XHTTP_COMPLETE) xusleep(1000);
    }

    CHECK(eStatus == XHTTP_COMPLETE && http.nStatusCode == 200, "The answer completes when the server closes");
    CHECK(XHTTP_GetBodySize(&http) == strlen(server.pBody), "With the whole body");
    CHECK(XHTTP_ReadContent(&http, &sock) == XHTTP_COMPLETE, "A complete answer reads as complete");
    XHTTP_Clear(&http);
    XSock_Close(&sock);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(client_paths),
    XTEST_CASE(connect),
    XTEST_CASE(link_exchange),
    XTEST_CASE(easy_exchange),
    XTEST_CASE(perform),
    XTEST_CASE(easy_perform),
    XTEST_CASE(auth_and_status),
    XTEST_CASE(client_failures),
    XTEST_CASE(truncated_answer),
    XTEST_CASE(stream_callback),
    XTEST_CASE(buffered_stream),
    XTEST_CASE(stopped_stream),
    XTEST_CASE(terminated_stream),
    XTEST_CASE(content_limit),
    XTEST_CASE(callback_guards),
    XTEST_CASE(length_stream),
    XTEST_CASE(length_buffered),
    XTEST_CASE(length_stopped),
    XTEST_CASE(length_terminated),
    XTEST_CASE(length_limit),
    XTEST_CASE(length_truncated)
)
