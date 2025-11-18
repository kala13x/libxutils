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
    pDstUsage->nKernelSpaceChilds = XSYNC_ATOMIC_GET(&pSrcUsage->nUserSpaceChilds);
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
}

int XMon_GetCPUStats(xmon_stats_t *pStats, xcpu_stats_t *pCpuStats)
{
    pCpuStats->nLoadAvg[0] = pCpuStats->nLoadAvg[1] = 0;
    pCpuStats->nLoadAvg[2] = pCpuStats->cores.nUsed = 0;

    int i, nCPUCores = XSYNC_ATOMIC_GET(&pStats->cpuStats.nCoreCount);
    if (nCPUCores <= 0) return 0;

    if (XArray_InitPool(&pCpuStats->cores, 0, 1, 0) == NULL) return -1;
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

static void XMon_UpdateNetworkStats(xmon_stats_t *pStats)
{
    DIR *pDir = opendir(XSYS_CLASS_NET);
    if (pDir == NULL) return;

    XSync_Lock(&pStats->netLock);
    xarray_t *pIfaces = &pStats->netIfaces;

    unsigned int i;
    for (i = 0; i < pIfaces->nUsed; i++)
    {
        xnet_iface_t *pIface = (xnet_iface_t*)XArray_GetData(pIfaces, i);
        pIface->bActive = XFALSE;
    }

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

                if (!strncmp(pIfaceEntry->d_name, "slave_", 6) || !strncmp(pIfaceEntry->d_name, "upper_", 6))
                    xstrncpy(netIface.sMembers[netIface.nMemberCount++], XNAME_MAX, &pIfaceEntry->d_name[6]);

                pIfaceEntry = readdir(pIfaceDir);
            }

            closedir(pIfaceDir);
        }

        if (pIfaces->nUsed > 0)
        {
            unsigned int i, nIntervalSecs = pStats->nIntervalU / XMON_INTERVAL_USEC;
            for (i = 0; i < pIfaces->nUsed; i++)
            {
                xnet_iface_t *pIface = (xnet_iface_t*)XArray_GetData(pIfaces, i);
                if (!strcmp(pIface->sName, netIface.sName))
                {
                    if (netIface.nBytesReceived > pIface->nBytesReceived && pIface->nBytesReceived > 0)
                        netIface.nBytesReceivedPerSec = (netIface.nBytesReceived - pIface->nBytesReceived) / nIntervalSecs;
            
                    if (netIface.nPacketsReceived > pIface->nPacketsReceived && pIface->nPacketsReceived > 0)
                        netIface.nPacketsReceivedPerSec = (netIface.nPacketsReceived - pIface->nPacketsReceived) / nIntervalSecs;
            
                    if (netIface.nBytesSent > pIface->nBytesSent && pIface->nBytesSent > 0)
                        netIface.nBytesSentPerSec = (netIface.nBytesSent - pIface->nBytesSent) / nIntervalSecs;
            
                    if (netIface.nPacketsSent > pIface->nPacketsSent && pIface->nPacketsSent > 0)
                        netIface.nPacketsSentPerSec = (netIface.nPacketsSent - pIface->nPacketsSent) / nIntervalSecs;

                    if (XAddr_GetIFCIP(netIface.sName, netIface.sIPAddr, sizeof(netIface.sIPAddr)) <= 0)
                        xstrncpy(netIface.sIPAddr, sizeof(netIface.sIPAddr), XNET_IPADDR_DEFAULT);

                    memcpy(pIface, &netIface, sizeof(xnet_iface_t));

                    pIface->bActive = XTRUE;
                    nHaveIface = XTRUE;
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

                if (XAddr_GetIFCIP(pNewIface->sName, pNewIface->sIPAddr, sizeof(pNewIface->sIPAddr)) <= 0)
                    xstrncpy(pNewIface->sIPAddr, sizeof(pNewIface->sIPAddr), XNET_IPADDR_DEFAULT);

                if (XArray_AddData(pIfaces, pNewIface, 0) < 0) free(pNewIface);
            }
        }

        pEntry = readdir(pDir);
    }

    // Remove unused interfaces
    for (i = 0; i < pIfaces->nUsed; i++)
    {
        xnet_iface_t *pIface = (xnet_iface_t*)XArray_GetData(pIfaces, i);
        if (pIface == NULL || pIface->bActive) continue;
        XArray_Delete(pIfaces, i--);
    }

    closedir(pDir);
    XSync_Unlock(&pStats->netLock);
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

static uint8_t XMon_UpdateCPUStats(xcpu_stats_t *pCpuStats, xpid_t nPID)
{
    char sBuffer[XPROC_BUFFER_SIZE];
    if (XPath_Read(XPROC_FILE_STAT, (uint8_t*)sBuffer, sizeof(sBuffer)) <= 0) return 0;

    xproc_info_t lastCpuUsage;
    XMon_CopyCPUUsage(&lastCpuUsage, &pCpuStats->usage);

    int nCoreCount = XSYNC_ATOMIC_GET(&pCpuStats->nCoreCount);
    char *pSavePtr = NULL;
    int nCPUID = -1;

    char *ptr = strtok_r(sBuffer, "\n", &pSavePtr);
    while (ptr != NULL && !strncmp(ptr, "cpu", 3))
    {
        xcpu_info_t cpuInfo;
        memset(&cpuInfo, 0, sizeof(xcpu_info_t));

        sscanf(ptr, "%*s %u %u %u %u %u %u %u %u %u %u", &cpuInfo.nUserSpaceRaw, 
            &cpuInfo.nUserSpaceNicedRaw, &cpuInfo.nKernelSpaceRaw, &cpuInfo.nIdleTimeRaw, 
            &cpuInfo.nIOWaitRaw, &cpuInfo.nHardInterruptsRaw, &cpuInfo.nSoftInterruptsRaw,
            &cpuInfo.nStealRaw, &cpuInfo.nGuestRaw, &cpuInfo.nGuestNicedRaw);

        cpuInfo.nTotalRaw = cpuInfo.nHardInterruptsRaw + cpuInfo.nSoftInterruptsRaw;
        cpuInfo.nTotalRaw += cpuInfo.nUserSpaceRaw + cpuInfo.nKernelSpaceRaw;
        cpuInfo.nTotalRaw += cpuInfo.nUserSpaceNicedRaw + cpuInfo.nStealRaw;
        cpuInfo.nTotalRaw += cpuInfo.nIdleTimeRaw + cpuInfo.nIOWaitRaw;

        cpuInfo.nID = nCPUID++;
        cpuInfo.nActive = 1;

        if (!nCoreCount && cpuInfo.nID >= 0)
        {
            xcpu_info_t *pInfo = (xcpu_info_t*)malloc(sizeof(xcpu_info_t));
            if (pInfo != NULL)
            {
                memcpy(pInfo, &cpuInfo, sizeof(xcpu_info_t));
                int nStatus = XArray_AddData(&pCpuStats->cores, pInfo, 0);
                if (nStatus < 0) free(pInfo);
            }
        }
        else
        {
            xcpu_info_t lastCpuInfo, *pGenCpuInfo;
            if (cpuInfo.nID < 0) pGenCpuInfo = &pCpuStats->sum;
            else pGenCpuInfo = (xcpu_info_t*)XArray_GetData(&pCpuStats->cores, cpuInfo.nID);

            XMon_CopyCPUInfo(&lastCpuInfo, pGenCpuInfo);
            uint32_t nTotalDiff = cpuInfo.nTotalRaw - lastCpuInfo.nTotalRaw;

            float fHardInterrupts = ((cpuInfo.nHardInterruptsRaw - lastCpuInfo.nHardInterruptsRaw) / (float)nTotalDiff) * 100;
            float fSoftInterrupts = ((cpuInfo.nSoftInterruptsRaw - lastCpuInfo.nSoftInterruptsRaw) / (float)nTotalDiff) * 100;
            float fKernelSpace = ((cpuInfo.nKernelSpaceRaw - lastCpuInfo.nKernelSpaceRaw) / (float)nTotalDiff) * 100;
            float fUserSpace = ((cpuInfo.nUserSpaceRaw - lastCpuInfo.nUserSpaceRaw) / (float)nTotalDiff) * 100;
            float fUserNiced = ((cpuInfo.nUserSpaceNicedRaw - lastCpuInfo.nUserSpaceNicedRaw) / (float)nTotalDiff) * 100;
            float fIdleTime = ((cpuInfo.nIdleTimeRaw - lastCpuInfo.nIdleTimeRaw) / (float)nTotalDiff) * 100;
            float fIOWait = ((cpuInfo.nIOWaitRaw - lastCpuInfo.nIOWaitRaw) / (float)nTotalDiff) * 100;
            float fSteal = ((cpuInfo.nStealRaw - lastCpuInfo.nStealRaw) / (float)nTotalDiff) * 100;
            float fGuest = ((cpuInfo.nGuestRaw - lastCpuInfo.nGuestRaw) / (float)nTotalDiff) * 100;
            float fGuestNi = ((cpuInfo.nGuestNiced - lastCpuInfo.nGuestNiced) / (float)nTotalDiff) * 100;

            XSYNC_ATOMIC_SET(&pGenCpuInfo->nHardInterrupts, XFloatToU32(fHardInterrupts));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nSoftInterrupts, XFloatToU32(fSoftInterrupts));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nKernelSpace, XFloatToU32(fKernelSpace));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nUserSpace, XFloatToU32(fUserSpace));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nUserSpaceNiced, XFloatToU32(fUserNiced));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nIdleTime, XFloatToU32(fIdleTime));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nIOWait, XFloatToU32(fIOWait));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nStealTime, XFloatToU32(fSteal));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nGuestTime, XFloatToU32(fGuest));
            XSYNC_ATOMIC_SET(&pGenCpuInfo->nGuestNiced, XFloatToU32(fGuestNi));

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
        }

        ptr = strtok_r(NULL, "\n", &pSavePtr);
    }

    XSYNC_ATOMIC_SET(&pCpuStats->nCoreCount, pCpuStats->cores.nUsed);
    char sPath[XPATH_MAX];

    if (nPID <= 0) xstrncpy(sPath, sizeof(sPath), XPROC_FILE_PIDSTAT);
    else xstrncpyf(sPath, sizeof(sPath), "/proc/%d/stat", nPID);
    if (XPath_Read(sPath, (uint8_t*)sBuffer, sizeof(sBuffer)) <= 0) return 0;

    xproc_info_t currCpuUsage;
    sscanf(sBuffer, "%*u %*s %*c %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %lu %lu %ld %ld",
        (unsigned long*)&currCpuUsage.nUserSpace, (unsigned long*)&currCpuUsage.nKernelSpace, 
        (unsigned long*)&currCpuUsage.nUserSpaceChilds, (unsigned long*)&currCpuUsage.nKernelSpaceChilds);

    currCpuUsage.nTotalTime = XSYNC_ATOMIC_GET(&pCpuStats->sum.nTotalRaw);
    uint64_t nTotalDiff = currCpuUsage.nTotalTime - lastCpuUsage.nTotalTime;

    float nUserCPU = 100 * (((currCpuUsage.nUserSpace + currCpuUsage.nUserSpaceChilds) - 
        (lastCpuUsage.nUserSpace + lastCpuUsage.nUserSpaceChilds)) / (float)nTotalDiff);

    float nSystemCPU = 100 * (((currCpuUsage.nKernelSpace + currCpuUsage.nKernelSpaceChilds) - 
        (lastCpuUsage.nKernelSpace + lastCpuUsage.nKernelSpaceChilds)) / (float)nTotalDiff);

    XSYNC_ATOMIC_SET(&pCpuStats->usage.nUserSpaceChilds, currCpuUsage.nUserSpaceChilds);
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nKernelSpaceChilds, currCpuUsage.nUserSpaceChilds);
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nUserSpace, currCpuUsage.nUserSpace);
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nKernelSpace, currCpuUsage.nKernelSpace);
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nTotalTime, currCpuUsage.nTotalTime);
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nUserSpaceUsage, XFloatToU32(nUserCPU));
    XSYNC_ATOMIC_SET(&pCpuStats->usage.nKernelSpaceUsage, XFloatToU32(nSystemCPU));
    
    if (XPath_Read(XPROC_FILE_LOADAVG, (uint8_t*)sBuffer, sizeof(sBuffer)) <= 0) return 0;
    float fOneMinInterval, fFiveMinInterval, fTenMinInterval;

    sscanf(sBuffer, "%f %f %f", &fOneMinInterval, &fFiveMinInterval, &fTenMinInterval);
    XSYNC_ATOMIC_SET(&pCpuStats->nLoadAvg[0], XFloatToU32(fOneMinInterval));
    XSYNC_ATOMIC_SET(&pCpuStats->nLoadAvg[1], XFloatToU32(fFiveMinInterval));
    XSYNC_ATOMIC_SET(&pCpuStats->nLoadAvg[2], XFloatToU32(fTenMinInterval));

    return 1;
}

int XMon_UpdateStats(void* pData)
{
    xmon_stats_t *pStats = (xmon_stats_t*)pData;
    XMon_UpdateCPUStats(&pStats->cpuStats, pStats->nPID);
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
