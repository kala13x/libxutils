/*!
 *  @file libxutils/src/sys/thread.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of the POSIX thread functionality
 */

#include "xstd.h"
#include "sync.h"
#include "thread.h"

#ifdef _WIN32
static DWORD WINAPI XThread_WinThread(void* pArg)
{
    xthread_t *pThread = (xthread_t*)pArg;
    pThread->functionCb(pThread->pArgument);
    return 0;
}
#endif

void XThread_Init(xthread_t *pThread) 
{
    pThread->nStackSize = XTHREAD_STACK_SIZE;
    pThread->functionCb = NULL;
    pThread->pArgument = NULL;
    pThread->nDetached = 0;
    pThread->nStatus = 0;
}

int XThread_Run(xthread_t *pThread)
{
    if (pThread->functionCb == NULL) return XSTDNON;
    pThread->nStatus = XTHREAD_FAIL;

#ifdef _WIN32
    pThread->threadId = CreateThread(NULL, pThread->nStackSize, XThread_WinThread, pThread, 0, NULL);
    if (pThread->threadId == NULL) return XSTDERR;
    if (pThread->nDetached) CloseHandle(pThread->threadId);
#else
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) ||
        pthread_attr_setstacksize(&attr, pThread->nStackSize))
    {
        fprintf(stderr, "<%s:%d> %s: Can not initialize pthread attribute: %d\n",
            __FILE__, __LINE__, __FUNCTION__, errno);

        pthread_attr_destroy(&attr);
        return XSTDERR;
    }

    if (pThread->nDetached)
    {
        if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) 
        {
            fprintf(stderr, "<%s:%d> %s Can not set detache state to the pthread attribute: %d\n",
                __FILE__, __LINE__, __FUNCTION__, errno);

            pthread_attr_destroy(&attr);
            return XSTDERR;
        }
    }

    if (pthread_create(&pThread->threadId, &attr, pThread->functionCb, pThread->pArgument))
    {
        fprintf(stderr, "<%s:%d> %s Can not create pthread: %d\n",
            __FILE__, __LINE__, __FUNCTION__, errno);

        pthread_attr_destroy(&attr);
        return XSTDERR;
    }

    pthread_attr_destroy(&attr);
#endif

    pThread->nStatus = XTHREAD_SUCCESS;
    return XSTDOK;
}

int XThread_Create(xthread_t *pThread, xthread_cb_t func, void *pArg, uint8_t nDtch)
{
    XThread_Init(pThread);
    pThread->functionCb = func;
    pThread->pArgument = pArg;
    pThread->nDetached = nDtch;
    return XThread_Run(pThread);
}

void* XThread_Join(xthread_t *pThread)
{
    if (pThread->nDetached) return NULL;
    void* pReturnValue = NULL;
    int nStatus = 0;

#ifdef _WIN32
    WaitForSingleObject(pThread->threadId, INFINITE);
    CloseHandle(pThread->threadId);
    pThread->nDetached = 1;
    nStatus = 1;
#else
    pthread_t nTid = pThread->threadId;
    nStatus = pthread_join(nTid, &pReturnValue);
#endif

    return nStatus ? NULL : pReturnValue;
}

void *XTask_WorkerThread(void *pContext)
{
    xtask_t *pTask = (xtask_t*)pContext;
    XSYNC_ATOMIC_SET(&pTask->nStatus, XTASK_STAT_ACTIVE);

    xbool_t bIsPaused = XFALSE;
    xatomic_t nAction = 0;

    while((nAction = XSYNC_ATOMIC_GET(&pTask->nAction)) != XTASK_CTRL_STOP)
    {
        xatomic_t nInterval = XSYNC_ATOMIC_GET(&pTask->nIntervalU);

        if (nAction == XTASK_CTRL_PAUSE)
        {
            if (!bIsPaused)
            {
                XSYNC_ATOMIC_SET(&pTask->nStatus, XTASK_STAT_PAUSED);
                nInterval = nInterval ? nInterval : XTASK_SLEEP_USEC;
                bIsPaused = XTRUE;
            }

            xusleep((uint32_t)nInterval);
            continue;
        }

        if (bIsPaused)
        {
            XSYNC_ATOMIC_SET(&pTask->nStatus, XTASK_STAT_ACTIVE);
            bIsPaused = XFALSE;
        }

        if (pTask->callback(pTask->pContext) < 0) break;
        if (nInterval) xusleep((uint32_t)nInterval);
    }

    XSYNC_ATOMIC_SET(&pTask->nStatus, XTASK_STAT_STOPPED);
    return NULL;
}

int XTask_Start(xtask_t *pTask, xtask_cb_t callback, void *pContext, uint32_t nIntervalU)
{
    if (callback == NULL) return 0;
    pTask->callback = callback;
    pTask->pContext = pContext;

    XSYNC_ATOMIC_SET(&pTask->nAction, XTASK_CTRL_RELEASE);
    XSYNC_ATOMIC_SET(&pTask->nIntervalU, nIntervalU);

    int nStatus = XThread_Create(&pTask->taskTd, XTask_WorkerThread, pTask, 1);
    XSYNC_ATOMIC_SET(&pTask->nStatus, nStatus == XSTDOK ? XTASK_STAT_CREATED : XTASK_STAT_FAIL);

    return nStatus;
}

uint32_t XTask_Wait(xtask_t *pTask, int nEvent, int nIntervalU)
{
    uint32_t nCheckCount = 0;

    while (XSYNC_ATOMIC_GET(&pTask->nStatus) != nEvent)
    {
        if (nIntervalU < 0) break;
        else if (!nIntervalU) continue;

        xusleep((uint32_t)nIntervalU);
        nCheckCount++;
    }

    return nCheckCount * nIntervalU;
}

uint32_t XTask_Hold(xtask_t *pTask, int nIntervalU)
{
    XSYNC_ATOMIC_SET(&pTask->nAction, XTASK_CTRL_PAUSE);
    return XTask_Wait(pTask, XTASK_STAT_PAUSED, nIntervalU);
}

uint32_t XTask_Release(xtask_t *pTask, int nIntervalU)
{
    XSYNC_ATOMIC_SET(&pTask->nAction, XTASK_CTRL_RELEASE);
    return XTask_Wait(pTask, XTASK_STAT_ACTIVE, nIntervalU);
}

uint32_t XTask_Stop(xtask_t *pTask, int nIntervalU)
{
    XSYNC_ATOMIC_SET(&pTask->nAction, XTASK_CTRL_STOP);
    return XTask_Wait(pTask, XTASK_STAT_STOPPED, nIntervalU);
}
