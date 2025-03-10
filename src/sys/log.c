/*
 *  libxutils/src/sys/log.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * Advanced logging library for C/C++
 */

#include "log.h"
#include "str.h"
#include "xtime.h"

#define XLOG_FILE_PATH_MAX XLOG_PATH_MAX + XLOG_NAME_MAX + XLOG_TIME_MAX

typedef struct XLogFile {
    char sFilePath[XLOG_FILE_PATH_MAX];    
    uint8_t nCurrDay;
    FILE *pHandle;
} xlog_file_t;

typedef struct XLog {
    xsync_mutex_t lock;
    xlog_file_t fileCtx;
    xlog_cfg_t config;
} xlog_t;

typedef struct XLogCtx {
    const char *pFormat;
    xlog_flag_t eFlag;
    xbool_t bNewLine;
    uint32_t nUsec;
    xtime_t time;
} xlog_ctx_t;

static xlog_t g_xlog;
static XATOMIC g_bInit = XFALSE;

static const char *XLog_GetIndent(xlog_flag_t eFlag)
{
    xlog_cfg_t *pCfg = &g_xlog.config;
    if (!pCfg->bIndent) return XSTR_EMPTY;

    switch (eFlag)
    {
        case XLOG_NONE:
            return XLOG_SPACE_IDENT;
        case XLOG_NOTE:
        case XLOG_INFO:
        case XLOG_WARN:
             return XSTR_SPACE;
        case XLOG_DEBUG:
        case XLOG_TRACE:
        case XLOG_FATAL:
        case XLOG_ERROR:
        case XLOG_ALL:
        default: break;
    }

    return XSTR_EMPTY;
}

static const char *XLog_GetTagStr(xlog_flag_t eFlag)
{
    switch (eFlag)
    {
        case XLOG_NOTE: return "note";
        case XLOG_INFO: return "info";
        case XLOG_WARN: return "warn";
        case XLOG_DEBUG: return "debug";
        case XLOG_TRACE: return "trace";
        case XLOG_ERROR: return "error";
        case XLOG_FATAL: return "fatal";
        case XLOG_NONE:
        case XLOG_ALL:
        default: break;
    }

    return NULL;
}

static const char *XLog_GetColor(xlog_flag_t eFlag)
{
    switch (eFlag)
    {
        case XLOG_NOTE: return XSTR_EMPTY;
        case XLOG_INFO: return XLOG_COLOR_GREEN;
        case XLOG_WARN: return XLOG_COLOR_YELLOW;
        case XLOG_DEBUG: return XLOG_COLOR_BLUE;
        case XLOG_ERROR: return XLOG_COLOR_RED;
        case XLOG_TRACE: return XLOG_COLOR_CYAN;
        case XLOG_FATAL: return XLOG_COLOR_MAGENTA;
        case XLOG_NONE:
        case XLOG_ALL:
        default: break;
    }

    return XSTR_EMPTY;
}

static size_t XLog_GetThreadID(void)
{
#ifdef __linux__
    return syscall(__NR_gettid);
#elif _WIN32
    return (size_t)GetCurrentThreadId();
#else
    return (size_t)pthread_self();
#endif
}


static void XLog_CloseFile(xlog_file_t *pFile)
{
    XASSERT_VOID_RET(pFile->pHandle);
    fclose(pFile->pHandle);
    pFile->pHandle = NULL;
}

static xbool_t XLog_OpenFile(xlog_file_t *pFile, const xlog_cfg_t *pCfg, const xtime_t *pTime)
{
    XLog_CloseFile(pFile);

    if (pCfg->bRotate || pFile->sFilePath[0] == XSTR_NUL)
    {
        snprintf(pFile->sFilePath, sizeof(pFile->sFilePath), "%s/%s-%04d-%02d-%02d.log",
            pCfg->sFilePath, pCfg->sFileName, pTime->nYear, pTime->nMonth, pTime->nDay);
    }

#ifdef _WIN32
    if (fopen_s(&pFile->pHandle, pFile->sFilePath, "a")) pFile->pHandle = NULL;
#else
    pFile->pHandle = fopen(pFile->sFilePath, "a");
#endif

    if (pFile->pHandle == NULL)
    {
        printf("<%s:%d> %s: [ERROR] Failed to open file: %s (%s)\n",
            __FILE__, __LINE__, __func__, pFile->sFilePath, XSTRERR);

        return XFALSE;
    }

    pFile->nCurrDay = pTime->nDay;
    return XTRUE;
}

static void XLog_CreateTag(char *pOut, int nSize, xlog_flag_t eFlag, const char *pColor)
{
    xlog_cfg_t *pCfg = &g_xlog.config;
    pOut[0] = XSTR_NUL;

    const char *pIndent = XLog_GetIndent(eFlag);
    const char *pTag = XLog_GetTagStr(eFlag);

    if (pTag == NULL)
    {
        if (pCfg->bIndent)
            xstrncpy(pOut, nSize, pIndent);

        return;
    }

    if (pCfg->eColorFormat != XLOG_COLORING_TAG) xstrncpyf(pOut, nSize, "<%s>%s", pTag, pIndent);
    else xstrncpyf(pOut, nSize, "%s<%s>%s%s", pColor, pTag, XLOG_COLOR_RESET, pIndent);
}

static void XLog_CreateTid(char *pOut, int nSize, uint8_t nTraceTid)
{
    if (!nTraceTid) pOut[0] = XSTR_NUL;
    else xstrncpyf(pOut, nSize, "(%zu) ", XLog_GetThreadID());
}

static void XLog_DisplayMessage(const xlog_ctx_t *pCtx, const char *pInfo, size_t nInfoLen, const char *pInput)
{
    xlog_file_t *pFile = &g_xlog.fileCtx;
    xlog_cfg_t *pCfg = &g_xlog.config;
    XSTATUS nCbVal = XSTDOK;

    xbool_t bFullColor = pCfg->eColorFormat == XLOG_COLORING_FULL ? XTRUE : XFALSE;
    const char *pNewLine = pCtx->bNewLine ? XSTR_NEW_LINE : XSTR_EMPTY;
    const char *pReset = bFullColor ? XLOG_COLOR_RESET : XSTR_EMPTY;
    const char *pSep = nInfoLen > 0 ? pCfg->sSeparator : XSTR_EMPTY;
    const char *pMsg = pInput != NULL ? pInput : XSTR_EMPTY;

    if (pCfg->logCallback != NULL)
    {
        size_t nLength = 0;
        char *pLog = NULL;

        pLog = xstracpyn(&nLength, "%s%s%s%s%s",
            pInfo, pSep, pMsg, pReset, pNewLine);

        if (pLog != NULL)
        {
            nCbVal = pCfg->logCallback (
                pLog,
                nLength,
                pCtx->eFlag,
                pCfg->pCbCtx
            );

            free(pLog);
        }
    }

    if (pCfg->bToScreen && nCbVal > 0)
    {
        printf("%s%s%s%s%s", pInfo, pSep, pMsg, pReset, pNewLine);
        if (pCfg->bFlush) fflush(stdout);
    }

    if (!pCfg->bToFile || nCbVal < 0) return;
    const xtime_t *pTime = &pCtx->time;

    if (pFile->nCurrDay != pTime->nDay && pCfg->bRotate) XLog_CloseFile(pFile);
    if (pFile->pHandle == NULL && !XLog_OpenFile(pFile, pCfg, pTime)) return;

    fprintf(pFile->pHandle, "%s%s%s%s%s",
        pInfo, pSep, pMsg, pReset, pNewLine);

    if (pCfg->bFlush) fflush(pFile->pHandle);
    if (!pCfg->bKeepOpen) XLog_CloseFile(pFile);
}

static size_t XLog_CreateLogInfo(const xlog_ctx_t *pCtx, char* pOut, size_t nSize)
{
    xlog_cfg_t *pCfg = &g_xlog.config;
    const xtime_t *pTime = &pCtx->time;
    char sDate[XLOG_TIME_MAX] = XSTR_INIT;
    uint32_t nMsecTime = pCtx->nUsec / 1000;

    if (pCfg->eTimeFormat == XLOG_TIME)
    {
        xstrncpyf(sDate, sizeof(sDate),
            "%02d:%02d:%02d.%03d%s",
            pTime->nHour,pTime->nMin,
            pTime->nSec, nMsecTime,
            XSTR_SPACE);
    }
    else if (pCfg->eTimeFormat == XLOG_DATE)
    {
        xstrncpyf(sDate, sizeof(sDate),
            "%04d.%02d.%02d-%02d:%02d:%02d.%03d%s",
            pTime->nYear, pTime->nMonth,
            pTime->nDay, pTime->nHour,
            pTime->nMin, pTime->nSec,
            nMsecTime, XSTR_SPACE);
    }

    char sTid[XLOG_TAG_MAX], sTag[XLOG_TAG_MAX];
    xbool_t bFullColor = pCfg->eColorFormat == XLOG_COLORING_FULL ? XTRUE : XFALSE;

    const char *pColorCode = XLog_GetColor(pCtx->eFlag);
    const char *pColor = bFullColor ? pColorCode : XSTR_EMPTY;

    XLog_CreateTid(sTid, sizeof(sTid), pCfg->bTraceTid);
    XLog_CreateTag(sTag, sizeof(sTag), pCtx->eFlag, pColorCode);
    return xstrncpyf(pOut, nSize, "%s%s%s%s", pColor, sTid, sDate, sTag);
}

static void XLog_DisplayHeap(const xlog_ctx_t *pCtx, va_list args)
{
    char *pInput = xstracpyarg(pCtx->pFormat, args);
    char sLogInfo[XLOG_INFO_MAX];
    size_t nLength = 0;

    nLength = XLog_CreateLogInfo(pCtx, sLogInfo, sizeof(sLogInfo));
    XLog_DisplayMessage(pCtx, sLogInfo, nLength, pInput);
    if (pInput != NULL) free(pInput);
}

static void XLog_DisplayStack(const xlog_ctx_t *pCtx, va_list args)
{
    char sMessage[XLOG_MESSAGE_MAX];
    char sLogInfo[XLOG_INFO_MAX];
    size_t nLength = 0;

    vsnprintf(sMessage, sizeof(sMessage), pCtx->pFormat, args);
    nLength = XLog_CreateLogInfo(pCtx, sLogInfo, sizeof(sLogInfo));
    XLog_DisplayMessage(pCtx, sLogInfo, nLength, sMessage);
}

void XLog_Display(xlog_flag_t eFlag, xbool_t bNewLine, const char *pFmt, ...)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);
    xlog_cfg_t *pCfg = &g_xlog.config;

    if ((XLOG_FLAGS_CHECK(g_xlog.config.nFlags, eFlag)) &&
       (g_xlog.config.logCallback ||
        g_xlog.config.bToScreen ||
        g_xlog.config.bToFile))
    {
        xlog_ctx_t ctx;
        ctx.nUsec = XTime_Get(&ctx.time);
        ctx.eFlag = eFlag;
        ctx.pFormat = pFmt;
        ctx.bNewLine = bNewLine;

        void(*XLog_DisplayArgs)(const xlog_ctx_t *pCtx, va_list args);
        XLog_DisplayArgs = pCfg->bUseHeap ? XLog_DisplayHeap : XLog_DisplayStack;

        va_list args;
        va_start(args, pFmt);
        XLog_DisplayArgs(&ctx, args);
        va_end(args);
    }

    XSync_Unlock(&g_xlog.lock);
}

XSTATUS XLog_Throw(int nRetVal, const char *pFmt, ...)
{
    XASSERT_RET(g_bInit, nRetVal);
    int nFlag = (nRetVal <= 0) ?
        XLOG_ERROR : XLOG_NONE;

    if (pFmt == NULL)
    {
        xlogfl(nFlag, "%s", XSTRERR);
        return nRetVal;
    }

    size_t nSize = 0;
    va_list args;

    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, &nSize);
    va_end(args);

    XASSERT(pDest, nRetVal);
    xlogfl(nFlag, "%s", pDest);

    free(pDest);
    return nRetVal;
}

XSTATUS XLog_Throwe(int nRetVal, const char *pFmt, ...)
{
    XASSERT_RET(g_bInit, nRetVal);
    int nFlag = (nRetVal <= 0) ?
        XLOG_ERROR : XLOG_NONE;

    if (pFmt == NULL)
    {
        xlogfl(nFlag, "%s", XSTRERR);
        return nRetVal;
    }

    size_t nSize = 0;
    va_list args;

    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, &nSize);
    va_end(args);

    XASSERT(pDest, nRetVal);
    xlogfl(nFlag, "%s (%s)", pDest, XSTRERR);

    free(pDest);
    return nRetVal;
}

void* XLog_ThrowPtr(void* pRetVal, const char *pFmt, ...)
{
    XASSERT_RET(g_bInit, pRetVal);

    if (pFmt == NULL)
    {
        xloge("%s", XSTRERR);
        return pRetVal;
    }

    size_t nSize = 0;
    va_list args;

    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, &nSize);
    va_end(args);

    XASSERT(pDest, pRetVal);
    xloge("%s", pDest);

    free(pDest);
    return pRetVal;
}

void XLog_ConfigGet(struct XLogConfig *pCfg)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);
    *pCfg = g_xlog.config;
    XSync_Unlock(&g_xlog.lock);
}

void XLog_ConfigSet(struct XLogConfig *pCfg)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);

    xlog_file_t *pFile = &g_xlog.fileCtx;
    xlog_cfg_t *pOldCfg = &g_xlog.config;

    if (!pCfg->bToFile ||
        strncmp(pOldCfg->sFilePath, pCfg->sFilePath, sizeof(pOldCfg->sFilePath)) ||
        strncmp(pOldCfg->sFileName, pCfg->sFileName, sizeof(pOldCfg->sFileName)))
    {
        XLog_CloseFile(pFile); /* Log function will open it again if required */
        pFile->sFilePath[0] = XSTR_NUL;
    }

    g_xlog.config = *pCfg;
    XSync_Unlock(&g_xlog.lock);
}

void XLog_FlagEnable(xlog_flag_t eFlag)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);

    if (eFlag == XLOG_NONE || eFlag == XLOG_ALL)
        g_xlog.config.nFlags = eFlag;
    else if (!XLOG_FLAGS_CHECK(g_xlog.config.nFlags, eFlag))
        g_xlog.config.nFlags |= eFlag;

    XSync_Unlock(&g_xlog.lock);
}

void XLog_FlagDisable(xlog_flag_t eFlag)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);

    if (XLOG_FLAGS_CHECK(g_xlog.config.nFlags, eFlag))
        g_xlog.config.nFlags &= ~eFlag;

    XSync_Unlock(&g_xlog.lock);
}

void XLog_CallbackSet(xlog_cb_t callback, void *pContext)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);

    xlog_cfg_t *pCfg = &g_xlog.config;
    pCfg->pCbCtx = pContext;
    pCfg->logCallback = callback;

    XSync_Unlock(&g_xlog.lock);
}

void XLog_SeparatorSet(const char *pSeparator)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);

    xlog_cfg_t *pCfg = &g_xlog.config;

    if (snprintf(pCfg->sSeparator, 
        sizeof(pCfg->sSeparator), 
        " %s ", pSeparator) <= 0)
    {
        pCfg->sSeparator[0] = ' ';
        pCfg->sSeparator[1] = '\0';
    }

    XSync_Unlock(&g_xlog.lock);
}

void XLog_ColorFormatSet(xlog_coloring_t eFmt)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);
    g_xlog.config.eColorFormat = eFmt;
    XSync_Unlock(&g_xlog.lock);
}

void XLog_TimeFormatSet(xlog_timing_t eFmt)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);
    g_xlog.config.eTimeFormat = eFmt;
    XSync_Unlock(&g_xlog.lock);
}

void XLog_IndentSet(xbool_t bEnable)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);
    g_xlog.config.bIndent = bEnable;
    XSync_Unlock(&g_xlog.lock);
}

void XLog_FlushSet(xbool_t bEnable)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);
    g_xlog.config.bFlush = bEnable;
    XSync_Unlock(&g_xlog.lock);
}

void XLog_FileLogSet(xbool_t bEnable)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);

    xlog_file_t *pFile = &g_xlog.fileCtx;
    xlog_cfg_t *pCfg = &g_xlog.config;

    if (!bEnable) XLog_CloseFile(pFile);
    pCfg->bToFile = bEnable;

    XSync_Unlock(&g_xlog.lock);
}

void XLog_ScreenLogSet(xbool_t bEnable)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);
    g_xlog.config.bToScreen = bEnable;
    XSync_Unlock(&g_xlog.lock);
}

void XLog_TraceTid(xbool_t bEnable)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);
    g_xlog.config.bTraceTid = bEnable;
    XSync_Unlock(&g_xlog.lock);
}

void XLog_UseHeap(xbool_t bEnable)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);
    g_xlog.config.bUseHeap = bEnable;
    XSync_Unlock(&g_xlog.lock);
}

void XLog_FlagsSet(uint16_t nFlags)
{
    XASSERT_VOID_RET(g_bInit);
    XSync_Lock(&g_xlog.lock);
    g_xlog.config.nFlags = nFlags;
    XSync_Unlock(&g_xlog.lock);
}

uint16_t XLog_FlagsGet(void)
{
    XASSERT_RET(g_bInit, XSTDNON);
    XSync_Lock(&g_xlog.lock);
    uint16_t nFlags = g_xlog.config.nFlags;
    XSync_Unlock(&g_xlog.lock);
    return nFlags;
}

size_t XLog_PathSet(const char *pPath)
{
    XASSERT_RET(g_bInit, XSTDNON);
    XSync_Lock(&g_xlog.lock);

    xlog_file_t *pFile = &g_xlog.fileCtx;
    xlog_cfg_t *pCfg = &g_xlog.config;
    size_t nLength = 0;

    if (strncmp(pCfg->sFilePath, pPath, strlen(pCfg->sFilePath))) XLog_CloseFile(pFile);
    nLength = xstrncpy(pCfg->sFilePath, sizeof(pCfg->sFilePath), pPath);

    XSync_Unlock(&g_xlog.lock);
    return nLength;
}

size_t XLog_NameSet(const char *pName)
{
    XASSERT_RET(g_bInit, XSTDNON);
    XSync_Lock(&g_xlog.lock);

    xlog_file_t *pFile = &g_xlog.fileCtx;
    xlog_cfg_t *pCfg = &g_xlog.config;
    size_t nLength = 0;

    if (strncmp(pCfg->sFileName, pName, sizeof(pCfg->sFileName))) XLog_CloseFile(pFile);
    nLength = xstrncpy(pCfg->sFileName, sizeof(pCfg->sFileName), pName);

    XSync_Unlock(&g_xlog.lock);
    return nLength;
}

void XLog_Init(const char* pName, uint16_t nFlags, xbool_t bTdSafe)
{
    XASSERT_VOID_RET(!g_bInit);
    xlog_file_t *pFile = &g_xlog.fileCtx;
    xlog_cfg_t *pCfg = &g_xlog.config;

    pCfg->eColorFormat = XLOG_COLORING_TAG;
    pCfg->eTimeFormat = XLOG_DISABLE;
    pCfg->pCbCtx = NULL;
    pCfg->logCallback = NULL;

    pCfg->sSeparator[0] = ' ';
    pCfg->sSeparator[1] = '\0';
    pCfg->sFilePath[0] = '.';
    pCfg->sFilePath[1] = '\0';

    pCfg->bTraceTid = XFALSE;
    pCfg->bToScreen = XTRUE;
    pCfg->bKeepOpen = XTRUE;
    pCfg->bUseHeap = XFALSE;
    pCfg->bToFile = XFALSE;
    pCfg->bIndent = XFALSE;
    pCfg->bRotate = XTRUE;
    pCfg->bFlush = XFALSE;
    pCfg->nFlags = nFlags;

    const char *pFileName = (pName != NULL) ? pName : XLOG_NAME_DEFAULT;
    xstrncpyf(pCfg->sFileName, sizeof(pCfg->sFileName), "%s", pFileName);

    pFile->sFilePath[0] = '\0';
    pFile->pHandle = NULL;
    pFile->nCurrDay = 0;

#ifdef _WIN32
    /* Enable color support */
    DWORD dwMode = XSTDNON;
    HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(hOutput, &dwMode);
    dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOutput, dwMode);
#endif

    /* Init mutex sync */
    if (bTdSafe) XSync_Init(&g_xlog.lock);
    else g_xlog.lock.bEnabled = XFALSE;

    g_bInit = XTRUE;
}

void XLog_Destroy(void)
{
    XSync_Lock(&g_xlog.lock);
    XLog_CloseFile(&g_xlog.fileCtx);

    memset(&g_xlog.config, 0, sizeof(g_xlog.config));
    g_xlog.config.logCallback = NULL;
    g_xlog.config.pCbCtx = NULL;

    XSync_Unlock(&g_xlog.lock);
    XSync_Destroy(&g_xlog.lock);
    g_bInit = XFALSE;
}
