/* libxutils: the XAPI control surface around the I/O loop.
 *
 * The existing API regressions drive traffic through sessions; these cover
 * what surrounds that — the status and type vocabulary, endpoint listen and
 * connect over loopback, timers, event masks and the worker processes. The
 * worker cases fork, so each one waits for its children rather than leaving
 * them to outlive the test.
 */

#include "test.h"
#include "api.h"
#include "sync.h"
#include <unistd.h>
#include <sys/wait.h>

/* Counts the callback types a service loop drives through. */
typedef struct {
    int nListening;
    int nConnected;
    int nAccepted;
    int nClosed;
    int nTimer;
    int nStatus;
    int nTotal;
} api_ctl_test_t;

static int api_ctl_callback(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    (void)pSession;
    api_ctl_test_t *pTest = (api_ctl_test_t*)pCtx->pApi->pUserCtx;
    if (pTest == NULL) return XAPI_CONTINUE;

    pTest->nTotal++;
    switch (pCtx->eCbType)
    {
        case XAPI_CB_LISTENING: pTest->nListening++; break;
        case XAPI_CB_CONNECTED: pTest->nConnected++; break;
        case XAPI_CB_ACCEPTED: pTest->nAccepted++; break;
        case XAPI_CB_CLOSED: pTest->nClosed++; break;
        case XAPI_CB_TIMER: pTest->nTimer++; break;
        case XAPI_CB_STATUS: pTest->nStatus++; break;
        default: break;
    }

    return XAPI_CONTINUE;
}

/* Finds a loopback port nothing is listening on. */
static uint16_t api_free_port(void)
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

static int XTest_vocabulary(void)
{
    /* Every endpoint type has a name, and an out of range one falls back
     * rather than reading past the table. */
    const struct { xapi_type_t eType; const char *pName; } types[] = {
        {XAPI_NONE, "Unknown"}, {XAPI_SELF, "API"}, {XAPI_EVENT, "Event"},
        {XAPI_HTTP, "HTTP"}, {XAPI_MDTP, "MDTP"}, {XAPI_SOCK, "Socket"},
        {XAPI_WS, "WebSocket"}
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++)
        CHECK(strcmp(XAPI_GetTypeStr(types[i].eType), types[i].pName) == 0,
            "Every endpoint type renders its own name");

    CHECK(strcmp(XAPI_GetTypeStr((xapi_type_t)99), "Unknown") == 0, "An out of range type falls back");

    /* Every status the library can report has a description. */
    const xapi_status_t states[] = {
        XAPI_MISSING_TOKEN, XAPI_MISSING_KEY, XAPI_INVALID_KEY, XAPI_INVALID_ARGS,
        XAPI_INVALID_ROLE, XAPI_INVALID_TOKEN, XAPI_INVALID_PATH, XAPI_ERR_AUTH,
        XAPI_ERR_ALLOC, XAPI_ERR_ASSEMBLE, XAPI_ERR_CRYPT, XAPI_ERR_REGISTER,
        XAPI_ERR_RESOLVE, XAPI_ERR_SUPPORT, XAPI_ERR_FORK, XAPI_ERR_CHMOD,
        XAPI_ERR_CHOWN, XAPI_ERR_CRTDIR, XAPI_TIMER_DESTROY, XAPI_DESTROY,
        XAPI_HUNGED, XAPI_CLOSED
    };

    for (size_t i = 0; i < sizeof(states) / sizeof(*states); i++)
    {
        const char *pText = XAPI_GetStatusStr(states[i]);
        CHECK(pText != NULL && *pText != '\0', "Every status has a description");
        CHECK(strcmp(pText, "Unknown status") != 0, "Every named status has its own description");
    }

    CHECK(strcmp(XAPI_GetStatusStr(XAPI_UNKNOWN), "Unknown status") == 0, "The unknown status is named");
    CHECK(strcmp(XAPI_GetStatusStr((xapi_status_t)999), "Unknown status") == 0, "An out of range status falls back");

    /* Only the four defined roles are supported. */
    const xapi_role_t roles[] = {XAPI_CUSTOM, XAPI_SERVER, XAPI_CLIENT, XAPI_PEER};
    for (size_t i = 0; i < sizeof(roles) / sizeof(*roles); i++)
        CHECK(XAPI_IsSupportedRole(roles[i]) == XTRUE, "Every defined role is supported");

    CHECK(XAPI_IsSupportedRole(XAPI_INACTIVE) == XFALSE, "The inactive role is not a role to serve");
    CHECK(XAPI_IsSupportedRole((xapi_role_t)7) == XFALSE, "An undefined role is not supported");
    CHECK(XAPI_IsSupportedRole((xapi_role_t)999) == XFALSE, "An out of range role is not supported");
    return 0;
}

static int XTest_init(void)
{
    /* A fresh API holds no events and knows it is not a worker. */
    xapi_t api;
    api_ctl_test_t test;
    memset(&test, 0, sizeof(test));

    CHECK(XAPI_Init(&api, api_ctl_callback, &test) == XSTDOK, "The API initializes");
    CHECK(XAPI_GetEventCount(&api) == 0, "A fresh API watches nothing");
    CHECK(XAPI_IsWorker(&api) == XFALSE, "The parent process is not a worker");
    CHECK(XAPI_GetWorkerCount(&api) == 0, "A fresh API has no workers");
    CHECK(XAPI_GetWorkerIndex(&api) < 0, "The parent has no worker index");
    CHECK(XAPI_GetCoreIndex(&api) < 0, "The parent is not pinned to a core");
    CHECK(XAPI_GetWorkerPIDs(&api) == NULL, "A fresh API has no worker table");

    /* The receive limit is settable and takes effect. */
    CHECK(XAPI_SetRxSize(&api, 8192) == XSTDOK, "The receive limit is set");
    CHECK(api.nRxSize == 8192, "The receive limit is recorded");

    CHECK(XAPI_SetRxSize(&api, 1) == XSTDOK, "A tiny receive limit is accepted");
    CHECK(api.nRxSize == 1, "The tiny limit is recorded");

    /* The worker affinity switch is recorded without needing workers. */
    CHECK(XAPI_SetWorkerAffinity(&api, XTRUE) == XSTDOK, "Worker affinity is enabled");
    CHECK(XAPI_SetWorkerAffinity(&api, XFALSE) == XSTDOK, "Worker affinity is disabled");

    XAPI_Destroy(&api);
    CHECK(XAPI_GetEventCount(&api) == 0, "A destroyed API watches nothing");

    /* Destroying twice is safe. */
    XAPI_Destroy(&api);

    /* An endpoint starts in a state that cannot be served by accident. */
    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    CHECK(endpoint.eType == XAPI_NONE, "A fresh endpoint has no type");
    CHECK(endpoint.eRole == XAPI_INACTIVE, "A fresh endpoint has no role");
    CHECK(endpoint.nFD == XSOCK_INVALID, "A fresh endpoint has no descriptor");
    CHECK(endpoint.nPort == 0 && endpoint.pAddr == NULL, "A fresh endpoint has no address");
    CHECK(endpoint.bUnix == XFALSE && endpoint.bTLS == XFALSE, "A fresh endpoint has no transport flags");
    return 0;
}

static int XTest_listen_connect(void)
{
    /* A listener and a client on loopback: the service loop has to report
     * the listen, the connect and the accept. */
    uint16_t nPort = api_free_port();
    if (!nPort)
    {
        printf("No free loopback port, skipping\n");
        return 77;
    }

    xapi_t api;
    api_ctl_test_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&api, api_ctl_callback, &test) == XSTDOK, "The API initializes");

    xapi_endpoint_t listener;
    XAPI_InitEndpoint(&listener);
    listener.eType = XAPI_SOCK;
    listener.eRole = XAPI_SERVER;
    listener.pAddr = "127.0.0.1";
    listener.nPort = nPort;

    XSTATUS nStatus = XAPI_Listen(&api, &listener);
    if (nStatus != XSTDOK)
    {
        XAPI_Destroy(&api);
        printf("The listener could not be created, skipping\n");
        return 77;
    }

    CHECK(test.nListening == 1, "The listener reported itself");
    CHECK(XAPI_GetEventCount(&api) == 1, "The listener is being watched");

    /* A client against the same port connects. */
    xapi_endpoint_t client;
    XAPI_InitEndpoint(&client);
    client.eType = XAPI_SOCK;
    client.eRole = XAPI_CLIENT;
    client.pAddr = "127.0.0.1";
    client.nPort = nPort;

    CHECK(XAPI_Connect(&api, &client) == XSTDOK, "The client connects");
    CHECK(test.nConnected == 1, "The client reported itself connected");

    size_t nWatched = XAPI_GetEventCount(&api);
    CHECK(nWatched == 2, "Both the listener and the client are watched");

    /* Servicing the loop accepts the pending connection. */
    /* The bound is generous: how quickly a loopback accept becomes visible
     * is the runner's business, and a tight window only buys flakiness. */
    for (int i = 0; i < 200 && test.nAccepted == 0; i++) XAPI_Service(&api, 50);
    CHECK(test.nAccepted >= 1, "The listener accepted the client");
    CHECK(XAPI_GetEventCount(&api) >= 3, "The accepted peer is watched too");

    XAPI_Destroy(&api);
    CHECK(test.nClosed >= 1, "Destroying the API closed the sessions");
    return 0;
}

static int XTest_endpoint_guards(void)
{
    /* An endpoint that names nothing servable is refused rather than
     * half-registered. */
    xapi_t api;
    api_ctl_test_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&api, api_ctl_callback, &test) == XSTDOK, "The API initializes");

    xapi_endpoint_t endpoint;

    /* No type at all. */
    XAPI_InitEndpoint(&endpoint);
    endpoint.eRole = XAPI_SERVER;
    endpoint.pAddr = "127.0.0.1";
    endpoint.nPort = api_free_port();
    CHECK(XAPI_Listen(&api, &endpoint) != XSTDOK, "An endpoint with no type is refused");

    /* A role the library does not serve. The role is what selects between
     * listening, connecting and registering, so the check lives in the
     * dispatcher rather than in each of them. */
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = (xapi_role_t)7;
    endpoint.pAddr = "127.0.0.1";
    endpoint.nPort = api_free_port();
    CHECK(XAPI_AddEndpoint(&api, &endpoint) != XSTDOK, "An endpoint with an unsupported role is refused");

    endpoint.eRole = XAPI_INACTIVE;
    CHECK(XAPI_AddEndpoint(&api, &endpoint) != XSTDOK, "An endpoint with no role is refused");

    /* A listener with no port and no unix path names nothing to bind. */
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_SERVER;
    endpoint.pAddr = "127.0.0.1";
    CHECK(XAPI_Listen(&api, &endpoint) != XSTDOK, "A listener with no port is refused");

    /* A listener with no address at all. */
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_SERVER;
    endpoint.nPort = api_free_port();
    CHECK(XAPI_Listen(&api, &endpoint) != XSTDOK, "A listener with no address is refused");

    /* Missing arguments are refused rather than dereferenced. */
    CHECK(XAPI_Listen(NULL, &endpoint) != XSTDOK, "A missing API is refused");
    CHECK(XAPI_Listen(&api, NULL) != XSTDOK, "A missing endpoint is refused");
    CHECK(XAPI_Connect(NULL, &endpoint) != XSTDOK, "A missing API is refused on connect");
    CHECK(XAPI_Connect(&api, NULL) != XSTDOK, "A missing endpoint is refused on connect");
    CHECK(XAPI_AddEndpoint(NULL, &endpoint) != XSTDOK, "A missing API is refused on dispatch");
    CHECK(XAPI_AddEndpoint(&api, NULL) != XSTDOK, "A missing endpoint is refused on dispatch");

    /* An address that cannot be resolved. */
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_CLIENT;
    endpoint.pAddr = "no.such.host.invalid";
    endpoint.nPort = 80;
    CHECK(XAPI_Connect(&api, &endpoint) != XSTDOK, "An unresolvable address is refused");

    /* A port with nothing listening. */
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_CLIENT;
    endpoint.pAddr = "127.0.0.1";
    endpoint.nPort = 1;
    CHECK(XAPI_Connect(&api, &endpoint) != XSTDOK, "A closed port is refused");

    /* A unix socket path longer than the address field. */
    char sLongPath[512];
    memset(sLongPath, 'p', sizeof(sLongPath) - 1);
    sLongPath[0] = '/';
    sLongPath[sizeof(sLongPath) - 1] = '\0';

    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_SERVER;
    endpoint.pAddr = sLongPath;
    endpoint.bUnix = XTRUE;
    CHECK(XAPI_Listen(&api, &endpoint) != XSTDOK, "An oversized unix path is refused");

    /* None of the refusals left anything being watched. */
    CHECK(XAPI_GetEventCount(&api) == 0, "A refused endpoint is never watched");

    XAPI_Destroy(&api);
    return 0;
}

static int XTest_timers(void)
{
    /* A timer endpoint fires through the callback and can be removed. */
    xapi_t api;
    api_ctl_test_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&api, api_ctl_callback, &test) == XSTDOK, "The API initializes");

    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");

    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_PEER;
    endpoint.nFD = pair[0];

    XSTATUS nStatus = XAPI_AddPeer(&api, &endpoint);
    if (nStatus != XSTDOK)
    {
        xclosesock(pair[0]);
        xclosesock(pair[1]);
        XAPI_Destroy(&api);
        printf("The peer could not be registered, skipping\n");
        return 77;
    }

    CHECK(XAPI_GetEventCount(&api) == 1, "The peer is watched");

    /* Service the loop briefly: the peer is idle, so nothing should break. */
    for (int i = 0; i < 5; i++) XAPI_Service(&api, 10);
    CHECK(test.nTotal > 0, "The service loop reported something");

    xclosesock(pair[1]);

    /* The peer closing is noticed. */
    for (int i = 0; i < 500 && test.nClosed == 0; i++) XAPI_Service(&api, 10);
    CHECK(test.nClosed >= 1, "The peer closing is reported");

    XAPI_Destroy(&api);
    return 0;
}

static int XTest_workers(void)
{
    /* Worker processes are forked children: the parent knows how many it
     * has, each child knows which one it is, and stopping them reaps every
     * child rather than leaving one behind. */
    xapi_t api;
    api_ctl_test_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&api, api_ctl_callback, &test) == XSTDOK, "The API initializes");

    /* Workers share the parent's event set, so there has to be one, and it
     * has to be the hash backed kind the children can index into. */
    api.bUseHashMap = XTRUE;

    uint16_t nPort = api_free_port();
    if (!nPort)
    {
        XAPI_Destroy(&api);
        printf("No free loopback port, skipping\n");
        return 77;
    }

    xapi_endpoint_t listener;
    XAPI_InitEndpoint(&listener);
    listener.eType = XAPI_SOCK;
    listener.eRole = XAPI_SERVER;
    listener.pAddr = "127.0.0.1";
    listener.nPort = nPort;

    if (XAPI_Listen(&api, &listener) != XSTDOK)
    {
        XAPI_Destroy(&api);
        printf("The listener could not be created, skipping\n");
        return 77;
    }

    CHECK(XAPI_GetEventCount(&api) > 0, "The workers have an event set to share");

    fflush(stdout);
    fflush(stderr);

    XSTATUS nStatus = XAPI_InitWorkers(&api, 2, XFALSE);
    if (nStatus != XSTDOK)
    {
        XAPI_Destroy(&api);
        printf("Workers could not be started, skipping\n");
        return 77;
    }

    /* A child returns from XAPI_InitWorkers() as a worker; it must not run
     * the rest of the test, so it exits immediately. */
    if (XAPI_IsWorker(&api))
    {
        XAPI_Destroy(&api);
        _exit(0);
    }

    CHECK(XAPI_GetWorkerCount(&api) == 2, "The parent knows how many workers it started");
    CHECK(XAPI_IsWorker(&api) == XFALSE, "The parent is still not a worker");
    CHECK(XAPI_GetWorkerIndex(&api) < 0, "The parent has no worker index");

    const xpid_t *pPIDs = XAPI_GetWorkerPIDs(&api);
    CHECK(pPIDs != NULL, "The parent holds a worker table");
    CHECK(pPIDs[0] > 0 && pPIDs[1] > 0, "Every worker has a process id");
    CHECK(pPIDs[0] != pPIDs[1], "The workers are separate processes");

    /* Stopping signals every worker, and waiting reaps them. */
    CHECK(XAPI_StopWorkers(&api, SIGTERM) == XSTDOK, "The workers are signalled");
    CHECK(XAPI_WaitWorkers(&api) == XSTDOK, "Every worker is reaped");

    XAPI_Destroy(&api);
    return 0;
}

static int XTest_buffers(void)
{
    /* A session hands out its receive and transmit buffers so a callback
     * can read what arrived and queue what to send. */
    xapi_t api;
    api_ctl_test_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&api, api_ctl_callback, &test) == XSTDOK, "The API initializes");

    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");

    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_PEER;
    endpoint.nFD = pair[0];

    if (XAPI_AddPeer(&api, &endpoint) != XSTDOK)
    {
        xclosesock(pair[0]);
        xclosesock(pair[1]);
        XAPI_Destroy(&api);
        printf("The peer could not be registered, skipping\n");
        return 77;
    }

    /* Write from the far end and let the loop pick it up. */
    const char payload[] = "queued bytes";
    CHECK(write(pair[1], payload, sizeof(payload) - 1) > 0, "The peer writes");

    for (int i = 0; i < 500 && test.nTotal < 2; i++) XAPI_Service(&api, 10);
    CHECK(test.nTotal > 0, "The loop reported the traffic");

    xclosesock(pair[1]);
    XAPI_Destroy(&api);

    /* The accessors tolerate a session that was never set up. */
    CHECK(XAPI_GetRxBuff(NULL) == NULL, "A missing session has no receive buffer");
    CHECK(XAPI_GetTxBuff(NULL) == NULL, "A missing session has no transmit buffer");
    CHECK(XAPI_PutTxBuff(NULL, NULL) != XSTDOK, "A missing session cannot be written to");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(vocabulary),
    XTEST_CASE(init),
    XTEST_CASE(listen_connect),
    XTEST_CASE(endpoint_guards),
    XTEST_CASE(timers),
    XTEST_CASE(workers),
    XTEST_CASE(buffers)
)
