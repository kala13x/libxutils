/* libxutils regression helpers. Tests run without the DirectGate application. */
#ifndef XUTILS_TEST_H
#define XUTILS_TEST_H

#include "xstd.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c, msg) \
    do \
    { \
        if (!(c)) \
        { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, msg); \
            return 1; \
        } \
    } while (0)
#define XTEST_CASE(name) {#name, XTest_##name}
#define XTEST_MAIN(...) \
    int main(int argc, char **argv) \
    { \
        const xtest_case_t cases[] = {__VA_ARGS__}; \
        return XTest_Run(argc, argv, cases, sizeof(cases) / sizeof(*cases)); \
    }

typedef struct xtest_case_ {
    const char *pName;
    int (*pRun)(void);
} xtest_case_t;

static int XTest_Run(int argc, char **argv, const xtest_case_t *pCases, size_t nCount)
{
    xlog_setfl(XLOG_NONE);
    size_t nRun = 0;
    for (size_t i = 0; i < nCount; i++)
    {
        if (argc > 1 && strcmp(argv[1], pCases[i].pName) != 0) continue;
        int nStatus = pCases[i].pRun();
        if (nStatus != 0) return nStatus;
        printf("%s: OK\n", pCases[i].pName);
        nRun++;
    }
    if (nRun == 0) fprintf(stderr, "Unknown regression case\n");
    return nRun > 0 ? 0 : 1;
}

#endif
