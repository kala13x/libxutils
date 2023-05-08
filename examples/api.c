/*!
 *  @file libxutils/examples/events.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of high performance event based non-blocking HTTP server.
 * The xUtils library will use poll() or epoll() depending on the operating system.
 */

#include <xutils/xstd.h>
#include <xutils/xver.h>
#include <xutils/xlog.h>
#include <xutils/xsig.h>
#include <xutils/api.h>

static int g_nInterrupted = 0;

void signal_callback(int sig)
{
    if (sig == SIGINT) printf("\n");
    g_nInterrupted = 1;
}

void print_status(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    const char *pStr = XAPI_GetStatus(pCtx);
    int nFD = pData ? (int)pData->nFD : XSTDERR;

    if (pCtx->eCbType == XAPI_CB_STATUS)
        xlogn("%s: fd(%d)", pStr, nFD);
    else if (pCtx->eCbType == XAPI_CB_ERROR)
        xloge("%s: fd(%d), errno(%d)", pStr, nFD, errno);
}

int handle_request(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xhttp_t *pHandle = (xhttp_t*)pData->pPacket;

    xlogn("Received request: fd(%d), buff(%zu)",
        (int)pData->nFD, pHandle->rawData.nUsed);

    char *pHeader = XHTTP_GetHeaderRaw(pHandle);
    if (pHeader != NULL)
    {
        xlogi("Raw request header:\n\n%s", pHeader);
        free(pHeader);
    }

    return XAPI_SetEvents(pData, XPOLLOUT);
}

int write_data(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xhttp_t handle;
    if (XHTTP_InitResponse(&handle, 200, NULL) <= 0)
    {
        xloge("Failed to initialize HTTP response: %s", strerror(errno));
        return XSTDERR;
    }

    if (XHTTP_AddHeader(&handle, "Server", "xutils/%s", XUtils_VersionShort()) < 0 ||
        XHTTP_AddHeader(&handle, "Content-Type", "text/plain") < 0)
    {
        xloge("Failed to setup HTTP headers: %s", strerror(errno));
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    char sBody[XSTR_TINY];
    size_t nLen = xstrncpy(sBody, sizeof(sBody), "Here is your response.");

    if (XHTTP_Assemble(&handle, (const uint8_t*)sBody, nLen) == NULL)
    {
        xloge("Failed to assemble HTTP response: %s", strerror(errno));
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    xlogn("Sending response: fd(%d), buff(%zu)",
            (int)pData->nFD, handle.rawData.nUsed);

    char *pHeader = XHTTP_GetHeaderRaw(&handle);
    if (pHeader != NULL)
    {
        xlogi("Raw response header:\n\n%s", pHeader);
        free(pHeader);
    }

    XByteBuffer_AddBuff(&pData->txBuffer, &handle.rawData);
    XHTTP_Clear(&handle);

    return XSTDOK;
}

int http_callback(xhttp_t *pHttp, xhttp_ctx_t *pCbCtx)
{
    xapi_data_t *pData = (xapi_data_t*)pHttp->pUserCtx;
    const char *pMessage = (const char*)pCbCtx->pData;

    switch (pCbCtx->eCbType)
    {
        case XHTTP_STATUS:
            xlogd("%s: fd(%d)", pMessage, (int)pData->nFD);
            return XSTDOK;
        case XHTTP_ERROR:
            xloge("%s: fd(%d)", pMessage, (int)pData->nFD);
            return XSTDERR;
        default:
            break;
    }

    return XSTDUSR;
}

int init_data(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xhttp_t *pHandle = (xhttp_t*)pData->pPacket;
    uint16_t nCallbacks = XHTTP_ERROR | XHTTP_STATUS;
    XHTTP_SetCallback(pHandle, http_callback, pData, nCallbacks);
    xlogn("Accepted connection: fd(%d)", (int)pData->nFD);
    return XAPI_SetEvents(pData, XPOLLIN);
}

int service_callback(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    switch (pCtx->eCbType)
    {
        case XAPI_CB_ERROR:
        case XAPI_CB_STATUS:
            print_status(pCtx, pData);
            break;
        case XAPI_CB_READ:
            return handle_request(pCtx, pData);
        case XAPI_CB_WRITE:
            return write_data(pCtx, pData);
        case XAPI_CB_ACCEPTED:
            return init_data(pCtx, pData);
        case XAPI_CB_CLOSED:
            xlogn("Connection closed: fd(%d)", (int)pData->nFD);
            break;
        case XAPI_CB_COMPLETE:
            xlogn("Response sent: fd(%d)", (int)pData->nFD);
            return XSTDERR;
        case XAPI_CB_INTERRUPT:
            if (g_nInterrupted) return XSTDERR;
            break;
        default:
            break;
    }

    return XSTDOK;
}

int main(int argc, char* argv[])
{
    xlog_defaults();
    xlog_timing(XLOG_TIME);
    xlog_setfl(XLOG_ALL);
    xlog_indent(XTRUE);

    int nSignals[2];
    nSignals[0] = SIGTERM;
    nSignals[1] = SIGINT;
    XSig_Register(nSignals, 2, signal_callback);

    if (argc < 2)
    {
        xlog("Usage: %s [address] [port]", argv[0]);
        xlog("Example: %s 127.0.0.1 6969", argv[0]);
        return 1;
    }

    xapi_t api;
    XAPI_Init(&api, service_callback, &api);

    if (XAPI_StartListener(&api, XAPI_HTTP, argv[1], atoi(argv[2])) < 0) return XSTDERR;
    xlogn("Socket started listen to port: %d", atoi(argv[2]));

    xevent_status_t status;
    do status = XAPI_Service(&api, 100);
    while (status == XEVENT_STATUS_SUCCESS);

    XAPI_Destroy(&api);
    return 0;
}