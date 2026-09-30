/* Deterministic procfs and sysfs snapshots, read through the normal file layer. */
#include "test.h"
#include "mon.h"
#include "xfs.h"
#include "type.h"
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <unistd.h>

int XMon_UpdateStats(void*);
static char g_sRoot[128];

int __real_open(const char*, int, ...);
DIR *__real_opendir(const char*);

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
    return __real_open(mon_path(pPath, path, sizeof(path)), nFlags, nMode);
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
    XTEST_CASE(network_rates),
    XTEST_CASE(network_members),
    XTEST_CASE(memory)
)
