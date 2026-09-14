/* libxutils: callback mutation, descriptor ownership, timer and wakeup regressions. */
#include "test.h"
#include "event.h"
#include "xtime.h"

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

XTEST_MAIN(XTEST_CASE(lifecycle), XTEST_CASE(modify), XTEST_CASE(callback_delete), XTEST_CASE(timers), XTEST_CASE(wakeup))
