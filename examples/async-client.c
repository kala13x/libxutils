/*!
 *  @file libxutils/examples/async-client.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of high performance event based non-blocking async client.
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
    char sAddr[XSOCK_ADDR_MAX];
    uint16_t nPort;
    xbool_t bUnix;
    xbool_t bSSL;
} xasync_client_args_t;

void signal_callback(int sig)
{
    if (sig == SIGINT) printf("\n");
    g_nInterrupted = 1;
}

int handle_status(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    const char *pStr = XAPI_GetStatus(pCtx);
    int nFD = pSession ? (int)pSession->sock.nFD : XSTDERR;

    if (pCtx->eCbType == XAPI_CB_STATUS)
        xlogn("%s: fd(%d)", pStr, nFD);
    else if (pCtx->eCbType == XAPI_CB_ERROR)
        xloge("%s: fd(%d), errno(%d)", pStr, nFD, errno);

    return XAPI_CONTINUE;
}

int handle_read(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    xbyte_buffer_t *pBuffer = XAPI_GetRxBuff(pSession);

    xlogn("Received response: fd(%d), buff(%zu): %s",
        (int)pSession->sock.nFD, pBuffer->nUsed,
        (const char*)pBuffer->pData);

    return XAPI_DISCONNECT;
}

int handle_write(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    xbyte_buffer_t *pBuffer = XAPI_GetTxBuff(pSession);
    XByteBuffer_AddFmt(pBuffer, "My simple request");
    return XAPI_EnableEvent(pSession, XPOLLOUT);
}

int send_complete(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    xlogn("Request sent: fd(%d)", (int)pSession->sock.nFD);

    xbyte_buffer_t *pBuffer = XAPI_GetTxBuff(pSession);
    if (!pBuffer->nUsed) XAPI_DisableEvent(pSession, XPOLLOUT);

    return XAPI_EnableEvent(pSession, XPOLLIN);
}

int init_data(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    xlogn("Conected to server: fd(%d)", (int)pSession->sock.nFD);
    return XAPI_SetEvents(pSession, XPOLLOUT);
}

int service_callback(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    switch (pCtx->eCbType)
    {
        case XAPI_CB_ERROR:
        case XAPI_CB_STATUS:
            return handle_status(pCtx, pSession);
        case XAPI_CB_READ:
            return handle_read(pCtx, pSession);
        case XAPI_CB_WRITE:
            return handle_write(pCtx, pSession);
        case XAPI_CB_CONNECTED:
            return init_data(pCtx, pSession);
        case XAPI_CB_COMPLETE:
            return send_complete(pCtx, pSession);
        case XAPI_CB_CLOSED:
            xlogn("Connection closed: fd(%d)", (int)pSession->sock.nFD);
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
    printf(" XAPI client example - xutils: %s\n", XUtils_Version());
    printf("============================================================\n");
    printf("Usage: %s [options]\n\n", pName);
    printf("Options are:\n");
    printf("  -a <addr>            # Listener address (%s*%s)\n", XSTR_CLR_RED, XSTR_FMT_RESET);
    printf("  -p <port>            # Listener port\n");
    printf("  -s                   # SSL mode\n");
    printf("  -u                   # Use unix socket\n");
    printf("  -h                   # Version and usage\n\n");
}

xbool_t parse_args(xasync_client_args_t *pArgs, int argc, char *argv[])
{
    pArgs->sAddr[0] = XSTR_NUL;
    pArgs->nPort = XSTDNON;
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
            case 'p':
                pArgs->nPort = atoi(optarg);
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

    xasync_client_args_t args;
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
    endpt.eRole = XAPI_CLIENT;
    endpt.pAddr = args.sAddr;
    endpt.nPort = args.nPort;
    endpt.bUnix = args.bUnix;
    endpt.bTLS = args.bSSL;

    if (XAPI_AddEndpoint(&api, &endpt) < 0)
    {
        XAPI_Destroy(&api);
        return XSTDERR;
    }

    while (!g_nInterrupted)
    {
        xevent_status_t status = XAPI_Service(&api, 100);
        if (status != XEVENTS_SUCCESS) break;

        // Manualy break if no more events
        if (!XAPI_GetEventCount(&api)) break;
    }

    XAPI_Destroy(&api);
    return 0;
}
