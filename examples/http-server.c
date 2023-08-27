/*!
 *  @file libxutils/examples/http-server.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of high performance event based non-blocking HTTP/S server.
 * The library will use poll(), epoll() or WSAPoll() depending on the operating system.
 */

#include "xstd.h"
#include "xver.h"
#include "xlog.h"
#include "xsig.h"
#include "api.h"

static int g_nInterrupted = 0;
extern char *optarg;

typedef struct {
    char sCaPath[XPATH_MAX];
    char sCertPath[XPATH_MAX];
    char sKeyPath[XPATH_MAX];
    char sAddr[XSOCK_ADDR_MAX];
    uint16_t nPort;
    xbool_t bSSL;
} xhttps_args_t;

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

    return XAPI_CONTINUE;
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

    return XAPI_EnableEvent(pData, XPOLLOUT);
}

int write_data(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xhttp_t handle;
    if (XHTTP_InitResponse(&handle, 200, NULL) <= 0)
    {
        xloge("Failed to initialize HTTP response: %s", XSTRERR);
        return XAPI_DISCONNECT;
    }

    if (XHTTP_AddHeader(&handle, "Server", "xutils/%s", XUtils_VersionShort()) < 0 ||
        XHTTP_AddHeader(&handle, "Content-Type", "text/plain") < 0)
    {
        xloge("Failed to setup HTTP headers: %s", XSTRERR);
        XHTTP_Clear(&handle);
        return XAPI_DISCONNECT;
    }

    char sBody[XSTR_TINY];
    size_t nLen = xstrncpy(sBody, sizeof(sBody), "Here is your response.");

    if (XHTTP_Assemble(&handle, (const uint8_t*)sBody, nLen) == NULL)
    {
        xloge("Failed to assemble HTTP response: %s", XSTRERR);
        XHTTP_Clear(&handle);
        return XAPI_DISCONNECT;
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
            return XAPI_DISCONNECT;
        case XAPI_CB_INTERRUPT:
            if (g_nInterrupted) return XAPI_DISCONNECT;
            break;
        default:
            break;
    }

    return XAPI_CONTINUE;
}

void display_usage(const char *pName)
{
    printf("============================================================\n");
    printf(" HTTP/S server example - xUtils: %s\n", XUtils_Version());
    printf("============================================================\n");
    printf("Usage: %s [options]\n\n", pName);
    printf("Options are:\n");
    printf("  -a <addr>            # Listener address (%s*%s)\n", XSTR_CLR_RED, XSTR_FMT_RESET);
    printf("  -p <port>            # Listener port (%s*%s)\n", XSTR_CLR_RED, XSTR_FMT_RESET);
    printf("  -c <path>            # SSL Cert file path\n");
    printf("  -k <path>            # SSL Key file path\n");
    printf("  -r <path>            # SSL CA file path\n");
    printf("  -s                   # SSL (WSS) mode\n");
    printf("  -h                   # Version and usage\n\n");
}

xbool_t parse_args(xhttps_args_t *pArgs, int argc, char *argv[])
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

    xhttps_args_t args;
    if (!parse_args(&args, argc, argv))
    {
        display_usage(argv[0]);
        return XSTDERR;
    }

    xapi_t api;
    XAPI_Init(&api, service_callback, &api, XSTDNON);

    xapi_endpoint_t endpt;
    XAPI_InitEndpoint(&endpt);

    endpt.eType = XAPI_HTTP;
    endpt.pAddr = args.sAddr;
    endpt.nPort = args.nPort;
    endpt.bTLS = args.bSSL;

    if (endpt.bTLS)
    {
        endpt.certs.pCaPath = args.sCaPath;
        endpt.certs.pKeyPath = args.sKeyPath;
        endpt.certs.pCertPath = args.sCertPath;
#ifdef SSL_VERIFY_PEER
        endpt.certs.nVerifyFlags = SSL_VERIFY_PEER;
#endif
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