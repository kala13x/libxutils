/*!
 *  @file libxutils/src/sys/sig.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Exit signal handling and backtrace.
 */

#include "xstd.h"
#include "sig.h"

#ifdef __linux__
#include <execinfo.h>
#endif

#ifndef XUTILS_BACKTRACE_SIZE
#define XUTILS_BACKTRACE_SIZE 10
#endif /* XUTILS_BACKTRACE_SIZE */

void XUtils_Backtrace(void)
{
#ifdef __linux__
    void *pAddrs[XUTILS_BACKTRACE_SIZE];
    char **pSymbol = NULL;
    int i = 0;

    int nCount = backtrace(pAddrs, XARR_SIZE(pAddrs));
    if (nCount <= 0)
    {
        xlogw("No backtrace available");
        return;
    }

    xlogd("Backtrace with %d functions", nCount);

    pSymbol = backtrace_symbols(pAddrs, nCount);
    if (pSymbol == NULL)
    {
        xloge("Can not get symbols for backtrace");
        return;
    }

    for (i = 0; i < nCount; ++i)
        xlogi("Function %d: %s", i, pSymbol[i]);

    if (pSymbol) free(pSymbol);
#endif
}

void XUtils_ErrExit(const char * pFmt, ...)
{
    if (pFmt != NULL)
    {
        char sMsg[XMSG_MID];
        va_list args;

        va_start(args, pFmt);
        vsnprintf(sMsg, sizeof(sMsg), pFmt, args);
        va_end(args);

        xloge("%s", sMsg);
    }

    exit(EXIT_FAILURE);
}

int XUtils_Daemonize(int bNoChdir, int bNoClose)
{
#ifdef __linux__
    return daemon(bNoChdir, bNoClose);
#elif _WIN32
    if (CreateProcess(NULL, NULL, NULL, NULL, FALSE,
        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
        NULL, NULL, NULL, NULL) == 0) return -1;

    if (!bNoChdir)
    {
        SetCurrentDirectory("C:\\");
    }

    if (!bNoClose)
    {
        CloseHandle(GetStdHandle(STD_INPUT_HANDLE));
        CloseHandle(GetStdHandle(STD_OUTPUT_HANDLE));
        CloseHandle(GetStdHandle(STD_ERROR_HANDLE));
    }
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) exit(0); // parent exits

    if (setsid() < 0) return -1;

    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) exit(0);

    umask(0);
    if (!bNoChdir) chdir("/");

    if (!bNoClose)
    {
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
    }
#endif

    return 0;
}

void XSig_Callback(int nSig)
{
    /* Handle signals */
    switch(nSig)
    {
        case SIGSEGV:
        case SIGILL:
#ifdef __linux__
        case SIGBUS:
#endif
            XUtils_Backtrace();
            break;
        case SIGINT:
        case SIGTERM:
            xlogi("Received interrupt/termination signal");
            break;
        default:
            break;
    }

    exit(EXIT_FAILURE);
}


int XSig_Register(int *pSignals, size_t nCount, xsig_cb_t callback)
{
    size_t i = 0;

#ifdef _WIN32
    for (i = 0; i < nCount; i++)
        signal(pSignals[i], callback);
#else
    struct sigaction sact;
    sigemptyset(&sact.sa_mask);
    sact.sa_flags = 0;
    sact.sa_handler = callback;

    for (i = 0; i < nCount; i++)
        if (sigaction(pSignals[i], &sact, NULL))
            return pSignals[i];
#endif

    return 0;
}

int XSig_RegExitSigs(void)
{
    int nSignals[5] = { SIGINT, SIGILL, SIGSEGV, SIGTERM };
    size_t nCount = 4;

#ifdef __linux__
    nSignals[4] = SIGBUS;
    nCount++;
#endif

    return XSig_Register(nSignals, nCount, XSig_Callback);
}
