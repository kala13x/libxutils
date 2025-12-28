/*!
 *  @file libxutils/examples/xtop.c
 *
 *  This source is part of "libxutils" project
 *  2015-2022  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of advanced system monitor based on the xUtils.
 * Collect and monitor network, memory and CPU statistics in one window.
 */

#include "xstd.h"
#include "thread.h"
#include "sig.h"
#include "json.h"
#include "str.h"
#include "addr.h"
#include "log.h"
#include "xver.h"
#include "mon.h"
#include "xfs.h"
#include "api.h"
#include "cli.h"

#define XTOP_VERSION_MAJ        1
#define XTOP_VERSION_MIN        17

#define XTOP_SORT_DISABLE       0
#define XTOP_SORT_BUSY          1
#define XTOP_SORT_FREE          2
#define XTOP_SORT_NAME          3
#define XTOP_SORT_LEN           4
#define XTOP_SORT_MAX           XTOP_SORT_LEN

#define XTOP_API_URI            "/api/all"
#define XTOP_TOTAL_LEN          5
#define XTOP_CPU_EXTRA_MIN      2

#define XTOP_CPU_HEADER " "\
    "CPU     IDL      "\
    "US      KS      "\
    "NI      SI      "\
    "HI      IO      "\
    "ST      GT      GN"

#define XTOP_IFACE_HEADER "IFACE"

static int g_nInterrupted = 0;
extern char *optarg;

#define XTOP_INVALID        400
#define XTOP_NOTFOUND       404
#define XTOP_NOTALLOWED     405

#define XIFACE_HDR_NARROW_PADDING   14
#define XIFACE_HDR_WIDE_PADDING     17
#define XIFACE_NAME_NARROW_PADDING  12
#define XIFACE_NAME_WIDE_PADDING    15

#define XTOP_REQUEST_TIMEOUT_MS     30000
#define XTOP_ACTIVE_IFACES_RESET    0
#define XTOP_CORE_COUNT_RESET       -1

#define XTOP_OPT_ON  "["XSTR_CLR_GREEN"on"XSTR_FMT_RESET"] "
#define XTOP_OPT_OFF "["XSTR_CLR_RED"off"XSTR_FMT_RESET"]"

#define XTOP_PORT_DEFAULT   6969
#define XTOP_ADDR_DEFAULT   "0.0.0.0"

typedef enum {
    XTOP_NONE = (uint8_t)0,
    XTOP_NETWORK,
    XTOP_MEMORY,
    XTOP_CPU,
    XTOP_ALL
} xmon_request_t;

typedef struct xmon_ctx_ {
    xmon_stats_t *pStats;
    xsock_t sock;

    xbool_t bCoreCountManualSet;
    xbool_t bDisplayHelp;
    xbool_t bRedrawHelp;
    xbool_t bDisplayHeader;
    xbool_t bShowAllIfaces;
    xbool_t bShowAllCPUs;
    xbool_t bShowCPUSum;
    xbool_t bLineByLine;
    xbool_t bDaemon;
    xbool_t bServer;
    xbool_t bClient;
    xbool_t bClear;
    xbool_t bQuit;

    char sLink[XLINK_MAX];
    char sAddr[XLINK_MAX];
    char sName[XNAME_MAX];
    char sLogs[XNAME_MAX];

    char sToken[XSTR_MIN];
    char sKey[XSTR_MIN];

    int nRealCores;
    int nCoreCount;

    size_t nIntervalU;
    uint16_t nCPUExtraMin;
    uint16_t nActiveIfaces;
    uint16_t nIfaceCount;
    uint16_t nPort;
    uint8_t nSort;
    xpid_t nPID;
} xtop_ctx_t;

void XTOP_InitContext(xtop_ctx_t *pCtx)
{
    XSock_Init(&pCtx->sock, XSOCK_TCP, XSOCK_INVALID);

    pCtx->bCoreCountManualSet = XFALSE;
    pCtx->bRedrawHelp = XFALSE;
    pCtx->bDisplayHelp = XFALSE;
    pCtx->bDisplayHeader = XFALSE;
    pCtx->bShowAllIfaces = XFALSE;
    pCtx->bShowAllCPUs = XFALSE;
    pCtx->bShowCPUSum = XTRUE;
    pCtx->bLineByLine = XFALSE;
    pCtx->bDaemon = XFALSE;
    pCtx->bServer = XFALSE;
    pCtx->bClient = XFALSE;
    pCtx->bClear = XFALSE;
    pCtx->pStats = NULL;
    pCtx->bQuit = XFALSE;
    pCtx->nSort = XTOP_SORT_LEN;

    xstrncpy(pCtx->sAddr, sizeof(pCtx->sAddr), XTOP_ADDR_DEFAULT);
    xstrnul(pCtx->sLogs);
    xstrnul(pCtx->sLink);
    xstrnul(pCtx->sName);
    xstrnul(pCtx->sToken);
    xstrnul(pCtx->sKey);

    pCtx->nCPUExtraMin = XTOP_CPU_EXTRA_MIN;
    pCtx->nActiveIfaces = 0;
    pCtx->nIfaceCount = 0;
    pCtx->nIntervalU = 0;
    pCtx->nCoreCount = -1;
    pCtx->nRealCores = -1;
    pCtx->nPort = XTOP_PORT_DEFAULT;
    pCtx->nPID = 0;
}

void XTOP_SignalCallback(int sig)
{
    if (sig == 2) printf("\n");
    g_nInterrupted = 1;
}

static char *XTOP_WhiteSpace(const int nLength)
{
    static char sRetVal[XSTR_TINY];
    xstrnul(sRetVal);
    int i = 0;

    int nLen = XSTD_MIN(nLength, sizeof(sRetVal) - 1);
    for (i = 0; i < nLen; i++) sRetVal[i] = ' ';

    sRetVal[i] = '\0';
    return sRetVal;
}

const char *XTOP_GetSortTypeName(uint8_t nSort)
{
    switch (nSort)
    {
        case XTOP_SORT_BUSY: return "[busy]";
        case XTOP_SORT_FREE: return "[free]";
        case XTOP_SORT_NAME: return "[name]";
        case XTOP_SORT_LEN:  return "[len] ";
        default:             return "[none]";
    }
}

void XTOP_DisplayHelp(xtop_ctx_t *pCtx)
{
    printf("======================================================================\n");
    printf("XTOP v%d.%d - (c) 2022-2025 Sandro Kalatozishvili (s.kalatoz@gmail.com)\n",
        XTOP_VERSION_MAJ, XTOP_VERSION_MIN);
    printf("======================================================================\n\n");

    printf("CPU usage bar: %s[%s%slow-priority/%s%snormal/%s%skernel/%s%svirtualized%s      %sused%%%s%s]%s\n",
        XSTR_FMT_BOLD, XSTR_FMT_RESET, XSTR_CLR_BLUE, XSTR_FMT_RESET, XSTR_CLR_GREEN, XSTR_FMT_RESET, XSTR_CLR_RED,
        XSTR_FMT_RESET, XSTR_CLR_CYAN, XSTR_FMT_RESET, XSTR_FMT_DIM, XSTR_FMT_RESET, XSTR_FMT_BOLD, XSTR_FMT_RESET);

    printf("Memory bar:    %s[%s%sused/%s%sbuffers/%s%sshared/%s%scache%s              %sused/total%s%s]%s\n",
    XSTR_FMT_BOLD, XSTR_FMT_RESET, XSTR_CLR_GREEN, XSTR_FMT_RESET, XSTR_CLR_BLUE, XSTR_FMT_RESET, XSTR_CLR_MAGENTA,
    XSTR_FMT_RESET, XSTR_CLR_YELLOW, XSTR_FMT_RESET, XSTR_FMT_DIM, XSTR_FMT_RESET, XSTR_FMT_BOLD, XSTR_FMT_RESET);

    printf("Swap bar:      %s[%s%sused/%s%scache%s                             %sused/total%s%s]%s\n\n",
    XSTR_FMT_BOLD, XSTR_FMT_RESET, XSTR_CLR_RED, XSTR_FMT_RESET, XSTR_CLR_YELLOW,
    XSTR_FMT_RESET, XSTR_FMT_DIM, XSTR_FMT_RESET, XSTR_FMT_BOLD, XSTR_FMT_RESET);

    const char *pShowCpuSum = pCtx->bShowCPUSum ? XTOP_OPT_ON : XTOP_OPT_OFF;
    const char *pShowAllCPU = pCtx->bShowAllCPUs ? XTOP_OPT_ON : XTOP_OPT_OFF;
    const char *pShowAllIfaces = pCtx->bShowAllIfaces ? XTOP_OPT_ON : XTOP_OPT_OFF;
    const char *pShowXTOPHeader = pCtx->bDisplayHeader ? XTOP_OPT_ON : XTOP_OPT_OFF;
    const char *pShowHelpMessage = pCtx->bDisplayHelp ? XTOP_OPT_ON : XTOP_OPT_OFF;
    const char *pScreenRendering = pCtx->bLineByLine ? "[lines]" : "[frame]";
    const char *pSortType = XTOP_GetSortTypeName(pCtx->nSort);

    if (pCtx->nCoreCount < 0 && pCtx->nRealCores)
        pCtx->nCoreCount = pCtx->nRealCores;

    char sSpace[XSTR_TINY] = XSTR_INIT;
    size_t nSpaceSize = pCtx->nCoreCount >= 10 ? 2 : 3;
    nSpaceSize = pCtx->nCoreCount >= 100 ? 1 : nSpaceSize;
    nSpaceSize = pCtx->nCoreCount >= 1000 ? 0 : nSpaceSize;
    xstrnfill(sSpace, sizeof(sSpace), nSpaceSize, XSTR_SPACE_CHAR);

    // S.K. >> Note: Messages may seem not aligned, but they are aligned because of the formatted arguments
    printf("Interactive options are:\n");
    printf("  %s%s+%s [%d] %s        # Increase CPU core count\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET, pCtx->nCoreCount, sSpace);
    printf("  %s%s-%s [%d] %s        # Decrease CPU core count\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET, pCtx->nCoreCount, sSpace);
    printf("  %s%sa%s %s          # Toggle - show CPU sum\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET, pShowCpuSum);
    printf("  %s%sc%s %s          # Toggle - show all CPU cores\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET, pShowAllCPU);
    printf("  %s%si%s %s          # Toggle - show all network interfaces\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET, pShowAllIfaces);
    printf("  %s%sx%s %s          # Toggle - show XTOP header\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET, pShowXTOPHeader);
    printf("  %s%ss%s %s         # Toggle - sort (none/busy/free/name/len)\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET, pSortType);
    printf("  %s%sl%s %s        # Toggle - screen rendering (lines/frame)\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET, pScreenRendering);
    printf("  %s%sh%s %s          # Toggle - show this help screen\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET, pShowHelpMessage);
    printf("  %s%sr%s                # Reset interface to defaults\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s%sq%s                # Quit/exit from XTOP\n\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET);

    printf("Command-line options:\n");
    printf("%sRun XTOP with '-h' argument to see help and command line options.%s\n\n", XSTR_FMT_DIM, XSTR_FMT_RESET);

    printf("%s%sPress 'h' to return.%s\n", XSTR_FMT_BOLD, XSTR_CLR_CYAN, XSTR_FMT_RESET);
}

void XTOP_DisplayUsage(const char *pName)
{
    int nLength = strlen(pName) + 6;

    printf("======================================================================\n");
    printf("XTOP v%d.%d - (c) 2022-2025 Sandro Kalatozishvili (s.kalatoz@gmail.com)\n",
        XTOP_VERSION_MAJ, XTOP_VERSION_MIN);
    printf("======================================================================\n\n");

    printf("CPU usage bar: %s[%s%slow-priority/%s%snormal/%s%skernel/%s%svirtualized%s      %sused%%%s%s]%s\n",
        XSTR_FMT_BOLD, XSTR_FMT_RESET, XSTR_CLR_BLUE, XSTR_FMT_RESET, XSTR_CLR_GREEN, XSTR_FMT_RESET, XSTR_CLR_RED,
        XSTR_FMT_RESET, XSTR_CLR_CYAN, XSTR_FMT_RESET, XSTR_FMT_DIM, XSTR_FMT_RESET, XSTR_FMT_BOLD, XSTR_FMT_RESET);

    printf("Memory bar:    %s[%s%sused/%s%sbuffers/%s%sshared/%s%scache%s              %sused/total%s%s]%s\n",
    XSTR_FMT_BOLD, XSTR_FMT_RESET, XSTR_CLR_GREEN, XSTR_FMT_RESET, XSTR_CLR_BLUE, XSTR_FMT_RESET, XSTR_CLR_MAGENTA,
    XSTR_FMT_RESET, XSTR_CLR_YELLOW, XSTR_FMT_RESET, XSTR_FMT_DIM, XSTR_FMT_RESET, XSTR_FMT_BOLD, XSTR_FMT_RESET);

    printf("Swap bar:      %s[%s%sused/%s%scache%s                             %sused/total%s%s]%s\n\n",
    XSTR_FMT_BOLD, XSTR_FMT_RESET, XSTR_CLR_RED, XSTR_FMT_RESET, XSTR_CLR_YELLOW,
    XSTR_FMT_RESET, XSTR_FMT_DIM, XSTR_FMT_RESET, XSTR_FMT_BOLD, XSTR_FMT_RESET);

    printf("Usage: %s [-e <count>] [-i <iface>] [-m <seconds>] [-t <type>]\n", pName);
    printf(" %s [-a <addr>] [-p <port>] [-l <path>] [-u <pid>] [-d] [-s]\n", XTOP_WhiteSpace(nLength));
    printf(" %s [-U <user>] [-P <pass>] [-K <key>] [-c] [-v] [-x] [-h]\n\n", XTOP_WhiteSpace(nLength));

    printf("Options are:\n");
    printf("  %s-e%s <count>            # Minimum count of extra CPU info\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-i%s <iface>            # Interface name to display on top\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-m%s <seconds>          # Monitoring interval seconds\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-t%s <type>             # Sort result by selected type%s*%s\n", XSTR_CLR_CYAN, XSTR_FMT_RESET, XSTR_CLR_RED, XSTR_FMT_RESET);
    printf("  %s-u%s <pid>              # Track process CPU and memory usage\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-x%s                    # Use system clear instead of ASCII code\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-h%s                    # Print version and usage\n\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);

    printf("Interactive options:\n");
    printf("%sPress the 'h' key when XTOP is running to see interactive options.%s\n\n", XSTR_FMT_DIM, XSTR_FMT_RESET);

    printf("%sXTOP has a REST API server and client mode to send%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("%sand receive statistics to or from a remote server:%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("  %s-a%s <addr>             # Address of the HTTP server\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-p%s <port>             # Port of the HTTP server\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-l%s <path>             # Output directory path for logs\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-c%s                    # Run XTOP as HTTP client\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-s%s                    # Run XTOP as HTTP server\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-d%s                    # Run server as a daemon\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-v%s                    # Enable verbosity\n\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);

    printf("%sWhen using REST server/client mode, the authentication%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("%sparameters can be set with the following arguments:%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("  %s-U%s <user>             # Auth basic user\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-P%s <pass>             # Auth basic pass\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("  %s-K%s <key>              # X-API key\n\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);

    printf("Sort types%s*%s:\n", XSTR_CLR_RED, XSTR_FMT_RESET);
    printf("   %sb%s: Busy on top\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sf%s: Free on top\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sn%s: Sort by name\n\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);

    printf("%sIf XTOP refresh does not clear the window, try system clear%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("%sfor screen clearing. Use CLI argument -x to system clear mode.%s\n\n", XSTR_FMT_DIM, XSTR_FMT_RESET);

    printf("%s%sHint%s:\n", XSTR_FMT_DIM, XSTR_CLR_RED, XSTR_FMT_RESET);
    printf("%sPress the 'h' key when XTOP is running to see interactive options.%s\n\n", XSTR_FMT_DIM, XSTR_FMT_RESET);

    printf("Examples:\n");
    printf("1) %s -x -e 8\n", pName);
    printf("2) %s -m 2 -t b -u 2274\n", pName);
    printf("3) %s -t f -u 2274 -i enp4s0\n", pName);
    printf("4) %s -sa 127.0.0.1 -p 8080\n\n", pName);
}

uint8_t XTOP_GetSortType(const char *pArg)
{
    if (pArg == NULL) return XTOP_SORT_DISABLE;
    else if (*pArg == 'b') return XTOP_SORT_BUSY;
    else if (*pArg == 'f') return XTOP_SORT_FREE;
    else if (*pArg == 'n') return XTOP_SORT_NAME;
    return XTOP_SORT_DISABLE;
}

int XTOP_ParseArgs(xtop_ctx_t *pCtx, int argc, char *argv[])
{
    XTOP_InitContext(pCtx);
    char sUser[XNAME_MAX] = XSTR_INIT;
    char sPass[XSTR_TINY] = XSTR_INIT;

    xbool_t bVerbose = XFALSE;
    int nChar = 0;

    while ((nChar = getopt(argc, argv, "a:e:i:K:U:P:l:m:p:t:u:c1:d1:s1:d1:x1:v1:h1")) != -1)
    {
        switch (nChar)
        {
            case 'a':
                xstrncpy(pCtx->sAddr, sizeof(pCtx->sAddr), optarg);
                break;
            case 'i':
                xstrncpy(pCtx->sName, sizeof(pCtx->sName), optarg);
                break;
            case 'l':
                xstrncpy(pCtx->sLogs, sizeof(pCtx->sLogs), optarg);
                break;
            case 'K':
                xstrncpy(pCtx->sKey, sizeof(pCtx->sKey), optarg);
                break;
            case 'U':
                xstrncpy(sUser, sizeof(sUser), optarg);
                break;
            case 'P':
                xstrncpy(sPass, sizeof(sPass), optarg);
                break;
            case 't':
                pCtx->nSort = XTOP_GetSortType(optarg);
                break;
            case 'e':
                pCtx->nCPUExtraMin = atoi(optarg);
                break;
            case 'm':
                pCtx->nIntervalU = atoi(optarg);
                break;
            case 'p':
                pCtx->nPort = atoi(optarg);
                break;
            case 'u':
                pCtx->nPID = atoi(optarg);
                break;
            case 'c':
                pCtx->bClient = XTRUE;
                break;
            case 'd':
                pCtx->bDaemon = XTRUE;
                break;
            case 's':
                pCtx->bServer = XTRUE;
                break;
            case 'x':
                pCtx->bClear = XTRUE;
                break;
            case 'v':
                bVerbose = XTRUE;
                break;
            case 'h':
            default:
                return 0;
        }
    }

    if (xstrused(sUser) || xstrused(sPass))
        XHTTP_GetAuthToken(pCtx->sToken, sizeof(pCtx->sToken), sUser, sPass);

    if (pCtx->bServer && pCtx->bClient)
    {
        xloge("Please specify only server or client mode");
        return XFALSE;
    }

    if (pCtx->bDaemon && !pCtx->bServer)
    {
        xloge("Daemon argument works only for HTTP server mode");
        return XFALSE;
    }

    if (pCtx->bServer || pCtx->bClient)
    {
        if (!xstrused(pCtx->sAddr) || !pCtx->nPort)
        {
            xloge("Missing addr/port arguments for HTTP server or client");
            return XFALSE;
        }

        xstrncpyf(pCtx->sLink, sizeof(pCtx->sLink), "%s:%u%s",
            pCtx->sAddr, pCtx->nPort, XTOP_API_URI);
    }

    if (!pCtx->nIntervalU) pCtx->nIntervalU = XMON_INTERVAL_USEC;
    else pCtx->nIntervalU *= XMON_INTERVAL_USEC;

    if (xstrused(pCtx->sLogs))
    {
        xlog_path(pCtx->sLogs);
        xlog_file(XTRUE);
    }

    if (xstrused(pCtx->sName))
    {
        char sIfcPath[XPATH_MAX];
        xstrncpyf(sIfcPath, sizeof(sIfcPath), "%s/%s", XSYS_CLASS_NET, pCtx->sName);

        if (!XPath_Exists(sIfcPath))
        {
            xloge("Interface not found: %s", pCtx->sName);
            return XFALSE;
        }
    }

    if (bVerbose && pCtx->bServer)
        xlog_enable(XLOG_ALL);

    return XTRUE;
}

int XTOP_CompareCPUs(const void *pData1, const void *pData2, void *pContext)
{
    xtop_ctx_t *pCtx = (xtop_ctx_t*)pContext;
    xcpu_info_t *pInfo1 = (xcpu_info_t*)((xarray_data_t*)pData1)->pData;
    xcpu_info_t *pInfo2 = (xcpu_info_t*)((xarray_data_t*)pData2)->pData;

    return (pCtx->nSort == XTOP_SORT_FREE) ?
        (int)pInfo2->nIdleTime - (int)pInfo1->nIdleTime:
        (int)pInfo1->nIdleTime - (int)pInfo2->nIdleTime;
}

int XTOP_CompareIFaces(const void *pData1, const void *pData2, void *pContext)
{
    xtop_ctx_t *pCtx = (xtop_ctx_t*)pContext;
    xnet_iface_t *pIface1 = (xnet_iface_t*)((xarray_data_t*)pData1)->pData;
    xnet_iface_t *pIface2 = (xnet_iface_t*)((xarray_data_t*)pData2)->pData;

    if (pCtx->nSort == XTOP_SORT_LEN) return (int)strlen(pIface1->sName) - (int)strlen(pIface2->sName);
    else if (pCtx->nSort == XTOP_SORT_NAME) return strcmp(pIface1->sName, pIface2->sName);

    int nData1 = (int)pIface1->nBytesReceivedPerSec + (int)pIface1->nBytesSentPerSec;
    int nData2 = (int)pIface2->nBytesReceivedPerSec + (int)pIface2->nBytesSentPerSec;
    return (pCtx->nSort == XTOP_SORT_BUSY) ? nData2 - nData1 : nData1 - nData2;
}

int XTOP_FillCPUBar(xcli_bar_t *pBar, xcpu_info_t *pCore, char *pDst, size_t nSize)
{
    if (pDst == NULL || !nSize) return XSTDNON;
    char sNormal[XLINE_MAX], sKernel[XLINE_MAX];
    char sVirt[XLINE_MAX], sLow[XLINE_MAX];

    /* Unpack raw percentage information */
    float fLow = XU32ToFloat(pCore->nUserSpaceNiced);
    float fVirt = XU32ToFloat(pCore->nStealTime);
    float fNormal = XU32ToFloat(pCore->nUserSpace);
    float fKernel = XU32ToFloat(pCore->nKernelSpace);
    fKernel += XU32ToFloat(pCore->nSoftInterrupts);
    fKernel += XU32ToFloat(pCore->nHardInterrupts);
    fKernel += XU32ToFloat(pCore->nIOWait);

    /* Calculate percentage */
    size_t nNormal = pBar->nBarLength * (size_t)XFTON(fNormal) / 100;
    size_t nKernel = pBar->nBarLength * (size_t)XFTON(fKernel) / 100;
    size_t nVirt = pBar->nBarLength * (size_t)XFTON(fVirt) / 100;
    size_t nLow = pBar->nBarLength * (size_t)XFTON(fLow) / 100;
    size_t nSum = nLow + nVirt + nNormal + nKernel;
    float fSum = fNormal + fLow + fVirt + fKernel;

    /* Round the calculated results to improve bar fill accurracy */
    if (fNormal > 0. && !nNormal && nSum < pBar->nBarLength) { nNormal++; nSum++; }
    if (fKernel > 0. && !nKernel && nSum < pBar->nBarLength) { nKernel++; nSum++; }
    if (fVirt > 0. && !nVirt && nSum < pBar->nBarLength) { nVirt++; nSum++; }
    if (fLow > 0. && !nLow && nSum < pBar->nBarLength) { nLow++; nSum++; }
    while (fSum >= 99.95 && nSum < pBar->nBarLength) { nLow++; nSum++; }

    /* Fill partial results with the bar used character */
    xstrnfill(sNormal, sizeof(sNormal), nNormal, pBar->cLoader);
    xstrnfill(sKernel, sizeof(sKernel), nKernel, pBar->cLoader);
    xstrnfill(sVirt, sizeof(sVirt), nVirt, pBar->cLoader);
    xstrnfill(sLow, sizeof(sLow), nLow, pBar->cLoader);

    /* Create colorized line for CPU usage bar */
    return xstrncpyf(pDst, nSize, "%s%s%s%s%s%s%s%s%s%s%s%s",
        XSTR_CLR_BLUE, sLow, XSTR_FMT_RESET,
        XSTR_CLR_GREEN, sNormal, XSTR_FMT_RESET,
        XSTR_CLR_RED, sKernel, XSTR_FMT_RESET,
        XSTR_CLR_CYAN, sVirt, XSTR_FMT_RESET);
}

XSTATUS XTOP_CreareCPUBar(xcli_bar_t *pBar, xcpu_info_t *pCore, size_t nPrefixLen, char *pDst, size_t nDstSize)
{
    if (pCore == NULL || !pCore->nActive) return XSTDNON;
    XCHAR(sUsed, XLINE_MAX);
    XCHAR(sCore, XSTR_TINY);

    xstrnlcpyf(sCore, sizeof(sCore), nPrefixLen, XSTR_SPACE_CHAR, "%d", pCore->nID);
    xstrnclr(pBar->sPrefix, sizeof(pBar->sPrefix), XSTR_CLR_CYAN, "%s", sCore);

    pBar->fPercent = XU32ToFloat(pCore->nUserSpace) + XU32ToFloat(pCore->nUserSpaceNiced);
    pBar->fPercent += XU32ToFloat(pCore->nKernelSpace) + XU32ToFloat(pCore->nSoftInterrupts);
    pBar->fPercent += XU32ToFloat(pCore->nHardInterrupts) + XU32ToFloat(pCore->nIOWait);
    pBar->fPercent += XU32ToFloat(pCore->nStealTime);

    xbool_t bHidePct = XProgBar_CalculateBounds(pBar);
    XTOP_FillCPUBar(pBar, pCore, sUsed, sizeof(sUsed));
    XProgBar_GetOutputAdv(pBar, pDst, nDstSize, sUsed, bHidePct);

    pCore->nActive = 0;
    return XSTDOK;
}

XSTATUS XTOP_AddCPULoadBar(xcli_win_t *pWin, xcli_bar_t *pBar, xcpu_stats_t *pCPU)
{
    xbool_t bSplitBars = pCPU->nCoreCount > 8 ? XTRUE : XFALSE;
    size_t nBarSpace = pCPU->nCoreCount < 100 ? 3 : 5;
    size_t nFirstLen = 0, nSecondLen = 0;
    uint16_t nEdge = 0, nUsedCount = 0;
    uint16_t i, nNext = pCPU->nCoreCount;

    xstrnul(pBar->sSuffix);
    XProgBar_UpdateWindowSize(pBar);

    size_t nDivideFactor = 2;
    if (pCPU->nCoreCount <= 8) nDivideFactor = 2;
    else if (pCPU->nCoreCount <= 12) nDivideFactor = 3;
    else nDivideFactor = 4;

    size_t nOutputColumns = pBar->frame.nColumns / 2;
    pBar->frame.nColumns /= nDivideFactor;

    float fDivideFactor = (float)pCPU->nCoreCount / (float)nDivideFactor;
    if (fDivideFactor != (size_t)fDivideFactor) fDivideFactor += 1.;

    for (i = 0; i < pCPU->nCoreCount; i++)
    {
        XCHAR(sFirst, XLINE_MAX);
        XCHAR(sSecond, XLINE_MAX);
        XCHAR(sThird, XLINE_MAX);
        XCHAR(sFourth, XLINE_MAX);

        xcpu_info_t *pCore = (xcpu_info_t*)XArray_GetData(&pCPU->cores, i);
        if (pCore != NULL)
        {
            if (nUsedCount >= pCPU->nCoreCount) break;
            else if (nEdge && i == nEdge) continue;

            nNext = i + (size_t)fDivideFactor;
            if (!nEdge) nEdge = nNext;

            XSTATUS nStatus = XTOP_CreareCPUBar(pBar, pCore, 5, sFirst, sizeof(sFirst));
            if (nStatus == XSTDOK) nUsedCount++;

            if (i == nNext || nNext >= pCPU->nCoreCount)
            {
                xstrnfill(sSecond, sizeof(sSecond), pBar->frame.nColumns, XSTR_SPACE_CHAR);
                nStatus = XCLIWin_AddLineFmt(pWin, "%s%s%s%s", sFirst, sSecond, sThird, sFourth);

                if ((!nFirstLen || !nSecondLen) && nDivideFactor >= 4)
                {
                    xstrextra(sFirst, strlen(sFirst), sizeof(sFirst), &nFirstLen, NULL);
                    xstrextra(sSecond, strlen(sSecond), sizeof(sSecond), &nSecondLen, NULL);
                    nOutputColumns = nFirstLen + (bSplitBars ? nSecondLen : 0) + 1;
                }

                pBar->frame.nColumns = nOutputColumns;
                return nStatus;
            }

            xcpu_info_t *pSecondCore = (xcpu_info_t*)XArray_GetData(&pCPU->cores, nNext);
            if (pSecondCore != NULL)
            {
                size_t nFixedSpace = bSplitBars ? nBarSpace : 5;
                nStatus = XTOP_CreareCPUBar(pBar, pSecondCore, nFixedSpace, sSecond, sizeof(sSecond));
                if (nStatus == XSTDOK) nUsedCount++;
            }

            uint16_t nThird = nNext + (size_t)fDivideFactor;
            uint16_t nFourth = nThird + (size_t)fDivideFactor;

            if (nDivideFactor >= 3 && nThird < pCPU->nCoreCount)
            {
                xcpu_info_t *pThirdCore = (xcpu_info_t*)XArray_GetData(&pCPU->cores, nThird);
                if (pThirdCore != NULL)
                {
                    nStatus = XTOP_CreareCPUBar(pBar, pThirdCore, nBarSpace, sThird, sizeof(sThird));
                    if (nStatus == XSTDOK) nUsedCount++;
                }
            }

            if (nDivideFactor >= 4 && nFourth < pCPU->nCoreCount)
            {
                xcpu_info_t *pFourthCore = (xcpu_info_t*)XArray_GetData(&pCPU->cores, nFourth);
                if (pFourthCore != NULL)
                {
                    nStatus = XTOP_CreareCPUBar(pBar, pFourthCore, nBarSpace, sFourth, sizeof(sFourth));
                    if (nStatus == XSTDOK) nUsedCount++;
                }
            }

            XCLIWin_AddLineFmt(pWin, "%s%s%s%s", sFirst, sSecond, sThird, sFourth);

            if ((!nFirstLen || !nSecondLen) && nDivideFactor >= 4)
            {
                xstrextra(sFirst, strlen(sFirst), sizeof(sFirst), &nFirstLen, NULL);
                xstrextra(sSecond, strlen(sSecond), sizeof(sSecond), &nSecondLen, NULL);
                nOutputColumns = nFirstLen + (bSplitBars ? nSecondLen : 0) + 1;
            }
        }
    }

    pBar->frame.nColumns = nOutputColumns;
    return XSTDOK;
}

int XTOP_FillMemoryBar(xcli_bar_t *pBar, xmem_info_t *pMemInfo, char *pDst, size_t nSize)
{
    if (pDst == NULL || !nSize) return XSTDNON;
    char sBuffers[XLINE_MAX], sUsed[XLINE_MAX];
    char sShared[XLINE_MAX], sCached[XLINE_MAX];

    size_t nMaxSize = pBar->nBarLength;
    size_t nMaxUsed = pBar->nBarUsed;

    size_t nTotalUsed = pMemInfo->nMemoryTotal - pMemInfo->nMemoryFree;
    size_t nCached = pMemInfo->nMemoryCached - pMemInfo->nMemoryShared;
    size_t nUsed = nTotalUsed - (pMemInfo->nBuffers + pMemInfo->nMemoryCached);

    double fBuffers = nTotalUsed ? (double)100 / nTotalUsed * pMemInfo->nBuffers : 0.;
    double fShared = nTotalUsed ? (double)100 / nTotalUsed * pMemInfo->nMemoryShared : 0.;
    double fCached = nTotalUsed ? (double)100 / nTotalUsed * nCached : 0.;
    double fUsed = nTotalUsed ? (double)100 / nTotalUsed * nUsed : 0.;

    size_t nBuffersPct = nMaxUsed * (size_t)floor(fBuffers) / 100;
    size_t nSharedPct = nMaxUsed * (size_t)floor(fShared) / 100;
    size_t nCachedPct = nMaxUsed * (size_t)floor(fCached) / 100;
    size_t nUsedPct = nMaxUsed * (size_t)floor(fUsed) / 100;
    size_t nSum = nUsedPct + nSharedPct + nBuffersPct + nCachedPct;

    /* Round the calculated results to improve bar fill accurracy */
    if (fBuffers > 0. && !nBuffersPct && nSum < nMaxSize) { nBuffersPct++; nSum++; }
    if (fShared > 0. && !nSharedPct && nSum < nMaxSize) { nSharedPct++; nSum++; }
    if (fCached > 0. && !nCachedPct && nSum < nMaxSize) { nCachedPct++; nSum++; }
    if (fUsed > 0. && !nUsedPct && nSum < nMaxSize) nUsedPct++;

    xstrnfill(sBuffers, sizeof(sBuffers), nBuffersPct, pBar->cLoader);
    xstrnfill(sShared, sizeof(sShared), nSharedPct, pBar->cLoader);
    xstrnfill(sCached, sizeof(sCached), nCachedPct, pBar->cLoader);
    xstrnfill(sUsed, sizeof(sUsed), nUsedPct, pBar->cLoader);

    return xstrncpyf(pDst, nSize, "%s%s%s%s%s%s%s%s%s%s%s%s",
        XSTR_CLR_GREEN, sUsed, XSTR_FMT_RESET,
        XSTR_CLR_BLUE, sBuffers, XSTR_FMT_RESET,
        XSTR_CLR_MAGENTA, sShared, XSTR_FMT_RESET,
        XSTR_CLR_YELLOW, sCached, XSTR_FMT_RESET);
}

int XTOP_FillSwapBar(xcli_bar_t *pBar, xmem_info_t *pMemInfo, char *pDst, size_t nSize)
{
    if (pDst == NULL || !nSize) return XSTDNON;
    char sUsed[XLINE_MAX], sCached[XLINE_MAX];

    size_t nMaxSize = pBar->nBarLength;
    size_t nMaxUsed = pBar->nBarUsed;

    /* Calculate swap and cache usage percents */
    size_t nSwapUsed = pMemInfo->nSwapTotal - pMemInfo->nSwapFree - pMemInfo->nSwapCached;
    double fCached = nSwapUsed ? (double)100 / nSwapUsed * pMemInfo->nSwapCached : 0.;
    double fUsed = nSwapUsed ? (double)100 / pMemInfo->nSwapTotal * nSwapUsed : 0.;

    /* Calculate swap and cached percents in usage bar */
    size_t nCachedPct = nMaxUsed * (size_t)floor(fCached) / 100;
    size_t nUsedPct = nMaxUsed * (size_t)floor(fUsed) / 100;
    size_t nSum = nUsedPct + nCachedPct;

    /* Round the calculated results to improve bar fill accurracy */
    if (fCached > 0. && !nCachedPct && nSum < nMaxSize) { nCachedPct++; nSum++; }
    if (fUsed > 0. && !nUsedPct && nSum < nMaxSize) nUsedPct++;

    /* Fill partial results with the bar used character */
    xstrnfill(sCached, sizeof(sCached), nCachedPct, pBar->cLoader);
    xstrnfill(sUsed, sizeof(sUsed), nUsedPct, pBar->cLoader);

    return xstrncpyf(pDst, nSize, "%s%s%s%s%s%s",
        XSTR_CLR_RED, sUsed, XSTR_FMT_RESET,
        XSTR_CLR_YELLOW, sCached, XSTR_FMT_RESET);
}

XSTATUS XTOP_AddOverallBar(xcli_win_t *pWin, xcli_bar_t *pBar, xmem_info_t *pMemInfo, xcpu_stats_t *pCPU)
{
    if (pMemInfo->nMemoryTotal < pMemInfo->nMemoryAvail) return XSTDNON;
    char sLine[XLINE_MAX], sBuff[XSTR_TINY];
    char sUsed[XSTR_TINY], sCache[XSTR_TINY];    

    /* Calculate memory usage percentage */
    size_t nTotalUsed = pMemInfo->nMemoryTotal - pMemInfo->nMemoryFree;
    size_t nUsed = nTotalUsed - (pMemInfo->nBuffers + pMemInfo->nMemoryCached);
    pBar->fPercent = nTotalUsed ? (double)100 / pMemInfo->nMemoryTotal * nTotalUsed : 0.;

    /* Create memory usage bar */
    XKBToUnit(sUsed, sizeof(sUsed), nUsed, XTRUE);
    XKBToUnit(sBuff, sizeof(sBuff), pMemInfo->nMemoryTotal, XTRUE);
    xstrnclr(pBar->sPrefix, sizeof(pBar->sPrefix), XSTR_CLR_CYAN, "%s", "  Mem");
    xstrncpyf(pBar->sSuffix, sizeof(pBar->sSuffix),
        "%s%s/%s%s", XSTR_FMT_DIM, sUsed, sBuff, XSTR_FMT_RESET);

    xstrnul(sUsed);
    xbool_t bHidePct = XProgBar_CalculateBounds(pBar);
    XTOP_FillMemoryBar(pBar, pMemInfo, sUsed, sizeof(sUsed));
    XProgBar_GetOutputAdv(pBar, sLine, sizeof(sLine), sUsed, bHidePct);

    /* Create and append memory usage info next to memory bar */
    XKBToUnit(sBuff, sizeof(sBuff), pMemInfo->nBuffers, XTRUE);
    XKBToUnit(sUsed, sizeof(sUsed), pMemInfo->nMemoryShared, XTRUE);
    XKBToUnit(sCache, sizeof(sCache), pMemInfo->nMemoryCached, XTRUE);
    XCLIWin_AddLineFmt(pWin, "%s "
                "%sBuff:%s %s, "
                "%sShared:%s %s, "
                "%sCached:%s %s", sLine,
                XSTR_CLR_CYAN, XSTR_FMT_RESET, sBuff,
                XSTR_CLR_CYAN, XSTR_FMT_RESET, sUsed,
                XSTR_CLR_CYAN, XSTR_FMT_RESET, sCache);

    /* Calculate swap usage percentage */
    if (pMemInfo->nSwapTotal < pMemInfo->nSwapFree) return XSTDNON;
    size_t nSwapUsed = pMemInfo->nSwapTotal - pMemInfo->nSwapFree - pMemInfo->nSwapCached;
    pBar->fPercent = nSwapUsed ? (double)100 / pMemInfo->nSwapTotal * nSwapUsed : 0.;

    /* Create swap usage bar */
    XKBToUnit(sUsed, sizeof(sUsed), nSwapUsed, XTRUE);
    XKBToUnit(sBuff, sizeof(sBuff), pMemInfo->nSwapTotal, XTRUE);
    xstrnclr(pBar->sPrefix, sizeof(pBar->sPrefix), XSTR_CLR_CYAN, "%s", "  Swp");
    xstrncpyf(pBar->sSuffix, sizeof(pBar->sSuffix),
        "%s%s/%s%s", XSTR_FMT_DIM, sUsed, sBuff, XSTR_FMT_RESET);

    xstrnul(sUsed);
    bHidePct = XProgBar_CalculateBounds(pBar);
    XTOP_FillSwapBar(pBar, pMemInfo, sUsed, sizeof(sUsed));
    XProgBar_GetOutputAdv(pBar, sLine, sizeof(sLine), sUsed, bHidePct);

    XKBToUnit(sCache, sizeof(sCache), pMemInfo->nSwapCached, XTRUE);
    XCLIWin_AddLineFmt(pWin, "%s "
                "%sSwp Cached:%s %s, "
                "%sLoad avg:%s %s%.2f%s %s%.2f%s %s%.2f%s",
                sLine, XSTR_CLR_CYAN, XSTR_FMT_RESET,
                sCache, XSTR_CLR_CYAN, XSTR_FMT_RESET,
                XSTR_FMT_BOLD, XU32ToFloat(pCPU->nLoadAvg[0]), XSTR_FMT_RESET,
                XSTR_CLR_LIGHT_CYAN, XU32ToFloat(pCPU->nLoadAvg[1]), XSTR_FMT_RESET,
                XSTR_CLR_LIGHT_BLUE, XU32ToFloat(pCPU->nLoadAvg[2]), XSTR_FMT_RESET);

    /* Create half-empry line for pretty print */
    XProgBar_UpdateWindowSize(pBar);

    size_t sLineLen = 0;
    xstrextra(sLine, strlen(sLine), sizeof(sLine), &sLineLen, NULL);
    xstrnfill(sLine, sizeof(sLine), sLineLen + 1, XSTR_SPACE_CHAR);

    /* Create and append process track info next to swap bar */
    XKBToUnit(sUsed, sizeof(sUsed), pMemInfo->nResidentMemory, XTRUE);
    XKBToUnit(sBuff, sizeof(sBuff), pMemInfo->nVirtualMemory, XTRUE);
    return XCLIWin_AddLineFmt(pWin, "%s%sRes:%s %s, %sVirt:%s %s, %sUS:%s %.2f, %sKS:%s %.2f", sLine,
                 XSTR_CLR_CYAN, XSTR_FMT_RESET, sUsed, XSTR_CLR_CYAN, XSTR_FMT_RESET, sBuff,
                 XSTR_CLR_CYAN, XSTR_FMT_RESET, XU32ToFloat(pCPU->usage.nUserSpaceUsage),
                 XSTR_CLR_CYAN, XSTR_FMT_RESET, XU32ToFloat(pCPU->usage.nKernelSpace));
}

void XTOP_AddCPUInfoUnit(char *pLine, size_t nSize, double fPct, xbool_t bIdle)
{
    char sBuff[XSTR_TINY];
    const char *pColor;

    if (bIdle)
    {
        if (fPct > 50.) pColor = XSTR_CLR_GREEN;
        else if (fPct <= 20.) pColor = XLOG_COLOR_RED;
        else pColor = XLOG_COLOR_YELLOW;
    }
    else
    {
        if (fPct < 50.) pColor = XSTR_CLR_NONE;
        else if (fPct >= 80.) pColor = XLOG_COLOR_RED;
        else pColor = XLOG_COLOR_YELLOW;
    }

    size_t nIdleLen = xstrnclr(sBuff, sizeof(sBuff), pColor, "%.2f", fPct);
    nIdleLen -= xstrextra(sBuff, nIdleLen, 0, NULL, NULL);

    if (nIdleLen < 8)
    {
        char sEmpty[XSTR_TINY];
        xstrnfill(sEmpty, sizeof(sEmpty), 8 - nIdleLen, XSTR_SPACE_CHAR);
        xstrncat(pLine, nSize, "%s%s", sEmpty, sBuff);
    }
}

XSTATUS XTOP_AddCPUInfo(xcli_win_t *pWin, xcpu_info_t *pCore)
{
    char sLine[XLINE_MAX];
    char sCore[XSTR_TINY];

    if (pCore->nID >= 0)
    {
        xstrnlcpyf(sCore, sizeof(sCore), 4, XSTR_SPACE_CHAR, "%d", pCore->nID);
        xstrncpyf(sLine, sizeof(sLine), "%s%s%s", XSTR_FMT_DIM, sCore, XSTR_FMT_RESET);
    }
    else
    {
        xstrnlcpyf(sCore, sizeof(sCore), 4, XSTR_SPACE_CHAR, "%s", "s");
        xstrncpyf(sLine, sizeof(sLine), "%s%s%s%s", XSTR_FMT_BOLD, XSTR_FMT_ITALIC, sCore, XSTR_FMT_RESET);
    }

    XTOP_AddCPUInfoUnit(sLine, sizeof(sLine), XU32ToFloat(pCore->nIdleTime), XTRUE);
    XTOP_AddCPUInfoUnit(sLine, sizeof(sLine), XU32ToFloat(pCore->nUserSpace), XFALSE);
    XTOP_AddCPUInfoUnit(sLine, sizeof(sLine), XU32ToFloat(pCore->nKernelSpace), XFALSE);
    XTOP_AddCPUInfoUnit(sLine, sizeof(sLine), XU32ToFloat(pCore->nUserSpaceNiced), XFALSE);
    XTOP_AddCPUInfoUnit(sLine, sizeof(sLine), XU32ToFloat(pCore->nSoftInterrupts), XFALSE);
    XTOP_AddCPUInfoUnit(sLine, sizeof(sLine), XU32ToFloat(pCore->nHardInterrupts), XFALSE);
    XTOP_AddCPUInfoUnit(sLine, sizeof(sLine), XU32ToFloat(pCore->nIOWait), XFALSE);
    XTOP_AddCPUInfoUnit(sLine, sizeof(sLine), XU32ToFloat(pCore->nStealTime), XFALSE);
    XTOP_AddCPUInfoUnit(sLine, sizeof(sLine), XU32ToFloat(pCore->nGuestTime), XFALSE);
    XTOP_AddCPUInfoUnit(sLine, sizeof(sLine), XU32ToFloat(pCore->nGuestNiced), XFALSE);
    return XCLIWin_AddLineFmt(pWin, "%s", sLine);
}

XSTATUS XTOP_AddCPUExtra(xtop_ctx_t *pCtx, xcli_win_t *pWin, xcli_bar_t *pBar, xmem_info_t *pMemInfo, xcpu_stats_t *pCPU)
{
    XCLIWin_AddAligned(pWin, XTOP_CPU_HEADER, XSTR_BACK_BLUE, XCLI_LEFT);
    if (pCtx->nRealCores < 0) pCtx->nRealCores = pCPU->nCoreCount;

    if (pCtx->bShowCPUSum)
    {
        XSTATUS nStatus = XTOP_AddCPUInfo(pWin, &pCPU->sum);
        if (nStatus <= 0) return nStatus;
    }

    if (pCtx->bShowAllCPUs || pCtx->nCoreCount < 0 ||
        pCtx->nCoreCount > (int)pCPU->nCoreCount)
        pCtx->nCoreCount = (int)pCPU->nCoreCount;

    if (!pCtx->bShowAllCPUs)
    {
        // + 3 for space, iface header and total CPU line
        size_t nOccupiedLines = pWin->lines.nUsed + pCtx->nActiveIfaces + 3;

        if (!pCtx->bCoreCountManualSet)
        {
            // If we have no space, show less cpus
            while ((nOccupiedLines + pCtx->nCoreCount) > pWin->frame.nRows)
            {
                if (pCtx->nCoreCount <= (int)pCtx->nCPUExtraMin) break;
                pCtx->nCoreCount--;
            }

            // If we have a space, show more cpus
            while ((nOccupiedLines + pCtx->nCoreCount + 1) <= pWin->frame.nRows)
            {
                if (pCtx->nCoreCount >= (int)pCPU->nCoreCount) break;
                pCtx->nCoreCount++;
            }
        }

        if ((pCtx->nSort && pCPU->nCoreCount &&
            pCtx->nSort != XTOP_SORT_NAME &&
            pCtx->nSort != XTOP_SORT_LEN) ||
            pCPU->nCoreCount != pCtx->nCoreCount)
            XArray_Sort(&pCPU->cores, XTOP_CompareCPUs, pCtx);
    }

    XSTATUS nStatus = XSTDOK;
    size_t i;

    for (i = 0; i < pCtx->nCoreCount; i++)
    {
        xcpu_info_t *pCore = (xcpu_info_t*)XArray_GetData(&pCPU->cores, i);
        if (pCore != NULL) nStatus = XTOP_AddCPUInfo(pWin, pCore);
    }

    return nStatus;
}

static xbool_t XTOP_IsNarrowInterface(xcli_win_t *pWin)
{
    // Below 102 columns we can not fit all interface data without truncation
    return (pWin->frame.nColumns < 102) ? XTRUE : XFALSE;
}

static uint8_t XTOP_GetIfaceSpacePadding(xcli_win_t *pWin, xbool_t bIsHeader)
{
    if (XTOP_IsNarrowInterface(pWin))
    {
        return bIsHeader ?
            XIFACE_HDR_NARROW_PADDING :
            XIFACE_NAME_NARROW_PADDING;
    }

    return bIsHeader ?
        XIFACE_HDR_WIDE_PADDING :
        XIFACE_NAME_WIDE_PADDING;
}

static uint8_t XTOP_GetAddrSpacePadding(xcli_win_t *pWin, size_t nMaxIPLen)
{
    // Calculate space padding for IP and MAC addresses based on window size
    uint8_t nSpacePadding = (pWin->frame.nColumns < 112) ? 7 : 8;
    nSpacePadding = (pWin->frame.nColumns < 110) ? 6 : nSpacePadding;
    nSpacePadding = (pWin->frame.nColumns < 108) ? 5 : nSpacePadding;
    nSpacePadding = (pWin->frame.nColumns < 106) ? 4 : nSpacePadding;
    nSpacePadding = (pWin->frame.nColumns < 104) ? 3 : nSpacePadding;
    nSpacePadding = (pWin->frame.nColumns < 102) ? 5 : nSpacePadding;
    nSpacePadding = (pWin->frame.nColumns < 97) ? 4 : nSpacePadding;
    nSpacePadding = (pWin->frame.nColumns < 95) ? 3 : nSpacePadding;

    if (nSpacePadding < 4 && !XTOP_IsNarrowInterface(pWin))
    {
        if (nMaxIPLen < 10) nSpacePadding += 3;
        else if (nMaxIPLen < 12) nSpacePadding += 2;
        else if (nMaxIPLen < 14) nSpacePadding += 1;
    }

    return nSpacePadding;
}

XSTATUS XTOP_AddInterface(xcli_win_t *pWin, xtop_ctx_t *pCtx, size_t nMaxIPLen, xnet_iface_t *pIface, size_t nLength)
{
    char sLine[XLINE_MAX], sName[XSTR_TINY], sRound[XSTR_TINY], sData[XSTR_TINY];
    xstrnlcpyf(sName, sizeof(sName), nLength + 1, XSTR_SPACE_CHAR, "%s", pIface->sName);
    xstrncpy(sLine, sizeof(sLine), sName);

    uint8_t nSpacePadding = XTOP_GetIfaceSpacePadding(pWin, XFALSE);
    xbool_t bShort = XTOP_IsNarrowInterface(pWin);

    XBytesToUnit(sData, sizeof(sData), pIface->nBytesReceivedPerSec, bShort);
    xstrnlcpyf(sRound, sizeof(sRound), nSpacePadding, XSTR_SPACE_CHAR, "%s", sData);
    xstrncat(sLine, sizeof(sLine), "%s/s", sRound);

    XBytesToUnit(sData, sizeof(sData), pIface->nBytesSentPerSec, bShort);
    xstrnlcpyf(sRound, sizeof(sRound), nSpacePadding, XSTR_SPACE_CHAR, "%s", sData);
    xstrncat(sLine, sizeof(sLine), "%s/s", sRound);

    uint64_t nSum = pIface->nBytesReceivedPerSec + pIface->nBytesSentPerSec;
    XBytesToUnit(sData, sizeof(sData), nSum, bShort);
    xstrnlcpyf(sRound, sizeof(sRound), nSpacePadding, XSTR_SPACE_CHAR, "%s", sData);
    xstrncat(sLine, sizeof(sLine), "%s/s", sRound);

    nSpacePadding = XTOP_GetAddrSpacePadding(pWin, nMaxIPLen);
    xstrnlcpyf(sRound, sizeof(sRound), strlen(pIface->sHWAddr) + nSpacePadding, XSTR_SPACE_CHAR, "%s", pIface->sHWAddr);
    if (strncmp(pIface->sHWAddr, XNET_HWADDR_DEFAULT, 17)) xstrncat(sLine, sizeof(sLine), "%s", sRound);
    else xstrncat(sLine, sizeof(sLine), "%s%s%s", XSTR_FMT_DIM, sRound, XSTR_FMT_RESET);

    xstrnlcpyf(sRound, sizeof(sRound), strlen(pIface->sIPAddr) + nSpacePadding, XSTR_SPACE_CHAR, "%s", pIface->sIPAddr);
    if (strncmp(pIface->sIPAddr, XNET_IPADDR_DEFAULT, 7)) xstrncat(sLine, sizeof(sLine), "%s", sRound);
    else xstrncat(sLine, sizeof(sLine), "%s%s%s", XSTR_FMT_DIM, sRound, XSTR_FMT_RESET);

    return XCLIWin_AddLineFmt(pWin, "%s", sLine);
}

XSTATUS XTOP_IsIfaceValidIP(xnet_iface_t *pIface)
{
    if (!pIface ||
        !xstrused(pIface->sIPAddr) ||
        !strncmp(pIface->sIPAddr, "0.0.0.0", 7))
    {
        return XFALSE;
    }

    return XTRUE;
}

xbool_t XTOP_HasIfaceValidMac(xnet_iface_t *pIface)
{
    if (!pIface ||
        !xstrused(pIface->sHWAddr) ||
        !strncmp(pIface->sHWAddr, "00:00:00:00:00:00", 17))
    {
        return XFALSE;
    }

    return XTRUE;
}

XSTATUS XTOP_AddNetworkInfo(xtop_ctx_t *pCtx, xcli_win_t *pWin, xarray_t *pIfaces)
{
    if (pCtx->nSort) XArray_Sort(pIfaces, XTOP_CompareIFaces, pCtx);
    size_t nTrackLen = strlen(pCtx->sName);
    size_t i, nLength = 0;
    int nTrackID = -1;

    size_t nSumRX, nSumTX, nMaxIPLen = 0;
    nSumRX = nSumTX = nMaxIPLen = 0;

    for (i = 0; i < pIfaces->nUsed; i++)
    {
        xnet_iface_t *pIface = (xnet_iface_t*)XArray_GetData(pIfaces, i);
        if (pIface == NULL) continue;

        nSumRX += pIface->nBytesReceivedPerSec;
        nSumTX += pIface->nBytesSentPerSec;

        if (xstrused(pIface->sName) == XTRUE && nTrackLen > 0 && nTrackID < 0 &&
            !strncmp(pCtx->sName, pIface->sName, nTrackLen)) nTrackID = (int)i;

        size_t nIPAddrLen = strnlen(pIface->sIPAddr, sizeof(pIface->sIPAddr));
        if (nIPAddrLen > nMaxIPLen) nMaxIPLen = nIPAddrLen;

        size_t nNextLength = strnlen(pIface->sName, sizeof(pIface->sName));
        if (pWin->frame.nColumns < 132 && nNextLength > 12)
        {
            nNextLength = 12;
            pIface->sName[9] = '.';
            pIface->sName[10] = '.';
            pIface->sName[11] = '.';
            pIface->sName[12] = XSTR_NUL;
        }

        if (nNextLength > nLength)
            nLength = nNextLength;
    }

    /* If iface length is less than "total", take "total" length as maximum */
    if (nLength < XTOP_TOTAL_LEN) nLength = XTOP_TOTAL_LEN;

    char sLine[XLINE_MAX], sRound[XSTR_TINY], sData[XSTR_TINY];
    size_t nPreHdr = nLength > 4 ? nLength - 4 : nLength;
    xstrnfill(sLine, sizeof(sLine), nPreHdr, XSTR_SPACE_CHAR);
    xstrncat(sLine, sizeof(sLine), "%s", XTOP_IFACE_HEADER);

    uint8_t nSpacePadding = XTOP_GetIfaceSpacePadding(pWin, XTRUE);
    xstrnlcpyf(sRound, sizeof(sRound), nSpacePadding, XSTR_SPACE_CHAR, "%s", "RX");
    xstrncat(sLine, sizeof(sLine), "%s", sRound);

    xstrnlcpyf(sRound, sizeof(sRound), nSpacePadding, XSTR_SPACE_CHAR, "%s", "TX");
    xstrncat(sLine, sizeof(sLine), "%s", sRound);

    xstrnlcpyf(sRound, sizeof(sRound), nSpacePadding, XSTR_SPACE_CHAR, "%s", "SUM");
    xstrncat(sLine, sizeof(sLine), "%s", sRound);

    nSpacePadding = XTOP_GetAddrSpacePadding(pWin, nMaxIPLen) + 10;
    xstrnlcpyf(sRound, sizeof(sRound), nSpacePadding, XSTR_SPACE_CHAR, "%s", "MAC");
    xstrncat(sLine, sizeof(sLine), "%s", sRound);

    xstrnlcpyf(sRound, sizeof(sRound), nSpacePadding - 1, XSTR_SPACE_CHAR, "%s", "IP");
    xstrncat(sLine, sizeof(sLine), "%s", sRound);

    XCLIWin_AddAligned(pWin, sLine, XSTR_BACK_BLUE, XCLI_LEFT);
    pCtx->nIfaceCount = pCtx->nActiveIfaces = 0;

    if (nTrackID >= 0)
    {
        xnet_iface_t *pIface = (xnet_iface_t*)XArray_GetData(pIfaces, nTrackID);
        if (pIface != NULL) XTOP_AddInterface(pWin, pCtx, nMaxIPLen, pIface, nLength);
    }

    int nAvailableLines = (int)pWin->frame.nRows - (int)pWin->lines.nUsed - 1;
    int nPrintableIfaces = 0;

    for (i = 0; i < pIfaces->nUsed; i++)
    {
        xnet_iface_t *pIface = (xnet_iface_t*)XArray_GetData(pIfaces, i);
        if (pIface != NULL)
        {
            if (XTOP_IsIfaceValidIP(pIface) || pCtx->bShowAllIfaces)
            {
                pCtx->nActiveIfaces++;
                nAvailableLines--;
            }
            else if (XTOP_HasIfaceValidMac(pIface)) nPrintableIfaces++;
        }
    }

    int nDummyIfaceSpace = nAvailableLines - nPrintableIfaces;

    for (i = 0; i < pIfaces->nUsed; i++)
    {
        if (nTrackID >= 0 && i == nTrackID) continue; 

        xnet_iface_t *pIface = (xnet_iface_t*)XArray_GetData(pIfaces, i);
        if (pIface != NULL)
        {
            if (XTOP_IsIfaceValidIP(pIface) || pCtx->bShowAllIfaces)
            {
                XTOP_AddInterface(pWin, pCtx, nMaxIPLen, pIface, nLength);
                pCtx->nIfaceCount++;
                continue;
            }

            if (nAvailableLines > 0)
            {
                if (XTOP_HasIfaceValidMac(pIface))
                {
                    if (nPrintableIfaces > 0)
                    {
                        nPrintableIfaces--;
                    }
                    else
                    {
                        continue;
                    }
                }
                else if (nDummyIfaceSpace > 0)
                {
                    nDummyIfaceSpace--;
                }
                else
                {
                    continue;
                }

                XTOP_AddInterface(pWin, pCtx, nMaxIPLen, pIface, nLength);
                pCtx->nIfaceCount++;
                nAvailableLines--;
            }
        }
    }

    nSpacePadding = XTOP_GetIfaceSpacePadding(pWin, XFALSE);
    xbool_t bShort = XTOP_IsNarrowInterface(pWin);

    xstrnlcpyf(sLine, sizeof(sLine), nLength + 1, XSTR_SPACE_CHAR, "%s", "total");
    XBytesToUnit(sData, sizeof(sData), nSumRX, bShort);
    xstrnlcpyf(sRound, sizeof(sRound), nSpacePadding, XSTR_SPACE_CHAR, "%s", sData);
    xstrncat(sLine, sizeof(sLine), "%s/s", sRound);

    XBytesToUnit(sData, sizeof(sData), nSumTX, bShort);
    xstrnlcpyf(sRound, sizeof(sRound), nSpacePadding, XSTR_SPACE_CHAR, "%s", sData);
    xstrncat(sLine, sizeof(sLine), "%s/s", sRound);

    XBytesToUnit(sData, sizeof(sData), nSumRX + nSumTX, bShort);
    xstrnlcpyf(sRound, sizeof(sRound), nSpacePadding, XSTR_SPACE_CHAR, "%s", sData);
    xstrncat(sLine, sizeof(sLine), "%s/s", sRound);
    return XCLIWin_AddAligned(pWin, sLine, XSTR_CLR_LIGHT_CYAN, XCLI_LEFT);
}

void XTOP_ParseCoreObj(xjson_obj_t *pCoreObj, xcpu_info_t *pCore)
{
    pCore->nSoftInterrupts = XJSON_GetU32(XJSON_GetObject(pCoreObj, "softInterrupts"));
    pCore->nHardInterrupts = XJSON_GetU32(XJSON_GetObject(pCoreObj, "hardInterrupts"));
    pCore->nUserSpaceNiced = XJSON_GetU32(XJSON_GetObject(pCoreObj, "userSpaceNiced"));
    pCore->nKernelSpace = XJSON_GetU32(XJSON_GetObject(pCoreObj, "kernelSpace"));
    pCore->nUserSpace = XJSON_GetU32(XJSON_GetObject(pCoreObj, "userSpace"));
    pCore->nIdleTime = XJSON_GetU32(XJSON_GetObject(pCoreObj, "idleTime"));
    pCore->nIOWait = XJSON_GetU32(XJSON_GetObject(pCoreObj, "ioWait"));
    pCore->nStealTime = XJSON_GetU32(XJSON_GetObject(pCoreObj, "stealTime"));
    pCore->nGuestTime = XJSON_GetU32(XJSON_GetObject(pCoreObj, "guestTime"));
    pCore->nGuestNiced = XJSON_GetU32(XJSON_GetObject(pCoreObj, "guestNiced"));
    pCore->nActive = XJSON_GetU32(XJSON_GetObject(pCoreObj, "active"));
    pCore->nID = XJSON_GetU32(XJSON_GetObject(pCoreObj, "id"));
}

int XTOP_GetJSONStats(xmon_stats_t *pStats, xjson_t *pJson)
{
    xcpu_stats_t *pCpuStats = &pStats->cpuStats;
    xmem_info_t *pMemInfo = &pStats->memInfo;

    XArray_Destroy(&pStats->netIfaces);
    XArray_Destroy(&pCpuStats->cores);

    xjson_obj_t *pCPUObj = XJSON_GetObject(pJson->pRootObj, "cpu");
    if (pCPUObj == NULL)
    {
        xloge("Response does not contain CPU object in JSON");
        return XSTDERR;
    }

    xjson_obj_t *pLoadAvgObj = XJSON_GetObject(pCPUObj, "loadAverage");
    if (pCPUObj == NULL)
    {
        xloge("Response does not contain CPU loadAverage object in JSON");
        return XSTDERR;
    }

    size_t i, nLength = XJSON_GetArrayLength(pLoadAvgObj);
    for (i = 0; i < nLength; i++)
    {
        xjson_obj_t *pArrItemObj = XJSON_GetArrayItem(pLoadAvgObj, i);
        if (pArrItemObj != NULL)
        {
            float fValue = XJSON_GetFloat(XJSON_GetObject(pArrItemObj, "value"));
            const char* pInter = XJSON_GetString(XJSON_GetObject(pArrItemObj, "interval"));
            if (pInter == NULL) continue;

            if (!strncmp(pInter, "1m", 2)) pCpuStats->nLoadAvg[0] = XFloatToU32(fValue);
            if (!strncmp(pInter, "5m", 2)) pCpuStats->nLoadAvg[1] = XFloatToU32(fValue);
            if (!strncmp(pInter, "15m", 3)) pCpuStats->nLoadAvg[2] = XFloatToU32(fValue);
        }
    }

    xjson_obj_t *pUsageObj = XJSON_GetObject(pCPUObj, "usage");
    if (pUsageObj == NULL)
    {
        xloge("Response does not contain CPU usage object in JSON");
        return XSTDERR;
    }

    xjson_obj_t *pProcObj = XJSON_GetObject(pCPUObj, "process");
    if (pProcObj == NULL)
    {
        xloge("Response does not contain CPU process object in JSON");
        return XSTDERR;
    }

    xjson_obj_t *pCoresObj = XJSON_GetObject(pUsageObj, "cores");
    if (pCoresObj == NULL)
    {
        xloge("Response does not contain CPU core object in JSON");
        return XSTDERR;
    }

    xjson_obj_t *pSumObj = XJSON_GetObject(pUsageObj, "sum");
    if (pSumObj == NULL)
    {
        xloge("Response does not contain CPU sum object in JSON");
        return XSTDERR;
    }

    float fKernelSpace =  XJSON_GetFloat(XJSON_GetObject(pProcObj, "kernelSpace"));
    float fUserSpace =  XJSON_GetFloat(XJSON_GetObject(pProcObj, "userSpace"));
    pCpuStats->usage.nKernelSpaceUsage = XFloatToU32(fKernelSpace);
    pCpuStats->usage.nUserSpaceUsage = XFloatToU32(fUserSpace);
    XTOP_ParseCoreObj(pSumObj, &pCpuStats->sum);

    nLength = XJSON_GetArrayLength(pCoresObj);
    XSYNC_ATOMIC_SET(&pStats->cpuStats.nCoreCount, nLength);

    for (i = 0; i < nLength; i++)
    {
        xjson_obj_t *pArrItemObj = XJSON_GetArrayItem(pCoresObj, i);
        if (pArrItemObj != NULL)
        {
            xcpu_info_t *pInfo = (xcpu_info_t*)malloc(sizeof(xcpu_info_t));
            if (pInfo == NULL)
            {
                xloge("Failed to allocate memory for CPU core object: %d", errno);
                return XSTDERR;
            }

            XTOP_ParseCoreObj(pArrItemObj, pInfo);

            if (XArray_AddData(&pCpuStats->cores, pInfo, 0) < 0)
            {
                xloge("Failed to store CPU core object: %d", errno);
                return XSTDERR;
            }
        }
    }

    xjson_obj_t *pMemoryObj = XJSON_GetObject(pJson->pRootObj, "memory");
    if (pMemoryObj == NULL)
    {
        xloge("Response does not contain memory object in JSON");
        return XSTDERR;
    }

    pMemInfo->nBuffers = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "memBuffered"));
    pMemInfo->nReclaimable = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "memReclaimable"));
    pMemInfo->nResidentMemory = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "memResident"));
    pMemInfo->nVirtualMemory = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "memVirtual"));
    pMemInfo->nMemoryCached = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "memCached"));
    pMemInfo->nMemoryShared = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "memShared"));
    pMemInfo->nMemoryAvail = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "memAvail"));
    pMemInfo->nMemoryTotal = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "memTotal"));
    pMemInfo->nMemoryFree = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "memFree"));
    pMemInfo->nSwapCached = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "swapCached"));
    pMemInfo->nSwapTotal = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "swapTotal"));
    pMemInfo->nSwapFree = XJSON_GetU64(XJSON_GetObject(pMemoryObj, "swapFree"));

    xjson_obj_t *pNetObj = XJSON_GetObject(pJson->pRootObj, "network");
    if (pNetObj == NULL)
    {
        xloge("Response does not contain network object in JSON");
        return XSTDERR;
    }

    nLength = XJSON_GetArrayLength(pNetObj);
    for (i = 0; i < nLength; i++)
    {
        xjson_obj_t *pArrItemObj = XJSON_GetArrayItem(pNetObj, i);
        if (pArrItemObj != NULL)
        {
            xnet_iface_t *pIfcObj = (xnet_iface_t*)malloc(sizeof(xnet_iface_t));
            if (pIfcObj == NULL)
            {
                xloge("Failed to allocate memory for network iface object: %d", errno);
                return XSTDERR;
            }

            memset(pIfcObj, 0, sizeof(xnet_iface_t));
            pIfcObj->nPacketsReceivedPerSec = XJSON_GetU64(XJSON_GetObject(pArrItemObj, "packetsReceivedPerSec"));
            pIfcObj->nBytesReceivedPerSec = XJSON_GetU64(XJSON_GetObject(pArrItemObj, "bytesReceivedPerSec"));
            pIfcObj->nPacketsSentPerSec = XJSON_GetU64(XJSON_GetObject(pArrItemObj, "packetsSentPerSec"));
            pIfcObj->nBytesSentPerSec = XJSON_GetU64(XJSON_GetObject(pArrItemObj, "bytesSentPerSec"));
            pIfcObj->nPacketsReceived = XJSON_GetU64(XJSON_GetObject(pArrItemObj, "packetsReceived"));
            pIfcObj->nBytesReceived = XJSON_GetU64(XJSON_GetObject(pArrItemObj, "bytesReceived"));
            pIfcObj->nPacketsSent = XJSON_GetU64(XJSON_GetObject(pArrItemObj, "packetsSent"));
            pIfcObj->nBytesSent = XJSON_GetU64(XJSON_GetObject(pArrItemObj, "bytesSent"));
            pIfcObj->nBandwidth = XJSON_GetU64(XJSON_GetObject(pArrItemObj, "bandwidth"));
            pIfcObj->bActive = XJSON_GetBool(XJSON_GetObject(pArrItemObj, "active"));
            pIfcObj->nType = XJSON_GetU32(XJSON_GetObject(pArrItemObj, "type"));

            const char *pName = XJSON_GetString(XJSON_GetObject(pArrItemObj, "name"));
            if (pName != NULL) xstrncpy(pIfcObj->sName, sizeof(pIfcObj->sName), pName);

            const char *pHwAddr = XJSON_GetString(XJSON_GetObject(pArrItemObj, "hwAddr"));
            if (pHwAddr != NULL) xstrncpy(pIfcObj->sHWAddr, sizeof(pIfcObj->sHWAddr), pHwAddr);

            const char *pIpAddr = XJSON_GetString(XJSON_GetObject(pArrItemObj, "ipAddr"));
            if (pIpAddr != NULL) xstrncpy(pIfcObj->sIPAddr, sizeof(pIfcObj->sIPAddr), pIpAddr);

            if (XArray_AddData(&pStats->netIfaces, pIfcObj, 0) < 0)
            {
                xloge("Failed to store network iface object: %d", errno);
                return XSTDERR;
            }
        }
    }

    return XSTDOK;
}

int XTOP_GetRemoteStats(xtop_ctx_t *pCtx, xmon_stats_t *pStats)
{
    const char *pVer = XUtils_VersionShort();
    xhttp_t handle;
    xlink_t link;

    if (XLink_Parse(&link, pCtx->sLink) < 0)
    {
        xloge("Failed to parse link: %s", pCtx->sLink);
        return XSTDERR;
    }

    if (XHTTP_InitRequest(&handle, XHTTP_GET, link.sUri, NULL) < 0)
    {
        xloge("Failed to initialize HTTP request: %d", errno);
        return XSTDERR;
    }

    if (XHTTP_AddHeader(&handle, "Host", "%s", link.sAddr) < 0 ||
        XHTTP_AddHeader(&handle, "User-Agent", "xutils/%s", pVer) < 0 ||
        XHTTP_AddHeader(&handle, "Accept", "application/json") < 0 ||
        XHTTP_AddHeader(&handle, "Connection", "keep-alive") < 0)
    {
        xloge("Failed to initialize HTTP request: %d", errno);
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    if ((xstrused(pCtx->sKey) && XHTTP_AddHeader(&handle, "X-API-KEY", "%s", pCtx->sKey) < 0) ||
        (xstrused(pCtx->sToken) && XHTTP_AddHeader(&handle, "Authorization", "Basic %s", pCtx->sToken) < 0))
    {
        xloge("Failed to setup authorization headers for request: %d", errno);
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    xhttp_status_t eStatus;
    if (XSock_Check(&pCtx->sock) != XSOCK_SUCCESS)
    {
        eStatus = XHTTP_Connect(&handle, &pCtx->sock, &link);
        if (eStatus != XHTTP_CONNECTED)
        {
            xloge("%s", XHTTP_GetStatusStr(eStatus));
            XSock_Close(&pCtx->sock);
            XHTTP_Clear(&handle);
            return XSTDERR;
        }
    }

    eStatus = XHTTP_Perform(&handle, &pCtx->sock, NULL, 0);
    if (eStatus != XHTTP_COMPLETE)
    {
        xloge("%s", XHTTP_GetStatusStr(eStatus));
        XSock_Close(&pCtx->sock);
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    if (handle.nStatusCode != 200)
    {
        xlogw("HTTP response: %d %s", handle.nStatusCode,
                    XHTTP_GetCodeStr(handle.nStatusCode));

        XSock_Close(&pCtx->sock);
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    const char *pBody = (const char *)XHTTP_GetBody(&handle);
    if (pBody == NULL)
    {
        xloge("HTTP response does not contain data");
        XSock_Close(&pCtx->sock);
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    xjson_t json;
    if (!XJSON_Parse(&json, NULL, pBody, handle.nContentLength))
    {
        char sError[256];
        XJSON_GetErrorStr(&json, sError, sizeof(sError));
        xloge("Failed to parse JSON: %s", sError);

        XSock_Close(&pCtx->sock);
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    int nStatus = XTOP_GetJSONStats(pStats, &json);

    XHTTP_Clear(&handle);
    XJSON_Destroy(&json);

    return nStatus;
}

int XTOP_PrintStatus(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    const char *pStr = XAPI_GetStatus(pCtx);
    int nFD = pData ? (int)pData->sock.nFD : XSTDERR;
    xuint_t nID = pData ? pData->nID : XSTDNON;

    if (pCtx->nStatus == XAPI_DESTROY)
        xlogn("%s", pStr);
    else if (pCtx->eCbType == XAPI_CB_STATUS)
        xlogi("%s: id(%u), fd(%d)", pStr, nID, nFD);
    else if (pCtx->eCbType == XAPI_CB_ERROR)
        xloge("%s: id(%u), fd(%d), errno(%d)", pStr, nID, nFD, errno);

    return XSTDOK;
}

int XTOP_HandleRequest(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xtop_ctx_t *pAppCtx = (xtop_ctx_t*)pData->pApi->pUserCtx;
    int nStatus = XAPI_AuthorizeHTTP(pData, pAppCtx->sToken, pAppCtx->sKey);
    if (nStatus <= 0) return nStatus;

    xmon_request_t *pRequest = (xmon_request_t*)pData->pSessionData;
    xhttp_t *pHttp = (xhttp_t*)pData->pPacket;
    *pRequest = XTOP_NONE;

    xlogi("Received request: id(%u), fd(%d), method(%s), uri(%s)",
        pData->nID, (int)pData->sock.nFD, XHTTP_GetMethodStr(pHttp->eMethod), pHttp->sUri);

    if (pHttp->eMethod != XHTTP_GET)
    {
        xlogw("Invalid or not supported HTTP method: id(%u), %s", pData->nID, XHTTP_GetMethodStr(pHttp->eMethod));
        return XAPI_RespondHTTP(pData, XTOP_NOTALLOWED, XAPI_NO_STATUS);
    }

    xarray_t *pArr = xstrsplit(pHttp->sUri, "/");
    if (pArr == NULL)
    {
        xlogw("Invalid request URL or API endpoint: id(%u), %s", pData->nID, pHttp->sUri);
        return XAPI_RespondHTTP(pData, XTOP_INVALID, XAPI_NO_STATUS);
    }

    char *pDirect = (char*)XArray_GetData(pArr, 0);
    char *pEntry = (char*)XArray_GetData(pArr, 1);

    if (pDirect != NULL && pEntry != NULL && !strncmp(pDirect, "api", 3))
    {
        if (!strncmp(pEntry, "all", 3)) *pRequest = XTOP_ALL;
        else if (!strncmp(pEntry, "cpu", 3)) *pRequest = XTOP_CPU;
        else if (!strncmp(pEntry, "memory", 6)) *pRequest = XTOP_MEMORY;
        else if (!strncmp(pEntry, "network", 7)) *pRequest = XTOP_NETWORK;
    }

    XArray_Destroy(pArr);

    if (*pRequest == XTOP_NONE)
    {
        xlogw("Requested API endpoint is not found: id(%u), uri(%s)", pData->nID, pHttp->sUri);
        return XAPI_RespondHTTP(pData, XTOP_NOTFOUND, XAPI_NO_STATUS);
    }

    if (!pHttp->nKeepAlive) XAPI_DeleteTimer(pData);
    else XAPI_AddTimer(pData, XTOP_REQUEST_TIMEOUT_MS);

    return XAPI_EnableEvent(pData, XPOLLOUT);
}

int XTOP_AppendMemoryJson(xmon_stats_t *pStats, xstring_t *pJsonStr)
{
    xmem_info_t memInfo;
    XMon_GetMemoryInfo(pStats, &memInfo);

    char sJsonBlock[XSTR_MIN];
    int nLen = snprintf(sJsonBlock, sizeof(sJsonBlock),
        "\"memory\": {"
            "\"memReclaimable\": %" PRIu64 ","
            "\"memBuffered\": %" PRIu64 ","
            "\"memResident\": %" PRIu64 ","
            "\"memVirtual\": %" PRIu64 ","
            "\"memCached\": %" PRIu64 ","
            "\"memShared\": %" PRIu64 ","
            "\"memAvail\": %" PRIu64 ","
            "\"memTotal\": %" PRIu64 ","
            "\"memFree\": %" PRIu64 ","
            "\"swapCached\": %" PRIu64 ","
            "\"swapTotal\": %" PRIu64 ","
            "\"swapFree\": %" PRIu64 ""
        "}",
        memInfo.nReclaimable,
        memInfo.nBuffers,
        memInfo.nResidentMemory,
        memInfo.nVirtualMemory,
        memInfo.nMemoryCached,
        memInfo.nMemoryShared,
        memInfo.nMemoryAvail,
        memInfo.nMemoryTotal,
        memInfo.nMemoryFree,
        memInfo.nSwapCached,
        memInfo.nSwapTotal,
        memInfo.nSwapFree);

    return XString_Add(pJsonStr, sJsonBlock, nLen);
}

int XTOP_AppendNetworkJson(xmon_stats_t *pStats, xstring_t *pJsonStr)
{
    xarray_t netIfaces;
    if (XMon_GetNetworkStats(pStats, &netIfaces) > 0)
    {
        if (XString_Append(pJsonStr, "\"network\": [") < 0)
        {
            XArray_Destroy(&netIfaces);
            return XSTDERR;
        }

        size_t i, nUsed = XArray_Used(&netIfaces);
        for (i = 0; i < nUsed; i++)
        {
            xnet_iface_t *pIface = (xnet_iface_t*)XArray_GetData(&netIfaces, i);
            if (pIface == NULL) continue;

            char sJsonBlock[XSTR_MIN];
            int nLen = snprintf(sJsonBlock, sizeof(sJsonBlock),
                "{"
                    "\"name\": \"%s\","
                    "\"type\": %d,"
                    "\"ipAddr\": \"%s\","
                    "\"hwAddr\": \"%s\","
                    "\"bandwidth\": %" PRId64 ","
                    "\"bytesSent\": %" PRId64 ","
                    "\"packetsSent\": %" PRId64 ","
                    "\"bytesReceived\": %" PRId64 ","
                    "\"packetsReceived\": %" PRId64 ","
                    "\"bytesSentPerSec\": %" PRIu64 ","
                    "\"packetsSentPerSec\": %" PRIu64 ","
                    "\"bytesReceivedPerSec\": %" PRIu64 ","
                    "\"packetsReceivedPerSec\": %" PRIu64 ","
                    "\"active\": %s"
                "}",
                pIface->sName,
                pIface->nType,
                pIface->sIPAddr,
                pIface->sHWAddr,
                pIface->nBandwidth,
                pIface->nBytesSent,
                pIface->nPacketsSent,
                pIface->nBytesReceived,
                pIface->nPacketsReceived,
                pIface->nBytesSentPerSec,
                pIface->nPacketsSentPerSec,
                pIface->nBytesReceivedPerSec,
                pIface->nPacketsReceivedPerSec,
                pIface->bActive ? "true" : "false");

            XString_Add(pJsonStr, sJsonBlock, nLen);

            if (pJsonStr->nStatus < 0 ||
                (i + 1 < nUsed &&
                XString_Append(pJsonStr, ",") < 0))
            {
                XArray_Destroy(&netIfaces);
                return XSTDERR;
            }
        }

        XArray_Destroy(&netIfaces);
        XString_Append(pJsonStr, "]");
        return pJsonStr->nStatus;
    }

    return XSTDERR;
}

int XTOP_AppendCoreJson(xcpu_info_t *pCpu, xstring_t *pJsonStr)
{
    return XString_Append(pJsonStr,
        "{"
            "\"id\": %d,"
            "\"active\": %d,"
            "\"softInterrupts\": %u,"
            "\"hardInterrupts\": %u,"
            "\"userSpaceNiced\": %u,"
            "\"kernelSpace\": %u,"
            "\"userSpace\": %u,"
            "\"idleTime\": %u,"
            "\"ioWait\": %u,"
            "\"stealTime\": %u,"
            "\"guestTime\": %u,"
            "\"guestNiced\": %u"
        "}",
        pCpu->nID,
        pCpu->nActive,
        pCpu->nSoftInterrupts,
        pCpu->nHardInterrupts,
        pCpu->nUserSpaceNiced,
        pCpu->nKernelSpace,
        pCpu->nUserSpace,
        pCpu->nIdleTime,
        pCpu->nIOWait,
        pCpu->nStealTime,
        pCpu->nGuestTime,
        pCpu->nGuestNiced);
}

int XTOP_AppendCPUJson(xmon_stats_t *pStats, xstring_t *pJsonStr)
{
    xcpu_stats_t cpuStats;
    if (XMon_GetCPUStats(pStats, &cpuStats) > 0)
    {
        XString_Append(pJsonStr,
            "\"cpu\":{"
                "\"loadAverage\": ["
                    "{"
                        "\"interval\": \"1m\","
                        "\"value\": %f"
                    "},"
                    "{"
                        "\"interval\": \"5m\","
                        "\"value\": %f"
                    "},"
                    "{"
                        "\"interval\": \"15m\","
                        "\"value\": %f"
                    "}"
                "]",
            XU32ToFloat(cpuStats.nLoadAvg[0]),
            XU32ToFloat(cpuStats.nLoadAvg[1]),
            XU32ToFloat(cpuStats.nLoadAvg[2]));

        if (pJsonStr->nStatus < 0)
        {
            XArray_Destroy(&cpuStats.cores);
            return XSTDERR;
        }

        XString_Append(pJsonStr,
            ",\"process\":"
                "{"
                    "\"kernelSpace\": %f,"
                    "\"userSpace\": %f"
                "},"
            "\"usage\":{"
                "\"sum\":",
            XU32ToFloat(cpuStats.usage.nUserSpaceUsage),
            XU32ToFloat(cpuStats.usage.nKernelSpaceUsage));

        if (pJsonStr->nStatus < 0 ||
            XTOP_AppendCoreJson(&cpuStats.sum, pJsonStr) < 0 ||
            XString_Append(pJsonStr, ",\"cores\":[") < 0)
        {
            XArray_Destroy(&cpuStats.cores);
            return XSTDERR;
        }

        size_t i, nUsed = XArray_Used(&cpuStats.cores);
        for (i = 0; i < nUsed; i++)
        {
            xcpu_info_t *pCore = (xcpu_info_t*)XArray_GetData(&cpuStats.cores, i);
            if (pCore == NULL) continue;

            if (XTOP_AppendCoreJson(pCore, pJsonStr) < 0 ||
                (i + 1 < nUsed && XString_Append(pJsonStr, ",") < 0))
            {
                XArray_Destroy(&cpuStats.cores);
                return XSTDERR;
            }
        }

        XArray_Destroy(&cpuStats.cores);
        XString_Append(pJsonStr, "]}}");
        return pJsonStr->nStatus;
    }

    return XSTDERR;
}

int XTOP_AssembleBody(xapi_data_t *pData, xstring_t *pJsonStr)
{
    xtop_ctx_t *pCtx = (xtop_ctx_t*)pData->pApi->pUserCtx;
    xmon_stats_t *pStats = (xmon_stats_t*)pCtx->pStats;
    xmon_request_t eRequest = *(xmon_request_t*)pData->pSessionData;

    if (XString_Append(pJsonStr, "{") < 0)
    {
        xloge("Failed to initialize JSON string: %d", errno);
        return XSTDERR;
    }

    xbool_t bNeedComma = XFALSE;

    if (eRequest == XTOP_ALL || eRequest == XTOP_CPU)
    {
        if (XTOP_AppendCPUJson(pStats, pJsonStr) < 0)
        {
            xloge("Failed to serialize CPU JSON string: %d", errno);
            return XSTDERR;
        }

        bNeedComma = XTRUE;
    }

    if (eRequest == XTOP_ALL || eRequest == XTOP_MEMORY)
    {
        if (bNeedComma && XString_Append(pJsonStr, ",") < 0)
        {
            xloge("Failed to assemble JSON string: %d", errno);
            return XSTDERR;
        }

        if (XTOP_AppendMemoryJson(pStats, pJsonStr) < 0)
        {
            xloge("Failed to serialize memory JSON string: %d", errno);
            return XSTDERR;
        }

        bNeedComma = XTRUE;
    }

    if (eRequest == XTOP_ALL || eRequest == XTOP_NETWORK)
    {
        if (bNeedComma && XString_Append(pJsonStr, ",") < 0)
        {
            xloge("Failed to assemble JSON string: %d", errno);
            return XSTDERR;
        }

        if (XTOP_AppendNetworkJson(pStats, pJsonStr) < 0)
        {
            xloge("Failed to serialize memory JSON string: %d", errno);
            return XSTDERR;
        }
    }

    if (XString_Append(pJsonStr, "}") < 0)
    {
        xloge("Failed to serialize JSON response: %d", errno);
        return XSTDERR;
    }

    return XSTDOK;
}

int XTOP_SendResponse(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    xhttp_t handle;
    if (XHTTP_InitResponse(&handle, 200, NULL) <= 0)
    {
        xloge("Failed initialize HTTP response: %d", errno);
        return XSTDERR;
    }

    xstring_t content;
    if (XString_Init(&content, XSTR_MID, XFALSE) < 0)
    {
        xloge("Failed to response content buffer: %d", errno);
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    if (XTOP_AssembleBody(pData, &content) < 0)
    {
        XString_Clear(&content);
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    if (XHTTP_AddHeader(&handle, "Content-Type", "application/json") < 0 ||
        XHTTP_AddHeader(&handle, "Server", "xutils/%s", XUtils_VersionShort()) < 0 ||
        XHTTP_Assemble(&handle, (const uint8_t*)content.pData, content.nLength) == NULL)
    {
        xloge("Failed to assemble HTTP response: %s", XSTRERR);
        XString_Clear(&content);
        XHTTP_Clear(&handle);
        return XSTDERR;
    }

    xlogi("Sending response: id(%u), fd(%d), status(%d), length(%zu)",
        pData->nID, (int)pData->sock.nFD, handle.nStatusCode, handle.rawData.nUsed);

    XByteBuffer_AddBuff(&pData->txBuffer, &handle.rawData);
    XString_Clear(&content);
    XHTTP_Clear(&handle);

    return XAPI_EnableEvent(pData, XPOLLOUT);
}

int XTOP_InitSessionData(xapi_data_t *pData)
{
    xmon_request_t *pRequest = (xmon_request_t *)malloc(sizeof(xmon_request_t));
    if (pRequest == NULL)
    {
        xloge("Failed to allocate memory for session data: %d", errno);
        return XSTDERR;
    }

    *pRequest = XTOP_INVALID;
    pData->pSessionData = pRequest;

    // Add inactivity timeout for the session
    XAPI_AddTimer(pData, XTOP_REQUEST_TIMEOUT_MS);

    xlogn("Accepted connection: id(%u), fd(%d), ip(%s)", pData->nID, (int)pData->sock.nFD, pData->sAddr);
    return XAPI_SetEvents(pData, XPOLLIN);
}

int XTOP_ClearSessionData(xapi_data_t *pData)
{
    xlogn("Connection closed: id(%u), fd(%d), ip(%s)", pData->nID, (int)pData->sock.nFD, pData->sAddr);
    free(pData->pSessionData);
    pData->pSessionData = NULL;
    return XSTDERR;
}

int XTOP_ServiceCb(xapi_ctx_t *pCtx, xapi_data_t *pData)
{
    switch (pCtx->eCbType)
    {
        case XAPI_CB_ERROR:
        case XAPI_CB_STATUS:
            return XTOP_PrintStatus(pCtx, pData);
        case XAPI_CB_READ:
            return XTOP_HandleRequest(pCtx, pData);
        case XAPI_CB_WRITE:
            return XTOP_SendResponse(pCtx, pData);
        case XAPI_CB_ACCEPTED:
            return XTOP_InitSessionData(pData);
        case XAPI_CB_CLOSED:
            return XTOP_ClearSessionData(pData);
        case XAPI_CB_TIMEOUT:
            xlogi("Timeout event for the session: id(%u), fd(%d)", pData->nID, (int)pData->sock.nFD);
            return XSTDERR;
        case XAPI_CB_COMPLETE:
            xlogi("Response sent to the client: id(%u), fd(%d)", pData->nID, (int)pData->sock.nFD);
            return pData->pTimer ? XSTDOK : XSTDERR;
        case XAPI_CB_INTERRUPT:
            if (g_nInterrupted) return XSTDERR;
            break;
        default:
            break;
    }

    return XSTDOK;
}

int XTOP_ServerMode(xtop_ctx_t *pCtx, xmon_stats_t *pStats)
{
    xapi_t api;
    XAPI_Init(&api, XTOP_ServiceCb, pCtx);

    pCtx->pStats = pStats;
    xevent_status_t status;

    xapi_endpoint_t endpt;
    XAPI_InitEndpoint(&endpt);

    endpt.eType = XAPI_HTTP;
    endpt.eRole = XAPI_SERVER;
    endpt.pAddr = pCtx->sAddr;
    endpt.nPort = pCtx->nPort;

    if (XAPI_AddEndpoint(&api, &endpt) < 0)
    {
        XAPI_Destroy(&api);
        return XSTDERR;
    }

    xlogn("Socket started listen to port: %d", pCtx->nPort);

    do status = XAPI_Service(&api, 100);
    while (status == XEVENTS_SUCCESS);

    XAPI_Destroy(&api);
    return XSTDNON;
}

#ifdef __linux__
static void XTOP_ProcessSTDIN(xtop_ctx_t *pCtx)
{
    char c;
    xbool_t bReset = XFALSE;
    xbool_t bRedraw = XFALSE;

    while (XCLI_GetChar(&c, XTRUE) == XSTDOK)
    {
        xbool_t bValid = XTRUE;

        if (c == 'c')
        {
            pCtx->bShowAllCPUs = !pCtx->bShowAllCPUs;
            bReset = XTRUE;
        }
        else if (c == 'i')
        {
            pCtx->bShowAllIfaces = !pCtx->bShowAllIfaces;
            bReset = XTRUE;
        }
        else if (c == 'l')
        {
            pCtx->bLineByLine = !pCtx->bLineByLine;
            bReset = XTRUE;
        }
        else if (c == 'x')
        {
            pCtx->bCoreCountManualSet = XFALSE;
            pCtx->bDisplayHeader = !pCtx->bDisplayHeader;
            pCtx->nCoreCount = XTOP_CORE_COUNT_RESET;
        }
        else if (c == 'a')
        {
            pCtx->bShowCPUSum = !pCtx->bShowCPUSum;
        }
        else if (c == 's')
        {
            if (++pCtx->nSort > XTOP_SORT_MAX)
                pCtx->nSort = XTOP_SORT_DISABLE;

            bReset = XTRUE;
        }
        else if (c == 'h')
        {
            pCtx->bDisplayHelp = !pCtx->bDisplayHelp;
            bRedraw = XTRUE;
        }
        else if (c == 'q')
        {
            pCtx->bQuit = XTRUE;
        }
        else if (c == 'r')
        {
            bReset = XTRUE;
            pCtx->nSort = XTOP_SORT_LEN;
            pCtx->nCPUExtraMin = XTOP_CPU_EXTRA_MIN;
        }
        else if (c == '+')
        {
            pCtx->bCoreCountManualSet = XTRUE;
            pCtx->nCoreCount++;

            if (pCtx->nRealCores > 0 &&
                pCtx->nCoreCount > pCtx->nRealCores)
                pCtx->nCoreCount = pCtx->nRealCores;
        }
        else if (c == '-')
        {
            pCtx->bCoreCountManualSet = XTRUE;
            pCtx->nCoreCount--;

            if (pCtx->nCoreCount < 0)
                pCtx->nCoreCount = 0;
        }
        else
        {
            bValid = XFALSE;
        }

        if (bValid)
        {
            bRedraw = XTRUE;
        }
    }

    if (bReset)
    {
        pCtx->bCoreCountManualSet = XFALSE;
        pCtx->nActiveIfaces = XTOP_ACTIVE_IFACES_RESET;
        pCtx->nCoreCount = XTOP_CORE_COUNT_RESET;
    }

    // Redraw help message only if its enabled and something is changed
    if (bRedraw) pCtx->bRedrawHelp = pCtx->bDisplayHelp;
}
#endif

int main(int argc, char *argv[])
{
    uint16_t nFlags = XLOG_NOTE | XLOG_WARN | XLOG_ERROR | XLOG_FATAL;
    xlog_init("xtop", nFlags, XFALSE);

    xmon_stats_t stats;
    xtop_ctx_t ctx;

    if (!XTOP_ParseArgs(&ctx, argc, argv))
    {
        XTOP_DisplayUsage(argv[0]);
        return XSTDERR;
    }

    if (ctx.bDaemon && XUtils_Daemonize(XTRUE, XTRUE) < 0)
    {
        xloge("Failed to run server as daemon: %d", errno);
        return XSTDERR;
    }

    if (XMon_InitStats(&stats) < 0)
    {
        xloge("Failed to initialize stats: %d", errno);
        return XSTDERR;
    }

    xlog_screen(!ctx.bDaemon);
    xlog_timing(XLOG_TIME);
    xlog_indent(XTRUE);

    int nSignals[2] = { SIGTERM, SIGINT };
    XSig_Register(nSignals, 2, XTOP_SignalCallback);

    if (!ctx.bClient)
    {
        int nStatus = XMon_StartMonitoring(&stats, ctx.nIntervalU, ctx.nPID);
        if (nStatus < 0)
        {
            xloge("Process not found: %d", ctx.nPID);
            XMon_DestroyStats(&stats);
            return XSTDERR;
        }
        else if (!nStatus)
        {
            xloge("Failed to start monitoring thread: %d", errno);
            XMon_DestroyStats(&stats);
            return XSTDERR;
        }

        XMon_WaitLoad(&stats, 1000);
    }

    if (ctx.bServer)
    {
        int nStatus = XTOP_ServerMode(&ctx, &stats);
        XMon_StopMonitoring(&stats, 1000);
        XMon_DestroyStats(&stats);

        usleep(10000); // Make valgrind happy
        return nStatus;
    }

    xcli_win_t win;
    XCLIWin_Init(&win, !ctx.bClear);

    xcli_bar_t bar;
    XProgBar_GetDefaults(&bar);
    bar.bInPercent = XTRUE;
    bar.bInSuffix = XTRUE;
    bar.cLoader = '|';

#ifdef __linux__
    struct termios cliAttrs;
    if (!ctx.bServer && !ctx.bDaemon)
        XCLI_SetInputMode(&cliAttrs);
#endif

    xbool_t bFirst = XTRUE;
    while (!g_nInterrupted)
    {
#ifdef __linux__
        if (!ctx.bServer && !ctx.bDaemon)
        {
            XTOP_ProcessSTDIN(&ctx);

            if (ctx.bQuit)
            {
                printf("\n");
                break;
            }

            if (ctx.bDisplayHelp)
            {
                if (ctx.bRedrawHelp)
                {
                    XCLIWin_ClearScreen(XFALSE);
                    XTOP_DisplayHelp(&ctx);
                    ctx.bRedrawHelp = XFALSE;
                }

                xusleep(ctx.nIntervalU);
                continue;
            }

            if (ctx.bLineByLine) win.eType = XCLI_LINE_BY_LINE;
            else win.eType = XCLI_RENDER_FRAME;
        }
#endif

        if (ctx.bClient)
        {
            if (XTOP_GetRemoteStats(&ctx, &stats) < 0)
            {
                xusleep(ctx.nIntervalU);
                continue;
            }
        }

        if (ctx.bDisplayHeader) XCLIWin_AddAligned(&win, "[XTOP]", XSTR_BACK_BLUE, XCLI_CENTER);
        XCLIWin_AddEmptyLine(&win);

        xcpu_stats_t cpuStats;
        if (XMon_GetCPUStats(&stats, &cpuStats) > 0)
        {
            xmem_info_t memInfo;
            XMon_GetMemoryInfo(&stats, &memInfo);

            XTOP_AddCPULoadBar(&win, &bar, &cpuStats);
            XTOP_AddOverallBar(&win, &bar, &memInfo, &cpuStats);

            if (ctx.nCPUExtraMin > 0 && (ctx.nCoreCount || ctx.bShowCPUSum))
            {
                XCLIWin_AddEmptyLine(&win);
                XTOP_AddCPUExtra(&ctx, &win, &bar, &memInfo, &cpuStats);
            }

            XCLIWin_AddEmptyLine(&win);
            XArray_Destroy(&cpuStats.cores);
        }

        xarray_t netIfaces;
        if (XMon_GetNetworkStats(&stats, &netIfaces) > 0)
        {
            XTOP_AddNetworkInfo(&ctx, &win, &netIfaces);
            XArray_Destroy(&netIfaces);
        }

        if (bFirst)
        {
            XCLIWin_ClearScreen(XFALSE);
            bFirst = XFALSE;
        }

        XCLIWin_Flush(&win);
        xusleep(ctx.nIntervalU);
    }

    if (!ctx.bClient)
        XMon_StopMonitoring(&stats, 1000);

#ifdef __linux__
    if (!ctx.bServer && !ctx.bDaemon)
        XCLI_RestoreAttributes(&cliAttrs);
#endif

    XMon_DestroyStats(&stats);
    XCLIWin_Destroy(&win);
    XSock_Close(&ctx.sock);

    usleep(10000); // Make valgrind happy
    return 0;
}
