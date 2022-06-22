/*!
 *  @file libxutils/src/xtop.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Get process and system statistics about 
 * cpu usage, network usage, memory usage, etc...
 */

#ifndef __XUTILS_XPROC_H__
#define __XUTILS_XPROC_H__

#include "xstd.h"
#include "thread.h"
#include "array.h"
#include "sync.h"

#define XNET_HWADDR_DEFAULT         "00:00:00:00:00:00"
#define XNET_IPADDR_DEFAULT         "0.0.0.0"

#define XSYS_CLASS_NET              "/sys/class/net"
#define XPROC_FILE_PIDSTATUS        "/proc/self/status"
#define XPROC_FILE_PIDSTAT          "/proc/self/stat"
#define XPROC_FILE_LOADAVG          "/proc/loadavg"
#define XPROC_FILE_MEMINFO          "/proc/meminfo"
#define XPROC_FILE_UPTIME           "/proc/uptime"
#define XPROC_FILE_STAT             "/proc/stat"

#define XTOP_INTERVAL_USEC  1000000
#define XMEMBERS_MAX        128

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XNetIface {
    uint64_t nPacketsReceivedPerSec;
    uint64_t nBytesReceivedPerSec;
    uint64_t nPacketsSentPerSec;
    uint64_t nBytesSentPerSec;
    uint16_t nMemberCount;

    int64_t nPacketsReceived;
    int64_t nBytesReceived;
    int64_t nPacketsSent;
    int64_t nBytesSent;
    int64_t nBandwidth;
    int32_t nType;

    char sName[XNAME_MAX];
    char sHWAddr[XADDR_MAX];
    char sIPAddr[XADDR_MAX];
    char sMembers[XMEMBERS_MAX][XNAME_MAX];
} xnet_iface_t;

typedef struct XMemInfo {
    uint64_t nResidentMemory;
    uint64_t nVirtualMemory;
    uint64_t nMemoryCached;
    uint64_t nMemoryShared;
    uint64_t nMemoryAvail;
    uint64_t nMemoryTotal;
    uint64_t nMemoryFree;
    uint64_t nReclaimable;
    uint64_t nSwapCached;
    uint64_t nSwapTotal;
    uint64_t nSwapFree;
    uint64_t nBuffers;
} xmem_info_t;

typedef struct XCPUInfo {
    int nID;

    /* Calculated percents about CPU usage */
    uint32_t nSoftInterrupts;
    uint32_t nHardInterrupts;
    uint32_t nUserSpaceNiced;
    uint32_t nKernelSpace;
    uint32_t nUserSpace;
    uint32_t nIdleTime;
    uint32_t nIOWait;
    uint32_t nStealTime;
    uint32_t nGuestTime;
    uint32_t nGuestNiced;

    /* Raw information for later percentage calculations */
    uint32_t nSoftInterruptsRaw;
    uint32_t nHardInterruptsRaw;
    uint32_t nUserSpaceNicedRaw;
    uint32_t nKernelSpaceRaw;
    uint32_t nUserSpaceRaw;
    uint32_t nIdleTimeRaw;
    uint32_t nIOWaitRaw;
    uint32_t nStealRaw;
    uint32_t nGuestRaw;
    uint32_t nGuestNicedRaw;
    uint64_t nTotalRaw;
} xcpu_info_t;

typedef struct XProcInfo {
    /* Calculated percents about CPU usage by this process */
    uint32_t nKernelSpaceUsage;
    uint32_t nUserSpaceUsage;

    /* Raw information for later percentage calculations */
    int64_t nKernelSpaceChilds;		// Amount of time that this process's waited-for children have been scheduled in kernel mode
    int64_t nUserSpaceChilds;		// Amount of time that this process's waited-for children have been scheduled in user mode
    uint64_t nKernelSpace;			// Amount of time that this process has been scheduled in kernel mode
    uint64_t nUserSpace;			// Amount of time that this process has been scheduled in user mode
    uint64_t nTotalTime;			// Total amout of time that this process has been sheduled in any mode
} xproc_info_t;

typedef struct XCPUStats {
    uint32_t nLoadAvg[3];
    uint16_t nCoreCount;
    xproc_info_t usage;
    xcpu_info_t sum;
    xarray_t cores;
} xcpu_stats_t;

typedef struct XTopStats {
    uint32_t nIntervalU;
    xsync_mutex_t netLock;
    xcpu_stats_t cpuStats;
    xmem_info_t memInfo;
    xatomic_t nLoadDone;
    xarray_t netIfaces;
    xtask_t monitoring;
    xpid_t nPID;
} xtop_stats_t;

#ifndef _WIN32
int XTop_InitStats(xtop_stats_t *pStats);
void XTop_DestroyStats(xtop_stats_t *pStats);

int XTop_StartMonitoring(xtop_stats_t *pStats, uint32_t nIntervalU, xpid_t nPID);
uint32_t XTop_StopMonitoring(xtop_stats_t *pStats, uint32_t nWaitUsecs);
uint32_t XTop_WaitLoad(xtop_stats_t *pStats, uint32_t nWaitUsecs);

int XTop_GetNetworkStats(xtop_stats_t *pStats, xarray_t *pIfaces);
int XTop_GetMemoryInfo(xtop_stats_t *pStats, xmem_info_t *pInfo);
int XTop_GetCPUStats(xtop_stats_t *pStats, xcpu_stats_t *pCpuStats);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XPROC_H__ */
