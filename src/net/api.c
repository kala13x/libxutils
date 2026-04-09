/*!
 *  @file libxutils/src/net/api.c
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of high performance, event based,
 * non-blocking client/server API for the HTTP, WebSocket
 * and raw TCP connections. The library will use epoll(),
 * poll() or WSAPoll() depending on the operating system.
 */

#include "api.h"
#include "xver.h"
#include "str.h"
#include "buf.h"
#include "cpu.h"
#include "sha1.h"
#include "base64.h"

#ifdef __linux__
#include <sys/prctl.h>
#endif

#define XAPI_RX_MAX     (5000 * 1024)
#define XAPI_RX_SIZE    4096

typedef struct XAPIWorkerEvents {
    xevent_data_t **ppEvents;
    size_t nCount;
    size_t nSize;
} xapi_worker_events_t;

static int XAPI_FindWorker(xapi_t *pApi, xpid_t nPID);
XSTATUS XAPI_SpawnWorker(xapi_t *pApi, size_t nIndex);
XSTATUS XAPI_WaitWorkerPIDs(xpid_t *pWorkerPIDs, size_t nWorkers);
XSTATUS XAPI_StopWorkerPIDs(xpid_t *pWorkerPIDs, size_t nWorkers, int nSignal);

const char* XAPI_GetStatusStr(xapi_status_t eStatus)
{
    switch (eStatus)
    {
        case XAPI_TIMER_DESTROY:
            return "Timer event destroyed";
        case XAPI_ERR_AUTH:
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
        case XAPI_ERR_RESOLVE:
            return "Failed to resolve address";
        case XAPI_ERR_SUPPORT:
            return "Unsupported API feature or runtime configuration";
        case XAPI_ERR_FORK:
            return "Failed to fork worker process";
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

const char* XAPI_GetTypeStr(xapi_type_t eType)
{
    switch (eType)
    {
        case XAPI_SELF:
            return "API";
        case XAPI_EVENT:
            return "Event";
        case XAPI_HTTP:
            return "HTTP";
        case XAPI_MDTP:
            return "MDTP";
        case XAPI_SOCK:
            return "Socket";
        case XAPI_WS:
            return "WebSocket";
        default:
            break;
    }

    return "Unknown";
}

const char* XAPI_GetStatus(xapi_ctx_t *pCtx)
{
    XCHECK(pCtx, "Invalid API context");

    switch (pCtx->eStatType)
    {
        case XAPI_SELF:
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

xbool_t XAPI_IsWorker(const xapi_t *pApi)
{
    XCHECK_NL((pApi != NULL), XFALSE);
    return pApi->bIsWorker;
}

size_t XAPI_GetWorkerCount(const xapi_t *pApi)
{
    XCHECK_NL((pApi != NULL), XSTDNON);
    return pApi->nWorkerCount;
}

int XAPI_GetWorkerIndex(const xapi_t *pApi)
{
    XCHECK_NL((pApi != NULL), XSTDERR);
    return pApi->nWorkerIndex;
}

int XAPI_GetCoreIndex(const xapi_t *pApi)
{
    XCHECK_NL((pApi != NULL), XSTDERR);
    return pApi->nCoreIndex;
}

const xpid_t* XAPI_GetWorkerPIDs(const xapi_t *pApi)
{
    XCHECK_NL((pApi != NULL), NULL);
    return pApi->pWorkerPIDs;
}

xpid_t XAPI_GetWorkerPID(const xapi_t *pApi)
{
    XCHECK_NL((pApi != NULL), XSTDNON);
    return pApi->nWorkerPID;
}

xpid_t XAPI_WaitWorker(xapi_t *pApi, int *pWaitStatus)
{
#ifdef _WIN32
    return XSTDNON;
#else
    XCHECK_NL((pApi != NULL), XSTDINV);
    XCHECK_NL((pApi->nWorkerCount > 0), XSTDNON);
    XCHECK_NL((pApi->pWorkerPIDs != NULL), XSTDNON);

    xpid_t nPID = waitpid(-1, pWaitStatus, 0);
    if (nPID > 0)
    {
        int nIndex = XAPI_FindWorker(pApi, nPID);
        if (nIndex >= 0) pApi->pWorkerPIDs[nIndex] = XSTDNON;
        return nPID;
    }

    if (nPID < 0 && (errno == EINTR || errno == ECHILD))
        return XSTDNON;

    return XSTDERR;
#endif
}

XSTATUS XAPI_WaitWorkers(xapi_t *pApi)
{
    XCHECK_NL((pApi != NULL), XSTDINV);
    XCHECK_NL((pApi->pWorkerPIDs != NULL && pApi->nWorkerCount > 0), XSTDNON);
    return XAPI_WaitWorkerPIDs(pApi->pWorkerPIDs, pApi->nWorkerCount);
}

XSTATUS XAPI_StopWorkers(xapi_t *pApi, int nSignal)
{
    XCHECK_NL((pApi != NULL), XSTDINV);
    XCHECK_NL((pApi->pWorkerPIDs != NULL && pApi->nWorkerCount > 0), XSTDNON);
    return XAPI_StopWorkerPIDs(pApi->pWorkerPIDs, pApi->nWorkerCount, nSignal);
}

XSTATUS XAPI_WatchWorkers(xapi_t *pApi, const volatile sig_atomic_t *pInterrupted)
{
#if defined(_XEVENTS_USE_EPOLL)
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK_NL((!pApi->bIsWorker), XSTDNON);
    XCHECK_NL((pApi->pWorkerPIDs != NULL && pApi->nWorkerCount > 0), XSTDNON);

    for (;;)
    {
        if (pInterrupted != NULL && *pInterrupted)
        {
            XSTATUS nStopStatus = XAPI_StopWorkers(pApi, SIGTERM);
            XSTATUS nWaitStatus = XAPI_WaitWorkers(pApi);

            if (nStopStatus == XSTDERR || nWaitStatus == XSTDERR) return XSTDERR;
            return (nStopStatus == XSTDOK || nWaitStatus == XSTDOK) ? XSTDOK : XSTDNON;
        }

        int nWaitStatus = 0;
        xpid_t nPID = waitpid(-1, &nWaitStatus, 0);
        if (nPID < 0)
        {
            if (errno == EINTR) continue;
            if (errno == ECHILD) return XSTDNON;
            return XSTDERR;
        }

        int nIndex = XAPI_FindWorker(pApi, nPID);
        if (nIndex >= 0) pApi->pWorkerPIDs[nIndex] = XSTDNON;
        if (nIndex < 0 || (pInterrupted != NULL && *pInterrupted)) continue;

        XSTATUS nStatus = XAPI_SpawnWorker(pApi, (size_t)nIndex);
        if (nStatus != XSTDOK) return nStatus;
    }
#else
    (void)pApi;
    (void)pInterrupted;
    return XSTDNON;
#endif
}

xbool_t XAPI_IsDestroyEvent(xapi_ctx_t *pCtx)
{
    XCHECK_NL((pCtx != NULL), XFALSE);
    return (pCtx->eStatType == XAPI_SELF &&
            pCtx->nStatus == XAPI_DESTROY);
}

static xapi_session_t* XAPI_NewData(xapi_t *pApi, xapi_type_t eType)
{
    XCHECK((pApi != NULL), NULL);

    xapi_session_t *pSession = (xapi_session_t*)malloc(sizeof(xapi_session_t));
    XCHECK((pSession != NULL), NULL);

    xstrncpyf(pSession->sUserAgent, sizeof(pSession->sUserAgent), "xutils/%s", XUtils_VersionShort());
    XSock_Init(&pSession->sock, XSOCK_UNDEFINED, XSOCK_INVALID);
    XByteBuffer_Init(&pSession->rxBuffer, XSTDNON, XFALSE);
    XByteBuffer_Init(&pSession->txBuffer, XSTDNON, XFALSE);
    XByteBuffer_Init(&pSession->wsBuffer, XSTDNON, XFALSE);

    pSession->sRealIP[0] = XSTR_NUL;
    pSession->sAddr[0] = XSTR_NUL;
    pSession->sKey[0] = XSTR_NUL;
    pSession->sUri[0] = XSTR_NUL;
    pSession->nPort = XSTDNON;
    pSession->nEvents = XSTDNON;

    pSession->bHandshakeStart = XFALSE;
    pSession->bHandshakeDone = XFALSE;
    pSession->bWSFragStart = XFALSE;
    pSession->bReadOnWrite = XFALSE;
    pSession->bWriteOnRead = XFALSE;
    pSession->bKeepRxBuffer = XFALSE;
    pSession->bKeepAlive = XFALSE;
    pSession->bCancel = XFALSE;
    pSession->bAlloc = XTRUE;

    pSession->pEvData = NULL;
    pSession->pTimer = NULL;
    pSession->eType = eType;
    pSession->pApi = pApi;
    pSession->eWSFragType = XWS_INVALID;

    pSession->pSessionData = NULL;
    pSession->pPacket = NULL;
    pSession->nID = ++pApi->nSessionCounter;

    return pSession;
}

static void XAPI_ClearData(xapi_session_t *pSession)
{
    XCHECK_VOID_NL(pSession);
    XAPI_DeleteTimer(pSession);
    XSock_Close(&pSession->sock);
    XByteBuffer_Clear(&pSession->rxBuffer);
    XByteBuffer_Clear(&pSession->txBuffer);
    XByteBuffer_Clear(&pSession->wsBuffer);
}

static void XAPI_FreeData(xapi_session_t **pSession)
{
    XCHECK_VOID((pSession && *pSession));
    xapi_session_t *pSessionPtr = *pSession;

    XAPI_ClearData(pSessionPtr);
    if (pSessionPtr->bAlloc)
    {
        free(pSessionPtr);
        *pSession = NULL;
    }
}

static int XAPI_Callback(xapi_t *pApi, xapi_session_t *pSession, xapi_cb_type_t eCbType, xapi_type_t eType, uint8_t nStat)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK_NL(pApi->callback, XSTDOK);

    xapi_ctx_t ctx;
    ctx.nWorkerIndex = pApi->nWorkerIndex;
    ctx.nCoreIndex = pApi->nCoreIndex;
    ctx.eCbType = eCbType;
    ctx.eStatType = eType;
    ctx.nStatus = nStat;
    ctx.pApi = pApi;

    return pApi->callback(&ctx, pSession);
}

static int XAPI_ServiceCb(xapi_t *pApi, xapi_session_t *pSession, xapi_cb_type_t eCbType)
{
    return XAPI_Callback(pApi, pSession,
        eCbType, XAPI_SELF, XAPI_UNKNOWN);
}

static int XAPI_ErrorCb(xapi_t *pApi, xapi_session_t *pSession, xapi_type_t eType, uint8_t nStat)
{
    return XAPI_Callback(pApi, pSession,
        XAPI_CB_ERROR, eType, nStat);
}

static int XAPI_StatusCb(xapi_t *pApi, xapi_session_t *pSession, xapi_type_t eType, uint8_t nStat)
{
    return XAPI_Callback(pApi, pSession,
        XAPI_CB_STATUS, eType, nStat);
}

static int XAPI_StatusToEvent(xapi_t *pApi, int nStatus)
{
    if (nStatus == XAPI_USER_CB)
    {
        nStatus = XAPI_ServiceCb(pApi, NULL, XAPI_CB_USER);
        return XAPI_StatusToEvent(pApi, nStatus);
    }

    if (nStatus == XAPI_RELOOP)
    {
        return XEVENTS_RELOOP;
    }

    return nStatus < XAPI_NO_ACTION ?
        XEVENTS_DISCONNECT :
        XEVENTS_CONTINUE;
}

static int XAPI_WorkerEventCb(xhash_pair_t *pPair, void *pCtx)
{
    XCHECK_NL((pPair != NULL), XSTDNON);
    XCHECK_NL((pCtx != NULL), XSTDERR);

    xapi_worker_events_t *pEvents = (xapi_worker_events_t*)pCtx;
    XCHECK_NL((pPair->pData != NULL), XSTDERR);
    XCHECK_NL((pEvents->nCount < pEvents->nSize), XSTDERR);

    pEvents->ppEvents[pEvents->nCount++] = (xevent_data_t*)pPair->pData;
    return XSTDNON;
}

static void XAPI_DetachWorkerEventMap(xevents_t *pEvents)
{
    XCHECK_VOID_NL((pEvents != NULL));
    XCHECK_VOID_NL(pEvents->bUseHash);

    pEvents->eventsMap.clearCb = NULL;
    pEvents->eventsMap.pUserContext = NULL;

    XHash_Destroy(&pEvents->eventsMap);
    pEvents->bUseHash = XFALSE;
}

static void XAPI_ResetWorkerEvents(xevents_t *pEvents)
{
    XCHECK_VOID_NL((pEvents != NULL));

    if (pEvents->pEventArray != NULL)
    {
        free(pEvents->pEventArray);
        pEvents->pEventArray = NULL;
    }

#if defined(_XEVENTS_USE_EPOLL)
    if (pEvents->nEventFd >= 0)
    {
        close(pEvents->nEventFd);
        pEvents->nEventFd = XSOCK_INVALID;
    }
#endif

    XAPI_DetachWorkerEventMap(pEvents);
    pEvents->nEventCount = XSTDNON;
    pEvents->bResync = XFALSE;
}

xevent_status_t XAPI_RebuildWorkerEvents(xapi_t *pApi)
{
    XCHECK((pApi != NULL), XEVENTS_EINVALID);
    XCHECK((pApi->bHaveEvents), XEVENTS_EINVALID);

    xevents_t *pEvents = &pApi->events;
    XCHECK((pEvents->bUseHash), XEVENTS_EINVALID);

    const size_t nEventCount = pEvents->eventsMap.nPairCount;
    XCHECK_NL((nEventCount > 0), XEVENTS_SUCCESS);

    xevent_data_t **ppEvents = (xevent_data_t**)calloc(nEventCount, sizeof(xevent_data_t*));
    XCHECK((ppEvents != NULL), XEVENTS_EALLOC);

    xapi_worker_events_t workerEvents;
    workerEvents.ppEvents = ppEvents;
    workerEvents.nCount = XSTDNON;
    workerEvents.nSize = nEventCount;

    XHash_Iterate(&pEvents->eventsMap, XAPI_WorkerEventCb, &workerEvents);
    if (workerEvents.nCount != nEventCount)
    {
        free(ppEvents);
        return XEVENTS_EINVALID;
    }

    const uint32_t nEventMax = pEvents->nEventMax;
    const xbool_t bUseHash = pEvents->bUseHash;
    void *pUserSpace = pEvents->pUserSpace;
    xevent_cb_t callback = pEvents->eventCallback;

    // Reset the event map to avoid duplicate events
    XAPI_ResetWorkerEvents(pEvents);

    xevent_status_t eStatus = XEvents_Create(pEvents, nEventMax, pUserSpace, callback, bUseHash);
    if (eStatus != XEVENTS_SUCCESS)
    {
        pApi->bHaveEvents = XFALSE;
        free(ppEvents);
        return eStatus;
    }

    size_t i = 0;
    for (i = 0; i < workerEvents.nCount; i++)
    {
        xevent_data_t *pEvData = ppEvents[i];
        if (pEvData == NULL || pEvData->nFD == XSOCK_INVALID) continue;

        pEvData->nIndex = -1;
        eStatus = XEvents_Add(pEvents, pEvData, pEvData->nEvents);
        if (eStatus != XEVENTS_SUCCESS)
        {
            XAPI_ResetWorkerEvents(pEvents);
            pApi->bHaveEvents = XFALSE;

            free(ppEvents);
            return eStatus;
        }
    }

    free(ppEvents);
    return XEVENTS_SUCCESS;
}

static int XAPI_FindWorker(xapi_t *pApi, xpid_t nPID)
{
    XCHECK_NL((pApi != NULL), XSTDERR);
    XCHECK_NL((pApi->pWorkerPIDs != NULL), XSTDERR);

    size_t i = 0;
    for (i = 0; i < pApi->nWorkerCount; i++)
    {
        if (pApi->pWorkerPIDs[i] == nPID)
            return (int)i;
    }

    return XSTDERR;
}

XSTATUS XAPI_InitWorkerDeathSignal(void)
{
#ifdef __linux__
    xpid_t nParentPID = getppid();

    if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0)
        return XSTDERR;

    if (nParentPID > 1 && getppid() != nParentPID)
        _exit(XSTDERR);
#endif

    return XSTDOK;
}

XSTATUS XAPI_SetWorkerCPUAffinity(xapi_t *pApi, size_t nIndex)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK_NL(pApi->bSetAffinity, XSTDOK);

    int nCPUCount = XCPU_GetCount();
    if (nCPUCount <= 0) return XSTDERR;

    int nCPUIndex = (int)(nIndex % (size_t)nCPUCount);
    int nStatus = XCPU_SetSingle(nCPUIndex, XCPU_CALLER_PID);
    XCHECK((nStatus >= 0), XSTDERR);

    pApi->nCoreIndex = nCPUIndex;
    return XSTDOK;
}

XSTATUS XAPI_SpawnWorker(xapi_t *pApi, size_t nIndex)
{
#if defined(_XEVENTS_USE_EPOLL)
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pApi->bHaveEvents), XSTDINV);
    XCHECK((pApi->bUseHashMap && pApi->events.bUseHash), XSTDINV);

    xpid_t nPID = fork();
    if (nPID < 0)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_SELF, XAPI_ERR_FORK);
        return XSTDERR;
    }

    if (!nPID)
    {
        if (XAPI_InitWorkerDeathSignal() < 0)
        {
            XAPI_ErrorCb(pApi, NULL, XAPI_SELF, XAPI_ERR_SUPPORT);
            return XSTDERR;
        }

        if (XAPI_SetWorkerCPUAffinity(pApi, nIndex) < 0)
        {
            XAPI_ErrorCb(pApi, NULL, XAPI_SELF, XAPI_ERR_SUPPORT);
            return XSTDERR;
        }

        free(pApi->pWorkerPIDs);
        pApi->pWorkerPIDs = NULL;
        pApi->nWorkerCount = XSTDNON;
        pApi->nWorkerIndex = (int)nIndex;
        pApi->nWorkerPID = getpid();
        pApi->bIsWorker = XTRUE;

        xevent_status_t eStatus = XAPI_RebuildWorkerEvents(pApi);
        if (eStatus != XEVENTS_SUCCESS)
        {
            XAPI_ErrorCb(pApi, NULL, XAPI_EVENT, eStatus);
            return XSTDERR;
        }

        return XSTDUSR;
    }

    pApi->pWorkerPIDs[nIndex] = nPID;
    return XSTDOK;
#else
    (void)pApi;
    (void)nIndex;
    return XSTDERR;
#endif
}

XSTATUS XAPI_StopWorkerPIDs(xpid_t *pWorkerPIDs, size_t nWorkers, int nSignal)
{
#if defined(_XEVENTS_USE_EPOLL)
    XCHECK_NL((pWorkerPIDs != NULL), XSTDINV);
    XCHECK_NL((nSignal > 0), XSTDINV);

    XSTATUS nStatus = XSTDNON;
    size_t i = 0;
    for (i = 0; i < nWorkers; i++)
    {
        if (pWorkerPIDs[i] > 0)
        {
            if (kill(pWorkerPIDs[i], nSignal) < 0 && errno != ESRCH)
                nStatus = XSTDERR;
            else if (nStatus >= XSTDNON)
                nStatus = XSTDOK;
        }
    }

    return nStatus;
#else
    (void)pWorkerPIDs;
    (void)nWorkers;
    (void)nSignal;
    return XSTDNON;
#endif
}

XSTATUS XAPI_WaitWorkerPIDs(xpid_t *pWorkerPIDs, size_t nWorkers)
{
#if defined(_XEVENTS_USE_EPOLL)
    XCHECK_NL((pWorkerPIDs != NULL), XSTDINV);

    XSTATUS nStatus = XSTDNON;
    size_t i = 0;
    for (i = 0; i < nWorkers; i++)
    {
        while (pWorkerPIDs[i] > 0)
        {
            xpid_t nPID = waitpid(pWorkerPIDs[i], NULL, 0);
            if (nPID == pWorkerPIDs[i])
            {
                pWorkerPIDs[i] = XSTDNON;
                nStatus = XSTDOK;
                break;
            }

            if (nPID < 0 && errno == EINTR)
                continue;

            if (nPID < 0 && errno == ECHILD)
            {
                pWorkerPIDs[i] = XSTDNON;
                break;
            }

            return XSTDERR;
        }
    }

    return nStatus;
#else
    (void)pWorkerPIDs;
    (void)nWorkers;
    return XSTDNON;
#endif
}

static int XAPI_ClearEvent(xapi_t *pApi, xevent_data_t *pEvData)
{
    XCHECK_NL(pEvData, XEVENTS_CONTINUE);
    XSTATUS nStatus = XSTDNON;

    if (pEvData->pContext != NULL)
    {
        if (pEvData->nType == XEVENT_TYPE_TIMER)
        {
            xapi_session_t *pSession = (xapi_session_t*)pEvData->pContext;
            XAPI_StatusCb(pApi, pSession, XAPI_SELF, XAPI_TIMER_DESTROY);

            pSession->pTimer = NULL;
            pEvData->pContext = NULL;
        }
        else
        {
            xapi_session_t *pSession = (xapi_session_t*)pEvData->pContext;
            nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_CLOSED);

            XAPI_FreeData(&pSession);
            pEvData->pContext = NULL;
        }
    }

    pEvData->nFD = XSOCK_INVALID;
    return XAPI_StatusToEvent(pApi, nStatus);
}

size_t XAPI_GetEventCount(xapi_t *pApi)
{
    XCHECK_NL((pApi != NULL), XSTDNON);
    xevents_t *pEvents = &pApi->events;
    return pEvents->nEventCount;
}

XSTATUS XAPI_DeleteTimer(xapi_session_t *pSession)
{
    XCHECK((pSession != NULL), XSTDINV);
    XCHECK_NL((pSession->pApi != NULL), XSTDINV);
    XCHECK_NL((pSession->pTimer != NULL), XSTDOK);

    xapi_t *pApi = pSession->pApi;
    xevent_status_t nStatus;

    nStatus = XEvents_Delete(&pApi->events, pSession->pTimer);
    return (nStatus == XEVENTS_SUCCESS) ? XSTDOK : XSTDERR;
}

XSTATUS XAPI_Disconnect(xapi_session_t *pSession)
{
    XCHECK((pSession != NULL), XSTDINV);
    XCHECK_NL((pSession->pApi != NULL), XSTDINV);
    XCHECK_NL((pSession->pEvData != NULL), XSTDOK);

    xapi_t *pApi = pSession->pApi;
    xevent_status_t nStatus;

    nStatus = XEvents_Delete(&pApi->events, pSession->pEvData);
    return (nStatus == XEVENTS_SUCCESS) ? XSTDOK : XSTDERR;
}

XSTATUS XAPI_ExtendTimer(xapi_session_t *pSession, int nTimeoutMs)
{
    XCHECK((pSession != NULL), XSTDINV);
    XCHECK((pSession->pApi != NULL), XSTDINV);
    XCHECK((pSession->pTimer != NULL), XSTDINV);
    XCHECK_NL((nTimeoutMs > 0), XSTDNON);

    xevent_data_t *pTimer = pSession->pTimer;
    xapi_t *pApi = pSession->pApi;

    xevent_status_t eStatus = XEvents_ExtendTimer(&pApi->events, pTimer, nTimeoutMs);
    if (eStatus != XEVENTS_SUCCESS)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_EVENT, eStatus);
        return XSTDERR;
    }

    return XSTDOK;
}

XSTATUS XAPI_AddTimer(xapi_session_t *pSession, int nTimeoutMs)
{
    XCHECK((pSession != NULL), XSTDINV);
    XCHECK((pSession->pApi != NULL), XSTDINV);
    XCHECK_NL((nTimeoutMs > 0), XSTDNON);

    if (pSession->pTimer != NULL)
        return XAPI_ExtendTimer(pSession, nTimeoutMs);

    xapi_t *pApi = pSession->pApi;
    xevents_t *pEvents = &pApi->events;

    xevent_data_t *pTimerData = XEvents_AddTimer(pEvents, pSession, nTimeoutMs);
    if (pTimerData == NULL)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_EVENT, XEVENTS_ETIMER);
        return XSTDERR;
    }

    pSession->pTimer = pTimerData;
    return XSTDOK;
}

XSTATUS XAPI_SetEvents(xapi_session_t *pSession, int nEvents)
{
    XCHECK((pSession != NULL), XSTDINV);
    XCHECK((pSession->pApi != NULL), XSTDINV);
    XCHECK((pSession->pEvData != NULL), XSTDINV);
    XCHECK((pSession->pApi->bHaveEvents), XSTDERR);

    xevent_data_t *pEvData = pSession->pEvData;
    xapi_t *pApi = pSession->pApi;

    xevent_status_t eStatus;
    eStatus = XEvents_Modify(&pApi->events, pEvData, nEvents);

    if (eStatus != XEVENTS_SUCCESS)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_EVENT, eStatus);
        return XSTDERR;
    }

    pSession->nEvents = nEvents;
    return XSTDOK;
}

XSTATUS XAPI_EnableEvent(xapi_session_t *pSession, int nEvent)
{
    XCHECK((pSession != NULL), XSTDINV);

    if (!XFLAGS_CHECK(pSession->nEvents, nEvent))
    {
        int nEvents = pSession->nEvents | nEvent;
        return XAPI_SetEvents(pSession, nEvents);
    }

    return XSTDOK;
}

XSTATUS XAPI_DisableEvent(xapi_session_t *pSession, int nEvent)
{
    XCHECK((pSession != NULL), XSTDINV);

    if (XFLAGS_CHECK(pSession->nEvents, nEvent))
    {
        int nEvents = pSession->nEvents & ~nEvent;
        return XAPI_SetEvents(pSession, nEvents);
    }

    return XSTDOK;
}

static XSTATUS XAPI_RollbackEvents(xapi_session_t *pSession)
{
    XCHECK((pSession != NULL), XSTDINV);
    return XAPI_SetEvents(pSession, pSession->nEvents);
}

xbyte_buffer_t* XAPI_GetTxBuff(xapi_session_t *pSession)
{
    XCHECK((pSession != NULL), NULL);
    return &pSession->txBuffer;
}

xbyte_buffer_t* XAPI_GetRxBuff(xapi_session_t *pSession)
{
    XCHECK((pSession != NULL), NULL);
    return &pSession->rxBuffer;
}

XSTATUS XAPI_PutTxBuff(xapi_session_t *pSession, xbyte_buffer_t *pBuffer)
{
    XCHECK((pSession != NULL), XSTDINV);
    XCHECK((pBuffer != NULL), XSTDINV);
    XCHECK_NL(pBuffer->nUsed, XSTDNON);

    XByteBuffer_AddBuff(&pSession->txBuffer, pBuffer);
    if (!pSession->txBuffer.nUsed)
    {
        XAPI_ErrorCb(pSession->pApi, pSession, XAPI_SELF, XAPI_ERR_ALLOC);
        return XSTDERR;
    }

    return XSTDOK;
}

static xbool_t XAPI_CopyTrimmedIP(char *pDst, size_t nDstSize, const char *pSrc)
{
    XCHECK_NL((pDst != NULL), XFALSE);
    XCHECK_NL((nDstSize > 0), XFALSE);

    pDst[0] = '\0';
    if (!xstrused(pSrc)) return XFALSE;
    while (*pSrc == ' ' || *pSrc == '\t') pSrc++;

    size_t nLen = 0;
    while (pSrc[nLen] != '\0' &&
           pSrc[nLen] != ',' &&
           pSrc[nLen] != ' ' &&
           pSrc[nLen] != '\t' &&
           nLen + 1 < nDstSize) nLen++;

    if (!nLen) return XFALSE;
    memcpy(pDst, pSrc, nLen);
    pDst[nLen] = '\0';

    return XTRUE;
}

static int XAPI_DetectRealIP(xapi_session_t *pSession, xhttp_t *pHandle)
{
    XCHECK((pSession != NULL), XSTDINV);
    XCHECK((pHandle != NULL), XSTDINV);

    if (XAPI_CopyTrimmedIP(pSession->sRealIP, sizeof(pSession->sRealIP),
        XHTTP_GetHeader(pHandle, "X-Client-IP"))) return XSTDOK;

    if (XAPI_CopyTrimmedIP(pSession->sRealIP, sizeof(pSession->sRealIP),
        XHTTP_GetHeader(pHandle, "X-Forwarded-For"))) return XSTDOK;

    if (XAPI_CopyTrimmedIP(pSession->sRealIP, sizeof(pSession->sRealIP),
        XHTTP_GetHeader(pHandle, "X-Real-IP"))) return XSTDOK;

    if (XAPI_CopyTrimmedIP(pSession->sRealIP, sizeof(pSession->sRealIP),
        pSession->sAddr)) return XSTDNON;

    return XSTDERR;
}

XSTATUS XAPI_RespondHTTP(xapi_session_t *pSession, int nCode, xapi_status_t eStatus)
{
    XCHECK((pSession != NULL), XSTDINV);
    XCHECK((pSession->pApi != NULL), XSTDINV);
    xapi_t *pApi = pSession->pApi;

    xhttp_t handle;
    XHTTP_InitResponse(&handle, nCode, NULL);

    char sContent[XSTR_MID];
    size_t nLength = xstrncpyf(sContent, sizeof(sContent), "{\"status\": \"%s\"}",
        eStatus != XAPI_UNKNOWN ? XAPI_GetStatusStr(eStatus) : XHTTP_GetCodeStr(nCode));

    if ((eStatus == XAPI_MISSING_TOKEN &&
        XHTTP_AddHeader(&handle, "WWW-Authenticate", "Basic realm=\"XAPI\"") < 0) ||
        XHTTP_AddHeader(&handle, "Server", "%s", pSession->sUserAgent) < 0 ||
        XHTTP_AddHeader(&handle, "Content-Type", "application/json") < 0 ||
        XHTTP_Assemble(&handle, (const uint8_t*)sContent, nLength) == NULL)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_SELF, XAPI_ERR_ASSEMBLE);
        XHTTP_Clear(&handle);

        pSession->bCancel = XTRUE;
        return XEVENTS_DISCONNECT;
    }

    if (eStatus > XAPI_UNKNOWN && eStatus < XAPI_STATUS_OK)
        XAPI_ErrorCb(pApi, pSession, XAPI_SELF, eStatus);
    else if (eStatus != XAPI_UNKNOWN)
        XAPI_StatusCb(pApi, pSession, XAPI_SELF, eStatus);

    XAPI_PutTxBuff(pSession, &handle.rawData);
    XHTTP_Clear(&handle);

    if (!pSession->txBuffer.nUsed) return XEVENTS_DISCONNECT;
    XSTATUS nStatus = XAPI_EnableEvent(pSession, XPOLLOUT);
    return XAPI_StatusToEvent(pApi, nStatus);
}

XSTATUS XAPI_AuthorizeHTTP(xapi_session_t *pSession, const char *pToken, const char *pKey)
{
    XCHECK((pSession != NULL), XSTDINV);
    XCHECK((pSession->pPacket != NULL), XSTDINV);
    xhttp_t *pHandle = (xhttp_t*)pSession->pPacket;

    size_t nTokenLength = xstrused(pToken) ? strlen(pToken) : XSTDNON;
    size_t nKeyLength = xstrused(pKey) ? strlen(pKey) : XSTDNON;
    if (!nTokenLength && !nKeyLength) return XSTDOK;

    if (nKeyLength)
    {
        const char *pXKey = XHTTP_GetHeader(pHandle, "X-API-KEY");
        if (!xstrused(pXKey)) return XAPI_RespondHTTP(pSession, 401, XAPI_MISSING_KEY);
        if (strncmp(pXKey, pKey, nKeyLength)) return XAPI_RespondHTTP(pSession, 401, XAPI_INVALID_KEY);
    }

    if (nTokenLength)
    {
        const char *pAuth = XHTTP_GetHeader(pHandle, "Authorization");
        if (!xstrused(pAuth)) return XAPI_RespondHTTP(pSession, 401, XAPI_MISSING_TOKEN);

        int nPosit = xstrsrc(pAuth, "Basic");
        if (nPosit < 0) return XAPI_RespondHTTP(pSession, 401, XAPI_MISSING_TOKEN);

        if (strncmp(&pAuth[nPosit + 6], pToken, nTokenLength))
            return XAPI_RespondHTTP(pSession, 401, XAPI_INVALID_TOKEN);
    }

    return XSTDOK;
}

static int XAPI_HandleMDTP(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pSession != NULL), XSTDINV);
    xbyte_buffer_t *pBuffer = &pSession->rxBuffer;
    xpacket_status_t eStatus = XPACKET_ERR_NONE;
    int nRetVal = XEVENTS_CONTINUE;

    xpacket_t packet;
    eStatus = XPacket_Parse(&packet, pBuffer->pData, pBuffer->nSize);

    if (eStatus == XPACKET_COMPLETE)
    {
        pSession->pPacket = &packet;

        int nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_READ);
        nRetVal = XAPI_StatusToEvent(pApi, nStatus);

        size_t nPacketSize = XPacket_GetSize(&packet);
        XByteBuffer_Advance(pBuffer, nPacketSize);
    }
    else if (eStatus != XPACKET_PARSED && eStatus != XPACKET_INCOMPLETE)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_MDTP, eStatus);
        nRetVal = XEVENTS_DISCONNECT;
    }
    else if (eStatus == XPACKET_INCOMPLETE && pBuffer->nUsed > pApi->nRxSize)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_MDTP, XPACKET_BIGDATA);
        nRetVal = XEVENTS_DISCONNECT;
    }

    pSession->pPacket = NULL;
    XPacket_Clear(&packet);

    if (eStatus == XPACKET_COMPLETE &&
        nRetVal == XEVENTS_CONTINUE &&
        XByteBuffer_HasData(pBuffer))
        return XAPI_HandleMDTP(pApi, pSession);

    return nRetVal;
}

static int XAPI_HandleHTTP(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pSession != NULL), XSTDINV);
    xbyte_buffer_t *pBuffer = &pSession->rxBuffer;
    xhttp_status_t eStatus = XHTTP_NONE;
    int nRetVal = XEVENTS_CONTINUE;

    xhttp_t handle;
    XHTTP_Init(&handle, XHTTP_DUMMY, XSTDNON);
    eStatus = XHTTP_ParseBuff(&handle, pBuffer);

    if (!xstrused(pSession->sRealIP) &&
        (eStatus == XHTTP_COMPLETE ||
         eStatus == XHTTP_PARSED))
        XAPI_DetectRealIP(pSession, &handle);

    if (eStatus == XHTTP_COMPLETE)
    {
        pSession->pPacket = &handle;
        pSession->bKeepAlive = handle.nKeepAlive;

        int nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_READ);
        nRetVal = XAPI_StatusToEvent(pApi, nStatus);

        size_t nPacketSize = XHTTP_GetPacketSize(&handle);
        XByteBuffer_Advance(pBuffer, nPacketSize);
    }
    else if (eStatus != XHTTP_PARSED && eStatus != XHTTP_INCOMPLETE)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_HTTP, eStatus);
        nRetVal = XEVENTS_DISCONNECT;
    }
    else if (eStatus == XHTTP_INCOMPLETE && pBuffer->nUsed > pApi->nRxSize)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_HTTP, XHTTP_BIGCNT);
        nRetVal = XEVENTS_DISCONNECT;
    }

    pSession->pPacket = NULL;
    XHTTP_Clear(&handle);

    if (eStatus == XHTTP_COMPLETE &&
        nRetVal == XEVENTS_CONTINUE &&
        XByteBuffer_HasData(pBuffer))
        return XAPI_HandleHTTP(pApi, pSession);

    return nRetVal;
}

static char* XAPI_GetWSKey(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pSession != NULL), NULL);
    uint8_t uDigest[XSHA1_DIGEST_SIZE];
    char sBuffer[XSTR_MID];
    size_t nLength;

    nLength = xstrncpyf(sBuffer, sizeof(sBuffer), "%s%s", pSession->sKey, XWS_GUID);
    XSHA1_Compute(uDigest, sizeof(uDigest), (const uint8_t*)sBuffer, nLength);

    nLength = XSHA1_DIGEST_SIZE;
    char *pSecKey = XBase64_Encrypt(uDigest, &nLength);
    if (pSecKey != NULL) return pSecKey;

    XAPI_ErrorCb(pApi, pSession, XAPI_SELF, XAPI_ERR_CRYPT);
    pSession->bCancel = XTRUE;

    return NULL;
}

static xbool_t XAPI_HeaderHasTokenCI(const char *pValue, const char *pToken)
{
    if (!xstrused(pValue) || !xstrused(pToken)) return XFALSE;
    const size_t nTokenLen = strlen(pToken);

    const char *p = pValue;
    while (*p != XSTR_NUL)
    {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (*p == XSTR_NUL) break;

        const char *pStart = p;
        while (*p != XSTR_NUL && *p != ',') p++;
        const char *pEnd = p;

        while (pEnd > pStart && (pEnd[-1] == ' ' || pEnd[-1] == '\t')) pEnd--;

        const size_t nLen = (size_t)(pEnd - pStart);
        if (nLen == nTokenLen && xstrncasecmp(pStart, pToken, nTokenLen))
            return XTRUE;

        if (*p == ',') p++;
    }

    return XFALSE;
}

static int XAPI_AnswerUpgrade(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pSession != NULL), XSTDINV);

    xhttp_t handle;
    XHTTP_InitResponse(&handle, 101, NULL);

    char *pSecKey = XAPI_GetWSKey(pApi, pSession);
    if (pSecKey == NULL)
    {
        XHTTP_Clear(&handle);
        return XEVENTS_DISCONNECT;
    }

    if (XHTTP_AddHeader(&handle, "Upgrade", "websocket") < 0 ||
        XHTTP_AddHeader(&handle, "Connection", "Upgrade") < 0 ||
        XHTTP_AddHeader(&handle, "Sec-WebSocket-Accept", "%s", pSecKey) < 0 ||
        XHTTP_AddHeader(&handle, "Server", "%s", pSession->sUserAgent) < 0 ||
        XHTTP_Assemble(&handle, NULL, XSTDNON) == NULL)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_SELF, XAPI_ERR_ASSEMBLE);
        XHTTP_Clear(&handle);
        free(pSecKey);

        pSession->bCancel = XTRUE;
        return XEVENTS_DISCONNECT;
    }

    free(pSecKey);
    pSession->pPacket = &handle;

    int nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_HANDSHAKE_ANSWER);
    int nRetVal = XAPI_StatusToEvent(pApi, nStatus);

    XAPI_PutTxBuff(pSession, &handle.rawData);
    xbyte_buffer_t *pBuffer = &pSession->txBuffer;

    XHTTP_Clear(&handle);
    pSession->pPacket = NULL;

    if (nRetVal != XEVENTS_CONTINUE) return nRetVal;
    else if (!pBuffer->nUsed) return XEVENTS_DISCONNECT;

    nStatus = XAPI_EnableEvent(pSession, XPOLLOUT);
    return XAPI_StatusToEvent(pApi, nStatus);
}

static int XAPI_RequestUpgrade(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pSession != NULL), XSTDINV);

    xhttp_t handle;
    XHTTP_InitRequest(&handle, XHTTP_GET, pSession->sUri, NULL);

    char sNonce[XWS_NONCE_LENGTH + 1];
    size_t nLength = XWS_NONCE_LENGTH;

    xstrrand(sNonce, sizeof(sNonce), nLength, XTRUE, XTRUE);
    char *pSecKey = XBase64_Encrypt((uint8_t*)sNonce, &nLength);

    if (pSecKey == NULL)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_SELF, XAPI_ERR_CRYPT);
        pSession->bCancel = XTRUE;

        XHTTP_Clear(&handle);
        return XEVENTS_DISCONNECT;
    }

    xstrncpy(pSession->sKey, sizeof(pSession->sKey), pSecKey);
    free(pSecKey);

    char sHost[XSOCK_ADDR_MAX + XSTR_TINY];
    xbool_t bSSL = XSock_IsSSL(&pSession->sock);
    xbool_t bDefaultPort = (bSSL && pSession->nPort == XHTTP_SSL_PORT) ||
                           (!bSSL && pSession->nPort == XHTTP_DEF_PORT);

    if (bDefaultPort) xstrncpy(sHost, sizeof(sHost), pSession->sAddr);
    else xstrncpyf(sHost, sizeof(sHost), "%s:%u", pSession->sAddr, (unsigned)pSession->nPort);

    if (XHTTP_AddHeader(&handle, "Upgrade", "websocket") < 0 ||
        XHTTP_AddHeader(&handle, "Connection", "Upgrade") < 0 ||
        XHTTP_AddHeader(&handle, "Sec-WebSocket-Version", "%d", XWS_SEC_WS_VERSION) < 0 ||
        XHTTP_AddHeader(&handle, "Sec-WebSocket-Key", "%s", pSession->sKey) < 0 ||
        XHTTP_AddHeader(&handle, "User-Agent", "%s", pSession->sUserAgent) < 0 ||
        XHTTP_AddHeader(&handle, "Host", "%s", sHost) < 0 ||
        XHTTP_Assemble(&handle, NULL, XSTDNON) == NULL)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_SELF, XAPI_ERR_ASSEMBLE);
        XHTTP_Clear(&handle);

        pSession->bCancel = XTRUE;
        return XEVENTS_DISCONNECT;
    }

    pSession->bHandshakeStart = XTRUE;
    pSession->pPacket = &handle;

    int nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_HANDSHAKE_REQUEST);
    int nRetVal = XAPI_StatusToEvent(pApi, nStatus);

    XAPI_PutTxBuff(pSession, &handle.rawData);
    xbyte_buffer_t *pBuffer = &pSession->txBuffer;

    XHTTP_Clear(&handle);
    pSession->pPacket = NULL;

    if (nRetVal != XEVENTS_CONTINUE) return nRetVal;
    else if (!pBuffer->nUsed) return XEVENTS_DISCONNECT;

    nStatus = XAPI_EnableEvent(pSession, XPOLLOUT);
    return XAPI_StatusToEvent(pApi, nStatus);
}

static int XAPI_ServerHandshake(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pSession != NULL), XSTDINV);

    if (pSession->bHandshakeStart) return XEVENTS_CONTINUE;
    xbyte_buffer_t *pBuffer = &pSession->rxBuffer;
    xhttp_status_t eStatus = XHTTP_NONE;
    int nRetVal = XEVENTS_CONTINUE;

    if (pBuffer->nUsed >= 3)
    {
        const uint8_t *pData = (const uint8_t*)pBuffer->pData;
        if (pData != NULL &&
            pData[0] == 0x16 &&
            pData[1] == 0x03 &&
            pData[2] <= 0x04)
        {
            XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_INVALID_REQUEST);
            return XEVENTS_DISCONNECT;
        }
    }

    xhttp_t handle;
    XHTTP_Init(&handle, XHTTP_DUMMY, XSTDNON);
    eStatus = XHTTP_ParseBuff(&handle, pBuffer);

    if (!xstrused(pSession->sRealIP) &&
        (eStatus == XHTTP_COMPLETE ||
         eStatus == XHTTP_PARSED))
        XAPI_DetectRealIP(pSession, &handle);

    if (eStatus == XHTTP_COMPLETE)
    {
        const char *pUpgrade = XHTTP_GetHeader(&handle, "Upgrade");
        const char *pSecKey = XHTTP_GetHeader(&handle, "Sec-WebSocket-Key");

        if (!XAPI_HeaderHasTokenCI(pUpgrade, "websocket"))
        {
            XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_INVALID_REQUEST);
            XHTTP_Clear(&handle);
            return XEVENTS_DISCONNECT;
        }

        if (xstrused(pSecKey))
            xstrncpy(pSession->sKey, sizeof(pSession->sKey), pSecKey);

        if (!xstrused(pSession->sKey) && !pApi->bAllowMissingKey)
        {
            XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_MISSING_SEC_KEY);
            XHTTP_Clear(&handle);
            return XEVENTS_DISCONNECT;
        }

        pSession->bHandshakeStart = XTRUE;
        pSession->pPacket = &handle;

        int nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_HANDSHAKE_REQUEST);
        nRetVal = XAPI_StatusToEvent(pApi, nStatus);

        size_t nPacketSize = XHTTP_GetPacketSize(&handle);
        XByteBuffer_Advance(pBuffer, nPacketSize);
    }
    else if (eStatus != XHTTP_PARSED && eStatus != XHTTP_INCOMPLETE)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_HTTP, eStatus);
        nRetVal = XEVENTS_DISCONNECT;
    }
    else if (eStatus == XHTTP_INCOMPLETE && pBuffer->nUsed > pApi->nRxSize)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_HTTP, XHTTP_BIGCNT);
        nRetVal = XEVENTS_DISCONNECT;
    }

    pSession->pPacket = NULL;
    XHTTP_Clear(&handle);

    if (eStatus == XHTTP_COMPLETE &&
        nRetVal == XEVENTS_CONTINUE)
        nRetVal = XAPI_AnswerUpgrade(pApi, pSession);

    return nRetVal;
}

static int XAPI_ClientHandshake(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pSession != NULL), XSTDINV);

    if (!pSession->bHandshakeStart) return XEVENTS_CONTINUE;
    xbyte_buffer_t *pBuffer = &pSession->rxBuffer;
    xhttp_status_t eStatus = XHTTP_NONE;
    int nRetVal = XEVENTS_CONTINUE;

    xhttp_t handle;
    XHTTP_Init(&handle, XHTTP_DUMMY, XSTDNON);
    eStatus = XHTTP_ParseBuff(&handle, pBuffer);

    if (eStatus == XHTTP_COMPLETE)
    {
        const char *pUpgrade = XHTTP_GetHeader(&handle, "Upgrade");
        const char *pSecKey = XHTTP_GetHeader(&handle, "Sec-WebSocket-Accept");

        if (!XAPI_HeaderHasTokenCI(pUpgrade, "websocket"))
        {
            XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_INVALID_RESPONSE);
            XHTTP_Clear(&handle);
            return XEVENTS_DISCONNECT;
        }

        if (xstrused(pSecKey))
        {
            char *pLocalKey = XAPI_GetWSKey(pApi, pSession);
            if (pLocalKey == NULL)
            {
                XHTTP_Clear(&handle);
                return XEVENTS_DISCONNECT;
            }

            if (!xstrncmp(pLocalKey, pSecKey, strlen(pLocalKey)))
            {
                XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_INVALID_SEC_KEY);
                XHTTP_Clear(&handle);
                free(pLocalKey);

                pSession->bCancel = XTRUE;
                return XEVENTS_DISCONNECT;
            }

            free(pLocalKey);
        }
        else if (!pApi->bAllowMissingKey)
        {
            XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_MISSING_SEC_KEY);
            XHTTP_Clear(&handle);

            pSession->bCancel = XTRUE;
            return XEVENTS_DISCONNECT;
        }

        pSession->bHandshakeStart = XFALSE;
        pSession->bHandshakeDone = XTRUE;
        pSession->pPacket = &handle;

        int nStatus = XAPI_EnableEvent(pSession, XPOLLOUT);
        if (nStatus != XSTDOK)
        {
            pSession->pPacket = NULL;
            XHTTP_Clear(&handle);
            return XEVENTS_DISCONNECT;
        }

        nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_HANDSHAKE_RESPONSE);
        nRetVal = XAPI_StatusToEvent(pApi, nStatus);

        size_t nPacketSize = XHTTP_GetPacketSize(&handle);
        XByteBuffer_Advance(pBuffer, nPacketSize);
    }
    else if (eStatus != XHTTP_PARSED && eStatus != XHTTP_INCOMPLETE)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_HTTP, eStatus);
        nRetVal = XEVENTS_DISCONNECT;
    }
    else if (eStatus == XHTTP_INCOMPLETE && pBuffer->nUsed > pApi->nRxSize)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_HTTP, XHTTP_BIGCNT);
        nRetVal = XEVENTS_DISCONNECT;
    }

    pSession->pPacket = NULL;
    XHTTP_Clear(&handle);

    return nRetVal;
}

static void XAPI_ResetWSFragments(xapi_session_t *pSession)
{
    XCHECK_VOID_NL((pSession != NULL));
    XByteBuffer_Clear(&pSession->wsBuffer);
    pSession->bWSFragStart = XFALSE;
    pSession->eWSFragType = XWS_INVALID;
}

static xbool_t XAPI_IsWSControlFrame(xws_frame_type_t eType)
{
    return eType == XWS_PING ||
           eType == XWS_PONG ||
           eType == XWS_CLOSE;
}

static xbool_t XAPI_IsWSDataFrame(xws_frame_type_t eType)
{
    return eType == XWS_BINARY ||
           eType == XWS_TEXT;
}

static int XAPI_DispatchWSFrame(xapi_t *pApi, xapi_session_t *pSession, xws_frame_t *pFrame)
{
    XCHECK((pApi != NULL), XEVENTS_DISCONNECT);
    XCHECK((pSession != NULL), XEVENTS_DISCONNECT);
    XCHECK((pFrame != NULL), XEVENTS_DISCONNECT);

    pSession->pPacket = pFrame;
    int nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_READ);
    int nRetVal = XAPI_StatusToEvent(pApi, nStatus);

    pSession->pPacket = NULL;
    return nRetVal;
}

static int XAPI_HandleWS(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pSession != NULL), XSTDINV);

    xbyte_buffer_t *pBuffer = &pSession->rxBuffer;
    xws_status_t eStatus = XWS_ERR_NONE;
    int nRetVal = XEVENTS_CONTINUE;

    if (!pSession->bHandshakeDone)
    {
        if (pSession->eRole == XAPI_PEER)
            nRetVal = XAPI_ServerHandshake(pApi, pSession);
        else if (pSession->eRole == XAPI_CLIENT)
            nRetVal = XAPI_ClientHandshake(pApi, pSession);

        XCHECK_NL((nRetVal == XEVENTS_CONTINUE), nRetVal);
        XCHECK_NL((pBuffer->nUsed > 0), XEVENTS_CONTINUE);
        XCHECK_NL(pSession->bHandshakeDone, XEVENTS_CONTINUE);
    }

    xws_frame_t frame;
    eStatus = XWebFrame_ParseBuff(&frame, pBuffer);

    if (eStatus == XWS_FRAME_COMPLETE)
    {
        const uint8_t *pPayload = XWebFrame_GetPayload(&frame);
        size_t nPayloadLength = XWebFrame_GetPayloadLength(&frame);
        size_t nFrameLength = XWebFrame_GetFrameLength(&frame);
        xbool_t bControl = XAPI_IsWSControlFrame(frame.eType);
        xbool_t bData = XAPI_IsWSDataFrame(frame.eType);
        xbool_t bContinuation = frame.eType == XWS_CONTINUATION;

        if (bControl)
        {
            // Control frames must not be fragmented
            nRetVal = XAPI_DispatchWSFrame(pApi, pSession, &frame);
        }
        else if (pSession->bWSFragStart)
        {
            if (!bContinuation)
            {
                XAPI_ResetWSFragments(pSession);
                XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_FRAME_INVALID);
                nRetVal = XEVENTS_DISCONNECT;
            }
            else if (nPayloadLength && XByteBuffer_Add(&pSession->wsBuffer, pPayload, nPayloadLength) <= 0)
            {
                XAPI_ResetWSFragments(pSession);
                XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_ERR_ALLOC);
                nRetVal = XEVENTS_DISCONNECT;
            }
            else if (pSession->wsBuffer.nUsed > pApi->nRxSize)
            {
                XAPI_ResetWSFragments(pSession);
                XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_FRAME_TOOBIG);
                nRetVal = XEVENTS_DISCONNECT;
            }
            else if (frame.bFin)
            {
                xws_frame_t assembled;
                xws_status_t eAssembled = XWebFrame_Create(
                    &assembled,
                    pSession->wsBuffer.pData,
                    pSession->wsBuffer.nUsed,
                    pSession->eWSFragType,
                    XFALSE,
                    XTRUE
                );

                if (eAssembled != XWS_ERR_NONE)
                {
                    XAPI_ResetWSFragments(pSession);
                    XAPI_ErrorCb(pApi, pSession, XAPI_WS, eAssembled);
                    nRetVal = XEVENTS_DISCONNECT;
                }
                else
                {
                    XAPI_ResetWSFragments(pSession);
                    nRetVal = XAPI_DispatchWSFrame(pApi, pSession, &assembled);
                    XWebFrame_Clear(&assembled);
                }
            }
        }
        else if (!frame.bFin)
        {
            if (!bData)
            {
                XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_FRAME_INVALID);
                nRetVal = XEVENTS_DISCONNECT;
            }
            else if (nPayloadLength && XByteBuffer_Add(&pSession->wsBuffer, pPayload, nPayloadLength) <= 0)
            {
                XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_ERR_ALLOC);
                nRetVal = XEVENTS_DISCONNECT;
            }
            else
            {
                pSession->bWSFragStart = XTRUE;
                pSession->eWSFragType = frame.eType;
            }
        }
        else if (bContinuation)
        {
            XAPI_ErrorCb(pApi, pSession, XAPI_WS, XWS_FRAME_INVALID);
            nRetVal = XEVENTS_DISCONNECT;
        }
        else
        {
            nRetVal = XAPI_DispatchWSFrame(pApi, pSession, &frame);
        }

        XByteBuffer_Advance(pBuffer, nFrameLength);
    }
    else if (eStatus != XWS_FRAME_PARSED && eStatus != XWS_FRAME_INCOMPLETE)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_WS, eStatus);
        nRetVal = XEVENTS_DISCONNECT;
    }
    else if (eStatus == XWS_FRAME_INCOMPLETE && pBuffer->nUsed > pApi->nRxSize)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_WS, XPACKET_BIGDATA);
        nRetVal = XEVENTS_DISCONNECT;
    }

    pSession->pPacket = NULL;
    XWebFrame_Clear(&frame);

    if (eStatus == XWS_FRAME_COMPLETE &&
        nRetVal == XEVENTS_CONTINUE &&
        XByteBuffer_HasData(pBuffer))
        return XAPI_HandleWS(pApi, pSession);

    return nRetVal;
}

static int XAPI_HandleRAW(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pSession != NULL), XSTDINV);

    xbyte_buffer_t *pBuffer = &pSession->rxBuffer;
    pSession->pPacket = (xbyte_buffer_t*)pBuffer;

    int nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_READ);
    int nRetVal = XAPI_StatusToEvent(pApi, nStatus);

    if (!pSession->bKeepRxBuffer) XByteBuffer_Clear(pBuffer);
    pSession->pPacket = NULL;

    return nRetVal;
}

static int XAPI_Read(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pSession != NULL), XEVENTS_DISCONNECT);
    xsock_t *pSock = (xsock_t*)&pSession->sock;
    uint8_t sBuffer[XAPI_RX_SIZE];

    int nBytes = XSock_Read(pSock, sBuffer, sizeof(sBuffer));
    if (nBytes <= 0)
    {
        if (pSock->eStatus == XSOCK_EOF)
        {
            XAPI_StatusCb(pApi, pSession, XAPI_SOCK, pSock->eStatus);
            return XEVENTS_DISCONNECT;
        }
        else if (pSock->eStatus == XSOCK_WANT_WRITE)
        {
            pSession->bReadOnWrite = XTRUE;
            int nEvents = pSession->nEvents;

            XSTATUS nStatus = XAPI_SetEvents(pSession, XPOLLOUT);
            XCHECK((nStatus > XSTDNON), XEVENTS_DISCONNECT);

            pSession->nEvents = nEvents;
            return XEVENTS_CONTINUE;
        }
        else if (pSock->eStatus == XSOCK_WANT_READ)
        {
            return XEVENTS_CONTINUE;
        }

        XAPI_ErrorCb(pApi, pSession, XAPI_SOCK, pSock->eStatus);
        return XEVENTS_DISCONNECT;
    }

    if (XByteBuffer_Add(&pSession->rxBuffer, sBuffer, nBytes) <= 0)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_SELF, XAPI_ERR_ALLOC);
        return XEVENTS_DISCONNECT;
    }

    switch (pSession->eType)
    {
        case XAPI_HTTP: return XAPI_HandleHTTP(pApi, pSession);
        case XAPI_MDTP: return XAPI_HandleMDTP(pApi, pSession);
        case XAPI_SOCK: return XAPI_HandleRAW(pApi, pSession);
        case XAPI_WS: return XAPI_HandleWS(pApi, pSession);
        default: break;
    }

    return XEVENTS_DISCONNECT;
}

static int XAPI_Accept(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pApi != NULL), XEVENTS_DISCONNECT);
    XCHECK((pSession != NULL), XEVENTS_DISCONNECT);
    XCHECK(pApi->bHaveEvents, XEVENTS_DISCONNECT);

    xapi_session_t *pPeerData = XAPI_NewData(pApi, pSession->eType);
    if (pPeerData == NULL)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_SELF, XAPI_ERR_ALLOC);
        return XEVENTS_CONTINUE;
    }

    xevents_t *pEvents = &pApi->events;
    xsock_t *pListener = &pSession->sock;
    xsock_t *pNewSock = &pPeerData->sock;

    if (XSock_Accept(pListener, pNewSock) == XSOCK_INVALID)
    {
        xsock_status_t status = pListener->eStatus != XSOCK_ERR_NONE ?
                                pListener->eStatus : pNewSock->eStatus;

        if (status != XSOCK_WANT_READ && status != XSOCK_WANT_WRITE)
            XAPI_ErrorCb(pApi, pPeerData, XAPI_SOCK, status);

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
    pPeerData->nPort = pSession->nPort;
    pPeerData->eRole = XAPI_PEER;

    xevent_data_t *pEventData = XEvents_RegisterEvent(pEvents, pPeerData, pNewSock->nFD, XSTDNON, XAPI_PEER);
    if (pEventData == NULL)
    {
        XAPI_ErrorCb(pApi, pPeerData, XAPI_SELF, XAPI_ERR_REGISTER);
        XAPI_FreeData(&pPeerData);
        return XEVENTS_CONTINUE;
    }

    pPeerData->pSessionData = NULL;
    pPeerData->pEvData = pEventData;
    pPeerData->nEvents = XSTDNON;

    if (XAPI_ServiceCb(pApi, pPeerData, XAPI_CB_ACCEPTED) < 0)
    {
        XEvents_Delete(pEvents, pEventData);
        return XEVENTS_CONTINUE;
    }

    return XEVENTS_ACCEPT;
}

static int XAPI_Write(xapi_t *pApi, xapi_session_t *pSession)
{
    XCHECK((pApi != NULL), XEVENTS_DISCONNECT);
    XCHECK((pSession != NULL), XEVENTS_DISCONNECT);
    XCHECK((!pSession->bCancel), XEVENTS_DISCONNECT);

    xbyte_buffer_t *pBuffer = &pSession->txBuffer;
    XCHECK_NL(pBuffer->nUsed, XEVENTS_CONTINUE);

    xsock_t *pSock = &pSession->sock;
    XSTATUS nStatus = XSTDNON;

    int nSent = XSock_Write(pSock, pBuffer->pData, pBuffer->nUsed);
    if (nSent <= 0)
    {
        if (pSock->eStatus == XSOCK_WANT_READ)
        {
            pSession->bWriteOnRead = XTRUE;
            int nEvents = pSession->nEvents;

            XSTATUS nStatus = XAPI_SetEvents(pSession, XPOLLIN);
            XCHECK((nStatus > XSTDNON), XEVENTS_DISCONNECT);

            pSession->nEvents = nEvents;
            return XEVENTS_CONTINUE;
        }
        else if (pSock->eStatus == XSOCK_WANT_WRITE)
        {
            return XEVENTS_CONTINUE;
        }

        XAPI_ErrorCb(pApi, pSession, XAPI_SOCK, pSock->eStatus);
        return XEVENTS_DISCONNECT;
    }

    if (!XByteBuffer_Advance(pBuffer, nSent))
    {
        nStatus = XAPI_DisableEvent(pSession, XPOLLOUT);
        XCHECK((nStatus > XSTDNON), XEVENTS_DISCONNECT);

        if (pSession->eType != XAPI_WS || pSession->bHandshakeDone)
        {
            nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_COMPLETE);
            XCHECK_NL((nStatus >= XSTDNON), XEVENTS_DISCONNECT);
        }
        else if (pSession->eRole == XAPI_CLIENT)
        {
            nStatus = XAPI_EnableEvent(pSession, XPOLLIN);
            XCHECK((nStatus > XSTDNON), XEVENTS_DISCONNECT);
        }

        if (pSession->eRole != XAPI_CLIENT &&
            pSession->eType == XAPI_WS &&
            pSession->bHandshakeStart)
        {
            pSession->bHandshakeStart = XFALSE;
            pSession->bHandshakeDone = XTRUE;
        }
    }

    return XAPI_StatusToEvent(pApi, nStatus);
}

static int XAPI_WriteEvent(xevents_t *pEvents, xevent_data_t *pEvData)
{
    XCHECK((pEvents != NULL), XEVENTS_DISCONNECT);
    XCHECK((pEvData != NULL), XEVENTS_DISCONNECT);
    XCHECK((pEvData->pContext != NULL), XEVENTS_DISCONNECT);

    xapi_session_t *pSession = (xapi_session_t*)pEvData->pContext;
    if (pSession->bCancel) return XEVENTS_DISCONNECT;

    xapi_t *pApi = (xapi_t*)pEvents->pUserSpace;
    XCHECK((pApi != NULL), XEVENTS_DISCONNECT);

    xbyte_buffer_t *pBuffer = &pSession->txBuffer;
    XSTATUS nStatus = XSTDNON;

    if (pSession->bReadOnWrite)
    {
        XSTATUS nStatus = XAPI_RollbackEvents(pSession);
        XCHECK((nStatus > XSTDNON), XEVENTS_DISCONNECT);

        pSession->bReadOnWrite = XFALSE;
        return XAPI_Read(pApi, pSession);
    }

    if (pSession->eRole == XAPI_CUSTOM)
    {
        nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_WRITE);
        return XAPI_StatusToEvent(pApi, nStatus);
    }

    if (pSession->eRole == XAPI_CLIENT &&
        pSession->eType == XAPI_WS &&
        !pSession->bHandshakeStart &&
        !pSession->bHandshakeDone)
    {
        int nRetVal = XAPI_RequestUpgrade(pApi, pSession);
        XCHECK_NL((nRetVal == XEVENTS_CONTINUE), nRetVal);
        XCHECK((pBuffer->nUsed > 0), XEVENTS_DISCONNECT);
    }

    if (!pBuffer->nUsed)
    {
        nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_WRITE);
        int nRetVal = XAPI_StatusToEvent(pApi, nStatus);

        XCHECK_NL((nRetVal == XEVENTS_CONTINUE), nRetVal);
        if (!pBuffer->nUsed)
        {
            if (pSession->eRole != XAPI_CUSTOM &&
                (pSession->nEvents & XPOLLOUT))
            {
                nStatus = XAPI_DisableEvent(pSession, XPOLLOUT);
                XCHECK((nStatus > XSTDNON), XEVENTS_DISCONNECT);
            }

            return XEVENTS_CONTINUE;
        }
    }

    return XAPI_Write(pApi, pSession);
}

static int XAPI_ReadEvent(xevents_t *pEvents, xevent_data_t *pEvData)
{
    XCHECK((pEvents != NULL), XEVENTS_DISCONNECT);
    XCHECK((pEvData != NULL), XEVENTS_DISCONNECT);
    XCHECK((pEvData->pContext != NULL), XEVENTS_DISCONNECT);

    xapi_t *pApi = (xapi_t*)pEvents->pUserSpace;
    XCHECK((pApi != NULL), XEVENTS_DISCONNECT);

    xapi_session_t *pSession = (xapi_session_t*)pEvData->pContext;
    if (pSession->bCancel) return XEVENTS_DISCONNECT;

    if (pSession->bWriteOnRead)
    {
        XSTATUS nStatus = XAPI_RollbackEvents(pSession);
        XCHECK((nStatus > XSTDNON), XEVENTS_DISCONNECT);

        pSession->bWriteOnRead = XFALSE;
        return XAPI_Write(pApi, pSession);
    }

    if (pSession->eRole == XAPI_CUSTOM)
    {
        int nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_READ);
        return XAPI_StatusToEvent(pApi, nStatus);
    }
    else if (pSession->eRole == XAPI_SERVER)
    {
        return XAPI_Accept(pApi, pSession);
    }
    else if (pSession->eRole == XAPI_PEER ||
             pSession->eRole == XAPI_CLIENT)
    {
        return XAPI_Read(pApi, pSession);
    }

    XAPI_ErrorCb(pApi, pSession, XAPI_SELF, XAPI_INVALID_ROLE);
    return XEVENTS_DISCONNECT;
}

static int XAPI_HungedEvent(xapi_t *pApi, xevent_data_t *pEvData)
{
    XCHECK((pApi != NULL), XEVENTS_DISCONNECT);
    xapi_session_t *pSession = NULL;
    if (pEvData != NULL) pSession = (xapi_session_t*)pEvData->pContext;
    XAPI_StatusCb(pApi, pSession, XAPI_SELF, XAPI_HUNGED);
    return XEVENTS_DISCONNECT;
}

static int XAPI_ClosedEvent(xapi_t *pApi, xevent_data_t *pEvData)
{
    XCHECK((pApi != NULL), XEVENTS_DISCONNECT);
    xapi_session_t *pSession = NULL;
    if (pEvData != NULL) pSession = (xapi_session_t*)pEvData->pContext;
    XAPI_StatusCb(pApi, pSession, XAPI_SELF, XAPI_CLOSED);
    return XEVENTS_DISCONNECT;
}

static int XAPI_TimeoutEvent(xapi_t *pApi, xevent_data_t *pEvData)
{
    XCHECK((pApi != NULL), XEVENTS_DISCONNECT);

    xapi_session_t *pSession = NULL;
    if (pEvData != NULL) pSession = (xapi_session_t*)pEvData->pContext;

    int nStatus = XAPI_ServiceCb(pApi, pSession, XAPI_CB_TIMEOUT);
    if (nStatus == XAPI_CONTINUE) return XEVENTS_CONTINUE;

    int nEvent = XAPI_StatusToEvent(pApi, nStatus);
    if (nEvent <= XEVENTS_DISCONNECT)
    {
        XAPI_Disconnect(pSession);
        return XEVENTS_RELOOP;
    }

    // Delete timer event only
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

static xevent_status_t XAPI_TickEvent(xapi_t *pApi)
{
    XCHECK((pApi != NULL), XEVENTS_EINVALID);

    int nStatus = XAPI_ServiceCb(pApi, NULL, XAPI_CB_TICK);
    int nEvent = XAPI_StatusToEvent(pApi, nStatus);

    return (nEvent <= XEVENTS_DISCONNECT) ?
        XEVENTS_EBREAK : XEVENTS_SUCCESS;
}

static int XAPI_EventCallback(void *events, void* data, XSOCKET fd, xevent_cb_type_t reason)
{
    XCHECK((events != NULL), XEVENTS_CONTINUE);
    xevent_data_t *pData = (xevent_data_t*)data;
    xevents_t *pEvents = (xevents_t*)events;
    xapi_t *pApi = (xapi_t*)pEvents->pUserSpace;

    switch(reason)
    {
        case XEVENT_CB_USER:
            return XAPI_UserEvent(pApi);
        case XEVENT_CB_INTERRUPT:
            return XAPI_InterruptEvent(pApi);
        case XEVENT_CB_CLEAR:
            return XAPI_ClearEvent(pApi, pData);
        case XEVENT_CB_HUNGED:
            return XAPI_HungedEvent(pApi, pData);
        case XEVENT_CB_CLOSED:
            return XAPI_ClosedEvent(pApi, pData);
        case XEVENT_CB_TIMEOUT:
            return XAPI_TimeoutEvent(pApi, pData);
        case XEVENT_CB_READ:
            return XAPI_ReadEvent(pEvents, pData);
        case XEVENT_CB_WRITE:
            return XAPI_WriteEvent(pEvents, pData);
        case XEVENT_CB_DESTROY:
            XAPI_StatusCb(pApi, NULL, XAPI_SELF, XAPI_DESTROY);
            break;
        default:
            break;
    }

    return XEVENTS_CONTINUE;
}

xevents_t* XAPI_GetOrCreateEvents(xapi_t *pApi)
{
    XCHECK((pApi != NULL), NULL);
    xevents_t *pEvents = &pApi->events;
    if (pApi->bHaveEvents) return pEvents;

    xevent_status_t status;
    status = XEvents_Create(pEvents, XSTDNON, pApi,
             XAPI_EventCallback, pApi->bUseHashMap);

    if (status != XEVENTS_SUCCESS)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_EVENT, status);
        return NULL;
    }

    pApi->bHaveEvents = XTRUE;
    return pEvents;
}

XSTATUS XAPI_Init(xapi_t *pApi, xapi_cb_t callback, void *pUserCtx)
{
    XCHECK((pApi != NULL), XSTDINV);
    pApi->nSessionCounter = XSTDNON;
    pApi->bAllowMissingKey = XFALSE;
    pApi->bHaveEvents = XFALSE;
    pApi->bUseHashMap = XTRUE;
    pApi->bIsWorker = XFALSE;
    pApi->bSetAffinity = XFALSE;
    pApi->nWorkerCount = XSTDNON;
    pApi->nWorkerIndex = XSTDERR;
    pApi->nCoreIndex = XSTDERR;
    pApi->pWorkerPIDs = NULL;
    pApi->nWorkerPID = XSTDNON;
    pApi->callback = callback;
    pApi->pUserCtx = pUserCtx;
    pApi->nRxSize = XAPI_RX_MAX;
    return XSTDOK;
}

XSTATUS XAPI_SetWorkerAffinity(xapi_t *pApi, xbool_t bEnable)
{
    XCHECK((pApi != NULL), XSTDINV);
    pApi->bSetAffinity = bEnable;
    return XSTDOK;
}

XSTATUS XAPI_InitWorkers(xapi_t *pApi, size_t nWorkers, xbool_t bSetAffinity)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((nWorkers > 0), XSTDINV);

#if defined(_XEVENTS_USE_EPOLL)
    XCHECK((pApi->bHaveEvents), XSTDINV);
    XCHECK((pApi->events.nEventCount > 0), XSTDINV);
    XCHECK((pApi->bUseHashMap && pApi->events.bUseHash), XSTDINV);
    XCHECK((pApi->pWorkerPIDs == NULL && !pApi->nWorkerCount), XSTDEXC);

    xpid_t *pWorkerPIDs = (xpid_t*)calloc(nWorkers, sizeof(xpid_t));
    if (pWorkerPIDs == NULL)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_SELF, XAPI_ERR_ALLOC);
        return XSTDERR;
    }

    pApi->bSetAffinity = bSetAffinity;
    pApi->pWorkerPIDs = pWorkerPIDs;
    pApi->nWorkerCount = nWorkers;
    pApi->nWorkerIndex = XSTDERR;
    pApi->nCoreIndex = XSTDERR;
    pApi->bIsWorker = XFALSE;

    size_t i = 0;
    for (i = 0; i < nWorkers; i++)
    {
        XSTATUS nStatus = XAPI_SpawnWorker(pApi, i);
        if (nStatus == XSTDERR)
        {
            XAPI_StopWorkerPIDs(pWorkerPIDs, i, SIGTERM);
            XAPI_WaitWorkerPIDs(pWorkerPIDs, i);

            free(pApi->pWorkerPIDs);
            pApi->pWorkerPIDs = NULL;

            pApi->nWorkerPID = XSTDNON;
            pApi->nWorkerCount = XSTDNON;
            pApi->nWorkerIndex = XSTDERR;
            pApi->nCoreIndex = XSTDERR;
            pApi->bIsWorker = XFALSE;
            return XSTDERR;
        }

        if (nStatus == XSTDUSR)
            return XSTDUSR;
    }

    return XSTDOK;
#else
    (void)bSetAffinity;
    return XSTDNON;
#endif
}

XSTATUS XAPI_SetRxSize(xapi_t *pApi, size_t nSize)
{
    XCHECK((pApi != NULL), XSTDINV);
    pApi->nRxSize = nSize ? nSize : XAPI_RX_MAX;
    return XSTDOK;
}

void XAPI_Destroy(xapi_t *pApi)
{
    XCHECK_VOID_NL((pApi != NULL));

    if (pApi->pWorkerPIDs != NULL)
    {
        free(pApi->pWorkerPIDs);
        pApi->pWorkerPIDs = NULL;
    }

    pApi->nWorkerCount = XSTDNON;
    pApi->nWorkerIndex = XSTDERR;
    pApi->nCoreIndex = XSTDERR;
    pApi->nWorkerPID = XSTDNON;
    pApi->bIsWorker = XFALSE;

    XCHECK_VOID_NL(pApi->bHaveEvents);
    xevents_t *pEvents = &pApi->events;
    XEvents_Destroy(pEvents);
}

xevent_status_t XAPI_Service(xapi_t *pApi, int nTimeoutMs)
{
    XCHECK((pApi != NULL), XEVENTS_EINVALID);
    XCHECK(pApi->bHaveEvents, XEVENTS_EINVALID);

    xevents_t *pEvents = &pApi->events;
    xevent_status_t eStatus = XEvents_Service(pEvents, nTimeoutMs);
    if (eStatus != XEVENTS_SUCCESS) return eStatus;
    return XAPI_TickEvent(pApi);
}

void XAPI_InitEndpoint(xapi_endpoint_t *pEndpt)
{
    XCHECK_VOID_NL((pEndpt != NULL));
    XSock_InitCert(&pEndpt->certs);
    pEndpt->pSessionData = NULL;
    pEndpt->nEvents = XSTDNON;
    pEndpt->eRole = XAPI_INACTIVE;
    pEndpt->eType = XAPI_NONE;
    pEndpt->nPort = XSTDNON;
    pEndpt->pAddr = NULL;
    pEndpt->pUri = NULL;
    pEndpt->bTLS = XFALSE;
    pEndpt->bUnix = XFALSE;
    pEndpt->bForce = XFALSE;
    pEndpt->bExclusive = XTRUE;
    pEndpt->nFD = XSOCK_INVALID;
}

XSTATUS XAPI_Listen(xapi_t *pApi, xapi_endpoint_t *pEndpt)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pEndpt != NULL), XSTDINV);
    XCHECK((pEndpt->eType != XAPI_NONE), XSTDINV);
    XCHECK((pEndpt->pAddr != NULL), XSTDINV);

    if (!pEndpt->nPort && !pEndpt->bUnix)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_SELF, XAPI_INVALID_ARGS);
        return XSTDINV;
    }

    xapi_session_t *pSession = XAPI_NewData(pApi, pEndpt->eType);
    if (pSession == NULL)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_SELF, XAPI_ERR_ALLOC);
        return XSTDERR;
    }

    const char *pEndpointUri = pEndpt->pUri ? pEndpt->pUri : "/";
    uint32_t nEvents = pEndpt->nEvents ? pEndpt->nEvents : XPOLLIN;
    xsock_t *pSock = &pSession->sock;

    xstrncpy(pSession->sAddr, sizeof(pSession->sAddr), pEndpt->pAddr);
    xstrncpy(pSession->sUri, sizeof(pSession->sUri), pEndpointUri);

    pSession->pSessionData = pEndpt->pSessionData;
    pSession->nPort = pEndpt->nPort;
    pSession->eRole = XAPI_SERVER;

    uint32_t nFlags = XSOCK_SERVER | XSOCK_REUSEADDR | XSOCK_NB;
    if (pEndpt->bForce) nFlags |= XSOCK_FORCE;
    if (pEndpt->bTLS) nFlags |= XSOCK_SSL;
    if (pEndpt->bUnix) nFlags |= XSOCK_UNIX;
    else nFlags |= XSOCK_TCP;

    XSock_Create(pSock, nFlags, pEndpt->pAddr, pEndpt->nPort);
    if (pEndpt->bTLS) XSock_SetSSLCert(pSock, &pEndpt->certs);

    if (pSock->nFD == XSOCK_INVALID)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_SOCK, pSock->eStatus);
        XAPI_FreeData(&pSession);
        return XSTDERR;
    }

    /* Create event instance */
    xevents_t *pEvents = XAPI_GetOrCreateEvents(pApi);
    if (pEvents == NULL)
    {
        XAPI_FreeData(&pSession);
        return XSTDERR;
    }

#ifdef EPOLLEXCLUSIVE
    if (pEndpt->bExclusive)
        nEvents |= EPOLLEXCLUSIVE;
#endif

    /* Add listener socket to the event instance */
    xevent_data_t *pEvData = XEvents_RegisterEvent(pEvents, pSession, pSock->nFD, nEvents, XAPI_SERVER);
    if (pEvData == NULL)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_SELF, XAPI_ERR_REGISTER);
        XAPI_FreeData(&pSession);
        return XSTDERR;
    }

    pSession->pEvData = pEvData;
    pSession->nEvents = nEvents;

    if (XAPI_ServiceCb(pApi, pSession, XAPI_CB_LISTENING) < 0)
    {
        XEvents_Delete(pEvents, pSession->pEvData);
        pSession->pEvData = NULL;
        return XSTDERR;
    }

    return XSTDOK;
}

XSTATUS XAPI_Connect(xapi_t *pApi, xapi_endpoint_t *pEndpt)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pEndpt != NULL), XSTDINV);
    XCHECK((pEndpt->eType != XAPI_SELF), XSTDINV);
    XCHECK((pEndpt->pAddr != NULL), XSTDINV);

    if (!pEndpt->nPort && !pEndpt->bUnix)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_SELF, XAPI_INVALID_ARGS);
        return XSTDINV;
    }

    xapi_session_t *pSession = XAPI_NewData(pApi, pEndpt->eType);
    if (pSession == NULL)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_SELF, XAPI_ERR_ALLOC);
        return XSTDERR;
    }

    const char *pEndpointUri = pEndpt->pUri ? pEndpt->pUri : "/";
    uint32_t nEvents = pEndpt->nEvents ? pEndpt->nEvents : XPOLLIO;
    xsock_t *pSock = &pSession->sock;

    xstrncpy(pSession->sAddr, sizeof(pSession->sAddr), pEndpt->pAddr);
    xstrncpy(pSession->sUri, sizeof(pSession->sUri), pEndpointUri);

    pSession->pSessionData = pEndpt->pSessionData;
    pSession->nPort = pEndpt->nPort;
    pSession->eRole = XAPI_CLIENT;

    uint32_t nFlags = XSOCK_CLIENT | XSOCK_NB;
    if (pEndpt->bTLS) nFlags |= XSOCK_SSL;
    if (pEndpt->bUnix) nFlags |= XSOCK_UNIX;
    else nFlags |= XSOCK_TCP;

    XSock_Create(pSock, nFlags, pEndpt->pAddr, pEndpt->nPort);
    if (pEndpt->bTLS) XSock_SetSSLCert(pSock, &pEndpt->certs);
    if (pSock->nFD == XSOCK_INVALID)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_SOCK, pSock->eStatus);
        XAPI_FreeData(&pSession);
        return XSTDERR;
    }

    /* Create event instance */
    xevents_t *pEvents = XAPI_GetOrCreateEvents(pApi);
    if (pEvents == NULL)
    {
        XAPI_FreeData(&pSession);
        return XSTDERR;
    }

    /* Add listener socket to the event instance */
    xevent_data_t *pEvData = XEvents_RegisterEvent(pEvents, pSession, pSock->nFD, nEvents, XAPI_CLIENT);
    if (pEvData == NULL)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_SELF, XAPI_ERR_REGISTER);
        XAPI_FreeData(&pSession);
        return XSTDERR;
    }

    pSession->pEvData = pEvData;
    pSession->nEvents = nEvents;

    if (XAPI_ServiceCb(pApi, pSession, XAPI_CB_CONNECTED) < 0)
    {
        XEvents_Delete(pEvents, pSession->pEvData);
        pSession->pEvData = NULL;
        return XSTDERR;
    }

    return XSTDOK;
}

XSTATUS XAPI_AddEvent(xapi_t *pApi, xapi_endpoint_t *pEndpt)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pEndpt != NULL), XSTDINV);
    XCHECK((pEndpt->nFD != XSOCK_INVALID), XSTDINV);
    XCHECK((pEndpt->eType != XAPI_SELF), XSTDINV);

    xapi_session_t *pSession = XAPI_NewData(pApi, pEndpt->eType);
    if (pSession == NULL)
    {
        XAPI_ErrorCb(pApi, NULL, XAPI_SELF, XAPI_ERR_ALLOC);
        return XSTDERR;
    }

    const char *pEndpointUri = pEndpt->pUri ? pEndpt->pUri : "/";
    uint32_t nEvents = pEndpt->nEvents ? pEndpt->nEvents : XPOLLIO;

    xstrncpy(pSession->sUri, sizeof(pSession->sUri), pEndpointUri);
    pSession->pSessionData = pEndpt->pSessionData;
    pSession->nPort = pEndpt->nPort;
    pSession->eRole = pEndpt->eRole;

    uint32_t nFlags = XSOCK_EVENT | XSOCK_NB;
    if (pEndpt->bTLS) nFlags |= XSOCK_SSL;
    if (pEndpt->bUnix) nFlags |= XSOCK_UNIX;
    else nFlags |= XSOCK_TCP;

    xsock_t *pSock = &pSession->sock;
    XSock_Init(pSock, nFlags, pEndpt->nFD);

    /* Create event instance */
    xevents_t *pEvents = XAPI_GetOrCreateEvents(pApi);
    if (pEvents == NULL)
    {
        XAPI_FreeData(&pSession);
        return XSTDERR;
    }

    /* Add listener socket to the event instance */
    xevent_data_t *pEvData = XEvents_RegisterEvent(pEvents, pSession, pSock->nFD, nEvents, (int)pEndpt->eRole);
    if (pEvData == NULL)
    {
        XAPI_ErrorCb(pApi, pSession, XAPI_SELF, XAPI_ERR_REGISTER);
        XAPI_FreeData(&pSession);
        return XSTDERR;
    }

    pSession->pEvData = pEvData;
    pSession->nEvents = nEvents;

    if (XAPI_ServiceCb(pApi, pSession, XAPI_CB_REGISTERED) < 0)
    {
        XEvents_Delete(pEvents, pSession->pEvData);
        pSession->pEvData = NULL;
        return XSTDERR;
    }

    return XSTDOK;
}

XSTATUS XAPI_AddPeer(xapi_t *pApi, xapi_endpoint_t *pEndpt)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pEndpt != NULL), XSTDINV);
    pEndpt->eRole = XAPI_PEER;
    return XAPI_AddEvent(pApi, pEndpt);
}

XSTATUS XAPI_AddEndpoint(xapi_t *pApi, xapi_endpoint_t *pEndpt)
{
    XCHECK((pApi != NULL), XSTDINV);
    XCHECK((pEndpt != NULL), XSTDINV);

    switch (pEndpt->eRole)
    {
        case XAPI_PEER: return XAPI_AddEvent(pApi, pEndpt);
        case XAPI_SERVER: return XAPI_Listen(pApi, pEndpt);
        case XAPI_CLIENT: return XAPI_Connect(pApi, pEndpt);
        case XAPI_CUSTOM: return XAPI_AddEvent(pApi, pEndpt);
        default: break;
    }

    return XSTDERR;
}
