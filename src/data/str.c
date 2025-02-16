/*!
 *  @file libxutils/src/data/str.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief This source includes string operations.
 */

#ifdef _XUTILS_USE_GNU
#define _GNU_SOURCE
#endif

#include "str.h"

#define XSTR_KEYMAP_SIZE 33

static const char *g_keyMapGe[XSTR_KEYMAP_SIZE] =
    {
        "ა", "ბ", "გ", "დ", "ე", "ვ", "ზ", "თ", "ი", 
        "კ", "ლ", "მ", "ნ", "ო", "პ", "ჟ", "რ", 
        "ს", "ტ", "უ", "ფ", "ქ", "ღ", "ყ", "შ", 
        "ჩ", "ც", "ძ", "წ", "ჭ", "ხ", "ჯ", "ჰ"
    };


static const char *g_keyMapEn[XSTR_KEYMAP_SIZE] =
    {
        "a", "b", "g", "d", "e", "v", "z", "T", "i",
        "k", "l", "m", "n", "o", "p", "J", "r",
        "s", "t", "u", "f", "q", "R", "y", "S",
        "C", "c", "Z", "w", "W", "x", "j", "h"
    };

/////////////////////////////////////////////////////////////////////////
// C string functions
/////////////////////////////////////////////////////////////////////////

char *xstrtok(char* pString, const char* pDelimiter, char **pContext)
{
#ifdef _WIN32
    return strtok_s(pString, pDelimiter, pContext);
#else
    return strtok_r(pString, pDelimiter, pContext);
#endif
}

size_t xstrarglen(const char *pFmt, va_list args)
{
    va_list locArgs;
#ifdef va_copy
    va_copy(locArgs, args);
#else
    memcpy(&locArgs, &args, sizeof(va_list));
#endif

    int nLength = vsnprintf(0, 0, pFmt, locArgs);

#ifdef _XUTILS_USE_EXT
    if (nLength > 0)
    {
        va_end(locArgs);
        return nLength;
    }

    int nFmtLen = nLength = strlen(pFmt);
    int nOffset = 0;

    while (nOffset < nFmtLen)
    {
        int nPosit = xstrsrc(&pFmt[nOffset], "%");
        if (nPosit < 0) break;

        nOffset += nPosit + 1;
        if (nOffset >= nFmtLen) break;

        char sBuff[XSTR_INT_ARG_MAX];
        uint8_t nBuffPosit = 0;

        while (isdigit(pFmt[nOffset]) &&
            nOffset < nFmtLen &&
            nBuffPosit < sizeof(sBuff) - 1)
                sBuff[nBuffPosit++] = pFmt[nOffset++];

        sBuff[nBuffPosit] = XSTR_NUL;
        if (nBuffPosit) nLength += atoi(sBuff);

        if (pFmt[nOffset] == '%')
            if (++nOffset >= nFmtLen) break;

        switch(pFmt[nOffset])
        {
            case '%':
                nLength++;
                break;
            case 's':
                const char* pArg = va_arg(locArgs,const char*);
                if (pArg != NULL) nLength += strlen(pArg);
                break;
            case 'f':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
                nLength += XSTR_DOUBLE_ARG_MAX;
                va_arg(locArgs,double);
                break;
            case 'd':
            case 'i':
            case 'o':
            case 'u':
            case 'x':
            case 'X':
            case 'c':
                nLength += XSTR_INT_ARG_MAX;
                va_arg(locArgs,int);
                break;
            default:
                nLength += XSTR_INT_ARG_MAX;
                va_arg(locArgs,void*);
                break;
        }
    }
#endif

    va_end(locArgs);
    return nLength;
}

char* xstralloc(size_t nSize)
{
    if (!nSize) return NULL;
    char *pStr = (char*)malloc(nSize);
    if (pStr != NULL) pStr[0] = XSTR_NUL;
    return pStr;
}

size_t xstrrand(char *pDst, size_t nSize, size_t nLength, xbool_t bUpper, xbool_t bNumbers)
{
    if (pDst == NULL || !nSize || !nLength) return XSTDNON;
    const char *pChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char *pNums = "0123456789";

    size_t i, nCharCount = XSTD_MIN(nLength, nSize - 1);
    int nLimit = bUpper ? XSTR_LETTERS : XSTR_LETTERS / 2;

    for (i = 0; i < nCharCount; i++)
    {
        if (bNumbers && rand() % 2)
        {
            int nKey = rand() % 10;
            pDst[i] = pNums[nKey];
        }
        else
        {
            int nKey = rand() % nLimit;
            pDst[i] = pChars[nKey];
        }
    }

    pDst[nCharCount] = XSTR_NUL;
    return nCharCount;
}

xbool_t xstrncmp(const char *pStr, const char *pCmp, size_t nCmpLen)
{
    XASSERT_RET((pStr && pCmp && nCmpLen), XFALSE);
    int nDifferent = strncmp(pStr, pCmp, nCmpLen);
    return nDifferent ? XFALSE : XTRUE;
}

xbool_t xstrncmpn(const char *pStr, size_t nStrLen, const char *pCmp, size_t nCmpLen)
{
    XASSERT_RET((pStr && pCmp && nStrLen), XFALSE);
    XASSERT_RET((nStrLen == nCmpLen), XFALSE);
    int nDiff = strncmp(pStr, pCmp, nCmpLen);
    return nDiff ? XFALSE : XTRUE;
}

xbool_t xstrcmp(const char *pStr, const char *pCmp)
{
    XASSERT_RET((xstrused(pStr) && xstrused(pCmp)), XFALSE);
    return xstrncmpn(pStr, strlen(pStr), pCmp, strlen(pCmp));
}

size_t xstrnfill(char *pDst, size_t nSize, size_t nLength, char cFill)
{
    if (pDst == NULL || !nSize) return XSTDNON;

    size_t i, nFillLength = XSTD_MIN(nLength, nSize - 1);
    for (i = 0; i < nFillLength; i++) pDst[i] = cFill;

    pDst[nFillLength] = XSTR_NUL;
    return nFillLength;
}

char *xstrfill(size_t nLength, char cFill)
{
    static char sRetVal[XSTR_MAX];
    size_t nLen, i = 0;
    xstrnul(sRetVal);

    nLen = XSTD_MIN(nLength, sizeof(sRetVal) - 1);
    for (i = 0; i < nLen; i++) sRetVal[i] = ' ';

    sRetVal[i] = '\0';
    return sRetVal;
}

int xstrncpyarg(char *pDest, size_t nSize, const char *pFmt, va_list args)
{
    if (pDest == NULL || !nSize) return XSTDERR;
    size_t nLength = 0;

    int nBytes = vsnprintf(pDest, nSize, pFmt, args);
    if (nBytes > 0) nLength = XSTD_MIN((size_t)nBytes, nSize - 1);

    pDest[nLength] = XSTR_NUL;
    return (int)nLength;
}

char* xstracpyargs(const char *pFmt, va_list args, size_t *nSize)
{
    size_t nLength = xstrarglen(pFmt, args);
    if (!nLength) return NULL;

    char *pDest = (char*)malloc(++nLength);
    if (pDest == NULL) return NULL;

    *nSize = xstrncpyarg(pDest, nLength, pFmt, args);
    if (*nSize <= 0 && pDest)
    {
        free(pDest);
        return NULL;
    }

    return pDest;
}

char* xstrpcpyargs(xpool_t *pPool, const char *pFmt, va_list args, size_t *nSize)
{
    size_t nLength = xstrarglen(pFmt, args);
    if (!nLength) return NULL;

    char *pDest = (char*)xalloc(pPool, ++nLength);
    if (pDest == NULL) return NULL;

    *nSize = xstrncpyarg(pDest, nLength, pFmt, args);
    if (*nSize <= 0 && pDest)
    {
        xfree(pPool, pDest);
        return NULL;
    }

    return pDest;
}

char* xstracpyarg(const char *pFmt, va_list args)
{
    size_t nDummy = 0;
    return xstracpyargs(pFmt, args, &nDummy);
}

char* xstracpy(const char *pFmt, ...)
{
    va_list args;
    va_start(args, pFmt);
    char *pDest = xstracpyarg(pFmt, args);
    va_end(args);
    return pDest;
}

char* xstracpyn(size_t *nSize, const char *pFmt, ...)
{
    *nSize = 0;
    va_list args;
    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, nSize);
    va_end(args);
    return pDest;
}

char* xstrxcpy(const char *pFmt, ...)
{
#ifdef _XUTILS_USE_GNU
    va_list args;
    char *pDest = NULL;
    int nLength = 0;

    va_start(args, pFmt);
    nLength = vasprintf(&pDest, pFmt, args);
    va_end(args);

    if (nLength <= 0 && pDest)
    {
        free(pDest);
        return NULL;
    }

    pDest[nLength] = XSTR_NUL;
    return pDest;
#endif
    (void)pFmt;
    return NULL;
}

size_t xstrxcpyf(char **pDst, const char *pFmt, ...)
{
#ifdef _XUTILS_USE_GNU
    va_list args;
    int nBytes = 0;

    va_start(args, pFmt);
    nBytes = vasprintf(pDst, pFmt, args);
    va_end(args);

    if (nBytes <= 0 && pDst)
    {
        free(pDst);
        pDst = NULL;
        return 0;
    }

    pDst[nBytes] = XSTR_NUL;
    return nBytes;
#endif
    (void)pDst;
    (void)pFmt;
    return 0;
}

size_t xstrncpy(char *pDst, size_t nSize, const char* pSrc)
{
    if (pDst == NULL || !nSize || !xstrused(pSrc)) return 0;
    size_t nCopySize = strnlen(pSrc, nSize - 1);
    if (nCopySize) memcpy(pDst, pSrc, nCopySize);
    pDst[nCopySize] = XSTR_NUL;
    return nCopySize;
}

size_t xstrncpys(char *pDst, size_t nDstSize, const char *pSrc, size_t nSrcLen)
{
    if (pDst == NULL || pSrc == NULL || !nDstSize) return 0;
    size_t nCopySize = XSTD_MIN(nSrcLen, nDstSize - 1);
    if (nCopySize) memcpy(pDst, pSrc, nCopySize);
    pDst[nCopySize] = XSTR_NUL;
    return nCopySize;
}

size_t xstrncpyf(char *pDst, size_t nSize, const char *pFmt, ...)
{
    int nBytes = 0;
    va_list args;
    va_start(args, pFmt);
    nBytes = xstrncpyarg(pDst, nSize, pFmt, args);
    va_end(args);
    return nBytes;
}

size_t xstrncpyfl(char *pDst, size_t nSize, size_t nFLen, char cFChar, const char* pFmt, ...)
{
    size_t nBytes = 0;
    va_list args;

    va_start(args, pFmt);
    nBytes = xstrncpyarg(pDst, nSize, pFmt, args);
    va_end(args);

    nSize -= nBytes;
    nFLen -= nBytes;
    pDst += nBytes;
    nBytes += xstrnfill(pDst, nSize, nFLen, cFChar);

    return nBytes;
}

size_t xstrnlcpyf(char *pDst, size_t nSize, size_t nFLen, char cFChar, const char* pFmt, ...)
{
    if (pDst == NULL || !nSize) return 0;
    if (nFLen > nSize) nFLen = nSize - 1;

    size_t nBytes = 0;
    va_list args;

    va_start(args, pFmt);
    nBytes = xstrarglen(pFmt, args);

    if (!nBytes)
    {
        va_end(args);
        return nBytes;
    }
    else if (nBytes >= nFLen)
    {
        nBytes = xstrncpyarg(pDst, nBytes, pFmt, args);
        va_end(args);
        return nBytes;
    }

    pDst[0] = XSTR_NUL;
    nFLen -= nBytes;
    nBytes = 0;

    nBytes += xstrnfill(pDst, nSize, nFLen, cFChar);
    nBytes = xstrncpyarg(&pDst[nBytes], nSize - nBytes, pFmt, args);

    va_end(args);
    return nBytes + nFLen;
}

size_t xstrisextra(const char *pOffset)
{
    if (!strncmp(pOffset, XSTR_CLR_LIGHT_MAGENTA, 7) ||
        !strncmp(pOffset, XSTR_CLR_LIGHT_YELLOW, 7) ||
        !strncmp(pOffset, XSTR_CLR_LIGHT_WHITE, 7) ||
        !strncmp(pOffset, XSTR_CLR_LIGHT_GREEN, 7) ||
        !strncmp(pOffset, XSTR_CLR_LIGHT_CYAN, 7) ||
        !strncmp(pOffset, XSTR_CLR_LIGHT_BLUE, 7) ||
        !strncmp(pOffset, XSTR_CLR_LIGHT_RED, 7)) return 7;

    if (!strncmp(pOffset, XSTR_CLR_MAGENTA, 5) ||
        !strncmp(pOffset, XSTR_CLR_YELLOW, 5) ||
        !strncmp(pOffset, XSTR_CLR_WHITE, 5) ||
        !strncmp(pOffset, XSTR_CLR_GREEN, 5) ||
        !strncmp(pOffset, XSTR_CLR_CYAN, 5) ||
        !strncmp(pOffset, XSTR_CLR_BLUE, 5) ||
        !strncmp(pOffset, XSTR_CLR_RED, 5)) return 5;

    if (!strncmp(pOffset, XSTR_BACK_MAGENTA, 5) ||
        !strncmp(pOffset, XSTR_BACK_YELLOW, 5) ||
        !strncmp(pOffset, XSTR_BACK_WHITE, 5) ||
        !strncmp(pOffset, XSTR_BACK_GREEN, 5) ||
        !strncmp(pOffset, XSTR_BACK_BLACK, 5) ||
        !strncmp(pOffset, XSTR_BACK_CYAN, 5) ||
        !strncmp(pOffset, XSTR_BACK_BLUE, 5) ||
        !strncmp(pOffset, XSTR_BACK_RED, 5)) return 5;

    if (!strncmp(pOffset, XSTR_FMT_HIGHLITE, 4) ||
        !strncmp(pOffset, XSTR_FMT_ITALIC, 4) ||
        !strncmp(pOffset, XSTR_FMT_ULINE, 4) ||
        !strncmp(pOffset, XSTR_FMT_FLICK, 4) ||
        !strncmp(pOffset, XSTR_FMT_BLINK, 4) ||
        !strncmp(pOffset, XSTR_FMT_CROSS, 4) ||
        !strncmp(pOffset, XSTR_FMT_RESET, 4) ||
        !strncmp(pOffset, XSTR_FMT_BOLD, 4) ||
        !strncmp(pOffset, XSTR_FMT_HIDE, 4) ||
        !strncmp(pOffset, XSTR_FMT_DIM, 4)) return 4;

    return 0;
}

size_t xstrextra(const char *pStr, size_t nLength, size_t nMaxChars, size_t *pChars, size_t *pPosit)
{
    if (pStr == NULL || !nLength) return XSTDNON;
    size_t nPosit, nChars = 0, nExtra = 0;

    for (nPosit = 0; nPosit < nLength; nPosit++)
    {
        if (pPosit != NULL) *pPosit = nPosit;
        if (nMaxChars && nChars >= nMaxChars) break;
        const char *pOffset = &pStr[nPosit];

        if (*pOffset == '\x1B')
        {
            size_t nFound = xstrisextra(pOffset);
            if (nFound)
            {
                nPosit += nFound - 1;
                nExtra += nFound;
                continue;
            }
        }

        nChars++;
    }

    if (pChars != NULL) *pChars = nChars;
    return nExtra;
}

size_t xstrncat(char *pDst, size_t nSize, const char *pFmt, ...)
{
    if (pDst == NULL || !nSize) return 0;
    size_t nUsed = strnlen(pDst, nSize - 1);
    if (nSize <= nUsed) return 0;

    size_t nAvail = nSize - nUsed;
    char *pBuffer = &pDst[nUsed];
    int nLength = 0;

    va_list args;
    va_start(args, pFmt);
    nLength = xstrncpyarg(pBuffer, nAvail, pFmt, args);
    va_end(args);

    return nLength;
}

size_t xstrncatf(char *pDst, size_t nAvail, const char *pFmt, ...)
{
    if (pDst == NULL || !nAvail) return 0;
    size_t nUsed = strlen(pDst);
    char *pBuffer = &pDst[nUsed];

    va_list args;
    va_start(args, pFmt);
    int nLength = xstrncpyarg(pBuffer, nAvail, pFmt, args);
    va_end(args);

    return nAvail - nLength;
}

size_t xstrncatsf(char *pDst, size_t nSize, size_t nAvail, const char *pFmt, ...)
{
    if (pDst == NULL || !nSize || !nAvail) return 0;
    size_t nUsed = nSize - nAvail;
    char *pBuffer = &pDst[nUsed];

    va_list args;
    va_start(args, pFmt);
    int nLength = xstrncpyarg(pBuffer, nAvail, pFmt, args);
    va_end(args);

    return nAvail - nLength;
}

size_t xstrnclr(char *pDst, size_t nSize, const char* pClr, const char* pStr, ...) 
{
    if (pDst == NULL || !nSize) return 0;
    char sBuffer[XSTR_STACK];
    char *pBuffer = &sBuffer[0];
    uint8_t nAlloc = 0;

    if (nSize > XSTR_STACK)
    {
        pBuffer = (char*)malloc(nSize);
        if (pBuffer == NULL) return 0;
        nAlloc = 1;
    }

    va_list args;
    va_start(args, pStr);
    xstrncpyarg(pBuffer, nSize, pStr, args);
    va_end(args);

    size_t nLength = xstrncpyf(pDst, nSize, "%s%s%s", 
        pClr, pBuffer, XSTR_FMT_RESET);

    if (nAlloc) free(pBuffer);
    return nLength;
}

size_t xstrcase(char *pSrc, xstr_case_t eCase)
{
    if (xstrused(pSrc)) return 0;
    size_t i, nCopyLength = strlen(pSrc);
    if (!nCopyLength) return 0;

    for (i = 0; i < nCopyLength; i++)
    {
        switch (eCase)
        {
            case XSTR_LOWER:
                pSrc[i] = (char)tolower(pSrc[i]);
                break;
            case XSTR_UPPER:
                pSrc[i] = (char)toupper(pSrc[i]);
                break;
            default:
                return 0;
        }
    }

    pSrc[nCopyLength] = 0;
    return nCopyLength;
}

size_t xstrncases(char* pDst, size_t nSize, xstr_case_t eCase, const char *pSrc, size_t nSrcLen)
{
    if (pDst == NULL || pSrc == NULL || !nSize) return 0;
    size_t i, nCopyLength = XSTD_MIN(nSrcLen, nSize - 1);

    for (i = 0; i < nCopyLength; i++)
    {
        switch (eCase)
        {
            case XSTR_LOWER:
                pDst[i] = (char)tolower(pSrc[i]);
                break;
            case XSTR_UPPER:
                pDst[i] = (char)toupper(pSrc[i]);
                break;
            default:
                pDst[i] = pSrc[i];
                break;
        }
    }

    pDst[nCopyLength] = 0;
    return nCopyLength;
}

size_t xstrncase(char* pDst, size_t nSize, xstr_case_t eCase, const char *pSrc)
{
    size_t nCopyLength = strnlen(pSrc, nSize - 1);
    return xstrncases(pDst, nSize, eCase, pSrc, nCopyLength);
}

char* xstracase(const char *pSrc, xstr_case_t eCase)
{
    size_t nCopyLength = strlen(pSrc);
    if (!nCopyLength) return NULL;

    size_t nDstSize = nCopyLength + 1;
    char *pDst = (char*)malloc(nDstSize);
    if (pDst == NULL) return NULL;

    xstrncases(pDst, nDstSize, eCase, pSrc, nCopyLength);
    return pDst;
}

char* xstracasen(const char *pSrc, xstr_case_t eCase, size_t nLength)
{
    size_t nCopyLength = strnlen(pSrc, nLength);
    if (!nCopyLength) return NULL;

    size_t nDstSize = nCopyLength + 1;
    char *pDst = (char*)malloc(nDstSize);
    if (pDst == NULL) return NULL;

    xstrncases(pDst, nDstSize, eCase, pSrc, nCopyLength);
    return pDst;
}

int xstrsrc(const char *pStr, const char *pSrc)
{
    if (!xstrused(pStr) || !xstrused(pSrc)) return XSTDERR;
    const char *pEndPosition = strstr(pStr, pSrc);
    if (pEndPosition == NULL) return XSTDERR;
    return (int)(pEndPosition - pStr);
}

int xstrnsrc(const char *pStr, size_t nLen, const char *pSrc, size_t nPos)
{
    if (pStr == NULL || pSrc == NULL ||
        nPos >= nLen) return XSTDERR;
    return xstrsrc(&pStr[nPos], pSrc);
}

int xstrsrcb(const char *pStr, size_t nLength, const char *pSrc)
{
    size_t pSearchLen = strlen(pSrc);
    int nPosit = XSTDERR;

#ifndef _WIN32
    void *pOffset = memmem(pStr, nLength, pSrc, pSearchLen);
    if (pOffset != NULL) nPosit = (int)((char*)pOffset - pStr);
#else
    nPosit = xstrsrc((char*)pStr, pSrc);
#endif

    return nPosit;
}

int xstrsrcp(const char *pStr, const char *pSrc, size_t nPos)
{
    if (pStr == NULL || pSrc == NULL) return XSTDERR;
    return xstrnsrc(pStr, strlen(pStr), pSrc, nPos);
}

int xstrntokn(char *pDst, size_t nSize, const char *pStr, size_t nLen, size_t nPosit, const char *pDlmt)
{
    if (pDst != NULL) pDst[0] = XSTR_NUL;
    if (nPosit >= nLen) return XSTDERR;
    size_t nDlmtLen = xstrused(pDlmt) ? strlen(pDlmt) : 0;

    int nOffset = xstrsrc(&pStr[nPosit], pDlmt);
    if (nOffset < 0)
    {
        xstrncpy(pDst, nSize, &pStr[nPosit]);
        return 0;
    }
    else if (!nOffset)
    {
        if (pDst != NULL) pDst[0] = '\0';
        return (int)(nPosit + nDlmtLen);
    }

    xstrncpys(pDst, nSize, &pStr[nPosit], nOffset);
    return (int)(nPosit + nOffset + nDlmtLen);
}

int xstrntok(char *pDst, size_t nSize, const char *pStr, size_t nPosit, const char *pDlmt)
{
    if (pDst != NULL) pDst[0] = XSTR_NUL;
    if (nPosit >= strlen(pStr)) return XSTDERR;
    size_t nDlmtLen = xstrused(pDlmt) ? strlen(pDlmt) : 0;

    int nOffset = xstrsrc(&pStr[nPosit], pDlmt);
    if (nOffset < 0)
    {
        xstrncpy(pDst, nSize, &pStr[nPosit]);
        return 0;
    }
    else if (!nOffset)
    {
        pDst[0] = '\0';
        return (int)(nPosit + nDlmtLen);
    }

    xstrncpys(pDst, nSize, &pStr[nPosit], nOffset);
    return (int)(nPosit + nOffset + nDlmtLen);
}

size_t xstrncuts(char *pDst, size_t nSize, const char *pSrc, const char *pStart, const char *pEnd)
{
    pDst[0] = XSTR_NUL;
    if (pStart == NULL)
    {
        if (pEnd == NULL) return XSTDNON;
        int nStatus = xstrntok(pDst, nSize, pSrc, 0, pEnd);
        return (nStatus >= 0) ? strnlen(pDst, nSize - 1) : XSTDNON;
    }

    int nPosit = xstrsrc(pSrc, pStart);
    if (nPosit < 0) return XSTDNON;
    nPosit += (int)strlen(pStart);

    int nStatus = xstrntok(pDst, nSize, pSrc, nPosit, pEnd);
    return (nStatus >= 0) ? strnlen(pDst, nSize - 1) : XSTDNON;
}

char* xstrcut(char *pData, const char *pStart, const char *pEnd)
{
    if (pStart == NULL)
    {
        if (pEnd == NULL) return NULL;
        char *pSavePtr = NULL;
        return xstrtok(pData, pEnd, &pSavePtr);
    }

    char *pLine = strstr(pData, pStart);
    if (pLine != NULL)
    {
        pLine += strlen(pStart);
        if (pEnd == NULL) return pLine;
        return xstrcut(pLine, NULL, pEnd);
    }

    return NULL;
}

size_t xstrncut(char *pDst, size_t nDstSize, const char *pSrc, size_t nPosit, size_t nSize)
{
    if (pSrc == NULL || !nDstSize || !nSize) return 0;
    size_t nDataLen = strlen(pSrc);
    if (!nDataLen || nPosit >= nDataLen) return 0;

    size_t nCopySize = XSTD_MIN(nSize, nDstSize);
    size_t nPartSize = nDataLen - nPosit;

    nCopySize = XSTD_MIN(nCopySize, nPartSize);
    xstrncpys(pDst, nDstSize, &pSrc[nPosit], nCopySize);
    return nCopySize;
}

char* xstracut(const char *pSrc, size_t nPosit, size_t nSize)
{
    if (pSrc == NULL || !nSize) return NULL;
    size_t nDataLen = strlen(pSrc);
    if (!nDataLen || nPosit >= nDataLen) return NULL;

    char *pDst = (char*)malloc(nSize + 1);
    if (pDst == NULL) return NULL;

    size_t nPartSize = nDataLen - nPosit;
    size_t nCopySize = XSTD_MIN(nSize, nPartSize);
    xstrncpys(pDst, nSize + 1, &pSrc[nPosit], nCopySize);
    return pDst;
}

size_t xstrnrm(char *pStr, size_t nPosit, size_t nSize)
{
    if (pStr == NULL || !nSize) return 0;
    int nStrLen = (int)strlen(pStr);

    if (nStrLen <= 0 || nPosit >= (size_t)nStrLen) return 0;
    nSize = ((nPosit + nSize) > (size_t)nStrLen) ? nStrLen - nPosit : nSize;

    size_t nTailOffset = nPosit + nSize;
    if (nTailOffset >= (size_t)nStrLen)
    {
        nStrLen = (int)nPosit;
        pStr[nStrLen] = XSTR_NUL;
        return nStrLen;
    }

    size_t nTailSize = nStrLen - nTailOffset;
    const char *pTail = &pStr[nTailOffset];
    memmove(&pStr[nPosit], pTail, nTailSize);

    nStrLen -= (int)nSize;
    pStr[nStrLen] = XSTR_NUL;
    return nStrLen;
}

char *xstrrep(const char *pOrig, const char *pRep, const char *pWith)
{
    size_t nOrigLen = strlen(pOrig);
    size_t nWithLen = strlen(pWith);
    size_t nRepLen = strlen(pRep);
    int nNext = 0, nCount = 0;

    while ((nNext = xstrntokn(NULL, 0, pOrig, nOrigLen, nNext, pRep)) > 0) nCount++;
    size_t nSpaceAvail = ((nWithLen - nRepLen) * nCount) + nOrigLen + 1;

    char *pResult = (char*)malloc(nSpaceAvail);
    XASSERT(pResult, NULL);
    char *pOffset = pResult;

    while (nCount--)
    {
        int nFirstPartLen = xstrsrc(pOrig, pRep);
        if (nFirstPartLen < 0) break;

        if (nFirstPartLen)
        {
            xstrncpys(pOffset, nSpaceAvail, pOrig, nFirstPartLen);
            nSpaceAvail -= nFirstPartLen;
            pOffset += nFirstPartLen;
        }

        xstrncpys(pOffset, nSpaceAvail, pWith, nWithLen);
        nSpaceAvail -= nWithLen;
        pOffset += nWithLen;
        pOrig += nFirstPartLen + nRepLen;
    }

    xstrncpyf(pOffset, nSpaceAvail, "%s", pOrig);
    return pResult;
}

int xstrnrep(char *pDst, size_t nSize, const char *pOrig, const char *pRep, const char *pWith)
{
    XASSERT(pDst, XSTDINV);
    char *pOffset = pDst;

    size_t nOrigLen = strlen(pOrig);
    size_t nWithLen = strlen(pWith);
    size_t nRepLen = strlen(pRep);

    int nAvail = (int)nSize;   
    int nNext = 0, nCount = 0;

    while ((nNext = xstrntokn(NULL, 0, pOrig, nOrigLen, nNext, pRep)) > 0) nCount++;

    while (nCount--)
    {
        int nFirstPartLen = xstrsrc(pOrig, pRep);
        if (nFirstPartLen < 0) break;

        if (nFirstPartLen)
        {
            xstrncpys(pOffset, nAvail, pOrig, nFirstPartLen);
            pOffset += nFirstPartLen;
            nAvail -= nFirstPartLen;
            if (nAvail <= 0) return XSTDNON;
        }

        xstrncpys(pOffset, nAvail, pWith, nWithLen);
        pOrig += (size_t)nFirstPartLen + nRepLen;
        nAvail -= (int)nWithLen;
        pOffset += (int)nWithLen;
        if (nAvail <= 0) return XSTDNON;
    }

    xstrncpyf(pOffset, nAvail, "%s", pOrig);
    return XSTDOK;
}

char *xstrdup(const char *pStr)
{
    if (pStr == NULL) return NULL;
    size_t nLength = strlen(pStr);

    char *pDst = (char*)malloc(++nLength);
    if (pDst == NULL) return NULL;

    xstrncpy(pDst, nLength, pStr);
    return pDst;
}

char *xstrpdup(xpool_t *pPool, const char *pStr)
{
    if (pStr == NULL) return NULL;
    size_t nLength = strlen(pStr);

    char *pDst = (char*)xalloc(pPool, ++nLength);
    if (pDst == NULL) return NULL;

    xstrncpy(pDst, nLength, pStr);
    return pDst;
}

xbool_t xstrused(const char *pStr)
{
    if (pStr == NULL) return XFALSE;
    return (pStr[0] != XSTR_NUL) ? XTRUE : XFALSE;
}

void xstrnull(char *pString, size_t nLength)
{
    if (!nLength) *pString = 0;
    else while (nLength--) *pString++ = 0;
}

void xstrnul(char *pString)
{
    if (pString == NULL) return; 
    pString[0] = XSTR_NUL;
}

xarray_t* xstrsplit(const char *pString, const char *pDlmt)
{
    if (!xstrused(pString) || pDlmt == NULL) return NULL;
    size_t nDlmtLen = strlen(pDlmt);
    if (!nDlmtLen) return NULL;

    xarray_t *pArray = XArray_NewPool(XSTDNON, XSTDNON, XFALSE);
    if (pArray == NULL) return NULL;

    char sToken[XSTR_MAX];
    int nNext = 0;

    while((nNext = xstrntok(sToken, sizeof(sToken), pString, nNext, pDlmt)) >= 0)
    {
        size_t nLength = strlen(sToken);
        if (!nLength) continue;

        XArray_AddData(pArray, sToken, nLength+1);
        if (!nNext) break;
    }

    if (!pArray->nUsed)
    {
        XArray_Destroy(pArray);
        return NULL;
    }

    return pArray;
}

xarray_t* xstrsplite(const char *pString, const char *pDlmt)
{
    if (!xstrused(pString) || !xstrused(pDlmt)) return NULL;

    char sDlmt[XSTR_MID];
    size_t nDlmtLen = xstrncpy(sDlmt, sizeof(sDlmt), pDlmt);
    if (!nDlmtLen) return NULL;

    xarray_t *pArray = XArray_NewPool(XSTDNON, XSTDNON, XFALSE);
    if (pArray == NULL) return NULL;

    char sToken[XSTR_MAX];
    int nNext = 0;

    while((nNext = xstrntok(sToken, sizeof(sToken), pString, nNext, sDlmt)) >= 0)
    {
        size_t nLength = strlen(sToken);
        if (!nLength)
        {
            XArray_AddData(pArray, XSTR_EMPTY, 1);
            continue;
        }

        XArray_AddData(pArray, sToken, nLength+1);
        if (!nNext) break;
    }

    if (!pArray->nUsed)
    {
        XArray_Destroy(pArray);
        return NULL;
    }

    return pArray;
}

char* xstrtoge(char *pBuffer, size_t nSize, const char *pStr)
{
    if (pStr == NULL) return NULL;
    char sInputLine[XSTR_MAX];
    xbool_t bStarted = XFALSE;
    size_t i;

    for (i = 0; i < XSTR_KEYMAP_SIZE; i++) 
    {
        const char *pInput = bStarted ? sInputLine : pStr;
        xstrnrep(pBuffer, nSize, pInput, g_keyMapEn[i], g_keyMapGe[i]);
        xstrncpy(sInputLine, sizeof(sInputLine), pBuffer);
        bStarted = XTRUE;
    }

    return pBuffer;
}

char* xstrtoen(char *pBuffer, size_t nSize, const char *pStr)
{
    if (pStr == NULL) return NULL;
    char sInputLine[XSTR_MAX];
    xbool_t bStarted = XFALSE;
    size_t i;

    for (i = 0; i < XSTR_KEYMAP_SIZE; i++) 
    {
        const char *pInput = bStarted ? sInputLine : pStr;
        xstrnrep(pBuffer, nSize, pInput, g_keyMapGe[i], g_keyMapEn[i]);
        xstrncpy(sInputLine, sizeof(sInputLine), pBuffer);
        bStarted = XTRUE;
    }

    return pBuffer;
}

/////////////////////////////////////////////////////////////////////////
// XString Implementation
/////////////////////////////////////////////////////////////////////////

int XString_Status(xstring_t *pString)
{
    return (pString->nStatus == XSTDERR) ?
        XSTDERR : (int)pString->nLength;
}

int XString_Resize(xstring_t *pString, size_t nSize)
{
    if (pString->pData == NULL && nSize)
    {
        pString->pData = (char*)malloc(nSize);
        if (pString->pData == NULL)
        {
            pString->nStatus = XSTDERR;
            return pString->nStatus;
        }

        pString->nSize = nSize;
        pString->nLength = 0;
        return (int)nSize;
    }
    else if (!pString->nSize && nSize)
    {
        char *pOldBuff = pString->pData;
        pString->pData = (char*)malloc(nSize);

        if (pString->pData == NULL)
        {
            pString->nStatus = XSTDERR;
            return pString->nStatus;
        }

        pString->nLength = xstrncpyf(pString->pData, nSize, "%s", pOldBuff);
        pString->nSize = nSize;
        return (int)nSize;
    }
    else if (pString->pData && pString->nSize && !nSize)
    {
        free(pString->pData);
        pString->pData = NULL;
        pString->nLength = 0;
        pString->nSize = 0;
        return 0;
    }
    else if (!nSize)
    {
        pString->pData = NULL;
        pString->nLength = 0;
        pString->nSize = 0;
        return 0;
    }

    char* pOldData = pString->pData;
    pString->pData = (char*)realloc(pString->pData, nSize);
    pString->nLength = XSTD_MIN(pString->nLength, nSize);

    if (nSize && pString->pData == NULL)
    {
        pString->pData = pOldData;
        pString->nStatus = XSTDERR;
        return pString->nStatus;
    }

    pString->nSize = nSize;
    return (int)pString->nSize;
}

int XString_Increase(xstring_t *pString, size_t nSize)
{
    if (pString->nStatus == XSTDERR) return XSTDERR;
    size_t nNewSize = pString->nLength + nSize;
    if (nNewSize <= pString->nSize) return (int)pString->nSize;
    else if (pString->nFast) nNewSize *= 2;
    return XString_Resize(pString, nNewSize);
}

int XString_Init(xstring_t *pString, size_t nSize, uint8_t nFastAlloc)
{
    pString->nStatus = 0;
    pString->nLength = 0;
    pString->nAlloc = 0;
    pString->nSize = 0;
    pString->pData = NULL;
    pString->nFast = nFastAlloc;

    XString_Resize(pString, nSize);

    if (nSize && pString->pData != NULL &&
        pString->nStatus != XSTDERR)
        pString->pData[pString->nLength] = XSTR_NUL;

    return XString_Status(pString);
}

void XString_Clear(xstring_t *pString)
{
    if (pString == NULL) return;

    if (pString->nSize > 0 && 
        pString->pData != NULL)
        free(pString->pData);

    if (pString->nAlloc)
    {
        free(pString);
        return;
    }

    pString->nStatus = XSTDNON;
    pString->nLength = 0;
    pString->nSize = 0;
    pString->pData = NULL;
}

int XString_Set(xstring_t *pString, char *pData, size_t nLength)
{
    pString->nStatus = XSTDNON;
    pString->nLength = nLength;
    pString->pData = pData;
    pString->nSize = 0;
    return (int)pString->nLength;
}

int XString_Add(xstring_t *pString, const char *pData, size_t nLength)
{
    if (!nLength) return (int)pString->nLength;
    else if (XString_Increase(pString, nLength + 1) <= 0) return XSTDERR;
    size_t nLeft = XSTD_MIN(nLength, pString->nSize - pString->nLength);

    memcpy(&pString->pData[pString->nLength], pData, nLeft);
    pString->nLength += nLeft;
    pString->pData[pString->nLength] = XSTR_NUL;
    return (int)pString->nLength;
}

int XString_Append(xstring_t *pString, const char *pFmt, ...)
{
    va_list args;
    size_t nBytes = 0;

    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, &nBytes);
    va_end(args);

    if (pDest == NULL)
    {
        pString->nStatus = XSTDERR;
        return XSTDERR;
    }

    XString_Add(pString, pDest, nBytes);
    free(pDest);

    return XString_Status(pString);
}

int XString_AddString(xstring_t *pString, xstring_t *pSrc)
{
    if (pSrc->pData == NULL || !pSrc->nLength) return XSTDERR;
    XString_Add(pString, pSrc->pData, pSrc->nLength);
    return XString_Status(pString);
}

int XString_Copy(xstring_t *pString, xstring_t *pSrc)
{
    if (pSrc->pData == NULL) return XSTDERR;
    XString_Init(pString, pSrc->nSize, pSrc->nFast);
    if (pString->nStatus == XSTDERR) return XSTDERR;

    memcpy(pString->pData, pSrc->pData, pSrc->nSize);
    pString->nLength = pSrc->nLength;
    pString->pData[pString->nLength] = XSTR_NUL;
    return (int)pString->nLength;
}

int XString_Insert(xstring_t *pString, size_t nPosit, const char *pData, size_t nLength)
{
    if (nPosit >= pString->nLength) return XString_Add(pString, pData, nLength);
    else if (XString_Increase(pString, nLength + 1) <= 0) return XSTDERR;

    char *pOffset = &pString->pData[nPosit];
    size_t nTailSize = pString->nLength - nPosit + 1;

    memmove(pOffset + nLength, pOffset, nTailSize);
    memcpy(pOffset, pData, nLength);

    pString->nLength += nLength;
    pString->pData[pString->nLength] = XSTR_NUL;
    return (int)pString->nLength;
}

int XString_InsertFmt(xstring_t *pString, size_t nPosit, const char *pFmt, ...)
{
    va_list args;
    size_t nBytes = 0;

    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, &nBytes);
    va_end(args);

    if (pDest == NULL)
    {
        pString->nStatus = XSTDERR;
        return XSTDERR;
    }

    XString_Insert(pString, nPosit, pDest, nBytes);
    free(pDest);

    return XString_Status(pString);
}

int XString_Remove(xstring_t *pString, size_t nPosit, size_t nSize)
{
    if (!nSize || nPosit >= pString->nLength) return 0;
    nSize = ((nPosit + nSize) > pString->nLength) ? 
            pString->nLength - nPosit : nSize;

    size_t nTailOffset = nPosit + nSize;
    if (nTailOffset >= pString->nLength)
    {
        pString->nLength = nPosit;
        pString->pData[pString->nLength] = XSTR_NUL;
        return (int)pString->nLength;
    }

    size_t nTailSize = pString->nLength - nTailOffset;
    const char *pTail = &pString->pData[nTailOffset];
    memmove(&pString->pData[nPosit], pTail, nTailSize);

    pString->nLength -= nSize;
    pString->pData[pString->nLength] = XSTR_NUL;
    return (int)pString->nLength;
}

int XString_Delete(xstring_t *pString, size_t nPosit, size_t nSize)
{
    XString_Remove(pString, nPosit, nSize);
    XString_Resize(pString, pString->nLength + 1);
    return XString_Status(pString);
}

int XString_Advance(xstring_t *pString, size_t nSize)
{
    XString_Delete(pString, 0, nSize);
    return XString_Status(pString);
}

int XString_Case(xstring_t *pString, xstr_case_t eCase, size_t nPosit, size_t nSize)
{
    if (pString->pData == NULL) return XSTDERR;
    size_t i, nStart = XSTD_MIN(nPosit, pString->nLength);
    size_t nLength = XSTD_MIN(nStart + nSize, pString->nLength);

    for (i = nStart; i < nLength; i++)
    {
        switch (eCase)
        {
            case XSTR_LOWER:
                pString->pData[i] = (char)tolower(pString->pData[i]);
                break;
            case XSTR_UPPER:
                pString->pData[i] = (char)toupper(pString->pData[i]);
                break;
            default:
                break;
        }
    }

    return (int)pString->nLength;
}

int XString_ChangeCase(xstring_t *pString, xstr_case_t eCase)
{
    if (pString->pData == NULL || !pString->nLength) return XSTDERR;
    XString_Case(pString, eCase, 0, pString->nLength);
    return (pString->nStatus == XSTDERR) ? XSTDERR : (int)pString->nLength;
}

int XString_Color(xstring_t *pString, const char* pClr, size_t nPosit, size_t nSize)
{
    if (pString->pData == NULL || !pString->nLength) return XSTDERR;
    size_t nFirstPart = XSTD_MIN(nPosit, pString->nLength);
    size_t nLastPart = nFirstPart + nSize;
    size_t nLastSize = pString->nLength - nLastPart;

    if (nFirstPart >= pString->nLength ||
        nLastPart > pString->nLength) return XSTDERR;

    xstring_t *pTempStr = XString_New(pString->nLength, pString->nFast);
    if (pTempStr == NULL) return XSTDERR;

    if ((XString_Add(pTempStr, pString->pData, nFirstPart) == XSTDERR) ||
        (XString_Add(pTempStr, pClr, strlen(pClr)) == XSTDERR) ||
        (XString_Add(pTempStr, &pString->pData[nFirstPart], nSize) == XSTDERR) ||
        (XString_Add(pTempStr, XSTR_FMT_RESET, strlen(XSTR_FMT_RESET)) == XSTDERR) ||
        (XString_Add(pTempStr, &pString->pData[nLastPart], nLastSize) == XSTDERR))
    {
        XString_Clear(pTempStr);
        return XSTDERR;
    }

    pString->nLength = 0;
    XString_AddString(pString, pTempStr);
    XString_Clear(pTempStr);

    return XString_Status(pString);
}

int XString_ChangeColor(xstring_t *pString, const char* pClr)
{
    if (pString->pData == NULL || !pString->nLength) return XSTDERR;
    XString_Color(pString, pClr, 0, pString->nLength);
    return XString_Status(pString);
}

int XString_Search(xstring_t *pString, size_t nPos, const char *pSearch)
{
    if (pString == NULL || nPos >= pString->nLength) return XSTDERR;
    return xstrsrc(&pString->pData[nPos], pSearch);
}

int XString_Tokenize(xstring_t *pString, char *pDst, size_t nSize, size_t nPosit, const char *pDlmt)
{
    if (pString == NULL || !pString->nLength) return XSTDERR;
    else if (pDst != NULL) pDst[0] = XSTR_NUL;

    if (nPosit >= pString->nLength) return XSTDERR;
    int nOffset = xstrsrc(&pString->pData[nPosit], pDlmt);

    if (nOffset <= 0)
    {
        xstrncpy(pDst, nSize, &pString->pData[nPosit]);
        return 0;
    }

    xstrncpys(pDst, nSize, &pString->pData[nPosit], nOffset);
    return (int)nPosit + nOffset + (int)strlen(pDlmt);
}

int XString_Token(xstring_t *pString, xstring_t *pDst, size_t nPosit, const char *pDlmt)
{
    if (pString == NULL || !pString->nLength) return XSTDERR;
    pDst->pData[0] = XSTR_NUL;
    pDst->nLength = 0;

    if (nPosit >= pString->nLength) return XSTDERR;
    int nOffset = xstrsrc(&pString->pData[nPosit], pDlmt);

    if (nOffset <= 0)
    {
        size_t nLength = pString->nLength - nPosit;
        XString_Add(pDst, &pString->pData[nPosit], nLength);
        return 0;
    }

    XString_Add(pDst, &pString->pData[nPosit], nOffset);
    return (int)nPosit + nOffset + (int)strlen(pDlmt);
}

int XString_Replace(xstring_t *pString, const char *pRep, const char *pWith)
{
    if (pString == NULL || !pString->nLength) return XSTDERR;
    size_t nWithLen = strlen(pWith);
    size_t nRepLen = strlen(pRep);
    int nPos = 0;

    while ((nPos = XString_Search(pString, nPos, pRep)) >= 0)
    {
        XString_Remove(pString, nPos, nRepLen);
        XString_Insert(pString, nPos, pWith, nWithLen);
        nPos += (int)nWithLen;
    }

    return XString_Status(pString);
}

int XString_Sub(xstring_t *pString, char *pDst, size_t nDstSize, size_t nPos, size_t nSize)
{
    if (pString == NULL || nPos >= pString->nLength) return XSTDERR;
    size_t nCopySize = XSTD_MIN(nSize, nDstSize);
    size_t nPartSize = pString->nLength - nPos;
    nCopySize = XSTD_MIN(nCopySize, nPartSize);
    xstrncpys(pDst, nDstSize, &pString->pData[nPos], nCopySize);
    return (int)nCopySize;
}

int XString_SubStr(xstring_t *pString, xstring_t *pSub, size_t nPos, size_t nSize)
{
    if (pString == NULL || nPos >= pString->nLength) return XSTDERR;
    XString_Init(pSub, nSize + 1, pString->nFast);
    if (pSub->nStatus == XSTDERR) return XSTDERR;

    int nLength = XString_Sub(pString, pSub->pData, pSub->nSize, nPos, nSize);
    if (nLength <= 0)
    {
        XString_Clear(pString);
        return XSTDERR;
    }

    pSub->nLength = nLength;
    return (int)pSub->nLength;
}

xstring_t *XString_SubNew(xstring_t *pString, size_t nPos, size_t nSize)
{
    xstring_t* pSub = (xstring_t*)malloc(sizeof(xstring_t));
    if (pSub == NULL) return NULL;

    if (XString_SubStr(pString, pSub, nPos, nSize) <= 0)
    {
        free(pSub);
        return NULL;
    }

    pSub->nAlloc = 1;
    return pSub;
}

xstring_t *XString_New(size_t nSize, uint8_t nFastAlloc)
{
    xstring_t* pString = (xstring_t*)malloc(sizeof(xstring_t));
    if (pString == NULL) return NULL;

    XString_Init(pString, nSize, nFastAlloc);
    pString->nAlloc = 1;

    if (pString->nStatus == XSTDERR)
    {
        XString_Clear(pString);
        return NULL;
    }

    return pString;
}

xstring_t *XString_From(const char *pData, size_t nLength)
{
    if (pData == NULL || !nLength) return NULL;

    xstring_t* pString = XString_New(nLength, 0);
    if (pString == NULL) return NULL;

    XString_Add(pString, pData, nLength);
    if (pString->nStatus == XSTDERR)
    {
        XString_Clear(pString);
        return NULL;
    }

    return pString;
}

xstring_t *XString_FromFmt(const char *pFmt, ...)
{
    va_list args;
    size_t nBytes = 0;

    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, &nBytes);
    va_end(args);

    if (pDest == NULL) return NULL;
    xstring_t* pString = XString_From(pDest, nBytes);

    free(pDest);
    return pString;
}

int XString_InitFrom(xstring_t *pStr, const char *pFmt, ...)
{
    va_list args;
    size_t nBytes = 0;

    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, &nBytes);
    va_end(args);
    if (pDest == NULL) return XSTDERR;

    if (XString_Init(pStr, nBytes, 0) == XSTDERR ||
        XString_Add(pStr, pDest, nBytes) == XSTDERR)
    {
        free(pDest);
        return XSTDERR;
    }

    free(pDest);
    return (int)pStr->nLength;
}

xstring_t *XString_FromStr(xstring_t *pString)
{
    if (pString == NULL || !pString->nLength) return NULL;
    return XString_From(pString->pData, pString->nLength);
}

int XString_Cut(xstring_t *pString, char *pDst, size_t nSize, const char *pFrom, const char *pTo)
{
    if (pString == NULL || !pString->nLength) return XSTDERR;
    int nStartPos = XString_Search(pString, 0, pFrom);
    if (nStartPos < 0) return XSTDERR;

    int nSubSize = 0;
    nStartPos += (int)strlen(pFrom);

    if ((size_t)nStartPos >= pString->nLength) return XSTDERR;
    if (pTo == NULL) nSubSize = (int)pString->nLength - nStartPos;
    else nSubSize = XString_Search(pString, (size_t)nStartPos, pTo);

    if (nSubSize < 0) return XSTDERR;
    xstrncpys(pDst, nSize, &pString->pData[nStartPos], nSubSize);
    return nSubSize;
}

int XString_CutSub(xstring_t *pString, xstring_t *pSub, const char *pFrom, const char *pTo)
{
    if (pString == NULL || !pString->nLength) return XSTDERR;
    XString_Init(pSub, pString->nLength, pString->nFast);
    if (pSub->nStatus == XSTDERR) return XSTDERR;

    int nLength = XString_Cut(pString, pSub->pData, pSub->nSize, pFrom, pTo);
    if (nLength <= 0)
    {
        XString_Clear(pSub);
        return XSTDERR;
    }

    pSub->nLength = nLength;
    return (int)pSub->nLength;
}

xstring_t *XString_CutNew(xstring_t *pString, const char *pFrom, const char *pTo)
{
    xstring_t* pSub = (xstring_t*)malloc(sizeof(xstring_t));
    if (pSub == NULL) return NULL;

    if (XString_CutSub(pString, pSub, pFrom, pTo) == XSTDERR)
    {
        free(pSub);
        return NULL;
    }

    pSub->nAlloc = 1;
    return pSub;
}

void XString_ArrayClearCb(xarray_data_t *pArrData)
{
    if (pArrData == NULL || pArrData->pData == NULL) return;
    xstring_t *pString = (xstring_t*)pArrData->pData;
    XString_Clear(pString);
}

xarray_t* XString_SplitStr(xstring_t *pString, const char *pDlmt)
{
    if (pString == NULL || !pString->nLength) return NULL;

    xstring_t *pToken = XString_New(XSTR_MIN, 0);
    if (pToken == NULL) return NULL;

    xarray_t *pArray = XArray_NewPool(XSTDNON, 2, 0);
    if (pArray == NULL)
    {
        XString_Clear(pToken);
        return NULL;
    }

    pArray->clearCb = XString_ArrayClearCb;
    int nNext = 0;

    while ((nNext = XString_Token(pString, pToken, nNext, pDlmt)) >= 0)
    {
        XArray_AddData(pArray, pToken, 0);
        if (!nNext) break;

        pToken = XString_New(XSTR_MIN, 0);
        if (pToken == NULL)
        {
            XArray_Destroy(pArray);
            return NULL;
        }
    }

    if (!pArray->nUsed)
    {
        XArray_Destroy(pArray);
        return NULL;
    }

    return pArray;
}

xarray_t* XString_Split(const char *pCStr, const char *pDlmt)
{
    xstring_t *pString = XString_From(pCStr, strlen(pCStr));
    if (pString == NULL) return NULL;

    xarray_t *pArray = XString_SplitStr(pString, pDlmt);
    XString_Clear(pString);
    return pArray;
}
