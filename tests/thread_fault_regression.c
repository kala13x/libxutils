/* Failed POSIX setup must not start a worker or retain initialized attributes. */
#include "test.h"
#include "thread.h"
#include "sync.h"
#include <sys/wait.h>
#include <unistd.h>

enum { THREAD_FAULT_NONE, THREAD_FAULT_ATTR, THREAD_FAULT_STACK, THREAD_FAULT_DETACH, THREAD_FAULT_CREATE,
    MUTEX_FAULT_ATTR, MUTEX_FAULT_TYPE, MUTEX_FAULT_INIT, MUTEX_FAULT_LOCK, MUTEX_FAULT_UNLOCK, MUTEX_FAULT_DESTROY,
    RW_FAULT_INIT, RW_FAULT_READ, RW_FAULT_WRITE, RW_FAULT_UNLOCK, RW_FAULT_DESTROY };

static _Thread_local int g_nFault;
static _Thread_local int g_nHits;
static _Thread_local int g_nAttrs;
static _Thread_local int g_nMutexAttrs;
static _Thread_local xbool_t g_bFatal;

static int thread_fault(int nFault)
{
    if (g_nFault != nFault) return 0;
    g_nHits++;
    return EAGAIN;
}

int __real_pthread_attr_init(pthread_attr_t*);
int __real_pthread_attr_destroy(pthread_attr_t*);
int __real_pthread_attr_setstacksize(pthread_attr_t*, size_t);
int __real_pthread_attr_setdetachstate(pthread_attr_t*, int);
int __real_pthread_create(pthread_t*, const pthread_attr_t*, void*(*)(void*), void*);
int __real_pthread_mutexattr_init(pthread_mutexattr_t*);
int __real_pthread_mutexattr_destroy(pthread_mutexattr_t*);
int __real_pthread_mutexattr_settype(pthread_mutexattr_t*, int);
int __real_pthread_mutex_init(pthread_mutex_t*, const pthread_mutexattr_t*);
int __real_pthread_mutex_lock(pthread_mutex_t*);
int __real_pthread_mutex_unlock(pthread_mutex_t*);
int __real_pthread_mutex_destroy(pthread_mutex_t*);
int __real_pthread_rwlock_init(pthread_rwlock_t*, const pthread_rwlockattr_t*);
int __real_pthread_rwlock_rdlock(pthread_rwlock_t*);
int __real_pthread_rwlock_wrlock(pthread_rwlock_t*);
int __real_pthread_rwlock_unlock(pthread_rwlock_t*);
int __real_pthread_rwlock_destroy(pthread_rwlock_t*);
void __real_exit(int) __attribute__((noreturn));

int __wrap_pthread_attr_init(pthread_attr_t *pAttr)
{
    int nStatus = thread_fault(THREAD_FAULT_ATTR);
    if (nStatus) return nStatus;
    nStatus = __real_pthread_attr_init(pAttr);
    if (!nStatus) g_nAttrs++;
    return nStatus;
}

int __wrap_pthread_attr_destroy(pthread_attr_t *pAttr)
{
    g_nAttrs--;
    return __real_pthread_attr_destroy(pAttr);
}

int __wrap_pthread_attr_setstacksize(pthread_attr_t *pAttr, size_t nSize)
{
    return thread_fault(THREAD_FAULT_STACK) ? EAGAIN : __real_pthread_attr_setstacksize(pAttr, nSize);
}

int __wrap_pthread_attr_setdetachstate(pthread_attr_t *pAttr, int nState)
{
    return thread_fault(THREAD_FAULT_DETACH) ? EAGAIN : __real_pthread_attr_setdetachstate(pAttr, nState);
}

int __wrap_pthread_create(pthread_t *pThread, const pthread_attr_t *pAttr, void*(*pCb)(void*), void *pArg)
{
    return thread_fault(THREAD_FAULT_CREATE) ? EAGAIN : __real_pthread_create(pThread, pAttr, pCb, pArg);
}

int __wrap_pthread_mutexattr_init(pthread_mutexattr_t *pAttr)
{
    int nStatus = thread_fault(MUTEX_FAULT_ATTR);
    if (nStatus) return nStatus;
    nStatus = __real_pthread_mutexattr_init(pAttr);
    if (!nStatus) g_nMutexAttrs++;
    return nStatus;
}

int __wrap_pthread_mutexattr_destroy(pthread_mutexattr_t *pAttr)
{
    g_nMutexAttrs--;
    return __real_pthread_mutexattr_destroy(pAttr);
}

int __wrap_pthread_mutexattr_settype(pthread_mutexattr_t *pAttr, int nType)
{
    return thread_fault(MUTEX_FAULT_TYPE) ? EAGAIN : __real_pthread_mutexattr_settype(pAttr, nType);
}

int __wrap_pthread_mutex_init(pthread_mutex_t *pMutex, const pthread_mutexattr_t *pAttr)
{
    return thread_fault(MUTEX_FAULT_INIT) ? EAGAIN : __real_pthread_mutex_init(pMutex, pAttr);
}

int __wrap_pthread_mutex_lock(pthread_mutex_t *pMutex)
{
    return thread_fault(MUTEX_FAULT_LOCK) ? EAGAIN : __real_pthread_mutex_lock(pMutex);
}

int __wrap_pthread_mutex_unlock(pthread_mutex_t *pMutex)
{
    return thread_fault(MUTEX_FAULT_UNLOCK) ? EAGAIN : __real_pthread_mutex_unlock(pMutex);
}

int __wrap_pthread_mutex_destroy(pthread_mutex_t *pMutex)
{
    return thread_fault(MUTEX_FAULT_DESTROY) ? EAGAIN : __real_pthread_mutex_destroy(pMutex);
}

int __wrap_pthread_rwlock_init(pthread_rwlock_t *pLock, const pthread_rwlockattr_t *pAttr)
{
    return thread_fault(RW_FAULT_INIT) ? EAGAIN : __real_pthread_rwlock_init(pLock, pAttr);
}

int __wrap_pthread_rwlock_rdlock(pthread_rwlock_t *pLock)
{
    return thread_fault(RW_FAULT_READ) ? EAGAIN : __real_pthread_rwlock_rdlock(pLock);
}

int __wrap_pthread_rwlock_wrlock(pthread_rwlock_t *pLock)
{
    return thread_fault(RW_FAULT_WRITE) ? EAGAIN : __real_pthread_rwlock_wrlock(pLock);
}

int __wrap_pthread_rwlock_unlock(pthread_rwlock_t *pLock)
{
    return thread_fault(RW_FAULT_UNLOCK) ? EAGAIN : __real_pthread_rwlock_unlock(pLock);
}

int __wrap_pthread_rwlock_destroy(pthread_rwlock_t *pLock)
{
    return thread_fault(RW_FAULT_DESTROY) ? EAGAIN : __real_pthread_rwlock_destroy(pLock);
}

void __wrap_exit(int nStatus)
{
    if (g_bFatal && (g_nHits != 1 || g_nAttrs || g_nMutexAttrs)) __real_exit(123);
    __real_exit(nStatus);
}

static void *thread_run(void *pArg)
{
    XSYNC_ATOMIC_ADD((xatomic_t*)pArg, 1);
    return pArg;
}

static int XTest_start(void)
{
    for (int nFault = THREAD_FAULT_ATTR; nFault <= THREAD_FAULT_CREATE; nFault++)
    {
        xthread_t thread;
        xatomic_t nCalled = 0;
        g_nFault = nFault;
        g_nHits = 0;
        int nStatus = XThread_Create(&thread, thread_run, &nCalled, nFault == THREAD_FAULT_DETACH);
        int nError = errno;
        CHECK(nStatus == XSTDERR && thread.nStatus == XTHREAD_FAIL && nError == EAGAIN,
            "Thread setup propagates the POSIX error and leaves the thread failed");
        CHECK(g_nHits == 1 && !g_nAttrs && !XSYNC_ATOMIC_GET(&nCalled), "A failed setup retains no attributes or running worker");
        g_nFault = THREAD_FAULT_NONE;
        CHECK(XThread_Create(&thread, thread_run, &nCalled, 0) == XSTDOK, "The thread object can be started after setup failure");
        CHECK(XThread_Join(&thread) == &nCalled && XSYNC_ATOMIC_GET(&nCalled) == 1 && !g_nAttrs,
            "The recovered thread runs exactly once and returns its original argument");
    }
    return 0;
}

static int XTest_mutex(void)
{
    for (int nRecursive = 0; nRecursive < 2; nRecursive++)
        for (int nFault = nRecursive ? MUTEX_FAULT_ATTR : MUTEX_FAULT_INIT; nFault <= MUTEX_FAULT_INIT; nFault++)
        {
            xsync_mutex_t mutex = {0};
            g_nFault = nFault;
            g_nHits = 0;
            CHECK(XSync_InitAdv(&mutex, nRecursive) == XSTDERR && !mutex.bEnabled && g_nHits == 1 && !g_nMutexAttrs,
                "Failed mutex setup does not enable a lock or retain initialized attributes");
            XSync_Destroy(&mutex);
            g_nFault = THREAD_FAULT_NONE;
            CHECK(XSync_InitAdv(&mutex, nRecursive) == XSTDOK, "Mutex initialization can be retried");
            XSync_Lock(&mutex);
            if (nRecursive) XSync_Lock(&mutex);
            if (nRecursive) XSync_Unlock(&mutex);
            XSync_Unlock(&mutex);
            XSync_Destroy(&mutex);
            CHECK(!mutex.bEnabled && !g_nMutexAttrs, "The recovered lock balances recursive ownership and tears down");
        }
    return 0;
}

static int XTest_fatal(void)
{
    for (int nFault = MUTEX_FAULT_ATTR; nFault <= RW_FAULT_DESTROY; nFault++)
    {
        fflush(NULL);
        pid_t nChild = fork();
        CHECK(nChild >= 0, "Fork the fatal-error fixture");
        if (!nChild)
        {
            xsync_mutex_t mutex = {0};
            xsync_rw_t rwlock = {0};
            if (nFault > MUTEX_FAULT_INIT && nFault <= MUTEX_FAULT_DESTROY) XSync_Init(&mutex);
            if (nFault > RW_FAULT_INIT) XRWSync_Init(&rwlock);
            if (nFault == MUTEX_FAULT_UNLOCK) XSync_Lock(&mutex);
            if (nFault == RW_FAULT_UNLOCK) XRWSync_WriteLock(&rwlock);
            g_nFault = nFault;
            g_nHits = 0;
            g_bFatal = XTRUE;
            switch (nFault)
            {
                case MUTEX_FAULT_ATTR: case MUTEX_FAULT_TYPE: XSync_InitRecursive(&mutex); break;
                case MUTEX_FAULT_INIT: XSync_Init(&mutex); break;
                case MUTEX_FAULT_LOCK: XSync_Lock(&mutex); break;
                case MUTEX_FAULT_UNLOCK: XSync_Unlock(&mutex); break;
                case MUTEX_FAULT_DESTROY: XSync_Destroy(&mutex); break;
                case RW_FAULT_INIT: XRWSync_Init(&rwlock); break;
                case RW_FAULT_READ: XRWSync_ReadLock(&rwlock); break;
                case RW_FAULT_WRITE: XRWSync_WriteLock(&rwlock); break;
                case RW_FAULT_UNLOCK: XRWSync_Unlock(&rwlock); break;
                case RW_FAULT_DESTROY: XRWSync_Destroy(&rwlock); break;
            }
            _exit(122);
        }
        int nStatus = 0;
        CHECK(waitpid(nChild, &nStatus, 0) == nChild && WIFEXITED(nStatus) && WEXITSTATUS(nStatus) == EXIT_FAILURE,
            "A failed mandatory lock operation exits once instead of returning an unprotected critical section");
    }
    return 0;
}

static int task_run(void *pArg)
{
    XSYNC_ATOMIC_ADD((xatomic_t*)pArg, 1);
    return XSTDERR;
}

static int XTest_task(void)
{
    for (int nFault = THREAD_FAULT_ATTR; nFault <= THREAD_FAULT_CREATE; nFault++)
    {
        xtask_t task = {0};
        xatomic_t nCalled = 0;
        g_nFault = nFault;
        g_nHits = 0;
        CHECK(XTask_Start(&task, task_run, &nCalled, 1000) == XSTDERR &&
            XSYNC_ATOMIC_GET(&task.nStatus) == XTASK_STAT_FAIL && !XSYNC_ATOMIC_GET(&nCalled) && !g_nAttrs && g_nHits == 1,
            "A task whose worker cannot start reports failure without invoking its callback");
        g_nFault = THREAD_FAULT_NONE;
        CHECK(XTask_Start(&task, task_run, &nCalled, 1000) == XSTDOK, "The failed task can be started again");
        for (int i = 0; i < 1000 && XSYNC_ATOMIC_GET(&task.nStatus) != XTASK_STAT_STOPPED; i++) xusleep(1000);
        CHECK(XSYNC_ATOMIC_GET(&task.nStatus) == XTASK_STAT_STOPPED && XSYNC_ATOMIC_GET(&nCalled) == 1,
            "The recovered task invokes its callback once and honors its stop result");
    }
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(start),
    XTEST_CASE(mutex),
    XTEST_CASE(fatal),
    XTEST_CASE(task)
)
