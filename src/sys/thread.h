/*!
 *  @file libxutils/src/sys/thread.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of the POSIX thread functionality
 */

#ifndef __XUTILS_XTHREAD_H__
#define __XUTILS_XTHREAD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"
#include "sync.h"

#define XTHREAD_STACK_SIZE  409600
#define XTHREAD_SUCCESS     XSTDOK
#define XTHREAD_FAIL        XSTDERR

#define XTASK_SLEEP_USEC    10000
#define XTASK_EMPTY_SET     -1
#define XTASK_STAT_FAIL     0
#define XTASK_STAT_IDLE     1
#define XTASK_STAT_CREATED  3
#define XTASK_STAT_ACTIVE   4
#define XTASK_STAT_PAUSED   5
#define XTASK_STAT_STOPPED  6
#define XTASK_CTRL_RELEASE  7
#define XTASK_CTRL_PAUSE    8
#define XTASK_CTRL_STOP     9

typedef void*(*xthread_cb_t)(void*);
typedef int(*xtask_cb_t)(void*);

typedef struct XThread {
    xthread_cb_t functionCb;
    void* pArgument;

#ifdef _WIN32
    HANDLE threadId;
#else
    pthread_t threadId;
#endif

    uint32_t nStackSize;
    uint8_t nDetached;
    int8_t nStatus;
} xthread_t;

void XThread_Init(xthread_t *pThread);
void* XThread_Join(xthread_t *pThread);
int XThread_Run(xthread_t *pThread);
int XThread_Create(xthread_t *pThread, xthread_cb_t func, void *pArg, uint8_t nDtch);

typedef struct XTask {
    void* pContext;
    xtask_cb_t callback;
    xthread_t taskTd;
    xatomic_t nIntervalU;
    xatomic_t nAction;
    xatomic_t nStatus;
} xtask_t;

int XTask_Start(xtask_t *pTask, xtask_cb_t callback, void *pContext, uint32_t nIntervalU);
uint32_t XTask_Stop(xtask_t *pTask, int nIntervalU);
uint32_t XTask_Hold(xtask_t *pTask, int nIntervalU);
uint32_t XTask_Release(xtask_t *pTask, int nIntervalU);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XTHREAD_H__ */
