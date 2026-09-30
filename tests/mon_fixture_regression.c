/* Deterministic procfs and sysfs snapshots, read through the normal file layer. */
#include "test.h"
#include "mon.h"
#include "xfs.h"
#include "type.h"
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>

int XMon_UpdateStats(void*);
static char g_sRoot[128];
static _Thread_local size_t g_nFailAlloc;
static _Thread_local size_t g_nAllocCount;
static _Thread_local xbool_t g_bCountAlloc;
static _Thread_local int g_nFailRead;
static _Thread_local int g_nStatFD = -1;
static _Thread_local int g_nSlowFD = -1;
static xatomic_t g_nBlockRead;
static xatomic_t g_nBlocked;

int __real_open(const char*, int, ...);
DIR *__real_opendir(const char*);
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void*, size_t);
ssize_t __real_read(int, void*, size_t);

void *__wrap_malloc(size_t nSize)
{
    if (g_bCountAlloc && ++g_nAllocCount == g_nFailAlloc) return NULL;
    return __real_malloc(nSize);
}

void *__wrap_calloc(size_t nCount, size_t nSize)
{
    if (g_bCountAlloc && ++g_nAllocCount == g_nFailAlloc) return NULL;
    return __real_calloc(nCount, nSize);
}

void *__wrap_realloc(void *pData, size_t nSize)
{
    if (g_bCountAlloc && ++g_nAllocCount == g_nFailAlloc) return NULL;
    return __real_realloc(pData, nSize);
}

ssize_t __wrap_read(int nFD, void *pData, size_t nSize)
{
    if (nFD == g_nSlowFD && XSYNC_ATOMIC_GET(&g_nBlockRead))
    {
        XSYNC_ATOMIC_SET(&g_nBlocked, 1);
        while (XSYNC_ATOMIC_GET(&g_nBlockRead)) xusleep(100);
    }
    if (nFD == g_nStatFD && g_nFailRead && !--g_nFailRead)
    {
        errno = EIO;
        return -1;
    }
    return __real_read(nFD, pData, nSize);
}

static const char *mon_path(const char *pPath, char *pOutput, size_t nSize)
{
    if (g_sRoot[0] && (!strncmp(pPath, "/proc/", 6) || !strncmp(pPath, "/sys/", 5)))
    {
        snprintf(pOutput, nSize, "%s%s", g_sRoot, pPath);
        return pOutput;
    }
    return pPath;
}

int __wrap_open(const char *pPath, int nFlags, ...)
{
    mode_t nMode = 0;
    if (nFlags & O_CREAT)
    {
        va_list args;
        va_start(args, nFlags);
        nMode = va_arg(args, mode_t);
        va_end(args);
    }
    char path[4096];
    int nFD = __real_open(mon_path(pPath, path, sizeof(path)), nFlags, nMode);
    if (!strcmp(pPath, "/proc/stat")) g_nStatFD = nFD;
    if (!strcmp(pPath, "/sys/class/net/xutils0/speed")) g_nSlowFD = nFD;
    return nFD;
}

DIR *__wrap_opendir(const char *pPath)
{
    char path[4096];
    return __real_opendir(mon_path(pPath, path, sizeof(path)));
}

static int mon_write(const char *pPath, const char *pData)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s%s", g_sRoot, pPath);
    CHECK(XPath_EnsureDirectory(path) > 0, "Create the synthetic procfs or sysfs parent");
    CHECK(XPath_Write(path, (const uint8_t*)pData, strlen(pData), "cwt") == (int)strlen(pData),
        "Write the complete fixture value");
    return 0;
}

static int mon_begin(xmon_stats_t *pStats)
{
    strcpy(g_sRoot, "/tmp/xutils-mon-fixture-XXXXXX");
    CHECK(mkdtemp(g_sRoot) != NULL && XMon_InitStats(pStats) == XSTDOK, "Initialize an isolated monitor fixture");
    pStats->nIntervalU = 1000000;
    CHECK(mon_write("/proc/stat", "cpu 10 0 10 80 0 0 0 0 2 1\ncpu0 10 0 10 80 0 0 0 0 2 1\n") == 0,
        "Provide one logical core and its aggregate");
    CHECK(mon_write("/proc/self/stat", "1 (worker) S 0 0 0 0 0 0 0 0 0 0 2 3 5 7\n") == 0 &&
        mon_write("/proc/loadavg", "1.25 2.5 3.75 1/1 1\n") == 0, "Provide process counters and all three load averages");
    CHECK(mon_write("/proc/meminfo", "MemTotal: 10000 kB\nMemFree: 2000 kB\nMemAvailable: 4000 kB\n"
        "Shmem: 30 kB\nCached: 120 kB\nSReclaimable: 40 kB\nBuffers: 10 kB\nSwapTotal: 200 kB\n"
        "SwapFree: 180 kB\nSwapCached: 4 kB\n") == 0 &&
        mon_write("/proc/self/status", "Name:\tworker\nVmRSS:\t300 kB\nVmSize:\t600 kB\n") == 0, "Provide exact memory fields");
    return 0;
}

static void mon_end(xmon_stats_t *pStats)
{
    XMon_DestroyStats(pStats);
    XDir_Remove(g_sRoot);
    g_sRoot[0] = '\0';
}

static int mon_temperature(const char *pName, const char *pLabel, int nIndex, const char *pValue, uint32_t nExpected)
{
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0, "Create the temperature fixture");
    if (pName) CHECK(mon_write("/sys/class/hwmon/hwmon0/name", pName) == 0, "Identify the sensor");
    char path[256];
    if (pLabel)
    {
        snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon0/temp%d_label", nIndex);
        CHECK(mon_write(path, pLabel) == 0, "Label the exact temperature input");
    }
    snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon0/temp%d_input", nIndex);
    CHECK(mon_write(path, pValue) == 0, "Write the sensor reading");
    XMon_UpdateStats(&stats);
    uint32_t nTemperature = stats.cpuStats.sum.nTemperature;
    mon_end(&stats);
    CHECK(nTemperature == nExpected, "The monitor selects the expected sensor and exact millidegree value");
    return 0;
}

static int XTest_temperature_labels(void)
{
    const char *labels[] = {"Package id 0\n", "Tctl\n", "Tdie\n", "CPU\n"};
    for (size_t i = 0; i < sizeof(labels) / sizeof(*labels); i++)
        CHECK(mon_temperature(NULL, labels[i], 3, "57123\n", 57123) == 0, "Each package label works without a driver name");
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0, "Create competing package sensors");
    CHECK(mon_write("/sys/class/hwmon/hwmon0/temp1_label", "Tdie\n") == 0 &&
        mon_write("/sys/class/hwmon/hwmon0/temp1_input", "42000\n") == 0 &&
        mon_write("/sys/class/hwmon/hwmon0/temp2_label", "Package id 0\n") == 0 &&
        mon_write("/sys/class/hwmon/hwmon0/temp2_input", "53000\n") == 0, "Write distinct sensor readings");
    XMon_UpdateStats(&stats);
    uint32_t nTemperature = stats.cpuStats.sum.nTemperature;
    mon_end(&stats);
    CHECK(nTemperature == 53000, "A package label has priority over a fallback label regardless of directory order");
    return 0;
}

static int XTest_temperature_names(void)
{
    const char *names[] = {"coretemp\n", "k10temp\n", "zenpower\n", "acpitz\n", "cpu_thermal\n", "soc_thermal\n"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++)
        CHECK(mon_temperature(names[i], NULL, 1, "48123\n", 48123) == 0, "Recognized CPU drivers supply unlabeled temperatures");
    CHECK(mon_temperature("coretemp\n", NULL, 4, "39123\n", 39123) == 0, "A later input works when temp1 is absent");
    CHECK(mon_temperature("disk\n", NULL, 1, "99123\n", 0) == 0, "A disk sensor cannot impersonate a CPU temperature");
    CHECK(mon_temperature(NULL, NULL, 1, "48123\n", 0) == 0, "An unlabeled and unnamed sensor has no CPU identity");
    CHECK(mon_temperature("coretemp\n", NULL, 4, "-1\n", 0) == 0, "An invalid fallback reading leaves temperature unavailable");
    return 0;
}

static int XTest_temperature_bounds(void)
{
    const char *values[] = {"-1\n", "0\n", "invalid\n", "4294967296\n", "4294967297\n",
        "9223372036854775807\n", "999999999999999999999999999999999999\n"};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++)
        CHECK(mon_temperature("coretemp\n", "Package\n", 1, values[i], 0) == 0,
            "Invalid values cannot wrap into valid temperatures");
    return 0;
}

static int XTest_sensor_index(void)
{
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0, "Create malformed sensor names");
    CHECK(mon_write("/sys/class/hwmon/hwmon0/name", "coretemp\n") == 0 &&
        mon_write("/sys/class/hwmon/hwmon0/temp999999999999999999999999999999_input", "99999\n") == 0 &&
        mon_write("/sys/class/hwmon/hwmon0/temp999999999999999999999999999999_label", "Package\n") == 0 &&
        mon_write("/sys/class/hwmon/hwmon0/temp2_input", "32123\n") == 0, "Write malformed indices beside a valid sensor");
    XMon_UpdateStats(&stats);
    uint32_t nTemperature = stats.cpuStats.sum.nTemperature;
    mon_end(&stats);
    CHECK(nTemperature == 32123, "An overflowing sensor index is ignored without hiding a valid sensor");
    return 0;
}

static xbool_t mon_percent(uint32_t nValue, float fExpected)
{
    float fValue = XU32ToFloat(nValue);
    return isfinite(fValue) && fabsf(fValue - fExpected) < 0.0001f;
}

static int XTest_cpu_counters(void)
{
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0, "Create two deterministic CPU samples");
    XMon_UpdateStats(&stats);
    CHECK(mon_write("/proc/stat", "cpu 30 0 20 140 0 0 0 10 7 4\ncpu0 30 0 20 140 0 0 0 10 7 4\n") == 0 &&
        mon_write("/proc/self/stat", "1 (worker) S 0 0 0 0 0 0 0 0 0 0 4 6 10 14\n") == 0,
        "Advance total CPU by 100, process user by 7 and process kernel by 10 ticks");
    XMon_UpdateStats(&stats);
    xcpu_stats_t copy = {0};
    CHECK(XMon_GetCPUStats(&stats, &copy) == 1, "Take an independent snapshot after the second sample");
    xbool_t bRaw = copy.usage.nUserSpaceChilds == 10 && copy.usage.nKernelSpaceChilds == 14 &&
        copy.usage.nUserSpace == 4 && copy.usage.nKernelSpace == 6 && copy.sum.nTotalRaw == 200;
    xbool_t bRates = mon_percent(copy.usage.nUserSpaceUsage, 7.0f) && mon_percent(copy.usage.nKernelSpaceUsage, 10.0f) &&
        mon_percent(copy.sum.nUserSpace, 20.0f) && mon_percent(copy.sum.nKernelSpace, 10.0f) &&
        mon_percent(copy.sum.nIdleTime, 60.0f) && mon_percent(copy.sum.nStealTime, 10.0f) &&
        mon_percent(copy.sum.nGuestTime, 5.0f) && mon_percent(copy.sum.nGuestNiced, 3.0f);
    XMon_UpdateStats(&stats);
    xbool_t bUnchanged = XU32ToFloat(stats.cpuStats.sum.nUserSpace) == 0.0f &&
        XU32ToFloat(stats.cpuStats.usage.nUserSpaceUsage) == 0.0f && XU32ToFloat(stats.cpuStats.usage.nKernelSpaceUsage) == 0.0f;
    mon_end(&stats);
    xbool_t bOwned = copy.usage.nKernelSpaceChilds == 14 && copy.cores.nUsed == 1;
    XArray_Destroy(&copy.cores);
    CHECK(bRaw && bRates, "Distinct raw counters and independently calculated CPU percentages survive the snapshot");
    CHECK(bUnchanged, "An unchanged tick snapshot reports zero activity without NaN or infinity");
    CHECK(bOwned, "The snapshot retains its values after the monitor is destroyed");
    return 0;
}

static int XTest_cpu_wide(void)
{
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0, "Create wide tick counters");
    XMon_UpdateStats(&stats);
    CHECK(mon_write("/proc/stat", "cpu 4294967200 4294967200 4294967200 4294967200 4294967200 4294967200 "
        "4294967200 4294967200 0 0\ncpu0 4294967200 4294967200 4294967200 4294967200 4294967200 "
        "4294967200 4294967200 4294967200 0 0\n") == 0, "The sum exceeds 32 bits while every individual field fits");
    XMon_UpdateStats(&stats);
    xcpu_info_t *pCore = XArray_GetData(&stats.cpuStats.cores, 0);
    xbool_t bWide = stats.cpuStats.sum.nTotalRaw == UINT64_C(34359737600) && pCore &&
        pCore->nTotalRaw == UINT64_C(34359737600) && XU32ToFloat(stats.cpuStats.sum.nUserSpace) == 12.5f &&
        XU32ToFloat(pCore->nKernelSpace) == 12.5f;
    CHECK(mon_write("/proc/stat", "cpu 0 0 0 0 0 0 0 0 0 0\ncpu0 0 0 0 0 0 0 0 0 0 0\n") == 0,
        "Reset the tick source to its initial state");
    XMon_UpdateStats(&stats);
    xbool_t bReset = !stats.cpuStats.sum.nTotalRaw && XU32ToFloat(stats.cpuStats.sum.nUserSpace) == 0.0f &&
        XU32ToFloat(stats.cpuStats.sum.nGuestNiced) == 0.0f && XU32ToFloat(stats.cpuStats.usage.nUserSpaceUsage) == 0.0f;
    mon_end(&stats);
    CHECK(bWide, "Totals and percentage denominators retain all 64 bits of the aggregate tick count");
    CHECK(bReset, "A counter reset cannot become unsigned wraparound activity");
    return 0;
}

static int XTest_process_names(void)
{
    const char *names[] = {"simple", "worker pool", "worker (io) name", "worker ) parser ("};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++)
    {
        xmon_stats_t stats;
        CHECK(mon_begin(&stats) == 0, "Create a process name fixture");
        char data[256];
        snprintf(data, sizeof(data), "1 (%s) S 0 0 0 0 0 0 0 0 0 0 2 3 5 7\n", names[i]);
        CHECK(mon_write("/proc/self/stat", data) == 0, "Place spaces and parentheses inside the process command field");
        XMon_UpdateStats(&stats);
        xbool_t bValid = stats.cpuStats.usage.nUserSpace == 2 && stats.cpuStats.usage.nKernelSpace == 3 &&
            stats.cpuStats.usage.nUserSpaceChilds == 5 && stats.cpuStats.usage.nKernelSpaceChilds == 7;
        CHECK(mon_write("/proc/self/stat", "1 (truncated) S 0 0\n") == 0, "Truncate the next process record");
        XMon_UpdateStats(&stats);
        xbool_t bRetained = stats.cpuStats.usage.nUserSpace == 2 && stats.cpuStats.usage.nKernelSpace == 3 &&
            stats.cpuStats.usage.nUserSpaceChilds == 5 && stats.cpuStats.usage.nKernelSpaceChilds == 7;
        mon_end(&stats);
        CHECK(bValid && bRetained,
            "Process command punctuation cannot shift counters, and truncated records retain the last sample");
    }
    return 0;
}

static int mon_cores(xmon_stats_t *pStats, const int *pIDs, size_t nCount, unsigned int nSample)
{
    char data[16384];
    size_t nUsed = snprintf(data, sizeof(data), "cpu %u 0 %u %u 0 0 0 0 0 0\n",
        nSample * 10, nSample * 20, nSample * 70);
    for (size_t i = 0; i < nCount; i++)
    {
        int nID = pIDs ? pIDs[i] : (int)i;
        int nBytes = snprintf(data + nUsed, sizeof(data) - nUsed, "cpu%d %u 0 %u %u 0 0 0 0 0 0\n",
            nID, nSample * (10 + nID), nSample * (20 + nID), nSample * (70 + nID));
        CHECK(nBytes > 0 && (size_t)nBytes < sizeof(data) - nUsed, "Every CPU record fits the fixture");
        nUsed += nBytes;
    }
    CHECK(mon_write("/proc/stat", data) == 0, "Publish the complete CPU topology");
    XMon_UpdateStats(pStats);
    return 0;
}

static int mon_check_cores(xmon_stats_t *pStats, const int *pIDs, size_t nCount, unsigned int nSample)
{
    xcpu_stats_t copy = {0};
    CHECK(XMon_GetCPUStats(pStats, &copy) == (int)nCount && copy.nCoreCount == nCount && copy.cores.nUsed == nCount,
        "The snapshot contains every online core exactly once");
    xbool_t bValid = XTRUE;
    for (size_t i = 0; i < nCount; i++)
    {
        int nID = pIDs ? pIDs[i] : (int)i;
        xcpu_info_t *pCore = XArray_GetData(&copy.cores, i);
        if (!pCore || pCore->nID != nID || !pCore->nActive || pCore->nUserSpaceRaw != nSample * (10 + nID) ||
            pCore->nKernelSpaceRaw != nSample * (20 + nID) || pCore->nIdleTimeRaw != nSample * (70 + nID)) bValid = XFALSE;
    }
    XArray_Destroy(&copy.cores);
    CHECK(bValid, "Each core retains its real CPU ID and its own exact counters");
    return 0;
}

static int XTest_cpu_hotplug(void)
{
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0 && mon_cores(&stats, NULL, 1, 1) == 0, "Sample the original CPU");
    CHECK(mon_cores(&stats, NULL, 3, 2) == 0 && mon_check_cores(&stats, NULL, 3, 2) == 0,
        "Newly online CPUs join a running monitor");
    const int remaining[] = {0, 2};
    CHECK(mon_cores(&stats, remaining, 2, 3) == 0 && mon_check_cores(&stats, remaining, 2, 3) == 0,
        "Taking the middle CPU offline preserves the identity of the last one");
    xcpu_info_t *pCore = XArray_GetData(&stats.cpuStats.cores, 1);
    CHECK(pCore && mon_percent(pCore->nUserSpace, 1200.0f / 106.0f),
        "The surviving core uses its own previous counter, not the removed CPU's counter");
    CHECK(mon_cores(&stats, NULL, 3, 4) == 0 && mon_check_cores(&stats, NULL, 3, 4) == 0,
        "An offline CPU can return without duplicates or stale counters");
    pCore = XArray_GetData(&stats.cpuStats.cores, 1);
    CHECK(pCore && mon_percent(pCore->nUserSpace, 0.0f), "A newly returned CPU starts with a fresh baseline");
    mon_end(&stats);
    return 0;
}

static int XTest_cpu_sparse(void)
{
    xmon_stats_t stats;
    const int ids[] = {1, 7, 31};
    CHECK(mon_begin(&stats) == 0 && mon_cores(&stats, ids, 3, 1) == 0 && mon_check_cores(&stats, ids, 3, 1) == 0,
        "CPU IDs come from procfs rather than row numbers");
    CHECK(mon_cores(&stats, ids, 3, 2) == 0 && mon_check_cores(&stats, ids, 3, 2) == 0,
        "Sparse CPU IDs keep their identities on subsequent samples");
    mon_end(&stats);
    return 0;
}

static int XTest_cpu_large(void)
{
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0 && mon_cores(&stats, NULL, 128, 1) == 0 && mon_check_cores(&stats, NULL, 128, 1) == 0,
        "A CPU table larger than the read buffer is sampled in full");
    CHECK(mon_cores(&stats, NULL, 128, 2) == 0 && mon_check_cores(&stats, NULL, 128, 2) == 0,
        "Every CPU receives the next sample, including records crossing read boundaries");
    mon_end(&stats);
    return 0;
}

static int XTest_load_invalid(void)
{
    const char *values[] = {"invalid\n", "9.25\n", "9.25 8.5\n", "9.25 invalid 7.75\n"};
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0, "Initialize load averages before a malformed read");
    XMon_UpdateStats(&stats);
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++)
    {
        CHECK(mon_write("/proc/loadavg", values[i]) == 0, "Provide an incomplete load-average record");
        XMon_UpdateStats(&stats);
        CHECK(XU32ToFloat(stats.cpuStats.nLoadAvg[0]) == 1.25f && XU32ToFloat(stats.cpuStats.nLoadAvg[1]) == 2.5f &&
            XU32ToFloat(stats.cpuStats.nLoadAvg[2]) == 3.75f, "A malformed record retains all three previous load averages");
    }
    mon_end(&stats);
    return 0;
}

static int XTest_cpu_faults(void)
{
    for (size_t i = 1; i <= 12; i++)
    {
        xmon_stats_t stats;
        CHECK(mon_begin(&stats) == 0 && mon_cores(&stats, NULL, 3, 1) == 0, "Prepare a CPU snapshot before allocation failure");
        xcpu_stats_t copy = {0};
        g_nFailAlloc = i;
        g_nAllocCount = 0;
        g_bCountAlloc = XTRUE;
        int nStatus = XMon_GetCPUStats(&stats, &copy);
        g_bCountAlloc = XFALSE;
        CHECK(nStatus == -1 || nStatus == -2 || nStatus > 0, "Snapshot allocation failure returns a defined result");
        if (nStatus > 0)
        {
            CHECK(copy.nCoreCount == copy.cores.nUsed && nStatus <= 3, "A partial snapshot counts only owned cores");
            int nLastID = -1;
            for (int j = 0; j < nStatus; j++)
            {
                xcpu_info_t *pCore = XArray_GetData(&copy.cores, j);
                CHECK(pCore && pCore->nID > nLastID && pCore->nID < 3 && pCore->nUserSpaceRaw == 10U + pCore->nID,
                    "Every surviving snapshot entry has its original identity and exact counter");
                nLastID = pCore->nID;
            }
            XArray_Destroy(&copy.cores);
        }
        CHECK(mon_check_cores(&stats, NULL, 3, 1) == 0, "Snapshot failure releases the lock and preserves the live monitor");
        mon_end(&stats);
    }

    for (size_t i = 1; i <= 20; i++)
    {
        xmon_stats_t stats;
        CHECK(mon_begin(&stats) == 0 && mon_cores(&stats, NULL, 3, 1) == 0, "Prepare live counters before sampler failure");
        XArray_Clear(&stats.cpuStats.cores);
        stats.cpuStats.nCoreCount = 0;
        g_nFailAlloc = i;
        g_nAllocCount = 0;
        g_bCountAlloc = XTRUE;
        XMon_UpdateStats(&stats);
        g_bCountAlloc = XFALSE;
        XMon_UpdateStats(&stats);
        CHECK(mon_check_cores(&stats, NULL, 3, 1) == 0, "A complete retry recovers after every sampler allocation failure");
        mon_end(&stats);
    }
    return 0;
}

static int XTest_cpu_read_error(void)
{
    for (int i = 1; i <= 2; i++)
    {
        xmon_stats_t stats;
        CHECK(mon_begin(&stats) == 0 && mon_cores(&stats, NULL, 128, 1) == 0, "Prepare a large live topology");
        g_nFailRead = i;
        XMon_UpdateStats(&stats);
        CHECK(!g_nFailRead && mon_check_cores(&stats, NULL, 128, 1) == 0,
            "An error before or after a partial procfs read preserves all previous cores");
        CHECK(mon_cores(&stats, NULL, 128, 2) == 0 && mon_check_cores(&stats, NULL, 128, 2) == 0,
            "The next successful read resumes sampling every core");
        mon_end(&stats);
    }
    return 0;
}

typedef struct mon_reader_ {
    xmon_stats_t *pStats;
    xatomic_t nStop;
    xatomic_t nReady;
    unsigned int nSamples;
    xbool_t bFailed;
} mon_reader_t;

static void *mon_reader(void *pData)
{
    mon_reader_t *pReader = pData;
    do
    {
        xcpu_stats_t copy = {0};
        int nCount = XMon_GetCPUStats(pReader->pStats, &copy);
        if (nCount <= 0 || nCount > 128 || copy.cores.nUsed != (size_t)nCount) pReader->bFailed = XTRUE;
        unsigned char seen[128] = {0};
        for (int i = 0; i < nCount; i++)
        {
            xcpu_info_t *pCore = XArray_GetData(&copy.cores, i);
            if (!pCore || pCore->nID < 0 || pCore->nID >= 128 || seen[pCore->nID]++ || !pCore->nActive ||
                pCore->nTotalRaw != (uint64_t)pCore->nUserSpaceRaw + pCore->nKernelSpaceRaw + pCore->nIdleTimeRaw)
                pReader->bFailed = XTRUE;
        }
        if (nCount > 0) XArray_Destroy(&copy.cores);
        pReader->nSamples++;
        XSYNC_ATOMIC_SET(&pReader->nReady, 1);
        xusleep(100);
    } while (!XSYNC_ATOMIC_GET(&pReader->nStop));
    return NULL;
}

static int XTest_cpu_concurrent(void)
{
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0 && mon_cores(&stats, NULL, 1, 1) == 0, "Publish the initial CPU before starting a reader");
    mon_reader_t reader = {.pStats = &stats};
    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, mon_reader, &reader), "Start an independent snapshot consumer");
    while (!XSYNC_ATOMIC_GET(&reader.nReady)) xusleep(100);
    for (unsigned int i = 2; i < 34; i++)
        CHECK(mon_cores(&stats, NULL, i % 2 ? 3 : 128, i) == 0, "Grow and shrink the live CPU table while snapshots are read");
    XSYNC_ATOMIC_SET(&reader.nStop, 1);
    CHECK(!pthread_join(thread, NULL), "Join the snapshot consumer before destroying the monitor");
    mon_end(&stats);
    CHECK(!reader.bFailed && reader.nSamples > 1, "Concurrent snapshots own distinct cores with coherent identity and counters");
    return 0;
}

static int mon_network(const char *pBytes, const char *pPackets)
{
    CHECK(mon_write("/sys/class/net/xutils0/address", "02:01:02:03:04:05\n") == 0 &&
        mon_write("/sys/class/net/xutils0/type", "1\n") == 0 && mon_write("/sys/class/net/xutils0/speed", "1000\n") == 0,
        "Write the interface identity");
    CHECK(mon_write("/sys/class/net/xutils0/statistics/rx_bytes", pBytes) == 0 &&
        mon_write("/sys/class/net/xutils0/statistics/tx_bytes", pBytes) == 0 &&
        mon_write("/sys/class/net/xutils0/statistics/rx_packets", pPackets) == 0 &&
        mon_write("/sys/class/net/xutils0/statistics/tx_packets", pPackets) == 0, "Write all four interface counters");
    return 0;
}

static int XTest_network_rates(void)
{
    struct { uint32_t nInterval; uint64_t nBytes; uint64_t nPackets; } cases[] = {
        {500000, 6000, 60}, {1500000, 2000, 20}, {1000000, 3000, 30}, {2000000, 1500, 15},
        {0, 0, 0}, {1, UINT64_C(3000000000), 30000000}, {UINT32_MAX, 0, 0}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    {
        xmon_stats_t stats;
        CHECK(mon_begin(&stats) == 0 && mon_network("100\n", "20\n") == 0, "Create known initial interface counters");
        stats.nIntervalU = cases[i].nInterval;
        XMon_UpdateStats(&stats);
        CHECK(mon_network("3100\n", "50\n") == 0, "Advance bytes by 3000 and packets by 30");
        XMon_UpdateStats(&stats);
        xarray_t copy;
        CHECK(XMon_GetNetworkStats(&stats, &copy) == 1, "Read the updated interface snapshot");
        xnet_iface_t *pIface = XArray_GetData(&copy, 0);
        xbool_t bValid = pIface && pIface->nBytesReceived == 3100 && pIface->nBytesSent == 3100 &&
            pIface->nPacketsReceived == 50 && pIface->nPacketsSent == 50 &&
            pIface->nBytesReceivedPerSec == cases[i].nBytes && pIface->nBytesSentPerSec == cases[i].nBytes &&
            pIface->nPacketsReceivedPerSec == cases[i].nPackets && pIface->nPacketsSentPerSec == cases[i].nPackets;
        mon_end(&stats);
        XArray_Destroy(&copy);
        CHECK(bValid, "Per-second byte and packet rates respect fractional intervals without truncation or division by zero");
    }
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0 && mon_network("1\n", "1\n") == 0, "Create the rate overflow fixture");
    stats.nIntervalU = 1;
    XMon_UpdateStats(&stats);
    CHECK(mon_network("9223372036854775807\n", "9223372036854775807\n") == 0, "Provide the largest supported raw counters");
    XMon_UpdateStats(&stats);
    xnet_iface_t *pIface = XArray_GetData(&stats.netIfaces, 0);
    xbool_t bSaturated = pIface && pIface->nBytesReceivedPerSec == UINT64_MAX && pIface->nPacketsSentPerSec == UINT64_MAX;
    CHECK(mon_network("1\n", "1\n") == 0, "Reset the network counters");
    XMon_UpdateStats(&stats);
    xbool_t bReset = pIface && !pIface->nBytesReceivedPerSec && !pIface->nBytesSentPerSec &&
        !pIface->nPacketsReceivedPerSec && !pIface->nPacketsSentPerSec;
    mon_end(&stats);
    CHECK(bSaturated && bReset, "Unrepresentable rates saturate and counter resets never become wraparound traffic");
    return 0;
}

static int XTest_network_members(void)
{
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0 && mon_network("100\n", "20\n") == 0, "Create a bridge interface");
    XMon_UpdateStats(&stats);
    xnet_iface_t *pIface = XArray_GetData(&stats.netIfaces, 0);
    CHECK(pIface && !pIface->nMemberCount, "Attribute files are not bridge members");
    for (int i = 0; i < XMEMBERS_MAX + 3; i++)
    {
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/net/xutils0/%s_port%03d", i % 2 ? "slave" : "upper", i);
        CHECK(mon_write(path, "1\n") == 0, "Populate both bridge membership naming forms");
    }
    XMon_UpdateStats(&stats);
    xarray_t copy;
    CHECK(XMon_GetNetworkStats(&stats, &copy) == 1, "Copy the populated interface");
    pIface = XArray_GetData(&copy, 0);
    xbool_t bValid = pIface && pIface->nMemberCount == XMEMBERS_MAX && !strcmp(pIface->sName, "xutils0") &&
        !strcmp(pIface->sHWAddr, "02:01:02:03:04:05") && pIface->nType == 1 && pIface->nBandwidth == 1000;
    unsigned char seen[XMEMBERS_MAX + 3] = {0};
    if (bValid)
        for (int i = 0; i < XMEMBERS_MAX; i++)
        {
            int nPort = -1;
            char extra;
            if (sscanf(pIface->sMembers[i], "port%d%c", &nPort, &extra) != 1 ||
                nPort < 0 || nPort >= XMEMBERS_MAX + 3 || seen[nPort]++) bValid = XFALSE;
        }
    char path[256];
    snprintf(path, sizeof(path), "%s/sys/class/net/xutils0", g_sRoot);
    CHECK(XDir_Remove(path) == XSTDOK, "Remove the interface from sysfs");
    XMon_UpdateStats(&stats);
    xbool_t bRemoved = !stats.netIfaces.nUsed;
    mon_end(&stats);
    xbool_t bOwned = pIface && pIface->nMemberCount == XMEMBERS_MAX && !strcmp(pIface->sName, "xutils0");
    XArray_Destroy(&copy);
    CHECK(bValid && bRemoved && bOwned, "Bridge members are bounded and unique, and copied snapshots survive interface removal");
    return 0;
}

static int XTest_network_large(void)
{
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0, "Create a network table larger than the inline activity bitmap");
    for (int i = 0; i < 260; i++)
    {
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/net/xutils%03d/type", i);
        CHECK(mon_write(path, "1\n") == 0, "Publish a uniquely named network interface");
    }
    XMon_UpdateStats(&stats);
    CHECK(stats.netIfaces.nUsed == 260, "Every interface is represented in the initial sample");
    for (int i = 0; i < 260; i += 2)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/sys/class/net/xutils%03d", g_sRoot, i);
        CHECK(XDir_Remove(path) == XSTDOK, "Take alternating interfaces offline");
    }
    g_bCountAlloc = XTRUE;
    g_nFailAlloc = 2;
    g_nAllocCount = 0;
    XMon_UpdateStats(&stats);
    g_bCountAlloc = XFALSE;
    CHECK(g_nAllocCount == 2 && stats.netIfaces.nUsed == 260, "Bitmap allocation failure retains the previous interface table");
    XMon_UpdateStats(&stats);
    xarray_t copy;
    CHECK(XMon_GetNetworkStats(&stats, &copy) == 130, "A successful retry removes every offline interface");
    unsigned char seen[260] = {0};
    for (size_t i = 0; i < copy.nUsed; i++)
    {
        xnet_iface_t *pIface = XArray_GetData(&copy, i);
        int nID = -1;
        CHECK(pIface && sscanf(pIface->sName, "xutils%d", &nID) == 1 && nID >= 0 && nID < 260 &&
            nID % 2 && !seen[nID]++ && pIface->bActive && pIface->nType == 1,
            "Each survivor keeps its exact name and attributes without duplicates");
    }
    XArray_Destroy(&copy);
    mon_end(&stats);
    return 0;
}

static void *mon_sample(void *pData)
{
    XMon_UpdateStats(pData);
    return NULL;
}

static int XTest_snapshot_io(void)
{
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0 && mon_network("1000\n", "10\n") == 0, "Publish a CPU and an interface before delayed I/O");
    XMon_UpdateStats(&stats);
    XSYNC_ATOMIC_SET(&g_nBlockRead, 1);
    XSYNC_ATOMIC_SET(&g_nBlocked, 0);
    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, mon_sample, &stats), "Start a sampler with a blocked sysfs read");
    while (!XSYNC_ATOMIC_GET(&g_nBlocked)) xusleep(100);
    xcpu_stats_t cpu = {0};
    xarray_t network;
    CHECK(XMon_GetCPUStats(&stats, &cpu) == 1 && XMon_GetNetworkStats(&stats, &network) == 1,
        "Both snapshots remain available while a device attribute read is stalled");
    xnet_iface_t *pIface = XArray_GetData(&network, 0);
    CHECK(pIface && !strcmp(pIface->sName, "xutils0") && pIface->nBytesReceived == 1000 && pIface->bActive,
        "A stalled read cannot temporarily hide an existing interface or erase its counters");
    XSYNC_ATOMIC_SET(&g_nBlockRead, 0);
    CHECK(!pthread_join(thread, NULL), "Resume and join the sampler after taking independent snapshots");
    XArray_Destroy(&cpu.cores);
    XArray_Destroy(&network);
    mon_end(&stats);
    return 0;
}

static int XTest_memory(void)
{
    xmon_stats_t stats;
    CHECK(mon_begin(&stats) == 0, "Create the memory and load fixture");
    XMon_UpdateStats(&stats);
    xmem_info_t memory;
    XMon_GetMemoryInfo(&stats, &memory);
    xbool_t bLoads = XU32ToFloat(stats.cpuStats.nLoadAvg[0]) == 1.25f &&
        XU32ToFloat(stats.cpuStats.nLoadAvg[1]) == 2.5f && XU32ToFloat(stats.cpuStats.nLoadAvg[2]) == 3.75f;
    mon_end(&stats);
    CHECK(memory.nMemoryTotal == 10000 && memory.nMemoryFree == 2000 && memory.nMemoryAvail == 4000 &&
        memory.nMemoryShared == 30 && memory.nMemoryCached == 120 && memory.nReclaimable == 40 && memory.nBuffers == 10 &&
        memory.nSwapTotal == 200 && memory.nSwapFree == 180 && memory.nSwapCached == 4 &&
        memory.nResidentMemory == 300 && memory.nVirtualMemory == 600, "Every memory field matches its own procfs value");
    CHECK(bLoads, "All three fractional load averages preserve their exact values");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(temperature_labels),
    XTEST_CASE(temperature_names),
    XTEST_CASE(temperature_bounds),
    XTEST_CASE(sensor_index),
    XTEST_CASE(cpu_counters),
    XTEST_CASE(cpu_wide),
    XTEST_CASE(process_names),
    XTEST_CASE(cpu_hotplug),
    XTEST_CASE(cpu_sparse),
    XTEST_CASE(cpu_large),
    XTEST_CASE(load_invalid),
    XTEST_CASE(cpu_faults),
    XTEST_CASE(cpu_read_error),
    XTEST_CASE(cpu_concurrent),
    XTEST_CASE(network_rates),
    XTEST_CASE(network_members),
    XTEST_CASE(network_large),
    XTEST_CASE(snapshot_io),
    XTEST_CASE(memory)
)
