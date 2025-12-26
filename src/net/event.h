/*!
 *  @file libxutils/src/net/event.h
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of the cross-platform 
 * async event engine based on POLL and EPOLL.
 */

#ifndef __XUTILS_EVENT_H__
#define __XUTILS_EVENT_H__

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
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#elif !defined(_WIN32)
#include <sys/poll.h>
#endif

// Event flags compatibility macros
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
#define XPOLLIO     (XPOLLIN | XPOLLOUT)

// Event service return codes
#define XEVENTS_DISCONNECT      -1
#define XEVENTS_CONTINUE        0
#define XEVENTS_RELOOP          1
#define XEVENTS_ACCEPT          2
#define XEVENTS_USERCALL        3
#define XEVENTS_BREAK           4

#ifdef __linux__
#define XEVENTS_ACTION          XEVENTS_CONTINUE
#else
#define XEVENTS_ACTION          XEVENTS_RELOOP
#endif

// Event status codes
typedef enum {
    XEVENT_STATUS_NONE = (int)0,
    XEVENT_STATUS_ECTL,
    XEVENT_STATUS_EMAX,
    XEVENT_STATUS_ENOCB,
    XEVENT_STATUS_EOMAX,
    XEVENT_STATUS_EWAIT,
    XEVENT_STATUS_EINTR,
    XEVENT_STATUS_EALLOC,
    XEVENT_STATUS_ETIMER,
    XEVENT_STATUS_EEXTEND,
    XEVENT_STATUS_ECREATE,
    XEVENT_STATUS_EINSERT,
    XEVENT_STATUS_EINVALID,
    XEVENT_STATUS_SUCCESS,
    XEVENT_STATUS_BREAK
} xevent_status_t;

// Event callback types
typedef enum {
    XEVENT_CB_NONE = (int)0,
    XEVENT_CB_ERROR,
    XEVENT_CB_READ,
    XEVENT_CB_WRITE,
    XEVENT_CB_HUNGED,
    XEVENT_CB_CLOSED,
    XEVENT_CB_CLEAR,
    XEVENT_CB_DESTROY,
    XEVENT_CB_INTERRUPT,
    XEVENT_CB_EXCEPTION,
    XEVENT_CB_TIMEOUT,
    XEVENT_CB_USER
} xevent_cb_type_t;

// Event data types
typedef enum {
    XEVENT_TYPE_NONE = (int)0,
    XEVENT_TYPE_SERVER,
    XEVENT_TYPE_CLIENT,
    XEVENT_TYPE_PEER,
    XEVENT_TYPE_EVENT,
    XEVENT_TYPE_TIMER,
    XEVENT_TYPE_CUSTOM
} xevent_type_t;

// Event data structure
typedef struct XEventData {
    void *pContext;
    uint32_t nEvents;
    xbool_t bIsOpen;
    XSOCKET nFD;
    int nIndex;
    int nType;
} xevent_data_t;

typedef int(*xevent_cb_t)(void *events, void* data, XSOCKET fd, xevent_cb_type_t reason);

typedef struct XEvents {
#ifdef __linux__
    struct epoll_event*     pEventArray;        /* EPOLL event array */
    int                     nEventFd;           /* EPOLL file decriptor */
#else
    struct pollfd*          pEventArray;        /* POLL event array */
#endif

    xevent_cb_t             eventCallback;      /* Service callback */
    void*                   pUserSpace;         /* User space pointer */
    uint32_t                nEventCount;        /* Number of event fds in array */
    uint32_t                nEventMax;          /* Max allowed file descriptors */

    xbool_t                 bUseHash;           /* Flag to enable/disable hash map usage*/
    xhash_t                 hashMap;            /* Hash map for events and related data */
} xevents_t;

const char *XEvents_GetStatusStr(xevent_status_t status);

xevent_status_t XEvents_Create(xevents_t *pEv, uint32_t nMax, void *pUser, xevent_cb_t callBack, xbool_t bUseHash);
void XEvents_Destroy(xevents_t *pEvents);

int XEvent_WriteByte(xevent_data_t *pData, const char cVal);
int XEvent_ReadByte(xevent_data_t *pData, char *pVal);
int XEvent_ReadU64(xevent_data_t *pData, uint64_t *pVal);

xevent_data_t *XEvents_NewData(void *pCtx, XSOCKET nFd, int nType);
xevent_data_t* XEvents_GetData(xevents_t *pEv, XSOCKET nFd);

xevent_data_t* XEvents_CreateEvent(xevents_t *pEvents, void *pCtx);
xevent_data_t* XEvents_AddTimer(xevents_t *pEvents, void *pContext, int nTimeoutMs);
xevent_status_t XEvent_ExtendTimer(xevent_data_t *pTimer, int nTimeoutMs);

xevent_data_t* XEvents_RegisterEvent(xevents_t *pEv, void *pCtx, XSOCKET nFd, int nEvents, int nType);
xevent_status_t XEvents_Add(xevents_t *pEv, xevent_data_t *pData, int nEvents);
xevent_status_t XEvents_Modify(xevents_t *pEv, xevent_data_t *pData, int nEvents);
xevent_status_t XEvents_Delete(xevents_t *pEv, xevent_data_t *pData);
xevent_status_t XEvents_Service(xevents_t *pEv, int nTimeoutMs);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_EVENT_H__ */