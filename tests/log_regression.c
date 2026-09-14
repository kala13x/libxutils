/* libxutils: log filtering, callback lifetime and dynamic message capacity. */
#include "test.h"

typedef struct xtest_log_ {
    int nCalls;
    xlog_flag_t eLast;
    size_t nLength;
    char sLast[8192];
} xtest_log_t;

static int XTest_Callback(const char *pData, size_t nLength, xlog_flag_t eFlag, void *pContext)
{
    xtest_log_t *pTest = (xtest_log_t*)pContext;
    pTest->nCalls++;
    pTest->eLast = eFlag;
    pTest->nLength = nLength;
    size_t nCopy = XSTD_MIN(nLength, sizeof(pTest->sLast) - 1);
    memcpy(pTest->sLast, pData, nCopy);
    pTest->sLast[nCopy] = 0;
    return XSTDNON;
}

static int XTest_filtering(void)
{
    xtest_log_t test = {0};
    xlog_init("regression", XLOG_ERROR, XTRUE);
    xlog_screen(XFALSE);
    xlog_callback(XTest_Callback, &test);
    xlogi("filtered");
    CHECK(test.nCalls == 0, "Disabled severity never invokes the callback");
    xloge("visible %d", 42);
    CHECK(test.nCalls == 1 && test.eLast == XLOG_ERROR && strstr(test.sLast, "visible 42"),
        "Enabled severity formats one callback");
    xlog_enable(XLOG_INFO);
    xlogi("enabled");
    CHECK(test.nCalls == 2 && test.eLast == XLOG_INFO, "Enabling a severity takes effect");
    xlog_disable(XLOG_INFO);
    xlogi("filtered again");
    CHECK(test.nCalls == 2, "Disabling a severity takes effect");
    xlog_callback(NULL, NULL);
    xlog_destroy();
    CHECK(!xlog_is_init(), "Destroy clears initialization state");
    return 0;
}

static int XTest_large_message(void)
{
    xtest_log_t test = {0};
    char message[4097];
    memset(message, 'x', sizeof(message) - 1);
    message[sizeof(message) - 1] = 0;
    xlog_init("regression", XLOG_INFO, XFALSE);
    xlog_screen(XFALSE);
    xlog_useheap(XTRUE);
    xlog_callback(XTest_Callback, &test);
    xlogi("begin:%s:end", message);
    CHECK(test.nCalls == 1 && strstr(test.sLast, ":end"), "Dynamic formatting retains the end of a large message");
    CHECK(test.nLength >= strlen(message) + 10, "Callback reports the complete formatted length");
    xlog_callback(NULL, NULL);
    xlog_destroy();
    return 0;
}

XTEST_MAIN(XTEST_CASE(filtering), XTEST_CASE(large_message))
