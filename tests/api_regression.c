/*!
 * @file libxutils/tests/api_regression.c
 * @brief Endpoint rejection must close each session exactly once.
 */

#include "api.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "api_regression: %s\n", msg); return 1; } } while (0)

static int nClosed;
static xapi_cb_type_t eReject;

static int reject_endpoint(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    (void)pSession;
    if (pCtx->eCbType == XAPI_CB_CLOSED) nClosed++;
    return pCtx->eCbType == eReject ? XAPI_DISCONNECT : XAPI_CONTINUE;
}

int main(void)
{
    xapi_t api;
    XAPI_Init(&api, reject_endpoint, NULL);
    xapi_endpoint_t endpt;
    XAPI_InitEndpoint(&endpt);
    endpt.eType = XAPI_SOCK;
    endpt.eRole = XAPI_CUSTOM;

    int nPair[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, nPair) == 0, "create an isolated endpoint pair");
    endpt.nFD = nPair[0];
    eReject = XAPI_CB_REGISTERED;
    CHECK(XAPI_AddEndpoint(&api, &endpt) < 0, "a rejected registration must report failure without accessing freed data");
    CHECK(nClosed == 1 && XAPI_GetEventCount(&api) == 0, "a rejected registration must close exactly once");
    close(nPair[1]);

    XAPI_InitEndpoint(&endpt);
    endpt.eType = XAPI_SOCK;
    endpt.eRole = XAPI_SERVER;
    char sDirectory[] = "/tmp/xutils-api-regression.XXXXXX";
    CHECK(mkdtemp(sDirectory) != NULL, "create a private listener directory");
    char sPath[256];
    snprintf(sPath, sizeof(sPath), "%s/listener.sock", sDirectory);
    endpt.pAddr = sPath;
    endpt.bUnix = XTRUE;
    eReject = XAPI_CB_LISTENING;
    CHECK(XAPI_AddEndpoint(&api, &endpt) < 0, "a rejected listener must report failure without accessing freed data");
    CHECK(nClosed == 2 && XAPI_GetEventCount(&api) == 0, "a rejected listener must close exactly once");
    unlink(sPath);
    rmdir(sDirectory);

    int nListener = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(nListener >= 0, "create an isolated loopback listener");
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(nListener, (struct sockaddr*)&addr, sizeof(addr)) == 0 && listen(nListener, 1) == 0,
        "bind the listener to an ephemeral port");
    socklen_t nAddrLen = sizeof(addr);
    CHECK(getsockname(nListener, (struct sockaddr*)&addr, &nAddrLen) == 0, "read the assigned port");
    XAPI_InitEndpoint(&endpt);
    endpt.eType = XAPI_SOCK;
    endpt.eRole = XAPI_CLIENT;
    endpt.pAddr = "127.0.0.1";
    endpt.nPort = ntohs(addr.sin_port);
    eReject = XAPI_CB_CONNECTED;
    CHECK(XAPI_AddEndpoint(&api, &endpt) < 0, "a rejected connection must report failure without accessing freed data");
    CHECK(nClosed == 3 && XAPI_GetEventCount(&api) == 0, "a rejected connection must close exactly once");
    close(nListener);

    xapi_session_t session;
    memset(&session, 0, sizeof(session));
    session.pApi = &api;
    XByteBuffer_Init(&session.txBuffer, 0, XFALSE);
    CHECK(XByteBuffer_Add(&session.txBuffer, (const uint8_t*)"queued", 6) > 0, "queue an existing valid message");
    xbyte_buffer_t oversized = {0};
    oversized.pData = (uint8_t*)"x";
    oversized.nUsed = SIZE_MAX;
    CHECK(XAPI_PutTxBuff(&session, &oversized) < 0, "an append failure must not report success for a nonempty queue");
    CHECK(session.txBuffer.nUsed == 6, "a failed append must preserve queued bytes");
    XByteBuffer_Clear(&session.txBuffer);
    XAPI_Destroy(&api);
    puts("api_regression: OK");
    return 0;
}
