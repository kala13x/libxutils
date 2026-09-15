/* libxutils: the event dispatch paths of the XAPI loop.
 *
 * Every poll result the backend can hand back reaches the user callback as a
 * distinct reason: a half closed peer, a hung up descriptor, a timer, a
 * signal that cut the wait short, a user callback the previous answer asked
 * for. Each of those is produced here for real rather than simulated, so the
 * mapping from what the kernel reported to what the callback is told is what
 * gets asserted.
 */

#include "test.h"
#include "api.h"
#include "xtime.h"
#include "thread.h"
#include "sync.h"
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

typedef struct {
    xapi_t api;

    int nListening;
    int nAccepted;
    int nConnected;
    int nClosedCb;
    int nRead;
    int nWrite;
    int nErrors;
    int nStatus;
    int nTimeouts;
    int nTimers;
    int nInterrupts;
    int nUserCbs;
    int nTicks;
    int nRegistered;

    int nHunged;
    int nClosedStatus;
    int nInvalidRole;
    int nDestroyed;

    int nTickSessions;
    int nInterruptSessions;

    /* What a peer should ask to watch once it has been accepted. */
    int nPeerEvents;

    xbool_t bAskUserCb;
    xbool_t bExtendOnAccept;
    const char *pPayload;
} api_ev_t;

static uint16_t api_ev_port(void)
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

static int api_ev_callback(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    api_ev_t *pTest = (api_ev_t*)pCtx->pApi->pUserCtx;

    switch (pCtx->eCbType)
    {
        case XAPI_CB_LISTENING: pTest->nListening++; break;
        case XAPI_CB_CONNECTED: pTest->nConnected++; break;
        case XAPI_CB_REGISTERED: pTest->nRegistered++; break;
        case XAPI_CB_CLOSED: pTest->nClosedCb++; break;
        case XAPI_CB_WRITE: pTest->nWrite++; break;
        case XAPI_CB_TIMER: pTest->nTimers++; break;
        default: break;
    }

    if (pCtx->eCbType == XAPI_CB_TICK)
    {
        /* The tick is the loop's own heartbeat, so it arrives with no
         * session attached. A callback that dereferenced one would fault
         * on the very first service call. */
        pTest->nTicks++;
        if (pSession != NULL) pTest->nTickSessions++;
        return XAPI_CONTINUE;
    }

    if (pCtx->eCbType == XAPI_CB_INTERRUPT)
    {
        pTest->nInterrupts++;
        if (pSession != NULL) pTest->nInterruptSessions++;
        return XAPI_CONTINUE;
    }

    if (pCtx->eCbType == XAPI_CB_USER)
    {
        pTest->nUserCbs++;
        return XAPI_CONTINUE;
    }

    if (pCtx->eCbType == XAPI_CB_ERROR)
    {
        pTest->nErrors++;
        if (pCtx->nStatus == XAPI_INVALID_ROLE) pTest->nInvalidRole++;
        return XAPI_CONTINUE;
    }

    if (pCtx->eCbType == XAPI_CB_STATUS)
    {
        pTest->nStatus++;
        if (pCtx->eStatType == XAPI_SELF)
        {
            if (pCtx->nStatus == XAPI_HUNGED) pTest->nHunged++;
            if (pCtx->nStatus == XAPI_CLOSED) pTest->nClosedStatus++;
        }
        if (XAPI_IsDestroyEvent(pCtx)) pTest->nDestroyed++;
        return XAPI_CONTINUE;
    }

    if (pCtx->eCbType == XAPI_CB_TIMEOUT)
    {
        pTest->nTimeouts++;
        return XAPI_DISCONNECT;
    }

    if (pCtx->eCbType == XAPI_CB_ACCEPTED)
    {
        /* An accepted peer watches nothing until it says otherwise, so this
         * is where the test picks which poll bits it wants to be told about. */
        if (pTest->bExtendOnAccept)
        {
            XAPI_AddTimer(pSession, 40);
            XAPI_AddTimer(pSession, 2000);
        }

        return XAPI_SetEvents(pSession, pTest->nPeerEvents);
    }

    if (pCtx->eCbType == XAPI_CB_READ)
    {
        pTest->nRead++;

        /* Asking for a user callback here proves the loop keeps calling
         * back until an answer other than the request comes out. */
        if (pTest->bAskUserCb)
        {
            pTest->bAskUserCb = XFALSE;
            return XAPI_USER_CB;
        }

        return XAPI_DISCONNECT;
    }

    return XAPI_CONTINUE;
}

/* Brings up an XAPI listener and a plain client socket connected to it, then
 * drives the loop until the peer has been accepted. Returns the client
 * descriptor, or -1 when the fixture could not be built. */
static int api_ev_connect(api_ev_t *pTest, int nPeerEvents, xbool_t bTimers)
{
    memset(pTest, 0, sizeof(*pTest));
    pTest->nPeerEvents = nPeerEvents;
    pTest->bExtendOnAccept = bTimers;

    if (XAPI_Init(&pTest->api, api_ev_callback, pTest) != XSTDOK) return -1;

    uint16_t nPort = api_ev_port();
    if (!nPort) { XAPI_Destroy(&pTest->api); return -1; }

    xapi_endpoint_t listener;
    XAPI_InitEndpoint(&listener);
    listener.eType = XAPI_SOCK;
    listener.eRole = XAPI_SERVER;
    listener.pAddr = "127.0.0.1";
    listener.nPort = nPort;

    if (XAPI_Listen(&pTest->api, &listener) != XSTDOK)
    {
        XAPI_Destroy(&pTest->api);
        return -1;
    }

    int nFD = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (nFD < 0) { XAPI_Destroy(&pTest->api); return -1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(nPort);

    if (connect(nFD, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        close(nFD);
        XAPI_Destroy(&pTest->api);
        return -1;
    }

    for (int i = 0; i < 100 && pTest->nAccepted == 0; i++)
    {
        XAPI_Service(&pTest->api, 20);
        if (XAPI_GetEventCount(&pTest->api) > 1) pTest->nAccepted = 1;
    }

    if (!pTest->nAccepted)
    {
        close(nFD);
        XAPI_Destroy(&pTest->api);
        return -1;
    }

    return nFD;
}

static int XTest_peer_closed(void)
{
    /* A peer that shuts down its writing half is reported as closed, not as
     * an error and not as a read of zero bytes. The loop has to be watching
     * for the half close to see it at all. */
    api_ev_t test;
    int nFD = api_ev_connect(&test, XPOLLIN | XPOLLRDHUP, XFALSE);
    if (nFD < 0) { printf("No loopback fixture, skipping\n"); return 77; }

    CHECK(test.nListening == 1, "The listener reported itself");
    CHECK(XAPI_GetEventCount(&test.api) == 2, "The listener and its peer are both registered");

    /* Half close: the peer's read side ends while the descriptor lives. */
    CHECK(shutdown(nFD, SHUT_WR) == 0, "The client shuts down its writing half");

    for (int i = 0; i < 100 && test.nClosedStatus == 0; i++) XAPI_Service(&test.api, 20);

    CHECK(test.nClosedStatus >= 1, "The half closed peer is reported as closed");
    CHECK(test.nRead == 0, "A closed peer is not reported as readable data");
    CHECK(test.nErrors == 0, "A closed peer is not an error");

    for (int i = 0; i < 20; i++) XAPI_Service(&test.api, 5);
    CHECK(XAPI_GetEventCount(&test.api) == 1, "The closed peer was dropped from the loop");

    close(nFD);
    XAPI_Destroy(&test.api);
    CHECK(test.nDestroyed >= 1, "The teardown reported itself");
    return 0;
}

static int XTest_peer_hunged(void)
{
    /* A descriptor the loop is only watching for writability still reports a
     * hangup, and that reaches the callback as its own status rather than
     * being mistaken for a write opportunity. */
    api_ev_t test;

    /* Watching only for out of band data: a hangup is reported whether or
     * not it was asked for, while readable bytes and a half close are only
     * reported when asked for. That is what leaves the hangup on its own. */
    int nFD = api_ev_connect(&test, XPOLLPRI, XFALSE);
    if (nFD < 0) { printf("No loopback fixture, skipping\n"); return 77; }

    /* A graceful close only ends one direction, and the peer would go on
     * living with its write half open. Zero linger turns the close into a
     * reset instead, which takes the whole connection down at once. */
    struct linger reset;
    reset.l_onoff = 1;
    reset.l_linger = 0;

    CHECK(setsockopt(nFD, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)) == 0,
        "The client is set to reset rather than close");

    close(nFD);

    for (int i = 0; i < 100 && test.nHunged == 0; i++) XAPI_Service(&test.api, 20);

    CHECK(test.nHunged >= 1, "The hung up peer is reported as hunged");
    CHECK(test.nRead == 0, "A hung up peer delivers no data");

    for (int i = 0; i < 20; i++) XAPI_Service(&test.api, 5);
    CHECK(XAPI_GetEventCount(&test.api) == 1, "The hung up peer was dropped from the loop");

    XAPI_Destroy(&test.api);
    return 0;
}

static int XTest_user_callback(void)
{
    /* A callback can hand control back to itself under a different reason.
     * The loop keeps asking until the answer is something else, so an
     * answer that always asked again would spin forever. */
    api_ev_t test;
    int nFD = api_ev_connect(&test, XPOLLIN | XPOLLRDHUP, XFALSE);
    if (nFD < 0) { printf("No loopback fixture, skipping\n"); return 77; }

    test.bAskUserCb = XTRUE;
    CHECK(write(nFD, "ping", 4) == 4, "The client sends something to read");

    for (int i = 0; i < 100 && test.nUserCbs == 0; i++) XAPI_Service(&test.api, 20);

    CHECK(test.nRead >= 1, "The data reached the callback");
    CHECK(test.nUserCbs == 1, "The requested user callback was delivered exactly once");

    close(nFD);
    XAPI_Destroy(&test.api);
    return 0;
}

static volatile sig_atomic_t g_nApiAlarms = 0;
static void api_ev_alarm(int nSignal) { (void)nSignal; g_nApiAlarms++; }

static int XTest_interrupted_service(void)
{
    /* A signal delivered while the loop is waiting is not a failure: it
     * comes back as an interrupt callback, with no session attached, and
     * the loop carries on when the callback says so. */
    api_ev_t test;
    int nFD = api_ev_connect(&test, XPOLLIN | XPOLLRDHUP, XFALSE);
    if (nFD < 0) { printf("No loopback fixture, skipping\n"); return 77; }

    struct sigaction action, previous;
    memset(&action, 0, sizeof(action));
    action.sa_handler = api_ev_alarm;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGALRM, &action, &previous) != 0)
    {
        close(nFD);
        XAPI_Destroy(&test.api);
        printf("SIGALRM cannot be installed, skipping\n");
        return 77;
    }

    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_usec = 40000;

    g_nApiAlarms = 0;
    CHECK(setitimer(ITIMER_REAL, &timer, NULL) == 0, "Arm the alarm");

    xevent_status_t nStatus = XAPI_Service(&test.api, 3000);

    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_REAL, &timer, NULL);
    sigaction(SIGALRM, &previous, NULL);

    CHECK(g_nApiAlarms >= 1, "The alarm was delivered");
    CHECK(test.nInterrupts >= 1, "The interrupted wait reached the callback");
    CHECK(test.nInterruptSessions == 0, "An interrupt carries no session");
    CHECK(nStatus == XEVENTS_SUCCESS, "A continuing callback keeps the loop alive");

    /* The loop is still usable afterwards. */
    CHECK(write(nFD, "after", 5) == 5, "The client can still write");
    for (int i = 0; i < 100 && test.nRead == 0; i++) XAPI_Service(&test.api, 20);
    CHECK(test.nRead >= 1, "The loop still delivers data after an interrupt");

    close(nFD);
    XAPI_Destroy(&test.api);
    return 0;
}

static int XTest_tick(void)
{
    /* Every service call that reaches the backend ticks once, whether or not
     * anything happened, and the tick never carries a session. An API with no
     * backend at all has nothing to service and says so instead. */
    api_ev_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&test.api, api_ev_callback, &test) == XSTDOK, "The API initializes");

    /* No endpoint has been added, so there is no event backend yet. */
    CHECK(XAPI_Service(&test.api, 0) == XEVENTS_EINVALID, "An API with no backend cannot be serviced");
    CHECK(test.nTicks == 0, "It did not tick either");
    CHECK(XAPI_Service(NULL, 0) == XEVENTS_EINVALID, "A missing API cannot be serviced");

    uint16_t nPort = api_ev_port();
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

    if (XAPI_Listen(&test.api, &listener) != XSTDOK)
    {
        XAPI_Destroy(&test.api);
        printf("The listener could not be created, skipping\n");
        return 77;
    }

    /* Nothing is going to connect, so every one of these is an idle pass. */
    for (int i = 0; i < 5; i++)
        CHECK(XAPI_Service(&test.api, 5) == XEVENTS_SUCCESS, "An idle service call succeeds");

    CHECK(test.nTicks == 5, "Every idle service call ticked once");
    CHECK(test.nTickSessions == 0, "A tick carries no session");
    CHECK(test.nRead == 0, "An idle loop delivers no data");

    XAPI_Destroy(&test.api);
    return 0;
}

static int XTest_extend_timer(void)
{
    /* A second timer on the same session extends the one already armed
     * rather than adding another, so a session that keeps being active
     * never accumulates timers and never fires the short first one. */
    api_ev_t test;
    int nFD = api_ev_connect(&test, XPOLLIN | XPOLLRDHUP, XTRUE);
    if (nFD < 0) { printf("No loopback fixture, skipping\n"); return 77; }

    size_t nEvents = XAPI_GetEventCount(&test.api);
    CHECK(nEvents == 3, "The listener, the peer and one timer are registered");

    /* The first timeout was 40ms and the extension pushed it to 2s, so
     * nothing may fire in the next quarter second. */
    for (int i = 0; i < 12; i++) XAPI_Service(&test.api, 20);
    CHECK(test.nTimeouts == 0, "The extended timer did not fire at its first deadline");
    CHECK(XAPI_GetEventCount(&test.api) == 3, "Extending did not add a second timer");

    close(nFD);
    XAPI_Destroy(&test.api);
    return 0;
}

static int XTest_session_guards(void)
{
    /* Every session call has to answer on a session that is not there, and
     * on one that is not attached to a loop, because a callback can be
     * handed a null session for the loop wide events. */
    CHECK(XAPI_SetEvents(NULL, XPOLLIN) == XSTDINV, "Setting events on nothing is rejected");
    CHECK(XAPI_EnableEvent(NULL, XPOLLIN) == XSTDINV, "Enabling an event on nothing is rejected");
    CHECK(XAPI_DisableEvent(NULL, XPOLLIN) == XSTDINV, "Disabling an event on nothing is rejected");
    CHECK(XAPI_AddTimer(NULL, 10) == XSTDINV, "Arming a timer on nothing is rejected");
    CHECK(XAPI_ExtendTimer(NULL, 10) == XSTDINV, "Extending a timer on nothing is rejected");
    CHECK(XAPI_DeleteTimer(NULL) == XSTDINV, "Deleting a timer on nothing is rejected");
    CHECK(XAPI_Disconnect(NULL) == XSTDINV, "Disconnecting nothing is rejected");
    CHECK(XAPI_GetTxBuff(NULL) == NULL, "There is no send buffer on nothing");
    CHECK(XAPI_GetRxBuff(NULL) == NULL, "There is no receive buffer on nothing");
    CHECK(XAPI_PutTxBuff(NULL, NULL) == XSTDINV, "Queueing to nothing is rejected");
    CHECK(XAPI_ProcessBuffered(NULL) == XAPI_DISCONNECT, "Dispatching nothing disconnects");
    CHECK(XAPI_GetEventCount(NULL) == 0, "A missing API has no events");

    /* A bare session has a loop pointer of its own to answer for. */
    xapi_session_t session;
    memset(&session, 0, sizeof(session));

    CHECK(XAPI_ExtendTimer(&session, 10) == XSTDINV, "Extending without a loop is rejected");
    CHECK(XAPI_AddTimer(&session, 10) == XSTDINV, "Arming without a loop is rejected");
    CHECK(XAPI_Disconnect(&session) == XSTDINV, "Disconnecting without a loop is rejected");
    CHECK(XAPI_ProcessBuffered(&session) == XAPI_DISCONNECT, "Dispatching without a loop disconnects");

    /* A timer that has never been armed has nothing to extend or delete,
     * and a non positive timeout is a no-op rather than an immediate fire. */
    api_ev_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&test.api, api_ev_callback, &test) == XSTDOK, "The API initializes");

    session.pApi = &test.api;
    CHECK(XAPI_ExtendTimer(&session, 10) == XSTDINV, "There is no timer to extend yet");
    CHECK(XAPI_DeleteTimer(&session) == XSTDOK, "Deleting a timer that was never armed is allowed");
    CHECK(XAPI_AddTimer(&session, 0) == XSTDNON, "A zero timeout arms nothing");
    CHECK(XAPI_AddTimer(&session, -1) == XSTDNON, "A negative timeout arms nothing");
    CHECK(XAPI_Disconnect(&session) == XSTDOK, "A session with no registration is already gone");

    XAPI_Destroy(&test.api);

    /* And destroying twice, or destroying nothing, must be safe. */
    XAPI_Destroy(&test.api);
    XAPI_Destroy(NULL);
    return 0;
}

static int XTest_rx_size(void)
{
    /* The read size bounds how much a single packet may buffer before the
     * loop gives up on it, so it has to be settable and guarded. */
    api_ev_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&test.api, api_ev_callback, &test) == XSTDOK, "The API initializes");

    size_t nDefault = test.api.nRxSize;
    CHECK(nDefault > 0, "A fresh API starts with a read limit");

    CHECK(XAPI_SetRxSize(&test.api, 4096) == XSTDOK, "A read size can be set");
    CHECK(test.api.nRxSize == 4096, "The read size is what was asked for");

    /* Zero means "whatever the default is", not "no limit": a loop with no
     * limit would buffer a partial packet until it ran out of memory. */
    CHECK(XAPI_SetRxSize(&test.api, 0) == XSTDOK, "A zero read size is accepted");
    CHECK(test.api.nRxSize == nDefault, "A zero read size restores the default");
    CHECK(XAPI_SetRxSize(NULL, 4096) == XSTDINV, "Setting it on nothing is rejected");

    XAPI_Destroy(&test.api);
    return 0;
}


/* Brings up an XAPI listener of the given protocol and a plain client
 * socket connected to it. Returns the client descriptor, or -1. */
static int api_ev_proto(api_ev_t *pTest, xapi_type_t eType, int nPeerEvents)
{
    memset(pTest, 0, sizeof(pTest[0]));
    pTest->nPeerEvents = nPeerEvents;

    if (XAPI_Init(&pTest->api, api_ev_callback, pTest) != XSTDOK) return -1;

    uint16_t nPort = api_ev_port();
    if (!nPort) { XAPI_Destroy(&pTest->api); return -1; }

    xapi_endpoint_t listener;
    XAPI_InitEndpoint(&listener);
    listener.eType = eType;
    listener.eRole = XAPI_SERVER;
    listener.pAddr = "127.0.0.1";
    listener.nPort = nPort;

    if (XAPI_Listen(&pTest->api, &listener) != XSTDOK)
    {
        XAPI_Destroy(&pTest->api);
        return -1;
    }

    int nFD = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (nFD < 0) { XAPI_Destroy(&pTest->api); return -1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(nPort);

    if (connect(nFD, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        close(nFD);
        XAPI_Destroy(&pTest->api);
        return -1;
    }

    for (int i = 0; i < 100 && XAPI_GetEventCount(&pTest->api) < 2; i++)
        XAPI_Service(&pTest->api, 20);

    if (XAPI_GetEventCount(&pTest->api) < 2)
    {
        close(nFD);
        XAPI_Destroy(&pTest->api);
        return -1;
    }

    pTest->nAccepted = 1;
    return nFD;
}

static int XTest_http_garbage(void)
{
    /* An HTTP endpoint handed something that is not a request has to drop
     * the peer with an error rather than sit on the bytes waiting for a
     * terminator that is never coming. */
    api_ev_t test;
    int nFD = api_ev_proto(&test, XAPI_HTTP, XPOLLIN | XPOLLRDHUP);
    if (nFD < 0) { printf("No loopback fixture, skipping\n"); return 77; }

    /* A request line with a version nobody defines, terminated properly so
     * the parser has a whole message to reject. */
    const char *pJunk = "\x01\x02 \xff\xfe HTTP/9.9\r\nHost: x\r\n\r\n";
    CHECK(write(nFD, pJunk, strlen(pJunk)) > 0, "The junk reaches the server");

    for (int i = 0; i < 100 && test.nErrors == 0 && XAPI_GetEventCount(&test.api) > 1; i++)
        XAPI_Service(&test.api, 20);

    CHECK(XAPI_GetEventCount(&test.api) == 1, "The peer was dropped");
    CHECK(test.nRead == 0, "Nothing was delivered as a parsed request");

    close(nFD);
    XAPI_Destroy(&test.api);
    return 0;
}

static int XTest_http_oversized(void)
{
    /* A request that never terminates must not be buffered without bound.
     * The read limit is what stops a peer from making the server allocate
     * until it dies, so the peer is dropped once the limit is passed. */
    api_ev_t test;
    int nFD = api_ev_proto(&test, XAPI_HTTP, XPOLLIN | XPOLLRDHUP);
    if (nFD < 0) { printf("No loopback fixture, skipping\n"); return 77; }

    CHECK(XAPI_SetRxSize(&test.api, 4096) == XSTDOK, "The read limit is lowered");

    /* A header line that keeps going and never terminates the message. */
    char sChunk[2048];
    memset(sChunk, 'H', sizeof(sChunk));

    const char *pStart = "GET / HTTP/1.1\r\nX-Long: ";
    CHECK(write(nFD, pStart, strlen(pStart)) > 0, "The request line reaches the server");

    for (int i = 0; i < 64 && XAPI_GetEventCount(&test.api) > 1; i++)
    {
        if (write(nFD, sChunk, sizeof(sChunk)) <= 0) break;
        XAPI_Service(&test.api, 5);
    }

    for (int i = 0; i < 40 && XAPI_GetEventCount(&test.api) > 1; i++) XAPI_Service(&test.api, 10);

    CHECK(XAPI_GetEventCount(&test.api) == 1, "The peer was dropped rather than buffered further");
    CHECK(test.nErrors >= 1, "The drop was reported as an error");
    CHECK(test.nRead == 0, "Nothing was delivered as a parsed request");

    close(nFD);
    XAPI_Destroy(&test.api);
    return 0;
}

static int XTest_mdtp_garbage(void)
{
    /* The same for MDTP: a header length that is not followed by JSON is a
     * protocol error, and the peer goes rather than the loop retrying. */
    api_ev_t test;
    int nFD = api_ev_proto(&test, XAPI_MDTP, XPOLLIN | XPOLLRDHUP);
    if (nFD < 0) { printf("No loopback fixture, skipping\n"); return 77; }

    /* Four bytes of length saying twelve, then twelve bytes that are not
     * a JSON object at all. */
    const char sJunk[] = "\x0c\x00\x00\x00" "############";
    CHECK(write(nFD, sJunk, sizeof(sJunk) - 1) > 0, "The junk reaches the server");

    for (int i = 0; i < 100 && XAPI_GetEventCount(&test.api) > 1; i++) XAPI_Service(&test.api, 20);

    CHECK(XAPI_GetEventCount(&test.api) == 1, "The peer was dropped");
    CHECK(test.nErrors >= 1, "The drop was reported as an error");
    CHECK(test.nRead == 0, "Nothing was delivered as a parsed packet");

    close(nFD);
    XAPI_Destroy(&test.api);
    return 0;
}

static int XTest_invalid_role(void)
{
    /* A registration whose role the loop does not know how to drive is
     * dropped the first time it becomes readable, with the role named.
     * Left in, it would be dispatched as whatever the role happened to
     * collide with. */
    api_ev_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&test.api, api_ev_callback, &test) == XSTDOK, "The API initializes");

    int nPipe[2];
    if (pipe(nPipe) != 0)
    {
        XAPI_Destroy(&test.api);
        printf("No pipe available, skipping\n");
        return 77;
    }

    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = (xapi_role_t)200;   /* Not one of the roles the loop drives */
    endpoint.nFD = nPipe[0];
    endpoint.nEvents = XPOLLIN;

    CHECK(XAPI_AddEvent(&test.api, &endpoint) == XSTDOK, "The descriptor is registered");
    CHECK(test.nRegistered == 1, "The registration was reported");
    CHECK(XAPI_IsSupportedRole((xapi_role_t)200) == XFALSE, "The role is not one the loop drives");

    CHECK(write(nPipe[1], "x", 1) == 1, "Something arrives on it");

    for (int i = 0; i < 100 && test.nInvalidRole == 0; i++) XAPI_Service(&test.api, 20);

    CHECK(test.nInvalidRole >= 1, "The unsupported role was reported");
    CHECK(test.nRead == 0, "Nothing was dispatched to the callback as data");

    for (int i = 0; i < 20; i++) XAPI_Service(&test.api, 5);
    CHECK(XAPI_GetEventCount(&test.api) == 0, "The session was dropped from the loop");

    close(nPipe[1]);
    XAPI_Destroy(&test.api);
    return 0;
}

static int XTest_inactive_role(void)
{
    /* The inactive role is the one unsupported value that is dropped
     * without an error: it is what a zeroed endpoint carries, so reporting
     * it would turn every uninitialized registration into a log line rather
     * than the quiet no-op the loop treats it as. It is still dropped. */
    api_ev_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&test.api, api_ev_callback, &test) == XSTDOK, "The API initializes");

    int nPipe[2];
    if (pipe(nPipe) != 0)
    {
        XAPI_Destroy(&test.api);
        printf("No pipe available, skipping\n");
        return 77;
    }

    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_INACTIVE;
    endpoint.nFD = nPipe[0];
    endpoint.nEvents = XPOLLIN;

    CHECK(XAPI_IsSupportedRole(XAPI_INACTIVE) == XFALSE, "The inactive role is not supported");
    CHECK(XAPI_AddEvent(&test.api, &endpoint) == XSTDOK, "The descriptor is registered");
    CHECK(write(nPipe[1], "x", 1) == 1, "Something arrives on it");

    for (int i = 0; i < 100 && XAPI_GetEventCount(&test.api) > 0; i++) XAPI_Service(&test.api, 20);

    CHECK(XAPI_GetEventCount(&test.api) == 0, "The session was dropped from the loop");
    CHECK(test.nInvalidRole == 0, "The inactive role was dropped without an error");
    CHECK(test.nRead == 0, "Nothing was dispatched to the callback as data");

    close(nPipe[1]);
    XAPI_Destroy(&test.api);
    return 0;
}

static int XTest_endpoint_guards(void)
{
    /* Attaching an existing descriptor is the escape hatch for anything the
     * endpoint types do not cover, so its own arguments have to be checked:
     * it takes ownership of the descriptor on some paths. */
    api_ev_t test;
    memset(&test, 0, sizeof(test));
    CHECK(XAPI_Init(&test.api, api_ev_callback, &test) == XSTDOK, "The API initializes");

    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);

    CHECK(endpoint.nFD == XSOCK_INVALID, "A fresh endpoint has no descriptor");
    CHECK(endpoint.eRole == XAPI_INACTIVE, "A fresh endpoint has no role");
    CHECK(endpoint.eType == XAPI_NONE, "A fresh endpoint has no type");

    CHECK(XAPI_AddEvent(&test.api, &endpoint) == XSTDINV, "An endpoint with no descriptor is rejected");
    CHECK(XAPI_AddEvent(&test.api, NULL) == XSTDINV, "A missing endpoint is rejected");
    CHECK(XAPI_AddEvent(NULL, &endpoint) == XSTDINV, "A missing API is rejected");

    int nPipe[2];
    if (pipe(nPipe) != 0)
    {
        XAPI_Destroy(&test.api);
        printf("No pipe available, skipping\n");
        return 77;
    }

    /* The loop's own type is not something a descriptor can be registered
     * under, since it names the loop rather than a protocol. */
    endpoint.nFD = nPipe[0];
    endpoint.eType = XAPI_SELF;
    endpoint.eRole = XAPI_CUSTOM;
    CHECK(XAPI_AddEvent(&test.api, &endpoint) == XSTDINV, "The loop's own type is rejected");

    endpoint.eType = XAPI_SOCK;
    CHECK(XAPI_AddEvent(&test.api, &endpoint) == XSTDOK, "A custom descriptor is registered");
    CHECK(XAPI_GetEventCount(&test.api) == 1, "It is in the loop");

    close(nPipe[1]);
    XAPI_Destroy(&test.api);
    return 0;
}


static int XTest_ws_garbage_request(void)
{
    /* A WebSocket listener is an HTTP server until the upgrade is done, so
     * what reaches it first is attacker shaped: a request that is not an
     * upgrade, or not HTTP at all, has to end the peer rather than leave a
     * half-upgraded session in the loop. */
    const char *pProbes[] = {
        "not http at all\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: x\r\n\r\n",                      /* no upgrade */
        "GET / HTTP/1.1\r\nUpgrade: websocket\r\nHost: x\r\n\r\n" /* no key */
    };

    for (size_t i = 0; i < sizeof(pProbes) / sizeof(*pProbes); i++)
    {
        api_ev_t test;
        int nFD = api_ev_proto(&test, XAPI_WS, XPOLLIN | XPOLLRDHUP);
        if (nFD < 0) { printf("No loopback fixture, skipping\n"); return 77; }

        CHECK(write(nFD, pProbes[i], strlen(pProbes[i])) > 0, "The probe reaches the server");

        for (int n = 0; n < 100 && XAPI_GetEventCount(&test.api) > 1; n++)
            XAPI_Service(&test.api, 20);

        CHECK(XAPI_GetEventCount(&test.api) == 1, "The peer was dropped");
        CHECK(test.nRead == 0, "Nothing was delivered as a frame");

        close(nFD);
        XAPI_Destroy(&test.api);
    }

    return 0;
}

static int XTest_ws_oversized_request(void)
{
    /* The upgrade request is read before anything is known about the peer,
     * so it is bounded by the same read limit as any other message. */
    api_ev_t test;
    int nFD = api_ev_proto(&test, XAPI_WS, XPOLLIN | XPOLLRDHUP);
    if (nFD < 0) { printf("No loopback fixture, skipping\n"); return 77; }

    CHECK(XAPI_SetRxSize(&test.api, 4096) == XSTDOK, "The read limit is lowered");

    char sChunk[2048];
    memset(sChunk, 'W', sizeof(sChunk));

    const char *pStart = "GET / HTTP/1.1\r\nX-Long: ";
    CHECK(write(nFD, pStart, strlen(pStart)) > 0, "The request line reaches the server");

    for (int i = 0; i < 64 && XAPI_GetEventCount(&test.api) > 1; i++)
    {
        if (write(nFD, sChunk, sizeof(sChunk)) <= 0) break;
        XAPI_Service(&test.api, 5);
    }

    for (int i = 0; i < 40 && XAPI_GetEventCount(&test.api) > 1; i++) XAPI_Service(&test.api, 10);

    CHECK(XAPI_GetEventCount(&test.api) == 1, "The peer was dropped rather than buffered further");
    CHECK(test.nErrors >= 1, "The drop was reported as an error");

    close(nFD);
    XAPI_Destroy(&test.api);
    return 0;
}

/* A listening socket that answers whatever it is told to, so a client in
 * the loop can be pointed at a server that does not play along. */
typedef struct {
    uint16_t nPort;
    const char *pAnswer;
    xatomic_t nReady;
    xatomic_t nServed;
} api_ev_rogue_t;

static void *api_ev_rogue(void *pContext)
{
    api_ev_rogue_t *pServer = (api_ev_rogue_t*)pContext;
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
        listen(nListen, 4) < 0)
    {
        close(nListen);
        XSYNC_ATOMIC_SET(&pServer->nReady, 2);
        return NULL;
    }

    pServer->nPort = ntohs(addr.sin_port);
    XSYNC_ATOMIC_SET(&pServer->nReady, 1);

    struct timeval timeout = {10, 0};
    setsockopt(nListen, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    int nPeer = (int)accept(nListen, NULL, NULL);
    if (nPeer >= 0)
    {
        setsockopt(nPeer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        char sRequest[4096];
        recv(nPeer, sRequest, sizeof(sRequest), 0);

        if (pServer->pAnswer != NULL)
            send(nPeer, pServer->pAnswer, strlen(pServer->pAnswer), MSG_NOSIGNAL);

        XSYNC_ATOMIC_ADD(&pServer->nServed, 1);
        xusleep(50000);
        close(nPeer);
    }

    close(nListen);
    return NULL;
}

static int XTest_ws_bad_response(void)
{
    /* The other direction: a WebSocket client in the loop, pointed at a
     * server that answers the upgrade with something that is not one. The
     * client has to give up rather than start framing on a plain socket. */
    const char *pAnswers[] = {
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n",           /* not an upgrade */
        "HTTP/1.1 101 Switching Protocols\r\n\r\n",                /* no accept key */
        "this is not a response at all\r\n\r\n"
    };

    for (size_t i = 0; i < sizeof(pAnswers) / sizeof(*pAnswers); i++)
    {
        api_ev_rogue_t server;
        memset(&server, 0, sizeof(server));
        server.pAnswer = pAnswers[i];

        xthread_t thread;
        if (XThread_Create(&thread, api_ev_rogue, &server, XFALSE) != XSTDOK)
        {
            printf("No server thread, skipping\n");
            return 77;
        }

        for (int n = 0; n < 2000 && !XSYNC_ATOMIC_GET(&server.nReady); n++) xusleep(5000);
        if (XSYNC_ATOMIC_GET(&server.nReady) != 1)
        {
            XThread_Join(&thread);
            printf("No rogue listener, skipping\n");
            return 77;
        }

        api_ev_t test;
        memset(&test, 0, sizeof(test));
        CHECK(XAPI_Init(&test.api, api_ev_callback, &test) == XSTDOK, "The API initializes");

        xapi_endpoint_t client;
        XAPI_InitEndpoint(&client);
        client.eType = XAPI_WS;
        client.eRole = XAPI_CLIENT;
        client.pAddr = "127.0.0.1";
        client.nPort = server.nPort;
        client.pUri = "/socket";

        if (XAPI_Connect(&test.api, &client) == XSTDOK)
        {
            for (int n = 0; n < 200 && XAPI_GetEventCount(&test.api) > 0; n++)
                XAPI_Service(&test.api, 10);

            CHECK(XAPI_GetEventCount(&test.api) == 0, "The client gave up on the session");
            CHECK(test.nRead == 0, "Nothing was delivered as a frame");
        }

        XAPI_Destroy(&test.api);
        XThread_Join(&thread);
    }

    return 0;
}

XTEST_MAIN(
    XTEST_CASE(peer_closed),
    XTEST_CASE(peer_hunged),
    XTEST_CASE(user_callback),
    XTEST_CASE(interrupted_service),
    XTEST_CASE(tick),
    XTEST_CASE(extend_timer),
    XTEST_CASE(session_guards),
    XTEST_CASE(rx_size),
    XTEST_CASE(http_garbage),
    XTEST_CASE(http_oversized),
    XTEST_CASE(mdtp_garbage),
    XTEST_CASE(invalid_role),
    XTEST_CASE(inactive_role),
    XTEST_CASE(endpoint_guards),
    XTEST_CASE(ws_garbage_request),
    XTEST_CASE(ws_oversized_request),
    XTEST_CASE(ws_bad_response)
)
