/* System call failures must preserve ownership and allow a clean retry. */
#include "test.h"
#include "api.h"
#include <fcntl.h>
#include <stdarg.h>
#include <sys/timerfd.h>
#include <unistd.h>

enum { FAULT_NONE, FAULT_SOCKET, FAULT_LISTEN, FAULT_CONNECT, FAULT_OPTION,
    FAULT_GETFL, FAULT_SETFL, FAULT_SETFD, FAULT_ACCEPT, FAULT_EPOLL, FAULT_CONTROL, FAULT_TIMER, FAULT_ARM, FAULT_READ };

static int g_nFault;
static int g_nHits;
static int g_nCreated = -1;
static int g_nAccepted = -1;
static int g_nTimer = -1;
static int g_nOption;
static int g_nOperation;
static xbool_t g_bFallback;
static int g_nReadFD = -1;

static void fault_arm(int nFault)
{
    g_nFault = nFault;
    g_nHits = 0;
    g_nCreated = g_nAccepted = g_nTimer = -1;
}

static xbool_t fault_hit(int nFault)
{
    if (g_nFault != nFault || g_nHits) return XFALSE;
    g_nHits++;
    errno = EIO;
    return XTRUE;
}

int __real_socket(int, int, int);
int __real_listen(int, int);
int __real_connect(int, const struct sockaddr*, socklen_t);
int __real_setsockopt(int, int, int, const void*, socklen_t);
int __real_fcntl(int, int, ...);
int __real_accept(int, struct sockaddr*, socklen_t*);
int __real_accept4(int, struct sockaddr*, socklen_t*, int);
int __real_epoll_create1(int);
int __real_epoll_ctl(int, int, int, struct epoll_event*);
int __real_timerfd_create(int, int);
int __real_timerfd_settime(int, int, const struct itimerspec*, struct itimerspec*);
ssize_t __real_read(int, void*, size_t);

ssize_t __wrap_read(int nFD, void *pData, size_t nSize)
{
    if (nFD == g_nReadFD && fault_hit(FAULT_READ)) return -1;
    return __real_read(nFD, pData, nSize);
}

int __wrap_socket(int nDomain, int nType, int nProtocol)
{
    if (fault_hit(FAULT_SOCKET)) return -1;
    return g_nCreated = __real_socket(nDomain, nType, nProtocol);
}

int __wrap_listen(int nFD, int nBacklog)
{
    return fault_hit(FAULT_LISTEN) ? -1 : __real_listen(nFD, nBacklog);
}

int __wrap_connect(int nFD, const struct sockaddr *pAddr, socklen_t nSize)
{
    return fault_hit(FAULT_CONNECT) ? -1 : __real_connect(nFD, pAddr, nSize);
}

int __wrap_setsockopt(int nFD, int nLevel, int nOption, const void *pValue, socklen_t nSize)
{
    if (nOption == g_nOption && fault_hit(FAULT_OPTION)) return -1;
    return __real_setsockopt(nFD, nLevel, nOption, pValue, nSize);
}

int __wrap_fcntl(int nFD, int nCommand, ...)
{
    if (nCommand == F_GETFL) return fault_hit(FAULT_GETFL) ? -1 : __real_fcntl(nFD, nCommand);
    if (nCommand == F_GETFD) return __real_fcntl(nFD, nCommand);
    va_list args;
    va_start(args, nCommand);
    int nArg = va_arg(args, int);
    va_end(args);
    if ((nCommand == F_SETFL && fault_hit(FAULT_SETFL)) || (nCommand == F_SETFD && fault_hit(FAULT_SETFD))) return -1;
    return __real_fcntl(nFD, nCommand, nArg);
}

int __wrap_accept(int nFD, struct sockaddr *pAddr, socklen_t *pSize)
{
    if (fault_hit(FAULT_ACCEPT)) return -1;
    return g_nAccepted = __real_accept(nFD, pAddr, pSize);
}

int __wrap_accept4(int nFD, struct sockaddr *pAddr, socklen_t *pSize, int nFlags)
{
    if (g_bFallback) { errno = ENOSYS; return -1; }
    if (fault_hit(FAULT_ACCEPT)) return -1;
    return g_nAccepted = __real_accept4(nFD, pAddr, pSize, nFlags);
}

int __wrap_epoll_create1(int nFlags)
{
    return fault_hit(FAULT_EPOLL) ? -1 : __real_epoll_create1(nFlags);
}

int __wrap_epoll_ctl(int nFD, int nOperation, int nPeer, struct epoll_event *pEvent)
{
    if (nOperation == g_nOperation && fault_hit(FAULT_CONTROL)) return -1;
    return __real_epoll_ctl(nFD, nOperation, nPeer, pEvent);
}

int __wrap_timerfd_create(int nClock, int nFlags)
{
    if (fault_hit(FAULT_TIMER)) return -1;
    return g_nTimer = __real_timerfd_create(nClock, nFlags);
}

int __wrap_timerfd_settime(int nFD, int nFlags, const struct itimerspec *pValue, struct itimerspec *pOld)
{
    return fault_hit(FAULT_ARM) ? -1 : __real_timerfd_settime(nFD, nFlags, pValue, pOld);
}

static xbool_t fault_closed(int nFD)
{
    return nFD < 0 || (__real_fcntl(nFD, F_GETFD) < 0 && errno == EBADF);
}

static int XTest_create(void)
{
    struct { int nFault; uint32_t nFlags; int nOption; xsock_status_t eStatus; } cases[] = {
        {FAULT_SOCKET, XSOCK_UDP_CLIENT, 0, XSOCK_ERR_CREATE},
        {FAULT_CONNECT, XSOCK_UDP_CLIENT, 0, XSOCK_ERR_CONNECT},
        {FAULT_CONNECT, XSOCK_TCP_CLIENT, 0, XSOCK_ERR_CONNECT},
        {FAULT_OPTION, XSOCK_UDP_BCAST, SO_BROADCAST, XSOCK_ERR_SETOPT},
        {FAULT_OPTION, XSOCK_UDP_MCAST, SO_REUSEADDR, XSOCK_ERR_SETOPT},
        {FAULT_GETFL, XSOCK_UDP_CLIENT | XSOCK_NB, 0, XSOCK_ERR_GETFL},
        {FAULT_SETFL, XSOCK_UDP_CLIENT | XSOCK_NB, 0, XSOCK_ERR_SETFL}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    {
        xsock_t sock;
        g_nOption = cases[i].nOption;
        fault_arm(cases[i].nFault);
        XSOCKET nFD = XSock_Create(&sock, cases[i].nFlags, "127.0.0.1", 9);
        xbool_t bClosed = fault_closed(g_nCreated);
        xsock_status_t eStatus = sock.eStatus;
        XSock_Close(&sock);
        CHECK(g_nHits == 1 && nFD == XSOCK_INVALID && eStatus == cases[i].eStatus, "Creation reports the failed system call");
        CHECK(bClosed, "A failed creation releases its socket descriptor");
        fault_arm(FAULT_NONE);
        CHECK(XSock_Create(&sock, XSOCK_UDP_CLIENT, "127.0.0.1", 9) != XSOCK_INVALID, "The same object can be created again");
        XSock_Close(&sock);
    }
    return 0;
}

static int XTest_nonblock(void)
{
    for (int nEnable = 0; nEnable < 2; nEnable++)
        for (int nFault = FAULT_GETFL; nFault <= FAULT_SETFL; nFault++)
        {
            int pair[2];
            CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "Create the option fixture");
            xsock_t sock;
            XSock_Init(&sock, XSOCK_UNIX_PEER, pair[0]);
            fault_arm(nFault);
            XSOCKET nResult = XSock_NonBlock(&sock, nEnable);
            xbool_t bClosed = fault_closed(pair[0]);
            XSock_Close(&sock);
            close(pair[1]);
            CHECK(nResult == XSOCK_INVALID && g_nHits == 1 && bClosed, "Failure to read or update flags closes the socket");
        }
    return 0;
}

static int fault_accept(int nFault, xbool_t bFallback)
{
    xsock_t listener, client, peer;
    XSock_Init(&listener, XSOCK_TCP_SERVER | XSOCK_NB, socket(AF_INET, SOCK_STREAM, 0));
    XSock_Init(&client, XSOCK_TCP_CLIENT, XSOCK_INVALID);
    XSock_Init(&peer, XSOCK_TCP_PEER, XSOCK_INVALID);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t nSize = sizeof(addr);
    CHECK(bind(listener.nFD, (struct sockaddr*)&addr, sizeof(addr)) == 0 && listen(listener.nFD, 4) == 0 &&
        getsockname(listener.nFD, (struct sockaddr*)&addr, &nSize) == 0, "Create an ephemeral listener");
    CHECK(XSock_Create(&client, XSOCK_TCP_CLIENT, "127.0.0.1", ntohs(addr.sin_port)) >= 0, "Queue the actual connection");
    fault_arm(nFault);
    g_bFallback = bFallback;
    XSOCKET nResult = XSock_Accept(&listener, &peer);
    xbool_t bClosed = fault_closed(g_nAccepted);
    int nDescriptorFlags = peer.nFD >= 0 ? __real_fcntl(peer.nFD, F_GETFD) : -1;
    int nStatusFlags = peer.nFD >= 0 ? __real_fcntl(peer.nFD, F_GETFL) : -1;
    g_bFallback = XFALSE;
    XSock_Close(&peer);
    XSock_Close(&client);
    XSock_Close(&listener);
    if (nFault)
    {
        CHECK(nResult == XSOCK_INVALID && g_nHits == 1, "A failed accept or flag update cannot hand back a usable peer");
        CHECK(bClosed, "The failed accepted peer was closed");
    }
    else CHECK(nResult >= 0 && (nDescriptorFlags & FD_CLOEXEC) && (nStatusFlags & O_NONBLOCK),
        "The accept fallback preserves nonblocking and close-on-exec semantics");
    return 0;
}

static int XTest_accept(void)
{
    CHECK(fault_accept(FAULT_NONE, XTRUE) == 0, "Accept succeeds on a kernel without accept4");
    CHECK(fault_accept(FAULT_ACCEPT, XFALSE) == 0, "accept4 failure preserves the listener");
    CHECK(fault_accept(FAULT_ACCEPT, XTRUE) == 0, "accept failure preserves the listener");
    CHECK(fault_accept(FAULT_GETFL, XTRUE) == 0, "Accepted peer flag lookup failure is reported");
    CHECK(fault_accept(FAULT_SETFL, XTRUE) == 0, "Accepted peer nonblocking failure is reported");
    CHECK(fault_accept(FAULT_SETFD, XTRUE) == 0, "Accepted peer inheritance protection failure is reported");
    return 0;
}

typedef struct { int nErrors; int nClosed; int nDestroyed; xapi_session_t *pSession; } fault_api_t;

static int fault_api_cb(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    fault_api_t *pTest = pCtx->pApi->pUserCtx;
    if (pCtx->eCbType == XAPI_CB_ERROR) pTest->nErrors++;
    if (pCtx->eCbType == XAPI_CB_LISTENING || pCtx->eCbType == XAPI_CB_REGISTERED) pTest->pSession = pSession;
    if (pCtx->eCbType == XAPI_CB_CLOSED) { pTest->pSession = NULL; pTest->nClosed++; }
    if (pCtx->eCbType == XAPI_CB_STATUS && pCtx->nStatus == XAPI_DESTROY) pTest->nDestroyed++;
    return XAPI_CONTINUE;
}

static int XTest_api_setup(void)
{
    char root[] = "/tmp/xutils-sys-fault-XXXXXX";
    CHECK(mkdtemp(root) != NULL, "Create the private listener directory");
    char path[108];
    snprintf(path, sizeof(path), "%s/peer.sock", root);
    const int faults[] = {FAULT_LISTEN, FAULT_EPOLL, FAULT_CONTROL};
    for (size_t i = 0; i < sizeof(faults) / sizeof(*faults); i++)
    {
        xapi_t api;
        fault_api_t test = {0};
        CHECK(XAPI_Init(&api, fault_api_cb, &test) == XSTDOK, "Initialize the API server");
        xapi_endpoint_t endpoint;
        XAPI_InitEndpoint(&endpoint);
        endpoint.eType = XAPI_HTTP;
        endpoint.eRole = XAPI_SERVER;
        endpoint.bUnix = XTRUE;
        endpoint.pAddr = path;
        endpoint.bForce = XTRUE;
        g_nOperation = EPOLL_CTL_ADD;
        fault_arm(faults[i]);
        int nStatus = XAPI_Listen(&api, &endpoint);
        xbool_t bClosed = fault_closed(g_nCreated);
        CHECK(g_nHits == 1 && nStatus < 0 && test.nErrors == 1 && !test.pSession, "Failed server setup reports one error");
        CHECK(!XAPI_GetEventCount(&api) && bClosed, "Failed server setup retains no descriptor or registration");
        fault_arm(FAULT_NONE);
        CHECK(XAPI_Listen(&api, &endpoint) == XSTDOK && test.pSession, "The same API can retry the same endpoint");
        CHECK(XAPI_GetEventCount(&api) == 1, "Only the successfully retried listener is registered");
        api.events.nEventMax = 4;
        XAPI_Destroy(&api);
    }
    unlink(path);
    rmdir(root);
    return 0;
}

static int XTest_api_timer(void)
{
    xapi_t api;
    fault_api_t test = {0};
    CHECK(XAPI_Init(&api, fault_api_cb, &test) == XSTDOK, "Initialize the timer fixture");
    int pair[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "Create an API peer");
    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_SERVER;
    endpoint.nFD = pair[0];
    endpoint.nEvents = XPOLLIN;
    CHECK(XAPI_AddEvent(&api, &endpoint) == XSTDOK, "Register the timer owner");
    xevent_data_t *pEvent = XEvents_GetData(&api.events, pair[0]);
    xapi_session_t *pSession = pEvent ? pEvent->pContext : NULL;
    CHECK(pSession != NULL, "Look up the registered timer owner");
    api.events.nEventMax = 4;
    const int faults[] = {FAULT_TIMER, FAULT_ARM, FAULT_CONTROL};
    for (size_t i = 0; i < sizeof(faults) / sizeof(*faults); i++)
    {
        int nBefore = test.nErrors;
        g_nOperation = EPOLL_CTL_ADD;
        fault_arm(faults[i]);
        int nStatus = XAPI_AddTimer(pSession, 60000);
        xbool_t bClosed = fault_closed(g_nTimer);
        xbool_t bEmpty = pSession->pTimer == NULL;
        if (pSession->pTimer) XAPI_DeleteTimer(pSession);
        CHECK(nStatus < 0 && g_nHits == 1 && test.nErrors == nBefore + 1, "A failed timer creation is reported once");
        CHECK(bClosed && bEmpty && XAPI_GetEventCount(&api) == 1, "A failed timer keeps its owner but leaves no timer");
    }
    fault_arm(FAULT_NONE);
    CHECK(XAPI_AddTimer(pSession, 60000) == XSTDOK, "Timer creation recovers after a failed attempt");
    fault_arm(FAULT_ARM);
    CHECK(XAPI_ExtendTimer(pSession, 30000) < 0 && g_nHits == 1, "A failed extension is reported");
    struct itimerspec remaining;
    CHECK(timerfd_gettime(pSession->pTimer->nFD, &remaining) == 0 && remaining.it_value.tv_sec > 30,
        "A failed extension preserves the previously armed timer");
    g_nOperation = EPOLL_CTL_MOD;
    fault_arm(FAULT_CONTROL);
    CHECK(XAPI_SetEvents(pSession, XPOLLOUT) < 0 && pSession->nEvents == XPOLLIN, "A failed modification preserves old interest");
    fault_arm(FAULT_NONE);
    CHECK(XAPI_SetEvents(pSession, XPOLLIO) == XSTDOK, "Readiness modification can be retried");
    XAPI_Destroy(&api);
    close(pair[1]);
    return 0;
}

static int fault_event_cb(void *pLoop, void *pData, XSOCKET nFD, xevent_cb_type_t eType)
{
    (void)pData;
    (void)nFD;
    xevents_t *pEvents = pLoop;
    int *pDestroyed = pEvents->pUserSpace;
    if (eType == XEVENT_CB_DESTROY) (*pDestroyed)++;
    return XEVENTS_CONTINUE;
}

static int fault_destroy(xbool_t bAPI)
{
    int nSaved = dup(STDIN_FILENO);
    int nGuard = open("/dev/null", O_RDONLY);
    CHECK(nGuard >= 0, "Keep an unrelated descriptor open across repeated destruction");
    if (nGuard == STDIN_FILENO) nGuard = dup(nGuard);
    CHECK(nGuard >= 0, "Retain a separate copy of the unrelated descriptor");
    xbool_t bPreserved = XTRUE, bOnce = XTRUE, bInactive = XTRUE;
    for (int nHash = 0; nHash < 2; nHash++)
    {
        CHECK(dup2(nGuard, STDIN_FILENO) == STDIN_FILENO, "Give descriptor zero an unrelated owner");
        if (bAPI)
        {
            xapi_t api;
            fault_api_t test = {0};
            int pair[2];
            CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "Create the API lifecycle transport");
            CHECK(XAPI_Init(&api, fault_api_cb, &test) == XSTDOK, "Initialize the lifecycle API");
            api.bUseHashMap = nHash;
            xapi_endpoint_t endpoint;
            XAPI_InitEndpoint(&endpoint);
            endpoint.eType = XAPI_SOCK;
            endpoint.eRole = XAPI_PEER;
            endpoint.nFD = pair[0];
            endpoint.nEvents = XPOLLIN;
            CHECK(XAPI_AddEvent(&api, &endpoint) == XSTDOK && test.pSession, "Register an owned API peer");
            api.events.nEventMax = 4;
            if (!nHash) CHECK(XEvents_Delete(&api.events, test.pSession->pEvData) == XEVENTS_SUCCESS,
                "Without a hash map the caller explicitly releases its registration");
            XAPI_Destroy(&api);
            XAPI_Destroy(&api);
            if (test.nClosed != 1 || test.nDestroyed != 1 || !fault_closed(pair[0])) bOnce = XFALSE;
            if (api.bHaveEvents || XAPI_Service(&api, 0) != XEVENTS_EINVALID) bInactive = XFALSE;
            close(pair[1]);
        }
        else
        {
            xevents_t events;
            int nDestroyed = 0;
            CHECK(XEvents_Create(&events, 4, &nDestroyed, fault_event_cb, nHash) == XEVENTS_SUCCESS,
                "Create the lifecycle event loop");
            xevent_data_t *pTimer = XEvents_AddTimer(&events, NULL, 60000);
            CHECK(pTimer != NULL, "Create an owned timer");
            int nTimer = pTimer->nFD;
            if (!nHash) CHECK(XEvents_Delete(&events, pTimer) == XEVENTS_SUCCESS,
                "Without a hash map the caller explicitly removes its timer");
            XEvents_Destroy(&events);
            XEvents_Destroy(&events);
            if (nDestroyed != 1 || !fault_closed(nTimer)) bOnce = XFALSE;
            if (events.nEventFd != XSOCK_INVALID || events.nEventCount) bInactive = XFALSE;
        }
        if (fcntl(STDIN_FILENO, F_GETFD) < 0) bPreserved = XFALSE;
    }
    if (nSaved >= 0) { dup2(nSaved, STDIN_FILENO); close(nSaved); }
    else close(STDIN_FILENO);
    close(nGuard);
    CHECK(bPreserved, "Repeated event or API destruction must never close an unrelated descriptor zero");
    CHECK(bOnce, "Owned registrations and the destruction callback are released exactly once");
    CHECK(bInactive, "A destroyed loop exposes no live backend or event registrations");
    return 0;
}

static int XTest_event_destroy(void) { return fault_destroy(XFALSE); }
static int XTest_api_destroy(void) { return fault_destroy(XTRUE); }

typedef struct {
    int nAction;
    int nErrors;
    int nCleared;
    int nRead;
    int nUser;
    xbool_t bDefer;
    xbool_t bFailed;
} fault_dispatch_t;

static int fault_dispatch_cb(void *pLoop, void *pData, XSOCKET nFD, xevent_cb_type_t eType)
{
    (void)pData;
    xevents_t *pEvents = pLoop;
    fault_dispatch_t *pTest = pEvents->pUserSpace;
    if (eType == XEVENT_CB_CLEAR) pTest->nCleared++;
    if (eType == XEVENT_CB_READ)
    {
        char cByte = 0;
        pTest->nRead++;
        if (read(nFD, &cByte, 1) != 1 || cByte != 'x') pTest->bFailed = XTRUE;
        return XEVENTS_USERCALL;
    }
    if (eType == XEVENT_CB_USER)
    {
        pTest->nUser++;
        return pTest->nUser == 1 ? XEVENTS_USERCALL : pTest->nAction;
    }
    if (eType == XEVENT_CB_ERROR)
    {
        pTest->nErrors++;
        return pTest->bDefer ? XEVENTS_USERCALL : pTest->nAction;
    }
    return XEVENTS_CONTINUE;
}

static int XTest_timer_read(void)
{
    const int actions[] = {XEVENTS_DISCONNECT, XEVENTS_CONTINUE, XEVENTS_BREAK};
    for (int nHash = 0; nHash < 2; nHash++)
        for (int nDefer = 0; nDefer < 2; nDefer++)
            for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); i++)
            {
                xevents_t events;
                fault_dispatch_t test = {0};
                test.nAction = actions[i];
                test.bDefer = nDefer;
                CHECK(XEvents_Create(&events, 4, &test, fault_dispatch_cb, nHash) == XEVENTS_SUCCESS, "Create the failing timer loop");
                xevent_data_t *pTimer = XEvents_AddTimer(&events, NULL, 1);
                CHECK(pTimer != NULL, "Arm a real timerfd");
                g_nReadFD = pTimer->nFD;
                fault_arm(FAULT_READ);
                int nStatus = XEvents_Service(&events, 1000);
                int nHits = g_nHits;
                xbool_t bRemoved = !events.nEventCount && test.nCleared == 1 && fault_closed(g_nReadFD);
                fault_arm(FAULT_NONE);
                g_nReadFD = -1;
                XEvents_Destroy(&events);
                CHECK(nHits == 1 && test.nErrors == 1 && !test.nRead, "A timer read failure reaches the error callback exactly once");
                CHECK(bRemoved && test.nCleared == 1, "The failed timer is released once for every callback response");
                CHECK(test.nUser == (nDefer ? 2 : 0), "Deferred error handling completes the requested user callback sequence");
                CHECK(nStatus == (actions[i] == XEVENTS_BREAK ? XEVENTS_EBREAK : XEVENTS_SUCCESS),
                    "The loop preserves an error callback's request to stop service");
            }
    return 0;
}

static int XTest_user_actions(void)
{
    const int actions[] = {XEVENTS_DISCONNECT, XEVENTS_CONTINUE, XEVENTS_BREAK, XEVENTS_ACCEPT};
    for (int nHash = 0; nHash < 2; nHash++)
        for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); i++)
        {
            int pair[2];
            CHECK(pipe(pair) == 0, "Create a real readable event");
            xevents_t events;
            fault_dispatch_t test = {0};
            test.nAction = actions[i];
            CHECK(XEvents_Create(&events, 4, &test, fault_dispatch_cb, nHash) == XEVENTS_SUCCESS, "Create the user callback loop");
            xevent_data_t *pEvent = XEvents_RegisterEvent(&events, NULL, pair[0], XPOLLIN, XEVENT_TYPE_CUSTOM);
            CHECK(pEvent && write(pair[1], "x", 1) == 1, "Publish the exact event byte");
            int nStatus = XEvents_Service(&events, 1000);
            xbool_t bRemoved = test.nCleared == 1 && !events.nEventCount;
            if (!nHash && !test.nCleared) XEvents_Delete(&events, pEvent);
            XEvents_Destroy(&events);
            close(pair[0]);
            close(pair[1]);
            CHECK(test.nRead == 1 && test.nUser == 2 && !test.nErrors && !test.bFailed, "The event and both requested user calls run once");
            CHECK(bRemoved == (actions[i] == XEVENTS_DISCONNECT), "A deferred disconnect removes its event during service");
            CHECK(test.nCleared == 1 && nStatus == (actions[i] == XEVENTS_BREAK ? XEVENTS_EBREAK : XEVENTS_SUCCESS),
                "Deferred actions retain their meaning and clear ownership exactly once");
        }
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(create),
    XTEST_CASE(nonblock),
    XTEST_CASE(accept),
    XTEST_CASE(api_setup),
    XTEST_CASE(api_timer),
    XTEST_CASE(event_destroy),
    XTEST_CASE(api_destroy),
    XTEST_CASE(timer_read),
    XTEST_CASE(user_actions)
)
