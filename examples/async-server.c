/*!
 *  @file libxutils/examples/async-server.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of high performance event based non-blocking async server.
 * The library will use poll(), epoll() or WSAPoll() depending on the operating system.
 */

#include "xstd.h"
#include "xver.h"
#include "log.h"
#include "sig.h"
#include "xfs.h"
#include "api.h"

static int g_nInterrupted = 0;
extern char *optarg;

typedef struct {
    char sCaPath[XPATH_MAX];
    char sCertPath[XPATH_MAX];
    char sKeyPath[XPATH_MAX];
    char sAddr[XSOCK_ADDR_MAX];
    uint16_t nPort;
    xbool_t bForce;
    xbool_t bUnix;
    xbool_t bSSL;
} xasync_srv_args_t;

void signal_callback(int sig)
{
    if (sig == SIGINT) printf("\n");
    g_nInterrupted = 1;
}

int handle_status(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    const char *pStr = XAPI_GetStatus(pCtx);
    int nFD = pData ? (int)pData->sock.nFD : XSTDERR;
    int nID = pData ? (int)pData->nID : XSTDERR;

    if (pCtx->eCbType == XAPI_CB_STATUS)
        xlogn("%s: id(%d), fd(%d)", pStr, nID, nFD);
    else if (pCtx->eCbType == XAPI_CB_ERROR)
        xloge("%s: id(%d), fd(%d), errno(%d)", pStr, nID, nFD, errno);

    if (pCtx->nStatus == XAPI_DESTROY)
    {
        xapi_t *pApi = pCtx->pApi;
        xasync_srv_args_t *pArgs = (xasync_srv_args_t*)pApi->pUserCtx;
        if (pArgs->bUnix) XPath_Remove(pArgs->sAddr);
    }

    return XAPI_CONTINUE;
}

int handle_request(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xbyte_buffer_t *pBuffer = XAPI_GetRxBuff(pData);

    xlogn("Received data: id(%u), fd(%d), buff(%zu)",
        pData->nID, (int)pData->sock.nFD, pBuffer->nUsed);

    // Echo response
    XByteBuffer_AddBuff(&pData->txBuffer, pBuffer);

    // Extend timeout for another 20 seconds
    XAPI_AddTimer(pData, 20000);

    return XAPI_EnableEvent(pData, XPOLLOUT);
}

int init_data(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xlogn("Accepted connection: id(%u), fd(%d)", pData->nID, (int)pData->sock.nFD);
    XAPI_AddTimer(pData, 20000); // Set 20 seconds timeout
    return XAPI_SetEvents(pData, XPOLLIN);
}

int service_callback(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    switch (pCtx->eCbType)
    {
        case XAPI_CB_ERROR:
        case XAPI_CB_STATUS:
            return handle_status(pCtx, pData);
        case XAPI_CB_READ:
            return handle_request(pCtx, pData);
        case XAPI_CB_ACCEPTED:
            return init_data(pCtx, pData);
        case XAPI_CB_LISTENING:
            xlogn("Server started listening: id(%u), fd(%d)", pData->nID, (int)pData->sock.nFD);
            break;
        case XAPI_CB_CLOSED:
            xlogn("Connection closed: id(%u), fd(%d)", pData->nID, (int)pData->sock.nFD);
            break;
        case XAPI_CB_COMPLETE:
            xlogn("Response sent: id(%u), fd(%d)", pData->nID, (int)pData->sock.nFD);
            break;
        case XAPI_CB_TIMEOUT:
            xlogn("Timeout event for the socket: id(%u), fd(%d)", pData->nID, (int)pData->sock.nFD);
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
    printf(" XAPI server example - xutils: %s\n", XUtils_Version());
    printf("============================================================\n");
    printf("Usage: %s [options]\n\n", pName);
    printf("Options are:\n");
    printf("  -a <addr>            # Listener address (%s*%s)\n", XSTR_CLR_RED, XSTR_FMT_RESET);
    printf("  -p <port>            # Listener port\n");
    printf("  -c <path>            # SSL Cert file path\n");
    printf("  -k <path>            # SSL Key file path\n");
    printf("  -r <path>            # SSL CA file path\n");
    printf("  -s                   # SSL mode\n");
    printf("  -f                   # Force bind socket\n");
    printf("  -u                   # Use unix socket\n");
    printf("  -h                   # Version and usage\n\n");
}

xbool_t parse_args(xasync_srv_args_t *pArgs, int argc, char *argv[])
{
    pArgs->sCertPath[0] = XSTR_NUL;
    pArgs->sKeyPath[0] = XSTR_NUL;
    pArgs->sCaPath[0] = XSTR_NUL;
    pArgs->sAddr[0] = XSTR_NUL;
    pArgs->nPort = XSTDNON;
    pArgs->bForce = XFALSE;
    pArgs->bUnix = XFALSE;
    pArgs->bSSL = XFALSE;
    int nChar = XSTDNON;

    while ((nChar = getopt(argc, argv, "a:p:c:k:r:f1:u1:s1:h1")) != -1)
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
            case 'f':
                pArgs->bForce = XTRUE;
                break;
            case 'u':
                pArgs->bUnix = XTRUE;
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

    if (!pArgs->nPort && !pArgs->bUnix)
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

    int nSignals[2] = { SIGTERM, SIGINT };
    XSig_Register(nSignals, 2, signal_callback);

    xasync_srv_args_t args;
    if (!parse_args(&args, argc, argv))
    {
        display_usage(argv[0]);
        return XSTDERR;
    }

    xapi_t api;
    XAPI_Init(&api, service_callback, &args);

    xapi_endpoint_t endpt;
    XAPI_InitEndpoint(&endpt);

    endpt.eType = XAPI_SOCK;
    endpt.eRole = XAPI_SERVER;
    endpt.pAddr = args.sAddr;
    endpt.nPort = args.nPort;
    endpt.bUnix = args.bUnix;
    endpt.bTLS = args.bSSL;
    endpt.bForce = args.bForce;

    if (endpt.bTLS)
    {
        endpt.certs.pCaPath = args.sCaPath;
        endpt.certs.pKeyPath = args.sKeyPath;
        endpt.certs.pCertPath = args.sCertPath;
#ifdef SSL_VERIFY_PEER
        endpt.certs.nVerifyFlags = SSL_VERIFY_PEER;
#endif
    }

    if (XAPI_AddEndpoint(&api, &endpt) < 0)
    {
        XAPI_Destroy(&api);
        return XSTDERR;
    }

    xevent_status_t status;
    do status = XAPI_Service(&api, 100);
    while (status == XEVENTS_SUCCESS);

    XAPI_Destroy(&api);
    return 0;
}
