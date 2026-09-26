#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"
#include "pool.h"

#include "test.h"

static int XTest_boundaries(void)
{
    xjson_t json;
    const size_t depth = 20000, count = 30000;
    char *data = malloc(count * 20 + 1);
    CHECK(data, "allocate test document");
    memset(data, '[', depth);
    memset(data + depth, ']', depth);
    CHECK(!XJSON_Parse(&json, NULL, data, depth * 2), "deep nesting rejected");
    CHECK(json.nError == XJSON_ERR_DEPTH && json.pRootObj == NULL, "depth failure frees partial tree");
    XJSON_Destroy(&json);
    memset(data, '[', XJSON_MAX_DEPTH);
    memset(data + XJSON_MAX_DEPTH, ']', XJSON_MAX_DEPTH);
    CHECK(XJSON_Parse(&json, NULL, data, XJSON_MAX_DEPTH * 2), "depth limit accepted");
    XJSON_Destroy(&json);

    size_t pos = 0;
    data[pos++] = '[';
    for (size_t i = 0; i < count; i++)
    {
        data[pos++] = '0';
        data[pos++] = i + 1 < count ? ',' : ']';
    }
    CHECK(XJSON_Parse(&json, NULL, data, pos), "large flat array uses bounded stack");
    CHECK(XJSON_GetArrayLength(json.pRootObj) == count, "all array items retained");
    XJSON_Destroy(&json);
    pos = 0;
    data[pos++] = '{';
    for (size_t i = 0; i < count; i++) pos += (size_t)sprintf(data + pos, "\"key%zu\":0%c", i, i + 1 < count ? ',' : '}');
    CHECK(XJSON_Parse(&json, NULL, data, pos), "large flat object uses bounded stack");
    CHECK(XJSON_GetObject(json.pRootObj, "key29999") != NULL, "last object member retained");
    XJSON_Destroy(&json);
    free(data);

    const char *numbers = "{\"u32\":4294967295,\"overflow32\":4294967296,"
                          "\"u16\":65535,\"overflow16\":65536,\"u64\":18446744073709551615,"
                          "\"overflow64\":18446744073709551616,\"negative\":-1,\"minimum\":-2147483648,"
                          "\"overflowInt\":2147483648}";
    CHECK(XJSON_Parse(&json, NULL, numbers, strlen(numbers)), "parse numeric limits");
    CHECK(XJSON_GetU32(XJSON_GetObject(json.pRootObj, "u32")) == UINT32_MAX, "full uint32 range");
    CHECK(XJSON_GetU16(XJSON_GetObject(json.pRootObj, "u16")) == UINT16_MAX, "full uint16 range");
    CHECK(XJSON_GetU64(XJSON_GetObject(json.pRootObj, "u64")) == UINT64_MAX, "full uint64 range");
    CHECK(XJSON_GetU32(XJSON_GetObject(json.pRootObj, "overflow32")) == 0, "uint32 overflow rejected");
    CHECK(XJSON_GetU16(XJSON_GetObject(json.pRootObj, "overflow16")) == 0, "uint16 overflow rejected");
    CHECK(XJSON_GetU64(XJSON_GetObject(json.pRootObj, "overflow64")) == 0, "uint64 overflow rejected");
    CHECK(XJSON_GetU32(XJSON_GetObject(json.pRootObj, "negative")) == 0, "unsigned negative rejected");
    CHECK(XJSON_GetInt(XJSON_GetObject(json.pRootObj, "minimum")) == INT_MIN, "signed minimum");
    CHECK(XJSON_GetInt(XJSON_GetObject(json.pRootObj, "overflowInt")) == 0, "signed overflow rejected");
    XJSON_Destroy(&json);
    puts("json_limits_regression: OK");
    return 0;
}

static int XTest_escape_nul(void)
{
    /* A NUL byte after a backslash passed as an escape, because strchr()
       also matches the terminator of the set it searches, and put a NUL into
       the middle of the parsed string. */
    const char data[] = { '{', '"', 'a', '"', ':', '"', 'x', '\\', '\0', 'y', '"', '}' };
    xjson_t json;
    CHECK(XJSON_Parse(&json, NULL, data, sizeof(data)) == XJSON_FAILURE, "A NUL escape is rejected");
    XJSON_Destroy(&json);

    /* Characters above 0x7F are not digits, whatever isdigit() makes of a
       negative char. */
    const char high[] = "[\xB2]";
    CHECK(XJSON_Parse(&json, NULL, high, strlen(high)) == XJSON_FAILURE, "A high byte is not a number");
    XJSON_Destroy(&json);

    const char valid[] = "{\"a\":\"x\\ny\"}";
    CHECK(XJSON_Parse(&json, NULL, valid, strlen(valid)) == XJSON_SUCCESS, "Ordinary escapes still parse");
    XJSON_Destroy(&json);
    return 0;
}

static int XTest_pool_dump(void)
{
    /* Dumping a pool backed document formatted every token into a pool string
       of its own, which a pool never gives back, and so the output buffer was
       never the last allocation and was copied whole on almost every token: a
       28 KB dump took 56 MB of pool. It is linear now, in and out of a pool. */
    xpool_t *pPool = XPool_Create(1024 * 1024);
    CHECK(pPool != NULL, "Create a pool");

    xjson_obj_t *pArray = XJSON_NewArray(pPool, NULL, 0);
    CHECK(pArray != NULL, "Create a pool backed array");

    for (int i = 0; i < 2000; i++)
        CHECK(XJSON_AddObject(pArray, XJSON_NewString(pPool, NULL, "entry value")) == XJSON_ERR_NONE, "Add an entry");

    size_t nBefore = XPool_GetUsed(pPool);
    size_t nSizeBefore = XPool_GetSize(pPool);
    size_t nLength = 0;

    char *pDump = XJSON_DumpObj(pArray, 0, &nLength);
    CHECK(pDump != NULL && nLength == 2000 * 14 + 1, "Dump every entry");
    CHECK(pDump[0] == '[' && pDump[nLength - 1] == ']' && pDump[nLength] == '\0', "The dump is a terminated array");
    const char *pHead = "[\"entry value\",\"entry value\",";
    CHECK(!strncmp(pDump, pHead, strlen(pHead)), "Entries are written in order");

    size_t nUsed = XPool_GetUsed(pPool) - nBefore;
    CHECK(nUsed <= nLength * 4 + 4096, "The dump takes pool memory in proportion to its length");
    CHECK(XPool_GetSize(pPool) == nSizeBefore, "The dump fits in the pool it started in");

    /* The pretty printer takes the same buffer path */
    nLength = 0;
    pDump = XJSON_DumpObj(pArray, 2, &nLength);
    CHECK(pDump != NULL && nLength == 2000 * 17 + 2, "An indented dump has every entry on its own line");
    pHead = "[\n  \"entry value\",\n  \"entry value\",";
    CHECK(!strncmp(pDump, pHead, strlen(pHead)), "Indented entries are written in order");
    CHECK(!strcmp(pDump + nLength - 15, "\"entry value\"\n]"), "The last entry closes the array");

    XPool_Destroy(pPool);
    return 0;
}

XTEST_MAIN(XTEST_CASE(boundaries), XTEST_CASE(escape_nul), XTEST_CASE(pool_dump))
