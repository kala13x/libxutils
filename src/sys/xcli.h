/*!
 *  @file libxutils/src/sys/xcli.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of CLI window operarions.
 */

#ifndef __XUTILS_XCLI_H__
#define __XUTILS_XCLI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xbuf.h"
#include "list.h"

#define XCLI_BAR_INTERVAL   100000
#define XCLI_BUF_SIZE       256

#define XCLI_CENTER         0
#define XCLI_RIGHT          1
#define XCLI_LEFT           2

typedef struct xcli_size_ {
    size_t nWinColumns;
    size_t nWinRows;
} xcli_size_t;

int XCLI_GetPass(const char *pText, char *pPass, size_t nSize);
XSTATUS XCLI_GetWindowSize(xcli_size_t *pCli);
size_t XCLI_CountFormat(xcli_size_t *pSize, const char *pLine, size_t nLength, size_t *pPosit);

typedef enum {
    XCLI_FLUSH_SCREEN = 0,
    XCLI_RENDER_FRAME,
    XCLI_LINE_BY_LINE
} xcli_disp_type_t;

typedef struct xcli_wind_ {
    xcli_disp_type_t eType;
    xcli_size_t frameSize;
    xarray_t lineArray;
} xcli_wind_t;

void XWindow_Init(xcli_wind_t *pWin);
void XWindow_Destroy(xcli_wind_t *pWin);

XSTATUS XWindow_UpdateSize(xcli_wind_t *pWin);
int XWindow_ClearScreen();

XSTATUS XWindow_AddAligned(xcli_wind_t *pWin, const char *pInput, const char *pFmt, uint8_t nAlign);
XSTATUS XWindow_AddLineFmt(xcli_wind_t *pWin, const char *pFmt, ...);
XSTATUS XWindow_AddLine(xcli_wind_t *pWin, char *pLine, size_t nLine);
XSTATUS XWindow_AddEmptyLine(xcli_wind_t *pWin);

XSTATUS XWindow_GetFrame(xcli_wind_t *pWin, xbyte_buffer_t *pFrame);
XSTATUS XWindow_Display(xcli_wind_t *pWin);
XSTATUS XWindow_Flush(xcli_wind_t *pWin);

typedef struct xcli_bar_ {
    xcli_size_t frameSize;
    size_t nBarLength;
    size_t nBarUsed;

    uint32_t nIntervalU;
    uint32_t nLastTime;

    xbool_t bInPercent;
    xbool_t bInSuffix;
    xbool_t bKeepBar;
    xbool_t bReverse;
    double fPercent;
    int nPosition;

    char cBackCursor;
    char cCursor;
    char cLoader;
    char cEmpty;
    char cStart;
    char cEnd;

    char sPercent[XCLI_BUF_SIZE];
    char sPrefix[XCLI_BUF_SIZE];
    char sSuffix[XCLI_BUF_SIZE];
} xcli_bar_t;

void XProgBar_GetDefaults(xcli_bar_t *pCtx);
XSTATUS XProgBar_UpdateWindowSize(xcli_bar_t *pCtx);

xbool_t XProgBar_CalculateBounds(xcli_bar_t *pCtx);
size_t XProgBar_GetOutputAdv(xcli_bar_t *pCtx, char *pDst, size_t nSize, char *pProgress, xbool_t bHidePct);
size_t XProgBar_GetOutput(xcli_bar_t *pCtx, char *pDst, size_t nSize);

void XProgBar_MakeMove(xcli_bar_t *pCtx);
void XProgBar_Update(xcli_bar_t *pCtx);
void XProgBar_Finish(xcli_bar_t *pCtx);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XCLI_H__ */