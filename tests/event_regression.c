/* libxutils: callback mutation, descriptor ownership, timer and wakeup regressions. */
#include "test.h"
#include "event.h"
#include "xtime.h"
#include "sock.h"
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

typedef struct xtest_events_ {
    xevent_data_t *pVictim;
    int nReads;
    int nWrites;
    int nClears;
    int nTimers;
    int nUsers;
    xbool_t bRemove;
    xbool_t bFailed;
} xtest_events_t;

static int XTest_EventCallback(void *pLoop, void *pData, XSOCKET nFD, xevent_cb_type_t eReason)
{
    xevents_t *pEvents = (xevents_t*)pLoop;
    xtest_events_t *pTest = (xtest_events_t*)pEvents->pUserSpace;
    xevent_data_t *pEvent = (xevent_data_t*)pData;
    (void)nFD;
    if (eReason == XEVENT_CB_CLEAR) pTest->nClears++;
    if (eReason == XEVENT_CB_WRITE) pTest->nWrites++;
    if (eReason == XEVENT_CB_TIMEOUT) pTest->nTimers++;
    if (eReason == XEVENT_CB_READ)
    {
        if (pEvent->nType == XEVENT_TYPE_EVENT)
        {
            uint64_t nValue = 0;
            if (XEvent_ReadU64(pEvent, &nValue) != sizeof(nValue) || nValue != 3) pTest->bFailed = XTRUE;
            pTest->nUsers++;
        }
        else
        {
            char cByte = 0;
            if (XEvent_ReadByte(pEvent, &cByte) != 1 || cByte != 'x') pTest->bFailed = XTRUE;
            pTest->nReads++;
        }
        if (pTest->pVictim == pEvent) pTest->pVictim = NULL;
        if (pTest->pVictim != NULL)
        {
            xevent_data_t *pVictim = pTest->pVictim;
            pTest->pVictim = NULL;
            if (XEvents_Delete(pEvents, pVictim) != XEVENTS_SUCCESS) pTest->bFailed = XTRUE;
        }
        if (pTest->bRemove) return XEVENTS_DISCONNECT;
    }
    return XEVENTS_CONTINUE;
}

static int XTest_lifecycle(void)
{
    for (int nHash = 0; nHash < 2; nHash++)
    {
        xtest_events_t test = {0};
        xevents_t loop;
        CHECK(XEvents_Create(&loop, 16, &test, XTest_EventCallback, nHash) == XEVENTS_SUCCESS, "Create either backend map");
        XSOCKET pair[2];
        CHECK(XSock_CreatePair(pair) == XSTDOK, "Create isolated stream pair");
        xevent_data_t *pEvent = XEvents_RegisterEvent(&loop, NULL, pair[0], XPOLLIN, XEVENT_TYPE_CUSTOM);
        CHECK(pEvent != NULL && loop.nEventCount == 1, "Registration increments the event count exactly once");
        CHECK(XEvents_Service(&loop, 0) == XEVENTS_SUCCESS && test.nReads == 0, "Idle service must not fabricate reads");
        CHECK(send(pair[1], "x", 1, 0) == 1, "Make the descriptor readable");
        CHECK(XEvents_Service(&loop, 100) == XEVENTS_SUCCESS, "Service the ready descriptor");
        CHECK(test.nReads == 1 && !test.bFailed, "Read exactly one notification byte");
        CHECK(XEvents_Service(&loop, 0) == XEVENTS_SUCCESS && test.nReads == 1, "Drained descriptors must stay quiet");
        CHECK(XEvents_Delete(&loop, pEvent) == XEVENTS_SUCCESS && loop.nEventCount == 0, "Deletion updates the loop");
        CHECK(test.nClears == 1, "Deleting an event invokes its clear callback exactly once");
        CHECK(send(pair[1], "x", 1, 0) == 1, "Deleting a custom event must not close its caller-owned descriptor");
        XEvents_Destroy(&loop);
        CHECK(test.nClears == 1, "Destruction must not clear an already deleted event again");
        xclosesock(pair[0]);
        xclosesock(pair[1]);
    }
    return 0;
}

static int XTest_modify(void)
{
    xtest_events_t test = {0};
    xevents_t loop;
    CHECK(XEvents_Create(&loop, 16, &test, XTest_EventCallback, XTRUE) == XEVENTS_SUCCESS, "Create event loop");
    XSOCKET pair[2];
    CHECK(XSock_CreatePair(pair) == XSTDOK, "Create event pair");
    xevent_data_t *pEvent = XEvents_RegisterEvent(&loop, NULL, pair[0], XPOLLOUT, XEVENT_TYPE_CUSTOM);
    CHECK(pEvent != NULL, "Register writable descriptor");
    CHECK(XEvents_Service(&loop, 100) == XEVENTS_SUCCESS && test.nWrites > 0, "Writable interest must dispatch writes");
    int nWrites = test.nWrites;
    CHECK(XEvents_Modify(&loop, pEvent, XPOLLIN) == XEVENTS_SUCCESS, "Replace write interest with read interest");
    CHECK(XEvents_Service(&loop, 0) == XEVENTS_SUCCESS && test.nWrites == nWrites, "Disabled writes must stop firing");
    CHECK(XEvents_RegisterEvent(&loop, NULL, pair[0], XPOLLIN, XEVENT_TYPE_CUSTOM) == NULL, "Duplicate fd must be refused");
    CHECK(loop.nEventCount == 1 && XEvents_GetData(&loop, pair[0]) == pEvent, "Duplicate rejection preserves the original");
    XEvents_Destroy(&loop);
    CHECK(test.nClears == 1, "Destroy closes one registration, including after duplicate rejection");
    xclosesock(pair[0]);
    xclosesock(pair[1]);
    return 0;
}

static int XTest_callback_delete(void)
{
    xtest_events_t test = {0};
    test.bRemove = XTRUE;
    xevents_t loop;
    CHECK(XEvents_Create(&loop, 16, &test, XTest_EventCallback, XTRUE) == XEVENTS_SUCCESS, "Create deletion test loop");
    XSOCKET pairs[2][2];
    xevent_data_t *pEvents[2];
    for (size_t i = 0; i < 2; i++)
    {
        CHECK(XSock_CreatePair(pairs[i]) == XSTDOK, "Create a ready-event pair");
        pEvents[i] = XEvents_RegisterEvent(&loop, NULL, pairs[i][0], XPOLLIN, XEVENT_TYPE_CUSTOM);
        CHECK(pEvents[i] != NULL && send(pairs[i][1], "x", 1, 0) == 1, "Queue two ready events in the same service batch");
    }
    test.pVictim = pEvents[1];
    for (int i = 0; i < 4 && loop.nEventCount > 0; i++)
        CHECK(XEvents_Service(&loop, 50) == XEVENTS_SUCCESS, "Callback deletion must not leave stale batch pointers");
    CHECK(loop.nEventCount == 0 && test.nClears == 2 && !test.bFailed, "Both events must be removed exactly once");
    XEvents_Destroy(&loop);
    for (size_t i = 0; i < 2; i++)
    {
        xclosesock(pairs[i][0]);
        xclosesock(pairs[i][1]);
    }
    return 0;
}

static int XTest_timers(void)
{
    xtest_events_t test = {0};
    xevents_t loop;
    CHECK(XEvents_Create(&loop, 16, &test, XTest_EventCallback, XTRUE) == XEVENTS_SUCCESS, "Create timer loop");
    xevent_data_t *pTimer = XEvents_AddTimer(&loop, NULL, 10);
    CHECK(pTimer != NULL, "Create one-shot timer");
    for (int nExpected = 1; nExpected <= 2; nExpected++)
    {
        uint64_t nUntil = XTime_GetMs() + 3000;
        while (test.nTimers < nExpected && XTime_GetMs() < nUntil)
            CHECK(XEvents_Service(&loop, 50) == XEVENTS_SUCCESS, "Service one-shot timer");
        CHECK(test.nTimers == nExpected, "Each arm must fire exactly once");
        CHECK(XEvents_Service(&loop, 0) == XEVENTS_SUCCESS && test.nTimers == nExpected, "Expired timer must not spin");
        if (nExpected == 1) CHECK(XEvents_ExtendTimer(&loop, pTimer, 10) == XEVENTS_SUCCESS, "Rearm the existing timer");
    }
    CHECK(XEvents_Delete(&loop, pTimer) == XEVENTS_SUCCESS, "Delete expired timer");
    XEvents_Destroy(&loop);
    CHECK(test.nClears == 1, "Timer deletion releases its internal resources once");
    return 0;
}

typedef struct {
    int aFired[8];
    int nFired;
} xtest_timer_order_t;

static int XTest_OrderCallback(void *pLoop, void *pData, XSOCKET nFD, xevent_cb_type_t eReason)
{
    xevents_t *pEvents = (xevents_t*)pLoop;
    xtest_timer_order_t *pOrder = (xtest_timer_order_t*)pEvents->pUserSpace;
    xevent_data_t *pEvent = (xevent_data_t*)pData;
    (void)nFD;

    if (eReason == XEVENT_CB_TIMEOUT && pEvent != NULL && pOrder->nFired < 8)
        pOrder->aFired[pOrder->nFired++] = *(int*)pEvent->pContext;

    return XEVENTS_CONTINUE;
}

static int XTest_timer_order(void)
{
    /* Timers fire in deadline order, not in the order they were added. The
       portable timer list used on macOS and Windows appended every new timer
       at its tail and only ever looked at the head, so a short timer added
       after a long one waited for the long one. A timer that fired and was not
       rearmed then went back to the head and held up all the others. */
    xtest_timer_order_t order = {0};
    xevents_t loop;
    CHECK(XEvents_Create(&loop, 16, &order, XTest_OrderCallback, XTRUE) == XEVENTS_SUCCESS, "Create an ordering loop");

    static int ids[] = { 1, 2, 3, 4, 5 };
    xevent_data_t *pLong = XEvents_AddTimer(&loop, &ids[0], 400);
    xevent_data_t *pShort = XEvents_AddTimer(&loop, &ids[1], 100);
    xevent_data_t *pMiddle = XEvents_AddTimer(&loop, &ids[2], 250);
    xevent_data_t *pFirst = XEvents_AddTimer(&loop, &ids[3], 20);
    CHECK(pLong && pShort && pMiddle && pFirst, "Add timers out of deadline order");

    uint64_t nUntil = XTime_GetMs() + 5000;
    while (order.nFired < 4 && XTime_GetMs() < nUntil)
        CHECK(XEvents_Service(&loop, 10) == XEVENTS_SUCCESS, "Service the ordering loop");

    CHECK(order.nFired == 4, "Every timer fires, including those behind a spent one");
    CHECK(order.aFired[0] == 4 && order.aFired[1] == 2 && order.aFired[2] == 3 && order.aFired[3] == 1,
        "Timers fire in deadline order");

    /* An indefinite wait still ends when the next timer is due. */
    xevent_data_t *pWake = XEvents_AddTimer(&loop, &ids[4], 50);
    CHECK(pWake != NULL, "Add a timer to end an indefinite wait");
    nUntil = XTime_GetMs() + 5000;
    while (order.nFired < 5 && XTime_GetMs() < nUntil)
        CHECK(XEvents_Service(&loop, -1) == XEVENTS_SUCCESS, "An indefinite wait returns for a due timer");
    CHECK(order.nFired == 5 && order.aFired[4] == 5, "The timer ends the indefinite wait");

    CHECK(XEvents_Delete(&loop, pLong) == XEVENTS_SUCCESS, "Delete a spent timer");
    CHECK(XEvents_Delete(&loop, pShort) == XEVENTS_SUCCESS, "Delete a spent timer");
    CHECK(XEvents_Delete(&loop, pMiddle) == XEVENTS_SUCCESS, "Delete a spent timer");
    CHECK(XEvents_Delete(&loop, pFirst) == XEVENTS_SUCCESS, "Delete a spent timer");
    CHECK(XEvents_Delete(&loop, pWake) == XEVENTS_SUCCESS, "Delete a spent timer");
    XEvents_Destroy(&loop);
    return 0;
}

static int XTest_wakeup(void)
{
#if defined(__linux__)
    xtest_events_t test = {0};
    xevents_t loop;
    CHECK(XEvents_Create(&loop, 16, &test, XTest_EventCallback, XTRUE) == XEVENTS_SUCCESS, "Create wakeup loop");
    xevent_data_t *pEvent = XEvents_CreateEvent(&loop, NULL);
    CHECK(pEvent != NULL, "Create eventfd wakeup");
    uint64_t nValue = 1;
    CHECK(write(pEvent->nFD, &nValue, sizeof(nValue)) == sizeof(nValue), "Queue first wakeup");
    nValue = 2;
    CHECK(write(pEvent->nFD, &nValue, sizeof(nValue)) == sizeof(nValue), "Queue second wakeup");
    CHECK(XEvents_Service(&loop, 100) == XEVENTS_SUCCESS, "Service coalesced wakeups");
    CHECK(test.nUsers == 1 && !test.bFailed, "Eventfd must deliver the accumulated counter");
    CHECK(XEvents_Service(&loop, 0) == XEVENTS_SUCCESS && test.nUsers == 1, "Consumed wakeup must not spin");
    XEvents_Destroy(&loop);
    CHECK(test.nClears == 1, "Destroy releases the internally owned eventfd");
    return 0;
#else
    return 77;
#endif
}


static int XTest_status_strings(void)
{
    /* Every status the loop can report has a description, so a caller
     * logging one never prints a bare number. */
    const xevent_status_t states[] = {
        XEVENTS_NONE, XEVENTS_ECTL, XEVENTS_EMAX, XEVENTS_ENOCB, XEVENTS_EOMAX,
        XEVENTS_EWAIT, XEVENTS_EINTR, XEVENTS_EALLOC, XEVENTS_ETIMER,
        XEVENTS_EEXTEND, XEVENTS_EBREAK, XEVENTS_ECREATE, XEVENTS_EINSERT,
        XEVENTS_EINVALID, XEVENTS_SUCCESS
    };

    for (size_t i = 0; i < sizeof(states) / sizeof(*states); i++)
    {
        const char *pText = XEvents_GetStatusStr(states[i]);
        CHECK(pText != NULL && *pText != '\0', "Every status has a description");

        /* Two different statuses reading the same would make a log line
         * useless for telling which one happened. The undefined status is
         * the one exception, since it is what the fallback also returns. */
        if (states[i] == XEVENTS_NONE) continue;

        for (size_t n = 0; n < i; n++)
        {
            if (states[n] == XEVENTS_NONE) continue;
            CHECK(strcmp(pText, XEvents_GetStatusStr(states[n])) != 0,
                "No two statuses share a description");
        }
    }

    CHECK(XEvents_GetStatusStr((xevent_status_t)999) != NULL, "An out of range status still has a description");
    CHECK(strcmp(XEvents_GetStatusStr((xevent_status_t)999),
        XEvents_GetStatusStr(XEVENTS_NONE)) == 0, "An unknown status reads the same as none");
    return 0;
}

static int XTest_capacity(void)
{
    /* The loop is created with a descriptor limit and must refuse to go
     * past it rather than growing its array out from under a service call. */
    xtest_events_t test;
    memset(&test, 0, sizeof(test));

    /* The hash map is what lets the loop find its registrations again at
     * teardown, so it is on here: without it every registration has to be
     * deleted by the caller before the loop is destroyed. */
    xevents_t events;
    CHECK(XEvents_Create(&events, 4, &test, XTest_EventCallback, XTRUE) == XEVENTS_SUCCESS,
        "The loop is created with a limit");
    CHECK(events.nEventMax == 4, "The loop records its limit");
    CHECK(events.nEventCount == 0, "A fresh loop watches nothing");

    XSOCKET pairs[8][2];
    size_t nRegistered = 0;

    for (size_t i = 0; i < 4; i++)
    {
        CHECK(XSock_CreatePair(pairs[i]) == XSTDOK, "A socket pair is created");
        xevent_data_t *pData = XEvents_RegisterEvent(&events, NULL, pairs[i][0], XPOLLIN, XEVENT_TYPE_PEER);
        if (pData == NULL) break;
        nRegistered++;
    }

    CHECK(nRegistered == 4, "Every descriptor up to the limit is registered");
    CHECK(events.nEventCount == 4, "The loop counts what it watches");

    /* What the limit means depends on the backend: for poll the array is
     * the descriptor table, so it caps what can be watched; for epoll it is
     * only the ready batch, so more descriptors are perfectly fine. Either
     * way the count has to agree with what was actually accepted. */
    CHECK(XSock_CreatePair(pairs[4]) == XSTDOK, "One more socket pair is created");
    xevent_data_t *pExtra = XEvents_RegisterEvent(&events, NULL, pairs[4][0], XPOLLIN, XEVENT_TYPE_PEER);

    if (pExtra == NULL)
    {
        CHECK(events.nEventCount == 4, "A refused descriptor is not counted");
        xclosesock(pairs[4][0]);
    }
    else
    {
        CHECK(events.nEventCount == 5, "An accepted descriptor past the batch size is counted");
        CHECK(pExtra->nFD == pairs[4][0], "The accepted descriptor is the one that was handed over");
        nRegistered++;
    }

    xclosesock(pairs[4][1]);

    XEvents_Destroy(&events);
    CHECK(test.nClears == (int)nRegistered, "Destroying cleared every watched descriptor exactly once");

    for (size_t i = 0; i < 4; i++) xclosesock(pairs[i][1]);
    return 0;
}

static int XTest_lookup(void)
{
    /* With the hash map on, a descriptor can be looked up again; without
     * it the loop still works but has nothing to look up by. */
    xtest_events_t test;
    memset(&test, 0, sizeof(test));

    xevents_t events;
    CHECK(XEvents_Create(&events, 16, &test, XTest_EventCallback, XTRUE) == XEVENTS_SUCCESS,
        "The loop is created with a hash map");
    CHECK(events.bUseHash == XTRUE, "The loop records that it hashes");

    XSOCKET pair[2];
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");

    xevent_data_t *pData = XEvents_RegisterEvent(&events, NULL, pair[0], XPOLLIN, XEVENT_TYPE_PEER);
    CHECK(pData != NULL, "The descriptor is registered");
    CHECK(pData->nFD == pair[0], "The registration records the descriptor");
    CHECK(pData->nType == XEVENT_TYPE_PEER, "The registration records the type");
    CHECK(pData->bIsOpen == XTRUE, "The registration is open");

    CHECK(XEvents_GetData(&events, pair[0]) == pData, "The descriptor is found again by number");
    CHECK(XEvents_GetData(&events, pair[1]) == NULL, "An unregistered descriptor is not found");
    CHECK(XEvents_GetData(&events, XSOCK_INVALID) == NULL, "An invalid descriptor is not found");

    /* Deleting removes it from the lookup too. */
    CHECK(XEvents_Delete(&events, pData) == XEVENTS_SUCCESS, "The descriptor is deleted");
    CHECK(XEvents_GetData(&events, pair[0]) == NULL, "The deleted descriptor is no longer found");
    CHECK(events.nEventCount == 0, "The loop watches nothing after the delete");

    XEvents_Destroy(&events);
    xclosesock(pair[1]);
    return 0;
}

static int XTest_event_fd(void)
{
    /* A user event is a descriptor the loop owns, used to wake the service
     * call from another thread. */
    xtest_events_t test;
    memset(&test, 0, sizeof(test));

    /* The hash map is what lets XEvents_Destroy() find the registrations
     * again; without it the loop has no way to reclaim what it handed out. */
    xevents_t events;
    CHECK(XEvents_Create(&events, 8, &test, XTest_EventCallback, XTRUE) == XEVENTS_SUCCESS,
        "The loop is created");

    xevent_data_t *pEvent = XEvents_CreateEvent(&events, NULL);
    CHECK(pEvent != NULL, "A user event is created");
    CHECK(pEvent->nType == XEVENT_TYPE_EVENT, "The user event knows its type");
    CHECK(events.nEventCount == 1, "The user event is watched");

    /* Writing to it wakes the loop and drives the read callback. */
    uint64_t nValue = 3;
    CHECK(write(pEvent->nFD, &nValue, sizeof(nValue)) == (ssize_t)sizeof(nValue), "The event is signalled");

    for (int i = 0; i < 200 && test.nUsers == 0; i++) XEvents_Service(&events, 50);
    CHECK(test.nUsers == 1, "The signalled event reached the callback");
    CHECK(test.bFailed == XFALSE, "The signalled value arrived intact");

    XEvents_Destroy(&events);
    return 0;
}

static int XTest_service_guards(void)
{
    /* A loop with nothing in it still services cleanly, and a zero timeout
     * returns promptly rather than blocking. */
    xtest_events_t test;
    memset(&test, 0, sizeof(test));

    xevents_t events;
    CHECK(XEvents_Create(&events, 8, &test, XTest_EventCallback, XFALSE) == XEVENTS_SUCCESS,
        "The loop is created");

    uint64_t nStart = XTime_GetMs();
    for (int i = 0; i < 5; i++)
    {
        xevent_status_t eStatus = XEvents_Service(&events, 0);
        CHECK(eStatus == XEVENTS_SUCCESS || eStatus == XEVENTS_CONTINUE,
            "Servicing an empty loop succeeds");
    }
    CHECK(XTime_GetMs() - nStart < 5000, "A zero timeout does not block");

    /* A short timeout waits about that long and then returns. */
    nStart = XTime_GetMs();
    XEvents_Service(&events, 100);
    uint64_t nElapsed = XTime_GetMs() - nStart;
    CHECK(nElapsed < 5000, "A short timeout returns promptly");

    /* Registering an invalid descriptor is refused. */
    CHECK(XEvents_RegisterEvent(&events, NULL, XSOCK_INVALID, XPOLLIN, XEVENT_TYPE_PEER) == NULL,
        "An invalid descriptor is refused");
    CHECK(events.nEventCount == 0, "A refused registration is not watched");

    /* Deleting takes ownership of the registration and releases it, so it
     * only ever takes something the loop handed out. A missing one is
     * refused rather than dereferenced. */
    CHECK(XEvents_Delete(&events, NULL) == XEVENTS_SUCCESS, "Deleting nothing is a no-op, not a failure");
    CHECK(XEvents_Delete(NULL, NULL) == XEVENTS_EINVALID, "Deleting from no loop is rejected");
    CHECK(XEvents_Add(NULL, NULL, XPOLLIN) != XEVENTS_SUCCESS, "Adding to no loop is rejected");
    CHECK(XEvents_Modify(NULL, NULL, XPOLLIN) != XEVENTS_SUCCESS, "Modifying on no loop is rejected");
    CHECK(XEvents_Service(NULL, 0) != XEVENTS_SUCCESS, "Servicing no loop is rejected");
    CHECK(events.nEventCount == 0, "A rejected call leaves the loop watching nothing");

    XEvents_Destroy(&events);

    /* Destroying twice is safe. */
    XEvents_Destroy(&events);
    return 0;
}

static int XTest_timer_lifecycle(void)
{
    /* A timer fires, can be extended before it fires, and can be removed. */
    xtest_events_t test;
    memset(&test, 0, sizeof(test));

    xevents_t events;
    CHECK(XEvents_Create(&events, 8, &test, XTest_EventCallback, XTRUE) == XEVENTS_SUCCESS,
        "The loop is created");

    xevent_data_t *pTimer = XEvents_AddTimer(&events, NULL, 50);
    if (pTimer == NULL)
    {
        XEvents_Destroy(&events);
        printf("Timers are not available on this build, skipping\n");
        return 77;
    }

    CHECK(pTimer->nType == XEVENT_TYPE_TIMER, "The timer knows its type");
    CHECK(events.nEventCount == 1, "The timer is watched");

    /* Extending it before it fires pushes the deadline out. */
    CHECK(XEvents_ExtendTimer(&events, pTimer, 60) == XEVENTS_SUCCESS, "The timer is extended");

    /* It eventually fires. */
    for (int i = 0; i < 500 && test.nTimers == 0; i++) XEvents_Service(&events, 20);
    CHECK(test.nTimers >= 1, "The timer eventually fired");

    /* A second timer can be removed before it ever fires. */
    int nFiredBefore = test.nTimers;
    xevent_data_t *pSecond = XEvents_AddTimer(&events, NULL, 10000);
    CHECK(pSecond != NULL, "A second timer is added");
    CHECK(XEvents_Delete(&events, pSecond) == XEVENTS_SUCCESS, "The second timer is removed");

    for (int i = 0; i < 5; i++) XEvents_Service(&events, 10);
    CHECK(test.nTimers == nFiredBefore, "The removed timer never fired");

    XEvents_Destroy(&events);
    return 0;
}

/* ---------------- interrupts and byte wakeups ---------------- */

typedef struct {
    int nInterrupts;
    int nReads;
    char cLast;
    xbool_t bStop;
} xtest_signal_t;

static int XTest_SignalCallback(void *pLoop, void *pData, XSOCKET nFD, xevent_cb_type_t eReason)
{
    xevents_t *pEvents = (xevents_t*)pLoop;
    xtest_signal_t *pTest = (xtest_signal_t*)pEvents->pUserSpace;
    (void)nFD;

    if (eReason == XEVENT_CB_INTERRUPT)
    {
        /* The loop hands the interrupt over with no event attached: it is the
         * wait itself that was cut short, not any one descriptor. */
        pTest->nInterrupts++;
        return pTest->bStop ? XEVENTS_DISCONNECT : XEVENTS_CONTINUE;
    }

    if (eReason == XEVENT_CB_READ && pData != NULL)
    {
        char cByte = 0;
        if (XEvent_ReadByte((xevent_data_t*)pData, &cByte) == 1)
        {
            pTest->cLast = cByte;
            pTest->nReads++;
        }
    }

    return XEVENTS_CONTINUE;
}

static volatile sig_atomic_t g_nAlarms = 0;
static void XTest_AlarmHandler(int nSignal) { (void)nSignal; g_nAlarms++; }

static int XTest_interrupted(void)
{
    /* A signal delivered while the loop is waiting has to come back as an
     * interrupt callback rather than as a failure, and the answer the
     * callback gives is what decides whether the loop carries on. */
#ifdef _WIN32
    return 77;
#else
    xtest_signal_t test;
    memset(&test, 0, sizeof(test));

    xevents_t loop;
    CHECK(XEvents_Create(&loop, 8, &test, XTest_SignalCallback, XTRUE) == XEVENTS_SUCCESS,
        "Create the interrupt loop");

    /* Something has to be registered: an empty loop just sleeps. */
    int nPipe[2];
    if (pipe(nPipe) != 0)
    {
        XEvents_Destroy(&loop);
        printf("No pipe available, skipping\n");
        return 77;
    }

    xevent_data_t *pRead = XEvents_RegisterEvent(&loop, NULL, nPipe[0], XPOLLIN, XEVENT_TYPE_CUSTOM);
    CHECK(pRead != NULL, "Register the read end");

    /* No SA_RESTART, so the wait is cut short instead of resumed. */
    struct sigaction action, previous;
    memset(&action, 0, sizeof(action));
    action.sa_handler = XTest_AlarmHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGALRM, &action, &previous) != 0)
    {
        close(nPipe[0]);
        close(nPipe[1]);
        XEvents_Destroy(&loop);
        printf("SIGALRM cannot be installed, skipping\n");
        return 77;
    }

    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_usec = 30000;

    g_nAlarms = 0;
    CHECK(setitimer(ITIMER_REAL, &timer, NULL) == 0, "Arm the alarm");

    /* A continuing callback turns the interrupt into an ordinary return. */
    xevent_status_t nStatus = XEvents_Service(&loop, 2000);
    CHECK(g_nAlarms >= 1, "The alarm was delivered");
    CHECK(test.nInterrupts >= 1, "The interrupted wait reached the callback");
    CHECK(nStatus == XEVENTS_SUCCESS, "A continuing callback keeps the loop alive");

    /* A callback that asks to stop turns the same interrupt into an error
     * the service loop reports to its caller. */
    test.bStop = XTRUE;
    test.nInterrupts = 0;

    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_usec = 30000;
    g_nAlarms = 0;
    CHECK(setitimer(ITIMER_REAL, &timer, NULL) == 0, "Re-arm the alarm");

    nStatus = XEvents_Service(&loop, 2000);
    CHECK(test.nInterrupts >= 1, "The second interrupt reached the callback too");
    CHECK(nStatus == XEVENTS_EINTR, "A stopping callback surfaces as an interrupt status");
    CHECK(XEvents_GetStatusStr(XEVENTS_EINTR) != NULL, "The interrupt status has a description");

    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_REAL, &timer, NULL);
    sigaction(SIGALRM, &previous, NULL);

    close(nPipe[1]);
    XEvents_Destroy(&loop);
    close(nPipe[0]);
    return 0;
#endif
}

static int XTest_write_byte(void)
{
    /* One byte through a registered descriptor is the loop's own wakeup
     * primitive, so it has to survive the round trip and reject a
     * descriptor that has already gone. */
#ifdef _WIN32
    return 77;
#else
    xtest_signal_t test;
    memset(&test, 0, sizeof(test));

    xevents_t loop;
    CHECK(XEvents_Create(&loop, 8, &test, XTest_SignalCallback, XTRUE) == XEVENTS_SUCCESS,
        "Create the wakeup loop");

    int nPipe[2];
    if (pipe(nPipe) != 0)
    {
        XEvents_Destroy(&loop);
        printf("No pipe available, skipping\n");
        return 77;
    }

    xevent_data_t *pRead = XEvents_RegisterEvent(&loop, NULL, nPipe[0], XPOLLIN, XEVENT_TYPE_CUSTOM);
    CHECK(pRead != NULL, "Register the read end");

    xevent_data_t writer;
    memset(&writer, 0, sizeof(writer));
    writer.nFD = nPipe[1];

    CHECK(XEvent_WriteByte(&writer, 'q') == 1, "One byte goes out");
    CHECK(XEvents_Service(&loop, 200) == XEVENTS_SUCCESS, "The loop services the wakeup");
    CHECK(test.nReads == 1, "The byte woke the loop exactly once");
    CHECK(test.cLast == 'q', "The byte arrived unchanged");

    /* Every byte value has to survive, including the zero byte a caller
     * might use as a sentinel. */
    const char sBytes[] = {'\0', 'A', (char)0xFF, '\n'};
    for (size_t i = 0; i < sizeof(sBytes); i++)
    {
        test.nReads = 0;
        test.cLast = 'z';

        CHECK(XEvent_WriteByte(&writer, sBytes[i]) == 1, "Each byte goes out");
        CHECK(XEvents_Service(&loop, 200) == XEVENTS_SUCCESS, "Each byte is serviced");
        CHECK(test.nReads == 1, "Each byte wakes the loop once");
        CHECK(test.cLast == sBytes[i], "Each byte arrives unchanged");
    }

    CHECK(XEvent_WriteByte(NULL, 'x') == XEVENTS_EINVALID, "A missing event is rejected");

    xevent_data_t closed;
    memset(&closed, 0, sizeof(closed));
    closed.nFD = XSOCK_INVALID;
    CHECK(XEvent_WriteByte(&closed, 'x') == XEVENTS_EINVALID, "An invalid descriptor is rejected");
    CHECK(XEvent_ReadByte(&closed, NULL) == XEVENTS_EINVALID, "Reading one is rejected too");
    CHECK(XEvent_ReadU64(&closed, NULL) == XEVENTS_EINVALID, "Reading a counter is rejected too");
    CHECK(XEvent_ReadByte(NULL, NULL) == XEVENTS_EINVALID, "A missing event has nothing to read");
    CHECK(XEvent_ReadU64(NULL, NULL) == XEVENTS_EINVALID, "A missing event has no counter");

    /* A byte written to a reader that has gone reports the failure rather
     * than the count, and must not raise SIGPIPE in the process. */
    close(nPipe[1]);
    XEvents_Destroy(&loop);
    close(nPipe[0]);
    return 0;
#endif
}

XTEST_MAIN(
    XTEST_CASE(lifecycle),
    XTEST_CASE(modify),
    XTEST_CASE(callback_delete),
    XTEST_CASE(timers),
    XTEST_CASE(timer_order),
    XTEST_CASE(wakeup),
    XTEST_CASE(status_strings),
    XTEST_CASE(capacity),
    XTEST_CASE(lookup),
    XTEST_CASE(event_fd),
    XTEST_CASE(service_guards),
    XTEST_CASE(timer_lifecycle),
    XTEST_CASE(interrupted),
    XTEST_CASE(write_byte)
)
