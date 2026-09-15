/* libxutils: resource monitor lifecycle and snapshots.
 *
 * The monitor reads /proc and /sys, so the exact numbers depend on the
 * machine. The assertions are on the contract instead: a snapshot taken
 * before the first sample is empty, a snapshot taken after one is
 * self-consistent and independently owned, and starting, stopping and
 * restarting the sampler leaves nothing behind.
 */

#include "test.h"
#include "mon.h"
#include "str.h"
#include "sync.h"
#include <unistd.h>

#define MON_WAIT_USEC   10000
#define MON_WAIT_TRIES  500

/* Waits for the sampler to publish its first load, bounded so a machine
 * without the expected /proc files cannot hang the suite. */
static int mon_wait_load(xmon_stats_t *pStats)
{
    for (int i = 0; i < MON_WAIT_TRIES; i++)
    {
        if (XSYNC_ATOMIC_GET(&pStats->nLoadDone) == XTRUE) return XSTDOK;
        xusleep(MON_WAIT_USEC);
    }
    return XSTDERR;
}

static int XTest_lifecycle(void)
{
    xmon_stats_t stats;
    CHECK(XMon_InitStats(&stats) == XSTDOK, "The monitor initializes");
    CHECK(stats.nPID == 0 && stats.nIntervalU == 0, "A fresh monitor targets nothing yet");
    CHECK(XSYNC_ATOMIC_GET(&stats.nLoadDone) == XFALSE, "No load has been taken yet");
    CHECK(stats.netIfaces.nUsed == 0, "No interfaces have been sampled yet");

    /* A snapshot before the first sample is empty rather than stale. */
    xcpu_stats_t cpu;
    memset(&cpu, 0, sizeof(cpu));
    CHECK(XMon_GetCPUStats(&stats, &cpu) == 0, "There are no CPU stats before the first sample");

    xarray_t ifaces;
    memset(&ifaces, 0, sizeof(ifaces));
    CHECK(XMon_GetNetworkStats(&stats, &ifaces) == 0, "There are no interfaces before the first sample");

    xmem_info_t mem;
    memset(&mem, 0, sizeof(mem));
    CHECK(XMon_GetMemoryInfo(&stats, &mem) == 0, "There is no memory information before the first sample");

    XMon_DestroyStats(&stats);

    /* Init and destroy must be repeatable without leaking the arrays. */
    for (int i = 0; i < 4; i++)
    {
        CHECK(XMon_InitStats(&stats) == XSTDOK, "The monitor re-initializes");
        XMon_DestroyStats(&stats);
    }
    return 0;
}

static int XTest_sampling(void)
{
    xmon_stats_t stats;
    CHECK(XMon_InitStats(&stats) == XSTDOK, "The monitor initializes");
    CHECK(XMon_StartMonitoring(&stats, XMON_INTERVAL_USEC, 0) > 0, "The sampler starts for this process");

    if (mon_wait_load(&stats) != XSTDOK)
    {
        XMon_StopMonitoring(&stats, MON_WAIT_USEC);
        XMon_DestroyStats(&stats);
        printf("The monitor produced no sample, skipping\n");
        return 77;
    }

    /* Memory: the totals have to be internally consistent. */
    xmem_info_t mem;
    memset(&mem, 0, sizeof(mem));
    int nTotal = XMon_GetMemoryInfo(&stats, &mem);
    CHECK(nTotal != 0, "The sampler reports a memory total");
    CHECK(mem.nMemoryTotal > 0, "The machine has some total memory");
    CHECK(mem.nMemoryFree <= mem.nMemoryTotal, "Free memory never exceeds the total");
    CHECK(mem.nSwapFree <= mem.nSwapTotal, "Free swap never exceeds the total");
    CHECK(mem.nResidentMemory > 0, "This process has a resident set");
    CHECK(mem.nVirtualMemory >= mem.nResidentMemory, "The virtual size covers the resident set");

    /* CPU: one entry per core, each with a plausible id. */
    xcpu_stats_t cpu;
    memset(&cpu, 0, sizeof(cpu));
    int nCores = XMon_GetCPUStats(&stats, &cpu);
    CHECK(nCores > 0, "The sampler reports at least one core");
    CHECK(cpu.nCoreCount == (uint16_t)nCores, "The reported count matches the array");
    CHECK(cpu.cores.nUsed == (size_t)nCores, "Every counted core is in the array");

    for (int i = 0; i < nCores; i++)
    {
        xcpu_info_t *pCore = (xcpu_info_t*)XArray_GetData(&cpu.cores, i);
        CHECK(pCore != NULL, "Every core slot is populated");
        CHECK(pCore->nID >= 0, "Every core has an identifier");
        CHECK(pCore->nUserSpace <= 100 && pCore->nKernelSpace <= 100, "Per-core percentages stay in range");
        CHECK(pCore->nIdleTime <= 100, "Idle time stays in range");
    }
    CHECK(cpu.sum.nTotalRaw > 0, "The summed core has accumulated jiffies");
    XArray_Destroy(&cpu.cores);

    /* Network: the snapshot is an independent copy the caller owns. */
    xarray_t ifaces;
    memset(&ifaces, 0, sizeof(ifaces));
    int nIfaces = XMon_GetNetworkStats(&stats, &ifaces);
    CHECK(nIfaces >= 0, "The interface snapshot is taken");

    for (int i = 0; i < nIfaces; i++)
    {
        xnet_iface_t *pIface = (xnet_iface_t*)XArray_GetData(&ifaces, i);
        CHECK(pIface != NULL, "Every interface slot is populated");
        CHECK(pIface->sName[0] != '\0', "Every interface has a name");
        CHECK(pIface->sHWAddr[0] != '\0', "Every interface has a hardware address, even the default one");
        CHECK(pIface->nBandwidth >= 0, "A negative link speed is normalised to zero");
        CHECK(pIface->nMemberCount <= XMEMBERS_MAX, "The member list never exceeds its capacity");

        /* The copy must survive the source being resampled. */
        char sName[XNAME_MAX];
        xstrncpy(sName, sizeof(sName), pIface->sName);
        xusleep(1000);
        CHECK(strcmp(pIface->sName, sName) == 0, "The snapshot is independent of the live sampler");
    }
    if (nIfaces > 0) XArray_Destroy(&ifaces);

    CHECK(XMon_StopMonitoring(&stats, MON_WAIT_USEC) != XTASK_STAT_ACTIVE, "The sampler stops");
    XMon_DestroyStats(&stats);
    return 0;
}

static int XTest_restart(void)
{
    /* Starting, stopping and starting again must not leave the sampler in
     * a state where the second run never publishes. */
    xmon_stats_t stats;
    CHECK(XMon_InitStats(&stats) == XSTDOK, "The monitor initializes");

    for (int i = 0; i < 2; i++)
    {
        XSYNC_ATOMIC_SET(&stats.nLoadDone, XFALSE);
        CHECK(XMon_StartMonitoring(&stats, XMON_INTERVAL_USEC, 0) > 0, "The sampler starts");

        if (mon_wait_load(&stats) != XSTDOK)
        {
            XMon_StopMonitoring(&stats, MON_WAIT_USEC);
            XMon_DestroyStats(&stats);
            printf("The monitor produced no sample, skipping\n");
            return 77;
        }

        CHECK(XSYNC_ATOMIC_GET(&stats.nLoadDone) == XTRUE, "The run published a sample");
        CHECK(XMon_WaitLoad(&stats, MON_WAIT_USEC) == 0, "Waiting on an already published load returns at once");
        CHECK(XMon_StopMonitoring(&stats, MON_WAIT_USEC) != XTASK_STAT_ACTIVE, "The sampler stops");
    }

    XMon_DestroyStats(&stats);
    return 0;
}

static int XTest_target_pid(void)
{
    xmon_stats_t stats;
    CHECK(XMon_InitStats(&stats) == XSTDOK, "The monitor initializes");

    /* A process that does not exist cannot be monitored. */
    CHECK(XMon_StartMonitoring(&stats, XMON_INTERVAL_USEC, 0x7ffffff0) == XSTDERR,
        "A nonexistent process is refused");
    CHECK(XSYNC_ATOMIC_GET(&stats.nLoadDone) == XFALSE, "A refused start takes no sample");

    /* This process, named explicitly, is monitored like the implicit case. */
    xpid_t nSelf = (xpid_t)getpid();
    CHECK(XMon_StartMonitoring(&stats, XMON_INTERVAL_USEC, nSelf) > 0, "An existing process is accepted");
    CHECK(stats.nPID == nSelf, "The requested process is recorded");

    if (mon_wait_load(&stats) != XSTDOK)
    {
        XMon_StopMonitoring(&stats, MON_WAIT_USEC);
        XMon_DestroyStats(&stats);
        printf("The monitor produced no sample, skipping\n");
        return 77;
    }

    xmem_info_t mem;
    memset(&mem, 0, sizeof(mem));
    CHECK(XMon_GetMemoryInfo(&stats, &mem) != 0, "The named process reports memory");
    CHECK(mem.nResidentMemory > 0, "The named process has a resident set");

    CHECK(XMon_StopMonitoring(&stats, MON_WAIT_USEC) != XTASK_STAT_ACTIVE, "The sampler stops");
    XMon_DestroyStats(&stats);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(lifecycle),
    XTEST_CASE(sampling),
    XTEST_CASE(restart),
    XTEST_CASE(target_pid)
)
