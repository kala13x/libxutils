/*!
 *  @file libxutils/examples/xws.c
 *
 *  This source is part of "libxutils" project
 *  2015-2023  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of high performance event based non-blocking WebSocket server.
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
    int nFD = pData ? (int)pData->nFD : XSTDERR;

    if (pCtx->eCbType == XAPI_CB_STATUS)
        xlogn("%s: fd(%d)", pStr, nFD);
    else if (pCtx->eCbType == XAPI_CB_ERROR)
        xloge("%s: fd(%d), errno(%d)", pStr, nFD, errno);

    return XSTDOK;
}

int handle_handshake_request(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xhttp_t *pHandle = (xhttp_t*)pData->pPacket;

    xlogn("Received handhshake request: fd(%d), url(%s), buff(%zu)",
        (int)pData->nFD, pHandle->sUrl, pHandle->rawData.nUsed);

    char *pHeader = XHTTP_GetHeaderRaw(pHandle);
    if (pHeader != NULL)
    {
        xlogi("Raw request header:\n\n%s", pHeader);
        free(pHeader);
    }

    return XSTDOK;
}

int handle_handshake_answer(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xhttp_t *pHandle = (xhttp_t*)pData->pPacket;

    xlogn("Sending handhshake answer: fd(%d), buff(%zu)",
        (int)pData->nFD, pHandle->rawData.nUsed);

    char *pHeader = XHTTP_GetHeaderRaw(pHandle);
    if (pHeader != NULL)
    {
        xlogi("Raw answer header:\n\n%s", pHeader);
        free(pHeader);
    }

    return XSTDOK;
}

int handle_frame(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xweb_frame_t *pFrame = (xweb_frame_t*)pData->pPacket;

    xlogn("Received WS frame: fd(%d), type(%s), fin(%d), hdr(%zu), pl(%zu), buff(%zu)",
        (int)pData->nFD, XWS_FrameTypeStr(pFrame->eType), pFrame->bFin,
        pFrame->nHeaderSize, pFrame->nPayloadLength, pFrame->buffer.nUsed);

    const char* pPayload = (const char*)XWebFrame_GetPayload(pFrame);
    if (pPayload != NULL) xlogd("Frame payload: %s", pPayload);

    return XAPI_SetEvents(pData, XPOLLOUT);
}

int write_data(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xweb_frame_t frame;
    xws_status_t status;

    char sPayload[XSTR_MIN];
    size_t nLength = xstrncpyf(sPayload, sizeof(sPayload), "Here is your response.");

    status = XWebFrame_Create(&frame, (uint8_t*)sPayload, nLength, XWS_TEXT, XTRUE);
    if (status != XWS_ERR_NONE)
    {
        xloge("Failed to create frame: %s", XWebSock_GetStatusStr(status));
        return XSTDERR;
    }

    xlogn("Sending response: fd(%d), buff(%zu)",
        (int)pData->nFD, frame.buffer.nUsed);

    XByteBuffer_AddBuff(&pData->txBuffer, &frame.buffer);
    XWebFrame_Clear(&frame);

    return XSTDOK;
}

int init_data(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xlogn("Accepted connection: fd(%d)", (int)pData->nFD);
    return XAPI_SetEvents(pData, XPOLLIN);
}

int service_callback(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    switch (pCtx->eCbType)
    {
        case XAPI_CB_ERROR:
        case XAPI_CB_STATUS:
            return print_status(pCtx, pData);
        case XAPI_CB_HANDSHAKE_REQUEST:
            return handle_handshake_request(pCtx, pData);
        case XAPI_CB_HANDSHAKE_ANSWER:
            return handle_handshake_answer(pCtx, pData);
        case XAPI_CB_READ:
            return handle_frame(pCtx, pData);
        case XAPI_CB_WRITE:
            return write_data(pCtx, pData);
        case XAPI_CB_ACCEPTED:
            return init_data(pCtx, pData);
        case XAPI_CB_STARTED:
            xlogn("Started web socket listener: %s:%u", pData->sAddr, pData->nPort);
            break;
        case XAPI_CB_CLOSED:
            xlogn("Connection closed: fd(%d)", (int)pData->nFD);
            break;
        case XAPI_CB_COMPLETE:
            xlogn("Response sent: fd(%d)", (int)pData->nFD);
            break;
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

    if (XAPI_StartListener(&api, XAPI_WS, argv[1], atoi(argv[2])) < 0)
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