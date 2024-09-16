/*
 *  examples/xlog.c
 * 
 *  Copyleft (C) 2015  Sun Dro (a.k.a. kala13x)
 *
 * Example file of useing XLog library.
 */


#include "xstd.h"
#include "xstr.h"
#include "xlog.h"
#include "xtime.h"

int logCallback(const char *pLog, size_t nLength, xlog_flag_t eFlag, void *pCtx)
{
    printf("%s", pLog);
    return 0;
}

void greet() 
{
    printf("=========================================\n");
    printf("XLog Version: %s\n", XLog_Version(XFALSE));
    printf("=========================================\n");
}

int main()
{
    /* Used variables */
    char char_arg[32];
    strcpy(char_arg, "test string");
    int int_arg = 69;

    /* Greet users */
    greet();

    /* Initialize XLog with default parameters */
    XLog_Init("example", XLOG_ALL, 0);
    xlog_separator("[xutils]");
    xlog_indent(1);

    /* Log and print something with level 0 */
    XLog_Note("Test message with level 0");

    /* Log and print something with level 1 */
    XLog_Warn("Warn message with level 1");

    /* Log and print something with level 2 */
    XLog_Info("Info message with level 2");

    /* Log and print something with level 3 */
    XLog_Note("Test message with level 3");

    xlog_cfg_t slgCfg;
    xlog_get(&slgCfg);
    slgCfg.eColorFormat = XLOG_COLORING_FULL;
    slgCfg.bToFile = 1;
    xlog_set(&slgCfg);

    /* Log and print something with char argument */
    XLog_Debug("Debug message with char argument: %s", char_arg);

    /* Log and print something with int argument */
    XLog_Error("Error message with int argument: %d", int_arg);

    slgCfg.eTimeFormat = XLOG_DATE;
    slgCfg.logCallback = logCallback;
    slgCfg.bUseHeap = 1;
    xlog_set(&slgCfg);

    /* Print message and save log in the file */
    XLog_Debug("Debug message in the file with int argument: %d", int_arg);

    /* On the fly change parameters (enable file logger) */
    slgCfg.bToFile = 1;
    xlog_set(&slgCfg);

    /* Log with user specified tag */
    XLog("Message without tag and with int argument: %d", int_arg);

    xlog("just another simple message");
    xlogd("just another debug message");
    xlogt("just another trace message");

    xlog_destroy();
    return 0;
}
