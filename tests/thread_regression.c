/* libxutils: joined workers, mutex/RW synchronization and atomic counters. */
#include "test.h"
#include "thread.h"
#ifdef __linux__
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

typedef struct xtest_threads_ {
    xsync_mutex_t mutex;
    xsync_rw_t rw;
    xatomic_t nAtomic;
    unsigned int nMutex;
    unsigned int nRW;
} xtest_threads_t;

static void *XTest_Worker(void *pContext)
{
    xtest_threads_t *pTest = (xtest_threads_t*)pContext;
    for (int i = 0; i < 2000; i++)
    {
        XSync_Lock(&pTest->mutex);
        pTest->nMutex++;
        XSync_Unlock(&pTest->mutex);
        XRWSync_WriteLock(&pTest->rw);
        pTest->nRW++;
        XRWSync_Unlock(&pTest->rw);
        XSYNC_ATOMIC_ADD(&pTest->nAtomic, 1);
    }
    return pContext;
}

static void *XTest_Idle(void *pContext)
{
    return pContext;
}

static int XTest_contention(void)
{
    xtest_threads_t test = {0};
    xthread_t threads[4];
    XSync_Init(&test.mutex);
    XRWSync_Init(&test.rw);
    CHECK(test.mutex.bEnabled && test.rw.bEnabled, "Initialize synchronization primitives");
    for (int i = 0; i < 4; i++) CHECK(XThread_Create(&threads[i], XTest_Worker, &test, XFALSE) == XSTDOK, "Start joined workers");
    for (int i = 0; i < 4; i++)
    {
        void *pResult = XThread_Join(&threads[i]);
#ifndef _WIN32
        CHECK(pResult == &test, "POSIX join returns the worker result");
#else
        (void)pResult;
#endif
    }
    XRWSync_ReadLock(&test.rw);
    unsigned int nRW = test.nRW;
    XRWSync_Unlock(&test.rw);
    CHECK(test.nMutex == 8000 && nRW == 8000 && XSYNC_ATOMIC_GET(&test.nAtomic) == 8000,
        "All synchronized increments survive contention");
    XSync_Destroy(&test.mutex);
    XRWSync_Destroy(&test.rw);
    return 0;
}

static int XTest_recursive(void)
{
    xsync_mutex_t mutex;
    XSync_InitRecursive(&mutex);
    CHECK(mutex.bEnabled, "Initialize recursive mutex");
    XSync_Lock(&mutex);
    XSync_Lock(&mutex);
    XSync_Unlock(&mutex);
    XSync_Unlock(&mutex);
    XSync_Destroy(&mutex);
    CHECK(!mutex.bEnabled, "Destroyed mutex is disabled");
    XSync_Lock(&mutex);
    XSync_Unlock(&mutex);
    return 0;
}

static int XTest_invalid_start(void)
{
    xthread_t thread;
    XThread_Init(&thread);
    CHECK(XThread_Run(&thread) == XSTDNON, "A missing callback starts no worker");
#ifndef _WIN32
    thread.functionCb = XTest_Worker;
    thread.nStackSize = 1;
    CHECK(XThread_Run(&thread) == XSTDERR && thread.nStatus == XTHREAD_FAIL, "Invalid stack size reports startup failure");
    CHECK(errno == EINVAL, "And leaves the reason in errno");
#endif
#ifdef __linux__
    /* Without room for another task the start fails, joinable or detached, and errno says why. Where the limit
       does not bind, as for root, the thread starts and there is nothing to check. */
    for (int nDetached = 0; nDetached < 2; nDetached++)
    {
        pid_t nPid = fork();
        CHECK(nPid >= 0, "Fork a child to limit");
        if (nPid == 0)
        {
            struct rlimit limit = { 1, 1 };
            int nNull = open("/dev/null", O_WRONLY);
            if (nNull >= 0) dup2(nNull, STDERR_FILENO);
            if (setrlimit(RLIMIT_NPROC, &limit) < 0) _exit(2);

            XThread_Init(&thread);
            thread.functionCb = XTest_Idle;
            thread.nDetached = (uint8_t)nDetached;
            errno = 0;
            int nStatus = XThread_Run(&thread);
            if (nStatus == XSTDOK && !nDetached) XThread_Join(&thread);
            if (nStatus == XSTDOK) _exit(0);
            _exit(nStatus == XSTDERR && thread.nStatus == XTHREAD_FAIL && errno == EAGAIN ? 0 : 1);
        }

        int nStatus = 0;
        CHECK(waitpid(nPid, &nStatus, 0) == nPid && WIFEXITED(nStatus), "The child finishes");
        CHECK(WEXITSTATUS(nStatus) == 0, "A start without room fails with EAGAIN");
    }
#endif
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(contention),
    XTEST_CASE(recursive),
    XTEST_CASE(invalid_start)
)
