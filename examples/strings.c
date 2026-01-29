/*
 *  examples/strings.c
 *
 *  Copyleft (C) 2015  Sun Dro (a.k.a. kala13x)
 *
 * Example file for working with strings.
 */

#include "xstd.h"
#include "str.h"
#include "log.h"
#include "array.h"

#define UPPER_STRING "TEST STRING WITH UPPER CASE"
#define LOVER_STRING "test string with lower case"

xbool_t va_string_test1(const char *pOriginalString, size_t nOriginalLength, const char *pFmt, ...)
{
    char *pDest = NULL;
    XSTRCPYFMT(pDest, pFmt, NULL);
    XASSERT(pDest, XFALSE);

    xlog("%s", pDest);

    xbool_t status = xstrcmp(pOriginalString, pDest);
    XASSERT_CALL(status, free, pDest, XFALSE);

    size_t nLength = strlen(pDest);
    XASSERT_CALL((nLength == nOriginalLength), free, pDest, XFALSE);

    free(pDest);
    return XTRUE;
}

xbool_t va_string_test2(const char *pOriginalString, size_t nOriginalLength, const char *pFmt, ...)
{
    size_t nLength = 0;
    char sDest[XSTR_MIN];
    sDest[0] = XSTR_NUL;

    XSTRNCPYFMT(sDest, sizeof(sDest), pFmt, &nLength);
    if (!xstrused(sDest)) return XFALSE;

    xlog("%s (%zu)", sDest, nLength);

    XASSERT((nLength == nOriginalLength), XFALSE);
    return xstrcmp(pOriginalString, sDest);
}

int main()
{
    xlog_defaults();

    char *ptest = xstracpy("the very %s test %s %d %s %.2f", "first", "string", 1, "2", 3.0);
    if (ptest != NULL)
    {
        // test
        xlog_cfg_t cfg;
        xlog_get(&cfg);
        cfg.bUseHeap = 1;
        xlog_set(&cfg);

        xlog("Test: %s", ptest);
        free(ptest);

        cfg.bUseHeap = 0;
        xlog_set(&cfg);
    }
    else
    {
        xloge("xstracpy failed");
        return XSTDERR;
    }

    char str1[128], str2[128];
    xlog("Initial strings: 1(%s) and 2(%s)", LOVER_STRING, UPPER_STRING);

    char *ptr = xstrrep(UPPER_STRING, "UPPER", "LOWER");
    xlog("Replaced word \"UPPER\" with \"LOWER\" in string 2: %s", ptr);

    xstrncase(str1, sizeof(str1), XSTR_LOWER, ptr);
    xlog("Changed from upper to lower case 2: %s", str1);
    free(ptr);

    ptr = xstrrep(LOVER_STRING, "lower", "upper");
    xlog("Replaced word \"lower\" with \"upper\" in string 1: %s", ptr);

    xstrncase(str2, sizeof(str2), XSTR_UPPER, ptr);
    xlog("Changed from lower to upper case 1: %s", str2);
    free(ptr);

    int i, nAvail = xstrncatf(str1, XSTR_AVAIL(str1), "(2) and (1)");
    if (nAvail > 0) xstrncatf(str1, nAvail, str2);

    char colorized[128];
    xstrnclr(colorized, sizeof(colorized), XSTR_CLR_GREEN, str1);
    xlog("Colorized output: %s", colorized);

    char sToken[32];
    int nNext = 0;

    while((nNext = xstrntok(sToken, sizeof(sToken), str1, nNext, " ")) >= 0)
    {
        xlog("Token: %s", sToken);
        if (!nNext) break;
    }

    xarray_t *pArr = xstrsplit("test.string.for.split", ".");
    if (pArr != NULL)
    {
        for (i = 0; i < pArr->nUsed; i++)
        {
            char *pSplit = XArray_GetData(pArr, i);
            xlog("xstrsplit: %s", pSplit);
        }

        XArray_Destroy(pArr);
    }
    else
    {
        xloge("xstrsplit failed");
        return XSTDERR;
    }

    // XString
    xstring_t string;
    XString_Init(&string, 1, 1);

    XString_Append(&string, "raise ");
    XString_Append(&string, "your ");
    XString_Append(&string, "arms");
    xlog(string.pData);

    XString_Remove(&string, 6, 5);
    xlog(string.pData);

    XString_Insert(&string, 6, "your ", 5);
    xlog(string.pData);

    XString_Append(&string, " to the big black sky");
    XString_Add(&string, "...", 3);
    xlog(string.pData);

    XString_Delete(&string, 36, 3);
    xlog(string.pData);

    XString_InsertFmt(&string, 36, " so whole universe will glow");
    xlog(string.pData);

    XString_Advance(&string, 40);
    xlog(string.pData);

    if (strlen(string.pData) != string.nLength)
    {
        xloge("String lengths are not equal: %zu/%zu", strlen(string.pData), string.nLength);
        XString_Clear(&string);
        return XSTDERR;
    }

    xstring_t string2;
    XString_Copy(&string2, &string);
    xlog(string2.pData);

    XString_ChangeCase(&string2, XSTR_UPPER);
    xlog(string2.pData);

    XString_Case(&string2, XSTR_LOWER, 6, 8);
    xlog(string2.pData);

    int nPosit = XString_Search(&string2, 0, "universe");
    if (nPosit >= 0)
    {
        XString_Case(&string2, XSTR_UPPER, nPosit, 8);
        xlog(string2.pData);

        XString_Color(&string2, XSTR_CLR_BLUE, nPosit, 8);
        xlog(string2.pData);

        XString_Delete(&string2, 6, strlen(XSTR_CLR_BLUE));
        XString_Delete(&string2, 14, strlen(XSTR_FMT_RESET));
        xlog(string2.pData);
    }
    else
    {
        xloge("XString_Search failed");
        XString_Clear(&string);
        XString_Clear(&string2);
        return XSTDERR;
    }

    XString_ChangeColor(&string2, XSTR_CLR_BLUE);
    xlog(string2.pData);

    XString_Replace(&string2, "UNIVERSE", "<<UNIVERSE>>");
    xlog(string2.pData);

    xstring_t tok;
    XString_Init(&tok, 32, 0);
    nNext = 0;

    while((nNext = XString_Token(&string, &tok, nNext, " ")) >= 0)
    {
        xlog("Token: %s", tok.pData);
        if (!nNext) break;
    }

    XString_Clear(&tok);

    pArr = XString_SplitStr(&string, " ");
    if (pArr != NULL)
    {
        for (i = 0; i < pArr->nUsed; i++)
        {
            xstring_t *pSplit = XArray_GetData(pArr, i);
            xlog("Split: %s", pSplit->pData);
        }

        XArray_Destroy(pArr);
    }
    else
    {
        xloge("XString_SplitStr failed");
        XString_Clear(&string);
        XString_Clear(&string2);
        return XSTDERR;
    }

    xstring_t sub;
    XString_SubStr(&string, &sub, 6, 8);
    xlog(sub.pData);

    XString_Clear(&sub);
    XString_Clear(&string2);

    xstring_t *pNewStr = XString_FromFmt("new string");
    xlog(pNewStr->pData);
    XString_Clear(pNewStr);

    pNewStr = XString_From("new string2", strlen("new string2"));
    xlog(pNewStr->pData);
    XString_Clear(pNewStr);

    pNewStr = XString_SubNew(&string, 6, 8);
    xlog(pNewStr->pData);
    XString_Clear(pNewStr);

    pNewStr = XString_CutNew(&string, "whole ", " will");
    if (pNewStr) xlog(pNewStr->pData);
    XString_Clear(pNewStr);

    xstring_t substring;
    XString_CutSub(&string, &substring, "whole ", " will");
    if (substring.nLength) xlog(substring.pData);
    XString_Clear(&substring);

    pArr = XString_Split("test.string.for.split", ".");
    if (pArr != NULL)
    {
        for (i = 0; i < pArr->nUsed; i++)
        {
            xstring_t *pSplit = XArray_GetData(pArr, i);
            xlog("Split2: %s", pSplit->pData);
        }

        XArray_Destroy(pArr);
    }
    else
    {
        xloge("XString_Split failed");
        XString_Clear(&string);
        return XSTDERR;
    }

    XString_Clear(&string);

    // Test pattern matching
    char pattern[] = "pattern*";
    char pattern2[] = "should*match*this*pattern*";

    char matchStr1[] = "pattern";
    size_t slen1 = strlen((char *)matchStr1);

    char matchStr2[] = "pattern.test";
    size_t slen2 = strlen((char *)matchStr2);

    char matchStr3[] = "not_match.pattern";
    size_t slen3 = strlen((char *)matchStr3);

    char matchStr4[] = "should match this cool pattern!";
    size_t slen4 = strlen((char *)matchStr4);

    xbool_t match1 = xstrmatch(matchStr1, slen1, pattern);
    xbool_t match2 = xstrmatch(matchStr2, slen2, pattern);
    xbool_t match3 = xstrmatch(matchStr3, slen3, pattern);
    xbool_t match4 = xstrmatch(matchStr4, slen4, pattern2);

    if (!match1 || !match2 || match3 || !match4)
    {
        xloge("Pattern matching failed with xstrmatch: %d/%d/%d/%d", match1, match2, match3, match4);
        return XSTDERR;
    }

    printf("Matching \"%s\" with pattern \"%s\": %s\n", matchStr1, pattern, match1 ? "MATCH" : "NO MATCH");
    printf("Matching \"%s\" with pattern \"%s\": %s\n", matchStr2, pattern, match2 ? "MATCH" : "NO MATCH");
    printf("Matching \"%s\" with pattern \"%s\": %s\n", matchStr3, pattern, match3 ? "MATCH" : "NO MATCH");
    printf("Matching \"%s\" with pattern \"%s\": %s\n", matchStr4, pattern2, match4 ? "MATCH" : "NO MATCH");

    // Test multy pattern matching
    char multiPattern[] = "pa??ern;te*t;string;rand?m*str*ing*her?!";
    char multiMatchStr1[] = "pattern";
    char multiMatchStr1a[] = "patterna";
    char multiMatchStr2[] = "random string here!";
    char multiMatchStr3[] = "random bad str here";

    xbool_t multiMatch1 = xstrmatchm(multiMatchStr1, strlen(multiMatchStr1), multiPattern, ";");
    xbool_t multiMatch1a = xstrmatchm(multiMatchStr1a, strlen(multiMatchStr1a), multiPattern, ";");
    xbool_t multiMatch2 = xstrmatchm(multiMatchStr2, strlen(multiMatchStr2), multiPattern, ";");
    xbool_t multiMatch3 = xstrmatchm(multiMatchStr3, strlen(multiMatchStr3), multiPattern, ";");

    if (!multiMatch1 || multiMatch1a || !multiMatch2 || multiMatch3)
    {
        xloge("Multy pattern matching failed with xstrmatchm: %d/%d/%d/%d",
                    multiMatch1, multiMatch1a, multiMatch2, multiMatch3);

        return XSTDERR;
    }

    printf("Matching \"%s\" with pattern \"%s\": %s\n", multiMatchStr1, multiPattern, multiMatch1 ? "MATCH" : "NO MATCH");
    printf("Matching \"%s\" with pattern \"%s\": %s\n", multiMatchStr1a, multiPattern, multiMatch1a ? "MATCH" : "NO MATCH");
    printf("Matching \"%s\" with pattern \"%s\": %s\n", multiMatchStr2, multiPattern, multiMatch2 ? "MATCH" : "NO MATCH");
    printf("Matching \"%s\" with pattern \"%s\": %s\n", multiMatchStr3, multiPattern, multiMatch3 ? "MATCH" : "NO MATCH");

    // Test VA string functions
    if (!va_string_test1("test string 69", 14, "test %s %d", "string", 69))
    {
        xloge("va_string_test1 failed");
        return XSTDERR;
    }

    if (!va_string_test2("test string2 96", 15, "test %s %d", "string2", 96))
    {
        xloge("va_string_test2 failed");
        return XSTDERR;
    }

    return 0;
}
