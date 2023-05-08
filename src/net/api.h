/*!
 *  @file libxutils/src/net/api.h
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of high performance event based non-blocking REST API listener.
 * The library will use poll(), epoll() or WSAPoll() depending on the operating system.
 */

#ifndef __XUTILS_API_H__
#define __XUTILS_API_H__

#include "xstd.h"
#include "event.h"
#include "sock.h"
#include "http.h"
#include "mdtp.h"
#include "ws.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xapi_ xapi_t;

typedef enum {
    XAPI_CB_ERROR = 0,
    XAPI_CB_STATUS,
    XAPI_CB_INTERRUPT,
    XAPI_CB_COMPLETE,
    XAPI_CB_ACCEPTED,
    XAPI_CB_STARTED,
    XAPI_CB_CLOSED,
    XAPI_CB_WRITE,
    XAPI_CB_READ,
    XAPI_CB_PING,
    XAPI_CB_PONG,
    XAPI_CB_USER,
    XAPI_CB_TIMER
} xapi_cb_type_t;

typedef enum {
    XAPI_UNKNOWN = (uint8_t)0,
    XAPI_MISSING_KEY,
    XAPI_INVALID_KEY,
    XAPI_MISSING_TOKEN,
    XAPI_INVALID_TOKEN,
    XAPI_AUTH_FAILURE,
    XAPI_EASSEMBLE,
    XAPI_EREGISTER,
    XAPI_EALLOC,
    XAPI_DESTROY,
    XAPI_HUNGED,
    XAPI_CLOSED
} xapi_status_t;

typedef enum {
    XAPI_NONE = 0,
    XAPI_EVENT,
    XAPI_HTTP,
    XAPI_MDTP,
    XAPI_SOCK,
    XAPI_WS
} xapi_type_t;

typedef enum {
    XAPI_INACTIVE = (int)0,
    XAPI_SERVER,
    XAPI_CLIENT,
    XAPI_PEER,
} xapi_role_t;

typedef struct XAPIData {
    char sAddr[XSOCK_ADDR_MAX];
    xbool_t bCancel;
    xbool_t bAlloc;
    XSOCKET nFD;

    xevent_data_t *pEvData;
    void *pSessionData;
    void *pPacket;

    xapi_type_t eType;
    xapi_role_t eRole;
    xapi_t *pApi;
} xapi_data_t;

typedef struct xapi_ctx_ {
    xapi_cb_type_t eCbType;
    xapi_type_t eStatType;
    uint8_t nStatus;
    xapi_t *pApi;
} xapi_ctx_t;

typedef int(*xapi_cb_t)(xapi_ctx_t *pCtx, xapi_data_t *pData);

struct xapi_ {
    xevents_t events;
    xbool_t bHaveEvents;
    xapi_cb_t callback;
    void *pUserCtx;
};

const char* XAPI_GetStatus(xapi_status_t eStatus);
const char* XAPI_GetStatusStr(xapi_ctx_t *pCtx);
xbyte_buffer_t* XAPI_GetTxBuff(xapi_data_t *pData);

int XAPI_SetEvents(xapi_data_t *pData, int nEvents);
int XAPI_SetResponse(xapi_data_t *pApiData, int nCode, xapi_status_t eStatus);
int XAPI_AuthorizeRequest(xapi_data_t *pApiData, const char *pToken, const char *pKey);

int XAPI_StartListener(xapi_t *pApi, xapi_type_t eType, const char *pAddr, uint16_t nPort);
int XAPI_Init(xapi_t *pApi, xapi_cb_t callback, void *pUserCtx);
xevent_status_t XAPI_Service(xapi_t *pApi, int nTimeoutMs);
void XAPI_Destroy(xapi_t *pApi);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_API_H__ */