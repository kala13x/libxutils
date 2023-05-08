/*!
 *  @file libxutils/src/sys/xcli.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of CLI window operarions.
 */

#include "xstd.h"
#include "xcli.h"
#include "xtime.h"

#ifdef __linux__
#include <termios.h>
#endif

#define XBAR_FRAME_BYTES 3
#define XCLI_PERCENT_MAX 4

int XCLI_GetPass(const char *pText, char *pPass, size_t nSize)
{
    size_t nLength = XSTDNON;
#ifdef __linux__
    struct termios oflags, nflags;
    tcgetattr(fileno(stdin), &oflags);

    nflags = oflags;
    nflags.c_lflag &= ~ECHO;
    nflags.c_lflag |= ECHONL;
    if (pText != NULL) printf("%s", pText);

    if (tcsetattr(fileno(stdin), TCSANOW, &nflags)) return XSTDERR;
    char *pRet = fgets(pPass, nSize, stdin);
    nLength = pRet != NULL ? strlen(pPass) - 1 : 0;
    if (tcsetattr(fileno(stdin), TCSANOW, &oflags)) return XSTDERR;
#endif

    pPass[nLength] = 0;
    return (int)nLength;
}

XSTATUS XCLI_GetWindowSize(xcli_size_t *pCli)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    csbi.srWindow.Right = csbi.srWindow.Left = 0;
    csbi.srWindow.Top = csbi.srWindow.Bottom = 0;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    pCli->nWinColumns = (size_t)csbi.srWindow.Right - (size_t)csbi.srWindow.Left + 1;
    pCli->nWinRows = (size_t)csbi.srWindow.Bottom - (size_t)csbi.srWindow.Top + 1;
#else
    struct winsize size;
    size.ws_col = size.ws_row = 0;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
    pCli->nWinColumns = size.ws_col;
    pCli->nWinRows = size.ws_row;
#endif

    return (pCli->nWinColumns && pCli->nWinRows) ?  XSTDOK : XSTDERR;
}

void XWindow_Init(xcli_wind_t *pWin)
{
    XArray_Init(&pWin->lineArray, 0, 0);
    pWin->eType = XCLI_RENDER_FRAME;
    pWin->frameSize.nWinColumns = 0;
    pWin->frameSize.nWinRows = 0;
}

XSTATUS XWindow_UpdateSize(xcli_wind_t *pWin)
{
    XSTATUS nStatus = XCLI_GetWindowSize(&pWin->frameSize);
    if (nStatus != XSTDERR) pWin->frameSize.nWinRows--;
    return nStatus;
}

XSTATUS XWindow_AddLine(xcli_wind_t *pWin, char *pLine, size_t nLength)
{
    xcli_size_t *pSize = &pWin->frameSize;
    xarray_t *pLines = &pWin->lineArray;

    if (XWindow_UpdateSize(pWin) == XSTDERR) return XSTDERR;
    if (pLines->nUsed >= pSize->nWinRows) return XSTDNON;

    if (XArray_AddData(pLines, pLine, nLength) < 0)
    {
        XArray_Clear(pLines);
        return XSTDERR;
    }
    
    return XSTDOK;
}

XSTATUS XWindow_AddLineFmt(xcli_wind_t *pWin, const char *pFmt, ...)
{
    xcli_size_t *pSize = &pWin->frameSize;
    xarray_t *pLines = &pWin->lineArray;

    if (XWindow_UpdateSize(pWin) == XSTDERR) return XSTDERR;
    if (pLines->nUsed >= pSize->nWinRows) return XSTDNON;

    size_t nLength = 0;
    va_list args;

    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, &nLength);
    va_end(args);

    XSTATUS nStatus = XSTDERR;
    if (pDest == NULL) return nStatus;

    if (XArray_PushData(pLines, pDest, nLength) < 0)
    {
        XArray_Clear(pLines);
        free(pDest);
        return XSTDERR;
    }

    return XSTDOK;
}

XSTATUS XWindow_AddEmptyLine(xcli_wind_t *pWin)     
{
    if (XWindow_UpdateSize(pWin) == XSTDERR) return XSTDERR;
    size_t nColumns = pWin->frameSize.nWinColumns;
    char emptyLine[XLINE_MAX];

    size_t i, nSpaces = XSTD_MIN(nColumns, sizeof(emptyLine) - 1);
    for (i = 0; i < nSpaces; i++) emptyLine[i] = XSTR_SPACE_CHAR;

    emptyLine[i] = XSTR_NUL;
    return XWindow_AddLine(pWin, emptyLine, i);
}

XSTATUS XWindow_AddAligned(xcli_wind_t *pWin, const char *pInput, const char *pFmt, uint8_t nAlign)
{
    if (XWindow_UpdateSize(pWin) == XSTDERR) return XSTDERR;
    size_t nColumns = pWin->frameSize.nWinColumns;

    size_t nInputLen = strlen(pInput);
    if (!nInputLen) return XSTDERR;

    char sAfterBuf[XLINE_MAX];
    char sPreBuf[XLINE_MAX];
    size_t nSpaces = 0;

    if (nAlign == XCLI_CENTER) nSpaces = (nColumns - nInputLen) / 2;
    else if (nAlign == XCLI_RIGHT) nSpaces = nColumns - nInputLen;
    xstrnfill(sPreBuf, sizeof(sPreBuf), nSpaces, XSTR_SPACE_CHAR);

    if (nAlign == XCLI_RIGHT) nSpaces = 0;
    else if (nAlign == XCLI_CENTER && nColumns % 2) nSpaces++;
    else if (nAlign == XCLI_LEFT) nSpaces = nColumns - nInputLen;
    xstrnfill(sAfterBuf, sizeof(sAfterBuf), nSpaces, XSTR_SPACE_CHAR);

    if (pFmt == NULL) return XWindow_AddLineFmt(pWin, "%s%s%s", sPreBuf, pInput, sAfterBuf);
    return XWindow_AddLineFmt(pWin, "%s%s%s%s%s", pFmt, sPreBuf, pInput, sAfterBuf, XSTR_FMT_RESET);
}

int XWindow_ClearScreen()
{
    int nRet = XSTDNON;
#if !defined(_WIN32) && !defined(_WIN64)
    nRet = system("clear");
#else
    nRet = system("cls");
#endif
    return nRet;
}

XSTATUS XWindow_RenderLine(xcli_wind_t *pWin, xbyte_buffer_t *pLine, xarray_data_t *pArrData)
{
    if (XWindow_UpdateSize(pWin) == XSTDERR) return XSTDERR;
    size_t nChars = 0, nMaxSize = pWin->frameSize.nWinColumns;

    XByteBuffer_SetData(pLine, (uint8_t*)pArrData->pData, pArrData->nSize);
    pLine->nSize = pArrData->nSize;
    pArrData->nSize = 0;

    const char *pRawLine = (const char*)pLine->pData;
    size_t nExtra = xstrextra(pRawLine, pLine->nUsed, nMaxSize, &nChars, NULL);

    if (nChars < nMaxSize)
    {
        size_t i, nAppendSize = nMaxSize - nChars;
        if (XByteBuffer_Reserve(pLine, nAppendSize) <= 0)
        {
            XByteBuffer_Clear(pLine);
            return XSTDERR;
        }

        for (i = 0; i < nAppendSize; i++)
        {
            if (XByteBuffer_AddByte(pLine, XSTR_SPACE_CHAR) <= 0)
            {
                XByteBuffer_Clear(pLine);
                return XSTDERR;
            }
        }
    }

    if (XByteBuffer_Resize(pLine, nMaxSize + nExtra + 1) <= 0 ||
        XByteBuffer_AddFmt(pLine, "%s", XSTR_FMT_RESET) <= 0)
    {
        XByteBuffer_Clear(pLine);
        return XSTDERR;
    }

    pArrData->pData = pLine->pData;
    pArrData->nSize = pLine->nSize;

    return XSTDOK;
}

XSTATUS XWindow_GetFrame(xcli_wind_t *pWin, xbyte_buffer_t *pFrame)
{
    if (pWin == NULL || pFrame == NULL) return XSTDERR;
    XByteBuffer_Init(pFrame, XSTDNON, XSTDNON);

    while (pWin->lineArray.nUsed < pWin->frameSize.nWinRows)
        if (XWindow_AddEmptyLine(pWin) < 0) return XSTDERR;

    xcli_size_t *pSize = &pWin->frameSize;
    xarray_t *pLines = &pWin->lineArray;
    size_t i, nRows = XSTD_MIN(pSize->nWinRows, pLines->nUsed);

    for (i = 0; i < nRows; i++)
    {
        xarray_data_t *pData = XArray_Get(pLines, i);
        if (pData == NULL) continue;

        xbyte_buffer_t lineBuff;
        XByteBuffer_Init(&lineBuff, XSTDNON, XFALSE);

        if (XWindow_RenderLine(pWin, &lineBuff, pData) < 0)
        {
            XByteBuffer_Clear(pFrame);
            XArray_Clear(pLines);
            return XSTDERR;
        }

        if (XByteBuffer_AddBuff(pFrame, &lineBuff) < 0)
        {
            XByteBuffer_Clear(pFrame);
            XArray_Clear(pLines);
            return XSTDERR;
        }
    }

    return XSTDOK;
}

XSTATUS XWindow_Display(xcli_wind_t *pWin)
{
    XSTATUS nStatus = XSTDNON;

    if (pWin->eType == XCLI_LINE_BY_LINE)
    {
        XWindow_ClearScreen();
        xarray_t *pLines = &pWin->lineArray;
        size_t i, nWinRows = pWin->frameSize.nWinRows;
        size_t nRows = XSTD_MIN(nWinRows, pLines->nUsed);

        for (i = 0; i < nRows; i++)
        {
            xarray_data_t *pData = XArray_Get(pLines, i);
            if (pData == NULL) continue;

            xbyte_buffer_t lineBuff;
            if (XWindow_RenderLine(pWin, &lineBuff, pData) < 0)
            {
                XArray_Clear(pLines);
                return XSTDERR;
            }

            printf("%s\n", (char*)lineBuff.pData);
        }

        for (i = pLines->nUsed; i < nWinRows; i++) printf("\n");

        fflush(stdout);
        nStatus = XSTDOK;
    }
    else if (pWin->eType == XCLI_RENDER_FRAME)
    {
        xbyte_buffer_t frameBuf;
        nStatus = XWindow_GetFrame(pWin, &frameBuf);
        if (nStatus == XSTDERR) return nStatus;

        XWindow_ClearScreen();
        printf("%s\r", (char*)frameBuf.pData);
        fflush(stdout);

        XByteBuffer_Clear(&frameBuf);
        nStatus = XSTDOK;
    }

    return nStatus;
}

XSTATUS XWindow_Flush(xcli_wind_t *pWin)
{
    XSTATUS nStatus = XWindow_Display(pWin);
    XArray_Clear(&pWin->lineArray);
    return nStatus;
}

void XWindow_Destroy(xcli_wind_t *pWin)
{
    xarray_t *pArr = &pWin->lineArray;
    XArray_Destroy(pArr);
}

/////////////////////////////////////////////////////////////////////////
// Implementation of CLI progress bar
/////////////////////////////////////////////////////////////////////////

XSTATUS XProgBar_UpdateWindowSize(xcli_bar_t *pCtx)
{
    xcli_size_t *pSize = &pCtx->frameSize;
    return XCLI_GetWindowSize(pSize);
}

void XProgBar_GetDefaults(xcli_bar_t *pCtx)
{
    pCtx->nIntervalU = XCLI_BAR_INTERVAL;
    pCtx->nBarLength = 0;
    pCtx->nLastTime = 0;
    pCtx->nPosition = 0;
    pCtx->fPercent = 0.;
    pCtx->nBarUsed = 0;

    pCtx->bInPercent = XFALSE;
    pCtx->bInSuffix = XFALSE;
    pCtx->bReverse = XFALSE;
    pCtx->bKeepBar = XFALSE;

    pCtx->cBackCursor = '<';
    pCtx->cCursor = '>';
    pCtx->cLoader = '=';
    pCtx->cEmpty = ' ';
    pCtx->cStart = '[';
    pCtx->cEnd = ']';

    xstrnul(pCtx->sPercent);
    xstrnul(pCtx->sPrefix);
    xstrnul(pCtx->sSuffix);
    XProgBar_UpdateWindowSize(pCtx);
}

void XProgBar_Finish(xcli_bar_t *pCtx)
{
    if (pCtx->bKeepBar)
    {
        printf("\n");
        return;
    }

    char sSpaces[XLINE_MAX];
    xstrnul(sSpaces);

    char sPercent[XCLI_BUF_SIZE];
    if (pCtx->fPercent < 0.) xstrncpy(sPercent, sizeof(sPercent), " N/A ");
    else xstrncpyf(sPercent, sizeof(sPercent), "%.1f%%", pCtx->fPercent);

    xstrnfill(sSpaces, sizeof(sSpaces), pCtx->nBarLength, XSTR_SPACE_CHAR);
    printf("%s%s %s %s\r\n", pCtx->sPrefix, sSpaces, sPercent, pCtx->sSuffix);
}

void XProgBar_MakeMove(xcli_bar_t *pCtx)
{
    XProgBar_UpdateWindowSize(pCtx);
    size_t nColumns = pCtx->frameSize.nWinColumns;

    char sProgress[XLINE_MAX];
    char sSpaces[XLINE_MAX];
    xstrnul(sProgress);
    xstrnul(sSpaces);

    size_t nUsedLength = strlen(pCtx->sPrefix) + strlen(pCtx->sSuffix) + 7;
    pCtx->nBarLength = (nColumns > nUsedLength) ? (nColumns - nUsedLength) : 0;
    size_t i, nLoaderPercent = pCtx->nBarLength / 10;

    if (pCtx->nBarLength)
    {
        for (i = 0; i < (size_t)pCtx->nPosition; i++)
            xstrncat(sProgress, sizeof(sProgress), "%c", pCtx->cEmpty);

        if (pCtx->bReverse == XTRUE && pCtx->nPosition < pCtx->nBarLength)
            xstrncat(sProgress, sizeof(sProgress), "%c", pCtx->cBackCursor);

        for (i = 0; i < nLoaderPercent; i++)
            xstrncat(sProgress, sizeof(sProgress), "%c", pCtx->cLoader);

        if (pCtx->bReverse == XFALSE && (size_t)pCtx->nPosition < pCtx->nBarLength - nLoaderPercent)
            xstrncat(sProgress, sizeof(sProgress), "%c", pCtx->cCursor);

        size_t nProgressLen = strlen(sProgress);
        if (nProgressLen < pCtx->nBarLength)
        {
            size_t nUnusedLength = pCtx->nBarLength - nProgressLen;
            for (i = 0; i < nUnusedLength; i++)
                xstrncat(sSpaces, sizeof(sSpaces), "%c", pCtx->cEmpty);
        }

        uint32_t nNowTime = 0;
        if (pCtx->nIntervalU) nNowTime = XTime_GetUsec();

        if (!pCtx->nIntervalU || (!pCtx->nLastTime || (nNowTime - pCtx->nLastTime) >= pCtx->nIntervalU))
        {
            if (!pCtx->bReverse)
            {
                if (++pCtx->nPosition > pCtx->nBarLength - nLoaderPercent - 1)
                {
                    pCtx->nPosition = (int)pCtx->nBarLength - (int)nLoaderPercent - 1;
                    pCtx->bReverse = XTRUE;
                }
            }
            else
            {
                if (--pCtx->nPosition < 0)
                {
                    pCtx->bReverse = XFALSE;
                    pCtx->nPosition = 0;
                }
            }

            if (pCtx->nIntervalU) pCtx->nLastTime = nNowTime;
        }
    }

    printf("%s%s%c%s%s%s%s%c%s N/A %s\r",
    pCtx->sPrefix, XSTR_FMT_BOLD, pCtx->cStart, XSTR_FMT_RESET, sProgress,
    sSpaces, XSTR_FMT_BOLD, pCtx->cEnd, XSTR_FMT_RESET, pCtx->sSuffix);
    fflush(stdout);
}

xbool_t XProgBar_CalculateBounds(xcli_bar_t *pCtx)
{
    if (pCtx->fPercent > 100.) pCtx->fPercent = 100.;
    else if (pCtx->fPercent < 0.) pCtx->fPercent = 0.;

    if (pCtx->bInPercent) 
        xstrncpyf(pCtx->sPercent, sizeof(pCtx->sPercent), "%s%.1f%%%s", 
            XSTR_FMT_DIM, pCtx->fPercent, XSTR_FMT_RESET);
    else
        xstrncpyfl(pCtx->sPercent, sizeof(pCtx->sPercent), 
            XCLI_PERCENT_MAX, XSTR_SPACE_CHAR, "%.1f%%", pCtx->fPercent);

    size_t nColumns = pCtx->frameSize.nWinColumns;
    size_t nPreLen = strlen(pCtx->sPrefix);
    size_t nSufLen = strlen(pCtx->sSuffix);
    size_t nPctLen = strlen(pCtx->sPercent);

    size_t nExtraPctChars = xstrextra(pCtx->sPercent, nPctLen, 0, NULL, NULL);
    size_t nExtraChars = xstrextra(pCtx->sPrefix, nPreLen, 0, NULL, NULL);
    nExtraChars += xstrextra(pCtx->sSuffix, nSufLen, 0, NULL, NULL);

    xbool_t bHidePercent = (pCtx->bInPercent && pCtx->bInSuffix && nSufLen);
    size_t nUsedLength = nPreLen + nSufLen + XBAR_FRAME_BYTES - nExtraChars;
    if (!bHidePercent) nUsedLength += nPctLen - nExtraPctChars;

    pCtx->nBarLength = (nColumns > nUsedLength) ? (nColumns - nUsedLength) : 0;
    pCtx->nBarUsed = pCtx->nBarLength * (size_t)floor(pCtx->fPercent) / 100;
    return bHidePercent;
}

size_t XProgBar_GetOutputAdv(xcli_bar_t *pCtx, char *pDst, size_t nSize, char *pProgress, xbool_t bHidePct)
{
    size_t nPosit = 0, nChars = 0;
    char sProgress[XLINE_MAX];
    char sSpaces[XLINE_MAX];

    size_t i, nProgLen = 0;
    xstrnul(sProgress);
    xstrnul(sSpaces);

    if (pProgress != NULL) nProgLen = xstrncpy(sProgress, sizeof(sProgress), pProgress);
    if (pCtx->nBarLength) xstrextra(pProgress, nProgLen, pCtx->nBarLength, &nChars, &nPosit);

    sProgress[nPosit] = XSTR_NUL;
    xstrncat(sProgress, sizeof(sProgress), "%s", XSTR_FMT_RESET);

    if (pCtx->nBarLength)
    {
        if (pProgress == NULL)
        {
            for (i = 0; i < pCtx->nBarUsed; i++)
                nChars += xstrncat(sProgress, sizeof(sProgress), "%c", pCtx->cLoader);

            if (pCtx->fPercent > 0. && pCtx->fPercent < 100.)
                nChars += xstrncat(sProgress, sizeof(sProgress), "%c", pCtx->cCursor);
        }

        if (nChars < pCtx->nBarLength)
        {
            size_t nUnused = pCtx->nBarLength - nChars;
            for (i = 0; i < nUnused; i++) xstrncat(sSpaces, sizeof(sSpaces), "%c", pCtx->cEmpty);
        }
    }

    if (bHidePct)
    {
        return xstrncpyf(pDst, nSize, "%s%s%c%s%s%s%s%s%c%s",
            pCtx->sPrefix, XSTR_FMT_BOLD, pCtx->cStart, XSTR_FMT_RESET, sProgress,
            sSpaces, pCtx->sSuffix, XSTR_FMT_BOLD, pCtx->cEnd, XSTR_FMT_RESET);
    }
    else if (pCtx->bInPercent)
    {
        return xstrncpyf(pDst, nSize, "%s%s%c%s%s%s%s%s%c%s%s",
            pCtx->sPrefix, XSTR_FMT_BOLD, pCtx->cStart, XSTR_FMT_RESET, sProgress, sSpaces,
            pCtx->sPercent, XSTR_FMT_BOLD, pCtx->cEnd, XSTR_FMT_RESET, pCtx->sSuffix);
    }
    else if (pCtx->bInSuffix)
    {
        return xstrncpyf(pDst, nSize, "%s%s%c%s%s%s%s%s%c%s %s",
            pCtx->sPrefix, XSTR_FMT_BOLD, pCtx->cStart, XSTR_FMT_RESET, sProgress, sSpaces,
            pCtx->sSuffix, XSTR_FMT_BOLD, pCtx->cEnd, XSTR_FMT_RESET, pCtx->sPercent);
    }

    return xstrncpyf(pDst, nSize, "%s%s%c%s%s%s%s%c%s %s%s",
        pCtx->sPrefix, XSTR_FMT_BOLD, pCtx->cStart, XSTR_FMT_RESET, sProgress, sSpaces,
        XSTR_FMT_BOLD, pCtx->cEnd, XSTR_FMT_RESET, pCtx->sPercent, pCtx->sSuffix);
}

size_t XProgBar_GetOutput(xcli_bar_t *pCtx, char *pDst, size_t nSize)
{
    xbool_t bPctOverride = XProgBar_CalculateBounds(pCtx);
    return XProgBar_GetOutputAdv(pCtx, pDst, nSize, NULL, bPctOverride);
}

void XProgBar_Update(xcli_bar_t *pCtx)
{
    if (pCtx->fPercent < 0.)
    {
        XProgBar_MakeMove(pCtx);
        return;
    }

    char sBuffer[XLINE_MAX];
    XProgBar_UpdateWindowSize(pCtx);

    if (XProgBar_GetOutput(pCtx, sBuffer, sizeof(sBuffer)))
        printf("%s\r", sBuffer);

    if (pCtx->fPercent == 100.)
        XProgBar_Finish(pCtx);

    fflush(stdout);
}
