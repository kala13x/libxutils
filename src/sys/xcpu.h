/*!
 *  @file libxutils/src/sys/xcpu.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of sched_setaffinity.
 */

#ifndef __XUTILS_XCPU_H__
#define __XUTILS_XCPU_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"

#define XCPU_CALLER_PID     -1

int XCPU_GetCount(void);
int XCPU_SetSingle(int nCPU, xpid_t nPID);
int XCPU_SetAffinity(int *pCPUs, size_t nCount, xpid_t nPID);
int XCPU_AddAffinity(int nCPU, xpid_t nPID);
int XCPU_DelAffinity(int nCPU, xpid_t nPID);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XCPU_H__ */