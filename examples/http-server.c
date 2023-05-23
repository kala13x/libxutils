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

int print_status(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    const char *pStr = XAPI_GetStatus(pCtx);
    int nFD = pData ? (int)pData->sock.nFD : XSTDERR;

    if (pCtx->eCbType == XAPI_CB_STATUS)
        xlogn("%s: fd(%d)", pStr, nFD);
    else if (pCtx->eCbType == XAPI_CB_ERROR)
        xloge("%s: fd(%d), errno(%d)", pStr, nFD, errno);

    return XSTDOK;
}

int handle_request(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xhttp_t *pHandle = (xhttp_t*)pData->pPacket;

    xlogn("Received request: fd(%d), buff(%zu)",
        (int)pData->sock.nFD, pHandle->rawData.nUsed);

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
            (int)pData->sock.nFD, handle.rawData.nUsed);

    XByteBuffer_AddBuff(&pData->txBuffer, &handle.rawData);
    XHTTP_Clear(&handle);

    return XAPI_EnableEvent(pData, XPOLLOUT);
}

int init_data(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xlogn("Accepted connection: fd(%d)", (int)pData->sock.nFD);
    return XAPI_SetEvents(pData, XPOLLIN);
}

int service_callback(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    switch (pCtx->eCbType)
    {
        case XAPI_CB_ERROR:
        case XAPI_CB_STATUS:
            return print_status(pCtx, pData);
        case XAPI_CB_READ:
            return handle_request(pCtx, pData);
        case XAPI_CB_WRITE:
            return write_data(pCtx, pData);
        case XAPI_CB_ACCEPTED:
            return init_data(pCtx, pData);
        case XAPI_CB_CLOSED:
            xlogn("Connection closed: fd(%d)", (int)pData->sock.nFD);
            break;
        case XAPI_CB_COMPLETE:
            xlogn("Response sent: fd(%d)", (int)pData->sock.nFD);
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
    XAPI_Init(&api, service_callback, &api, XSTDNON);

    xapi_endpoint_t endpt;
    XAPI_InitEndpoint(&endpt);
    endpt.eType = XAPI_HTTP;
    endpt.pAddr = argv[1];
    endpt.nPort = atoi(argv[2]);

    if (XAPI_Listen(&api, &endpt) < 0)
    {
        XAPI_Destroy(&api);
        return XSTDERR;
    }

    xevent_status_t status;
    do status = XAPI_Service(&api, 100);
    while (status == XEVENT_STATUS_SUCCESS);

    XAPI_Destroy(&api);
    return 0;
}