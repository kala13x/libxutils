/*!
 *  @file libxutils/src/sys/sync.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of the various cross-platform 
 * sync functionality with the POSIX mutex and SynchAPI 
 * critical section, rwlocks, atomic built-ins, and etc.
 */

#ifndef __XUTILS_XSYNC_H__
#define __XUTILS_XSYNC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"

typedef struct XSyncMutex {
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;  
#endif
    xbool_t bEnabled;
} xsync_mutex_t;

typedef struct XSyncRW {
#ifdef _WIN32
    xbool_t bExclusive;
    SRWLOCK rwLock;
#else
    pthread_rwlock_t rwLock;
#endif
    xbool_t bEnabled;
} xsync_rw_t;

typedef struct XSyncBar {
    xatomic_t nBar;
    xatomic_t nAck;
} xsync_bar_t;

#ifdef _WIN32
#define XSYNC_ATOMIC_ADD(dst,val) InterlockedExchangeAdd(dst, val)
#define XSYNC_ATOMIC_SUB(dst,val) InterlockedExchangeSubtract(dst, val)
#define XSYNC_ATOMIC_SET(dst,val) InterlockedExchange(dst, val)
#define XSYNC_ATOMIC_GET(dst) InterlockedExchangeAdd(dst, 0)
#else
#define XSYNC_ATOMIC_ADD(dst,val) __sync_add_and_fetch(dst, val)
#define XSYNC_ATOMIC_SUB(dst,val) __sync_sub_and_fetch(dst, val)
#define XSYNC_ATOMIC_SET(dst,val) __sync_lock_test_and_set(dst, val)
#define XSYNC_ATOMIC_GET(dst) __sync_add_and_fetch(dst, 0)
#endif

void xusleep(uint32_t nUsecs);

void XSync_Init(xsync_mutex_t *pSync);
void XSync_Destroy(xsync_mutex_t *pSync);
void XSync_Lock(xsync_mutex_t *pSync);
void XSync_Unlock(xsync_mutex_t *pSync);

void XRWSync_Init(xsync_rw_t *pSync);
void XRWSync_ReadLock(xsync_rw_t *pSync);
void XRWSync_WriteLock(xsync_rw_t *pSync);
void XRWSync_Unlock(xsync_rw_t *pSync);
void XRWSync_Destroy(xsync_rw_t *pSync);

void XSyncBar_Bar(xsync_bar_t *pBar);
void XSyncBar_Ack(xsync_bar_t *pBar);
void XSyncBar_Reset(xsync_bar_t *pBar);
uint8_t XSyncBar_CheckBar(xsync_bar_t *pBar);
uint8_t XSyncBar_CheckAck(xsync_bar_t *pBar);
uint32_t XSyncBar_WaitAck(xsync_bar_t *pBar, uint32_t nSleepUsec);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XSYNC_H__ */
