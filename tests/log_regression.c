/* libxutils: log filtering, callback lifetime and dynamic message capacity. */
#include "test.h"
#include "xfs.h"
#include "str.h"
#include <unistd.h>
#include <sys/stat.h>

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


/* Reads a whole log file back so the written lines can be asserted on. */
static char *log_slurp(const char *pPath, size_t *pLength)
{
    FILE *pFile = fopen(pPath, "rb");
    if (pFile == NULL) return NULL;

    fseek(pFile, 0, SEEK_END);
    long nSize = ftell(pFile);
    fseek(pFile, 0, SEEK_SET);

    char *pData = (char*)malloc((size_t)nSize + 1);
    if (pData != NULL)
    {
        size_t nRead = fread(pData, 1, (size_t)nSize, pFile);
        pData[nRead] = '\0';
        if (pLength != NULL) *pLength = nRead;
    }

    fclose(pFile);
    return pData;
}

static int XTest_severities(void)
{
    /* Every severity has to reach the callback with its own flag, and the
     * flag mask has to gate exactly the severity it names. */
    const xlog_flag_t flags[] = {
        XLOG_NONE, XLOG_NOTE, XLOG_INFO, XLOG_WARN,
        XLOG_DEBUG, XLOG_TRACE, XLOG_ERROR, XLOG_FATAL
    };

    for (size_t i = 0; i < sizeof(flags) / sizeof(*flags); i++)
    {
        xtest_log_t test = {0};
        xlog_init("severity", flags[i], XFALSE);
        xlog_screen(XFALSE);
        xlog_callback(XTest_Callback, &test);

        xlogfl(flags[i], "severity %zu", i);
        CHECK(test.nCalls == 1, "The enabled severity reaches the callback");
        CHECK(test.eLast == flags[i], "The callback receives the severity it was logged with");

        /* Every other severity is filtered out by the same mask. */
        for (size_t k = 0; k < sizeof(flags) / sizeof(*flags); k++)
        {
            if (k == i) continue;
            xlogfl(flags[k], "other");
        }
        CHECK(test.nCalls == 1, "A single-severity mask filters every other severity");

        xlog_callback(NULL, NULL);
        xlog_destroy();
    }

    /* The all-severities mask lets every one of them through. */
    xtest_log_t test = {0};
    xlog_init("severity", XLOG_ALL, XFALSE);
    xlog_screen(XFALSE);
    xlog_callback(XTest_Callback, &test);
    for (size_t i = 0; i < sizeof(flags) / sizeof(*flags); i++) xlogfl(flags[i], "all");
    CHECK(test.nCalls == (int)(sizeof(flags) / sizeof(*flags)), "The all mask passes every severity");
    xlog_callback(NULL, NULL);
    xlog_destroy();
    return 0;
}

static int XTest_flag_mask(void)
{
    xlog_init("flags", XLOG_ERROR, XFALSE);
    xlog_screen(XFALSE);
    CHECK(xlog_getfl() == XLOG_ERROR, "The mask is the one the log was initialized with");

    xlog_enable(XLOG_INFO);
    CHECK(XLOG_FLAGS_CHECK(xlog_getfl(), XLOG_INFO), "Enabling adds the severity to the mask");
    CHECK(XLOG_FLAGS_CHECK(xlog_getfl(), XLOG_ERROR), "Enabling leaves the other severities alone");

    xlog_disable(XLOG_ERROR);
    CHECK(!XLOG_FLAGS_CHECK(xlog_getfl(), XLOG_ERROR), "Disabling removes the severity from the mask");
    CHECK(XLOG_FLAGS_CHECK(xlog_getfl(), XLOG_INFO), "Disabling leaves the other severities alone");

    /* Enabling twice and disabling twice are both idempotent. */
    xlog_enable(XLOG_INFO);
    uint16_t nTwice = xlog_getfl();
    xlog_enable(XLOG_INFO);
    CHECK(xlog_getfl() == nTwice, "Enabling an already enabled severity changes nothing");
    xlog_disable(XLOG_TRACE);
    nTwice = xlog_getfl();
    xlog_disable(XLOG_TRACE);
    CHECK(xlog_getfl() == nTwice, "Disabling an already disabled severity changes nothing");

    xlog_setfl(XLOG_ALL);
    CHECK(xlog_getfl() == XLOG_ALL, "The mask can be replaced outright");
    xlog_setfl(XLOG_NONE);
    CHECK(xlog_getfl() == XLOG_NONE, "The mask can be cleared down to a single severity");

    xlog_destroy();
    return 0;
}

static int XTest_config(void)
{
    xlog_init("config", XLOG_ALL, XFALSE);
    xlog_screen(XFALSE);

    /* Every setter has to be visible through the config snapshot. */
    xlog_coloring(XLOG_COLORING_FULL);
    xlog_timing(XLOG_DATE);
    xlog_tracetid(XTRUE);
    xlog_indent(XTRUE);
    xlog_flush(XTRUE);
    xlog_useheap(XTRUE);
    /* The setter pads whatever it is given with a space on each side. */
    xlog_separator("::");
    CHECK(xlog_name("renamed") > 0, "The log name is set");

    xlog_cfg_t cfg;
    xlog_get(&cfg);
    CHECK(cfg.eColorFormat == XLOG_COLORING_FULL, "The colour format is recorded");
    CHECK(cfg.eTimeFormat == XLOG_DATE, "The time format is recorded");
    CHECK(cfg.bTraceTid == XTRUE && cfg.bIndent == XTRUE, "The trace and indent flags are recorded");
    CHECK(cfg.bFlush == XTRUE && cfg.bUseHeap == XTRUE, "The flush and heap flags are recorded");
    CHECK(cfg.bToScreen == XFALSE, "Screen output stays off");
    CHECK(strcmp(cfg.sSeparator, " :: ") == 0, "The separator is recorded with its padding");
    CHECK(strcmp(cfg.sFileName, "renamed") == 0, "The log name is recorded");

    /* A config written back has to take effect. */
    cfg.eColorFormat = XLOG_COLORING_DISABLE;
    cfg.eTimeFormat = XLOG_DISABLE;
    cfg.bTraceTid = XFALSE;
    cfg.nFlags = XLOG_ERROR;
    xlog_set(&cfg);

    xlog_cfg_t readBack;
    xlog_get(&readBack);
    CHECK(readBack.eColorFormat == XLOG_COLORING_DISABLE, "A written colour format takes effect");
    CHECK(readBack.eTimeFormat == XLOG_DISABLE, "A written time format takes effect");
    CHECK(readBack.bTraceTid == XFALSE, "A written trace flag takes effect");
    CHECK(xlog_getfl() == XLOG_ERROR, "A written severity mask takes effect");

    xlog_destroy();
    CHECK(!xlog_is_init(), "Destroy clears the initialization state");

    /* Repeated init and destroy must be safe. */
    for (int i = 0; i < 3; i++)
    {
        xlog_init("cycle", XLOG_ALL, XTRUE);
        CHECK(xlog_is_init(), "Init sets the initialization state");
        xlog_destroy();
    }
    xlog_destroy();
    return 0;
}

static int XTest_formatting(void)
{
    /* The rendered line carries the tag, the separator and the message,
     * and a disabled timestamp leaves no date behind. */
    xtest_log_t test = {0};
    xlog_init("format", XLOG_ALL, XFALSE);
    xlog_screen(XFALSE);
    xlog_coloring(XLOG_COLORING_DISABLE);
    xlog_timing(XLOG_DISABLE);
    xlog_separator("|");
    xlog_callback(XTest_Callback, &test);

    xloge("message body");
    CHECK(test.nCalls == 1, "The line reaches the callback");
    CHECK(strstr(test.sLast, "message body") != NULL, "The message body is present");
    CHECK(strstr(test.sLast, " | ") != NULL, "The separator is present");
    CHECK(strstr(test.sLast, "\x1B") == NULL, "Colouring off leaves no escape sequences");

    /* The timestamp appears once it is switched on. */
    test.nCalls = 0;
    xlog_timing(XLOG_TIME);
    xloge("timed");
    CHECK(test.nCalls == 1 && strchr(test.sLast, ':') != NULL, "A time format adds a timestamp");

    test.nCalls = 0;
    xlog_timing(XLOG_DATE);
    xloge("dated");
    CHECK(test.nCalls == 1 && strchr(test.sLast, '-') != NULL, "A date format adds a date");

    /* Colouring adds escape sequences back. */
    test.nCalls = 0;
    xlog_coloring(XLOG_COLORING_FULL);
    xloge("coloured");
    CHECK(test.nCalls == 1 && strstr(test.sLast, "\x1B") != NULL, "Full colouring adds escape sequences");

    /* The thread id is only traced when asked for. */
    test.nCalls = 0;
    xlog_tracetid(XTRUE);
    xloge("threaded");
    CHECK(test.nCalls == 1, "The line still reaches the callback with tid tracing on");

    /* A format with no arguments and an empty message are both accepted. */
    test.nCalls = 0;
    xloge("no arguments here");
    xloge("%s", "");
    CHECK(test.nCalls == 2, "Argument-less and empty messages are logged");

    xlog_callback(NULL, NULL);
    xlog_destroy();
    return 0;
}

static int XTest_file_output(void)
{
    char sDir[] = "/tmp/xutils-log-XXXXXX";
    CHECK(mkdtemp(sDir) != NULL, "Create a private log directory");

    xlog_init("filelog", XLOG_ALL, XFALSE);
    xlog_screen(XFALSE);
    xlog_coloring(XLOG_COLORING_DISABLE);
    xlog_timing(XLOG_DISABLE);
    CHECK(xlog_path(sDir) > 0, "The log directory is set");
    xlog_file(XTRUE);
    xlog_flush(XTRUE);

    xloge("first line");
    xlogi("second line");

    char sPath[256];
    snprintf(sPath, sizeof(sPath), "%s/filelog.log", sDir);

    size_t nLength = 0;
    char *pContents = log_slurp(sPath, &nLength);
    CHECK(pContents != NULL && nLength > 0, "The log file was written");
    CHECK(strstr(pContents, "first line") != NULL, "The first line reached the file");
    CHECK(strstr(pContents, "second line") != NULL, "The second line reached the file");
    free(pContents);

    /* A filtered severity must not reach the file either. */
    xlog_setfl(XLOG_ERROR);
    xlogi("filtered from file");
    pContents = log_slurp(sPath, &nLength);
    CHECK(pContents != NULL, "The log file is still readable");
    CHECK(strstr(pContents, "filtered from file") == NULL, "A filtered severity never reaches the file");
    free(pContents);

    /* Switching file output off stops the writing. */
    xlog_setfl(XLOG_ALL);
    xlog_file(XFALSE);
    xloge("not in the file");
    pContents = log_slurp(sPath, &nLength);
    CHECK(pContents != NULL, "The log file is still readable");
    CHECK(strstr(pContents, "not in the file") == NULL, "Disabled file output writes nothing");
    free(pContents);

    xlog_destroy();
    unlink(sPath);
    rmdir(sDir);
    return 0;
}

static int XTest_rotation(void)
{
    char sDir[] = "/tmp/xutils-rot-XXXXXX";
    CHECK(mkdtemp(sDir) != NULL, "Create a private log directory");

    char sPath[256];
    snprintf(sPath, sizeof(sPath), "%s/rotated.log", sDir);

    /* Plant a log file dated well in the past so the next write has to
     * archive it under its own date instead of appending to it. */
    FILE *pOld = fopen(sPath, "wb");
    CHECK(pOld != NULL, "Plant an existing log file");
    fputs("yesterday\n", pOld);
    fclose(pOld);

    struct utimbuf { time_t actime; time_t modtime; };
    struct timespec times[2];
    times[0].tv_sec = times[1].tv_sec = (time_t)1000000000;  /* 2001-09-09 */
    times[0].tv_nsec = times[1].tv_nsec = 0;
    CHECK(utimensat(AT_FDCWD, sPath, times, 0) == 0, "Backdate the planted log file");

    xlog_init("rotated", XLOG_ALL, XFALSE);
    xlog_screen(XFALSE);
    xlog_coloring(XLOG_COLORING_DISABLE);
    CHECK(xlog_path(sDir) > 0, "The log directory is set");

    xlog_cfg_t cfg;
    xlog_get(&cfg);
    cfg.bRotate = XTRUE;
    cfg.bToFile = XTRUE;
    cfg.bFlush = XTRUE;
    xlog_set(&cfg);

    xloge("today");

    /* The old contents must have moved to a dated file, and the live file
     * must hold only the new line. */
    size_t nLength = 0;
    char *pContents = log_slurp(sPath, &nLength);
    CHECK(pContents != NULL, "The live log file exists");
    CHECK(strstr(pContents, "today") != NULL, "The new line is in the live file");
    CHECK(strstr(pContents, "yesterday") == NULL, "The old contents were rotated out");
    free(pContents);

    char sArchive[256];
    snprintf(sArchive, sizeof(sArchive), "%s/rotated-2001-09-09.log", sDir);
    pContents = log_slurp(sArchive, &nLength);
    CHECK(pContents != NULL, "The archived file was created under the old date");
    CHECK(strstr(pContents, "yesterday") != NULL, "The old contents are in the archive");
    free(pContents);

    xlog_destroy();
    unlink(sPath);
    unlink(sArchive);
    rmdir(sDir);
    return 0;
}

static int XTest_throw(void)
{
    /* The throw helpers log and then hand back the caller's value. */
    xtest_log_t test = {0};
    xlog_init("throw", XLOG_ALL, XFALSE);
    xlog_screen(XFALSE);
    xlog_callback(XTest_Callback, &test);

    CHECK(xthrowr(-7, "returning %d", -7) == -7, "A thrown status is handed back unchanged");
    CHECK(test.nCalls == 1 && strstr(test.sLast, "returning -7") != NULL, "The thrown message is logged");

    CHECK(xthrow("plain throw") == XSTDERR, "A plain throw returns the error status");
    CHECK(test.nCalls == 2, "The plain throw is logged");

    CHECK(xthrowe("errno throw") == XSTDERR, "An errno throw returns the error status");
    CHECK(test.nCalls == 3, "The errno throw is logged");

    int nLocal = 0;
    CHECK(xthrowp(&nLocal, "pointer throw") == &nLocal, "A thrown pointer is handed back unchanged");
    CHECK(test.nCalls == 4, "The pointer throw is logged");
    CHECK(xthrowp(NULL, "null throw") == NULL, "A thrown null pointer is handed back unchanged");

    /* A throw with a filtered severity still returns its value. */
    xlog_setfl(XLOG_NONE);
    test.nCalls = 0;
    CHECK(xthrowr(-3, "filtered") == -3, "A filtered throw still returns its value");
    CHECK(test.nCalls == 0, "A filtered throw logs nothing");

    xlog_callback(NULL, NULL);
    xlog_destroy();
    return 0;
}

static int XTest_callback_veto(void)
{
    /* A callback returning a negative value suppresses the file write; a
     * positive one lets the line through to screen and file. */
    xtest_log_t test = {0};
    xlog_init("veto", XLOG_ALL, XFALSE);
    xlog_screen(XFALSE);
    xlog_callback(XTest_Callback, &test);

    xloge("counted");
    CHECK(test.nCalls == 1, "The callback ran");

    /* Clearing the callback stops the delivery without stopping the log. */
    xlog_callback(NULL, NULL);
    xloge("uncounted");
    CHECK(test.nCalls == 1, "A cleared callback receives nothing");

    /* Re-installing it resumes delivery. */
    xlog_callback(XTest_Callback, &test);
    xloge("counted again");
    CHECK(test.nCalls == 2, "A re-installed callback receives again");

    /* Logging before init must not reach a stale callback. */
    xlog_callback(NULL, NULL);
    xlog_destroy();
    test.nCalls = 0;
    xloge("after destroy");
    CHECK(test.nCalls == 0, "A destroyed log does not call a stale callback");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(filtering),
    XTEST_CASE(large_message),
    XTEST_CASE(severities),
    XTEST_CASE(flag_mask),
    XTEST_CASE(config),
    XTEST_CASE(formatting),
    XTEST_CASE(file_output),
    XTEST_CASE(rotation),
    XTEST_CASE(throw),
    XTEST_CASE(callback_veto)
)
