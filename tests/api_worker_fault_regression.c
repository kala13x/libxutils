/* Script process failures without signalling or reaping unrelated processes. */
#include "test.h"
#include "api.h"
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    int nForks;
    int nKills;
    int nWaits;
    int nForkFail;
    int nAllocFail;
    int killErrors[8];
    pid_t waitResults[8];
    int waitErrors[8];
    int waitStatuses[8];
    pid_t killPIDs[8];
    pid_t waitPIDs[8];
    volatile sig_atomic_t *pStop;
    int nWaitSteps;
    xbool_t bInvalid;
} worker_script_t;

static worker_script_t g_script;
void *__real_calloc(size_t, size_t);

void *__wrap_calloc(size_t nCount, size_t nSize)
{
    if (g_script.nAllocFail && nCount == 3 && nSize == sizeof(xpid_t))
    {
        g_script.nAllocFail = 0;
        return NULL;
    }
    return __real_calloc(nCount, nSize);
}

pid_t __wrap_fork(void)
{
    g_script.nForks++;
    if (g_script.nForks == g_script.nForkFail) { errno = EAGAIN; return -1; }
    return 41000 + g_script.nForks;
}

int __wrap_kill(pid_t nPID, int nSignal)
{
    int nIndex = g_script.nKills++;
    if (nIndex < 8) g_script.killPIDs[nIndex] = nPID;
    if (nPID < 41001 || nPID > 41008 || nSignal != SIGTERM || nIndex >= 8) g_script.bInvalid = XTRUE;
    int nError = nIndex < 8 ? g_script.killErrors[nIndex] : EINVAL;
    if (nError) { errno = nError; return -1; }
    return 0;
}

pid_t __wrap_waitpid(pid_t nPID, int *pStatus, int nOptions)
{
    int nIndex = g_script.nWaits++;
    if (nIndex < 8) g_script.waitPIDs[nIndex] = nPID;
    if (g_script.pStop && !nIndex) *g_script.pStop = 1;
    if (nOptions || (nPID != -1 && (nPID < 41001 || nPID > 41008)) || nIndex >= 8) g_script.bInvalid = XTRUE;
    if (pStatus) *pStatus = nIndex < g_script.nWaitSteps ? g_script.waitStatuses[nIndex] : 0;
    if (nIndex < g_script.nWaitSteps)
    {
        errno = g_script.waitErrors[nIndex];
        return g_script.waitResults[nIndex];
    }
    if (nPID > 0) return nPID;
    errno = ECHILD;
    return -1;
}

typedef struct { int nErrors; int nLastError; int nClosed; xapi_session_t *pSession; } worker_fault_t;

static int worker_fault_cb(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    worker_fault_t *pTest = pCtx->pApi->pUserCtx;
    if (pCtx->eCbType == XAPI_CB_REGISTERED) pTest->pSession = pSession;
    if (pCtx->eCbType == XAPI_CB_ERROR) { pTest->nErrors++; pTest->nLastError = pCtx->nStatus; }
    if (pCtx->eCbType == XAPI_CB_CLOSED) { pTest->nClosed++; pTest->pSession = NULL; }
    return XAPI_CONTINUE;
}

static int worker_fault_open(xapi_t *pApi, worker_fault_t *pTest, int *pPeer)
{
    int pair[2];
    memset(&g_script, 0, sizeof(g_script));
    CHECK(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair), "Create a real transport around scripted process calls");
    CHECK(XAPI_Init(pApi, worker_fault_cb, pTest) == XSTDOK, "Initialize the worker owner");
    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eType = XAPI_SOCK;
    endpoint.eRole = XAPI_PEER;
    endpoint.nEvents = XPOLLIN;
    endpoint.nFD = pair[0];
    CHECK(XAPI_AddEvent(pApi, &endpoint) == XSTDOK && pTest->pSession, "Register the parent-owned transport");
    CHECK(XSock_NonBlock(&pTest->pSession->sock, XTRUE) >= 0, "Keep parent I/O nonblocking");
    pTest->pSession->bKeepRxBuffer = XTRUE;
    pApi->events.nEventMax = 8;
    *pPeer = pair[1];
    return 0;
}

static int XTest_start_failure(void)
{
    for (int nFail = 0; nFail <= 3; nFail++)
    {
        xapi_t api;
        worker_fault_t test = {0};
        int nPeer;
        CHECK(!worker_fault_open(&api, &test, &nPeer), "Open the failed worker-start fixture");
        g_script.nForkFail = nFail;
        g_script.nAllocFail = !nFail;
        CHECK(XAPI_InitWorkers(&api, 3, XFALSE) == XSTDERR, "An allocation or fork failure refuses the worker group");
        CHECK(test.nErrors == 1 && test.nLastError == (nFail ? XAPI_ERR_FORK : XAPI_ERR_ALLOC),
            "Starting workers reports the exact failing operation once");
        CHECK(!api.pWorkerPIDs && !api.nWorkerCount && !api.bIsWorker && api.nWorkerIndex == -1 &&
            g_script.nForks == nFail && g_script.nKills == (nFail ? nFail - 1 : 0) &&
            g_script.nWaits == g_script.nKills && !g_script.bInvalid,
            "Failure stops and reaps only the workers already started, then clears the worker table");
        const uint8_t data[] = {0, 'p', 0xff};
        CHECK(write(nPeer, data, sizeof(data)) == sizeof(data) && XAPI_Service(&api, 100) == XEVENTS_SUCCESS &&
            test.pSession->rxBuffer.nUsed == sizeof(data) && !memcmp(test.pSession->rxBuffer.pData, data, sizeof(data)),
            "The parent's original event backend still delivers exact data after worker-start failure");
        int nFD = test.pSession->sock.nFD;
        XAPI_Destroy(&api);
        close(nPeer);
        CHECK(test.nClosed == 1 && fcntl(nFD, F_GETFD) < 0 && errno == EBADF, "Parent ownership stays intact until destruction");
    }
    return 0;
}

static int XTest_wait_failures(void)
{
    const int errors[] = {EINTR, ECHILD, EIO};
    for (size_t i = 0; i < sizeof(errors) / sizeof(*errors); i++)
    {
        xapi_t api;
        worker_fault_t test = {0};
        int nPeer;
        CHECK(!worker_fault_open(&api, &test, &nPeer) && XAPI_InitWorkers(&api, 3, XFALSE) == XSTDOK,
            "Start a scripted group whose exact PIDs are known");
        g_script.nWaitSteps = 1;
        g_script.waitResults[0] = -1;
        g_script.waitErrors[0] = errors[i];
        int nStatus = XAPI_WaitWorkers(&api);
        if (errors[i] == EIO)
        {
            CHECK(nStatus == XSTDERR && api.pWorkerPIDs[0] == 41001 && api.pWorkerPIDs[1] == 41002 &&
                api.pWorkerPIDs[2] == 41003 && g_script.nWaits == 1,
                "A real wait failure preserves every unreaped PID for a later retry");
            g_script.nWaitSteps = 0;
            CHECK(XAPI_WaitWorkers(&api) == XSTDOK, "Waiting can recover without losing any worker identity");
        }
        else CHECK(nStatus == XSTDOK && g_script.nWaits == (errors[i] == EINTR ? 4 : 3),
            "Interrupted waits retry the same worker, while an already reaped worker is skipped");
        CHECK(!api.pWorkerPIDs[0] && !api.pWorkerPIDs[1] && !api.pWorkerPIDs[2] &&
            XAPI_WaitWorkers(&api) == XSTDNON && !g_script.bInvalid && !g_script.nKills,
            "Only successfully reaped or absent workers are cleared, and a second wait is a no-op");
        XAPI_Destroy(&api);
        close(nPeer);
        CHECK(test.nClosed == 1 && !test.nErrors, "Waiting never alters the parent's transport ownership");
    }
    return 0;
}

static int XTest_stop_failures(void)
{
    const int errors[] = {ESRCH, EPERM};
    for (size_t i = 0; i < sizeof(errors) / sizeof(*errors); i++)
        for (int nSlot = 0; nSlot < 3; nSlot++)
        {
            xapi_t api;
            worker_fault_t test = {0};
            int nPeer;
            CHECK(!worker_fault_open(&api, &test, &nPeer) && XAPI_InitWorkers(&api, 3, XFALSE) == XSTDOK,
                "Start the group before a selected stop fails");
            CHECK(XAPI_StopWorkers(&api, 0) == XSTDINV && XAPI_StopWorkers(&api, -1) == XSTDINV && !g_script.nKills,
                "Invalid signals cannot reach any worker");
            g_script.killErrors[nSlot] = errors[i];
            int nStatus = XAPI_StopWorkers(&api, SIGTERM);
            CHECK(nStatus == (errors[i] == ESRCH ? XSTDOK : XSTDERR) && g_script.nKills == 3 && !g_script.bInvalid,
                "An absent process is tolerated, but a real stop failure remains visible after later successful stops");
            CHECK(api.pWorkerPIDs[0] == 41001 && api.pWorkerPIDs[1] == 41002 && api.pWorkerPIDs[2] == 41003,
                "Sending a signal never pretends the worker has already been reaped");
            CHECK(XAPI_WaitWorkers(&api) == XSTDOK && XAPI_StopWorkers(&api, SIGTERM) == XSTDNON && g_script.nKills == 3,
                "Reaped slots are never signalled again");
            XAPI_Destroy(&api);
            close(nPeer);
            CHECK(test.nClosed == 1 && !test.nErrors, "Stop failures leave parent event ownership unchanged");
        }
    return 0;
}

static int XTest_watch_failures(void)
{
    for (int nCase = 0; nCase < 4; nCase++)
    {
        xapi_t api;
        worker_fault_t test = {0};
        int nPeer;
        CHECK(!worker_fault_open(&api, &test, &nPeer) && XAPI_InitWorkers(&api, 3, XFALSE) == XSTDOK,
            "Create the supervised group before wait or replacement fails");
        int nBackend = api.events.nEventFd;
        g_script.nWaitSteps = nCase < 2 ? 2 : 1;
        g_script.waitResults[0] = nCase == 1 ? 49999 : nCase == 3 ? 41002 : -1;
        g_script.waitErrors[0] = nCase == 0 ? EINTR : nCase == 2 ? EIO : 0;
        g_script.waitResults[1] = -1;
        g_script.waitErrors[1] = ECHILD;
        g_script.nForkFail = 4;
        int nStatus = XAPI_WatchWorkers(&api, NULL);
        CHECK(nStatus == (nCase < 2 ? XSTDNON : XSTDERR) && g_script.nWaits == g_script.nWaitSteps,
            "The watcher retries interruptions, ignores unrelated children and reports actual wait failures");
        CHECK(g_script.nForks == (nCase == 3 ? 4 : 3) && !g_script.nKills && !g_script.bInvalid &&
            api.pWorkerPIDs[0] == 41001 && api.pWorkerPIDs[1] == (nCase == 3 ? 0 : 41002) && api.pWorkerPIDs[2] == 41003,
            "A failed replacement clears only the terminated worker and preserves sibling identities");
        CHECK(test.nErrors == (nCase == 3) && (!test.nErrors || test.nLastError == XAPI_ERR_FORK) &&
            api.events.nEventFd < 0 && !api.events.pEventArray && fcntl(nBackend, F_GETFD) < 0 && errno == EBADF,
            "The supervisor releases its polling backend and reports only a replacement fork failure");
        CHECK(test.pSession && XAPI_GetEventCount(&api) == 1 && api.events.eventsMap.nPairCount == 1,
            "The original session map retains ownership after the polling backend is gone");
        int nFD = test.pSession->sock.nFD;
        XAPI_Destroy(&api);
        close(nPeer);
        CHECK(test.nClosed == 1 && fcntl(nFD, F_GETFD) < 0 && errno == EBADF, "Destruction releases the parent's session once");
    }
    return 0;
}

static int XTest_watch_shutdown(void)
{
    for (int nCase = 0; nCase < 4; nCase++)
    {
        xapi_t api;
        worker_fault_t test = {0};
        int nPeer;
        CHECK(!worker_fault_open(&api, &test, &nPeer) && XAPI_InitWorkers(&api, 3, XFALSE) == XSTDOK,
            "Create the group before supervisor shutdown");
        if (nCase == 1) g_script.killErrors[1] = EPERM;
        if (nCase == 2)
        {
            g_script.nWaitSteps = 1;
            g_script.waitResults[0] = -1;
            g_script.waitErrors[0] = EIO;
        }
        if (nCase == 3) CHECK(XAPI_WaitWorkers(&api) == XSTDOK, "Reap every worker before an idle shutdown");
        volatile sig_atomic_t nStop = 1;
        int nStatus = XAPI_WatchWorkers(&api, &nStop);
        CHECK(nStatus == (nCase == 0 ? XSTDOK : nCase == 3 ? XSTDNON : XSTDERR) &&
            g_script.nKills == (nCase == 3 ? 0 : 3) && g_script.nWaits == (nCase == 2 ? 1 : 3) && !g_script.bInvalid,
            "Shutdown waits even after a stop error and preserves errors from either operation");
        for (int i = 0; i < 3; i++)
            CHECK(api.pWorkerPIDs[i] == (nCase == 2 ? 41001 + i : 0), "Only an unreaped worker remains in the table");
        XAPI_Destroy(&api);
        close(nPeer);
        CHECK(test.nClosed == 1 && !test.nErrors && !api.pWorkerPIDs && !api.nWorkerCount,
            "Every shutdown path releases the parent-owned event and worker table once");
    }
    return 0;
}

static int XTest_wait_single(void)
{
    const int errors[] = {0, 0, EINTR, ECHILD, EIO};
    for (size_t i = 0; i < sizeof(errors) / sizeof(*errors); i++)
    {
        xapi_t api;
        worker_fault_t test = {0};
        int nPeer;
        CHECK(!worker_fault_open(&api, &test, &nPeer) && XAPI_InitWorkers(&api, 3, XFALSE) == XSTDOK,
            "Start a worker group before waiting for one child");
        g_script.nWaitSteps = 1;
        g_script.waitResults[0] = i == 0 ? 41002 : i == 1 ? 49999 : -1;
        g_script.waitErrors[0] = errors[i];
        g_script.waitStatuses[0] = 23 << 8;
        int nWaitStatus = 0;
        xpid_t nPID = XAPI_WaitWorker(&api, &nWaitStatus);
        CHECK(nPID == (i < 2 ? g_script.waitResults[0] : i == 4 ? XSTDERR : XSTDNON) && g_script.nWaits == 1,
            "Single-child waits return the exact PID, tolerate interruptions and absent children, and report real errors");
        CHECK(i >= 2 || (WIFEXITED(nWaitStatus) && WEXITSTATUS(nWaitStatus) == 23),
            "The caller receives the child's actual exit status");
        CHECK(api.pWorkerPIDs[0] == 41001 && api.pWorkerPIDs[1] == (i == 0 ? 0 : 41002) && api.pWorkerPIDs[2] == 41003,
            "Only the known reaped child is removed; unrelated children and wait errors preserve every other slot");
        CHECK(XAPI_WaitWorkers(&api) == XSTDOK && g_script.nWaits == (i == 0 ? 3 : 4) && !g_script.bInvalid,
            "A later group wait still reaps every remaining worker exactly once");
        XAPI_Destroy(&api);
        close(nPeer);
        CHECK(test.nClosed == 1 && !test.nErrors, "Waiting for one child preserves the parent's session ownership");
    }
    return 0;
}

static int XTest_watch_reaped(void)
{
    for (int nStop = 0; nStop < 2; nStop++)
    {
        xapi_t api;
        worker_fault_t test = {0};
        int nPeer;
        CHECK(!worker_fault_open(&api, &test, &nPeer) && XAPI_InitWorkers(&api, 3, XFALSE) == XSTDOK,
            "Create the group before its middle worker exits");
        volatile sig_atomic_t nInterrupted = 0;
        if (nStop) g_script.pStop = &nInterrupted;
        g_script.nWaitSteps = nStop ? 1 : 2;
        g_script.waitResults[0] = 41002;
        g_script.waitResults[1] = -1;
        g_script.waitErrors[1] = ECHILD;
        CHECK(XAPI_WatchWorkers(&api, &nInterrupted) == (nStop ? XSTDOK : XSTDNON) && !g_script.bInvalid,
            "The watcher either replaces the reaped worker or shuts down when interrupted during the wait");
        if (nStop)
        {
            CHECK(g_script.nForks == 3 && g_script.nKills == 2 && g_script.nWaits == 3 &&
                g_script.killPIDs[0] == 41001 && g_script.killPIDs[1] == 41003 &&
                g_script.waitPIDs[0] == -1 && g_script.waitPIDs[1] == 41001 && g_script.waitPIDs[2] == 41003,
                "Shutdown never restarts or signals the reaped child and waits for each surviving sibling exactly once");
            CHECK(!api.pWorkerPIDs[0] && !api.pWorkerPIDs[1] && !api.pWorkerPIDs[2], "Shutdown clears every reaped slot");
        }
        else
            CHECK(g_script.nForks == 4 && !g_script.nKills && g_script.nWaits == 2 &&
                api.pWorkerPIDs[0] == 41001 && api.pWorkerPIDs[1] == 41004 && api.pWorkerPIDs[2] == 41003,
                "Only the exited worker's slot receives the replacement PID; live sibling identities are preserved");
        XAPI_Destroy(&api);
        close(nPeer);
        g_script.pStop = NULL;
        CHECK(test.nClosed == 1 && !test.nErrors, "A replacement or interrupted wait preserves parent cleanup ownership");
    }
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(start_failure),
    XTEST_CASE(wait_failures),
    XTEST_CASE(stop_failures),
    XTEST_CASE(watch_failures),
    XTEST_CASE(watch_shutdown),
    XTEST_CASE(wait_single),
    XTEST_CASE(watch_reaped)
)
