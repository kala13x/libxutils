/*!
 *  @file libxutils/src/net/event.h
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of the cross-platform 
 * async event engine based on POLL and EPOLL.
 */

#ifndef __XUTILS_EVENT_H__
#define __XUTILS_EVENT_H__

#define XEVENT_TYPE_USER        0
#define XEVENT_TYPE_CLIENT      1
#define XEVENT_TYPE_LISTENER    2

#define XEVENT_ERROR            3
#define XEVENT_READ             4
#define XEVENT_WRITE            5
#define XEVENT_HUNGED           6
#define XEVENT_CLOSED           7
#define XEVENT_CLEAR            8
#define XEVENT_DESTROY          9
#define XEVENT_INTERRUPT        10
#define XEVENT_EXCEPTION        11
#define XEVENT_USER             12

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _XUTILS_USE_GNU
#define _GNU_SOURCE
#endif

#include "xstd.h"
#include "sock.h"
#include "hash.h"

#ifdef __linux__
#include <sys/eventfd.h>
#include <sys/epoll.h>
#elif !defined(_WIN32)
#include <sys/poll.h>
#endif

#ifdef __linux__
#define XPOLLRDHUP  EPOLLRDHUP
#define XPOLLHUP    EPOLLHUP
#define XPOLLERR    EPOLLERR
#define XPOLLPRI    EPOLLPRI
#define XPOLLOUT    EPOLLOUT
#define XPOLLIN     EPOLLIN
#else
#ifdef POLLRDHUP
#define XPOLLRDHUP  POLLRDHUP
#endif
#define XPOLLHUP    POLLHUP
#define XPOLLERR    POLLERR
#define XPOLLPRI    POLLPRI
#define XPOLLOUT    POLLOUT
#define XPOLLIN     POLLIN
#endif

#define XEVENTS_DISCONNECT  -1
#define XEVENTS_CONTINUE    0
#define XEVENTS_RELOOP      1
#define XEVENTS_ACCEPT      2
#define XEVENTS_USERCB      3
#define XEVENTS_BREAK       4

#ifdef __linux__
#define XEVENTS_ACTION      XEVENTS_CONTINUE
#else
#define XEVENTS_ACTION      XEVENTS_RELOOP
#endif

typedef enum {
    XEVENT_STATUS_NONE = 0,
    XEVENT_STATUS_ECTL,
    XEVENT_STATUS_EMAX,
    XEVENT_STATUS_ENOCB,
    XEVENT_STATUS_EOMAX,
    XEVENT_STATUS_EWAIT,
    XEVENT_STATUS_EINTR,
    XEVENT_STATUS_EALLOC,
    XEVENT_STATUS_ECREATE,
    XEVENT_STATUS_EINSERT,
    XEVENT_STATUS_EINVALID,
    XEVENT_STATUS_SUCCESS,
    XEVENT_STATUS_BREAK
} xevent_status_t;

typedef struct XEventData {
    void *pContext;
    uint32_t nEvents;
    xbool_t bIsOpen;
    XSOCKET nFD;
    int nIndex;
    int nType;
} xevent_data_t;

typedef int(*xevent_cb_t)(void *events, void* data, XSOCKET fd, int reason);

typedef struct XEvents {
#ifdef __linux__
    struct epoll_event*     pEventArray;        /* EPOLL event array */
    int                     nEventFd;           /* EPOLL file decriptor */
#else
    struct pollfd*          pEventArray;        /* POLL event array */
    uint32_t                nEventCount;        /* Number of event fds in array */
#endif

    xevent_cb_t             eventCallback;      /* Service callback */
    void*                   pUserSpace;         /* User space pointer */
    uint32_t                nEventMax;          /* Max allowed file descriptors */

    xbool_t                 bUseHash;           /* Flag to enable/disable hash map usage*/
    xhash_t                 hashMap;            /* Hash map for events and related data */
} xevents_t;

const char *XEvents_GetStatusStr(xevent_status_t status);

xevent_status_t XEvents_Create(xevents_t *pEv, uint32_t nMax, void *pUser, xevent_cb_t callBack, xbool_t bUseHash);
void XEvents_Destroy(xevents_t *pEvents);

int XEvent_Write(xevent_data_t *pData);
int XEvent_Read(xevent_data_t *pData);

xevent_data_t *XEvents_NewData(void *pCtx, XSOCKET nFd, int nType);
xevent_data_t* XEvents_GetData(xevents_t *pEv, XSOCKET nFd);

#ifdef __linux__
xevent_data_t* XEvents_CreateEvent(xevents_t *pEv, void *pCtx);
#endif
xevent_data_t* XEvents_RegisterEvent(xevents_t *pEv, void *pCtx, XSOCKET nFd, int nEvents, int nType);

xevent_status_t XEvents_Add(xevents_t *pEv, xevent_data_t *pData, int nEvents);
xevent_status_t XEvents_Modify(xevents_t *pEv, xevent_data_t *pData, int nEvents);
xevent_status_t XEvents_Delete(xevents_t *pEv, xevent_data_t *pData);
xevent_status_t XEvents_Service(xevents_t *pEv, int nTimeoutMs);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_EVENT_H__ */