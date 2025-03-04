/*
 *  libxutils/src/sys/log.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * Advanced logging library for C/C++
 */

#ifndef __XLOG_H__
#define __XLOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"
#include "sync.h"

/* Definations for version info */
#define XLOG_VERSION_MAJOR   1
#define XLOG_VERSION_MINOR   8
#define SLOG_BUILD_NUMBER    28

#define XLOG_NAME_DEFAULT   "xlog"

/* Supported colors and tags */
#define XLOG_COLOR_NORMAL   "\x1B[0m"
#define XLOG_COLOR_RED      "\x1B[31m"
#define XLOG_COLOR_GREEN    "\x1B[32m"
#define XLOG_COLOR_YELLOW   "\x1B[33m"
#define XLOG_COLOR_BLUE     "\x1B[34m"
#define XLOG_COLOR_MAGENTA  "\x1B[35m"
#define XLOG_COLOR_CYAN     "\x1B[36m"
#define XLOG_COLOR_WHITE    "\x1B[37m"
#define XLOG_COLOR_RESET    "\033[0m"
#define XLOG_SPACE_IDENT    "       "

/* Trace source location helpers */
#define XLOG_LOCATION_LVL1(LINE) #LINE
#define XLOG_LOCATION_LVL2(LINE) XLOG_LOCATION_LVL1(LINE)
#define XLOG_THROW_LOCATION "[" __FILE__ ":" XLOG_LOCATION_LVL2(__LINE__) "] "

#define XLOG_FLAGS_CHECK(c, f) (((c) & (f)) == (f))
#define XLOG_FLAGS_DEFAULT   203

/* SLog limits (To be safe while avoiding dynamic allocations) */
#define XLOG_MESSAGE_MAX     8196
#define XLOG_PATH_MAX        2048
#define XLOG_INFO_MAX        512
#define XLOG_NAME_MAX        256
#define XLOG_TIME_MAX        64
#define XLOG_TAG_MAX         32
#define XLOG_CLR_MAX         16

typedef enum
{
    XLOG_NONE = (1 << 0),
    XLOG_NOTE = (1 << 1),
    XLOG_INFO = (1 << 2),
    XLOG_WARN = (1 << 3),
    XLOG_DEBUG = (1 << 4),
    XLOG_TRACE = (1 << 5),
    XLOG_ERROR = (1 << 6),
    XLOG_FATAL = (1 << 7),
    XLOG_DEFAULT = 203,
    XLOG_ALL = 255
} xlog_flag_t;

typedef int(*xlog_cb_t)(const char *pLog, size_t nLength, xlog_flag_t eFlag, void *pCtx);

/* Output coloring control flags */
typedef enum
{
    XLOG_COLORING_DISABLE = 0,
    XLOG_COLORING_TAG,
    XLOG_COLORING_FULL
} xlog_coloring_t;

/* Output time and date control flags */
typedef enum
{
    XLOG_DISABLE = 0,
    XLOG_TIME,
    XLOG_DATE
} xlog_timing_t;

/* XLog function definitions */
#define XLog(...) XLog_Display(XLOG_NONE, 1, __VA_ARGS__)
#define XLog_Note(...) XLog_Display(XLOG_NOTE, 1, __VA_ARGS__)
#define XLog_Info(...) XLog_Display(XLOG_INFO, 1, __VA_ARGS__)
#define XLog_Warn(...) XLog_Display(XLOG_WARN, 1, __VA_ARGS__)
#define XLog_Debug(...) XLog_Display(XLOG_DEBUG, 1, __VA_ARGS__)
#define XLog_Error(...) XLog_Display(XLOG_ERROR, 1, __VA_ARGS__)
#define XLog_Trace(...) XLog_Display(XLOG_TRACE, 1, XLOG_THROW_LOCATION __VA_ARGS__)
#define XLog_Fatal(...) XLog_Display(XLOG_FATAL, 1, XLOG_THROW_LOCATION __VA_ARGS__)

/* No new line definitions */
#define XLog_(...) XLog_Display(XLOG_NONE, 0, __VA_ARGS__)
#define XLog_Note_(...) XLog_Display(XLOG_NOTE, 0, __VA_ARGS__)
#define XLog_Info_(...) XLog_Display(XLOG_INFO, 0, __VA_ARGS__)
#define XLog_Warn_(...) XLog_Display(XLOG_WARN, 0, __VA_ARGS__)
#define XLog_Debug_(...) XLog_Display(XLOG_DEBUG, 0, __VA_ARGS__)
#define XLog_Error_(...) XLog_Display(XLOG_ERROR, 0, __VA_ARGS__)
#define XLog_Trace_(...) XLog_Display(XLOG_TRACE, 0, XLOG_THROW_LOCATION __VA_ARGS__)
#define XLog_Fatal_(...) XLog_Display(XLOG_FATAL, 0, XLOG_THROW_LOCATION __VA_ARGS__)

/* Lower case short name functions */
#define xlog(...) XLog(__VA_ARGS__)
#define xlogn(...) XLog_Note(__VA_ARGS__)
#define xlogi(...) XLog_Info(__VA_ARGS__)
#define xlogw(...) XLog_Warn(__VA_ARGS__)
#define xlogd(...) XLog_Debug( __VA_ARGS__)
#define xloge(...) XLog_Error( __VA_ARGS__)
#define xlogt(...) XLog_Trace(__VA_ARGS__)
#define xlogf(...) XLog_Fatal(__VA_ARGS__)
#define xlogfl(f, ...) XLog_Display(f, 1, __VA_ARGS__)

/* Lower case short name functions without new line */
#define xlog_wn(...) XLog_(__VA_ARGS__)
#define xlogn_wn(...) XLog_Note_(__VA_ARGS__)
#define xlogi_wn(...) XLog_Info_(__VA_ARGS__)
#define xlogw_wn(...) XLog_Warn_(__VA_ARGS__)
#define xlogd_wn(...) XLog_Debug_( __VA_ARGS__)
#define xloge_wn(...) XLog_Error_( __VA_ARGS__)
#define xlogt_wn(...) XLog_Trace_(__VA_ARGS__)
#define xlogf_wn(...) XLog_Fatal_(__VA_ARGS__)
#define xlogfl_wn(f, ...) XLog_Display(f, 0, __VA_ARGS__)

#define xthrowp(r, ...) XLog_ThrowPtr(r, __VA_ARGS__)
#define xthrowr(r, ...) XLog_Throw(r, __VA_ARGS__)
#define xthrowe(...) XLog_Throwe(XSTDERR, __VA_ARGS__)
#define xthrow(...) XLog_Throw(XSTDERR, __VA_ARGS__)

#define xlog_init(name,flags,safe) XLog_Init(name, flags, safe)
#define xlog_defaults() xlog_init(NULL, XLOG_DEFAULT, 0)
#define xlog_get(cfg) XLog_ConfigGet(cfg)
#define xlog_set(cfg) XLog_ConfigSet(cfg)
#define xlog_destroy() XLog_Destroy()

#define xlog_callback(cb,ctx) XLog_CallbackSet(cb,ctx)
#define xlog_separator(sep) XLog_SeparatorSet(sep)
#define xlog_coloring(fmt) XLog_ColorFormatSet(fmt)
#define xlog_tracetid(fl) XLog_TraceTid(fl)
#define xlog_newline(fl) XLog_NewLine(fl)
#define xlog_useheap(fl) XLog_UseHeap(fl)
#define xlog_timing(fmt) XLog_TimeFormatSet(fmt)
#define xlog_screen(fl) XLog_ScreenLogSet(fl)
#define xlog_file(fl) XLog_FileLogSet(fl)
#define xlog_indent(fl) XLog_IndentSet(fl)
#define xlog_flush(fl) XLog_FlushSet(fl)
#define xlog_enable(fl) XLog_FlagEnable(fl)
#define xlog_disable(fl) XLog_FlagDisable(fl)
#define xlog_getfl(fl) XLog_FlagsGet(fl)
#define xlog_setfl(fl) XLog_FlagsSet(fl)
#define xlog_path(path) XLog_PathSet(path)
#define xlog_name(name) XLog_NameSet(name)

#define XLOG_ASSERT(condition, value, ...)  \
    do {                                    \
        if (!condition) {                   \
            if (__VA_ARGS__)                \
                xloge(__VA_ARGS__);         \
            else                            \
                xloge(XLOG_THROW_LOCATION   \
                "Assert failed");           \
            return value;                   \
        }                                   \
    }                                       \
    while (XSTDNON)

#define XLOG_TRY(condition, value, ...)     \
    do {                                    \
        if (!condition) {                   \
            if (__VA_ARGS__)                \
                xlogw(__VA_ARGS__);         \
            return value;                   \
        }                                   \
    }                                       \
    while (XSTDNON)

typedef struct XLogConfig {
    xlog_coloring_t eColorFormat;
    xlog_timing_t eTimeFormat;
    xlog_cb_t logCallback;
    void* pCbCtx;

    xbool_t bTraceTid;
    xbool_t bToScreen;
    xbool_t bKeepOpen;
    xbool_t bUseHeap;
    xbool_t bRotate;
    xbool_t bToFile;
    xbool_t bIndent;
    xbool_t bFlush;
    uint16_t nFlags;

    char sFileName[XLOG_NAME_MAX];
    char sFilePath[XLOG_PATH_MAX];
    char sSeparator[XLOG_NAME_MAX];
} xlog_cfg_t;

const char* XLog_Version(xbool_t bShort);
void XLog_ConfigGet(struct XLogConfig *pCfg);
void XLog_ConfigSet(struct XLogConfig *pCfg);

void XLog_FlagEnable(xlog_flag_t eFlag);
void XLog_FlagDisable(xlog_flag_t eFlag);

void XLog_CallbackSet(xlog_cb_t callback, void *pContext);
void XLog_SeparatorSet(const char *pSeparator);
void XLog_ColorFormatSet(xlog_coloring_t eFmt);
void XLog_TimeFormatSet(xlog_timing_t eFmt);
void XLog_IndentSet(xbool_t nEnable);

size_t XLog_PathSet(const char *pPath);
size_t XLog_NameSet(const char *pName);
void XLog_ScreenLogSet(xbool_t bEnable);
void XLog_FileLogSet(xbool_t bEnable);
void XLog_FlushSet(xbool_t bEnable);
void XLog_TraceTid(xbool_t bEnable);
void XLog_UseHeap(xbool_t bEnable);
void XLog_FlagsSet(uint16_t nFlags);
uint16_t XLog_FlagsGet(void);

void XLog_Init(const char* pName, uint16_t nFlags, xbool_t bTdSafe);
void XLog_Destroy(void);

void XLog_Display(xlog_flag_t eFlag, xbool_t bNewLine, const char *pFormat, ...);
XSTATUS XLog_Throw(int nRetVal, const char *pFmt, ...);
XSTATUS XLog_Throwe(int nRetVal, const char *pFmt, ...);
void* XLog_ThrowPtr(void* pRetVal, const char *pFmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __XLOG_H__ */