/*!
 *  @file libxutils/examples/http-workers.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of high performance event based non-blocking HTTP/S workers.
 */

#include "api.h"
#include "sig.h"
#include "cpu.h"
#include "xdef.h"
#include <stdio.h>

static volatile sig_atomic_t g_nInterrupted = 0;

static void signal_handler(int sig)
{
    (void)sig;
    g_nInterrupted = 1;
}

static int on_request(xapi_ctx_t *ctx, xapi_session_t *s)
{
    xhttp_t *req = (xhttp_t*)s->pPacket;

    printf("Worker[%d] received request: %s %s (length: %zu)\n",
        ctx->nWorkerIndex,
        XHTTP_GetMethodStr(req->eMethod),
        req->sUri, req->rawData.nUsed);

    return XAPI_EnableEvent(s, XPOLLOUT);
}

static int on_write(xapi_ctx_t *ctx, xapi_session_t *s)
{
    xhttp_t res;
    XHTTP_InitResponse(&res, 200, NULL);
    XHTTP_AddHeader(&res, "Content-Type", "text/plain");

    const char *body = "Hello from libxutils\n";
    XHTTP_Assemble(&res, (const uint8_t*)body, strlen(body));

    XAPI_PutTxBuff(s, &res.rawData);
    XHTTP_Clear(&res);

    return XAPI_EnableEvent(s, XPOLLOUT);
}

static int on_listening(xapi_ctx_t *ctx, xapi_session_t *s)
{
    printf("Worker[%d] listening on %s:%u\n",
        ctx->nWorkerIndex, s->sAddr, (unsigned)s->nPort);

    return XAPI_CONTINUE;
}

static int callback(xapi_ctx_t *ctx, xapi_session_t *s)
{
    switch (ctx->eCbType)
    {
        case XAPI_CB_ERROR:
            fprintf(stderr, "Worker[%d] error: %s\n", ctx->nWorkerIndex, XAPI_GetStatus(ctx));
            return XAPI_CONTINUE;

        case XAPI_CB_LISTENING:
            return on_listening(ctx, s);

        case XAPI_CB_ACCEPTED:
            return XAPI_SetEvents(s, XPOLLIN);

        case XAPI_CB_READ:
            return on_request(ctx, s);

        case XAPI_CB_WRITE:
            return on_write(ctx, s);

        case XAPI_CB_COMPLETE:
            return XAPI_DISCONNECT;

        case XAPI_CB_INTERRUPT:
            return g_nInterrupted ? XAPI_DISCONNECT : XAPI_CONTINUE;

        default:
            return XAPI_CONTINUE;
    }
}

static int run_worker_service(xapi_t *pApi)
{
    do {
        xevent_status_t eStatus = XAPI_Service(pApi, 100);
        if (eStatus != XEVENTS_SUCCESS) return XSTDERR;
    } while (!g_nInterrupted);

    return XSTDNON;
}

static int run_parent_service(xapi_t *pApi)
{
    const xpid_t *pWorkerPIDs = XAPI_GetWorkerPIDs(pApi);
    const size_t nWorkers = XAPI_GetWorkerCount(pApi);

    if (pWorkerPIDs == NULL || !nWorkers)
    {
        xlogn("Worker mode is unavailable, running in single-process mode");
        return run_worker_service(pApi);
    }

    XSTATUS nStatus = XSTDNON;
    printf("Parent process: pid(%d)\n", (int)getpid());

    for (size_t i = 0; i < nWorkers; i++)
        printf("Started worker[%zu]: pid(%d)\n", i, (int)pWorkerPIDs[i]);

    do {
        nStatus = XAPI_WatchWorkers(pApi, &g_nInterrupted);
        if (nStatus == XSTDUSR)
        {
            printf("Respawned worker[%d]: pid(%d)\n",
                XAPI_GetWorkerIndex(pApi),
                (int)XAPI_GetWorkerPID(pApi));

            run_worker_service(pApi);
        }
    } while (nStatus == XSTDUSR);

    return nStatus < 0 ? XSTDERR : XSTDNON;
}

int main(void)
{
    int nSignals[2] = { SIGTERM, SIGINT };
    XSig_Register(nSignals, 2, signal_handler);

    xapi_t api;
    XAPI_Init(&api, callback, NULL);

    xapi_endpoint_t ep;
    XAPI_InitEndpoint(&ep);

    ep.pAddr = "0.0.0.0";
    ep.nPort = 8080;
    ep.eType = XAPI_HTTP;
    ep.eRole = XAPI_SERVER;

    if (XAPI_AddEndpoint(&api, &ep) < 0)
    {
        XAPI_Destroy(&api);
        return 1;
    }

    size_t nWorkers = (size_t)XCPU_GetCount();
    if (!nWorkers) nWorkers = 2;

    XSTATUS nStatus = XAPI_InitWorkers(&api, nWorkers, XTRUE);
    if (nStatus < 0)
    {
        XAPI_Destroy(&api);
        return 1;
    }

    nStatus = XAPI_IsWorker(&api) ?
        run_worker_service(&api) :
        run_parent_service(&api);

    XAPI_Destroy(&api);
    return nStatus;
}
