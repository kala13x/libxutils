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

#define XEVENTS_DEFAULT_FD_MAX 1024

const char *XEvents_GetStatusStr(xevent_status_t status)
{
    switch(status)
    {
        case XEVENT_STATUS_EMAX:
            return "Maximum number of file descriptors reached";
        case XEVENT_STATUS_ECTL:
            return "Failed to call poll()/epoll_ctl()";
        case XEVENT_STATUS_EWAIT:
            return "Failed to call poll()/epoll_wait()";
        case XEVENT_STATUS_ENOCB:
            return "Event service callback is not set up";
        case XEVENT_STATUS_EOMAX:
            return "Unable to detect max file descriptors for events";
        case XEVENT_STATUS_EALLOC:
            return "Failed to allocate memory for event array";
        case XEVENT_STATUS_ETIMER:
            return "Failed to create or register timer event";
        case XEVENT_STATUS_EEXTEND:
            return "Failed to extend timer event";
        case XEVENT_STATUS_EINTR:
            return "Event service loop interrupted by signal";
        case XEVENT_STATUS_BREAK:
            return "Requested break from event service loop";
        case XEVENT_STATUS_ECREATE:
            return "Failed to create event instance";
        case XEVENT_STATUS_EINSERT:
            return "Failed to insert event data to hash map";
        case XEVENT_STATUS_EINVALID:
            return "Invalid argument for event operation";
        case XEVENT_STATUS_SUCCESS:
            return "Last operation completed successfully";
        case XEVENT_STATUS_NONE:
        default: break;
    }

    return "Undefined error";
}

static int XEvents_EventCb(xevents_t *pEv, xevent_data_t *pData, XSOCKET nFD, int nReason)
{
    int nRetVal = pEv->eventCallback(pEv, pData, nFD, nReason);
    if (nRetVal == XEVENTS_DISCONNECT) { XEvents_Delete(pEv, pData); return XEVENTS_DISCONNECT; }
    else if (nRetVal == XEVENTS_USERCALL) return pEv->eventCallback(pEv, pData, nFD, XEVENT_USER);
    else if (nRetVal == XEVENTS_ACCEPT) return XEVENTS_ACTION;
    else if (nRetVal != XEVENTS_CONTINUE) return nRetVal;
    return XEVENTS_CONTINUE;
}

#ifdef __linux__
xevent_data_t* XEvents_CreateEvent(xevents_t *pEv, void *pCtx)
{
    XASSERT((pEv != NULL), NULL);
    int nEventFD = eventfd(0, EFD_NONBLOCK);
    XASSERT((nEventFD >= 0), NULL);

    xevent_data_t* pData = XEvents_NewData(pCtx, nEventFD, XEVENT_TYPE_USER);
    XASSERT_CALL((pData != NULL), close, nEventFD, NULL);

    int nStatus = XEvents_Add(pEv, pData, XPOLLIN);
    if (nStatus != XEVENT_STATUS_SUCCESS)
    {
        close(nEventFD);
        free(pData);
        return NULL;
    }

    return pData;
}

xevent_data_t* XEvents_RegisterTimer(xevents_t *pEvents, void *pContext, int nTimeoutMs)
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
    XASSERT_CALL2((nStatus == XEVENT_STATUS_SUCCESS), close, nTimerFD, free, pTimerData, NULL);

    pTimerData->nEvents = XPOLLIN;
    return pTimerData;
}

xevent_data_t* XEvents_AddTimeout(xevents_t *pEvents, xevent_data_t *pData, int nTimeoutMs)
{
    XASSERT((pEvents != NULL && pData != NULL), NULL);
    xevent_data_t* pTimerData;

    pTimerData = XEvents_RegisterTimer(pEvents, pData, nTimeoutMs);
    XASSERT((pTimerData != NULL), NULL);

    pData->pTimerEvent = pTimerData;
    return pTimerData;
}

xevent_status_t XEvent_ExtendTimer(xevent_data_t *pTimer, int nTimeoutMs)
{
    XASSERT((pTimer != NULL), XEVENT_STATUS_EINVALID);
    XASSERT((nTimeoutMs > 0), XEVENT_STATUS_EINVALID);
    XASSERT((pTimer->nType == XEVENT_TYPE_TIMER), XEVENT_STATUS_EINVALID);

    struct itimerspec its = {0};
    its.it_value.tv_sec  = nTimeoutMs / 1000;
    its.it_value.tv_nsec = (nTimeoutMs % 1000) * 1000000;

    int nRet = timerfd_settime(pTimer->nFD, 0, &its, NULL);
    XASSERT((nRet == 0), XEVENT_STATUS_EEXTEND);

    return XEVENT_STATUS_SUCCESS;
}

xevent_status_t XEvent_ExtendTimeout(xevent_data_t *pData, int nTimeoutMs)
{
    XASSERT((pData != NULL), XEVENT_STATUS_EINVALID);
    XASSERT((nTimeoutMs > 0), XEVENT_STATUS_EINVALID);
    XASSERT((pData->pTimerEvent != NULL), XEVENT_STATUS_EINVALID);
    return XEvent_ExtendTimer((xevent_data_t*)pData->pTimerEvent, nTimeoutMs);
}
#endif

static int XEvents_ServiceCb(xevents_t *pEvents, xevent_data_t *pData, XSOCKET nFD, uint32_t nEvents)
{
    XASSERT((pEvents && pData), XEVENT_STATUS_EINVALID);
    if (pData != NULL) pData->nEvents = nEvents;
    int nRetVal = XEVENTS_CONTINUE;

#ifdef __linux__
    if (pData->nType == XEVENT_TYPE_TIMER)
    {
        if (pData->bIsTimer)
        {
            if ((nEvents & XPOLLIN) && XEvent_Read(pData) >= 0)
            {
                nEvents = XEvents_EventCb(pEvents, pData, nFD, XEVENT_TIMER);
                if (nRetVal == XEVENTS_DISCONNECT) return XEVENTS_DISCONNECT;
                else if (nRetVal == XEVENTS_CONTINUE) return XEVENTS_CONTINUE;
            }

            XEvents_Delete(pEvents, pData);
            return XEVENTS_ACTION;
        }
        else
        {
            xevent_data_t *pEvData = (xevent_data_t*)pData->pContext;
            if ((nEvents & XPOLLIN) && XEvent_Read(pData) >= 0 && pEvData != NULL)
            {
                nRetVal = XEvents_EventCb(pEvents, pEvData, pEvData->nFD, XEVENT_TIMEOUT);
                if (nRetVal == XEVENTS_DISCONNECT) return XEVENTS_DISCONNECT;
                else if (nRetVal == XEVENTS_CONTINUE) return XEVENTS_CONTINUE;
            }

            XEvents_Delete(pEvents, pData);
            pEvData->pTimerEvent = NULL;
            return XEVENTS_ACTION;
        }
    }
#endif

    /* Check error condition */
#ifdef XPOLLRDHUP
    if (nEvents & XPOLLRDHUP)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_CLOSED);
        return nRetVal != XEVENTS_CONTINUE ? nRetVal : XEVENTS_ACTION;
    }
#endif

    if (nEvents & XPOLLHUP)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_HUNGED);
        return nRetVal != XEVENTS_CONTINUE ? nRetVal : XEVENTS_ACTION;
    }

    if (nEvents & XPOLLERR)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_ERROR);
        return nRetVal != XEVENTS_CONTINUE ? nRetVal : XEVENTS_ACTION;
    }

    if (nEvents & XPOLLPRI)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_EXCEPTION);
        return nRetVal != XEVENTS_CONTINUE ? nRetVal : XEVENTS_ACTION;
    }

    if (nEvents & XPOLLOUT)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_WRITE);
        if (nRetVal != XEVENTS_CONTINUE) return nRetVal;
    }

    if (nEvents & XPOLLIN)
    {
        nRetVal = XEvents_EventCb(pEvents, pData, nFD, XEVENT_READ);
        if (nRetVal != XEVENTS_CONTINUE) return nRetVal;
    }

    return XEVENTS_CONTINUE;
}

static int XEvents_InterruptCb(void *pCtx)
{
    XASSERT(pCtx, XEVENT_STATUS_EINVALID);
    xevents_t *pEvents = (xevents_t*)pCtx;

    int nRetVal = pEvents->eventCallback(pEvents, NULL, XSOCK_INVALID, XEVENT_INTERRUPT);
    return (nRetVal == XEVENTS_CONTINUE) ? XEVENT_STATUS_SUCCESS : XEVENT_STATUS_EINTR;
}

static void XEvents_ClearCb(void *pCtx, void *pData, int nKey)
{
    xevent_data_t *pEvData = (xevent_data_t*)pData;
    xevents_t *pEvents = (xevents_t*)pCtx;
    XSOCKET nFD = (XSOCKET)nKey;

    if (pEvData != NULL)
    {
        if (pEvData->nType == XEVENT_TYPE_TIMER)
        {
            if (pEvData->nFD != XSOCK_INVALID)
            {
                close(pEvData->nFD);
                pEvData->nFD = XSOCK_INVALID;
            }
        }

        if (pEvents != NULL)
        {
            pEvents->eventCallback(pEvents, pEvData, nFD, XEVENT_CLEAR);
        }

        free(pEvData);
    }
}

static void XEvents_DestroyMap(xevents_t *pEvents)
{
    XASSERT_VOID_RET(pEvents);
    if (pEvents->bUseHash)
    {
        pEvents->hashMap.clearCb = XEvents_ClearCb;
        pEvents->hashMap.pUserContext = pEvents;
        XHash_Destroy(&pEvents->hashMap);
        pEvents->bUseHash = XFALSE;
    }
}

xevent_status_t XEvents_Create(xevents_t *pEvents, uint32_t nMax, void *pUser, xevent_cb_t callBack, xbool_t bUseHash)
{
    XASSERT(pEvents, XEVENT_STATUS_EINVALID);
    XASSERT(callBack, XEVENT_STATUS_ENOCB);

#ifdef _WIN32
    uint32_t nSysMax = XEVENTS_DEFAULT_FD_MAX;
#else
    uint32_t nSysMax = sysconf(_SC_OPEN_MAX);
#endif

    if (nSysMax && nMax) pEvents->nEventMax = nMax > nSysMax ? nSysMax : nMax;
    else if (nSysMax) pEvents->nEventMax = nSysMax;
    else if (nMax) pEvents->nEventMax = nMax;
    else return XEVENT_STATUS_EOMAX;

    pEvents->eventCallback = callBack;
    pEvents->pUserSpace = pUser;
    pEvents->bUseHash = bUseHash;
    pEvents->nEventCount = 0;

    if (pEvents->bUseHash && XHash_Init(&pEvents->hashMap, XEvents_ClearCb, pEvents) < 0)
    {
        pEvents->bUseHash = XFALSE;
        return XEVENT_STATUS_EALLOC;
    }

#ifdef __linux__
    pEvents->pEventArray = calloc(pEvents->nEventMax, sizeof(struct epoll_event));
    if (pEvents->pEventArray == NULL)
    {
        XEvents_DestroyMap(pEvents);
        return XEVENT_STATUS_EALLOC;
    }

    pEvents->nEventFd = epoll_create1(0);
    if (pEvents->nEventFd < 0)
    {
        XEvents_DestroyMap(pEvents);
        free(pEvents->pEventArray);
        pEvents->pEventArray = NULL;
        return XEVENT_STATUS_ECREATE;
    }
#else
    pEvents->pEventArray = calloc(pEvents->nEventMax, sizeof(struct pollfd));
    if (pEvents->pEventArray == NULL)
    {
        XEvents_DestroyMap(pEvents);
        return XEVENT_STATUS_EALLOC;
    }

    uint32_t i = 0;
    for (i = 0; i < pEvents->nEventMax; i++)
    {
        pEvents->pEventArray[i].revents = 0;
        pEvents->pEventArray[i].events = 0;
        pEvents->pEventArray[i].fd = XSOCK_INVALID;
    }
#endif

    return XEVENT_STATUS_SUCCESS;
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

    XEvents_DestroyMap(pEvents);
    pEvents->eventCallback(pEvents, NULL, XSOCK_INVALID, XEVENT_DESTROY);
}

xevent_data_t *XEvents_NewData(void *pCtx, XSOCKET nFd, int nType)
{
    xevent_data_t* pData = (xevent_data_t*)malloc(sizeof(xevent_data_t));
    if (pData == NULL) return NULL;

    pData->pTimerEvent = NULL;
    pData->bIsTimer = XFALSE;
    pData->pContext = pCtx;
    pData->bIsOpen = XTRUE;
    pData->nEvents = 0;
    pData->nIndex = -1;
    pData->nType = nType;
    pData->nFD = nFd;
    return pData;
}

xevent_status_t XEvents_Add(xevents_t *pEv, xevent_data_t* pData, int nEvents)
{
    XASSERT((pEv != NULL && pData != NULL), XEVENT_STATUS_EINVALID);
    XASSERT((pData->nFD != XSOCK_INVALID), XEVENT_STATUS_EINVALID);

#ifdef __linux__
    struct epoll_event event;
    event.data.ptr = pData;
    event.events = nEvents;
    if (epoll_ctl(pEv->nEventFd, EPOLL_CTL_ADD, pData->nFD, &event) < 0) return XEVENT_STATUS_ECTL;
#else
    if (pEv->nEventCount >= pEv->nEventMax) return XEVENT_STATUS_ECTL;
    pData->nIndex = pEv->nEventCount;
    pEv->pEventArray[pData->nIndex].revents = 0;
    pEv->pEventArray[pData->nIndex].events = (short)nEvents;
    pEv->pEventArray[pData->nIndex].fd = pData->nFD;
#endif
    pEv->nEventCount++;

    if (pEv->bUseHash && XHash_Insert(&pEv->hashMap, pData, 0, (int)pData->nFD) < 0)
    {
#ifdef __linux__
        epoll_ctl(pEv->nEventFd, EPOLL_CTL_DEL, pData->nFD, NULL);
#else
        pEv->pEventArray[pData->nIndex].revents = 0;
        pEv->pEventArray[pData->nIndex].events = 0;
        pEv->pEventArray[pData->nIndex].fd = XSOCK_INVALID;
        pData->nIndex = -1;
#endif
        pEv->nEventCount--;
        return XEVENT_STATUS_EINSERT;
    }

    pData->nEvents = nEvents;
    return XEVENT_STATUS_SUCCESS;
}

xevent_data_t* XEvents_RegisterEvent(xevents_t *pEv, void *pCtx, XSOCKET nFd, int nEvents, int nType)
{
    XASSERT((pEv != NULL), NULL);
    XASSERT((nFd != XSOCK_INVALID), NULL);

    xevent_data_t* pData = XEvents_NewData(pCtx, nFd, nType);
    XASSERT((pData != NULL), NULL);

    xevent_status_t nStatus = XEvents_Add(pEv, pData, nEvents);
    XASSERT_CALL((nStatus == XEVENT_STATUS_SUCCESS), free, pData, NULL);

    pData->nEvents = nEvents;
    return pData;
}

int XEvent_Write(xevent_data_t *pData)
{
    const char nVal = 1;
    XASSERT((pData && pData->nFD != XSOCK_INVALID), XEVENT_STATUS_EINVALID);
    return send(pData->nFD, &nVal, sizeof(nVal), XMSG_NOSIGNAL);
}

int XEvent_Recv(xevent_data_t *pData)
{
    char nVal = 0;
    XASSERT((pData && pData->nFD != XSOCK_INVALID), XEVENT_STATUS_EINVALID);
    return recv(pData->nFD, &nVal, sizeof(nVal), XMSG_NOSIGNAL);
}

int XEvent_Read(xevent_data_t *pData)
{
    uint64_t nVal = 0;
    XASSERT((pData && pData->nFD != XSOCK_INVALID), XEVENT_STATUS_EINVALID);
    return (int)read(pData->nFD, &nVal, sizeof(uint64_t));
}

xevent_status_t XEvents_Modify(xevents_t *pEv, xevent_data_t *pData, int nEvents)
{
    XASSERT((pEv && pData), XEVENT_STATUS_EINVALID);
#ifdef __linux__
    struct epoll_event event;
    event.data.ptr = pData;
    event.events = nEvents;
    if (epoll_ctl(pEv->nEventFd, EPOLL_CTL_MOD, pData->nFD, &event) < 0) return XEVENT_STATUS_ECTL;
#else
    if (pData->nIndex < 0 && (uint32_t)pData->nIndex >= pEv->nEventCount) return XEVENT_STATUS_ECTL;
    pEv->pEventArray[pData->nIndex].events = (short)nEvents;
#endif

    pData->nEvents = nEvents;
    return XEVENT_STATUS_SUCCESS;
}

xevent_status_t XEvents_Delete(xevents_t *pEv, xevent_data_t *pData)
{
    XASSERT((pEv != NULL), XEVENT_STATUS_EINVALID);
    XASSERT_RET((pData != NULL), XEVENT_STATUS_SUCCESS);
    XSTATUS nStatus = XSTDERR;

#ifdef __linux__
    if (pData->nFD >= 0)
    {
        nStatus = epoll_ctl(pEv->nEventFd, EPOLL_CTL_DEL, pData->nFD, NULL);
        if (nStatus >= 0 && pEv->nEventCount) pEv->nEventCount--;
    }
#else
    if (pData->nIndex >= 0 && (uint32_t)pData->nIndex < pEv->nEventCount)
    {
        pEv->pEventArray[pData->nIndex].revents = XSTDNON;
        pEv->pEventArray[pData->nIndex].events = XSTDNON;
        pEv->pEventArray[pData->nIndex].fd = XSOCK_INVALID;
        int i = nStatus = XSTDNON;

        for (i = pData->nIndex; (uint32_t)i < pEv->nEventCount; i++)
            pEv->pEventArray[i] = pEv->pEventArray[i + 1];

        pEv->nEventCount--;
        pData->nIndex = -1;
    }
#endif

    if (pData->pTimerEvent != NULL)
    {
        XEvents_Delete(pEv, (xevent_data_t*)pData->pTimerEvent);
        pData->pTimerEvent = NULL;
    }

    if (!pEv->bUseHash || pData->nFD == XSOCK_INVALID ||
        XHash_Delete(&pEv->hashMap, (int)pData->nFD) < 0)
        XEvents_ClearCb(pEv, pData, (int)pData->nFD);

    return (nStatus < 0) ? XEVENT_STATUS_ECTL : XEVENT_STATUS_SUCCESS;
}

xevent_data_t* XEvents_GetData(xevents_t *pEv, XSOCKET nFD)
{
    XASSERT_RET((pEv && pEv->bUseHash && nFD != XSOCK_INVALID), NULL);
    return (xevent_data_t*)XHash_GetData(&pEv->hashMap, (int)nFD);
}

xevent_status_t XEvents_Service(xevents_t *pEv, int nTimeoutMs)
{
    XASSERT(pEv, XEVENT_STATUS_EINVALID);
    int i = 0, nCount = 0, nRet = 0;

#ifdef __linux__
    nCount = epoll_wait(pEv->nEventFd, pEv->pEventArray, pEv->nEventMax, nTimeoutMs);
#elif _WIN32
    nCount = WSAPoll(pEv->pEventArray, pEv->nEventCount, nTimeoutMs);
#else
    nCount = poll(pEv->pEventArray, pEv->nEventCount, nTimeoutMs);
#endif

    if (errno == EINTR) return XEvents_InterruptCb(pEv);
    else if (nCount < 0) return XEVENT_STATUS_EWAIT;
    else if (!nCount) return XEVENT_STATUS_SUCCESS;

#ifdef __linux__
    for (i = 0; i < nCount; i++)
    {
        if (pEv->pEventArray[i].data.ptr == NULL) continue;

        xevent_data_t *pData = (xevent_data_t*)pEv->pEventArray[i].data.ptr;
        uint32_t nEvents = pEv->pEventArray[i].events;

        nRet = XEvents_ServiceCb(pEv, pData, pData->nFD, nEvents);
        if (nRet != XEVENTS_CONTINUE) break;
    }
#else
    for (i = 0; i < (int)pEv->nEventCount; i++)
    {
        if (pEv->pEventArray[i].revents <= 0) continue;
        else if (pEv->pEventArray[i].fd == XSOCK_INVALID) break;

        int nEvents = pEv->pEventArray[i].revents;
        XSOCKET nFD = pEv->pEventArray[i].fd;
        pEv->pEventArray[i].revents = 0;

        xevent_data_t *pData = XEvents_GetData(pEv, nFD);
        nRet = XEvents_ServiceCb(pEv, pData, nFD, nEvents);
        if (nRet != XEVENTS_CONTINUE) break;
    }
#endif

    return nRet == XEVENTS_BREAK ?
            XEVENT_STATUS_BREAK :
            XEVENT_STATUS_SUCCESS;
}
