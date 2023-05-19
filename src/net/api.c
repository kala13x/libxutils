/*!
 *  @file libxutils/src/net/api.c
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of high performance, event based,
 * non-blocking client/server API for the HTTP, WebSocket
 * and raw TCP connections. The library will use epoll(),
 * poll() or WSAPoll() depending on the operating system.
 */

#include "api.h"
#include "xver.h"
#include "xbuf.h"
#include "sha1.h"
#include "base64.h"

#define XAPI_RX_MAX     (5000 * 1024)
#define XAPI_RX_SIZE    4096

const char* XAPI_GetStatusStr(xapi_status_t eStatus)
{
    switch (eStatus)
    {
        case XAPI_AUTH_FAILURE:
            return "Authorization failure";
        case XAPI_MISSING_TOKEN:
            return "Missing auth basic header";
        case XAPI_INVALID_TOKEN:
            return "Invalid auth basic header";
        case XAPI_INVALID_ARGS:
            return "Function called with invalid arguments";
        case XAPI_INVALID_ROLE:
            return "Invalid or unsupported API role";
        case XAPI_MISSING_KEY:
            return "Missing X-API-KEY header";
        case XAPI_INVALID_KEY:
            return "Invalid X-API-KEY header";
        case XAPI_ERR_REGISTER:
            return "Failed to register event";
        case XAPI_ERR_ALLOC:
            return "Memory allocation failure";
        case XAPI_ERR_ASSEMBLE:
            return "Failed to initialize response";
        case XAPI_ERR_CRYPT:
            return "Failed to crypt WS Sec-Key";
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

const char* XAPI_GetStatus(xapi_ctx_t *pCtx)
{
    XASSERT(pCtx, "Invalid API context");

    switch (pCtx->eStatType)
    {
        case XAPI_NONE:
            return XAPI_GetStatusStr((xapi_status_t)pCtx->nStatus);
        case XAPI_EVENT:
            return XEvents_GetStatusStr((xevent_status_t)pCtx->nStatus);
        case XAPI_MDTP:
            return XPacket_GetStatusStr((xpacket_status_t)pCtx->nStatus);
        case XAPI_HTTP:
            return XHTTP_GetStatusStr((xhttp_status_t)pCtx->nStatus);
        case XAPI_SOCK:
            return XSock_GetStatusStr((xsock_status_t)pCtx->nStatus);
        case XAPI_WS:
            return XWebSock_GetStatusStr((xws_status_t)pCtx->nStatus);
        default: break;
    }

    return "Unknown status";
}

static xapi_data_t* XAPI_NewData(xapi_t *pApi, xapi_type_t eType)
{
    xapi_data_t *pData = (xapi_data_t*)malloc(sizeof(xapi_data_t));
    XASSERT((pData != NULL), NULL);

    XSock_Init(&pData->sock, XSOCK_UNDEFINED, XSOCK_INVALID, XFALSE);
    XByteBuffer_Init(&pData->rxBuffer, XSTDNON, XFALSE);
    XByteBuffer_Init(&pData->txBuffer, XSTDNON, XFALSE);

    pData->sAddr[0] = XSTR_NUL;
    pData->sKey[0] = XSTR_NUL;
    pData->sUri[0] = XSTR_NUL;
    pData->nPort = XSTDNON;

    pData->bHandshakeStart = XFALSE;
    pData->bHandshakeDone = XFALSE;
    pData->bCancel = XFALSE;
    pData->bAlloc = XTRUE;

    pData->pEvData = NULL;
    pData->eType = eType;
    pData->pApi = pApi;

    pData->pSessionData = NULL;
    pData->pPacket = NULL;

    return pData;
}

static void XAPI_ClearData(xapi_data_t *pData)
{
    XASSERT_VOID_RET(pData);
    XSock_Close(&pData->sock);
    XByteBuffer_Clear(&pData->rxBuffer);
    XByteBuffer_Clear(&pData->txBuffer);
}

static void XAPI_FreeData(xapi_data_t **pData)
{
    XASSERT_VOID((pData && *pData));
    xapi_data_t *pApiData = *pData;

    XAPI_ClearData(pApiData);
    if (pApiData->bAlloc)
    {
        free(pApiData);
        *pData = NULL;
    }
}

static int XAPI_Callback(xapi_t *pApi, xapi_data_t *pApiData, xapi_cb_type_t eCbType, xapi_type_t eType, uint8_t nStat)
{
    XASSERT((pApi != NULL), XSTDERR);
    XASSERT_RET(pApi->callback, XSTDOK);

    xapi_ctx_t ctx;
    ctx.eCbType = eCbType;
    ctx.eStatType = eType;
    ctx.nStatus = nStat;
    ctx.pApi = pApi;

    return pApi->callback(&ctx, pApiData);
}

static int XAPI_ServiceCb(xapi_t *pApi, xapi_data_t *pApiData, xapi_cb_type_t eCbType)
{
    return XAPI_Callback(pApi, pApiData,
        eCbType, XAPI_NONE, XAPI_UNKNOWN);
}

static int XAPI_ErrorCb(xapi_t *pApi, xapi_data_t *pApiData, xapi_type_t eType, uint8_t nStat)
{
    return XAPI_Callback(pApi, pApiData,
        XAPI_CB_ERROR, eType, nStat);
}

static int XAPI_StatusCb(xapi_t *pApi, xapi_data_t *pApiData, xapi_type_t eType, uint8_t nStat)
{
    return XAPI_Callback(pApi, pApiData,
        XAPI_CB_STATUS, eType, nStat);
}

static int XAPI_StatusToEvent(xapi_t *pApi, int nStatus)
{
    if (nStatus == XSTDUSR)
    {
        nStatus = XAPI_ServiceCb(pApi, NULL, XAPI_CB_USER);
        return XAPI_StatusToEvent(pApi, nStatus);
    }

    return nStatus < XSTDNON ?
        XEVENTS_DISCONNECT :
        XEVENTS_CONTINUE;
}

static int XAPI_ClearEvent(xapi_t *pApi, xevent_data_t *pEvData)
{
    XASSERT_RET(pEvData, XEVENTS_CONTINUE);

    if (pEvData->pContext != NULL)
    {
        xapi_data_t *pApiData = (xapi_data_t*)pEvData->pContext;
        XAPI_ServiceCb(pApi, pApiData, XAPI_CB_CLOSED);
        XAPI_FreeData(&pApiData);
        pEvData->pContext = NULL;
    }

    pEvData->nFD = XSOCK_INVALID;
    return XEVENTS_CONTINUE;
}

XSTATUS XAPI_SetEvents(xapi_data_t *pData, int nEvents)
{
    XASSERT_RET((pData && pData->pEvData), XSTDERR);
    xevent_data_t *pEvData = pData->pEvData;
    xapi_t *pApi = pData->pApi;

    XASSERT((pApi != NULL), XSTDERR);
    XASSERT(pApi->bHaveEvents, XSTDERR);

    xevent_status_t eStatus = XEvents_Modify(&pApi->events, pEvData, nEvents);
    if (eStatus != XEVENT_STATUS_SUCCESS)
    {
        XAPI_ErrorCb(pApi, pData, XAPI_EVENT, eStatus);
        return XSTDERR;
    }

    return XSTDOK;
}

XSTATUS XAPI_EnableEvent(xapi_data_t *pData, int nEvent)
{
    XASSERT_RET((pData && pData->pEvData), XSTDERR);
    xevent_data_t *pEvData = pData->pEvData;

    if (!(pEvData->nEvents & nEvent))
    {
        int nEvents = pEvData->nEvents | nEvent;
        return XAPI_SetEvents(pData, nEvents);
    }

    return XSTDOK;
}

xbyte_buffer_t* XAPI_GetTxBuff(xapi_data_t *pApiData)
{
    XASSERT_RET(pApiData, NULL);
    return &pApiData->txBuffer;
}

XSTATUS XAPI_PutTxBuff(xapi_data_t *pApiData, xbyte_buffer_t *pBuffer)
{
    XASSERT_RET((pApiData && pBuffer), XSTDINV);
    XASSERT_RET(pBuffer->nUsed, pApiData->txBuffer.nUsed);

    XByteBuffer_AddBuff(&pApiData->txBuffer, pBuffer);
    if (!pApiData->txBuffer.nUsed)
    {
        XAPI_ErrorCb(pApiData->pApi, pApiData, XAPI_NONE, XAPI_ERR_ALLOC);
        return XSTDERR;
    }

    return XSTDOK;
}

XSTATUS XAPI_SetWriteable(xapi_data_t *pData)
{
    XASSERT_RET((pData && pData->pEvData), XSTDERR);
    return XAPI_EnableEvent(pData, XPOLLOUT);
}

XSTATUS XAPI_RespondHTTP(xapi_data_t *pApiData, int nCode, xapi_status_t eStatus)
{
    XASSERT((pApiData && pApiData->pApi), XSTDERR);
    xapi_t *pApi = pApiData->pApi;

    xhttp_t handle;
    XHTTP_InitResponse(&handle, nCode, NULL);

    char sContent[XSTR_MIN];
    size_t nLength = xstrncpyf(sContent, sizeof(sContent), "{\"status\": \"%s\"}",
        eStatus != XAPI_UNKNOWN ? XAPI_GetStatusStr(eStatus) : XHTTP_GetCodeStr(nCode));

    if ((eStatus == XAPI_MISSING_TOKEN &&
        XHTTP_AddHeader(&handle, "WWW-Authenticate", "Basic realm=\"XAPI\"") < 0) ||
        XHTTP_AddHeader(&handle, "Server", "xutils/%s", XUtils_VersionShort()) < 0 ||
        XHTTP_AddHeader(&handle, "Content-Type", "application/json") < 0 ||
        XHTTP_Assemble(&handle, (const uint8_t*)sContent, nLength) == NULL)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_NONE, XAPI_ERR_ASSEMBLE);
        XHTTP_Clear(&handle);

        pApiData->bCancel = XTRUE;
        return XEVENTS_DISCONNECT;
    }

    if (eStatus > XAPI_UNKNOWN && eStatus < XAPI_STATUS)
        XAPI_ErrorCb(pApi, pApiData, XAPI_NONE, eStatus);
    else if (eStatus != XAPI_UNKNOWN)
        XAPI_StatusCb(pApi, pApiData, XAPI_NONE, eStatus);

    XAPI_PutTxBuff(pApiData, &handle.rawData);
    XHTTP_Clear(&handle);

    if (!pApiData->txBuffer.nUsed) return XEVENTS_DISCONNECT;
    XSTATUS nStatus = XAPI_SetEvents(pApiData, XPOLLOUT);
    return XAPI_StatusToEvent(pApi, nStatus);
}

XSTATUS XAPI_AuthorizeHTTP(xapi_data_t *pApiData, const char *pToken, const char *pKey)
{
    XASSERT((pApiData && pApiData->pPacket), XSTDERR);
    xhttp_t *pHandle = (xhttp_t*)pApiData->pPacket;

    size_t nTokenLength = xstrused(pToken) ? strlen(pToken) : XSTDNON;
    size_t nKeyLength = xstrused(pKey) ? strlen(pKey) : XSTDNON;
    if (!nTokenLength && !nKeyLength) return XSTDOK;

    if (nKeyLength)
    {
        const char *pXKey = XHTTP_GetHeader(pHandle, "X-API-KEY");
        if (!xstrused(pXKey)) return XAPI_RespondHTTP(pApiData, 401, XAPI_MISSING_KEY);
        if (strncmp(pXKey, pKey, nKeyLength)) return XAPI_RespondHTTP(pApiData, 401, XAPI_INVALID_KEY);
    }

    if (nTokenLength)
    {
        const char *pAuth = XHTTP_GetHeader(pHandle, "Authorization");
        if (!xstrused(pAuth)) return XAPI_RespondHTTP(pApiData, 401, XAPI_MISSING_TOKEN);

        int nPosit = xstrsrc(pAuth, "Basic");
        if (nPosit < 0) return XAPI_RespondHTTP(pApiData, 401, XAPI_MISSING_TOKEN);

        if (strncmp(&pAuth[nPosit + 6], pToken, nTokenLength))
            return XAPI_RespondHTTP(pApiData, 401, XAPI_INVALID_TOKEN);
    }

    return XSTDOK;
}

static int XAPI_HandleMDTP(xapi_t *pApi, xapi_data_t *pApiData)
{
    xbyte_buffer_t *pBuffer = &pApiData->rxBuffer;
    xpacket_status_t eStatus = XPACKET_ERR_NONE;
    int nRetVal = XEVENTS_CONTINUE;

    xpacket_t packet;
    eStatus = XPacket_Parse(&packet, pBuffer->pData, pBuffer->nSize);

    if (eStatus == XPACKET_COMPLETE)
    {
        pApiData->pPacket = &packet;

        int nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_READ);
        nRetVal = XAPI_StatusToEvent(pApi, nStatus);

        size_t nPacketSize = XPacket_GetSize(&packet);
        XByteBuffer_Advance(pBuffer, nPacketSize);
    }
    else if (eStatus != XPACKET_PARSED && eStatus != XPACKET_INCOMPLETE)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_MDTP, eStatus);
        nRetVal = XEVENTS_DISCONNECT;
    }
    else if (eStatus == XPACKET_INCOMPLETE && pBuffer->nUsed > pApi->nRxSize)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_MDTP, XPACKET_BIGDATA);
        nRetVal = XEVENTS_DISCONNECT;
    }

    pApiData->pPacket = NULL;
    XPacket_Clear(&packet);

    if (eStatus == XPACKET_COMPLETE &&
        nRetVal == XEVENTS_CONTINUE &&
        XByteBuffer_HasData(pBuffer))
        return XAPI_HandleMDTP(pApi, pApiData);

    return nRetVal;
}

static int XAPI_HandleHTTP(xapi_t *pApi, xapi_data_t *pApiData)
{
    xbyte_buffer_t *pBuffer = &pApiData->rxBuffer;
    xhttp_status_t eStatus = XHTTP_NONE;
    int nRetVal = XEVENTS_CONTINUE;

    xhttp_t handle;
    XHTTP_Init(&handle, XHTTP_DUMMY, XSTDNON);
    eStatus = XHTTP_ParseBuff(&handle, pBuffer);

    if (eStatus == XHTTP_COMPLETE)
    {
        pApiData->pPacket = &handle;

        int nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_READ);
        nRetVal = XAPI_StatusToEvent(pApi, nStatus);

        size_t nPacketSize = XHTTP_GetPacketSize(&handle);
        XByteBuffer_Advance(pBuffer, nPacketSize);
    }
    else if (eStatus != XHTTP_PARSED && eStatus != XHTTP_INCOMPLETE)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_HTTP, eStatus);
        nRetVal = XEVENTS_DISCONNECT;
    }
    else if (eStatus == XHTTP_INCOMPLETE && pBuffer->nUsed > pApi->nRxSize)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_HTTP, XHTTP_BIGCNT);
        nRetVal = XEVENTS_DISCONNECT;
    }

    pApiData->pPacket = NULL;
    XHTTP_Clear(&handle);

    if (eStatus == XHTTP_COMPLETE &&
        nRetVal == XEVENTS_CONTINUE &&
        XByteBuffer_HasData(pBuffer))
        return XAPI_HandleHTTP(pApi, pApiData);

    return nRetVal;
}

static char* XAPI_GetWSKey(xapi_t *pApi, xapi_data_t *pApiData)
{
    uint8_t uDigest[XSHA1_DIGEST_SIZE];
    char sBuffer[XSTR_MIN];
    size_t nLength;

    nLength = xstrncpyf(sBuffer, sizeof(sBuffer), "%s%s", pApiData->sKey, XWS_GUID);
    XSHA1_Compute(uDigest, sizeof(uDigest), (const uint8_t*)sBuffer, nLength);

    nLength = XSHA1_DIGEST_SIZE;
    char *pSecKey = XBase64_Encrypt(uDigest, &nLength);
    if (pSecKey != NULL) return pSecKey;

    XAPI_ErrorCb(pApi, pApiData, XAPI_NONE, XAPI_ERR_CRYPT);
    pApiData->bCancel = XTRUE;

    return NULL;
}

static int XAPI_AnswerUpgrade(xapi_t *pApi, xapi_data_t *pApiData)
{
    xhttp_t handle;
    XHTTP_InitResponse(&handle, 101, NULL);

    const char *pLibVersion = XUtils_VersionShort();
    char *pSecKey = XAPI_GetWSKey(pApi, pApiData);

    if (pSecKey == NULL)
    {
        XHTTP_Clear(&handle);
        return XEVENTS_DISCONNECT;
    }

    if (XHTTP_AddHeader(&handle, "Upgrade", "websocket") < 0 ||
        XHTTP_AddHeader(&handle, "Connection", "Upgrade") < 0 ||
        XHTTP_AddHeader(&handle, "Sec-WebSocket-Accept", "%s", pSecKey) < 0 ||
        XHTTP_AddHeader(&handle, "Server", "xutils/%s", pLibVersion) < 0 ||
        XHTTP_Assemble(&handle, NULL, XSTDNON) == NULL)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_NONE, XAPI_ERR_ASSEMBLE);
        XHTTP_Clear(&handle);
        free(pSecKey);

        pApiData->bCancel = XTRUE;
        return XEVENTS_DISCONNECT;
    }

    free(pSecKey);
    pApiData->pPacket = &handle;

    int nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_HANDSHAKE_ANSWER);
    int nRetVal = XAPI_StatusToEvent(pApi, nStatus);

    XAPI_PutTxBuff(pApiData, &handle.rawData);
    xbyte_buffer_t *pBuffer = &pApiData->txBuffer;

    XHTTP_Clear(&handle);
    pApiData->pPacket = NULL;

    if (nRetVal != XEVENTS_CONTINUE) return nRetVal;
    else if (!pBuffer->nUsed) return XEVENTS_DISCONNECT;

    nStatus = XAPI_SetEvents(pApiData, XPOLLOUT);
    return XAPI_StatusToEvent(pApi, nStatus);
}

static int XAPI_RequestUpgrade(xapi_t *pApi, xapi_data_t *pApiData)
{
    xhttp_t handle;
    XHTTP_InitRequest(&handle, XHTTP_GET, pApiData->sUri, NULL);

    char sNonce[XWS_NONCE_LENGTH + 1];
    size_t nLength = XWS_NONCE_LENGTH;

    xstrrand(sNonce, sizeof(sNonce), nLength, XTRUE, XTRUE);
    char *pSecKey = XBase64_Encrypt((uint8_t*)sNonce, &nLength);

    if (pSecKey == NULL)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_NONE, XAPI_ERR_CRYPT);
        pApiData->bCancel = XTRUE;

        XHTTP_Clear(&handle);
        return XEVENTS_DISCONNECT;
    }

    const char *pLibVersion = XUtils_VersionShort();
    xstrncpy(pApiData->sKey, sizeof(pApiData->sKey), pSecKey);
    free(pSecKey);

    if (XHTTP_AddHeader(&handle, "Upgrade", "websocket") < 0 ||
        XHTTP_AddHeader(&handle, "Connection", "Upgrade") < 0 ||
        XHTTP_AddHeader(&handle, "Sec-WebSocket-Version", "%d", XWS_SEC_WS_VERSION) < 0 ||
        XHTTP_AddHeader(&handle, "Sec-WebSocket-Key", "%s", pApiData->sKey) < 0 ||
        XHTTP_AddHeader(&handle, "Server", "xutils/%s", pLibVersion) < 0 ||
        XHTTP_Assemble(&handle, NULL, XSTDNON) == NULL)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_NONE, XAPI_ERR_ASSEMBLE);
        XHTTP_Clear(&handle);

        pApiData->bCancel = XTRUE;
        return XEVENTS_DISCONNECT;
    }

    pApiData->bHandshakeStart = XTRUE;
    pApiData->pPacket = &handle;

    int nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_HANDSHAKE_REQUEST);
    int nRetVal = XAPI_StatusToEvent(pApi, nStatus);

    XAPI_PutTxBuff(pApiData, &handle.rawData);
    xbyte_buffer_t *pBuffer = &pApiData->txBuffer;

    XHTTP_Clear(&handle);
    pApiData->pPacket = NULL;

    if (nRetVal != XEVENTS_CONTINUE) return nRetVal;
    else if (!pBuffer->nUsed) return XEVENTS_DISCONNECT;

    nStatus = XAPI_SetEvents(pApiData, XPOLLOUT);
    return XAPI_StatusToEvent(pApi, nStatus);
}

static int XAPI_ServerHandshake(xapi_t *pApi, xapi_data_t *pApiData)
{
    if (pApiData->bHandshakeStart) return XEVENTS_CONTINUE;
    xbyte_buffer_t *pBuffer = &pApiData->rxBuffer;
    xhttp_status_t eStatus = XHTTP_NONE;
    int nRetVal = XEVENTS_CONTINUE;

    xhttp_t handle;
    XHTTP_Init(&handle, XHTTP_DUMMY, XSTDNON);
    eStatus = XHTTP_ParseBuff(&handle, pBuffer);

    if (eStatus == XHTTP_COMPLETE)
    {
        const char *pUpgrade = XHTTP_GetHeader(&handle, "Upgrade");
        const char *pSecKey = XHTTP_GetHeader(&handle, "Sec-WebSocket-Key");

        if (!xstrused(pUpgrade) || strncmp(pUpgrade, "websocket", 9))
        {
            XAPI_ErrorCb(pApi, pApiData, XAPI_WS, XWS_INVALID_REQUEST);
            XHTTP_Clear(&handle);
            return XEVENTS_DISCONNECT;
        }

        if (xstrused(pSecKey))
            xstrncpy(pApiData->sKey, sizeof(pApiData->sKey), pSecKey);

        if (!xstrused(pApiData->sKey) && !pApi->bAllowMissingKey)
        {
            XAPI_ErrorCb(pApi, pApiData, XAPI_WS, XWS_MISSING_SEC_KEY);
            XHTTP_Clear(&handle);
            return XEVENTS_DISCONNECT;
        }

        pApiData->bHandshakeStart = XTRUE;
        pApiData->pPacket = &handle;

        int nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_HANDSHAKE_REQUEST);
        nRetVal = XAPI_StatusToEvent(pApi, nStatus);

        size_t nPacketSize = XHTTP_GetPacketSize(&handle);
        XByteBuffer_Advance(pBuffer, nPacketSize);
    }
    else if (eStatus != XHTTP_PARSED && eStatus != XHTTP_INCOMPLETE)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_HTTP, eStatus);
        nRetVal = XEVENTS_DISCONNECT;
    }
    else if (eStatus == XHTTP_INCOMPLETE && pBuffer->nUsed > pApi->nRxSize)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_HTTP, XHTTP_BIGCNT);
        nRetVal = XEVENTS_DISCONNECT;
    }

    pApiData->pPacket = NULL;
    XHTTP_Clear(&handle);

    if (eStatus == XHTTP_COMPLETE &&
        nRetVal == XEVENTS_CONTINUE)
        nRetVal = XAPI_AnswerUpgrade(pApi, pApiData);

    return nRetVal;
}

static int XAPI_ClientHandshake(xapi_t *pApi, xapi_data_t *pApiData)
{
    if (!pApiData->bHandshakeStart) return XEVENTS_CONTINUE;
    xbyte_buffer_t *pBuffer = &pApiData->rxBuffer;
    xhttp_status_t eStatus = XHTTP_NONE;
    int nRetVal = XEVENTS_CONTINUE;

    xhttp_t handle;
    XHTTP_Init(&handle, XHTTP_DUMMY, XSTDNON);
    eStatus = XHTTP_ParseBuff(&handle, pBuffer);

    if (eStatus == XHTTP_COMPLETE)
    {
        const char *pUpgrade = XHTTP_GetHeader(&handle, "Upgrade");
        const char *pSecKey = XHTTP_GetHeader(&handle, "Sec-WebSocket-Accept");

        if (!xstrused(pUpgrade) || strncmp(pUpgrade, "websocket", 9))
        {
            XAPI_ErrorCb(pApi, pApiData, XAPI_WS, XWS_INVALID_RESPONSE);
            XHTTP_Clear(&handle);
            return XEVENTS_DISCONNECT;
        }

        if (xstrused(pSecKey))
        {
            char *pLocalKey = XAPI_GetWSKey(pApi, pApiData);
            if (pLocalKey == NULL)
            {
                XHTTP_Clear(&handle);
                return XEVENTS_DISCONNECT;
            }

            if (strncmp(pLocalKey, pSecKey, strlen(pLocalKey)))
            {
                XAPI_ErrorCb(pApi, pApiData, XAPI_WS, XWS_INVALID_SEC_KEY);
                XHTTP_Clear(&handle);
                free(pLocalKey);

                pApiData->bCancel = XTRUE;
                return XEVENTS_DISCONNECT;
            }

            free(pLocalKey);
        }
        else if (!pApi->bAllowMissingKey)
        {
            XAPI_ErrorCb(pApi, pApiData, XAPI_WS, XWS_MISSING_SEC_KEY);
            XHTTP_Clear(&handle);

            pApiData->bCancel = XTRUE;
            return XEVENTS_DISCONNECT;
        }

        pApiData->bHandshakeStart = XFALSE;
        pApiData->bHandshakeDone = XTRUE;
        pApiData->pPacket = &handle;

        int nStatus = XAPI_SetWriteable(pApiData);
        if (nStatus != XSTDOK)
        {
            pApiData->pPacket = NULL;
            XHTTP_Clear(&handle);
            return XEVENTS_DISCONNECT;
        }

        nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_HANDSHAKE_RESPONSE);
        nRetVal = XAPI_StatusToEvent(pApi, nStatus);

        size_t nPacketSize = XHTTP_GetPacketSize(&handle);
        XByteBuffer_Advance(pBuffer, nPacketSize);
    }
    else if (eStatus != XHTTP_PARSED && eStatus != XHTTP_INCOMPLETE)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_HTTP, eStatus);
        nRetVal = XEVENTS_DISCONNECT;
    }
    else if (eStatus == XHTTP_INCOMPLETE && pBuffer->nUsed > pApi->nRxSize)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_HTTP, XHTTP_BIGCNT);
        nRetVal = XEVENTS_DISCONNECT;
    }

    pApiData->pPacket = NULL;
    XHTTP_Clear(&handle);

    return nRetVal;
}

static int XAPI_HandleWS(xapi_t *pApi, xapi_data_t *pApiData)
{
    xbyte_buffer_t *pBuffer = &pApiData->rxBuffer;
    xws_status_t eStatus = XWS_ERR_NONE;
    int nRetVal = XEVENTS_CONTINUE;

    if (!pApiData->bHandshakeDone)
    {
        if (pApiData->eRole == XAPI_PEER)
            nRetVal = XAPI_ServerHandshake(pApi, pApiData);
        else if (pApiData->eRole == XAPI_CLIENT)
            nRetVal = XAPI_ClientHandshake(pApi, pApiData);

        XASSERT_RET((nRetVal == XEVENTS_CONTINUE), nRetVal);
        XASSERT_RET((pBuffer->nUsed > 0), XEVENTS_CONTINUE);
    }

    xws_frame_t frame;
    eStatus = XWebFrame_ParseBuff(&frame, pBuffer);

    if (eStatus == XWS_FRAME_COMPLETE)
    {
        pApiData->pPacket = &frame;

        int nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_READ);
        nRetVal = XAPI_StatusToEvent(pApi, nStatus);

        size_t nFrameLength = XWebFrame_GetFrameLength(&frame);
        XByteBuffer_Advance(pBuffer, nFrameLength);
    }
    else if (eStatus != XWS_FRAME_PARSED && eStatus != XWS_FRAME_INCOMPLETE)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_WS, eStatus);
        nRetVal = XEVENTS_DISCONNECT;
    }
    else if (eStatus == XWS_FRAME_INCOMPLETE && pBuffer->nUsed > pApi->nRxSize)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_WS, XPACKET_BIGDATA);
        nRetVal = XEVENTS_DISCONNECT;
    }

    pApiData->pPacket = NULL;
    XWebFrame_Clear(&frame);

    if (eStatus == XWS_FRAME_COMPLETE &&
        nRetVal == XEVENTS_CONTINUE &&
        XByteBuffer_HasData(pBuffer))
        return XAPI_HandleWS(pApi, pApiData);

    return nRetVal;
}

static int XAPI_HandleRAW(xapi_t *pApi, xapi_data_t *pApiData)
{
    xbyte_buffer_t *pBuffer = &pApiData->rxBuffer;
    pApiData->pPacket = (xbyte_buffer_t*)pBuffer;

    int nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_READ);
    int nRetVal = XAPI_StatusToEvent(pApi, nStatus);

    XByteBuffer_Clear(pBuffer);
    pApiData->pPacket = NULL;

    return nRetVal;
}

static int XAPI_Accept(xapi_t *pApi, xapi_data_t *pApiData)
{
    XASSERT((pApi && pApiData), XEVENTS_DISCONNECT);
    XASSERT(pApi->bHaveEvents, XEVENTS_DISCONNECT);

    xapi_data_t *pPeerData = XAPI_NewData(pApi, pApiData->eType);
    if (pPeerData == NULL)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_NONE, XAPI_ERR_ALLOC);
        return XEVENTS_CONTINUE;
    }

    xevents_t *pEvents = &pApi->events;
    xsock_t *pListener = &pApiData->sock;
    xsock_t *pNewSock = &pPeerData->sock;

    if (XSock_Accept(pListener, pNewSock) == XSOCK_INVALID)
    {
        XAPI_ErrorCb(pApi, pPeerData, XAPI_SOCK, pListener->eStatus);
        XAPI_FreeData(&pPeerData);
        return XEVENTS_CONTINUE;
    }

    if (XSock_NonBlock(pNewSock, XTRUE) == XSOCK_INVALID)
    {
        XAPI_ErrorCb(pApi, pPeerData, XAPI_SOCK, pNewSock->eStatus);
        XAPI_FreeData(&pPeerData);
        return XEVENTS_CONTINUE;
    }

    XSock_IPAddr(pNewSock, pPeerData->sAddr, sizeof(pPeerData->sAddr));
    pPeerData->nPort = pApiData->nPort;
    pPeerData->eRole = XAPI_PEER;

    xevent_data_t *pEventData = XEvents_RegisterEvent(pEvents, pPeerData, pNewSock->nFD, XSTDNON, XAPI_PEER);
    if (pEventData == NULL)
    {
        XAPI_ErrorCb(pApi, pPeerData, XAPI_NONE, XAPI_ERR_REGISTER);
        XAPI_FreeData(&pPeerData);
        return XEVENTS_CONTINUE;
    }

    pPeerData->pSessionData = NULL;
    pPeerData->pEvData = pEventData;

    if (XAPI_ServiceCb(pApi, pPeerData, XAPI_CB_ACCEPTED) < 0)
    {
        XEvents_Delete(pEvents, pEventData);
        return XEVENTS_CONTINUE;
    }

    return XEVENTS_ACCEPT;
}

static int XAPI_Read(xapi_t *pApi, xapi_data_t *pApiData)
{
    XASSERT((pApiData != NULL), XEVENTS_DISCONNECT);
    xsock_t *pSock = (xsock_t*)&pApiData->sock;
    uint8_t sBuffer[XAPI_RX_SIZE];

    int nBytes = XSock_Read(pSock, sBuffer, sizeof(sBuffer));
    if (nBytes <= 0)
    {
        if (pSock->eStatus == XSOCK_EOF)
            XAPI_StatusCb(pApi, pApiData, XAPI_SOCK, pSock->eStatus);
        else
            XAPI_ErrorCb(pApi, pApiData, XAPI_SOCK, pSock->eStatus);

        return XEVENTS_DISCONNECT;
    }

    if (XByteBuffer_Add(&pApiData->rxBuffer, sBuffer, nBytes) <= 0)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_NONE, XAPI_ERR_ALLOC);
        return XEVENTS_DISCONNECT;
    }

    switch (pApiData->eType)
    {
        case XAPI_HTTP: return XAPI_HandleHTTP(pApi, pApiData);
        case XAPI_MDTP: return XAPI_HandleMDTP(pApi, pApiData);
        case XAPI_SOCK: return XAPI_HandleRAW(pApi, pApiData);
        case XAPI_WS: return XAPI_HandleWS(pApi, pApiData);
        default: break;
    }

    return XEVENTS_DISCONNECT;
}

static int XAPI_ReadEvent(xevents_t *pEvents, xevent_data_t *pEvData)
{
    XASSERT((pEvents && pEvData), XEVENTS_DISCONNECT);
    xapi_t *pApi = (xapi_t*)pEvents->pUserSpace;
    XASSERT(pApi, XEVENTS_DISCONNECT);

    xapi_data_t *pApiData = (xapi_data_t*)pEvData->pContext;
    XASSERT((pApiData && !pApiData->bCancel), XEVENTS_DISCONNECT);

    if (pApiData->eRole == XAPI_MANUAL)
    {
        int nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_READ);
        return XAPI_StatusToEvent(pApi, nStatus);
    }
    else if (pApiData->eRole == XAPI_SERVER)
    {
        return XAPI_Accept(pApi, pApiData);
    }
    else if (pApiData->eRole == XAPI_PEER ||
             pApiData->eRole == XAPI_CLIENT)
    {
        return XAPI_Read(pApi, pApiData);
    }

    XAPI_ErrorCb(pApi, pApiData, XAPI_NONE, XAPI_INVALID_ROLE);
    return XEVENTS_DISCONNECT;
}

static int XAPI_WriteEvent(xevents_t *pEvents, xevent_data_t *pEvData)
{
    XASSERT((pEvents && pEvData), XEVENTS_DISCONNECT);
    xapi_data_t *pApiData = (xapi_data_t*)pEvData->pContext;
    XASSERT((pApiData && !pApiData->bCancel), XEVENTS_DISCONNECT);

    xapi_t *pApi = (xapi_t*)pEvents->pUserSpace;
    XASSERT((pApi != NULL), XEVENTS_DISCONNECT);

    xbyte_buffer_t *pBuffer = &pApiData->txBuffer;
    xsock_t *pSock = (xsock_t*)&pApiData->sock;
    XSTATUS nStatus = XSTDNON;

    if (pApiData->eRole == XAPI_MANUAL)
    {
        nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_WRITE);
        return XAPI_StatusToEvent(pApi, nStatus);
    }

    if (pApiData->eRole == XAPI_CLIENT &&
        pApiData->eType == XAPI_WS &&
        !pApiData->bHandshakeStart &&
        !pApiData->bHandshakeDone)
    {
        int nRetVal = XAPI_RequestUpgrade(pApi, pApiData);
        XASSERT_RET((nRetVal == XEVENTS_CONTINUE), nRetVal);
        XASSERT((pBuffer->nUsed > 0), XEVENTS_DISCONNECT);
    }

    if (!pBuffer->nUsed)
    {
        nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_WRITE);
        int nRetVal = XAPI_StatusToEvent(pApi, nStatus);

        XASSERT_RET((nRetVal == XEVENTS_CONTINUE), nRetVal);
        XASSERT_RET((pBuffer->nUsed > 0), XEVENTS_CONTINUE);
    }

    int nSent = XSock_Write(pSock, pBuffer->pData, pBuffer->nUsed);
    if (nSent <= 0)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_SOCK, pSock->eStatus);
        return XEVENTS_DISCONNECT;
    }

    if (!XByteBuffer_Advance(pBuffer, nSent))
    {
        nStatus = XAPI_SetEvents(pApiData, XPOLLIN);
        XASSERT((nStatus == XSTDOK), XEVENTS_DISCONNECT);

        if (pApiData->bHandshakeDone || pApiData->eType != XAPI_WS)
            nStatus = XAPI_ServiceCb(pApi, pApiData, XAPI_CB_COMPLETE);

        if (pApiData->eRole != XAPI_CLIENT &&
            pApiData->eType == XAPI_WS &&
            pApiData->bHandshakeStart)
        {
            pApiData->bHandshakeStart = XFALSE;
            pApiData->bHandshakeDone = XTRUE;
        }
    }

    return XAPI_StatusToEvent(pApi, nStatus);
}

static int XAPI_HungedEvent(xapi_t *pApi, xevent_data_t *pData)
{
    xapi_data_t *pApiData = NULL;
    if (pData) pApiData = (xapi_data_t*)pData->pContext;
    XAPI_StatusCb(pApi, pApiData, XAPI_NONE, XAPI_HUNGED);
    return XEVENTS_DISCONNECT;
}

static int XAPI_ClosedEvent(xapi_t *pApi, xevent_data_t *pData)
{
    xapi_data_t *pApiData = NULL;
    if (pData != NULL) pApiData = (xapi_data_t*)pData->pContext;
    XAPI_StatusCb(pApi, pApiData, XAPI_NONE, XAPI_CLOSED);
    return XEVENTS_DISCONNECT;
}

static int XAPI_InterruptEvent(xapi_t *pApi)
{
    int nStatus = XAPI_ServiceCb(pApi, NULL, XAPI_CB_INTERRUPT);
    return XAPI_StatusToEvent(pApi, nStatus);
}

static int XAPI_UserEvent(xapi_t *pApi)
{
    int nStatus = XAPI_ServiceCb(pApi, NULL, XAPI_CB_USER);
    return XAPI_StatusToEvent(pApi, nStatus);
}

static int XAPI_EventCallback(void *events, void* data, XSOCKET fd, int reason)
{
    xevent_data_t *pData = (xevent_data_t*)data;
    xevents_t *pEvents = (xevents_t*)events;
    xapi_t *pApi = (xapi_t*)pEvents->pUserSpace;

    switch(reason)
    {
        case XEVENT_USER:
            return XAPI_UserEvent(pApi);
        case XEVENT_INTERRUPT:
            return XAPI_InterruptEvent(pApi);
        case XEVENT_CLEAR:
            return XAPI_ClearEvent(pApi, pData);
        case XEVENT_HUNGED:
            return XAPI_HungedEvent(pApi, pData);
        case XEVENT_CLOSED:
            return XAPI_ClosedEvent(pApi, pData);
        case XEVENT_READ:
            return XAPI_ReadEvent(pEvents, pData);
        case XEVENT_WRITE:
            return XAPI_WriteEvent(pEvents, pData);
        case XEVENT_DESTROY:
            XAPI_StatusCb(pApi, NULL, XAPI_NONE, XAPI_DESTROY);
            break;
        default:
            break;
    }

    return XEVENTS_CONTINUE;
}

xevents_t* XAPI_GetOrCreateEvents(xapi_t *pApi)
{
    XASSERT((pApi != NULL), NULL);
    xevents_t *pEvents = &pApi->events;
    if (pApi->bHaveEvents) return pEvents;

    xevent_status_t status;
    status = XEvents_Create(pEvents, XSTDNON, pApi,
             XAPI_EventCallback, pApi->bUseHashMap);

    if (status != XEVENT_STATUS_SUCCESS)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_EVENT, status);
        return NULL;
    }

    pApi->bHaveEvents = XTRUE;
    return pEvents;
}

XSTATUS XAPI_Init(xapi_t *pApi, xapi_cb_t callback, void *pUserCtx, size_t nRxSize)
{
    XASSERT((pApi != NULL), XSTDINV);
    pApi->bAllowMissingKey = XFALSE;
    pApi->bHaveEvents = XFALSE;
    pApi->bUseHashMap = XTRUE;
    pApi->callback = callback;
    pApi->pUserCtx = pUserCtx;
    pApi->nRxSize = nRxSize ? nRxSize : XAPI_RX_MAX;
    return XSTDOK;
}

void XAPI_Destroy(xapi_t *pApi)
{
    XASSERT_VOID_RET(pApi->bHaveEvents);
    xevents_t *pEvents = &pApi->events;
    XEvents_Destroy(pEvents);
}

xevent_status_t XAPI_Service(xapi_t *pApi, int nTimeoutMs)
{
    XASSERT((pApi != NULL), XEVENT_STATUS_EINVALID);
    XASSERT(pApi->bHaveEvents, XEVENT_STATUS_EINVALID);

    xevents_t *pEvents = &pApi->events;
    return XEvents_Service(pEvents, nTimeoutMs);
}

void XAPI_InitEndpoint(xapi_endpoint_t *pEndpt)
{
    pEndpt->pSessionData = NULL;
    pEndpt->nEvents = XSTDNON;
    pEndpt->eType = XAPI_NONE;
    pEndpt->nPort = XSTDNON;
    pEndpt->pAddr = NULL;
    pEndpt->pUri = NULL;
    pEndpt->bTLS = XFALSE;
}

XSTATUS XAPI_Listen(xapi_t *pApi, xapi_endpoint_t *pEndpt)
{
    XASSERT((pApi != NULL && pEndpt != NULL), XSTDINV);
    XASSERT((pEndpt->pAddr && pEndpt->nPort), XSTDINV);
    XASSERT((pEndpt->eType != XAPI_NONE), XSTDINV);

    xapi_data_t *pApiData = XAPI_NewData(pApi, pEndpt->eType);
    if (pApiData == NULL)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_NONE, XAPI_ERR_ALLOC);
        return XSTDERR;
    }

    const char *pEndpointUri = pEndpt->pUri ? pEndpt->pUri : "/";
    uint32_t nEvents = pEndpt->nEvents ? pEndpt->nEvents : XPOLLIN;
    xsock_t *pSock = &pApiData->sock;

    xstrncpy(pApiData->sAddr, sizeof(pApiData->sAddr), pEndpt->pAddr);
    xstrncpy(pApiData->sUri, sizeof(pApiData->sUri), pEndpointUri);
    pApiData->pSessionData = pEndpt->pSessionData;
    pApiData->nPort = pEndpt->nPort;
    pApiData->eRole = XAPI_SERVER;

    XSock_Create(pSock, XSOCK_TCP_SERVER, pEndpt->pAddr, pEndpt->nPort);
    XSock_ReuseAddr(pSock, XTRUE);
    XSock_NonBlock(pSock, XTRUE);

    if (pSock->nFD == XSOCK_INVALID)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_SOCK, pSock->eStatus);
        XAPI_FreeData(&pApiData);
        return XSTDERR;
    }

    /* Create event instance */
    xevents_t *pEvents = XAPI_GetOrCreateEvents(pApi);
    if (pEvents == NULL)
    {
        XAPI_FreeData(&pApiData);
        return XSTDERR;
    }

    /* Add listener socket to the event instance */
    pApiData->pEvData = XEvents_RegisterEvent(pEvents, pApiData, pSock->nFD, nEvents, XAPI_SERVER);
    if (pApiData->pEvData == NULL)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_NONE, XAPI_ERR_REGISTER);
        XAPI_FreeData(&pApiData);
        return XSTDERR;
    }

    if (XAPI_ServiceCb(pApi, pApiData, XAPI_CB_LISTENING) < 0)
    {
        XEvents_Delete(pEvents, pApiData->pEvData);
        pApiData->pEvData = NULL;
        return XSTDERR;
    }

    return XSTDOK;
}

XSTATUS XAPI_Connect(xapi_t *pApi, xapi_endpoint_t *pEndpt)
{
    XASSERT((pApi != NULL && pEndpt != NULL), XSTDINV);
    XASSERT((pEndpt->pAddr && pEndpt->nPort), XSTDINV);
    XASSERT((pEndpt->eType != XAPI_NONE), XSTDINV);

    xapi_data_t *pApiData = XAPI_NewData(pApi, pEndpt->eType);
    if (pApiData == NULL)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_NONE, XAPI_ERR_ALLOC);
        return XSTDERR;
    }

    const char *pEndpointUri = pEndpt->pUri ? pEndpt->pUri : "/";
    uint32_t nEvents = pEndpt->nEvents ? pEndpt->nEvents : XPOLLIO;
    xsock_t *pSock = &pApiData->sock;

    xstrncpy(pApiData->sAddr, sizeof(pApiData->sAddr), pEndpt->pAddr);
    xstrncpy(pApiData->sUri, sizeof(pApiData->sUri), pEndpointUri);
    pApiData->pSessionData = pEndpt->pSessionData;
    pApiData->nPort = pEndpt->nPort;
    pApiData->eRole = XAPI_CLIENT;

    XSock_Create(pSock, XSOCK_TCP_CLIENT, pEndpt->pAddr, pEndpt->nPort);
    XSock_NonBlock(pSock, XTRUE);

    if (pApiData->sock.nFD == XSOCK_INVALID)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_SOCK, pSock->eStatus);
        XAPI_FreeData(&pApiData);
        return XSTDERR;
    }

    /* Create event instance */
    xevents_t *pEvents = XAPI_GetOrCreateEvents(pApi);
    if (pEvents == NULL)
    {
        XAPI_FreeData(&pApiData);
        return XSTDERR;
    }

    /* Add listener socket to the event instance */
    pApiData->pEvData = XEvents_RegisterEvent(pEvents, pApiData, pSock->nFD, nEvents, XAPI_CLIENT);
    if (pApiData->pEvData == NULL)
    {
        XAPI_ErrorCb(pApi, pApiData, XAPI_NONE, XAPI_ERR_REGISTER);
        XAPI_FreeData(&pApiData);
        return XSTDERR;
    }

    if (XAPI_ServiceCb(pApi, pApiData, XAPI_CB_CONNECTED) < 0)
    {
        XEvents_Delete(pEvents, pApiData->pEvData);
        pApiData->pEvData = NULL;
        return XSTDERR;
    }

    return XSTDOK;
}
