/*!
 *  @file libxutils/src/sys/sync.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of the various cross-platform
 * sync functionality with the POSIX mutex and SynchAPI
 * critical section, atomic builtins, rwlocks, and etc.
 */

#include "sync.h"

#if !defined(_WIN32) && !defined(PTHREAD_MUTEX_RECURSIVE)
#define PTHREAD_MUTEX_RECURSIVE PTHREAD_MUTEX_RECURSIVE_NP
#endif

void xusleep(uint32_t nUsecs)
{
#ifdef _WIN32
    DWORD nMilliSec = 0;
    if (nUsecs < 1000) nMilliSec = 1;
    else nMilliSec = nUsecs / 1000;
    Sleep(nMilliSec);
#else
    usleep(nUsecs);
#endif
}

#ifndef _WIN32
/* The pthread functions return their error instead of setting errno */
static int XSync_InitMutex(pthread_mutex_t *pMutex, xbool_t bRecursive)
{
    if (!bRecursive) return pthread_mutex_init(pMutex, NULL);

    pthread_mutexattr_t mutexAttr;
    int nStatus = pthread_mutexattr_init(&mutexAttr);
    if (nStatus) return nStatus;

    nStatus = pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);
    if (!nStatus) nStatus = pthread_mutex_init(pMutex, &mutexAttr);

    pthread_mutexattr_destroy(&mutexAttr);
    return nStatus;
}
#endif

void XSync_InitRecursive(xsync_mutex_t *pSync)
{
#ifndef _WIN32
    int nStatus = XSync_InitMutex(&pSync->mutex, XTRUE);
    if (nStatus)
    {
        fprintf(stderr, "<%s:%d> %s: Can not initialize recursive mutex: %d\n",
            __FILE__, __LINE__, __FUNCTION__, nStatus);

        exit(EXIT_FAILURE);
    }
#else
    InitializeCriticalSection(&pSync->mutex);
#endif

    pSync->bEnabled = XTRUE;
}

void XSync_Init(xsync_mutex_t *pSync)
{
#ifndef _WIN32
    int nStatus = XSync_InitMutex(&pSync->mutex, XFALSE);
    if (nStatus)
    {
        fprintf(stderr, "<%s:%d> %s: Can not initialize mutex: %d\n",
            __FILE__, __LINE__, __FUNCTION__, nStatus);

        exit(EXIT_FAILURE);
    }
#else
    InitializeCriticalSection(&pSync->mutex);
#endif

    pSync->bEnabled = XTRUE;
}

XSTATUS XSync_InitAdv(xsync_mutex_t *pSync, xbool_t bRecursive)
{
#ifndef _WIN32
    int nStatus = XSync_InitMutex(&pSync->mutex, bRecursive);
    if (nStatus)
    {
        fprintf(stderr, "<%s:%d> %s: Can not initialize %smutex: %d\n",
            __FILE__, __LINE__, __FUNCTION__, bRecursive ? "recursive " : "", nStatus);

        return XSTDERR;
    }
#else
    InitializeCriticalSection(&pSync->mutex);
#endif

    pSync->bEnabled = XTRUE;
    return XSTDOK;
}

void XSync_Destroy(xsync_mutex_t *pSync)
{
    if (pSync->bEnabled)
    {
#ifndef _WIN32
        int nStatus = pthread_mutex_destroy(&pSync->mutex);
        if (nStatus)
        {
            fprintf(stderr, "<%s:%d> %s: Can not deinitialize mutex: %d\n",
                __FILE__, __LINE__, __FUNCTION__, nStatus);

            exit(EXIT_FAILURE);
        }
#else
        DeleteCriticalSection(&pSync->mutex);
#endif
    }

    pSync->bEnabled = XFALSE;
}

void XSync_Lock(xsync_mutex_t *pSync)
{
    if (pSync->bEnabled)
    {
#ifndef _WIN32
        int nStatus = pthread_mutex_lock(&pSync->mutex);
        if (nStatus)
        {
            fprintf(stderr, "<%s:%d> %s: Can not lock mutex: %d\n",
                __FILE__, __LINE__, __FUNCTION__, nStatus);

            exit(EXIT_FAILURE);
        }
#else
        EnterCriticalSection(&pSync->mutex);
#endif
    }
}

void XSync_Unlock(xsync_mutex_t *pSync)
{
    if (pSync->bEnabled)
    {
#ifndef _WIN32
        int nStatus = pthread_mutex_unlock(&pSync->mutex);
        if (nStatus)
        {
            fprintf(stderr, "<%s:%d> %s: Can not unlock mutex: %d\n",
                __FILE__, __LINE__, __FUNCTION__, nStatus);

            exit(EXIT_FAILURE);
        }
#else
        LeaveCriticalSection(&pSync->mutex);
#endif
    }
}

void XRWSync_Init(xsync_rw_t *pSync)
{
#ifndef _WIN32
    int nStatus = pthread_rwlock_init(&pSync->rwLock, NULL);
    if (nStatus)
    {
        fprintf(stderr, "<%s:%d> %s: Can not init rwlock: %d\n",
            __FILE__, __LINE__, __FUNCTION__, nStatus);

        exit(EXIT_FAILURE);
    }
#else
    InitializeSRWLock(&pSync->rwLock);
    pSync->bExclusive = XFALSE;
#endif

    pSync->bEnabled = XTRUE;
}

void XRWSync_ReadLock(xsync_rw_t *pSync)
{
    if (pSync->bEnabled)
    {
#ifndef _WIN32
        int nStatus = pthread_rwlock_rdlock(&pSync->rwLock);
        if (nStatus)
        {
            fprintf(stderr, "<%s:%d> %s: Can not read lock rwlock: %d\n",
                __FILE__, __LINE__, __FUNCTION__, nStatus);

            exit(EXIT_FAILURE);
        }
#else
        AcquireSRWLockShared(&pSync->rwLock);
#endif
    }
}

void XRWSync_WriteLock(xsync_rw_t *pSync)
{
    if (pSync->bEnabled)
    {
#ifndef _WIN32
        int nStatus = pthread_rwlock_wrlock(&pSync->rwLock);
        if (nStatus)
        {
            fprintf(stderr, "<%s:%d> %s: Can not write lock rwlock: %d\n",
                __FILE__, __LINE__, __FUNCTION__, nStatus);

            exit(EXIT_FAILURE);
        }
#else
        AcquireSRWLockExclusive(&pSync->rwLock);
        pSync->bExclusive = XTRUE;
#endif
    }
}

void XRWSync_Unlock(xsync_rw_t *pSync)
{
    if (pSync->bEnabled)
    {
#ifndef _WIN32
        int nStatus = pthread_rwlock_unlock(&pSync->rwLock);
        if (nStatus)
        {
            fprintf(stderr, "<%s:%d> %s: Can not unlock rwlock: %d\n",
                __FILE__, __LINE__, __FUNCTION__, nStatus);

            exit(EXIT_FAILURE);
        }
#else
        if (pSync->bExclusive)
        {
            ReleaseSRWLockExclusive(&pSync->rwLock);
            pSync->bExclusive = XFALSE;
        }
        else ReleaseSRWLockShared(&pSync->rwLock);
#endif
    }
}

void XRWSync_Destroy(xsync_rw_t *pSync)
{
    if (pSync->bEnabled)
    {
#ifndef _WIN32
        int nStatus = pthread_rwlock_destroy(&pSync->rwLock);
        if (nStatus)
        {
            fprintf(stderr, "<%s:%d> %s: Can not destroy rwlock: %d\n",
                __FILE__, __LINE__, __FUNCTION__, nStatus);

            exit(EXIT_FAILURE);
        }
#else
        (void)pSync;
#endif
    }

    pSync->bEnabled = XFALSE;
}

void XSyncBar_Bar(xsync_bar_t *pBar)
{
    XSYNC_ATOMIC_SET(&pBar->nBar, 1);
    XSYNC_ATOMIC_SET(&pBar->nAck, 0);
}

void XSyncBar_Ack(xsync_bar_t *pBar)
{
    XSYNC_ATOMIC_SET(&pBar->nAck, 1);
}

void XSyncBar_Reset(xsync_bar_t *pBar)
{
    XSYNC_ATOMIC_SET(&pBar->nBar, 0);
    XSYNC_ATOMIC_SET(&pBar->nAck, 0);
}

uint8_t XSyncBar_CheckBar(xsync_bar_t *pBar)
{
    return XSYNC_ATOMIC_GET(&pBar->nBar) ? XSTDOK : XSTDNON;
}

uint8_t XSyncBar_CheckAck(xsync_bar_t *pBar)
{
    return XSYNC_ATOMIC_GET(&pBar->nAck) ? XSTDOK : XSTDNON;
}

uint32_t XSyncBar_WaitAck(xsync_bar_t *pBar, uint32_t nSleepUsec)
{
    uint32_t nUsecs = 0;

    while (!XSyncBar_CheckAck(pBar))
    {
        if (nSleepUsec) xusleep(nSleepUsec);
        nUsecs += nSleepUsec;
    }

    return nUsecs;
}
