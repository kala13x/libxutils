/*!
 *  @file libxutils/src/sys/cli.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of CLI window operarions.
 */

#include "xstd.h"
#include "cli.h"
#include "str.h"
#include "xtime.h"

#define XBAR_FRAME_BYTES 3
#define XCLI_PERCENT_MAX 4

XSTATUS XCLI_SetInputMode(void *pAttributes)
{
#ifdef __linux__
    struct termios tattr;
    if (!isatty(STDIN_FILENO)) return XSTDERR;

    struct termios *pSavedAttrs = (struct termios *)pAttributes;
    tcgetattr(STDIN_FILENO, pSavedAttrs);
    tcgetattr(STDIN_FILENO, &tattr);

    tattr.c_lflag &= ~(ICANON | ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &tattr);
    return XSTDOK;
#else
    (void)pAttributes;
    return XSTDNON;
#endif
}

XSTATUS XCLI_RestoreAttributes(void *pAttributes)
{
#ifdef __linux__
    if (pAttributes == NULL) return XSTDERR;
    struct termios *pSavedAttrs = (struct termios *)pAttributes;
    tcsetattr(STDIN_FILENO, TCSANOW, pSavedAttrs);
    return XSTDOK;
#else
    (void)pAttributes;
    return XSTDNON;
#endif
}

XSTATUS XCLI_ReadStdin(char *pBuffer, size_t nSize, xbool_t bAsync)
{
    if (pBuffer == NULL || !nSize) return XSTDINV;
    int nLength = XSTDNON;
    pBuffer[0] = XSTR_NUL;

#ifdef __linux__
    if (bAsync)
    {
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags < 0) return XSTDERR;

        if (!(flags & O_NONBLOCK))
            fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    nLength = read(STDIN_FILENO, pBuffer, nSize);
    if (nLength < 0)
    {
        if (errno == EWOULDBLOCK ||
            errno == EAGAIN) return XSTDNON;

        return XSTDERR;
    }
#endif

    if (nSize > 1) pBuffer[nLength] = 0;
    return nLength;
}

XSTATUS XCLI_GetChar(char *pChar, xbool_t bAsync)
{
#ifdef __linux__
    if (pChar == NULL) return XSTDERR;
    return XCLI_ReadStdin(pChar, 1, bAsync);
#else
    (void)pChar;
    return XSTDNON;
#endif
}

XSTATUS XCLI_GetPass(const char *pText, char *pPass, size_t nSize)
{
    if (pPass == NULL || !nSize) return XSTDERR;
    size_t nLength = 0;

#if !defined(_WIN32) && !defined(_WIN64)
    struct termios oflags, nflags;
    int nFD = fileno(stdin);
    if (nFD < 0) return XSTDERR;
    if (tcgetattr(nFD, &oflags)) return XSTDERR;

    nflags = oflags;
    nflags.c_lflag &= ~ECHO;
    nflags.c_lflag |= ECHONL;
    if (pText != NULL)
    {
        printf("%s", pText);
        fflush(stdout);
    }

    if (tcsetattr(nFD, TCSANOW, &nflags)) return XSTDERR;
    char *pRet = fgets(pPass, (int)nSize, stdin);
    if (tcsetattr(nFD, TCSANOW, &oflags)) return XSTDERR;

    if (pRet != NULL)
    {
        nLength = strlen(pPass);
        if (nLength > 0 && pPass[nLength - 1] == '\n')
            pPass[--nLength] = '\0';
    }
#else
    if (pText != NULL)
    {
        printf("%s", pText);
        fflush(stdout);
    }

    char *pRet = fgets(pPass, (int)nSize, stdin);
    if (pRet != NULL)
    {
        nLength = strlen(pPass);
        if (nLength > 0 && pPass[nLength - 1] == '\n')
            pPass[--nLength] = '\0';
    }
#endif

    pPass[nLength] = 0;
    return (XSTATUS)nLength;
}

XSTATUS XCLI_GetInput(const char *pText, char *pInput, size_t nSize, xbool_t bCutNewLine)
{
    XASSERT(pInput, XSTDINV);
    pInput[0] = XSTR_NUL;

    if (pText != NULL)
    {
        printf("%s", pText);
        fflush(stdout);
    }
    char *pRet = fgets(pInput, (int)nSize, stdin);

    XASSERT_RET((pRet != NULL), XSTDERR);
    if (!xstrused(pInput)) return XSTDNON;
    else if (!bCutNewLine) return XSTDOK;

    size_t nLength = strlen(pInput);
    if (pInput[nLength-1] == '\n')
        pInput[--nLength] = '\0';

    return XSTDOK;
}

XSTATUS XCLI_GetWindowSize(xcli_size_t *pSize)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    csbi.srWindow.Right = csbi.srWindow.Left = 0;
    csbi.srWindow.Top = csbi.srWindow.Bottom = 0;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    pSize->nColumns = (size_t)csbi.srWindow.Right - (size_t)csbi.srWindow.Left + 1;
    pSize->nRows = (size_t)csbi.srWindow.Bottom - (size_t)csbi.srWindow.Top + 1;
#else
    struct winsize size;
    size.ws_col = size.ws_row = 0;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
    pSize->nColumns = size.ws_col;
    pSize->nRows = size.ws_row;
#endif

    return (pSize->nColumns && pSize->nRows) ?  XSTDOK : XSTDERR;
}

void XCLIWin_Init(xcli_win_t *pWin, xbool_t bAscii)
{
    XArray_Init(&pWin->lines, NULL, 0, 0);
    pWin->eType = XCLI_RENDER_FRAME;
    pWin->frame.nColumns = 0;
    pWin->frame.nRows = 0;
    pWin->bAscii = bAscii;
}

XSTATUS XCLIWin_UpdateSize(xcli_win_t *pWin)
{
    XSTATUS nStatus = XCLI_GetWindowSize(&pWin->frame);
    if (pWin->eType == XCLI_LINE_BY_LINE &&
        nStatus != XSTDERR) pWin->frame.nRows--;
    return nStatus;
}

XSTATUS XCLIWin_AddLine(xcli_win_t *pWin, char *pLine, size_t nLength)
{
    xcli_size_t *pFrame = &pWin->frame;
    xarray_t *pLines = &pWin->lines;

    if (XCLIWin_UpdateSize(pWin) == XSTDERR) return XSTDERR;
    if (pLines->nUsed >= pFrame->nRows) return XSTDNON;

    if (XArray_AddData(pLines, pLine, nLength) < 0)
    {
        XArray_Clear(pLines);
        return XSTDERR;
    }

    return XSTDOK;
}

XSTATUS XCLIWin_AddLineFmt(xcli_win_t *pWin, const char *pFmt, ...)
{
    xcli_size_t *pFrame = &pWin->frame;
    xarray_t *pLines = &pWin->lines;

    if (XCLIWin_UpdateSize(pWin) == XSTDERR) return XSTDERR;
    if (pLines->nUsed >= pFrame->nRows) return XSTDNON;

    size_t nLength = 0;
    char *pDest = NULL;

    XSTRCPYFMT(pDest, pFmt, &nLength);
    if (pDest == NULL) return XSTDERR;

    if (XArray_PushData(pLines, pDest, nLength) < 0)
    {
        XArray_Clear(pLines);
        free(pDest);
        return XSTDERR;
    }

    return XSTDOK;
}

XSTATUS XCLIWin_AddEmptyLine(xcli_win_t *pWin)
{
    if (XCLIWin_UpdateSize(pWin) == XSTDERR) return XSTDERR;
    size_t nColumns = pWin->frame.nColumns;
    char emptyLine[XLINE_MAX];

    size_t i, nSpaces = XSTD_MIN(nColumns, sizeof(emptyLine) - 1);
    for (i = 0; i < nSpaces; i++) emptyLine[i] = XSTR_SPACE_CHAR;

    emptyLine[i] = XSTR_NUL;
    return XCLIWin_AddLine(pWin, emptyLine, i);
}

XSTATUS XCLIWin_AddAligned(xcli_win_t *pWin, const char *pInput, const char *pFmt, uint8_t nAlign)
{
    if (XCLIWin_UpdateSize(pWin) == XSTDERR) return XSTDERR;
    size_t nColumns = pWin->frame.nColumns;

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

    if (pFmt == NULL) return XCLIWin_AddLineFmt(pWin, "%s%s%s", sPreBuf, pInput, sAfterBuf);
    return XCLIWin_AddLineFmt(pWin, "%s%s%s%s%s", pFmt, sPreBuf, pInput, sAfterBuf, XSTR_FMT_RESET);
}

int XCLIWin_ClearScreen(xbool_t bAscii)
{
    int nRet = XSTDNON;

    if (bAscii)
    {
        printf(XSTR_SCREEN_CLEAR);
        fflush(stdout);
        return nRet;
    }

#if !defined(_WIN32) && !defined(_WIN64)
    nRet = system("clear");
#else
    nRet = system("cls");
#endif

    return nRet;
}

XSTATUS XCLIWin_RenderLine(xcli_win_t *pWin, xbyte_buffer_t *pLine, xarray_data_t *pArrData)
{
    if (XCLIWin_UpdateSize(pWin) == XSTDERR) return XSTDERR;
    size_t nChars = 0, nMaxSize = pWin->frame.nColumns;

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

XSTATUS XCLIWin_GetFrame(xcli_win_t *pWin, xbyte_buffer_t *pFrameBuff)
{
    if (pWin == NULL || pFrameBuff == NULL) return XSTDERR;
    XByteBuffer_Init(pFrameBuff, XSTDNON, XFALSE);

    while (pWin->lines.nUsed < pWin->frame.nRows)
        if (XCLIWin_AddEmptyLine(pWin) < 0) return XSTDERR;

    xcli_size_t *pFrame = &pWin->frame;
    xarray_t *pLines = &pWin->lines;
    size_t i, nRows = XSTD_MIN(pFrame->nRows, pLines->nUsed);

    for (i = 0; i < nRows; i++)
    {
        xarray_data_t *pData = XArray_Get(pLines, i);
        if (pData == NULL) continue;

        xbyte_buffer_t lineBuff;
        XByteBuffer_Init(&lineBuff, XSTDNON, XFALSE);

        if (XCLIWin_RenderLine(pWin, &lineBuff, pData) < 0)
        {
            XByteBuffer_Clear(pFrameBuff);
            XArray_Clear(pLines);
            return XSTDERR;
        }

        if (XByteBuffer_AddBuff(pFrameBuff, &lineBuff) < 0)
        {
            XByteBuffer_Clear(pFrameBuff);
            XArray_Clear(pLines);
            return XSTDERR;
        }
    }

    return XSTDOK;
}

XSTATUS XCLIWin_Display(xcli_win_t *pWin)
{
    XSTATUS nStatus = XSTDNON;
    size_t nLineCount = 0;

    if (pWin->eType == XCLI_LINE_BY_LINE)
    {
        XCLIWin_ClearScreen(pWin->bAscii);
        xarray_t *pLines = &pWin->lines;
        size_t i, nWinRows = pWin->frame.nRows;
        size_t nRows = XSTD_MIN(nWinRows, pLines->nUsed);

        for (i = 0; i < nRows; i++)
        {
            xarray_data_t *pData = XArray_Get(pLines, i);
            if (pData == NULL) continue;

            xbyte_buffer_t lineBuff;
            if (XCLIWin_RenderLine(pWin, &lineBuff, pData) < 0)
            {
                XArray_Clear(pLines);
                return XSTDERR;
            }

            printf("%s\n", (char*)lineBuff.pData);
            nLineCount++;
        }

        for (i = nLineCount; i < nWinRows; i++) printf("\n");

        fflush(stdout);
        nStatus = XSTDOK;
    }
    else if (pWin->eType == XCLI_RENDER_FRAME)
    {
        xbyte_buffer_t frameBuf;
        nStatus = XCLIWin_GetFrame(pWin, &frameBuf);
        if (nStatus == XSTDERR) return nStatus;

        XCLIWin_ClearScreen(pWin->bAscii);
        printf("%s\r", (char*)frameBuf.pData);
        fflush(stdout);

        XByteBuffer_Clear(&frameBuf);
        nStatus = XSTDOK;
    }

    return nStatus;
}

XSTATUS XCLIWin_Flush(xcli_win_t *pWin)
{
    XSTATUS nStatus = XCLIWin_Display(pWin);
    XArray_Clear(&pWin->lines);
    return nStatus;
}

void XCLIWin_Destroy(xcli_win_t *pWin)
{
    xarray_t *pArr = &pWin->lines;
    XArray_Destroy(pArr);
}

/////////////////////////////////////////////////////////////////////////
// Implementation of CLI progress bar
/////////////////////////////////////////////////////////////////////////

XSTATUS XProgBar_UpdateWindowSize(xcli_bar_t *pCtx)
{
    xcli_size_t *pFrame = &pCtx->frame;
    return XCLI_GetWindowSize(pFrame);
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
    size_t nColumns = pCtx->frame.nColumns;

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

    size_t nColumns = pCtx->frame.nColumns;
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
