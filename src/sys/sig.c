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
    int nSignals[4] = { SIGINT, SIGILL, SIGSEGV, SIGTERM };
    return XSig_Register(nSignals, 4, XSig_Callback);
}
