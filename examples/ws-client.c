/*!
 *  @file libxutils/examples/ws-client.c
 *
 *  This source is part of "libxutils" project
 *  2015-2023  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of high performance event based non-blocking Web Socket client.
 * The library will use poll(), WSAPoll(), or epoll() depending on the operating system.
 */

#include <xutils/xstd.h>
#include <xutils/xver.h>
#include <xutils/xlog.h>
#include <xutils/xsig.h>
#include <xutils/xcli.h>
#include <xutils/addr.h>
#include <xutils/api.h>

static int g_nInterrupted = 0;

/* Unique for all sessions */
typedef struct {
    size_t nRxCount;
    size_t nTxCount;

    /*
        Session related stuff...
    */
} session_data_t;

void signal_callback(int sig)
{
    if (sig == SIGINT) printf("\n");
    g_nInterrupted = 1;
}

int print_status(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    int nFD = pData ? (int)pData->sock.nFD : XSTDERR;
    const char *pStr = XAPI_GetStatus(pCtx);

    xlogn("%s: fd(%d)", pStr, nFD);
    return XSTDOK;
}

int print_error(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    int nFD = pData ? (int)pData->sock.nFD : XSTDERR;
    const char *pStr = XAPI_GetStatus(pCtx);

    xloge("%s: fd(%d), errno(%d)", pStr, nFD, errno);
    return XSTDOK;
}

int handshake_request(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xhttp_t *pHandle = (xhttp_t*)pData->pPacket;

    xlogn("Sending handhshake request: fd(%d), url(%s), buff(%zu)",
        (int)pData->sock.nFD, pHandle->sUrl, pHandle->rawData.nUsed);

    char *pHeader = XHTTP_GetHeaderRaw(pHandle);
    if (pHeader != NULL)
    {
        xlogi("Raw request header:\n\n%s", pHeader);
        free(pHeader);
    }

    return XSTDOK;
}

int handshake_response(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xhttp_t *pHandle = (xhttp_t*)pData->pPacket;

    xlogn("Received handhshake response: fd(%d), buff(%zu)",
        (int)pData->sock.nFD, pHandle->rawData.nUsed);

    char *pHeader = XHTTP_GetHeaderRaw(pHandle);
    if (pHeader != NULL)
    {
        xlogi("Raw answer response:\n\n%s", pHeader);
        free(pHeader);
    }

    return XSTDOK;
}

int handle_frame(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xws_frame_t *pFrame = (xws_frame_t*)pData->pPacket;
    session_data_t *pSession = (session_data_t*)pData->pSessionData;

    xlogn("Received WS frame: fd(%d), type(%s), fin(%s), hdr(%zu), pl(%zu), buff(%zu)",
        (int)pData->sock.nFD, XWS_FrameTypeStr(pFrame->eType), pFrame->bFin?"true":"false",
        pFrame->nHeaderSize, pFrame->nPayloadLength, pFrame->buffer.nUsed);

    /* Received close request, we should destroy this session */
    if (pFrame->eType == XWS_CLOSE) return XSTDERR;
    const char* pPayload = (const char*)XWebFrame_GetPayload(pFrame);

    if (pPayload && pFrame->eType == XWS_TEXT)
        xlogn("WS frame payload: %s", pPayload);

    pSession->nRxCount++;
    return XAPI_CallbackOnWrite(pData, XTRUE);
}

int send_answer(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    session_data_t *pSession = (session_data_t*)pData->pSessionData;
    xws_status_t status;
    xws_frame_t frame;

    char sPayload[XSTR_MIN];
    XCLI_GetInput("Enter message: ", sPayload, sizeof(sPayload), XTRUE);

    size_t nLength = strlen(sPayload);
    if (!nLength) return XSTDNON;

    status = XWebFrame_Create(&frame, (uint8_t*)sPayload, nLength, XWS_TEXT, XTRUE);
    if (status != XWS_ERR_NONE)
    {
        xloge("Failed to create WS frame: %s",
            XWebSock_GetStatusStr(status));

        return XSTDERR;
    }

    xlogn("Sending message: fd(%d), buff(%zu)",
        (int)pData->sock.nFD, frame.buffer.nUsed);
    xlogn("Message payload: %s", sPayload);

    XByteBuffer_AddBuff(&pData->txBuffer, &frame.buffer);
    XWebFrame_Clear(&frame);

    pSession->nTxCount++;
    return XAPI_CallbackOnWrite(pData, XFALSE);
}

int init_session(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xlogn("Client connected to server: fd(%d) %s:%u",
        (int)pData->sock.nFD, pData->sAddr, pData->nPort);

    session_data_t *pSession = (session_data_t*)pData->pSessionData;
    pSession->nRxCount = 0;
    pSession->nTxCount = 0;

    return XAPI_CallbackOnRead(pData, XTRUE);
}

int destroy_session(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    session_data_t *pSession = (session_data_t*)pData->pSessionData;
    xlogn("Connection closed: fd(%d)", (int)pData->sock.nFD);

    xlogi("Frame statistics: rx(%d), tx(%d)",
        pSession->nRxCount, pSession->nTxCount);

    return XSTDOK;
}

int service_callback(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    switch (pCtx->eCbType)
    {
        case XAPI_CB_HANDSHAKE_REQUEST:
            return handshake_request(pCtx, pData);
        case XAPI_CB_HANDSHAKE_RESPONSE:
            return handshake_response(pCtx, pData);
        case XAPI_CB_CONNECTED:
            return init_session(pCtx, pData);
        case XAPI_CB_CLOSED:
            return destroy_session(pCtx, pData);
        case XAPI_CB_READ:
            return handle_frame(pCtx, pData);
        case XAPI_CB_WRITE:
            return send_answer(pCtx, pData);
        case XAPI_CB_ERROR:
            return print_error(pCtx, pData);
        case XAPI_CB_STATUS:
            return print_status(pCtx, pData);
        case XAPI_CB_COMPLETE:
            xlogn("TX complete: fd(%d)", (int)pData->sock.nFD);
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
        xlog("Usage: %s [ws-url]", argv[0]);
        xlog("Example: %s ws://127.0.0.1:6969/ws", argv[0]);
        return 1;
    }

    xapi_t api;
    XAPI_Init(&api, service_callback, &api, XSTDNON);

    xlink_t link;
    if (XLink_Parse(&link, argv[1]) < 0 || !link.nPort)
    {
        xloge("Failed to parse link: %s", argv[1]);
        xlogi("Example: ws://127.0.0.1:6969/ws");

        XAPI_Destroy(&api);
        return XSTDERR;
    }

    xapi_endpoint_t endpt;
    XAPI_InitEndpoint(&endpt);

    endpt.eType = XAPI_WS;
    endpt.pAddr = link.sAddr;
    endpt.nPort = link.nPort;
    endpt.pUri = link.sUrl;

    session_data_t sessData;
    endpt.pSessionData = &sessData;

    if (XAPI_Connect(&api, &endpt) < 0)
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