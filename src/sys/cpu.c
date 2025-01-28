/*!
 *  @file libxutils/src/sys/cpu.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of sched_setaffinity.
 */

#ifdef _XUTILS_USE_GNU
#define _GNU_SOURCE
#endif

#include "cpu.h"
#include "sync.h"
#include "str.h"
#include "xfs.h"

#define XCPU_INFO_FILE      "/proc/cpuinfo"
#define XCPU_KEYWORD        "processor"

static XATOMIC g_nCPUCount = 0;

int XCPU_GetCount(void)
{
    xatomic_t nCount = XSYNC_ATOMIC_GET(&g_nCPUCount);
    if (nCount > 0) return (int)nCount;

#ifdef _WIN32
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    nCount = sysInfo.dwNumberOfProcessors;
    XSYNC_ATOMIC_SET(&g_nCPUCount, nCount);
#else
    xbyte_buffer_t buffer;
    int nFoundPosit = 0;
    int nPosit = 0;

    if (XPath_LoadBuffer(XCPU_INFO_FILE, &buffer) <= 0) return XSTDERR;

    do
    {
        nFoundPosit = xstrnsrc(
            (char*)buffer.pData,
            buffer.nUsed,
            XCPU_KEYWORD,
            nPosit
        );

        if (nFoundPosit < 0) break;
        nPosit += nFoundPosit + 1;
        nCount++;
    }
    while (nFoundPosit >= 0);

    XSYNC_ATOMIC_SET(&g_nCPUCount, nCount);
    XByteBuffer_Clear(&buffer);
#endif
    return nCount;
}

int XCPU_SetAffinity(int *pCPUs, size_t nCount, xpid_t nPID)
{
    if (pCPUs == NULL || !nCount) return XSTDERR;
    int nCPUCount = XCPU_GetCount();
    if (nCPUCount <= 0) return XSTDERR;

#ifdef _XUTILS_USE_GNU
    cpu_set_t mask;
    CPU_ZERO(&mask);
    size_t i;

    for (i = 0; i < nCount; i++)
    {
        if (pCPUs[i] >= 0 && 
            pCPUs[i] < nCPUCount)
            CPU_SET(pCPUs[i], &mask);
    }

    if (nPID == XCPU_CALLER_PID) nPID = syscall(SYS_gettid);
    return sched_setaffinity(nPID, sizeof(mask), &mask);
#elif _WIN32
    size_t i = 0;
    for (i = 0; i < nCount; i++)
    {
        if (pCPUs[i] >= 0 && pCPUs[i] < nCPUCount)
        {
            DWORD_PTR nMask = ((DWORD_PTR)1) << pCPUs[i];
            if (!SetThreadAffinityMask(GetCurrentThread(), nMask)) return XSTDERR;
        }
    }
    (void)nPID;
    return XSTDOK;
#else
    return XSTDERR;
#endif
}

int XCPU_SetSingle(int nCPU, xpid_t nPID)
{
    int nCPUS[1];
    nCPUS[0] = nCPU;
    return XCPU_SetAffinity(nCPUS, 1, nPID);
}

int XCPU_AddAffinity(int nCPU, xpid_t nPID)
{
#ifdef _XUTILS_USE_GNU
    int nCPUCount = XCPU_GetCount();
    if (nCPU < 0 || nCPU >= nCPUCount) return XSTDERR;

    cpu_set_t mask;
    CPU_ZERO(&mask);

    if (nPID == XCPU_CALLER_PID) nPID = syscall(SYS_gettid);
    if (sched_getaffinity(nPID, sizeof(mask), &mask) < 0) return XSTDERR;

    if (!CPU_ISSET(nCPU, &mask))
    {
        CPU_SET(nCPU, &mask);
        return sched_setaffinity(nPID, sizeof(mask), &mask);
    }

    return XSTDNON;
#elif _WIN32
    return XCPU_SetSingle(nCPU, nPID);
#else
    return XSTDERR;
#endif
}

int XCPU_DelAffinity(int nCPU, xpid_t nPID)
{
#ifdef _XUTILS_USE_GNU
    int nCPUCount = XCPU_GetCount();
    if (nCPU < 0 || nCPU >= nCPUCount) return XSTDERR;

    cpu_set_t mask;
    CPU_ZERO(&mask);

    if (nPID == XCPU_CALLER_PID) nPID = syscall(SYS_gettid);
    if (sched_getaffinity(nPID, sizeof(mask), &mask) < 0) return XSTDERR;

    if (CPU_ISSET(nCPU, &mask))
    {
        CPU_CLR(nCPU, &mask);
        return sched_setaffinity(nPID, sizeof(mask), &mask);
    }

    return XSTDNON;
#endif
    (void)nPID;
    (void)nCPU;
    return XSTDERR;
}
