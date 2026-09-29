/* CPU discovery when sysconf or procfs is unavailable. */
#include "test.h"
#include "cpu.h"
#include "xfs.h"
#include <unistd.h>

long __real_sysconf(int nName);
size_t __real_XPath_LoadBuffer(const char *pPath, xbyte_buffer_t *pBuffer);
static int g_nMode;
static int g_nQueries;
static int g_nLoads;

long __wrap_sysconf(int nName)
{
    if (nName != _SC_NPROCESSORS_ONLN) return __real_sysconf(nName);
    g_nQueries++;
    return -1;
}

size_t __wrap_XPath_LoadBuffer(const char *pPath, xbyte_buffer_t *pBuffer)
{
    if (strcmp(pPath, "/proc/cpuinfo")) return __real_XPath_LoadBuffer(pPath, pBuffer);
    g_nLoads++;
    XByteBuffer_Init(pBuffer, 0, XFALSE);
    if (g_nMode == 1) return 0;
    if (g_nMode == 2) return XSTDNON;
    const char *pText = g_nMode == 3 ? "model name : test\n" :
        "processor : 0\nmodel name : test\n\nprocessor : 1\n\nprocessor : 2\n";
    return XByteBuffer_Add(pBuffer, (const uint8_t*)pText, strlen(pText));
}

static int XTest_count(void)
{
    CHECK(XCPU_GetCount() == 3, "Procfs supplies the count when sysconf cannot");
    CHECK(g_nQueries == 1 && g_nLoads == 1, "Discovery used the fallback exactly once");
    g_nMode = 1;
    CHECK(XCPU_GetCount() == 3, "A valid cached count remains usable when procfs disappears");
    CHECK(g_nQueries == 1 && g_nLoads == 1, "The cache avoids repeated system queries");
    return 0;
}

static int XTest_recovery(void)
{
    g_nMode = 1;
    CHECK(XCPU_GetCount() == XSTDERR, "Unavailable procfs reports failure");
    int nCPU = 0;
    CHECK(XCPU_SetAffinity(&nCPU, 1, XCPU_CALLER_PID) == XSTDERR, "Failed discovery cannot change affinity");
    g_nMode = 2;
    CHECK(XCPU_GetCount() == XSTDERR, "An empty procfs response reports failure");
    g_nMode = 3;
    CHECK(XCPU_GetCount() == 0, "Metadata without processors does not invent a CPU");
    g_nMode = 0;
    CHECK(XCPU_GetCount() == 3, "Failed discovery is retried when procfs becomes available");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(count),
    XTEST_CASE(recovery)
)
