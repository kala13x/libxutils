/*
 *  examples/strings.c
 * 
 *  Copyleft (C) 2015  Sun Dro (a.k.a. kala13x)
 *
 * Example file for working with strings.
 */

#include <xutils/xstd.h>
#include <xutils/xstr.h>
#include <xutils/xlog.h>
#include <xutils/array.h>

#define UPPER_STRING "TEST STRING WITH UPPER CASE"
#define LOVER_STRING "test string with lower case"

int main() 
{
    xlog_defaults();

    char *ptest = xstracpy("the very %s test %s %d %s %.2f", "first", "string", 1, "2", 3.0);
    if (ptest != NULL)
    {
        // test
        xlog_cfg_t cfg;
        xlog_get(&cfg);
        cfg.nUseHeap = 1;
        xlog_set(&cfg);

        xlog("Test: %s", ptest);
        free(ptest);

        cfg.nUseHeap = 0;
        xlog_set(&cfg);
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
        xloge("should not happen: %zu/%zu", strlen(string.pData), string.nLength);

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

    XString_Clear(&string);
    return 0;
}
