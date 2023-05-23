/*!
 *  @file libxutils/examples/ws-server.c
 *
 *  This source is part of "libxutils" project
 *  2015-2023  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of high performance event based non-blocking Web Socket server.
 * The library will use poll(), WSAPoll(), or epoll() depending on the operating system.
 */

#include <xutils/xstd.h>
#include <xutils/xver.h>
#include <xutils/xlog.h>
#include <xutils/xsig.h>
#include <xutils/api.h>

static int g_nInterrupted = 0;
extern char *optarg;

typedef struct {
    char sCaPath[XPATH_MAX];
    char sCertPath[XPATH_MAX];
    char sKeyPath[XPATH_MAX];
    char sAddr[XSOCK_ADDR_MAX];
    uint16_t nPort;
    xbool_t bSSL;
} xws_args_t;

/* Unique for all sessions */
typedef struct {
    size_t nRxCount;
    size_t nTxCount;
} session_data_t;

session_data_t* new_session_data()
{
    session_data_t *pSession = (session_data_t*)malloc(sizeof(session_data_t));
    XASSERT(pSession, xthrowp(NULL, "Failed to allocate per session data"));

    /*
        Initialize additional sesion related stuff here...
    */

    pSession->nRxCount = 0;
    pSession->nTxCount = 0;
    return pSession;
}

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

    xlogn("Received handhshake request: fd(%d), uri(%s), buff(%zu)",
        (int)pData->sock.nFD, pHandle->sUrl, pHandle->rawData.nUsed);

    char *pHeader = XHTTP_GetHeaderRaw(pHandle);
    if (pHeader != NULL)
    {
        xlogi("Raw request header:\n\n%s", pHeader);
        free(pHeader);
    }

    return XSTDOK;
}

int handshake_answer(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xhttp_t *pHandle = (xhttp_t*)pData->pPacket;

    xlogn("Sending handhshake answer: fd(%d), buff(%zu)",
        (int)pData->sock.nFD, pHandle->rawData.nUsed);

    char *pHeader = XHTTP_GetHeaderRaw(pHandle);
    if (pHeader != NULL)
    {
        xlogi("Raw answer header:\n\n%s", pHeader);
        free(pHeader);
    }

    return XSTDOK;
}

int send_pong(xapi_data_t *pData)
{
    xws_status_t status;
    xws_frame_t frame;

    status = XWebFrame_Create(&frame, NULL, 0, XWS_PONG, XTRUE);
    if (status != XWS_ERR_NONE)
    {
        xloge("Failed to create WS PONG frame: %s",
            XWebSock_GetStatusStr(status));

        return XSTDERR;
    }

    xlogn("Sending PONG: fd(%d), buff(%zu)",
        (int)pData->sock.nFD, frame.buffer.nUsed);

    XAPI_PutTxBuff(pData, &frame.buffer);
    XWebFrame_Clear(&frame);

    return XAPI_EnableEvent(pData, XPOLLOUT);
}

int send_response(xapi_data_t *pData, const char *pPayload, size_t nLength)
{
    session_data_t *pSession = (session_data_t*)pData->pSessionData;
    xws_status_t status;
    xws_frame_t frame;

    status = XWebFrame_Create(&frame, (uint8_t*)pPayload, nLength, XWS_TEXT, XTRUE);
    if (status != XWS_ERR_NONE)
    {
        xloge("Failed to create WS frame: %s",
            XWebSock_GetStatusStr(status));

        return XSTDERR;
    }

    xlogn("Sending response: fd(%d), buff(%zu)",
        (int)pData->sock.nFD, frame.buffer.nUsed);

    xlogn("Response payload: %s", pPayload);

    XAPI_PutTxBuff(pData, &frame.buffer);
    XWebFrame_Clear(&frame);

    pSession->nTxCount++;
    return XAPI_EnableEvent(pData, XPOLLOUT);
}

int handle_frame(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xws_frame_t *pFrame = (xws_frame_t*)pData->pPacket;
    session_data_t *pSession = (session_data_t*)pData->pSessionData;

    xlogn("Received WS frame: fd(%d), type(%s), fin(%s), hdr(%zu), pl(%zu), buff(%zu)",
        (int)pData->sock.nFD, XWS_FrameTypeStr(pFrame->eType), pFrame->bFin?"true":"false",
        pFrame->nHeaderSize, pFrame->nPayloadLength, pFrame->buffer.nUsed);

    if (pFrame->eType == XWS_CLOSE)
    {
        xlogd("Received CLOSE frame");
        return XSTDERR;
    }
    else if (pFrame->eType == XWS_PING)
    {
        pSession->nRxCount++;
        pSession->nTxCount++;
        return send_pong(pData);
    }
    else if (pFrame->eType != XWS_TEXT)
    {
        char sPayload[XSTR_MIN];

        size_t nLength = xstrncpyf(
            sPayload, sizeof(sPayload),
            "{\"error\":\"unsupported type\"}");

        return send_response(pData, sPayload, nLength);
    }

    /* Received close request, we should destroy this session */
    const char* pPayload = (const char*)XWebFrame_GetPayload(pFrame);
    if (xstrused(pPayload)) xlogn("WS frame payload: %s", pPayload);

    pSession->nRxCount++;
    return XAPI_EnableEvent(pData, XPOLLOUT);
}

int send_answer(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    char sPayload[XSTR_MIN];

    size_t nLength = xstrncpyf(
        sPayload, sizeof(sPayload),
        "{\"message\":\"here is your response\"}");

    return send_response(pData, sPayload, nLength);
}

int init_session(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xlogn("Accepted connection: fd(%d)", (int)pData->sock.nFD);

    /* Create session data that will be unique for each session */
    session_data_t *pSession = new_session_data();
    XASSERT_RET((pSession != NULL), XSTDERR);

    pData->pSessionData = pSession;
    return XAPI_SetEvents(pData, XPOLLIN);
}

int destroy_session(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xlogn("Connection closed: fd(%d)", (int)pData->sock.nFD);

    if (pData->pSessionData != NULL)
    {
        free(pData->pSessionData);
        pData->pSessionData = NULL;
    }

    return XSTDOK;
}

int service_callback(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    switch (pCtx->eCbType)
    {
        case XAPI_CB_HANDSHAKE_REQUEST:
            return handshake_request(pCtx, pData);
        case XAPI_CB_HANDSHAKE_ANSWER:
            return handshake_answer(pCtx, pData);
        case XAPI_CB_ACCEPTED:
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
        case XAPI_CB_LISTENING:
            xlogn("Started web socket listener: %s:%u", pData->sAddr, pData->nPort);
            break;
        case XAPI_CB_COMPLETE:
            xlogn("Response sent: fd(%d)", (int)pData->sock.nFD);
            XAPI_SetEvents(pData, XPOLLIN);
            break;
        case XAPI_CB_INTERRUPT:
            if (g_nInterrupted) return XSTDERR;
            break;
        default:
            break;
    }

    return XSTDOK;
}

void display_usage(const char *pName)
{
    printf("============================================================\n");
    printf(" WS/WSS server example - xUtils: %s\n", XUtils_Version());
    printf("============================================================\n");
    printf("Usage: %s [options]\n\n", pName);
    printf("Options are:\n");
    printf("  -a <path>            # Listener address (%s*%s)\n", XSTR_CLR_RED, XSTR_FMT_RESET);
    printf("  -p <path>            # Listener port (%s*%s)\n", XSTR_CLR_RED, XSTR_FMT_RESET);
    printf("  -c <format>          # SSL Cert file path\n");
    printf("  -k <format>          # SSL Key file path\n");
    printf("  -r <format>          # SSL CA file path\n");
    printf("  -s                   # SSL (WSS) mode\n");
    printf("  -h                   # Version and usage\n\n");
}

xbool_t parse_args(xws_args_t *pArgs, int argc, char *argv[])
{
    pArgs->sCertPath[0] = XSTR_NUL;
    pArgs->sKeyPath[0] = XSTR_NUL;
    pArgs->sCaPath[0] = XSTR_NUL;
    pArgs->sAddr[0] = XSTR_NUL;
    pArgs->nPort = XSTDNON;
    pArgs->bSSL = XFALSE;
    int nChar = XSTDNON;

    while ((nChar = getopt(argc, argv, "a:p:c:k:r:s1:h1")) != -1)
    {
        switch (nChar)
        {
            case 'a':
                xstrncpy(pArgs->sAddr, sizeof(pArgs->sAddr), optarg);
                break;
            case 'c':
                xstrncpy(pArgs->sCertPath, sizeof(pArgs->sCertPath), optarg);
                break;
            case 'k':
                xstrncpy(pArgs->sKeyPath, sizeof(pArgs->sKeyPath), optarg);
                break;
            case 'r':
                xstrncpy(pArgs->sCaPath, sizeof(pArgs->sCaPath), optarg);
                break;
            case 'p':
                pArgs->nPort = atoi(optarg);
                break;
            case 's':
                pArgs->bSSL = XTRUE;
                break;
            case 'h':
            default:
                return XFALSE;
        }
    }

    if (!xstrused(pArgs->sAddr))
    {
        xloge("Missing listener addr");
        return XFALSE;
    }

    if (!pArgs->nPort)
    {
        xloge("Missing listener port");
        return XFALSE;
    }

    if (pArgs->bSSL &&
        (!xstrused(pArgs->sCertPath) ||
         !xstrused(pArgs->sKeyPath)))
    {
        xloge("Missing SSL cert or key path");
        return XFALSE;
    }

    return XTRUE;
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

    xws_args_t args;
    if (!parse_args(&args, argc, argv))
    {
        display_usage(argv[0]);
        return XSTDERR;
    }

    xapi_t api;
    XAPI_Init(&api, service_callback, &api, XSTDNON);

    xapi_endpoint_t endpt;
    XAPI_InitEndpoint(&endpt);

    endpt.eType = XAPI_WS;
    endpt.pAddr = args.sAddr;
    endpt.nPort = args.nPort;
    endpt.bTLS = args.bSSL;

    if (endpt.bTLS)
    {
        endpt.certs.pCaPath = args.sCaPath;
        endpt.certs.pKeyPath = args.sKeyPath;
        endpt.certs.pCertPath = args.sCertPath;
        endpt.certs.nVerifyFlags = SSL_VERIFY_NONE;
    }

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