/*!
 *  @file libxutils/media/xapi.c
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of high performance event based non-blocking REST API listener.
 * The library will use poll(), epoll() or WSAPoll() depending on the operating system.
 */

#include "xapi.h"
#include "xbuf.h"
#include "xver.h"

const char* XAPI_GetStatus(xapi_status_t eStatus)
{
    switch (eStatus)
    {
        case XAPI_AUTH_FAILURE:
            return "Authorization failure";
        case XAPI_MISSING_TOKEN:
            return "Missing auth basic header";
        case XAPI_INVALID_TOKEN:
            return "Invalid auth basic header";
        case XAPI_MISSING_KEY:
            return "Missing X-API-KEY header";
        case XAPI_INVALID_KEY:
            return "Invalid X-API-KEY header";
        case XAPI_EREGISTER:
            return "Failed to register event";
        case XAPI_EALLOC:
            return "Memory allocation failure";
        case XAPI_EASSEMBLE:
            return "Failed to initialize response";
        case XAPI_CLOSED:
            return "Closed remote connection";
        case XAPI_HUNGED:
            return "Connection hunged";
        case XAPI_DESTROY:
            return "Service destroyed";
        default:
            break;
    }

    return "Unknown status";
}

const char* XAPI_GetStatusStr(xapi_ctx_t *pCtx)
{
    switch (pCtx->eStType)
    {
        case XAPI_ST_EVENT:
            return XEvents_Status((xevent_status_t)pCtx->nStatus);
        case XAPI_ST_HTTP:
            return XHTTP_GetStatusStr((xhttp_status_t)pCtx->nStatus);
        case XAPI_ST_SOCK:
            return XSock_GetStatusStr((xsock_status_t)pCtx->nStatus);
        case XAPI_ST_API:
            return XAPI_GetStatus((xapi_status_t)pCtx->nStatus);
        default: break;
    }

    return "Unknown status";
}

static xapi_data_t* XAPI_NewData(xapi_t *pApi)
{
    xapi_data_t *pData = (xapi_data_t*)malloc(sizeof(xapi_data_t));
    if (pData == NULL) return NULL;

    pData->sIPAddr[0] = XSTR_NUL;
    pData->pSessionData = NULL;
    pData->ePktType = XAPI_NONE;
    pData->bCancel = XFALSE;
    pData->pPacket = NULL;
    pData->pEvData = NULL;
    pData->pApi = pApi;
    pData->nFD = XSOCK_INVALID;
    return pData;
}

static int XAPI_Callback(xapi_t *pApi, xapi_data_t *pApiData, xapi_cb_type_t eCbType, xapi_st_type_t eStType, uint8_t nStat)
{
    if (pApi == NULL) return XSTDERR;
    else if (pApi->callback == NULL) return XSTDOK;

    xapi_ctx_t ctx;
    ctx.eCbType = eCbType;
    ctx.eStType = eStType;
    ctx.nStatus = nStat;
    ctx.pApi = pApi;

    return pApi->callback(&ctx, pApiData);
}

static int XAPI_ServiceCb(xapi_t *pApi, xapi_data_t *pApiData, xapi_cb_type_t eCbType)
{
    return XAPI_Callback(
        pApi,
        pApiData,
        eCbType,
        XAPI_ST_API,
        XAPI_NONE
    );
}

static int XAPI_ErrorCb(xapi_t *pApi, xapi_data_t *pApiData, xapi_st_type_t eType, uint8_t nStat)
{
    return XAPI_Callback(
        pApi,
        pApiData,
        XAPI_CB_ERROR,
        eType,
        nStat
    );
}

static int XAPI_StatusCb(xapi_t *pApi, xapi_data_t *pApiData, xapi_st_type_t eType, uint8_t nStat)
{
    return XAPI_Callback(
        pApi,
        pApiData,
        XAPI_CB_STATUS,
        eType,
        nStat
    );
}

int XAPI_SetEvents(xapi_data_t *pData, int nEvents)
{
    xevent_data_t *pEvData = pData->pEvData;
    xapi_t *pApi = pData->pApi;

    xevent_status_t eStatus = XEvents_Modify(&pApi->events, pEvData, nEvents);
    if (eStatus != XEVENT_STATUS_SUCCESS)
    {
        XAPI_ErrorCb(pApi, pData, XAPI_ST_EVENT, eStatus);
        return XSTDERR;
    }

    return XSTDOK;
}

xbyte_buffer_t* XAPI_GetTxBuff(xapi_data_t *pData)
{
    if (pData == NULL) return NULL;
    xhttp_t *pHandle = (xhttp_t*)pData->pPacket;
    return pHandle != NULL ? &pHandle->dataRaw : NULL;
}

static int XAPI_ClearEvent(xapi_t *pApi, xevent_data_t *pEvData)
{
    if (pEvData == NULL) return XEVENTS_CONTINUE;

    if (pEvData->nFD >= 0 && pEvData->bIsOpen)
    {
        shutdown(pEvData->nFD, XSHUT_RDWR);
        xclosesock(pEvData->nFD);
        pEvData->bIsOpen = XFALSE;
        pEvData->nFD = XSOCK_INVALID;
    }

    if (pEvData->pContext != NULL)
    {
        xapi_data_t *pApiData = (xapi_data_t*)pEvData->pContext;
        XAPI_ServiceCb(pApi, pApiData, XAPI_CB_CLOSED);

        if (pApiData->pPacket != NULL)
        {
            XHTTP_Clear((xhttp_t*)pApiData->pPacket);
            pApiData->pPacket = NULL;
        }

        free(pApiData);
        pEvData->pContext = NULL;
    }

    return XEVENTS_CONTINUE;
}

int XAPI_SetResponse(xapi_data_t *pApiData, int nCode, xapi_status_t eStatus)
{
    xhttp_t *pHandle = (xhttp_t*)pApiData->pPacket;
    xapi_t *pApi = pApiData->pApi;
    char sContent[XSTR_MIN];

    XHTTP_Recycle(pHandle, XFALSE);
    pHandle->nStatusCode = nCode;
    pHandle->eType = XHTTP_RESPONSE;

    size_t nLength = xstrncpyf(sContent, sizeof(sContent), "{\"status\": \"%s\"}",
            eStatus != XAPI_NONE ? XAPI_GetStatus(eStatus) : XHTTP_GetCodeStr(nCode));

    if ((eStatus == XAPI_MISSING_TOKEN &&
        XHTTP_AddHeader(pHandle, "WWW-Authenticate", "Basic realm=\"XAPI\"") < 0) ||
        XHTTP_AddHeader(pHandle, "Server", "xutils/%s", XUtils_VersionShort()) < 0 ||
        XHTTP_AddHeader(pHandle, "Content-Type", "application/json") < 0 ||
        XHTTP_Assemble(pHandle, (const uint8_t*)sContent, nLength) == NULL)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_ST_API, XAPI_EASSEMBLE);
        pApiData->bCancel = XTRUE;
        return XSTDERR;
    }

    if (eStatus > XAPI_NONE && eStatus <= XAPI_EALLOC)
        XAPI_ErrorCb(pApi, pApiData, XAPI_ST_API, eStatus);
    else if (eStatus != XAPI_NONE)
        XAPI_StatusCb(pApi, pApiData, XAPI_ST_API, eStatus);

    return XAPI_SetEvents(pApiData, XPOLLOUT) < 0 ? XSTDERR : XSTDNON;
}

int XAPI_AuthorizeRequest(xapi_data_t *pApiData, const char *pToken, const char *pKey)
{
    if (pApiData == NULL) return XSTDERR;
    xhttp_t *pHandle = (xhttp_t*)pApiData->pPacket;

    size_t nTokenLength = xstrused(pToken) ? strlen(pToken) : XSTDNON;
    size_t nKeyLength = xstrused(pKey) ? strlen(pKey) : XSTDNON;
    if (!nTokenLength && !nKeyLength) return XSTDOK;

    if (nKeyLength)
    {
        const char *pXKey = XHTTP_GetHeader(pHandle, "X-API-KEY");
        if (!xstrused(pXKey)) return XAPI_SetResponse(pApiData, 401, XAPI_MISSING_KEY);;
        if (strncmp(pXKey, pKey, nKeyLength)) return XAPI_SetResponse(pApiData, 401, XAPI_INVALID_KEY);
    }

    if (nTokenLength)
    {
        const char *pAuth = XHTTP_GetHeader(pHandle, "Authorization");
        if (!xstrused(pAuth)) return XAPI_SetResponse(pApiData, 401, XAPI_MISSING_TOKEN);

        int nPosit = xstrsrc(pAuth, "Basic");
        if (nPosit < 0) return XAPI_SetResponse(pApiData, 401, XAPI_MISSING_TOKEN);

        if (strncmp(&pAuth[nPosit + 6], pToken, nTokenLength))
            return XAPI_SetResponse(pApiData, 401, XAPI_INVALID_TOKEN);
    }

    return XSTDOK;
}

static int XAPI_ReadEvent(xevents_t *pEvents, xevent_data_t *pEvData)
{
    if (pEvents == NULL || pEvData == NULL) return XEVENTS_DISCONNECT;
    xapi_t *pApi = (xapi_t*)pEvents->pUserSpace;
    xsock_t *pListener = (xsock_t*)&pApi->listener;
    xsock_t clientSock;

    if (pListener->nFD == pEvData->nFD &&
        pEvData->nType == XAPI_EVENT_LISTENER)
    {
        if (XSock_Accept(pListener, &clientSock) == XSOCK_INVALID || 
            XSock_NonBlock(&clientSock, XTRUE) == XSOCK_INVALID)
        {
            XAPI_ErrorCb(pApi, NULL, XAPI_ST_SOCK, clientSock.eStatus);
            return XEVENTS_CONTINUE;
        }

        xapi_data_t *pApiData = XAPI_NewData(pApi);
        if (pApiData == NULL)
        {
            XAPI_ErrorCb(pApi, NULL, XAPI_ST_API, XAPI_EALLOC);
            XSock_Close(&clientSock);
            return XEVENTS_CONTINUE;
        }

        XSock_IPAddr(&clientSock, pApiData->sIPAddr, sizeof(pApiData->sIPAddr));
        pApiData->nFD = clientSock.nFD;

        xhttp_t *pHandle = XHTTP_Alloc(XHTTP_DUMMY, XSTDNON);
        if (pHandle == NULL)
        {
            XAPI_ErrorCb(pApi, pApiData, XAPI_ST_API, XAPI_EALLOC);
            XSock_Close(&clientSock);
            return XEVENTS_CONTINUE;
        }

        pApiData->ePktType = XPKT_HTTP;
        pApiData->pPacket = pHandle;

        xevent_data_t *pEventData = XEvents_RegisterEvent(pEvents, pApiData, pApiData->nFD, XSTDNON, XAPI_EVENT_PEER);
        if (pEventData == NULL)
        {
            XAPI_ErrorCb(pApi, pApiData, XAPI_ST_API, XAPI_EREGISTER);
            XSock_Close(&clientSock);
            XHTTP_Clear(pHandle);
            free(pApiData);
            return XEVENTS_CONTINUE;
        }

        pApiData->pSessionData = NULL;
        pApiData->pEvData = pEventData;

        if (XAPI_ServiceCb(pApi, pApiData, XAPI_CB_ACCEPTED) < 0)
        {
            XEvents_Delete(pEvents, pEventData);
            return XEVENTS_CONTINUE;
        }

        return XEVENTS_ACCEPT;
    }
    else if (pEvData->nType == XAPI_EVENT_PEER)
    {
        xapi_data_t *pApiData = (xapi_data_t*)pEvData->pContext;
        if (pApiData->bCancel) return XEVENTS_DISCONNECT;

        xhttp_t *pHandle = (xhttp_t*)pApiData->pPacket;
        if (pHandle == NULL) return XEVENTS_DISCONNECT;

        XSock_Init(&clientSock, XSOCK_TCP_PEER, pEvData->nFD, XTRUE);
        xhttp_status_t eStatus = XHTTP_Receive(pHandle, &clientSock);

        if (eStatus == XHTTP_COMPLETE)
        {
            int nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_REQUEST);
            if (nStatus < 0) return XEVENTS_DISCONNECT;
            else if (!nStatus) return XEVENTS_CONTINUE;

            XHTTP_Recycle(pHandle, XFALSE);
            pHandle->eType = XHTTP_RESPONSE;
            pApiData->ePktType = XPKT_HTTP;
            pApiData->pPacket = pHandle;

            return (nStatus == XSTDUSR) ?
                    XEVENTS_USERCB :
                    XEVENTS_CONTINUE;
        }
        else if (eStatus == XHTTP_ERRREAD)
        {
            XAPI_ErrorCb(pApi, pApiData, XAPI_ST_SOCK, clientSock.eStatus);
            pEvData->bIsOpen = XFALSE;
            return XEVENTS_DISCONNECT;
        }
        else if (eStatus != XHTTP_PARSED &&
                 eStatus != XHTTP_INCOMPLETE)
        {
            XAPI_ErrorCb(pApi, pApiData, XAPI_ST_HTTP, eStatus);
            return XEVENTS_DISCONNECT;
        }
    }

    return XEVENTS_CONTINUE;
}

static int XAPI_WriteEvent(xevents_t *pEvents, xevent_data_t *pEvData)
{
    if (pEvents == NULL || pEvData == NULL) return XEVENTS_DISCONNECT;
    xapi_data_t *pApiData = (xapi_data_t*)pEvData->pContext;
    if (pApiData->bCancel) return XEVENTS_DISCONNECT;

    xhttp_t *pHandle = (xhttp_t*)pApiData->pPacket;
    xapi_t *pApi = (xapi_t*)pEvents->pUserSpace;
    XSTATUS nStatus = XSTDNON;

    xbyte_buffer_t *pBuffer = &pHandle->dataRaw;
    if (!pBuffer->nUsed)
    {
        nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_WRITE);
        if (!nStatus) return XEVENTS_CONTINUE;
        else if (nStatus < 0) return XEVENTS_DISCONNECT;
        else if (nStatus == XSTDUSR) return XEVENTS_USERCB;
        else if (!pBuffer->nUsed) return XEVENTS_CONTINUE;
    }

    xsock_t socket;
    XSock_Init(&socket, XSOCK_TCP_PEER, pEvData->nFD, XTRUE);

    int nSent = XSock_Write(&socket, pBuffer->pData, pBuffer->nUsed);
    if (nSent <= 0)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_ST_SOCK, socket.eStatus);
        pEvData->bIsOpen = XFALSE;
        return XEVENTS_DISCONNECT;
    }

    if (!XByteBuffer_Advance(pBuffer, nSent))
    {
        nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_COMPLETE);
        if (nStatus < 0) return XEVENTS_DISCONNECT;
        else if (!nStatus) return XEVENTS_CONTINUE;
        XHTTP_Recycle(pHandle, XFALSE);
    }
    
    return nStatus == XSTDUSR ?
            XEVENTS_USERCB :
            XEVENTS_CONTINUE;
}

static int XAPI_InterruptEvent(xapi_t *pApi)
{
    int nStatus = XAPI_ServiceCb(pApi, NULL, XAPI_CB_INTERRUPT);
    if (nStatus == XSTDUSR) return XEVENTS_USERCB;
    else if (nStatus < 0) return XEVENTS_DISCONNECT;
    return XEVENTS_CONTINUE;
}

static int XAPI_UserEvent(xapi_t *pApi)
{
    int nStatus = XAPI_ServiceCb(pApi, NULL, XAPI_CB_USER);
    if (nStatus == XSTDUSR) return XEVENTS_USERCB;
    else if (nStatus < 0) return XEVENTS_DISCONNECT;
    return XEVENTS_CONTINUE;
}

static int XAPI_EventCallback(void *events, void* data, XSOCKET fd, int reason)
{
    xevent_data_t *pData = (xevent_data_t*)data;
    xevents_t *pEvents = (xevents_t*)events;
    xapi_t *pApi = (xapi_t*)pEvents->pUserSpace;

    xapi_data_t *pApiData = (pData != NULL) ?
        (xapi_data_t*)pData->pContext : NULL;

    switch(reason)
    {
        case XEVENT_USER:
            return XAPI_UserEvent(pApi);
        case XEVENT_INTERRUPT:
            return XAPI_InterruptEvent(pApi);
        case XEVENT_CLEAR:
            return XAPI_ClearEvent(pApi, pData);
        case XEVENT_READ:
            return XAPI_ReadEvent(pEvents, pData);
        case XEVENT_WRITE:
            return XAPI_WriteEvent(pEvents, pData);
        case XEVENT_HUNGED:
            XAPI_StatusCb(pApi, pApiData, XAPI_ST_API, XAPI_HUNGED);
            return XEVENTS_DISCONNECT;
        case XEVENT_CLOSED:
            XAPI_StatusCb(pApi, pApiData, XAPI_ST_API, XAPI_CLOSED);
            return XEVENTS_DISCONNECT;
        case XEVENT_DESTROY:
            XAPI_StatusCb(pApi, NULL, XAPI_ST_API, XAPI_DESTROY);
            break;
        default:
            break;
    }

    return XEVENTS_CONTINUE;
}

int XAPI_StartListener(xapi_t *pApi, const char *pAddr, uint16_t nPort)
{
    /* Used variables */
    xevents_t *pEvents = &pApi->events;
    xsock_t *pSock = &pApi->listener;
    xevent_status_t status;

    /* Create server socket */
    XSock_Create(pSock, XSOCK_TCP_SERVER, pAddr, nPort);
    XSock_ReuseAddr(pSock, XTRUE);

    if (pSock->nFD == XSOCK_INVALID)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_ST_SOCK, pSock->eStatus);
        return XSTDERR;
    }

    xapi_data_t *pApiData = XAPI_NewData(pApi);
    if (pApiData == NULL)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_ST_API, XAPI_EALLOC);
        XSock_Close(pSock);
        return XSTDERR;
    }

    xstrncpy(pApiData->sIPAddr, sizeof(pApiData->sIPAddr), pAddr);
    pApiData->nFD = pSock->nFD;

    /* Create event instance */
    status = XEvents_Create(pEvents, XSTDNON, pApi, XAPI_EventCallback, XTRUE);
    if (status != XEVENT_STATUS_SUCCESS)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_ST_EVENT, status);
        XSock_Close(pSock);
        free(pApiData);
        return XSTDERR;
    }

    /* Add listener socket to the event instance */
    xevent_data_t *pEvData = XEvents_RegisterEvent(pEvents, pApiData, pSock->nFD, XPOLLIN, XAPI_EVENT_LISTENER);
    if (pEvData == NULL)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_ST_API, XAPI_EREGISTER);
        XEvents_Destroy(pEvents);
        XSock_Close(pSock);
        free(pApiData);
        return XSTDERR;
    }

    pApiData->pSessionData = NULL;
    pApiData->pEvData = pEvData;

    if (XAPI_ServiceCb(pApi, pApiData, XAPI_CB_STARTED) < 0)
    {
        XEvents_Delete(pEvents, pEvData);
        return XSTDERR;
    }

    return XSTDOK;
}

xevent_status_t XAPI_Service(xapi_t *pApi, int nTimeoutMs)
{
    xevents_t *pEvents = &pApi->events;
    return XEvents_Service(pEvents, nTimeoutMs);
}

void XAPI_Destroy(xapi_t *pApi)
{
    xevents_t *pEvents = &pApi->events;
    XEvents_Destroy(pEvents);
}