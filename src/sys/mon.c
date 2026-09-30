/*!
 *  @file libxutils/src/sys/mon.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief xutils resource monitor implementation.
 * CPU usage, network usage, memory usage, etc...
 */

#include "xstd.h"
#include "addr.h"
#include "type.h"
#include "str.h"
#include "mon.h"
#include "xfs.h"

#define XPROC_BUFFER_SIZE           2048
#define XSYS_CLASS_HWMON            "/sys/class/hwmon"
#define XSYS_CPU_TOPOLOGY_CORE      "/sys/devices/system/cpu/cpu%d/topology/core_id"

void XMon_ClearCb(xarray_data_t *pArrData)
{
    if (pArrData == NULL) return;
    free(pArrData->pData);
}

#ifndef _WIN32
int XMon_GetMemoryInfo(xmon_stats_t *pStats, xmem_info_t *pMemInfo)
{
    pMemInfo->nResidentMemory = XSYNC_ATOMIC_GET(&pStats->memInfo.nResidentMemory);
    pMemInfo->nVirtualMemory = XSYNC_ATOMIC_GET(&pStats->memInfo.nVirtualMemory);
    pMemInfo->nMemoryShared = XSYNC_ATOMIC_GET(&pStats->memInfo.nMemoryShared);
    pMemInfo->nMemoryCached = XSYNC_ATOMIC_GET(&pStats->memInfo.nMemoryCached);
    pMemInfo->nReclaimable = XSYNC_ATOMIC_GET(&pStats->memInfo.nReclaimable);
    pMemInfo->nMemoryAvail = XSYNC_ATOMIC_GET(&pStats->memInfo.nMemoryAvail);
    pMemInfo->nMemoryTotal = XSYNC_ATOMIC_GET(&pStats->memInfo.nMemoryTotal);
    pMemInfo->nMemoryFree = XSYNC_ATOMIC_GET(&pStats->memInfo.nMemoryFree);
    pMemInfo->nSwapCached = XSYNC_ATOMIC_GET(&pStats->memInfo.nSwapCached);
    pMemInfo->nSwapTotal = XSYNC_ATOMIC_GET(&pStats->memInfo.nSwapTotal);
    pMemInfo->nSwapFree = XSYNC_ATOMIC_GET(&pStats->memInfo.nSwapFree);
    pMemInfo->nBuffers = XSYNC_ATOMIC_GET(&pStats->memInfo.nBuffers);
    return pMemInfo->nMemoryTotal;
}

static void XMon_CopyCPUUsage(xproc_info_t *pDstUsage, xproc_info_t *pSrcUsage)
{
    pDstUsage->nUserSpaceChilds = XSYNC_ATOMIC_GET(&pSrcUsage->nUserSpaceChilds);
    pDstUsage->nKernelSpaceChilds = XSYNC_ATOMIC_GET(&pSrcUsage->nKernelSpaceChilds);
    pDstUsage->nUserSpace = XSYNC_ATOMIC_GET(&pSrcUsage->nUserSpace);
    pDstUsage->nKernelSpace = XSYNC_ATOMIC_GET(&pSrcUsage->nKernelSpace);
    pDstUsage->nTotalTime = XSYNC_ATOMIC_GET(&pSrcUsage->nTotalTime);
    pDstUsage->nUserSpaceUsage = XSYNC_ATOMIC_GET(&pSrcUsage->nUserSpaceUsage);
    pDstUsage->nKernelSpaceUsage = XSYNC_ATOMIC_GET(&pSrcUsage->nKernelSpaceUsage);
}

static void XMon_CopyCPUInfo(xcpu_info_t *pDstInfo, xcpu_info_t *pSrcInfo)
{
    pDstInfo->nSoftInterruptsRaw = XSYNC_ATOMIC_GET(&pSrcInfo->nSoftInterruptsRaw);
    pDstInfo->nHardInterruptsRaw = XSYNC_ATOMIC_GET(&pSrcInfo->nHardInterruptsRaw);
    pDstInfo->nKernelSpaceRaw = XSYNC_ATOMIC_GET(&pSrcInfo->nKernelSpaceRaw);
    pDstInfo->nUserSpaceNicedRaw = XSYNC_ATOMIC_GET(&pSrcInfo->nUserSpaceNicedRaw);
    pDstInfo->nGuestNicedRaw = XSYNC_ATOMIC_GET(&pSrcInfo->nGuestNicedRaw);
    pDstInfo->nUserSpaceRaw = XSYNC_ATOMIC_GET(&pSrcInfo->nUserSpaceRaw);
    pDstInfo->nIdleTimeRaw = XSYNC_ATOMIC_GET(&pSrcInfo->nIdleTimeRaw);
    pDstInfo->nIOWaitRaw = XSYNC_ATOMIC_GET(&pSrcInfo->nIOWaitRaw);
    pDstInfo->nStealRaw = XSYNC_ATOMIC_GET(&pSrcInfo->nStealRaw);
    pDstInfo->nGuestRaw = XSYNC_ATOMIC_GET(&pSrcInfo->nGuestRaw);
    pDstInfo->nTotalRaw = XSYNC_ATOMIC_GET(&pSrcInfo->nTotalRaw);
    pDstInfo->nSoftInterrupts = XSYNC_ATOMIC_GET(&pSrcInfo->nSoftInterrupts);
    pDstInfo->nHardInterrupts = XSYNC_ATOMIC_GET(&pSrcInfo->nHardInterrupts);
    pDstInfo->nKernelSpace = XSYNC_ATOMIC_GET(&pSrcInfo->nKernelSpace);
    pDstInfo->nUserSpaceNiced = XSYNC_ATOMIC_GET(&pSrcInfo->nUserSpaceNiced);
    pDstInfo->nGuestNiced = XSYNC_ATOMIC_GET(&pSrcInfo->nGuestNiced);
    pDstInfo->nUserSpace = XSYNC_ATOMIC_GET(&pSrcInfo->nUserSpace);
    pDstInfo->nIdleTime = XSYNC_ATOMIC_GET(&pSrcInfo->nIdleTime);
    pDstInfo->nIOWait = XSYNC_ATOMIC_GET(&pSrcInfo->nIOWait);
    pDstInfo->nStealTime = XSYNC_ATOMIC_GET(&pSrcInfo->nStealTime);
    pDstInfo->nGuestTime = XSYNC_ATOMIC_GET(&pSrcInfo->nGuestTime);
    pDstInfo->nActive = XSYNC_ATOMIC_GET(&pSrcInfo->nActive);
    pDstInfo->nID = XSYNC_ATOMIC_GET(&pSrcInfo->nID);
    pDstInfo->nTemperature = XSYNC_ATOMIC_GET(&pSrcInfo->nTemperature);
}

int XMon_GetCPUStats(xmon_stats_t *pStats, xcpu_stats_t *pCpuStats)
{
    pCpuStats->nLoadAvg[0] = pCpuStats->nLoadAvg[1] = 0;
    pCpuStats->nLoadAvg[2] = pCpuStats->cores.nUsed = 0;
    pCpuStats->nCoreCount = 0;

    XSync_Lock(&pStats->netLock);
    int i, nCPUCores = pStats->cpuStats.cores.nUsed;
    if (nCPUCores <= 0)
    {
        XSync_Unlock(&pStats->netLock);
        return 0;
    }

    if (XArray_InitPool(&pCpuStats->cores, 0, 1, 0) == NULL)
    {
        XSync_Unlock(&pStats->netLock);
        return -1;
    }
    pCpuStats->cores.clearCb = XMon_ClearCb;

    XMon_CopyCPUUsage(&pCpuStats->usage, &pStats->cpuStats.usage);
    XMon_CopyCPUInfo(&pCpuStats->sum, &pStats->cpuStats.sum);

    for (i = 0; i < nCPUCores; i++)
    {
        xcpu_info_t *pSrcInfo = (xcpu_info_t*)XArray_GetData(&pStats->cpuStats.cores, i);
        if (pSrcInfo == NULL) continue;

        xcpu_info_t *pDstInfo = (xcpu_info_t*)malloc(sizeof(xcpu_info_t));
        if (pDstInfo == NULL) continue;

        XMon_CopyCPUInfo(pDstInfo, pSrcInfo);
        int nStatus = XArray_AddData(&pCpuStats->cores, pDstInfo, 0);
        if (nStatus < 0) free(pDstInfo);
    }

    pCpuStats->nLoadAvg[0] = XSYNC_ATOMIC_GET(&pStats->cpuStats.nLoadAvg[0]);
    pCpuStats->nLoadAvg[1] = XSYNC_ATOMIC_GET(&pStats->cpuStats.nLoadAvg[1]);
    pCpuStats->nLoadAvg[2] = XSYNC_ATOMIC_GET(&pStats->cpuStats.nLoadAvg[2]);
    XSync_Unlock(&pStats->netLock);

    pCpuStats->nCoreCount = pCpuStats->cores.nUsed;
    if (pCpuStats->nCoreCount) return pCpuStats->nCoreCount;

    XArray_Destroy(&pCpuStats->cores);
    return -2;
}

int XMon_GetNetworkStats(xmon_stats_t *pStats, xarray_t *pIfaces)
{
    XSync_Lock(&pStats->netLock);

    if (!pStats->netIfaces.nUsed ||
        !XArray_InitPool(pIfaces, 0, 1, 0))
    {
        XSync_Unlock(&pStats->netLock);
        return 0;
    }

    pIfaces->clearCb = XMon_ClearCb;
    int i, nUsed = pStats->netIfaces.nUsed;

    for (i = 0; i < nUsed; i++)
    {
        xnet_iface_t *pSrcIface = (xnet_iface_t*)XArray_GetData(&pStats->netIfaces, i);
        if (pSrcIface == NULL || !pSrcIface->bActive) continue;

        xnet_iface_t *pDstIface = (xnet_iface_t*)malloc(sizeof(xnet_iface_t));
        if (pDstIface == NULL) continue;

        memcpy(pDstIface, pSrcIface, sizeof(xnet_iface_t));
        if (XArray_AddData(pIfaces, pDstIface, 0) < 0) free(pDstIface);
    }

    XSync_Unlock(&pStats->netLock);
    return pIfaces->nUsed;
}

static uint64_t XMon_ParseMemInfo(char *pBuffer, size_t nBuffSize, const char *pField)
{
    const char *pEnd = pBuffer + nBuffSize;
    char *pOffset = strstr(pBuffer, pField);
    if (pOffset == NULL) return 0;
    pOffset += strlen(pField) + 1;
    if (pOffset >= pEnd) return 0;
    return atoll(pOffset);
}

static uint64_t XMon_NetworkRate(int64_t nCurrent, int64_t nPrevious, uint32_t nInterval)
{
    if (!nInterval || nPrevious <= 0 || nCurrent <= nPrevious) return 0;
    uint64_t nDelta = (uint64_t)(nCurrent - nPrevious);
    if (!(nInterval % XMON_INTERVAL_USEC)) return nDelta / (nInterval / XMON_INTERVAL_USEC);

    uint64_t nWhole = nDelta / nInterval;
    uint64_t nFraction = (nDelta % nInterval) * XMON_INTERVAL_USEC / nInterval;
    if (nWhole > (UINT64_MAX - nFraction) / XMON_INTERVAL_USEC) return UINT64_MAX;
    return nWhole * XMON_INTERVAL_USEC + nFraction;
}

static void XMon_UpdateNetworkStats(xmon_stats_t *pStats)
{
    DIR *pDir = opendir(XSYS_CLASS_NET);
    if (pDir == NULL) return;

    XSync_Lock(&pStats->netLock);
    xarray_t *pIfaces = &pStats->netIfaces;
    size_t nPrevCount = pIfaces->nUsed;
    unsigned char sActive[256] = {0};
    unsigned char *pActive = nPrevCount > sizeof(sActive) ? (unsigned char*)calloc(nPrevCount, 1) : sActive;
    if (pActive == NULL)
    {
        XSync_Unlock(&pStats->netLock);
        closedir(pDir);
        return;
    }
    XSync_Unlock(&pStats->netLock);

    struct dirent *pEntry = readdir(pDir);
    while(pEntry != NULL)
    {
        /* Found an entry, but ignore . and .. */
        if (!strcmp(".", pEntry->d_name) ||
            !strcmp("..", pEntry->d_name))
        {
            pEntry = readdir(pDir);
            continue;
        }

        char sBuffer[XPROC_BUFFER_SIZE];
        char sIfacePath[XPATH_MAX];
        xbool_t nHaveIface = XFALSE;

        xnet_iface_t netIface;
        memset(&netIface, 0, sizeof(netIface));

        xstrncpy(netIface.sName, sizeof(netIface.sName), pEntry->d_name);
        xstrncpyf(sIfacePath, sizeof(sIfacePath), "%s/%s/address", XSYS_CLASS_NET, netIface.sName);

        if (XPath_Read(sIfacePath, (uint8_t*)sBuffer, sizeof(sBuffer)) > 0)
        {
            char *pSavePtr = NULL;
            xstrncpy(netIface.sHWAddr, sizeof(netIface.sHWAddr), sBuffer);
            strtok_r(netIface.sHWAddr, "\n", &pSavePtr);
            if (netIface.sHWAddr[0] == '\n') xstrnul(netIface.sHWAddr);
        }

        xstrncpyf(sIfacePath, sizeof(sIfacePath), "%s/%s/type", XSYS_CLASS_NET, netIface.sName);
        if (XPath_Read(sIfacePath, (uint8_t*)sBuffer, sizeof(sBuffer)) > 0) netIface.nType = atol(sBuffer);

        xstrncpyf(sIfacePath, sizeof(sIfacePath), "%s/%s/speed", XSYS_CLASS_NET, netIface.sName);
        if (XPath_Read(sIfacePath, (uint8_t*)sBuffer, sizeof(sBuffer)) > 0) netIface.nBandwidth = atol(sBuffer);

        xstrncpyf(sIfacePath, sizeof(sIfacePath), "%s/%s/statistics/rx_bytes", XSYS_CLASS_NET, netIface.sName);
        if (XPath_Read(sIfacePath, (uint8_t*)sBuffer, sizeof(sBuffer)) > 0) netIface.nBytesReceived = atol(sBuffer);

        xstrncpyf(sIfacePath, sizeof(sIfacePath), "%s/%s/statistics/tx_bytes", XSYS_CLASS_NET, netIface.sName);
        if (XPath_Read(sIfacePath, (uint8_t*)sBuffer, sizeof(sBuffer)) > 0) netIface.nBytesSent = atol(sBuffer);

        xstrncpyf(sIfacePath, sizeof(sIfacePath), "%s/%s/statistics/rx_packets", XSYS_CLASS_NET, netIface.sName);
        if (XPath_Read(sIfacePath, (uint8_t*)sBuffer, sizeof(sBuffer)) > 0) netIface.nPacketsReceived = atol(sBuffer);

        xstrncpyf(sIfacePath, sizeof(sIfacePath), "%s/%s/statistics/tx_packets", XSYS_CLASS_NET, netIface.sName);
        if (XPath_Read(sIfacePath, (uint8_t*)sBuffer, sizeof(sBuffer)) > 0) netIface.nPacketsSent = atol(sBuffer);

        if (netIface.nBandwidth < 0) netIface.nBandwidth = 0;
        if (!xstrused(netIface.sHWAddr)) xstrncpy(netIface.sHWAddr, sizeof(netIface.sHWAddr), XNET_HWADDR_DEFAULT);

        xstrncpyf(sIfacePath, sizeof(sIfacePath), "%s/%s", XSYS_CLASS_NET, netIface.sName);
        DIR *pIfaceDir = opendir(sIfacePath);

        if (pIfaceDir != NULL)
        {
            struct dirent *pIfaceEntry = readdir(pIfaceDir);
            while(pIfaceEntry != NULL)
            {
                /* Found an entry, but ignore . and .. */
                if (!strcmp(".", pIfaceEntry->d_name) ||
                    !strcmp("..", pIfaceEntry->d_name))
                    {
                        pIfaceEntry = readdir(pIfaceDir);
                        continue;
                    }

                /* The member list is a fixed array: a bridge or bond with more
                   members than it holds must stop filling it, not run past it. */
                if ((!strncmp(pIfaceEntry->d_name, "slave_", 6) ||
                     !strncmp(pIfaceEntry->d_name, "upper_", 6)) &&
                     netIface.nMemberCount < XMEMBERS_MAX)
                {
                    xstrncpy(netIface.sMembers[netIface.nMemberCount++], XNAME_MAX, &pIfaceEntry->d_name[6]);
                }

                pIfaceEntry = readdir(pIfaceDir);
            }

            closedir(pIfaceDir);
        }

        if (XAddr_GetIFCIP(netIface.sName, netIface.sIPAddr, sizeof(netIface.sIPAddr)) <= 0)
            xstrncpy(netIface.sIPAddr, sizeof(netIface.sIPAddr), XNET_IPADDR_DEFAULT);

        XSync_Lock(&pStats->netLock);
        if (pIfaces->nUsed > 0)
        {
            unsigned int i;
            for (i = 0; i < pIfaces->nUsed; i++)
            {
                xnet_iface_t *pIface = (xnet_iface_t*)XArray_GetData(pIfaces, i);
                if (!strcmp(pIface->sName, netIface.sName))
                {
                    netIface.nBytesReceivedPerSec = XMon_NetworkRate(netIface.nBytesReceived, pIface->nBytesReceived, pStats->nIntervalU);
                    netIface.nPacketsReceivedPerSec = XMon_NetworkRate(netIface.nPacketsReceived, pIface->nPacketsReceived, pStats->nIntervalU);
                    netIface.nBytesSentPerSec = XMon_NetworkRate(netIface.nBytesSent, pIface->nBytesSent, pStats->nIntervalU);
                    netIface.nPacketsSentPerSec = XMon_NetworkRate(netIface.nPacketsSent, pIface->nPacketsSent, pStats->nIntervalU);

                    memcpy(pIface, &netIface, sizeof(xnet_iface_t));

                    pIface->bActive = XTRUE;
                    nHaveIface = XTRUE;
                    if (i < nPrevCount) pActive[i] = 1;
                }
            }
        }

        if (!nHaveIface)
        {
            xnet_iface_t *pNewIface = (xnet_iface_t*)malloc(sizeof(xnet_iface_t));
            if (pNewIface != NULL)
            {
                memcpy(pNewIface, &netIface, sizeof(xnet_iface_t));
                pNewIface->bActive = XTRUE;

                if (XArray_AddData(pIfaces, pNewIface, 0) < 0) free(pNewIface);
            }
        }
        XSync_Unlock(&pStats->netLock);

        pEntry = readdir(pDir);
    }

    /* Remove unused interfaces */
    XSync_Lock(&pStats->netLock);
    while (nPrevCount)
    {
        nPrevCount--;
        if (!pActive[nPrevCount]) XArray_Delete(pIfaces, nPrevCount);
    }

    XSync_Unlock(&pStats->netLock);
    closedir(pDir);
    if (pActive != sActive) free(pActive);
}

static uint8_t XMon_UpdateMemoryInfo(xmem_info_t *pDstInfo, xpid_t nPID)
{
    char sBuffer[XPROC_BUFFER_SIZE];

    if (XPath_Read(XPROC_FILE_MEMINFO, (uint8_t*)sBuffer, sizeof(sBuffer)) <= 0) return 0;
    XSYNC_ATOMIC_SET(&pDstInfo->nMemoryTotal, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "MemTotal"));
    XSYNC_ATOMIC_SET(&pDstInfo->nMemoryFree, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "MemFree"));
    XSYNC_ATOMIC_SET(&pDstInfo->nMemoryShared, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "Shmem"));
    XSYNC_ATOMIC_SET(&pDstInfo->nMemoryCached, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "Cached"));
    XSYNC_ATOMIC_SET(&pDstInfo->nReclaimable, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "SReclaimable"));
    XSYNC_ATOMIC_SET(&pDstInfo->nMemoryAvail, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "MemAvailable"));
    XSYNC_ATOMIC_SET(&pDstInfo->nBuffers, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "Buffers"));
    XSYNC_ATOMIC_SET(&pDstInfo->nSwapCached, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "SwapCached"));
    XSYNC_ATOMIC_SET(&pDstInfo->nSwapTotal, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "SwapTotal"));
    XSYNC_ATOMIC_SET(&pDstInfo->nSwapFree, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "SwapFree"));

    char sPath[XPATH_MAX];
    if (nPID <= 0) xstrncpy(sPath, sizeof(sPath), XPROC_FILE_PIDSTATUS);
    else xstrncpyf(sPath, sizeof(sPath), "/proc/%d/status", nPID);

    if (XPath_Read(sPath, (uint8_t*)sBuffer, sizeof(sBuffer)) <= 0) return 0;
    XSYNC_ATOMIC_SET(&pDstInfo->nResidentMemory, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "VmRSS"));
    XSYNC_ATOMIC_SET(&pDstInfo->nVirtualMemory, XMon_ParseMemInfo(sBuffer, sizeof(sBuffer), "VmSize"));

    return 1;
}

static int XMon_ParseHWMONTempIndex(const char *pName, const char *pSuffix)
{
    int nIndex = 0;
    size_t i = 4;

    if (strncmp(pName, "temp", 4)) return 0;
    while (isdigit((unsigned char)pName[i]))
    {
        if (nIndex > (INT_MAX - (pName[i] - '0')) / 10) return 0;
        nIndex = nIndex * 10 + (pName[i] - '0');
        i++;
    }

    if (!nIndex || strcmp(&pName[i], pSuffix)) return 0;
    return nIndex;
}

static uint32_t XMon_ReadHWMONTempInput(const char *pHWMONPath, int nIndex)
{
    char sBuffer[XPROC_BUFFER_SIZE];
    char sPath[XPATH_MAX];

    xstrncpyf(sPath, sizeof(sPath), "%s/temp%d_input", pHWMONPath, nIndex);
    if (XPath_Read(sPath, (uint8_t*)sBuffer, sizeof(sBuffer)) <= 0) return 0;

    errno = 0;
    int64_t nTemperature = strtoll(sBuffer, NULL, 10);
    return errno != ERANGE && nTemperature > 0 && nTemperature <= UINT32_MAX ? (uint32_t)nTemperature : 0;
}

static uint32_t XMon_ReadFirstHWMONTemp(const char *pHWMONPath)
{
    uint32_t nTemperature = XMon_ReadHWMONTempInput(pHWMONPath, 1);
    if (nTemperature) return nTemperature;

    DIR *pDir = opendir(pHWMONPath);
    if (pDir == NULL) return 0;

    struct dirent *pEntry = readdir(pDir);
    while(pEntry != NULL)
    {
        int nIndex = XMon_ParseHWMONTempIndex(pEntry->d_name, "_input");
        if (nIndex > 1)
        {
            nTemperature = XMon_ReadHWMONTempInput(pHWMONPath, nIndex);
            if (nTemperature)
            {
                closedir(pDir);
                return nTemperature;
            }
        }

        pEntry = readdir(pDir);
    }

    closedir(pDir);
    return 0;
}

static xbool_t XMon_HWMONNameIsCPU(const char *pHWMONPath)
{
    char sBuffer[XPROC_BUFFER_SIZE];
    char sPath[XPATH_MAX];

    xstrncpyf(sPath, sizeof(sPath), "%s/name", pHWMONPath);
    if (XPath_Read(sPath, (uint8_t*)sBuffer, sizeof(sBuffer)) <= 0) return XFALSE;

    if (strstr(sBuffer, "coretemp") != NULL) return XTRUE;
    if (strstr(sBuffer, "k10temp") != NULL) return XTRUE;
    if (strstr(sBuffer, "zenpower") != NULL) return XTRUE;
    if (strstr(sBuffer, "acpitz") != NULL) return XTRUE;
    if (strstr(sBuffer, "cpu") != NULL) return XTRUE;

    return strstr(sBuffer, "soc") != NULL ? XTRUE : XFALSE;
}

static uint32_t XMon_ReadHWMONTempLabel(const char *pHWMONPath, const char *pLabel)
{
    DIR *pDir = opendir(pHWMONPath);
    if (pDir == NULL) return 0;

    char sBuffer[XPROC_BUFFER_SIZE];
    char sPath[XPATH_MAX];
    uint32_t nTemperature = 0;

    struct dirent *pEntry = readdir(pDir);
    while(pEntry != NULL)
    {
        int nIndex = XMon_ParseHWMONTempIndex(pEntry->d_name, "_label");
        if (!nIndex)
        {
            pEntry = readdir(pDir);
            continue;
        }

        xstrncpyf(sPath, sizeof(sPath), "%s/%s", pHWMONPath, pEntry->d_name);
        if (XPath_Read(sPath, (uint8_t*)sBuffer, sizeof(sBuffer)) > 0 &&
            strstr(sBuffer, pLabel) != NULL)
        {
            nTemperature = XMon_ReadHWMONTempInput(pHWMONPath, nIndex);
            if (nTemperature) break;
        }

        pEntry = readdir(pDir);
    }

    closedir(pDir);
    return nTemperature;
}

static uint32_t XMon_ReadHWMONCoreTemp(const char *pHWMONPath, int nCoreID)
{
    DIR *pDir = opendir(pHWMONPath);
    if (pDir == NULL) return 0;

    char sBuffer[XPROC_BUFFER_SIZE];
    char sPath[XPATH_MAX];
    uint32_t nTemperature = 0;

    struct dirent *pEntry = readdir(pDir);
    while(pEntry != NULL)
    {
        int nIndex = XMon_ParseHWMONTempIndex(pEntry->d_name, "_label");
        if (!nIndex)
        {
            pEntry = readdir(pDir);
            continue;
        }

        int nTempCoreID = -1;
        xstrncpyf(sPath, sizeof(sPath), "%s/%s", pHWMONPath, pEntry->d_name);

        if (XPath_Read(sPath, (uint8_t*)sBuffer, sizeof(sBuffer)) > 0 &&
            sscanf(sBuffer, "Core %d", &nTempCoreID) == 1 &&
            nTempCoreID == nCoreID)
        {
            nTemperature = XMon_ReadHWMONTempInput(pHWMONPath, nIndex);
            if (nTemperature) break;
        }

        pEntry = readdir(pDir);
    }

    closedir(pDir);
    return nTemperature;
}

static uint32_t XMon_ReadCPUTempByLabel(const char *pLabel)
{
    DIR *pDir = opendir(XSYS_CLASS_HWMON);
    if (pDir == NULL) return 0;

    char sPath[XPATH_MAX];
    uint32_t nTemperature = 0;

    struct dirent *pEntry = readdir(pDir);
    while(pEntry != NULL)
    {
        if (!strncmp(pEntry->d_name, "hwmon", 5))
        {
            xstrncpyf(sPath, sizeof(sPath), "%s/%s", XSYS_CLASS_HWMON, pEntry->d_name);
            nTemperature = XMon_ReadHWMONTempLabel(sPath, pLabel);
            if (nTemperature) break;
        }

        pEntry = readdir(pDir);
    }

    closedir(pDir);
    return nTemperature;
}

static uint32_t XMon_ReadCPUCoreTemp(int nCoreID)
{
    DIR *pDir = opendir(XSYS_CLASS_HWMON);
    if (pDir == NULL) return 0;

    char sPath[XPATH_MAX];
    uint32_t nTemperature = 0;

    struct dirent *pEntry = readdir(pDir);
    while(pEntry != NULL)
    {
        if (!strncmp(pEntry->d_name, "hwmon", 5))
        {
            xstrncpyf(sPath, sizeof(sPath), "%s/%s", XSYS_CLASS_HWMON, pEntry->d_name);
            nTemperature = XMon_ReadHWMONCoreTemp(sPath, nCoreID);
            if (nTemperature) break;
        }

        pEntry = readdir(pDir);
    }

    closedir(pDir);
    return nTemperature;
}

static uint32_t XMon_ReadCPUTempByName(xbool_t bCPUOnly)
{
    DIR *pDir = opendir(XSYS_CLASS_HWMON);
    if (pDir == NULL) return 0;

    char sPath[XPATH_MAX];
    uint32_t nTemperature = 0;

    struct dirent *pEntry = readdir(pDir);
    while(pEntry != NULL)
    {
        if (!strncmp(pEntry->d_name, "hwmon", 5))
        {
            xstrncpyf(sPath, sizeof(sPath), "%s/%s", XSYS_CLASS_HWMON, pEntry->d_name);
            if (!bCPUOnly || XMon_HWMONNameIsCPU(sPath))
            {
                nTemperature = XMon_ReadFirstHWMONTemp(sPath);
                if (nTemperature) break;
            }
        }

        pEntry = readdir(pDir);
    }

    closedir(pDir);
    return nTemperature;
}

static int XMon_ReadCPUCoreID(int nCPUID)
{
    char sBuffer[XPROC_BUFFER_SIZE];
    char sPath[XPATH_MAX];

    xstrncpyf(sPath, sizeof(sPath), XSYS_CPU_TOPOLOGY_CORE, nCPUID);
    if (XPath_Read(sPath, (uint8_t*)sBuffer, sizeof(sBuffer)) <= 0) return nCPUID;

    return atoi(sBuffer);
}

static uint32_t XMon_ReadCPUPackageTemp(void)
{
    uint32_t nTemperature = 0;

    nTemperature = XMon_ReadCPUTempByLabel("Package");
    if (nTemperature) return nTemperature;

    nTemperature = XMon_ReadCPUTempByLabel("Tctl");
    if (nTemperature) return nTemperature;

    nTemperature = XMon_ReadCPUTempByLabel("Tdie");
    if (nTemperature) return nTemperature;

    nTemperature = XMon_ReadCPUTempByLabel("CPU");
    if (nTemperature) return nTemperature;

    nTemperature = XMon_ReadCPUTempByName(XTRUE);
    if (nTemperature) return nTemperature;

    return 0;
}

static uint32_t XMon_ReadCPUTemperature(int nCPUID)
{
    if (nCPUID < 0) return XMon_ReadCPUPackageTemp();
    int nCoreID = XMon_ReadCPUCoreID(nCPUID);
    return XMon_ReadCPUCoreTemp(nCoreID);
}

static uint32_t XMon_CPUPercent(uint64_t nCurrent, uint64_t nPrevious, uint64_t nTotal)
{
    if (!nTotal || nCurrent < nPrevious) return XFloatToU32(0.0f);
    return XFloatToU32(((nCurrent - nPrevious) / (float)nTotal) * 100);
}

static int XMon_ReadCPUStats(xbyte_buffer_t *pBuffer)
{
    xfile_t file;
    if (XFile_Open(&file, XPROC_FILE_STAT, "r", NULL) < 0) return XSTDERR;

    int nRead;
    char sBuffer[XPROC_BUFFER_SIZE];
    while ((nRead = XFile_Read(&file, sBuffer, sizeof(sBuffer))) > 0)
    {
        if (XByteBuffer_Add(pBuffer, (uint8_t*)sBuffer, nRead) < 0)
        {
            nRead = XSTDERR;
            break;
        }
    }

    XFile_Close(&file);
    return nRead < 0 ? XSTDERR : (int)pBuffer->nUsed;
}

static uint8_t XMon_UpdateCPUStats(xmon_stats_t *pStats)
{
    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, 0, XTRUE);
    if (XMon_ReadCPUStats(&buffer) <= 0)
    {
        XByteBuffer_Clear(&buffer);
        return 0;
    }

    xcpu_stats_t *pCpuStats = &pStats->cpuStats;
    xproc_info_t lastCpuUsage;
    XMon_CopyCPUUsage(&lastCpuUsage, &pCpuStats->usage);

    char *pSavePtr = NULL;
    size_t nCoreCount = 0;

    char *ptr = strtok_r((char*)buffer.pData, "\n", &pSavePtr);
    while (ptr != NULL && !strncmp(ptr, "cpu", 3))
    {
        xcpu_info_t cpuInfo;
        memset(&cpuInfo, 0, sizeof(xcpu_info_t));

        char *pFields = ptr + 3;
        cpuInfo.nID = -1;
        if (isdigit((unsigned char)*pFields))
        {
            errno = 0;
            long nCPUID = strtol(pFields, &pFields, 10);
            if (errno == ERANGE || nCPUID > INT_MAX) break;
            cpuInfo.nID = (int)nCPUID;
        }
        if (!isspace((unsigned char)*pFields)) break;

        int nFields = sscanf(pFields, "%u %u %u %u %u %u %u %u %u %u", &cpuInfo.nUserSpaceRaw,
            &cpuInfo.nUserSpaceNicedRaw, &cpuInfo.nKernelSpaceRaw, &cpuInfo.nIdleTimeRaw,
            &cpuInfo.nIOWaitRaw, &cpuInfo.nHardInterruptsRaw, &cpuInfo.nSoftInterruptsRaw,
            &cpuInfo.nStealRaw, &cpuInfo.nGuestRaw, &cpuInfo.nGuestNicedRaw);
        if (nFields < 4) break;

        cpuInfo.nTotalRaw = (uint64_t)cpuInfo.nHardInterruptsRaw + cpuInfo.nSoftInterruptsRaw;
        cpuInfo.nTotalRaw += (uint64_t)cpuInfo.nUserSpaceRaw + cpuInfo.nKernelSpaceRaw;
        cpuInfo.nTotalRaw += (uint64_t)cpuInfo.nUserSpaceNicedRaw + cpuInfo.nStealRaw;
        cpuInfo.nTotalRaw += (uint64_t)cpuInfo.nIdleTimeRaw + cpuInfo.nIOWaitRaw;

        cpuInfo.nActive = 1;
        cpuInfo.nTemperature = XMon_ReadCPUTemperature(cpuInfo.nID);

        XSync_Lock(&pStats->netLock);
        xcpu_info_t *pGenCpuInfo = &pCpuStats->sum;
        if (cpuInfo.nID >= 0)
        {
            size_t nIndex = nCoreCount;
            for (; nIndex < pCpuStats->cores.nUsed; nIndex++)
            {
                pGenCpuInfo = (xcpu_info_t*)XArray_GetData(&pCpuStats->cores, nIndex);
                if (pGenCpuInfo->nID == cpuInfo.nID) break;
            }

            if (nIndex == pCpuStats->cores.nUsed)
            {
                pGenCpuInfo = nIndex < UINT16_MAX ? (xcpu_info_t*)malloc(sizeof(xcpu_info_t)) : NULL;
                if (pGenCpuInfo != NULL)
                {
                    memcpy(pGenCpuInfo, &cpuInfo, sizeof(xcpu_info_t));
                    if (XArray_AddData(&pCpuStats->cores, pGenCpuInfo, 0) < 0)
                    {
                        free(pGenCpuInfo);
                        pGenCpuInfo = NULL;
                    }
                }
            }

            if (pGenCpuInfo != NULL)
            {
                if (nIndex != nCoreCount) XArray_Swap(&pCpuStats->cores, nIndex, nCoreCount);
                nCoreCount++;
            }
        }

        if (pGenCpuInfo != NULL)
        {
            xcpu_info_t lastCpuInfo;
            XMon_CopyCPUInfo(&lastCpuInfo, pGenCpuInfo);
            uint64_t nTotalDiff = cpuInfo.nTotalRaw >= lastCpuInfo.nTotalRaw ? cpuInfo.nTotalRaw - lastCpuInfo.nTotalRaw : 0;

            XSYNC_ATOMIC_SET(&pGenCpuInfo->nHardInterrupts, XMon_CPUPercent(cpuInfo.nHardInterruptsRaw, lastCpuInfo.nHardInterruptsRaw, nTotalDiff));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nSoftInterrupts, XMon_CPUPercent(cpuInfo.nSoftInterruptsRaw, lastCpuInfo.nSoftInterruptsRaw, nTotalDiff));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nKernelSpace, XMon_CPUPercent(cpuInfo.nKernelSpaceRaw, lastCpuInfo.nKernelSpaceRaw, nTotalDiff));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nUserSpace, XMon_CPUPercent(cpuInfo.nUserSpaceRaw, lastCpuInfo.nUserSpaceRaw, nTotalDiff));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nUserSpaceNiced, XMon_CPUPercent(cpuInfo.nUserSpaceNicedRaw, lastCpuInfo.nUserSpaceNicedRaw, nTotalDiff));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nIdleTime, XMon_CPUPercent(cpuInfo.nIdleTimeRaw, lastCpuInfo.nIdleTimeRaw, nTotalDiff));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nIOWait, XMon_CPUPercent(cpuInfo.nIOWaitRaw, lastCpuInfo.nIOWaitRaw, nTotalDiff));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nStealTime, XMon_CPUPercent(cpuInfo.nStealRaw, lastCpuInfo.nStealRaw, nTotalDiff));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nGuestTime, XMon_CPUPercent(cpuInfo.nGuestRaw, lastCpuInfo.nGuestRaw, nTotalDiff));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nGuestNiced, XMon_CPUPercent(cpuInfo.nGuestNicedRaw, lastCpuInfo.nGuestNicedRaw, nTotalDiff));

            /* Save raw information about CPU usage for later percentage calculations */
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nHardInterruptsRaw, cpuInfo.nHardInterruptsRaw);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nSoftInterruptsRaw, cpuInfo.nSoftInterruptsRaw);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nKernelSpaceRaw, cpuInfo.nKernelSpaceRaw);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nUserSpaceRaw, cpuInfo.nUserSpaceRaw);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nUserSpaceNicedRaw, cpuInfo.nUserSpaceNicedRaw);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nIdleTimeRaw, cpuInfo.nIdleTimeRaw);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nIOWaitRaw, cpuInfo.nIOWaitRaw);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nStealRaw, cpuInfo.nStealRaw);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nGuestRaw, cpuInfo.nGuestRaw);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nGuestNicedRaw, cpuInfo.nGuestNicedRaw);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nTotalRaw, cpuInfo.nTotalRaw);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nActive, cpuInfo.nActive);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nID, cpuInfo.nID);
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nTemperature, cpuInfo.nTemperature);
        }
        XSync_Unlock(&pStats->netLock);

        ptr = strtok_r(NULL, "\n", &pSavePtr);
    }

    XSync_Lock(&pStats->netLock);
    if (ptr == NULL || strncmp(ptr, "cpu", 3))
        while (pCpuStats->cores.nUsed > nCoreCount) XArray_Delete(&pCpuStats->cores, pCpuStats->cores.nUsed - 1);
    XSYNC_ATOMIC_SET(&pCpuStats->nCoreCount, pCpuStats->cores.nUsed);
    XSync_Unlock(&pStats->netLock);
    XByteBuffer_Clear(&buffer);

    char sBuffer[XPROC_BUFFER_SIZE];
    char sPath[XPATH_MAX];

    if (pStats->nPID <= 0) xstrncpy(sPath, sizeof(sPath), XPROC_FILE_PIDSTAT);
    else xstrncpyf(sPath, sizeof(sPath), "/proc/%d/stat", pStats->nPID);
    if (XPath_Read(sPath, (uint8_t*)sBuffer, sizeof(sBuffer)) <= 0) return 0;

    char *pFields = strrchr(sBuffer, ')');
    if (pFields == NULL) return 0;

    xproc_info_t currCpuUsage;
    int nFields = sscanf(pFields + 1, " %*c %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %lu %lu %ld %ld",
        (unsigned long*)&currCpuUsage.nUserSpace, (unsigned long*)&currCpuUsage.nKernelSpace,
        (unsigned long*)&currCpuUsage.nUserSpaceChilds, (unsigned long*)&currCpuUsage.nKernelSpaceChilds);
    if (nFields != 4) return 0;

    currCpuUsage.nTotalTime = XSYNC_ATOMIC_GET(&pCpuStats->sum.nTotalRaw);
    uint64_t nTotalDiff = currCpuUsage.nTotalTime >= lastCpuUsage.nTotalTime ?
        currCpuUsage.nTotalTime - lastCpuUsage.nTotalTime : 0;

    uint32_t nUserCPU = XMon_CPUPercent(currCpuUsage.nUserSpace + currCpuUsage.nUserSpaceChilds,
        lastCpuUsage.nUserSpace + lastCpuUsage.nUserSpaceChilds, nTotalDiff);

    uint32_t nSystemCPU = XMon_CPUPercent(currCpuUsage.nKernelSpace + currCpuUsage.nKernelSpaceChilds,
        lastCpuUsage.nKernelSpace + lastCpuUsage.nKernelSpaceChilds, nTotalDiff);

    XSYNC_ATOMIC_SET(&pCpuStats->usage.nUserSpaceChilds, currCpuUsage.nUserSpaceChilds);
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nKernelSpaceChilds, currCpuUsage.nKernelSpaceChilds);
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nUserSpace, currCpuUsage.nUserSpace);
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nKernelSpace, currCpuUsage.nKernelSpace);
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nTotalTime, currCpuUsage.nTotalTime);
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nUserSpaceUsage, nUserCPU);
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nKernelSpaceUsage, nSystemCPU);

    if (XPath_Read(XPROC_FILE_LOADAVG, (uint8_t*)sBuffer, sizeof(sBuffer)) <= 0) return 0;
    float fOneMinInterval, fFiveMinInterval, fTenMinInterval;

    if (sscanf(sBuffer, "%f %f %f", &fOneMinInterval, &fFiveMinInterval, &fTenMinInterval) != 3) return 0;
    XSYNC_ATOMIC_SET(&pCpuStats->nLoadAvg[0], XFloatToU32(fOneMinInterval));
    XSYNC_ATOMIC_SET(&pCpuStats->nLoadAvg[1], XFloatToU32(fFiveMinInterval));
    XSYNC_ATOMIC_SET(&pCpuStats->nLoadAvg[2], XFloatToU32(fTenMinInterval));

    return 1;
}

int XMon_UpdateStats(void* pData)
{
    xmon_stats_t *pStats = (xmon_stats_t*)pData;
    XMon_UpdateCPUStats(pStats);
    XMon_UpdateMemoryInfo(&pStats->memInfo, pStats->nPID);
    XMon_UpdateNetworkStats(pStats);
    XSYNC_ATOMIC_SET(&pStats->nLoadDone, XTRUE);
    return 0;
}

int XMon_InitCPUStats(xcpu_stats_t *pStats)
{
    if (XArray_InitPool(&pStats->cores, 0, 1, 0) == NULL) return 0;
    pStats->cores.clearCb = XMon_ClearCb;

    memset(&pStats->usage, 0, sizeof(xproc_info_t));
    memset(&pStats->sum, 0, sizeof(xcpu_info_t));

    pStats->nLoadAvg[0] = pStats->nLoadAvg[1] = 0;
    pStats->nLoadAvg[2] = pStats->nCoreCount = 0;

    return 1;
}

int XMon_InitStats(xmon_stats_t *pStats)
{
    if (XArray_InitPool(&pStats->netIfaces, 0, 1, 0) == NULL) return XSTDERR;
    pStats->netIfaces.clearCb = XMon_ClearCb;

    if (!XMon_InitCPUStats(&pStats->cpuStats))
    {
        XArray_Destroy(&pStats->netIfaces);
        return XSTDERR;
    }

    memset(&pStats->memInfo, 0, sizeof(xmem_info_t));
    pStats->monitoring.nStatus = 0;
    pStats->nIntervalU = 0;
    pStats->nPID = 0;

    XSYNC_ATOMIC_SET(&pStats->nLoadDone, XFALSE);
    XSync_Init(&pStats->netLock);
    return XSTDOK;
}

void XMon_DestroyStats(xmon_stats_t *pStats)
{
    XArray_Destroy(&pStats->cpuStats.cores);
    XArray_Destroy(&pStats->netIfaces);
    XSync_Destroy(&pStats->netLock);
}

int XMon_StartMonitoring(xmon_stats_t *pStats, uint32_t nIntervalU, xpid_t nPID)
{
    if (nPID > 0)
    {
        char sPath[XPATH_MAX];
        xstrncpyf(sPath, sizeof(sPath), "/proc/%d", nPID);
        if (!XPath_Exists(sPath)) return XSTDERR;
    }

    pStats->nIntervalU = nIntervalU;
    pStats->nPID = nPID;

    XTask_Start(&pStats->monitoring, XMon_UpdateStats, pStats, nIntervalU);
    return XSYNC_ATOMIC_GET(&pStats->monitoring.nStatus);;
}

uint32_t XMon_WaitLoad(xmon_stats_t *pStats, uint32_t nWaitUsecs)
{
    uint32_t nCheckCount = 0;

    while (XSYNC_ATOMIC_GET(&pStats->nLoadDone) != XTRUE)
    {
        if (!nWaitUsecs) continue;
        xusleep((uint32_t)nWaitUsecs);
        nCheckCount++;
    }
    return nCheckCount * nWaitUsecs;
}

uint32_t XMon_StopMonitoring(xmon_stats_t *pStats, uint32_t nWaitUsecs)
{
    xtask_t *pMonTask = &pStats->monitoring;
    return XTask_Stop(pMonTask, nWaitUsecs);
}
#endif /* #ifndef _WIN32 */
