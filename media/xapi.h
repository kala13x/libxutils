/*!
 *  @file libxutils/media/xapi.h
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
#include "sock.h"
#include "http.h"
#include "event.h"

#define XAPI_EVENT_PEER         0
#define XAPI_EVENT_LISTENER     1

typedef enum
{
    XAPI_CB_ERROR = 0,
    XAPI_CB_STATUS,
    XAPI_CB_INTERRUPT,
    XAPI_CB_COMPLETE,
    XAPI_CB_ACCEPTED,
    XAPI_CB_REQUEST,
    XAPI_CB_STARTED,
    XAPI_CB_CLOSED,
    XAPI_CB_WRITE,
    XAPI_CB_USER
} xapi_cb_type_t;

typedef enum
{
    XAPI_ST_EVENT = 0,
    XAPI_ST_HTTP,
    XAPI_ST_SOCK,
    XAPI_ST_API
} xapi_st_type_t;

typedef enum
{
    XAPI_NONE = (uint8_t)0,
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
    XPKT_INVALID = 0,
    XPKT_MDTP,
    XPKT_HTTP
} xpacket_type_t;

typedef struct XAPI xapi_t;

typedef struct XAPIData
{
    char sIPAddr[XSOCK_ADDR_MAX];
    xbool_t bCancel;
    XSOCKET nFD;

    xpacket_type_t ePktType;
    void *pPacket;

    xevent_data_t *pEvData;
    void *pSessionData;
    xapi_t *pApi;
} xapi_data_t;

typedef struct XAPICTX
{
    xapi_cb_type_t eCbType;
    xapi_st_type_t eStType;
    uint8_t nStatus;
    xapi_t *pApi;
} xapi_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef int(*xapi_cb_t)(xapi_ctx_t *pCtx, xapi_data_t *pData);

struct XAPI {
    xsock_t listener;
    xevents_t events;
    xapi_cb_t callback;
    void *pUserCtx;
};

const char* XAPI_GetStatus(xapi_status_t eStatus);
const char* XAPI_GetStatusStr(xapi_ctx_t *pCtx);
xbyte_buffer_t* XAPI_GetTxBuff(xapi_data_t *pData);

int XAPI_SetEvents(xapi_data_t *pData, int nEvents);
int XAPI_SetResponse(xapi_data_t *pApiData, int nCode, xapi_status_t eStatus);
int XAPI_AuthorizeRequest(xapi_data_t *pApiData, const char *pToken, const char *pKey);

int XAPI_StartListener(xapi_t *pApi, const char *pAddr, uint16_t nPort);
xevent_status_t XAPI_Service(xapi_t *pApi, int nTimeoutMs);
void XAPI_Destroy(xapi_t *pApi);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_API_H__ */