/*!
 *  @file libxutils/src/net/event.c
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of the cross-platform
 * async event engine based on POLL and EPOLL.
 */

#include "xstd.h"
#include "event.h"
#include "xtime.h"

#define XEVENTS_DEFAULT_FD_MAX      1024
#define XEVENTS_RETURN_VALUE(val)   ((val != XEVENTS_CONTINUE) ? val : XEVENTS_ACTION)

const char *XEvents_GetStatusStr(xevent_status_t status)
{
    switch(status)
    {
       case XEVENTS_ECTL:
            return "Failed to call poll()/epoll_ctl()";
        case XEVENTS_EWAIT:
            return "Failed to call poll()/epoll_wait()";
        case XEVENTS_ENOCB:
            return "Event service callback is not set up";
        case XEVENTS_EMAX:
            return "Maximum number of file descriptors reached";
        case XEVENTS_EOMAX:
            return "Unable to detect max file descriptors for events";
        case XEVENTS_EALLOC:
            return "Failed to allocate memory for event array";
        case XEVENTS_ETIMER:
            return "Failed to create or register timer event";
        case XEVENTS_EEXTEND:
            return "Failed to extend existing timer event";
        case XEVENTS_EINTR:
            return "Event service loop interrupted by signal";
        case XEVENTS_EBREAK:
            return "Requested break from event service loop";
        case XEVENTS_ECREATE:
            return "Failed to create event instance";
        case XEVENTS_EINSERT:
            return "Failed to insert event data to hash map";
        case XEVENTS_EINVALID:
            return "Invalid argument for event operation";
        case XEVENTS_SUCCESS:
            return "Last operation completed successfully";
        case XEVENTS_NONE:
        default: break;
    }

    return "Undefined error";
}

static int XEvents_InterruptCb(void *pCtx)
{
    XASSERT(pCtx, XEVENTS_EINVALID);
    xevents_t *pEvents = (xevents_t*)pCtx;

    int nRetVal = pEvents->eventCallback(pEvents, NULL, XSOCK_INVALID, XEVENT_CB_INTERRUPT);
    return (nRetVal == XEVENTS_CONTINUE) ? XEVENTS_SUCCESS : XEVENTS_EINTR;
}

static void XEvents_ClearCb(void *pCtx, void *pData, int nKey)
{
    xevent_data_t *pEvData = (xevent_data_t*)pData;
    xevents_t *pEvents = (xevents_t*)pCtx;
    XSOCKET nFD = (XSOCKET)nKey;

    if (pEvData != NULL)
    {
#ifdef __linux__
        // Close fd for timer and event types
        // All other types are managed by user
        if (pEvData->nType == XEVENT_TYPE_TIMER ||
            pEvData->nType == XEVENT_TYPE_EVENT)
        {
            if (pEvData->nFD != XSOCK_INVALID)
            {
                close(pEvData->nFD);
                pEvData->nFD = XSOCK_INVALID;
            }
        }
#endif

        if (pEvents != NULL && pEvents->eventCallback != NULL)
            pEvents->eventCallback(pEvents, pEvData, nFD, XEVENT_CB_CLEAR);

        free(pEvData);
    }
}

static int XEvents_EventCb(xevents_t *pEvents, xevent_data_t *pData, XSOCKET nFD, xevent_cb_type_t nReason)
{
    int nRetVal = pEvents->eventCallback(pEvents, pData, nFD, nReason);
    if (nRetVal == XEVENTS_ACCEPT) return XEVENTS_ACTION;

    if (nRetVal <= XEVENTS_DISCONNECT)
    {
        XEvents_Delete(pEvents, pData);
        return XEVENTS_DISCONNECT;
    }

    while (nRetVal == XEVENTS_USERCALL)
    {
        // Provide user callback until it is asked
        nRetVal = pEvents->eventCallback(pEvents, pData, nFD, XEVENT_CB_USER);
    }

    return nRetVal;
}

int XEvent_WriteByte(xevent_data_t *pData, const char cVal)
{
    XASSERT((pData && pData->nFD != XSOCK_INVALID), XEVENTS_EINVALID);

#ifdef _WIN32
    int nRet = (int)send(pData->nFD, &cVal, sizeof(cVal), XMSG_NOSIGNAL);
#else
    int nRet = (int)write(pData->nFD, &cVal, sizeof(char));
#endif

    return nRet;
}

int XEvent_ReadByte(xevent_data_t *pData, char *pVal)
{
    XASSERT((pData && pData->nFD != XSOCK_INVALID), XEVENTS_EINVALID);
    char nVal = 0;

#ifdef _WIN32
    int nRet = (int)recv(pData->nFD, (char*)&nVal, sizeof(char), XMSG_NOSIGNAL);
#else
    int nRet = (int)read(pData->nFD, &nVal, sizeof(char));
#endif

    if (nRet > 0 && pVal != NULL) *pVal = nVal;
    return nRet;
}

/*
    Note: This function is primarily used for reading timerfd values on Linux.
    On other platforms, it can be used for reading uint64_t values from any fd,
    but note that reading uint64_t is atomic operation only on Linux timerfds.
*/
int XEvent_ReadU64(xevent_data_t *pData, uint64_t *pVal)
{
    XASSERT((pData && pData->nFD != XSOCK_INVALID), XEVENTS_EINVALID);
    uint64_t nVal = 0;

#ifdef _WIN32
    int nRet = (int)recv(pData->nFD, (char*)&nVal, sizeof(uint64_t), XMSG_NOSIGNAL);
#else
    int nRet = (int)read(pData->nFD, &nVal, sizeof(uint64_t));
#endif

    if (nRet > 0 && pVal != NULL) *pVal = nVal;
    return nRet;
}

xevent_data_t* XEvents_RegisterEvent(xevents_t *pEvents, void *pCtx, XSOCKET nFd, int nEvents, int nType)
{
    XASSERT((pEvents != NULL), NULL);
    XASSERT((nFd != XSOCK_INVALID), NULL);

    xevent_data_t* pData = XEvents_NewData(pCtx, nFd, nType);
    XASSERT((pData != NULL), NULL);

    xevent_status_t nStatus = XEvents_Add(pEvents, pData, nEvents);
    XASSERT_CALL((nStatus == XEVENTS_SUCCESS), free, pData, NULL);

    pData->nEvents = nEvents;
    return pData;
}

xevent_data_t* XEvents_CreateEvent(xevents_t *pEvents, void *pCtx)
{
    XASSERT((pEvents != NULL), NULL);

#ifdef __linux__
    int nEventFD = eventfd(0, EFD_NONBLOCK);
    XASSERT((nEventFD >= 0), NULL);

    xevent_data_t* pData = XEvents_RegisterEvent(pEvents, pCtx, nEventFD, XPOLLIN, XEVENT_TYPE_EVENT);
    XASSERT_CALL((pData != NULL), close, nEventFD, NULL);

    return pData;
#else
    // No-op for non-linux systems
    (void)pCtx;
    return NULL;
#endif
}

#ifdef _USE_EVENT_LIST
static void XEvents_ListClearCb(void *pCtx, void *pData)
{
    xevents_t *pEvents = (xevents_t*)pCtx;
    xevent_data_t *pEvData = (xevent_data_t*)pData;
    XSOCKET nFD = pEvData ? pEvData->nFD : XSOCK_INVALID;
    XEvents_ClearCb(pEvents, pEvData, (int)nFD);
}

static int XEvents_NodeSearchCb(void *pUserPtr, xlist_t *pNode)
{
    xevent_data_t *pSearchData = (xevent_data_t*)pUserPtr;
    XASSERT_RET((pNode && pNode->data.pData), XFALSE);
    xevent_data_t *pNodeData = (xevent_data_t*)pNode->data.pData;
    if (!pNodeData->nTimerValue) return XTRUE; // Placed at the end if no timer value
    return (pSearchData->nTimerValue <= pNodeData->nTimerValue) ? XTRUE : XFALSE;
}

static int XEvents_TimerSearchCb(void *pUserPtr, xlist_t *pNode)
{
    xevent_data_t *pSearchData = (xevent_data_t*)pUserPtr;
    XASSERT_RET((pNode && pNode->data.pData), XFALSE);
    xevent_data_t *pNodeData = (xevent_data_t*)pNode->data.pData;
    return (pNodeData->nTimerValue == pSearchData->nTimerValue &&
            pNodeData->pContext == pSearchData->pContext) ? XTRUE : XFALSE;
}

static xlist_t* XEvents_AddNodeSorted(xlist_t *pList, xlist_t *pNode)
{
    XASSERT_RET((pList != NULL && pNode != NULL), NULL);
    xevent_data_t *pData = (xevent_data_t*)pNode->data.pData;
    xlist_t *pSearchNode = XList_Search(pList, pData, XEvents_NodeSearchCb);
    if (pSearchNode != NULL) return XList_InsertPrev(pSearchNode, pNode);
    return XList_InsertTail(pList, pNode);
}

static xlist_t* XEvents_AddTimerSorted(xlist_t *pList, void *pData)
{
    XASSERT_RET((pList != NULL && pData != NULL), NULL);
    xlist_t *pNode = XList_Search(pList, pData, XEvents_TimerSearchCb);
    if (pNode != NULL) return XList_PushPrev(pNode, pData, XSTDNON);
    return XList_PushBack(pList, pData, XSTDNON);
}

static xevent_data_t* XEvents_AddTimerCommon(xevents_t *pEvents, void *pCtx, int nTimeoutMs)
{
    XASSERT((pEvents != NULL), NULL);
    XASSERT((nTimeoutMs > 0), NULL);

    xevent_data_t* pData = XEvents_NewData(pCtx, XSOCK_INVALID, XEVENT_TYPE_TIMER);
    XASSERT((pData != NULL), NULL);

    pData->nTimerValue = XTime_GetMs() + nTimeoutMs;
    pData->pTimerNode = XEvents_AddTimerSorted(&pEvents->timerList, pData);
    XASSERT_CALL((pData->pTimerNode != NULL), free, pData, NULL);

    return pData;
}

static xevent_status_t XEvents_ExtendTimerCommon(xevents_t *pEvents, xevent_data_t *pTimer, int nTimeoutMs)
{
    XASSERT((pTimer != NULL), XEVENTS_EINVALID);
    XASSERT((nTimeoutMs > 0), XEVENTS_EINVALID);
    XASSERT((pTimer->nType == XEVENT_TYPE_TIMER), XEVENTS_EINVALID);

    if (pTimer->pTimerNode == NULL)
    {
        pTimer->nTimerValue = XTime_GetMs() + nTimeoutMs;
        pTimer->pTimerNode = XEvents_AddTimerSorted(&pEvents->timerList, pTimer);
        return (pTimer->pTimerNode == NULL) ? XEVENTS_EEXTEND : XEVENTS_SUCCESS;
    }

    pTimer->nTimerValue = XTime_GetMs() + nTimeoutMs;
    XList_Detach(pTimer->pTimerNode);

    xlist_t *pNode = XEvents_AddNodeSorted(&pEvents->timerList, pTimer->pTimerNode);
    return (pNode != NULL) ? XEVENTS_SUCCESS : XEVENTS_EEXTEND;
}

static int XEvents_TimerServiceCommon(xevents_t *pEvents, uint64_t nNowMs, xbool_t *pBreak)
{
    XASSERT_RET((pEvents != NULL), XSTDNON);
    XASSERT_RET((pEvents->timerList.pNext != NULL), XSTDNON);

    xlist_t *pNode = pEvents->timerList.pNext;
    xevent_data_t *pTimerData = (xevent_data_t*)pNode->data.pData;

    while (pTimerData && pTimerData->nTimerValue && pTimerData->nTimerValue <= nNowMs)
    {
        pTimerData->nTimerValue = XSTDNON;

        int nRetVal = XEvents_EventCb(pEvents, pTimerData, pTimerData->nFD, XEVENT_CB_TIMEOUT);
        if (nRetVal > XEVENTS_DISCONNECT && pEvents->timerList.pNext == pNode)
        {
            XList_Detach(pNode);
            XEvents_AddNodeSorted(&pEvents->timerList, pNode);
        }

        if (nRetVal == XEVENTS_BREAK)
        {
            if (pBreak) *pBreak = XTRUE;
            return XSTDERR;
        }

        pNode = pEvents->timerList.pNext;
        pTimerData = pNode ? (xevent_data_t*)pNode->data.pData : NULL;
    }

    return pTimerData != NULL && pTimerData->nTimerValue ?
        (int)(pTimerData->nTimerValue - nNowMs) : XSTDNON;
}
#else
static xevent_data_t* XEvents_AddTimerLinux(xevents_t *pEvents, void *pContext, int nTimeoutMs)
{
    XASSERT((pEvents != NULL), NULL);
    XASSERT((nTimeoutMs > 0), NULL);

    XSOCKET nTimerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    XASSERT((nTimerFD >= 0), NULL);

    struct itimerspec its = {0};
    its.it_value.tv_sec  = nTimeoutMs / 1000;
    its.it_value.tv_nsec = (nTimeoutMs % 1000) * 1000000;
    timerfd_settime(nTimerFD, 0, &its, NULL);

    xevent_data_t* pTimerData = XEvents_NewData(pContext, nTimerFD, XEVENT_TYPE_TIMER);
    XASSERT_CALL((pTimerData != NULL), close, nTimerFD, NULL);

    xevent_status_t nStatus = XEvents_Add(pEvents, pTimerData, XPOLLIN);
    XASSERT_CALL2((nStatus == XEVENTS_SUCCESS), close, nTimerFD, free, pTimerData, NULL);

    pTimerData->nEvents = XPOLLIN;
    return pTimerData;
}

static xevent_status_t XEvents_ExtendTimerLinux(xevents_t *pEvents, xevent_data_t *pTimer, int nTimeoutMs)
{
    XASSERT((pTimer != NULL), XEVENTS_EINVALID);
    XASSERT((nTimeoutMs > 0), XEVENTS_EINVALID);
    XASSERT((pTimer->nType == XEVENT_TYPE_TIMER), XEVENTS_EINVALID);

    struct itimerspec its = {0};
    its.it_value.tv_sec  = nTimeoutMs / 1000;
    its.it_value.tv_nsec = (nTimeoutMs % 1000) * 1000000;

    int nRet = timerfd_settime(pTimer->nFD, 0, &its, NULL);
    XASSERT((nRet == 0), XEVENTS_EEXTEND);

    (void)pEvents;
    return XEVENTS_SUCCESS;
}

static int XEvents_TimerService(xevents_t *pEvents, xevent_data_t *pData, XSOCKET nFD, uint32_t nEvents)
{
    XASSERT((pEvents && pData), XEVENTS_EINVALID);
    XASSERT_RET((pData->nType == XEVENT_TYPE_TIMER), XEVENTS_CONTINUE);

    if ((nEvents & XPOLLIN) && XEvent_ReadU64(pData, NULL) > 0)
        return XEvents_EventCb(pEvents, pData, nFD, XEVENT_CB_TIMEOUT);

    XEvents_EventCb(pEvents, pData, nFD, XEVENT_CB_ERROR);
    XEvents_Delete(pEvents, pData);

    return XEVENTS_RELOOP;
}
#endif

xevent_data_t* XEvents_AddTimer(xevents_t *pEvents, void *pContext, int nTimeoutMs)
{
#ifdef _USE_EVENT_LIST
    return XEvents_AddTimerCommon(pEvents, pContext, nTimeoutMs);
#else
    return XEvents_AddTimerLinux(pEvents, pContext, nTimeoutMs);
#endif
}

xevent_status_t XEvents_ExtendTimer(xevents_t *pEvents, xevent_data_t *pTimer, int nTimeoutMs)
{
#ifdef _USE_EVENT_LIST
    return XEvents_ExtendTimerCommon(pEvents, pTimer, nTimeoutMs);
#else
    return XEvents_ExtendTimerLinux(pEvents, pTimer, nTimeoutMs);
#endif
}

static int XEvents_ServiceCb(xevents_t *pEvents, xevent_data_t *pData, XSOCKET nFD, uint32_t nEvents)
{
    XASSERT((pEvents && pData), XEVENTS_EINVALID);
    if (pData != NULL) pData->nEvents = nEvents;
    int nRetVal = XEVENTS_CONTINUE;

#ifndef _USE_EVENT_LIST
    if (pData->nType == XEVENT_TYPE_TIMER)
    {
        nRetVal = XEvents_TimerService(pEvents, pData, nFD, nEvents);
        return XEVENTS_RETURN_VALUE(nRetVal);
    }
#endif

#ifdef XPOLLRDHUP
    if (nEvents & XPOLLRDHUP)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_CB_CLOSED);
        return XEVENTS_RETURN_VALUE(nRetVal);
    }
#endif

    if (nEvents & XPOLLHUP)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_CB_HUNGED);
        return XEVENTS_RETURN_VALUE(nRetVal);
    }

    if (nEvents & XPOLLERR)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_CB_ERROR);
        return XEVENTS_RETURN_VALUE(nRetVal);
    }

    if (nEvents & XPOLLPRI)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_CB_EXCEPTION);
        return XEVENTS_RETURN_VALUE(nRetVal);
    }

    if (nEvents & XPOLLOUT)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_CB_WRITE);
        if (nRetVal != XEVENTS_CONTINUE) return nRetVal;
    }

    if (nEvents & XPOLLIN)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_CB_READ);
        if (nRetVal != XEVENTS_CONTINUE) return nRetVal;
    }

    return XEVENTS_CONTINUE;
}

static void XEvents_DestroyEventMap(xevents_t *pEvents)
{
    XASSERT_VOID_RET(pEvents);
    if (pEvents->bUseHash)
    {
        pEvents->eventsMap.clearCb = XEvents_ClearCb;
        pEvents->eventsMap.pUserContext = pEvents;
        XHash_Destroy(&pEvents->eventsMap);
        pEvents->bUseHash = XFALSE;
    }
}

void XEvents_Destroy(xevents_t *pEvents)
{
    XASSERT_VOID_RET(pEvents);

    if (pEvents->pEventArray)
    {
        free(pEvents->pEventArray);
        pEvents->pEventArray = NULL;
    }

#ifdef __linux__
    if (pEvents->nEventFd >= 0)
    {
        close(pEvents->nEventFd);
        pEvents->nEventFd = 0;
    }
#endif

#ifdef _USE_EVENT_LIST
    XList_Clear(&pEvents->timerList);
#endif

    XEvents_DestroyEventMap(pEvents);
    pEvents->eventCallback(pEvents, NULL, XSOCK_INVALID, XEVENT_CB_DESTROY);
}

xevent_status_t XEvents_Create(xevents_t *pEvents, uint32_t nMax, void *pUser, xevent_cb_t callBack, xbool_t bUseHash)
{
    XASSERT(pEvents, XEVENTS_EINVALID);
    XASSERT(callBack, XEVENTS_ENOCB);

#ifdef _WIN32
    uint32_t nSysMax = XEVENTS_DEFAULT_FD_MAX;
#else
    uint32_t nSysMax = sysconf(_SC_OPEN_MAX);
#endif

    if (nSysMax && nMax) pEvents->nEventMax = nMax > nSysMax ? nSysMax : nMax;
    else if (nSysMax) pEvents->nEventMax = nSysMax;
    else if (nMax) pEvents->nEventMax = nMax;
    else return XEVENTS_EOMAX;

    pEvents->eventCallback = callBack;
    pEvents->pUserSpace = pUser;
    pEvents->bUseHash = bUseHash;
    pEvents->nEventCount = 0;
    pEvents->pEventArray = NULL;

#ifdef _USE_EVENT_LIST
    XList_Init(&pEvents->timerList, NULL, XSTDNON, XEvents_ListClearCb, pEvents);
#endif

    if (pEvents->bUseHash) XHash_Init(&pEvents->eventsMap, XEvents_ClearCb, pEvents);

#ifdef __linux__
    struct epoll_event *pEventArray = calloc(pEvents->nEventMax, sizeof(struct epoll_event));
    XASSERT_CALL((pEventArray != NULL), XEvents_DestroyEventMap, pEvents, XEVENTS_EALLOC);

    pEvents->nEventFd = epoll_create1(0);
    XASSERT_CALL2((pEvents->nEventFd >= 0),
        XEvents_DestroyEventMap, pEvents,
        free, pEventArray,
        XEVENTS_ECREATE);

    pEvents->pEventArray = pEventArray;
#else
    pEvents->pEventArray = calloc(pEvents->nEventMax, sizeof(struct pollfd));
    XASSERT_CALL((pEvents->pEventArray != NULL), XEvents_DestroyEventMap, pEvents, XEVENTS_EALLOC);

    uint32_t i = 0;
    for (i = 0; i < pEvents->nEventMax; i++)
    {
        pEvents->pEventArray[i].revents = 0;
        pEvents->pEventArray[i].events = 0;
        pEvents->pEventArray[i].fd = XSOCK_INVALID;
    }
#endif

    return XEVENTS_SUCCESS;
}

xevent_data_t *XEvents_NewData(void *pCtx, XSOCKET nFd, int nType)
{
    xevent_data_t* pData = (xevent_data_t*)malloc(sizeof(xevent_data_t));
    XASSERT((pData != NULL), NULL);

#ifdef _USE_EVENT_LIST
    pData->nTimerValue = XSTDNON;
    pData->pTimerNode = NULL;
#endif

    pData->pContext = pCtx;
    pData->bIsOpen = XTRUE;
    pData->nEvents = 0;
    pData->nIndex = -1;
    pData->nType = nType;
    pData->nFD = nFd;
    return pData;
}

xevent_status_t XEvents_Add(xevents_t *pEvents, xevent_data_t* pData, int nEvents)
{
    XASSERT((pEvents != NULL && pData != NULL), XEVENTS_EINVALID);
    XASSERT((pData->nFD != XSOCK_INVALID), XEVENTS_EINVALID);

#ifdef __linux__
    struct epoll_event event;
    event.data.ptr = pData;
    event.events = nEvents;
    if (epoll_ctl(pEvents->nEventFd, EPOLL_CTL_ADD, pData->nFD, &event) < 0) return XEVENTS_ECTL;
#else
    if (pEvents->nEventCount >= pEvents->nEventMax) return XEVENTS_ECTL;
    pData->nIndex = pEvents->nEventCount;
    pEvents->pEventArray[pData->nIndex].revents = 0;
    pEvents->pEventArray[pData->nIndex].events = (short)nEvents;
    pEvents->pEventArray[pData->nIndex].fd = pData->nFD;
#endif

    if (pEvents->bUseHash && XHash_Insert(&pEvents->eventsMap, pData, XSTDNON, (int)pData->nFD) < 0)
    {
#ifdef __linux__
        epoll_ctl(pEvents->nEventFd, EPOLL_CTL_DEL, pData->nFD, NULL);
#else
        pEvents->pEventArray[pData->nIndex].revents = 0;
        pEvents->pEventArray[pData->nIndex].events = 0;
        pEvents->pEventArray[pData->nIndex].fd = XSOCK_INVALID;
        pData->nIndex = -1;
#endif
        return XEVENTS_EINSERT;
    }

    pData->nEvents = nEvents;
    pEvents->nEventCount++;

    return XEVENTS_SUCCESS;
}

xevent_status_t XEvents_Modify(xevents_t *pEvents, xevent_data_t *pData, int nEvents)
{
    XASSERT((pEvents != NULL), XEVENTS_EINVALID);
    XASSERT((pData != NULL), XEVENTS_EINVALID);

#ifdef __linux__
    struct epoll_event event;
    event.data.ptr = pData;
    event.events = nEvents;
    XASSERT((pData->nFD != XSOCK_INVALID), XEVENTS_ECTL);
    if (epoll_ctl(pEvents->nEventFd, EPOLL_CTL_MOD, pData->nFD, &event) < 0) return XEVENTS_ECTL;
#else
    XASSERT((pData->nIndex >= 0), XEVENTS_ECTL);
    XASSERT(((uint32_t)pData->nIndex < pEvents->nEventCount), XEVENTS_ECTL);
    pEvents->pEventArray[pData->nIndex].events = (short)nEvents;
#endif

    pData->nEvents = nEvents;
    return XEVENTS_SUCCESS;
}

xevent_status_t XEvents_Delete(xevents_t *pEvents, xevent_data_t *pData)
{
    XASSERT((pEvents != NULL), XEVENTS_EINVALID);
    XASSERT_RET((pData != NULL), XEVENTS_SUCCESS);
    XSTATUS nStatus = XSTDERR;

#ifdef _USE_EVENT_LIST
    if (pData->nType == XEVENT_TYPE_TIMER)
    {
        XList_Unlink(pData->pTimerNode);
        return XEVENTS_SUCCESS;
    }
#endif

#ifdef __linux__
    if (pData->nFD >= 0)
    {
        nStatus = epoll_ctl(pEvents->nEventFd, EPOLL_CTL_DEL, pData->nFD, NULL);
        if (nStatus >= 0 && pEvents->nEventCount) pEvents->nEventCount--;
    }
#else
    if (pData->nIndex >= 0 && (uint32_t)pData->nIndex < pEvents->nEventCount)
    {
        pEvents->pEventArray[pData->nIndex].revents = XSTDNON;
        pEvents->pEventArray[pData->nIndex].events = XSTDNON;
        pEvents->pEventArray[pData->nIndex].fd = XSOCK_INVALID;
        int i = nStatus = XSTDNON;

        for (i = pData->nIndex; (uint32_t)i < pEvents->nEventCount; i++)
            pEvents->pEventArray[i] = pEvents->pEventArray[i + 1];

        pEvents->nEventCount--;
        pData->nIndex = -1;
    }
#endif

    if (!pEvents->bUseHash || pData->nFD == XSOCK_INVALID ||
        XHash_Delete(&pEvents->eventsMap, (int)pData->nFD) < 0)
        XEvents_ClearCb(pEvents, pData, (int)pData->nFD);

    return (nStatus < 0) ? XEVENTS_ECTL : XEVENTS_SUCCESS;
}

xevent_data_t* XEvents_GetData(xevents_t *pEvents, XSOCKET nFD)
{
    XASSERT((pEvents != NULL), NULL);
    XASSERT_RET((pEvents->bUseHash && nFD != XSOCK_INVALID), NULL);
    return (xevent_data_t*)XHash_GetData(&pEvents->eventsMap, (int)nFD);
}

xevent_status_t XEvents_Service(xevents_t *pEvents, int nTimeoutMs)
{
    XASSERT(pEvents, XEVENTS_EINVALID);
    int i = 0, nCount = 0, nRet = 0;
    int nTimeout = nTimeoutMs;

#ifdef _USE_EVENT_LIST
    xbool_t bBreak = XFALSE;
    nTimeout = XEvents_TimerServiceCommon(pEvents, XTime_GetMs(), &bBreak);
    if (nTimeout <= 0 || nTimeout > nTimeoutMs) nTimeout = nTimeoutMs;
    if (bBreak) return XEVENTS_BREAK;
#endif

#ifdef __linux__
    nCount = epoll_wait(pEvents->nEventFd, pEvents->pEventArray, pEvents->nEventMax, nTimeout);
#elif _WIN32
    nCount = WSAPoll(pEvents->pEventArray, pEvents->nEventCount, nTimeout);
#else
    nCount = poll(pEvents->pEventArray, pEvents->nEventCount, nTimeout);
#endif

    if (errno == EINTR) return XEvents_InterruptCb(pEvents);
    if (!nCount) return XEVENTS_SUCCESS;

    XASSERT((nCount > 0), XEVENTS_EWAIT);
    XASSERT(((uint32_t)nCount <= pEvents->nEventMax), XEVENTS_EWAIT);

#ifdef __linux__
    for (i = 0; i < nCount; i++)
    {
        if (pEvents->pEventArray[i].data.ptr == NULL) continue;

        xevent_data_t *pData = (xevent_data_t*)pEvents->pEventArray[i].data.ptr;
        uint32_t nEvents = pEvents->pEventArray[i].events;

        nRet = XEvents_ServiceCb(pEvents, pData, pData->nFD, nEvents);
        if (nRet != XEVENTS_CONTINUE) break;
    }
#else
    for (i = 0; i < (int)pEvents->nEventCount; i++)
    {
        if (pEvents->pEventArray[i].revents <= 0) continue;
        else if (pEvents->pEventArray[i].fd == XSOCK_INVALID) break;

        pEvents->pEventArray[i].revents = 0;
        XSOCKET nFD = pEvents->pEventArray[i].fd;
        int nEvents = pEvents->pEventArray[i].revents;
        xevent_data_t *pData = XEvents_GetData(pEvents, nFD);

        nRet = XEvents_ServiceCb(pEvents, pData, nFD, nEvents);
        if (nRet != XEVENTS_CONTINUE) break;
    }
#endif

    return nRet == XEVENTS_BREAK ?
        XEVENTS_EBREAK :
        XEVENTS_SUCCESS;
}
