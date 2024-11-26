/*!
 *  @file libxutils/src/net/api.h
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of high performance, event based,
 * non-blocking client/server API for the HTTP, WebSocket
 * and raw TCP connections. The library will use epoll(),
 * poll() or WSAPoll() depending on the operating system.
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

#define XAPI_DISCONNECT XSTDERR
#define XAPI_CONTINUE   XSTDOK
#define XAPI_USER_CB    XSTDUSR
#define XAPI_NO_ACTION  XSTDNON

typedef enum {
    XAPI_CB_ERROR = 0,
    XAPI_CB_STATUS,
    XAPI_CB_HANDSHAKE_REQUEST,
    XAPI_CB_HANDSHAKE_RESPONSE,
    XAPI_CB_HANDSHAKE_ANSWER,
    XAPI_CB_CONNECTED,
    XAPI_CB_LISTENING,
    XAPI_CB_INTERRUPT,
    XAPI_CB_COMPLETE,
    XAPI_CB_ACCEPTED,
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
    XAPI_INVALID_ARGS,
    XAPI_INVALID_ROLE,
    XAPI_INVALID_TOKEN,
    XAPI_MISSING_TOKEN,
    XAPI_AUTH_FAILURE,
    XAPI_ERR_ASSEMBLE,
    XAPI_ERR_REGISTER,
    XAPI_ERR_RESOLVE,
    XAPI_ERR_CRYPT,
    XAPI_ERR_ALLOC,
    XAPI_STATUS,
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
    XAPI_MANUAL,
    XAPI_SERVER,
    XAPI_CLIENT,
    XAPI_PEER,
} xapi_role_t;

typedef struct xapi_endpoint_ {
    xapi_type_t eType;
    xsock_cert_t certs;
    void *pSessionData;
    const char *pAddr;
    const char *pUri;
    uint32_t nEvents;
    uint16_t nPort;
    xbool_t bForce;
    xbool_t bUnix;
    xbool_t bTLS;
} xapi_endpoint_t;

typedef struct xapi_data_ {
    char sAddr[XSOCK_ADDR_MAX];
    char sKey[XSOCK_ADDR_MAX];
    char sUri[XHTTP_URL_MAX];

    uint16_t nPort;
    xsock_t sock;
    int nEvents;

    xbool_t bCancel;
    xbool_t bAlloc;

    /* Non-blocking SSL routine */
    xbool_t bReadOnWrite;
    xbool_t bWriteOnRead;

    /* WebSocket handshake routine */
    xbool_t bHandshakeStart;
    xbool_t bHandshakeDone;

    xbyte_buffer_t rxBuffer;
    xbyte_buffer_t txBuffer;
    xevent_data_t *pEvData;

    xapi_type_t eType;
    xapi_role_t eRole;
    xapi_t *pApi;

    void *pSessionData;
    void *pPacket;
} xapi_data_t;

typedef struct xapi_ctx_ {
    xapi_cb_type_t eCbType;
    xapi_type_t eStatType;
    uint8_t nStatus;
    xapi_t *pApi;
} xapi_ctx_t;

typedef int(*xapi_cb_t)(xapi_ctx_t *pCtx, xapi_data_t *pData);

struct xapi_ {
    xapi_cb_t callback;
    xevents_t events;
    size_t nRxSize;
    void *pUserCtx;

    xbool_t bAllowMissingKey;
    xbool_t bHaveEvents;
    xbool_t bUseHashMap;
};

const char* XAPI_GetStatus(xapi_ctx_t *pCtx);
const char* XAPI_GetStatusStr(xapi_status_t eStatus);

xbyte_buffer_t* XAPI_GetTxBuff(xapi_data_t *pApiData);
xbyte_buffer_t* XAPI_GetRxBuff(xapi_data_t *pApiData);
XSTATUS XAPI_PutTxBuff(xapi_data_t *pApiData, xbyte_buffer_t *pBuffer);

XSTATUS XAPI_Init(xapi_t *pApi, xapi_cb_t callback, void *pUserCtx, size_t nRxSize);
void XAPI_Destroy(xapi_t *pApi);

XSTATUS XAPI_DisableEvent(xapi_data_t *pData, int nEvent);
XSTATUS XAPI_EnableEvent(xapi_data_t *pData, int nEvent);
XSTATUS XAPI_SetEvents(xapi_data_t *pData, int nEvents);
size_t XAPI_GetEventCount(xapi_t *pApi);

XSTATUS XAPI_RespondHTTP(xapi_data_t *pApiData, int nCode, xapi_status_t eStatus);
XSTATUS XAPI_AuthorizeHTTP(xapi_data_t *pApiData, const char *pToken, const char *pKey);

void XAPI_InitEndpoint(xapi_endpoint_t *pEndpt);
XSTATUS XAPI_Listen(xapi_t *pApi, xapi_endpoint_t *pEndpt);
XSTATUS XAPI_Connect(xapi_t *pApi, xapi_endpoint_t *pEndpt);

xevent_status_t XAPI_Service(xapi_t *pApi, int nTimeoutMs);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_API_H__ */