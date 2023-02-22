/*!
 *  @file libxutils/src/xstr.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief This source includes string operations.
 */

#ifndef __XUTILS_XSTR_H__
#define __XUTILS_XSTR_H__

#include "xstd.h"
#include "xtype.h"
#include "array.h"

/* Supported colors */
#define XSTR_CLR_NONE               "\x1B[0m"
#define XSTR_CLR_RED                "\x1B[31m"
#define XSTR_CLR_GREEN              "\x1B[32m"
#define XSTR_CLR_YELLOW             "\x1B[33m"
#define XSTR_CLR_BLUE               "\x1B[34m"
#define XSTR_CLR_MAGENTA            "\x1B[35m"
#define XSTR_CLR_CYAN               "\x1B[36m"
#define XSTR_CLR_WHITE              "\x1B[37m"

#define XSTR_CLR_LIGHT_RED          "\x1B[31;1m"
#define XSTR_CLR_LIGHT_GREEN        "\x1B[32;1m"
#define XSTR_CLR_LIGHT_YELLOW       "\x1B[33;1m"
#define XSTR_CLR_LIGHT_BLUE         "\x1B[34;1m"
#define XSTR_CLR_LIGHT_MAGENTA      "\x1B[35;1m"
#define XSTR_CLR_LIGHT_CYAN         "\x1B[36;1m"
#define XSTR_CLR_LIGHT_WHITE        "\x1B[37;1m"

/* Supported background colors */
#define XSTR_BACK_BLACK             "\x1B[40m"
#define XSTR_BACK_RED               "\x1B[41m"
#define XSTR_BACK_GREEN             "\x1B[42m"
#define XSTR_BACK_YELLOW            "\x1B[43m"
#define XSTR_BACK_BLUE              "\x1B[44m"
#define XSTR_BACK_MAGENTA           "\x1B[45m"
#define XSTR_BACK_CYAN              "\x1B[46m"
#define XSTR_BACK_WHITE             "\x1B[47m"

/* Supported string formats */
#define XSTR_FMT_BOLD               "\x1B[1m"
#define XSTR_FMT_DIM                "\x1B[2m"
#define XSTR_FMT_ITALIC             "\x1B[3m"
#define XSTR_FMT_ULINE              "\x1B[4m"
#define XSTR_FMT_FLICK              "\x1B[5m"
#define XSTR_FMT_BLINK              "\x1B[6m"
#define XSTR_FMT_HIGHLITE           "\x1B[7m"
#define XSTR_FMT_HIDE               "\x1B[8m"
#define XSTR_FMT_CROSS              "\x1B[9m"
#define XSTR_FMT_RESET              XSTR_CLR_NONE

#define XSTR_DOUBLE_ARG_MAX         309
#define XSTR_INT_ARG_MAX            32
#define XSTR_LETTERS                52

#define XSTR_MAX                    8192
#define XSTR_MID                    4096
#define XSTR_MIN                    2048
#define XSTR_TINY                   256
#define XSTR_NPOS                   UINT_MAX
#define XSTR_STACK                  XSTR_MID

#define XSTR_SPACE_CHAR             ' '
#define XSTR_NEW_LINE               "\n"
#define XSTR_SPACE                  " "
#define XSTR_EMPTY                  ""
#define XSTR_NUL                    '\0'
#define XSTR_INIT                   { XSTR_NUL }

#define XSTR_AVAIL(arr)((int)sizeof(arr)-(int)strlen(arr))

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XSTR_LOWER = 0,
    XSTR_UPPER
} xstr_case_t;

/////////////////////////////////////////////////////////////////////////
// C string functions
/////////////////////////////////////////////////////////////////////////

xarray_t* xstrsplit(const char *pString, const char *pDlmt);
char* xstrrep(const char *pOrig, const char *pRep, const char *pWith);
int xstrnrep(char *pDst, size_t nSize, const char *pOrig, const char *pRep, const char *pWith);
char* xstrdup(const char *pStr);
char* xstracpy(const char *pFmt, ...);
char* xstracpyn(size_t *nSize, const char *pFmt, ...);
char* xstralloc(size_t nSize);

int xstrncpyarg(char *pDest, size_t nSize, const char *pFmt, va_list args);
char* xstracpyarg(const char *pFmt, va_list args);
char* xstracpyargs(const char *pFmt, va_list args, size_t *nSize);
size_t xstrarglen(const char *pFmt, va_list args);

size_t xstrxcpyf(char **pDst, const char *pFmt, ...);
char* xstrxcpy(const char *pFmt, ...);

size_t xstrncpy(char *pDst, size_t nSize, const char* pSrc);
size_t xstrncpys(char *pDst, size_t nDstSize, const char *pSrc, size_t nSrcLen);
size_t xstrncpyf(char *pDst, size_t nSize, const char *pFmt, ...);
size_t xstrncat(char *pDst, size_t nSize, const char *pFmt, ...);
size_t xstrncatf(char *pDst, size_t nAvail, const char *pFmt, ...);
size_t xstrncatsf(char *pDst, size_t nSize, size_t nAvail, const char *pFmt, ...);

size_t xstrrand(char *pDst, size_t nSize, size_t nLength, xbool_t bUpper, xbool_t bNumbers);
size_t xstrextra(const char *pStr, size_t nLength, size_t nMaxChars, size_t *pChars, size_t *pPosit);
size_t xstrncpyfl(char *pDst, size_t nSize, size_t nFLen, char cFChar, const char* pFmt, ...);
size_t xstrnlcpyf(char *pDst, size_t nSize, size_t nFLen, char cFChar, const char* pFmt, ...);
size_t xstrnfill(char *pDst, size_t nSize, size_t nLength, char cFill);
size_t xstrisextra(const char *pOffset);
char *xstrfill(size_t nLength, char cFill);

int xstrnsrc(const char *pStr, size_t nLen, const char *pSrc, size_t nPos);
int xstrsrcp(const char *pStr, const char *pSrc, size_t nPos);
int xstrsrc(const char *pStr, const char *pSrc);

char* xstracase(const char *pSrc, xstr_case_t eCase);
char* xstracasen(const char *pSrc, xstr_case_t eCase, size_t nLength);
size_t xstrcase(char *pSrc, xstr_case_t eCase);
size_t xstrncase(char *pDst, size_t nSize, xstr_case_t eCase, const char *pSrc);
size_t xstrncases(char* pDst, size_t nSize, xstr_case_t eCase, const char *pSrc, size_t nSrcLen);

char* xstrtok(char* pString, const char* pDelimiter, char** pContext);
char* xstrcut(char *pData, const char *pStart, const char *pEnd);
char* xstracut(const char *pSrc, size_t nPosit, size_t nSize);
size_t xstrncut(char *pDst, size_t nDstSize, const char *pData, size_t nPosit, size_t nSize);
size_t xstrncuts(char *pDst, size_t nSize, const char *pSrc, const char *pStart, const char *pEnd);

int xstrntok(char *pDst, size_t nSize, const char *pStr, size_t nPosit, const char *pDlmt);
size_t xstrnclr(char *pDst, size_t nSize, const char* pClr, const char* pStr, ...);
size_t xstrnrm(char *pStr, size_t nPosit, size_t nSize);
void xstrnull(char *pString, size_t nLength);
void xstrnul(char *pString);
xbool_t xstrused(const char *pStr);

char* xstrtoen(char *pBuffer, size_t nSize, const char *pStr);
char* xstrtoge(char *pBuffer, size_t nSize, const char *pStr);

/////////////////////////////////////////////////////////////////////////
// XString Implementation
/////////////////////////////////////////////////////////////////////////

typedef struct XString {
    char* pData;
    size_t nLength;
    size_t nSize;
    int16_t nStatus;
    xbool_t nAlloc;
    xbool_t nFast;
} xstring_t;

xstring_t *XString_New(size_t nSize, uint8_t nFastAlloc);
xstring_t *XString_From(const char *pData, size_t nLength);
xstring_t *XString_FromFmt(const char *pFmt, ...);
xstring_t *XString_FromStr(xstring_t *pString);

int XString_Init(xstring_t *pString, size_t nSize, uint8_t nFastAlloc);
int XString_InitFrom(xstring_t *pStr, const char *pFmt, ...);
int XString_Set(xstring_t *pString, char *pData, size_t nLength);
int XString_Increase(xstring_t *pString, size_t nSize);
int XString_Resize(xstring_t *pString, size_t nSize);
void XString_Clear(xstring_t *pString);

int XString_Add(xstring_t *pString, const char *pData, size_t nLength);
int XString_Copy(xstring_t *pString, xstring_t *pSrc);
int XString_Append(xstring_t *pString, const char *pFmt, ...);
int XString_AddString(xstring_t *pString, xstring_t *pSrc);

int XString_Insert(xstring_t *pString, size_t nPosit, const char *pData, size_t nLength);
int XString_InsertFmt(xstring_t *pString, size_t nPosit, const char *pFmt, ...);

int XString_Remove(xstring_t *pString, size_t nPosit, size_t nSize);
int XString_Delete(xstring_t *pString, size_t nPosit, size_t nSize);
int XString_Advance(xstring_t *pString, size_t nSize);

int XString_Tokenize(xstring_t *pString, char *pDst, size_t nSize, size_t nPosit, const char *pDlmt);
int XString_Token(xstring_t *pString, xstring_t *pDst, size_t nPosit, const char *pDlmt);
int XString_Case(xstring_t *pString, xstr_case_t eCase, size_t nPosit, size_t nSize);
int XString_Color(xstring_t *pString, const char* pClr, size_t nPosit, size_t nSize);
int XString_ChangeCase(xstring_t *pString, xstr_case_t eCase);
int XString_ChangeColor(xstring_t *pString, const char* pClr);
int XString_Replace(xstring_t *pString, const char *pRep, const char *pWith);
int XString_Search(xstring_t *pString, size_t nPos, const char *pSearch);

xarray_t* XString_SplitStr(xstring_t *pString, const char *pDlmt);
xarray_t* XString_Split(const char *pCStr, const char *pDlmt);

int XString_Sub(xstring_t *pString, char *pDst, size_t nDstSize, size_t nPos, size_t nSize);
int XString_SubStr(xstring_t *pString, xstring_t *pSub, size_t nPos, size_t nSize);
xstring_t *XString_SubNew(xstring_t *pString, size_t nPos, size_t nSize);

int XString_Cut(xstring_t *pString, char *pDst, size_t nSize, const char *pFrom, const char *pTo);
int XString_CutSub(xstring_t *pString, xstring_t *pSub, const char *pFrom, const char *pTo);
xstring_t *XString_CutNew(xstring_t *pString, const char *pFrom, const char *pTo);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XSTR_H__ */