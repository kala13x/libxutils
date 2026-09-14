#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"

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

XTEST_MAIN(XTEST_CASE(boundaries))
