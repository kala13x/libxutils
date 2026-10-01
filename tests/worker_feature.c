/* Real HTTP and HTTPS traffic through direct, forked and restarted XAPI workers. */
#include "test.h"
#include "api.h"
#include "cpu.h"
#ifdef XSOCK_USE_SSL
#include "tls_fixture.h"
#endif
#include <pthread.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int nIndex;
    pid_t nPID;
    int nStatus;
} worker_ready_t;

typedef struct {
    int nWorkers;
    int nClients;
    int nRounds;
    int nBytes;
    int nReadyFD;
    int nListenerFD;
    int nErrors;
    int nAccepted;
    int nClosed;
    int nTimers;
    int nTimersClosed;
    int nTimeout;
    uint16_t nPort;
    xbool_t bExclusive;
    xbool_t bUnix;
    xbool_t bChurn;
    xbool_t bRestart;
    xbool_t bBenchmark;
    xbool_t bTLS;
    xbool_t bShutdown;
    xbool_t bPending;
    xsock_cert_t cert;
    xapi_session_t *pListener;
    xapi_session_t *pHeld;
    char sPath[108];
    worker_ready_t ready[8];
} worker_test_t;

typedef struct { int nClient; int nNext; } worker_session_t;

typedef struct {
    worker_test_t *pTest;
    int nClient;
    int nRounds;
    int nExpectedWorker;
    int nResult;
    pid_t nPID;
    uint64_t *pLatency;
} worker_client_t;

static volatile sig_atomic_t g_nStop;
static int g_nReapErrors;
static pid_t g_nExpectedKill;
static const char g_partial[] = "POST /worker HTTP/1.1\r\nContent-Length: 4096\r\n\r\n\x00\xff\x31";
xevent_status_t XAPI_RebuildWorkerEvents(xapi_t*);
pid_t __real_waitpid(pid_t, int*, int);

pid_t __wrap_waitpid(pid_t nPID, int *pStatus, int nOptions)
{
    int nStatus = 0;
    pid_t nResult = __real_waitpid(nPID, &nStatus, nOptions);
    if (pStatus) *pStatus = nStatus;
    if (nResult > 0 && WIFSIGNALED(nStatus) && WTERMSIG(nStatus) == SIGKILL && nResult == g_nExpectedKill)
        g_nExpectedKill = 0;
    else if (nResult > 0 && ((WIFEXITED(nStatus) && WEXITSTATUS(nStatus)) || WIFSIGNALED(nStatus))) g_nReapErrors++;
    return nResult;
}

static uint64_t worker_now(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

static int worker_pin(const char *pName)
{
    const char *pValue = getenv(pName);
    if (!pValue) return 0;
    return XCPU_SetSingle(atoi(pValue), XCPU_CALLER_PID);
}

static void worker_stop(int nSignal)
{
    (void)nSignal;
    g_nStop = 1;
}

static uint8_t worker_byte(int nClient, int nSequence, size_t nOffset)
{
    return (uint8_t)(nClient * 31 + nSequence * 17 + nOffset * 13);
}

static int worker_number(xhttp_t *pHttp, const char *pName, int *pValue)
{
    const char *pText = XHTTP_GetHeader(pHttp, pName);
    if (!pText || !*pText) return 0;
    char *pEnd;
    errno = 0;
    long nValue = strtol(pText, &pEnd, 10);
    if (*pEnd || errno || nValue < -1 || nValue > INT_MAX) return 0;
    *pValue = (int)nValue;
    return 1;
}

static int worker_cb(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    worker_test_t *pTest = pCtx->pApi->pUserCtx;
    if (pCtx->eCbType == XAPI_CB_REGISTERED && pSession->eRole == XAPI_SERVER) pTest->pListener = pSession;
    if (pCtx->eCbType == XAPI_CB_STATUS && pCtx->eStatType == XAPI_SELF && pCtx->nStatus == XAPI_TIMER_DESTROY)
        pTest->nTimersClosed++;
    if (pCtx->eCbType == XAPI_CB_ERROR)
    {
        pTest->nErrors++;
        return XAPI_DISCONNECT;
    }
    if (pCtx->eCbType == XAPI_CB_ACCEPTED)
    {
        worker_session_t *pState = calloc(1, sizeof(*pState));
        if (!pState) { pTest->nErrors++; return XAPI_DISCONNECT; }
        pState->nClient = -1;
        pSession->pSessionData = pState;
        pTest->nAccepted++;
        return XAPI_SetEvents(pSession, XPOLLIN);
    }
    if (pCtx->eCbType == XAPI_CB_CLOSED && pSession->eRole == XAPI_PEER)
    {
        free(pSession->pSessionData);
        pSession->pSessionData = NULL;
        pTest->nClosed++;
        if (pTest->pHeld == pSession) pTest->pHeld = NULL;
    }
    if (pCtx->eCbType != XAPI_CB_READ) return XAPI_CONTINUE;

    worker_session_t *pState = pSession->pSessionData;
    xhttp_t *pRequest = pSession->pPacket;
    int nClient = -1, nSequence = -1;
    const char *pType = XHTTP_GetHeader(pRequest, "Content-Type");
    xbool_t bValid = pState && pRequest->eType == XHTTP_REQUEST && pRequest->eMethod == XHTTP_POST &&
        !strcmp(pRequest->sUri, "/worker") && !strcmp(pRequest->sVersion, "1.1") &&
        pType && !strcmp(pType, "application/octet-stream") &&
        worker_number(pRequest, "X-Client", &nClient) && nClient >= 0 &&
        worker_number(pRequest, "X-Sequence", &nSequence) && nSequence == pState->nNext &&
        (pState->nClient < 0 || pState->nClient == nClient) && XHTTP_GetBodySize(pRequest) == (size_t)pTest->nBytes;
    const uint8_t *pBody = (const uint8_t*)XHTTP_GetBody(pRequest);
    if (bValid)
        for (int i = 0; i < pTest->nBytes; i++)
            if (pBody[i] != worker_byte(nClient, nSequence, i)) bValid = XFALSE;
    if (!bValid) { pTest->nErrors++; return XAPI_DISCONNECT; }

    if (pTest->bShutdown && nClient >= pTest->nClients + 16)
    {
        if (pTest->pHeld || XAPI_AddTimer(pSession, 600000) != XSTDOK)
        {
            pTest->nErrors++;
            return XAPI_DISCONNECT;
        }
        pTest->pHeld = pSession;
        pTest->nTimers++;
    }
    pState->nClient = nClient;
    pState->nNext++;
    uint8_t body[4096];
    for (int i = 0; i < pTest->nBytes; i++) body[i] = pBody[i] ^ 0xa7;
    xhttp_t response;
    XHTTP_InitResponse(&response, 200, "1.1");
    XHTTP_AddHeader(&response, "Content-Type", "application/octet-stream");
    XHTTP_AddHeader(&response, "X-Client", "%d", nClient);
    XHTTP_AddHeader(&response, "X-Sequence", "%d", nSequence);
    XHTTP_AddHeader(&response, "X-Worker", "%d", XAPI_GetWorkerIndex(pCtx->pApi));
    XHTTP_AddHeader(&response, "X-PID", "%d", (int)getpid());
    xbyte_buffer_t *pWire = XHTTP_Assemble(&response, body, pTest->nBytes);
    int nStatus = pWire ? XByteBuffer_AddBuff(&pSession->txBuffer, pWire) : XSTDERR;
    XHTTP_Clear(&response);
    if (nStatus <= 0) { pTest->nErrors++; return XAPI_DISCONNECT; }
    return XAPI_EnableEvent(pSession, XPOLLOUT);
}

static void worker_serve(xapi_t *pApi, worker_test_t *pTest, int nStartStatus)
{
    if (pTest->bBenchmark && worker_pin("XUTILS_BENCH_SERVER_CPU")) nStartStatus = XSTDERR;
    worker_ready_t ready = {XAPI_GetWorkerIndex(pApi), getpid(), nStartStatus};
    if (write(pTest->nReadyFD, &ready, sizeof(ready)) != sizeof(ready)) nStartStatus = XSTDERR;
    while (nStartStatus >= 0 && !g_nStop && !pTest->nErrors)
    {
        XAPI_Service(pApi, 50);
        if (pTest->pHeld && !pTest->bPending && pTest->pHeld->rxBuffer.nUsed >= sizeof(g_partial) - 1)
        {
            xbyte_buffer_t *pBuffer = &pTest->pHeld->rxBuffer;
            if (pBuffer->nUsed != sizeof(g_partial) - 1 || memcmp(pBuffer->pData, g_partial, sizeof(g_partial) - 1))
                pTest->nErrors++;
            else
            {
                pTest->bPending = XTRUE;
                if (write(pTest->nReadyFD, &ready, sizeof(ready)) != sizeof(ready)) pTest->nErrors++;
            }
        }
    }
    xbool_t bLive = !pTest->bShutdown || (pTest->bPending && pTest->pHeld && pTest->pHeld->pTimer &&
        pTest->nAccepted - pTest->nClosed == 1 && pTest->nTimers == 1);
    int descriptors[] = {pTest->nListenerFD, pApi->bHaveEvents ? pApi->events.nEventFd : -1,
        pTest->pHeld ? pTest->pHeld->sock.nFD : -1,
        pTest->pHeld && pTest->pHeld->pTimer ? pTest->pHeld->pTimer->nFD : -1};
    XAPI_Destroy(pApi);
    xbool_t bValid = nStartStatus >= 0 && !pTest->nErrors && bLive && pTest->nAccepted == pTest->nClosed &&
        pTest->nTimers == pTest->nTimersClosed;
    for (size_t i = 0; i < sizeof(descriptors) / sizeof(*descriptors); i++)
        if (descriptors[i] >= 0 && (fcntl(descriptors[i], F_GETFD) != -1 || errno != EBADF)) bValid = XFALSE;
    if (!bValid)
    {
        ready.nStatus = XSTDERR;
        (void)write(pTest->nReadyFD, &ready, sizeof(ready));
    }
    close(pTest->nReadyFD);
    exit(bValid ? 0 : 1);
}

static void worker_supervisor(worker_test_t *pTest)
{
    struct sigaction action = {0};
    action.sa_handler = worker_stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    g_nStop = 0;
    xapi_t api;
    XAPI_Init(&api, worker_cb, pTest);
    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_HTTP;
    endpoint.eRole = XAPI_SERVER;
    endpoint.bExclusive = pTest->bExclusive;
    endpoint.bUnix = pTest->bUnix;
    endpoint.bTLS = pTest->bTLS;
    endpoint.pAddr = pTest->sPath;
    endpoint.nFD = pTest->nListenerFD;
    endpoint.nEvents = XPOLLIN | (pTest->bExclusive ? EPOLLEXCLUSIVE : 0);
    int nStatus = pTest->bUnix ? XAPI_Listen(&api, &endpoint) : XAPI_AddEvent(&api, &endpoint);
    if (nStatus >= 0 && pTest->bTLS)
    {
        xsock_t *pSock = pTest->pListener ? &pTest->pListener->sock : NULL;
        if (!pSock || XSock_InitSSLServer(pSock, 0) == XSOCK_INVALID ||
            XSock_SetSSLCert(pSock, &pTest->cert) == XSOCK_INVALID) nStatus = XSTDERR;
    }
    if (nStatus < 0) worker_serve(&api, pTest, nStatus);
    api.events.nEventMax = 256;
    if (!pTest->nWorkers) worker_serve(&api, pTest, XSTDOK);
    nStatus = XAPI_InitWorkers(&api, pTest->nWorkers, XFALSE);
    if (XAPI_IsWorker(&api) || nStatus < 0) worker_serve(&api, pTest, nStatus);
    g_nExpectedKill = pTest->bRestart ? api.pWorkerPIDs[0] : 0;
    nStatus = XAPI_WatchWorkers(&api, &g_nStop);
    if (XAPI_IsWorker(&api)) worker_serve(&api, pTest, nStatus);
    XAPI_Destroy(&api);
    close(pTest->nReadyFD);
    exit(nStatus >= 0 && !g_nReapErrors ? 0 : 1);
}

static int worker_read_ready(int nFD, worker_ready_t *pReady, int nTimeout)
{
    struct pollfd pollfd = {.fd = nFD, .events = POLLIN};
    CHECK(poll(&pollfd, 1, nTimeout) > 0 && read(nFD, pReady, sizeof(*pReady)) == sizeof(*pReady),
        "A worker reports readiness within the bounded startup time");
    CHECK(pReady->nStatus >= 0 && pReady->nPID > 0, "The worker rebuilt its backend successfully");
    return 0;
}

static int worker_connect(worker_test_t *pTest, xsock_t *pSock)
{
    uint32_t nFlags = pTest->bUnix ? XSOCK_UNIX_CLIENT : XSOCK_TCP_CLIENT;
    if (XSock_Create(pSock, nFlags, pTest->bUnix ? pTest->sPath : "127.0.0.1", pTest->nPort) < 0) return 1;
    if (!pTest->bUnix) XSock_NoDelay(pSock, XTRUE);
    XSock_TimeOutR(pSock, pTest->nTimeout / 1000, 0);
    XSock_TimeOutS(pSock, pTest->nTimeout / 1000, 0);
#ifdef XSOCK_USE_SSL
    if (pTest->bTLS)
    {
        pSock->nFlags |= XSOCK_SSL;
        CHECK(XSock_InitSSLClient(pSock, "localhost") != XSOCK_INVALID, "Complete a verified TLS handshake with the worker");
        SSL *pSSL = XSock_GetSSL(pSock);
        X509 *pCert = pSSL ? SSL_get_peer_certificate(pSSL) : NULL;
        xbool_t bValid = pCert && (SSL_get_verify_mode(pSSL) & SSL_VERIFY_PEER) &&
            SSL_get_verify_result(pSSL) == X509_V_OK;
        X509_free(pCert);
        CHECK(bValid, "The worker presents a trusted certificate for the requested host");
    }
#endif
    return 0;
}

static int worker_exchange(worker_client_t *pClient, xsock_t *pSock, int nSequence)
{
    worker_test_t *pTest = pClient->pTest;
    uint8_t body[4096];
    for (int i = 0; i < pTest->nBytes; i++) body[i] = worker_byte(pClient->nClient, nSequence, i);
    xhttp_t request, response;
    XHTTP_InitRequest(&request, XHTTP_POST, "/worker", "1.1");
    XHTTP_Init(&response, XHTTP_DUMMY, 0);
    XHTTP_AddHeader(&request, "Content-Type", "application/octet-stream");
    XHTTP_AddHeader(&request, "X-Client", "%d", pClient->nClient);
    XHTTP_AddHeader(&request, "X-Sequence", "%d", nSequence);
    xbool_t bValid = XHTTP_Assemble(&request, body, pTest->nBytes) &&
        XHTTP_Exchange(&request, &response, pSock) == XHTTP_COMPLETE;
    int nClient = -1, nReply = -1, nWorker = -2, nPID = -1;
    const char *pType = XHTTP_GetHeader(&response, "Content-Type");
    bValid = bValid && response.eType == XHTTP_RESPONSE && response.nStatusCode == 200 &&
        !strcmp(response.sVersion, "1.1") && pType && !strcmp(pType, "application/octet-stream") &&
        worker_number(&response, "X-Client", &nClient) && nClient == pClient->nClient &&
        worker_number(&response, "X-Sequence", &nReply) && nReply == nSequence &&
        worker_number(&response, "X-Worker", &nWorker) && worker_number(&response, "X-PID", &nPID) &&
        XHTTP_GetBodySize(&response) == (size_t)pTest->nBytes;
    if (bValid)
    {
        int nSlot = pTest->nWorkers ? nWorker : 0;
        bValid = nSlot >= 0 && nSlot < (pTest->nWorkers ? pTest->nWorkers : 1) &&
            nPID == pTest->ready[nSlot].nPID && nWorker == pTest->ready[nSlot].nIndex &&
            (pClient->nExpectedWorker == -2 || nWorker == pClient->nExpectedWorker) &&
            (!pClient->nPID || pClient->nPID == nPID);
        const uint8_t *pBody = (const uint8_t*)XHTTP_GetBody(&response);
        for (int i = 0; i < pTest->nBytes; i++)
            if (pBody[i] != (uint8_t)(worker_byte(pClient->nClient, nSequence, i) ^ 0xa7)) bValid = XFALSE;
        pClient->nPID = nPID;
    }
    XHTTP_Clear(&request);
    XHTTP_Clear(&response);
    CHECK(bValid, "HTTP status, version, request identity, sequence, worker ownership and every response byte match");
    return 0;
}

static void *worker_client(void *pData)
{
    worker_client_t *pClient = pData;
    if (pClient->pTest->bBenchmark && worker_pin("XUTILS_BENCH_CLIENT_CPU"))
    {
        pClient->nResult = 1;
        return NULL;
    }
    xsock_t sock;
    XSock_Init(&sock, XSOCK_UNDEFINED, XSOCK_INVALID);
    for (int i = 0; i < pClient->nRounds; i++)
    {
        if (sock.nFD < 0 && worker_connect(pClient->pTest, &sock)) { pClient->nResult = 1; break; }
        uint64_t nStart = worker_now();
        pClient->nResult = worker_exchange(pClient, &sock, pClient->pTest->bChurn ? 0 : i);
        if (pClient->pLatency) pClient->pLatency[i] = worker_now() - nStart;
        if (pClient->nResult) break;
        if (pClient->pTest->bChurn) { XSock_Close(&sock); pClient->nPID = 0; }
    }
    XSock_Close(&sock);
    return NULL;
}

static int worker_pause(pid_t nPID, int nTimeout)
{
    CHECK(!kill(nPID, SIGSTOP), "Pause the worker before selecting an acceptor");
    char path[64], state[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)nPID);
    uint64_t nDeadline = worker_now() + (uint64_t)nTimeout * 1000000;
    do
    {
        FILE *pFile = fopen(path, "r");
        if (!pFile) break;
        char *pText = fgets(state, sizeof(state), pFile);
        fclose(pFile);
        char *pEnd = pText ? strrchr(state, ')') : NULL;
        if (pEnd && pEnd[1] == ' ' && pEnd[2] == 'T') return 0;
        usleep(1000);
    } while (worker_now() < nDeadline);
    CHECK(0, "The worker actually stops before another worker receives traffic");
    return 1;
}

static int worker_probe(worker_test_t *pTest)
{
    int nCount = pTest->nWorkers ? pTest->nWorkers : 1;
    for (int i = 0; i < nCount; i++)
        CHECK(!worker_pause(pTest->ready[i].nPID, pTest->nTimeout), "Every worker is stopped before the first probe");
    int nFailed = 0;
    for (int i = 0; i < nCount; i++)
    {
        CHECK(!kill(pTest->ready[i].nPID, SIGCONT), "Allow exactly the selected worker to accept");
        worker_client_t client = {.pTest = pTest, .nClient = pTest->nClients + i, .nRounds = 2,
            .nExpectedWorker = pTest->ready[i].nIndex};
        worker_client(&client);
        nFailed |= client.nResult;
        CHECK(!worker_pause(pTest->ready[i].nPID, pTest->nTimeout), "Finish the selected worker's request/response probe");
    }
    for (int i = 0; i < nCount; i++) CHECK(!kill(pTest->ready[i].nPID, SIGCONT), "Resume all workers for concurrent traffic");
    CHECK(!nFailed, "Every worker independently accepts and answers exact requests on the shared listener");
    return 0;
}

static int worker_compare(const void *pLeft, const void *pRight)
{
    uint64_t a = *(const uint64_t*)pLeft, b = *(const uint64_t*)pRight;
    return (a > b) - (a < b);
}

static int worker_open(worker_test_t *pTest, xsock_t *pSockets, worker_client_t *pClients)
{
    int nCount = pTest->nWorkers ? pTest->nWorkers : 1;
    int nFailed = 0;
    for (int i = 0; i < nCount; i++)
    {
        XSock_Init(&pSockets[i], XSOCK_UNDEFINED, XSOCK_INVALID);
        if (worker_pause(pTest->ready[i].nPID, pTest->nTimeout)) nFailed = 1;
    }
    for (int i = 0; !nFailed && i < nCount; i++)
    {
        pClients[i].pTest = pTest;
        pClients[i].nClient = pTest->nClients + 16 + i;
        pClients[i].nExpectedWorker = pTest->ready[i].nIndex;
        if (kill(pTest->ready[i].nPID, SIGCONT) || worker_connect(pTest, &pSockets[i]) ||
            worker_exchange(&pClients[i], &pSockets[i], 0)) nFailed = 1;
        if (worker_pause(pTest->ready[i].nPID, pTest->nTimeout)) nFailed = 1;
    }
    for (int i = 0; i < nCount; i++) kill(pTest->ready[i].nPID, SIGCONT);
    return nFailed;
}

static int worker_restart(worker_test_t *pTest, int nReadyFD)
{
    xsock_t sockets[8];
    worker_client_t clients[8] = {0};
    int nFailed = worker_open(pTest, sockets, clients);
    pid_t nPrevious = pTest->ready[0].nPID;
    if (!nFailed && kill(nPrevious, SIGKILL)) nFailed = 1;
    if (!nFailed)
    {
        worker_ready_t replacement;
        nFailed = worker_read_ready(nReadyFD, &replacement, pTest->nTimeout);
        if (!nFailed && (replacement.nIndex != 0 || replacement.nPID == nPrevious)) nFailed = 1;
        if (!nFailed) pTest->ready[0] = replacement;
    }
    if (!nFailed)
    {
        char cByte;
        ssize_t nRead = recv(sockets[0].nFD, &cByte, sizeof(cByte), 0);
        if (nRead != 0 && !(nRead < 0 && errno == ECONNRESET)) nFailed = 1;
        for (int i = 1; i < pTest->nWorkers; i++)
            if (worker_exchange(&clients[i], &sockets[i], 1)) nFailed = 1;
    }
    for (int i = 0; i < pTest->nWorkers; i++) XSock_Close(&sockets[i]);
    CHECK(!nFailed, "Only the terminated worker loses its connection; sibling PIDs, sessions and sequence remain intact");
    return worker_probe(pTest);
}

static int worker_pending(worker_test_t *pTest, xsock_t *pSockets, int nReadyFD)
{
    worker_client_t clients[8] = {0};
    int nCount = pTest->nWorkers ? pTest->nWorkers : 1;
    int nFailed = worker_open(pTest, pSockets, clients);
    for (int i = 0; !nFailed && i < nCount; i++)
        if (XSock_Write(&pSockets[i], g_partial, sizeof(g_partial) - 1) != sizeof(g_partial) - 1) nFailed = 1;
    unsigned int nSeen = 0;
    for (int i = 0; !nFailed && i < nCount; i++)
    {
        worker_ready_t ready;
        if (worker_read_ready(nReadyFD, &ready, pTest->nTimeout)) { nFailed = 1; break; }
        int nSlot = pTest->nWorkers ? ready.nIndex : 0;
        if (nSlot < 0 || nSlot >= nCount || ready.nPID != pTest->ready[nSlot].nPID || (nSeen & (1u << nSlot))) nFailed = 1;
        else nSeen |= 1u << nSlot;
    }
    CHECK(!nFailed && nSeen == (1u << nCount) - 1,
        "Every worker holds the exact incomplete request on its verified connection before shutdown");
    return 0;
}

static int worker_run(worker_test_t *pTest)
{
    pTest->nTimeout = 10000;
    const char *pTimeout = getenv("XUTILS_TEST_TIMEOUT_MS");
    if (pTimeout && atoi(pTimeout) > pTest->nTimeout) pTest->nTimeout = atoi(pTimeout);
    char root[] = "/tmp/xutils-workers-XXXXXX";
    CHECK(mkdtemp(root) != NULL, "Create an isolated worker fixture");
    snprintf(pTest->sPath, sizeof(pTest->sPath), "%s/http.sock", root);
    pTest->nListenerFD = -1;
    if (!pTest->bUnix)
    {
        pTest->nListenerFD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
        socklen_t nSize = sizeof(addr);
        CHECK(pTest->nListenerFD >= 0 && !bind(pTest->nListenerFD, (struct sockaddr*)&addr, nSize) &&
            !listen(pTest->nListenerFD, 128) && !getsockname(pTest->nListenerFD, (struct sockaddr*)&addr, &nSize),
            "Reserve an actual ephemeral TCP listener without a port-selection race");
        pTest->nPort = ntohs(addr.sin_port);
    }
    int readyPipe[2];
    CHECK(!pipe(readyPipe), "Create the worker readiness channel");
    pTest->nReadyFD = readyPipe[1];
    fflush(NULL);
    uint64_t nStart = worker_now();
    pid_t nSupervisor = fork();
    CHECK(nSupervisor >= 0, "Start the server supervisor before creating client threads");
    if (!nSupervisor) { close(readyPipe[0]); worker_supervisor(pTest); }
    close(readyPipe[1]);
    if (pTest->nListenerFD >= 0) close(pTest->nListenerFD);
    int nCount = pTest->nWorkers ? pTest->nWorkers : 1;
    int nFailed = 0;
    for (int i = 0; i < nCount; i++)
    {
        worker_ready_t ready;
        if (worker_read_ready(readyPipe[0], &ready, pTest->nTimeout)) { nFailed = 1; break; }
        int nSlot = pTest->nWorkers ? ready.nIndex : 0;
        if (nSlot < 0 || nSlot >= nCount || pTest->ready[nSlot].nPID) { nFailed = 1; break; }
        pTest->ready[nSlot] = ready;
    }
    uint64_t nStartup = worker_now() - nStart;
    if (!nFailed && !pTest->bBenchmark) nFailed = worker_probe(pTest);
    if (!nFailed && pTest->bRestart) nFailed = worker_restart(pTest, readyPipe[0]);

    size_t nTotal = (size_t)pTest->nClients * pTest->nRounds;
    uint64_t *pLatency = calloc(nTotal, sizeof(*pLatency));
    pthread_t threads[32];
    worker_client_t clients[32] = {0};
    int nStarted = 0;
    nStart = worker_now();
    if (!pLatency) nFailed = 1;
    for (int i = 0; !nFailed && i < pTest->nClients; i++)
    {
        clients[i].pTest = pTest;
        clients[i].nClient = i;
        clients[i].nRounds = pTest->nRounds;
        clients[i].nExpectedWorker = -2;
        clients[i].pLatency = pLatency + (size_t)i * pTest->nRounds;
        if (pthread_create(&threads[i], NULL, worker_client, &clients[i])) nFailed = 1;
        else nStarted++;
    }
    for (int i = 0; i < nStarted; i++)
    {
        if (pthread_join(threads[i], NULL) || clients[i].nResult) nFailed = 1;
    }
    uint64_t nElapsed = worker_now() - nStart;
    xsock_t held[8];
    for (int i = 0; i < nCount; i++) XSock_Init(&held[i], XSOCK_UNDEFINED, XSOCK_INVALID);
    if (!nFailed && pTest->bShutdown) nFailed = worker_pending(pTest, held, readyPipe[0]);
    for (int i = 0; i < nCount; i++)
        if (pTest->ready[i].nPID > 0) kill(pTest->ready[i].nPID, SIGCONT);
    kill(nSupervisor, SIGTERM);
    int nStatus = 0;
    if (waitpid(nSupervisor, &nStatus, 0) != nSupervisor || !WIFEXITED(nStatus) || WEXITSTATUS(nStatus)) nFailed = 1;
    for (int i = 0; i < nCount; i++)
    {
        if (held[i].nFD >= 0)
        {
            char cByte;
            if (XSock_Read(&held[i], &cByte, sizeof(cByte)) != 0 || XSock_Status(&held[i]) != XSOCK_EOF) nFailed = 1;
        }
        XSock_Close(&held[i]);
    }
    worker_ready_t extra;
    while (read(readyPipe[0], &extra, sizeof(extra)) == sizeof(extra))
        if (extra.nStatus < 0) nFailed = 1;
    close(readyPipe[0]);
    unlink(pTest->sPath);
    rmdir(root);
    if (!nFailed && pTest->bBenchmark)
    {
        qsort(pLatency, nTotal, sizeof(*pLatency), worker_compare);
        printf("{\"workers\":%d,\"clients\":%d,\"bytes\":%d,\"requests\":%zu,\"startup_us\":%.3f,"
            "\"rps\":%.3f,\"p50_us\":%.3f,\"p95_us\":%.3f,\"p99_us\":%.3f}\n",
            pTest->nWorkers, pTest->nClients, pTest->nBytes, nTotal, nStartup / 1000.0,
            nTotal * 1000000000.0 / nElapsed, pLatency[nTotal / 2] / 1000.0,
            pLatency[nTotal * 95 / 100] / 1000.0, pLatency[nTotal * 99 / 100] / 1000.0);
    }
    free(pLatency);
    CHECK(!nFailed, "Every client exchange and every worker's final ownership check succeeds");
    return 0;
}

static int worker_case(int nWorkers, xbool_t bExclusive, xbool_t bUnix, xbool_t bRestart, xbool_t bChurn)
{
    worker_test_t test = {.nWorkers = nWorkers, .nClients = 16, .nRounds = 24, .nBytes = 257,
        .bExclusive = bExclusive, .bUnix = bUnix, .bRestart = bRestart, .bChurn = bChurn};
    return worker_run(&test);
}

static int XTest_direct(void) { return worker_case(0, XTRUE, XFALSE, XFALSE, XFALSE); }
static int XTest_one(void) { return worker_case(1, XTRUE, XFALSE, XFALSE, XFALSE); }
static int XTest_two(void) { return worker_case(2, XTRUE, XFALSE, XFALSE, XFALSE); }
static int XTest_four(void) { return worker_case(4, XTRUE, XFALSE, XFALSE, XFALSE); }
static int XTest_eight(void) { return worker_case(8, XTRUE, XFALSE, XFALSE, XFALSE); }
static int XTest_shared(void) { return worker_case(4, XFALSE, XFALSE, XFALSE, XFALSE); }
static int XTest_unix(void) { return worker_case(4, XTRUE, XTRUE, XFALSE, XFALSE); }
static int XTest_restart_one(void) { return worker_case(1, XTRUE, XFALSE, XTRUE, XFALSE); }
static int XTest_restart_four(void) { return worker_case(4, XTRUE, XFALSE, XTRUE, XFALSE); }
static int XTest_churn(void) { return worker_case(4, XTRUE, XFALSE, XFALSE, XTRUE); }

static int worker_tls_case(int nWorkers, xbool_t bExclusive, xbool_t bRestart, xbool_t bChurn, xbool_t bShutdown)
{
#ifdef XSOCK_USE_SSL
    tls_fixture_t identity;
    int nStatus = tls_fixture_begin(&identity);
    if (nStatus != XSTDOK) { tls_fixture_end(&identity); return 1; }
    const char *pNames[] = {"SSL_CERT_FILE", "SSL_CERT_DIR"};
    const char *pValues[] = {identity.sCert, identity.sRoot};
    char *saved[2] = {NULL, NULL};
    int nChanged = 0;
    /* Install the private trust store before a blocking client starts its handshake. */
    for (int i = 0; i < 2; i++)
    {
        const char *pValue = getenv(pNames[i]);
        if (pValue && !(saved[i] = strdup(pValue))) { nStatus = XSTDERR; break; }
        if (setenv(pNames[i], pValues[i], 1)) { nStatus = XSTDERR; break; }
        nChanged++;
    }
    if (nStatus == XSTDOK)
    {
        worker_test_t test = {.nWorkers = nWorkers, .nClients = 16, .nRounds = 12, .nBytes = 4096,
            .bExclusive = bExclusive, .bRestart = bRestart, .bChurn = bChurn, .bTLS = XTRUE, .bShutdown = bShutdown};
        XSock_InitCert(&test.cert);
        test.cert.pCertPath = identity.sCert;
        test.cert.pKeyPath = identity.sKey;
        nStatus = worker_run(&test) ? XSTDERR : XSTDOK;
    }
    for (int i = 0; i < 2; i++)
    {
        if (i < nChanged && (saved[i] ? setenv(pNames[i], saved[i], 1) : unsetenv(pNames[i]))) nStatus = XSTDERR;
        free(saved[i]);
    }
    tls_fixture_end(&identity);
    CHECK(nStatus == XSTDOK, "TLS requests, responses, worker ownership and cleanup are valid");
    return 0;
#else
    (void)nWorkers;
    (void)bExclusive;
    (void)bRestart;
    (void)bChurn;
    (void)bShutdown;
    return 77;
#endif
}

static int XTest_tls_direct(void) { return worker_tls_case(0, XTRUE, XFALSE, XFALSE, XFALSE); }
static int XTest_tls_one(void) { return worker_tls_case(1, XTRUE, XFALSE, XFALSE, XFALSE); }
static int XTest_tls_four(void) { return worker_tls_case(4, XTRUE, XFALSE, XFALSE, XFALSE); }
static int XTest_tls_shared(void) { return worker_tls_case(4, XFALSE, XFALSE, XFALSE, XFALSE); }
static int XTest_tls_restart_one(void) { return worker_tls_case(1, XTRUE, XTRUE, XFALSE, XFALSE); }
static int XTest_tls_restart_four(void) { return worker_tls_case(4, XTRUE, XTRUE, XFALSE, XFALSE); }
static int XTest_tls_churn(void) { return worker_tls_case(4, XTRUE, XFALSE, XTRUE, XFALSE); }
static int XTest_tls_shutdown_one(void) { return worker_tls_case(1, XTRUE, XFALSE, XFALSE, XTRUE); }
static int XTest_tls_shutdown_four(void) { return worker_tls_case(4, XTRUE, XFALSE, XFALSE, XTRUE); }

static int worker_shutdown_case(int nWorkers)
{
    worker_test_t test = {.nWorkers = nWorkers, .nClients = 16, .nRounds = 24, .nBytes = 257,
        .bExclusive = XTRUE, .bShutdown = XTRUE};
    return worker_run(&test);
}

static int XTest_shutdown_direct(void) { return worker_shutdown_case(0); }
static int XTest_shutdown_one(void) { return worker_shutdown_case(1); }
static int XTest_shutdown_four(void) { return worker_shutdown_case(4); }

typedef struct { int nRead; int nClosed; int nErrors; } worker_rebuild_t;

static int worker_rebuild_cb(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    worker_rebuild_t *pTest = pCtx->pApi->pUserCtx;
    if (pCtx->eCbType == XAPI_CB_ERROR) pTest->nErrors++;
    if (pCtx->eCbType == XAPI_CB_CLOSED) pTest->nClosed++;
    if (pCtx->eCbType == XAPI_CB_READ)
    {
        uint64_t nValue = 0;
        if (read(pSession->sock.nFD, &nValue, sizeof(nValue)) != sizeof(nValue) ||
            nValue != (uintptr_t)pSession->pSessionData * 101) pTest->nErrors++;
        pTest->nRead++;
    }
    return XAPI_CONTINUE;
}

static int worker_rebuild(int nEvents, int nRounds, xbool_t bBenchmark, xbool_t bLimited)
{
    worker_rebuild_t test = {0};
    xapi_t api;
    CHECK(XAPI_Init(&api, worker_rebuild_cb, &test) == XSTDOK, "Initialize a rebuild fixture");
    int *pFDs = calloc(nEvents, sizeof(*pFDs));
    CHECK(pFDs, "Allocate the registered descriptor list");
    for (int i = 0; i < nEvents; i++)
    {
        xapi_endpoint_t endpoint;
        XAPI_InitEndpoint(&endpoint);
        endpoint.eType = XAPI_SOCK;
        endpoint.eRole = XAPI_CUSTOM;
        endpoint.nEvents = XPOLLIN;
        endpoint.pSessionData = (void*)(uintptr_t)(i + 1);
        endpoint.nFD = pFDs[i] = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        CHECK(endpoint.nFD >= 0 && XAPI_AddEvent(&api, &endpoint) == XSTDOK, "Register each independent event counter");
    }
    api.events.nEventMax = nEvents < 256 ? 256 : nEvents * 2;
    if (bLimited)
    {
        struct rlimit original, limited;
        CHECK(!getrlimit(RLIMIT_NOFILE, &original), "Save the limit before rebuilding a smaller backend");
        limited = original;
        if (limited.rlim_cur > 64) limited.rlim_cur = 64;
        CHECK(!setrlimit(RLIMIT_NOFILE, &limited), "Lower capacity while existing descriptors remain open");
        int nStatus = XAPI_RebuildWorkerEvents(&api);
        int nRestore = setrlimit(RLIMIT_NOFILE, &original);
        xbool_t bValid = nStatus == XEVENTS_SUCCESS && !nRestore && api.events.nEventMax == limited.rlim_cur;
        if (!bValid) { XAPI_Destroy(&api); free(pFDs); }
        CHECK(bValid, "The published event capacity matches the smaller replacement allocation");
    }
    uint64_t nStart = worker_now();
    for (int i = 0; i < nRounds; i++)
        CHECK(XAPI_RebuildWorkerEvents(&api) == XEVENTS_SUCCESS, "Every repeated rebuild succeeds");
    uint64_t nElapsed = worker_now() - nStart;
    for (int i = 0; i < nEvents; i++)
    {
        uint64_t nValue = (uint64_t)(i + 1) * 101;
        CHECK(write(pFDs[i], &nValue, sizeof(nValue)) == sizeof(nValue), "Queue a distinct value on every descriptor");
    }
    CHECK(XAPI_RebuildWorkerEvents(&api) == XEVENTS_SUCCESS, "Rebuild with pending input on every descriptor");
    uint64_t nDeadline = worker_now() + 2000000000;
    while (test.nRead < nEvents && !test.nErrors && worker_now() < nDeadline) XAPI_Service(&api, 10);
    xbool_t bValid = XAPI_GetEventCount(&api) == (size_t)nEvents && api.events.eventsMap.nPairCount == (size_t)nEvents;
    XAPI_Destroy(&api);
    free(pFDs);
    CHECK(bValid && !test.nErrors && test.nRead == nEvents && test.nClosed == nEvents,
        "Repeated rebuilds preserve every pending value, session association and final close callback");
    if (bBenchmark) printf("{\"events\":%d,\"rounds\":%d,\"rebuild_us\":%.3f}\n",
        nEvents, nRounds, nElapsed / (1000.0 * nRounds));
    return 0;
}

static int XTest_repeated(void) { return worker_rebuild(256, 32, XFALSE, XFALSE); }
static int XTest_capacity(void) { return worker_rebuild(256, 1, XFALSE, XTRUE); }

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "rebuild"))
    {
        xlog_setfl(XLOG_NONE);
        int nEvents = atoi(argv[2]), nRounds = atoi(argv[3]);
        CHECK(nEvents > 0 && nEvents <= 4096 && nRounds > 0, "Rebuild benchmark dimensions are valid");
        return worker_rebuild(nEvents, nRounds, XTRUE, XFALSE);
    }
    if (argc == 6 && !strcmp(argv[1], "benchmark"))
    {
        xlog_setfl(XLOG_NONE);
        worker_test_t test = {.nWorkers = atoi(argv[2]), .nClients = atoi(argv[3]), .nRounds = atoi(argv[4]),
            .nBytes = atoi(argv[5]), .bExclusive = XTRUE, .bBenchmark = XTRUE};
        CHECK(test.nWorkers >= 0 && test.nWorkers <= 8 && test.nClients > 0 && test.nClients <= 32 &&
            test.nRounds > 0 && test.nBytes > 0 && test.nBytes <= 4096, "Benchmark dimensions are valid");
        return worker_run(&test);
    }

    const xtest_case_t cases[] = {
        XTEST_CASE(direct),
        XTEST_CASE(one),
        XTEST_CASE(two),
        XTEST_CASE(four),
        XTEST_CASE(eight),
        XTEST_CASE(shared),
        XTEST_CASE(unix),
        XTEST_CASE(restart_one),
        XTEST_CASE(restart_four),
        XTEST_CASE(churn),
        XTEST_CASE(tls_direct),
        XTEST_CASE(tls_one),
        XTEST_CASE(tls_four),
        XTEST_CASE(tls_shared),
        XTEST_CASE(tls_restart_one),
        XTEST_CASE(tls_restart_four),
        XTEST_CASE(tls_churn),
        XTEST_CASE(shutdown_direct),
        XTEST_CASE(shutdown_one),
        XTEST_CASE(shutdown_four),
        XTEST_CASE(tls_shutdown_one),
        XTEST_CASE(tls_shutdown_four),
        XTEST_CASE(repeated),
        XTEST_CASE(capacity)
    };

    return XTest_Run(argc, argv, cases, sizeof(cases) / sizeof(*cases));
}
