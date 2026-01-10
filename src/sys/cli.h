/*!
 *  @file libxutils/src/sys/cli.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of CLI window operarions.
 */

#ifndef __XUTILS_XCLI_H__
#define __XUTILS_XCLI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"
#include "buf.h"
#include "list.h"

#ifdef __linux__
#include <termios.h>
#endif

#define XCLI_BAR_INTERVAL   100000
#define XCLI_BUF_SIZE       256

#define XCLI_CENTER         0
#define XCLI_RIGHT          1
#define XCLI_LEFT           2

typedef struct xcli_size_ {
    size_t nColumns;
    size_t nRows;
} xcli_size_t;

XSTATUS XCLI_SetInputMode(void *pAttributes);
XSTATUS XCLI_RestoreAttributes(void *pAttributes);
XSTATUS XCLI_ReadStdin(char *pBuffer, size_t nSize, xbool_t bAsync);

XSTATUS XCLI_GetChar(char *pChar, xbool_t bAsync);
XSTATUS XCLI_GetPass(const char *pText, char *pPass, size_t nSize);
XSTATUS XCLI_GetInput(const char *pText, char *pInput, size_t nSize, xbool_t bCutNewLine);

XSTATUS XCLI_GetWindowSize(xcli_size_t *pSize);
size_t XCLI_CountFormat(xcli_size_t *pSize, const char *pLine, size_t nLength, size_t *pPosit);

typedef enum {
    XCLI_FLUSH_SCREEN = 0,
    XCLI_RENDER_FRAME,
    XCLI_LINE_BY_LINE
} xcli_disp_type_t;

typedef struct xcli_win_ {
    xcli_disp_type_t eType;
    xcli_size_t frame;
    xarray_t lines;
    xbool_t bAscii;
} xcli_win_t;

void XCLIWin_Init(xcli_win_t *pWin, xbool_t bAscii);
void XCLIWin_Destroy(xcli_win_t *pWin);

XSTATUS XCLIWin_UpdateSize(xcli_win_t *pWin);
int XCLIWin_ClearScreen(xbool_t bAscii);

XSTATUS XCLIWin_AddAligned(xcli_win_t *pWin, const char *pInput, const char *pFmt, uint8_t nAlign);
XSTATUS XCLIWin_AddLineFmt(xcli_win_t *pWin, const char *pFmt, ...);
XSTATUS XCLIWin_AddLine(xcli_win_t *pWin, char *pLine, size_t nLine);
XSTATUS XCLIWin_AddEmptyLine(xcli_win_t *pWin);

XSTATUS XCLIWin_GetFrame(xcli_win_t *pWin, xbyte_buffer_t *pFrameBuff);
XSTATUS XCLIWin_Display(xcli_win_t *pWin);
XSTATUS XCLIWin_Flush(xcli_win_t *pWin);

typedef struct xcli_bar_ {
    xcli_size_t frame;
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
