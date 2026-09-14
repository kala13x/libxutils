/* libxutils: bounded copies and dynamic string mutation. */
#include "test.h"
#include "str.h"

static int XTest_capacities(void)
{
    char data[32];
    const char input[] = "abcdef";
    for (size_t nSize = 0; nSize <= 10; nSize++)
    {
        memset(data, 0x5a, sizeof(data));
        size_t nExpected = nSize ? XSTD_MIN(nSize - 1, sizeof(input) - 1) : 0;
        CHECK(xstrncpys(data, nSize, input, sizeof(input) - 1) == nExpected, "Bounded copy reports the bytes actually stored");
        CHECK(!nSize || (data[nExpected] == 0 && memcmp(data, input, nExpected) == 0), "Every nonempty capacity terminates");
        for (size_t i = nSize; i < sizeof(data); i++) CHECK(data[i] == 0x5a, "Copy respects the exact destination capacity");
    }
    const char raw[] = {'a', 'b', 'c'};
    CHECK(xstrncmpn(raw, 3, "abc", 3), "Compare a nonterminated slice");
    CHECK(!xstrncmpn(raw, 3, "ab", 2), "Different slice lengths are not equal");
    CHECK(xstrnsrc(raw, 3, "bc", 0) == 1, "Search respects bounded nonterminated input");
    CHECK(xstrnsrc(raw, 3, "z", 0) < 0, "Missing search never reads past a nonterminated slice");
    return 0;
}

static int XTest_dynamic(void)
{
    xstring_t string;
    CHECK(XString_InitFrom(&string, "%s", "abc") > 0, "Initialize dynamic string");
    CHECK(XString_AddString(&string, &string) > 0 && strcmp(string.pData, "abcabc") == 0, "Self append survives growth");
    CHECK(XString_Insert(&string, 2, string.pData + 1, 3) > 0, "Insert an overlapping source slice");
    CHECK(strcmp(string.pData, "abbcacabc") == 0, "Aliased insertion retains original characters");
    CHECK(XString_Remove(&string, 2, 3) > 0 && strcmp(string.pData, "abcabc") == 0, "Remove restores the original string");
    CHECK(XString_Replace(&string, "abc", "xy") > 0 && strcmp(string.pData, "xyxy") == 0, "Replace every occurrence");
    CHECK(
        XString_ChangeCase(&string, XSTR_UPPER) > 0 && strcmp(string.pData, "XYXY") == 0, "Case conversion updates dynamic data");
    XString_Clear(&string);
    return 0;
}

static int XTest_split_replace(void)
{
    xarray_t *pArray = XString_Split("one::two::three", "::");
    CHECK(pArray && pArray->nUsed == 3, "Split by a multi-character separator");
    CHECK(strcmp(((xstring_t*)XArray_GetData(pArray, 1))->pData, "two") == 0, "Middle token has exact content");
    XArray_Destroy(pArray);
    char *pReplaced = xstrrep("aaaa", "aa", "b");
    CHECK(pReplaced && strcmp(pReplaced, "bb") == 0, "Replacement consumes nonoverlapping matches");
    free(pReplaced);
    pReplaced = xstrrep("a-b-a", "a", "long");
    CHECK(pReplaced && strcmp(pReplaced, "long-b-long") == 0, "Replacement grows destination safely");
    free(pReplaced);
    return 0;
}

XTEST_MAIN(XTEST_CASE(capacities), XTEST_CASE(dynamic), XTEST_CASE(split_replace))
