/* libxutils JSON engine: every protocol header crosses this parser with
 * relay-supplied bytes, so it must roundtrip cleanly and reject garbage
 * without crashing or leaking. */

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "map.h"
#include "log.h"
#include "str.h"

#include "test.h"

static int parse_ok(const char *pData)
{
    xjson_t json;
    if (!XJSON_Parse(&json, NULL, pData, strlen(pData))) return 0;
    if (json.nError != XJSON_ERR_NONE)
    {
        XJSON_Destroy(&json);
        return 0;
    }
    XJSON_Destroy(&json);
    return 1;
}

static int parse_bytes_ok(const char *pData, size_t nSize)
{
    xjson_t json;
    if (!XJSON_Parse(&json, NULL, pData, nSize)) return 0;
    int nStatus = json.nError == XJSON_ERR_NONE && json.pRootObj != NULL;
    XJSON_Destroy(&json);
    return nStatus;
}

static int parse_bytes_rejected(const char *pData, size_t nSize)
{
    xjson_t json;
    if (XJSON_Parse(&json, NULL, pData, nSize))
    {
        XJSON_Destroy(&json);
        return 0;
    }

    char sError[128];
    int nStatus = json.nError != XJSON_ERR_NONE && json.pRootObj == NULL &&
                  XJSON_GetErrorStr(&json, sError, sizeof(sError)) > 0 && sError[0] != '\0';
    XJSON_Destroy(&json);
    return nStatus;
}

static int count_log(const char *pLog, size_t nLength, xlog_flag_t eFlag, void *pContext)
{
    (void)pLog;
    (void)nLength;
    (void)eFlag;
    int *pCount = (int*)pContext;
    (*pCount)++;
    return XSTDNON;
}

typedef struct json_case_ {
    const char *pData;
    size_t nSize;
} json_case_t;

#define JSON_CASE(text) {(text), sizeof(text) - 1}

static int test_parse_matrix(void)
{
    static const json_case_t valid[] = {JSON_CASE("{}"), JSON_CASE("[]"), JSON_CASE(" \t\r\n{}\n\r\t "), JSON_CASE("{\"\":1}"),
        JSON_CASE("{\"empty\":\"\",\"object\":{},\"array\":[]}"),
        JSON_CASE("[null,true,false,\"\",0,-0,1,-1,1.0,-0.5,1e2,1E+2,1e-2]"),
        JSON_CASE("{\"escapes\":\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"}"),
        JSON_CASE("{\"unicode\":\"\\u0000\\u00ff\\uD800\\uDC00\\uFFFF\"}"),
        JSON_CASE("{\"nested\":[{},[],{\"a\":[1,{\"b\":false}]}]}"),
        JSON_CASE("{\"n\":1234567890123456789012345678901234567890}"), JSON_CASE("{\"n\":1.234567890123456789e+123}"),
        JSON_CASE("{\n\"a\"\t:\r1,\n\"b\"\t:\r[\ntrue,\rfalse,\tnull\n]\n}"),
        {"{\"utf8\":\"\xE1\x83\xA5\xE1\x83\x90\xE1\x83\xA0\xE1\x83\x97\xE1\x83\xA3\xE1\x83\x9A\xE1\x83\x98\"}",
            sizeof("{\"utf8\":\"\xE1\x83\xA5\xE1\x83\x90\xE1\x83\xA0\xE1\x83\x97\xE1\x83\xA3\xE1\x83\x9A\xE1\x83\x98\"}") - 1}};

    static const json_case_t invalid[] = {JSON_CASE(""), JSON_CASE(" \t\r\n"), JSON_CASE("null"), JSON_CASE("true"),
        JSON_CASE("0"), JSON_CASE("\"root\""), JSON_CASE("{"), JSON_CASE("["), JSON_CASE("}"), JSON_CASE("]"), JSON_CASE("{]"),
        JSON_CASE("[}"), JSON_CASE("{\"a\"}"), JSON_CASE("{\"a\":}"), JSON_CASE("{:\"a\"}"), JSON_CASE("{,\"a\":1}"),
        JSON_CASE("{\"a\":1,}"), JSON_CASE("{\"a\":1,,\"b\":2}"), JSON_CASE("{\"a\":1 \"b\":2}"), JSON_CASE("{\"a\":1,\"a\":2}"),
        JSON_CASE("[,]"), JSON_CASE("[1,]"), JSON_CASE("[1,,2]"), JSON_CASE("[1 2]"), JSON_CASE("[1,2"), JSON_CASE("{\"a\":01}"),
        JSON_CASE("{\"a\":-01}"), JSON_CASE("{\"a\":+1}"), JSON_CASE("{\"a\":.1}"), JSON_CASE("{\"a\":1.}"),
        JSON_CASE("{\"a\":1e}"), JSON_CASE("{\"a\":1e+}"), JSON_CASE("{\"a\":1e-}"), JSON_CASE("{\"a\":--1}"),
        JSON_CASE("{\"a\":1..2}"), JSON_CASE("{\"a\":NaN}"), JSON_CASE("{\"a\":Infinity}"), JSON_CASE("{\"a\":TRUE}"),
        JSON_CASE("{\"a\":False}"), JSON_CASE("{\"a\":nul}"), JSON_CASE("{\"a\":nullx}"), JSON_CASE("{\"a\":truefalse}"),
        JSON_CASE("{\"a\":\"unterminated}"), JSON_CASE("{\"a\":\"bad\\"), JSON_CASE("{\"a\":\"bad\\q\"}"),
        JSON_CASE("{\"a\":\"bad\\u\"}"), JSON_CASE("{\"a\":\"bad\\u0\"}"), JSON_CASE("{\"a\":\"bad\\u00\"}"),
        JSON_CASE("{\"a\":\"bad\\u000\"}"), JSON_CASE("{\"a\":\"bad\\u00xz\"}"), JSON_CASE("{\"a\":\"raw\nnewline\"}"),
        JSON_CASE("{\"a\":\"raw\ttab\"}"), JSON_CASE("{\"a\":\"\x01\"}"), JSON_CASE("{}{}"), JSON_CASE("{} []"),
        JSON_CASE("{} trailing"), JSON_CASE("{}\v"), JSON_CASE("{}\f")};

    int nLogCount = 0;
    int nValidStatus = 1;
    xlog_init("json-matrix", XLOG_ALL, XFALSE);
    xlog_screen(XFALSE);
    xlog_callback(count_log, &nLogCount);
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++)
    {
        if (!parse_bytes_ok(valid[i].pData, valid[i].nSize))
        {
            nValidStatus = 0;
            break;
        }
    }
    xlog_destroy();
    if (!nValidStatus || nLogCount != 0) return 0;

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        if (!parse_bytes_rejected(invalid[i].pData, invalid[i].nSize)) return 0;

    return 1;
}

static int test_parse_boundaries(void)
{
    static const char sNulAfterRoot[] = {'{', '}', '\0'};
    static const char sNulThenData[] = {'{', '}', '\0', '{', '}'};
    static const char sBounded[] = {'{', '}', 'g', 'a', 'r', 'b', 'a', 'g', 'e'};

    if (!parse_bytes_rejected(NULL, 0) || !parse_bytes_rejected(NULL, 1) ||
        !parse_bytes_rejected(sNulAfterRoot, sizeof(sNulAfterRoot)) ||
        !parse_bytes_rejected(sNulThenData, sizeof(sNulThenData)) || !parse_bytes_ok(sBounded, 2))
        return 0;

    const char *pDoc = "{\"a\":[1,true,null,{\"b\":\"value\"}],\"c\":-1.25e+3}";
    size_t nLength = strlen(pDoc);
    for (size_t i = 0; i < nLength; i++)
        if (!parse_bytes_rejected(pDoc, i)) return 0;

    return parse_bytes_ok(pDoc, nLength);
}

static int test_builders_and_writers(void)
{
    xjson_obj_t *pRoot = XJSON_NewObject(NULL, NULL, XTRUE);
    if (pRoot == NULL) return 0;

    int nStatus =
        XJSON_AddU16(pRoot, "u16", UINT16_MAX) == XJSON_ERR_NONE && XJSON_AddU32(pRoot, "u32", UINT32_MAX) == XJSON_ERR_NONE &&
        XJSON_AddU64(pRoot, "u64", UINT64_MAX) == XJSON_ERR_NONE && XJSON_AddInt(pRoot, "minInt", INT32_MIN) == XJSON_ERR_NONE &&
        XJSON_AddFloat(pRoot, "float", -123.5) == XJSON_ERR_NONE && XJSON_AddBool(pRoot, "true", XTRUE) == XJSON_ERR_NONE &&
        XJSON_AddBool(pRoot, "false", XFALSE) == XJSON_ERR_NONE && XJSON_AddString(pRoot, "", "emptyName") == XJSON_ERR_NONE &&
        XJSON_AddString(pRoot, "empty", "") == XJSON_ERR_NONE &&
        XJSON_AddString(pRoot, "nullFromString", NULL) == XJSON_ERR_NONE && XJSON_AddNull(pRoot, "null") == XJSON_ERR_NONE &&
        XJSON_AddStrIfUsed(pRoot, "unused", "") == XJSON_ERR_NONE && XJSON_GetObject(pRoot, "unused") == NULL;
    if (!nStatus)
    {
        XJSON_FreeObject(pRoot);
        return 0;
    }

    if (XJSON_AddInt(pRoot, "minInt", 77) != XJSON_ERR_NONE || XJSON_GetInt(XJSON_GetObject(pRoot, "minInt")) != 77)
    {
        XJSON_FreeObject(pRoot);
        return 0;
    }

    xjson_obj_t *pUpdated = XJSON_GetObject(pRoot, "minInt");
    xjson_obj_t *pUnnamedObjectChild = XJSON_NewInt(NULL, NULL, 1);
    if (pUpdated == NULL || XJSON_AddObject(pRoot, pUpdated) != XJSON_ERR_NONE ||
        XJSON_GetInt(XJSON_GetObject(pRoot, "minInt")) != 77 || pUnnamedObjectChild == NULL ||
        XJSON_AddObject(pRoot, pUnnamedObjectChild) != XJSON_ERR_INVALID ||
        XJSON_GetOrCreateObject(pRoot, "minInt", XFALSE) != NULL || XJSON_GetOrCreateArray(pRoot, "minInt", XFALSE) != NULL)
    {
        XJSON_FreeObject(pUnnamedObjectChild);
        XJSON_FreeObject(pRoot);
        return 0;
    }
    XJSON_FreeObject(pUnnamedObjectChild);

    xjson_obj_t *pNested = XJSON_GetOrCreateObject(pRoot, "nested", XFALSE);
    xjson_obj_t *pArray = XJSON_GetOrCreateArray(pRoot, "array", XFALSE);
    if (pNested == NULL || pArray == NULL || XJSON_AddString(pNested, "value", "nested") != XJSON_ERR_NONE)
    {
        XJSON_FreeObject(pRoot);
        return 0;
    }

    xjson_obj_t *pNamedArrayChild = XJSON_NewInt(NULL, "named", 1);
    if (pNamedArrayChild == NULL || XJSON_AddObject(pArray, pNamedArrayChild) != XJSON_ERR_INVALID)
    {
        XJSON_FreeObject(pNamedArrayChild);
        XJSON_FreeObject(pRoot);
        return 0;
    }
    XJSON_FreeObject(pNamedArrayChild);

    for (int i = 0; i < 5; i++)
    {
        xjson_obj_t *pItem = XJSON_NewInt(NULL, NULL, i);
        if (pItem == NULL || XJSON_AddObject(pArray, pItem) != XJSON_ERR_NONE)
        {
            XJSON_FreeObject(pItem);
            XJSON_FreeObject(pRoot);
            return 0;
        }
    }

    if (XJSON_RemoveArrayItem(pArray, 0) != XARRAY_SUCCESS || XJSON_RemoveArrayItem(pArray, 2) != XARRAY_SUCCESS ||
        XJSON_RemoveArrayItem(pArray, 2) != XARRAY_SUCCESS || XJSON_GetArrayLength(pArray) != 2 ||
        XJSON_GetInt(XJSON_GetArrayItem(pArray, 0)) != 1 || XJSON_GetInt(XJSON_GetArrayItem(pArray, 1)) != 2)
    {
        XJSON_FreeObject(pRoot);
        return 0;
    }

    xarray_t *pObjects = XJSON_GetObjects(pRoot);
    if (pObjects == NULL || XArray_Used(pObjects) != 13)
    {
        XArray_Destroy(pObjects);
        XJSON_FreeObject(pRoot);
        return 0;
    }
    XArray_Destroy(pObjects);

    size_t nCompactLen = 0;
    size_t nPrettyLen = 0;
    char *pCompact = XJSON_DumpObj(pRoot, 0, &nCompactLen);
    char *pPretty = XJSON_DumpObj(pRoot, 2, &nPrettyLen);
    if (pCompact == NULL || pPretty == NULL || nCompactLen != strlen(pCompact) || nPrettyLen != strlen(pPretty) ||
        strchr(pCompact, '\n') != NULL || strchr(pPretty, '\n') == NULL || !parse_bytes_ok(pCompact, nCompactLen) ||
        !parse_bytes_ok(pPretty, nPrettyLen))
    {
        free(pCompact);
        free(pPretty);
        XJSON_FreeObject(pRoot);
        return 0;
    }

    xjson_t wrapper;
    XJSON_Init(&wrapper);
    wrapper.pRootObj = pRoot;

    char sOutput[4096];
    char sTiny[4] = "xxx";
    if (!XJSON_Write(&wrapper, sOutput, sizeof(sOutput)) || !parse_ok(sOutput) || XJSON_Write(&wrapper, sTiny, sizeof(sTiny)))
    {
        free(pCompact);
        free(pPretty);
        XJSON_FreeObject(pRoot);
        return 0;
    }

    xjson_writer_t dynamicWriter;
    xjson_writer_t fixedWriter;
    char sFixed[4096];
    if (!XJSON_InitWriter(&dynamicWriter, NULL, NULL, 1) || !XJSON_WriteObject(pRoot, &dynamicWriter) ||
        !parse_bytes_ok(dynamicWriter.pData, dynamicWriter.nLength) ||
        !XJSON_InitWriter(&fixedWriter, NULL, sFixed, sizeof(sFixed)) || !XJSON_WriteObject(pRoot, &fixedWriter) ||
        !parse_bytes_ok(fixedWriter.pData, fixedWriter.nLength))
    {
        XJSON_DestroyWriter(&dynamicWriter);
        free(pCompact);
        free(pPretty);
        XJSON_FreeObject(pRoot);
        return 0;
    }
    XJSON_DestroyWriter(&dynamicWriter);

    xjson_format_t format;
    XJSON_FormatInit(&format);
    size_t nFormatLen = 0;
    char *pFormatted = XJSON_FormatObj(pRoot, 2, &format, &nFormatLen);
    if (pFormatted == NULL || nFormatLen != strlen(pFormatted))
    {
        free(pFormatted);
        free(pCompact);
        free(pPretty);
        XJSON_FreeObject(pRoot);
        return 0;
    }

    free(pFormatted);
    free(pCompact);
    free(pPretty);
    XJSON_FreeObject(pRoot);

    xjson_obj_t *pFromStr = XJSON_FromStr(NULL, "{\"ok\":true,\"a\":[1,2,3]}");
    if (pFromStr == NULL) return 0;
    nStatus = XJSON_GetBool(XJSON_GetObject(pFromStr, "ok")) && XJSON_GetArrayLength(XJSON_GetObject(pFromStr, "a")) == 3;
    XJSON_FreeObject(pFromStr);
    return nStatus;
}

static int test_generated_string_edges(void)
{
    xjson_obj_t *pRoot = XJSON_NewObject(NULL, NULL, XFALSE);
    if (pRoot == NULL) return 0;

    int nStatus = XJSON_AddString(pRoot, "quote\"key", "quote\"value") == XJSON_ERR_NONE &&
                  XJSON_AddString(pRoot, "backslash", "C:\\path\\file") == XJSON_ERR_NONE &&
                  XJSON_AddString(pRoot, "validEscapes", "\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0041") == XJSON_ERR_NONE &&
                  XJSON_AddString(pRoot, "controls", "line\nrow\rtab\tback\bform\f") == XJSON_ERR_NONE &&
                  XJSON_AddString(pRoot, "control\nkey", "\x01") == XJSON_ERR_NONE &&
                  XJSON_AddString(pRoot, "utf8", "ქართული") == XJSON_ERR_NONE &&
                  XJSON_AddFloat(pRoot, "nan", NAN) == XJSON_ERR_INVALID &&
                  XJSON_AddFloat(pRoot, "positiveInf", INFINITY) == XJSON_ERR_INVALID &&
                  XJSON_AddFloat(pRoot, "negativeInf", -INFINITY) == XJSON_ERR_INVALID;
    if (!nStatus)
    {
        XJSON_FreeObject(pRoot);
        return 0;
    }

    size_t nLength = 0;
    char *pDump = XJSON_DumpObj(pRoot, 0, &nLength);
    nStatus = pDump != NULL && nLength == strlen(pDump) && strstr(pDump, "\\\"") != NULL && strstr(pDump, "\\\\path") != NULL &&
              strstr(pDump, "\\u0001") != NULL && strstr(pDump, "\\n") != NULL && strstr(pDump, "\\r") != NULL &&
              strstr(pDump, "\\t") != NULL && parse_bytes_ok(pDump, nLength);

    free(pDump);
    XJSON_FreeObject(pRoot);
    return nStatus;
}

static int test_byte_boundaries(void)
{
    for (int nByte = 0; nByte <= UINT8_MAX; nByte++)
    {
        char sTrailing[] = {'{', '}', (char)nByte};
        int nWhitespace = nByte == ' ' || nByte == '\n' || nByte == '\r' || nByte == '\t';
        if (parse_bytes_ok(sTrailing, sizeof(sTrailing)) != nWhitespace) return 0;
    }

    for (int nByte = 0; nByte <= 0x7f; nByte++)
    {
        char sString[] = {'{', '"', 'a', '"', ':', '"', (char)nByte, '"', '}'};
        int nValid = nByte >= 0x20 && nByte != '"' && nByte != '\\';
        if (parse_bytes_ok(sString, sizeof(sString)) != nValid) return 0;
    }

    return 1;
}

static int test_error_states(void)
{
    static const struct
    {
        const char *pData;
        size_t nSize;
        xjson_error_t nError;
        size_t nOffset;
    } cases[] = {{"{", 1, XJSON_ERR_BOUNDS, 1}, {"[", 1, XJSON_ERR_BOUNDS, 1},
        {"{\"a\":\"unterminated", sizeof("{\"a\":\"unterminated") - 1, XJSON_ERR_BOUNDS, sizeof("{\"a\":\"unterminated") - 1},
        {"{\"a\":-", sizeof("{\"a\":-") - 1, XJSON_ERR_BOUNDS, sizeof("{\"a\":-") - 1}, {"{}x", 3, XJSON_ERR_UNEXPECTED, 2},
        {"{\"a\":1,\"a\":2}", sizeof("{\"a\":1,\"a\":2}") - 1, XJSON_ERR_EXITS, 12}};

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        xjson_t json;
        if (XJSON_Parse(&json, NULL, cases[i].pData, cases[i].nSize))
        {
            XJSON_Destroy(&json);
            return 0;
        }

        char sError[128];
        int nStatus = json.nError == cases[i].nError && json.nOffset == cases[i].nOffset && json.pRootObj == NULL &&
                      XJSON_GetErrorStr(&json, sError, sizeof(sError)) > 0;
        if (!nStatus)
            fprintf(stderr, "json_smoke: error case %zu got error(%d) offset(%zu), expected error(%d) offset(%zu)\n", i,
                json.nError, json.nOffset, cases[i].nError, cases[i].nOffset);
        XJSON_Destroy(&json);
        if (!nStatus) return 0;
    }

    return 1;
}

static int test_pool_and_stress(void)
{
    xpool_t pool;
    if (XPool_Init(&pool, 64) != XSTDOK) return 0;

    const char *pDoc = "{\"pool\":[1,2,3],\"nested\":{\"ok\":true}}";
    xjson_t pooled;
    if (!XJSON_Parse(&pooled, &pool, pDoc, strlen(pDoc)))
    {
        XPool_Destroy(&pool);
        return 0;
    }

    size_t nDumpLen = 0;
    char *pDump = XJSON_Dump(&pooled, 2, &nDumpLen);
    int nStatus = pDump != NULL && parse_bytes_ok(pDump, nDumpLen);
    XJSON_Destroy(&pooled);
    XPool_Destroy(&pool);
    if (!nStatus) return 0;

    const size_t nItems = 2000;
    size_t nCapacity = nItems * 12 + 2;
    char *pArray = (char*)malloc(nCapacity);
    if (pArray == NULL) return 0;

    size_t nOffset = 0;
    pArray[nOffset++] = '[';
    for (size_t i = 0; i < nItems; i++)
    {
        int nWritten = snprintf(&pArray[nOffset], nCapacity - nOffset, "%s%zu", i ? "," : "", i);
        if (nWritten < 0 || (size_t)nWritten >= nCapacity - nOffset)
        {
            free(pArray);
            return 0;
        }
        nOffset += (size_t)nWritten;
    }
    pArray[nOffset++] = ']';

    xjson_t large;
    nStatus = XJSON_Parse(&large, NULL, pArray, nOffset) && XJSON_GetArrayLength(large.pRootObj) == nItems;
    if (nStatus) XJSON_Destroy(&large);
    free(pArray);
    if (!nStatus) return 0;

    const size_t nPairs = 1000;
    nCapacity = nPairs * 24 + 2;
    char *pObject = (char*)malloc(nCapacity);
    if (pObject == NULL) return 0;

    nOffset = 0;
    pObject[nOffset++] = '{';
    for (size_t i = 0; i < nPairs; i++)
    {
        int nWritten = snprintf(&pObject[nOffset], nCapacity - nOffset, "%s\"k%zu\":%zu", i ? "," : "", i, i);
        if (nWritten < 0 || (size_t)nWritten >= nCapacity - nOffset)
        {
            free(pObject);
            return 0;
        }
        nOffset += (size_t)nWritten;
    }
    pObject[nOffset++] = '}';

    nStatus = XJSON_Parse(&large, NULL, pObject, nOffset) && XJSON_GetU32(XJSON_GetObject(large.pRootObj, "k999")) == 999;
    if (nStatus) XJSON_Destroy(&large);
    free(pObject);
    if (!nStatus) return 0;

    char sDeep[2048];
    nOffset = 0;
    const size_t nDepth = 128;
    for (size_t i = 0; i < nDepth; i++) sDeep[nOffset++] = '[';
    sDeep[nOffset++] = '0';
    for (size_t i = 0; i < nDepth; i++) sDeep[nOffset++] = ']';
    return parse_bytes_ok(sDeep, nOffset);
}

static int test_root_array_and_api_boundaries(void)
{
    xjson_obj_t *pRoot = XJSON_NewArray(NULL, NULL, XFALSE);
    if (pRoot == NULL) return 0;

    xjson_obj_t *pNestedObject = XJSON_NewObject(NULL, NULL, XFALSE);
    xjson_obj_t *pNestedArray = XJSON_NewArray(NULL, NULL, XFALSE);
    if (pNestedObject == NULL || pNestedArray == NULL || XJSON_AddString(pNestedObject, "key", "value") != XJSON_ERR_NONE ||
        XJSON_AddObject(pNestedArray, XJSON_NewBool(NULL, NULL, XTRUE)) != XJSON_ERR_NONE ||
        XJSON_AddObject(pRoot, XJSON_NewNull(NULL, NULL)) != XJSON_ERR_NONE ||
        XJSON_AddObject(pRoot, XJSON_NewString(NULL, NULL, "text")) != XJSON_ERR_NONE ||
        XJSON_AddObject(pRoot, XJSON_NewInt(NULL, NULL, -7)) != XJSON_ERR_NONE ||
        XJSON_AddObject(pRoot, XJSON_NewFloat(NULL, NULL, 2.5)) != XJSON_ERR_NONE ||
        XJSON_AddObject(pRoot, pNestedObject) != XJSON_ERR_NONE || XJSON_AddObject(pRoot, pNestedArray) != XJSON_ERR_NONE)
    {
        XJSON_FreeObject(pNestedObject);
        XJSON_FreeObject(pNestedArray);
        XJSON_FreeObject(pRoot);
        return 0;
    }

    size_t nLength = 0;
    char *pDump = XJSON_DumpObj(pRoot, 0, &nLength);
    if (pDump == NULL || !parse_bytes_ok(pDump, nLength))
    {
        free(pDump);
        XJSON_FreeObject(pRoot);
        return 0;
    }

    xjson_t wrapper;
    XJSON_Init(&wrapper);
    wrapper.pRootObj = pRoot;

    char *pExact = (char*)malloc(nLength + 1);
    char *pShort = (char*)malloc(nLength);
    if (pExact == NULL || pShort == NULL || !XJSON_Write(&wrapper, pExact, nLength + 1) ||
        XJSON_Write(&wrapper, pShort, nLength) || strcmp(pExact, pDump) != 0)
    {
        free(pExact);
        free(pShort);
        free(pDump);
        XJSON_FreeObject(pRoot);
        return 0;
    }

    size_t nWrapperDumpLen = 0;
    size_t nWrapperFormatLen = 0;
    char *pWrapperDump = XJSON_Dump(&wrapper, 0, &nWrapperDumpLen);
    char *pWrapperFormat = XJSON_Format(&wrapper, 2, NULL, &nWrapperFormatLen);
    xjson_writer_t invalidWriter;
    char sZeroSize[1] = {'\0'};
    char sError[8];
    int nStatus =
        pWrapperDump != NULL && nWrapperDumpLen == strlen(pWrapperDump) && pWrapperFormat != NULL &&
        nWrapperFormatLen == strlen(pWrapperFormat) && XJSON_Write(NULL, pExact, nLength + 1) == XJSON_FAILURE &&
        XJSON_Write(&wrapper, NULL, nLength + 1) == XJSON_FAILURE && XJSON_Write(&wrapper, pExact, 0) == XJSON_FAILURE &&
        XJSON_DumpObj(NULL, 0, NULL) == NULL && XJSON_FormatObj(NULL, 0, NULL, NULL) == NULL &&
        XJSON_GetErrorStr(NULL, sError, sizeof(sError)) == 0 && XJSON_GetErrorStr(&wrapper, NULL, sizeof(sError)) == 0 &&
        XJSON_GetErrorStr(&wrapper, sError, 0) == 0 && XJSON_InitWriter(NULL, NULL, NULL, 1) == XJSON_FAILURE &&
        XJSON_InitWriter(&invalidWriter, NULL, sZeroSize, 0) == XJSON_FAILURE &&
        XJSON_AddObject(NULL, NULL) == XJSON_ERR_INVALID && XJSON_AddU16(NULL, "x", 1) == XJSON_ERR_INVALID &&
        XJSON_AddU32(NULL, "x", 1) == XJSON_ERR_INVALID && XJSON_AddU64(NULL, "x", 1) == XJSON_ERR_INVALID &&
        XJSON_AddInt(NULL, "x", 1) == XJSON_ERR_INVALID && XJSON_AddFloat(NULL, "x", 1.0) == XJSON_ERR_INVALID &&
        XJSON_AddString(NULL, "x", "x") == XJSON_ERR_INVALID && XJSON_AddStrIfUsed(NULL, "x", "x") == XJSON_ERR_INVALID &&
        XJSON_AddBool(NULL, "x", XTRUE) == XJSON_ERR_INVALID && XJSON_AddNull(NULL, "x") == XJSON_ERR_INVALID &&
        XJSON_NewFloat(NULL, "nan", NAN) == NULL && XJSON_NewFloat(NULL, "inf", INFINITY) == NULL &&
        XJSON_GetOrCreateObject(NULL, "x", XFALSE) == NULL && XJSON_GetOrCreateArray(NULL, "x", XFALSE) == NULL &&
        XJSON_GetObject(NULL, "x") == NULL && XJSON_GetArrayItem(NULL, 0) == NULL && XJSON_GetArrayLength(NULL) == 0 &&
        XJSON_GetObjects(NULL) == NULL && XJSON_GetInt(NULL) == 0 && XJSON_GetU16(NULL) == 0 && XJSON_GetU32(NULL) == 0 &&
        XJSON_GetU64(NULL) == 0 && XJSON_GetFloat(NULL) == 0.0 && XJSON_GetBool(NULL) == 0 &&
        strcmp(XJSON_GetString(NULL), "") == 0 && XJSON_GetInt(XJSON_GetArrayItem(pRoot, 1)) == 0 &&
        XJSON_GetString(XJSON_GetArrayItem(pRoot, 2))[0] == '\0';

    free(pWrapperDump);
    free(pWrapperFormat);
    free(pExact);
    free(pShort);
    free(pDump);
    XJSON_FreeObject(pRoot);
    XJSON_Init(NULL);
    XJSON_FormatInit(NULL);
    XJSON_Destroy(NULL);
    XJSON_FreeObject(NULL);
    return nStatus;
}

static int XTest_parse_matrix(void)
{
    CHECK(test_parse_matrix(), "JSON parse_matrix");
    return 0;
}

static int XTest_parse_boundaries(void)
{
    CHECK(test_parse_boundaries(), "JSON parse_boundaries");
    return 0;
}

static int XTest_builders_and_writers(void)
{
    CHECK(test_builders_and_writers(), "JSON builders_and_writers");
    return 0;
}

static int XTest_generated_string_edges(void)
{
    CHECK(test_generated_string_edges(), "JSON generated_string_edges");
    return 0;
}

static int XTest_byte_boundaries(void)
{
    CHECK(test_byte_boundaries(), "JSON byte_boundaries");
    return 0;
}

static int XTest_error_states(void)
{
    CHECK(test_error_states(), "JSON error_states");
    return 0;
}

static int XTest_pool_and_stress(void)
{
    CHECK(test_pool_and_stress(), "JSON pool_and_stress");
    return 0;
}

static int XTest_root_array_and_api_boundaries(void)
{
    CHECK(test_root_array_and_api_boundaries(), "JSON root_array_and_api_boundaries");
    return 0;
}

static int XTest_empty_objects(void)
{
    /* An empty object has an empty list of members, which is not a failure:
       only something that is not an object has no list at all. */
    const char data[] = "{\"empty\":{},\"array\":[],\"one\":{\"key\":1},\"two\":{\"a\":true,\"b\":null}}";

    for (int nPool = 0; nPool < 2; nPool++)
    {
        xpool_t *pPool = nPool ? XPool_Create(256) : NULL;
        CHECK(!nPool || pPool != NULL, "Create a pool for the second pass");

        xjson_t json;
        CHECK(XJSON_Parse(&json, pPool, data, sizeof(data) - 1) == XJSON_SUCCESS, "The document parses");

        xarray_t *pEmpty = XJSON_GetObjects(XJSON_GetObject(json.pRootObj, "empty"));
        CHECK(pEmpty != NULL && XArray_Used(pEmpty) == 0, "An empty object lists no members");
        XArray_Destroy(pEmpty);

        xarray_t *pOne = XJSON_GetObjects(XJSON_GetObject(json.pRootObj, "one"));
        CHECK(pOne != NULL && XArray_Used(pOne) == 1, "An object with one member lists it");
        xmap_pair_t *pPair = (xmap_pair_t*)XArray_GetData(pOne, 0);
        CHECK(pPair != NULL && strcmp(pPair->pKey, "key") == 0 && XJSON_GetInt((xjson_obj_t*)pPair->pData) == 1,
            "The listed member is the pair itself");
        XArray_Destroy(pOne);

        xarray_t *pTwo = XJSON_GetObjects(XJSON_GetObject(json.pRootObj, "two"));
        CHECK(pTwo != NULL && XArray_Used(pTwo) == 2, "Every member is listed");
        XArray_Destroy(pTwo);

        xarray_t *pRoot = XJSON_GetObjects(json.pRootObj);
        CHECK(pRoot != NULL && XArray_Used(pRoot) == 4, "The root lists its members");
        XArray_Destroy(pRoot);

        CHECK(XJSON_GetObjects(XJSON_GetObject(json.pRootObj, "array")) == NULL, "An array is not an object");
        CHECK(XJSON_GetObjects(XJSON_GetObject(json.pRootObj, "missing")) == NULL, "A missing member has no list");

        /* An object emptied by the builder API lists nothing as well */
        xjson_obj_t *pBuilt = XJSON_NewObject(pPool, NULL, XFALSE);
        CHECK(pBuilt != NULL, "Build an empty object");
        xarray_t *pBuiltList = XJSON_GetObjects(pBuilt);
        CHECK(pBuiltList != NULL && XArray_Used(pBuiltList) == 0, "A built empty object lists no members");
        XArray_Destroy(pBuiltList);
        XJSON_FreeObject(pBuilt);

        XJSON_Destroy(&json);
        XPool_Destroy(pPool);
    }

    return 0;
}

static int XTest_pair_names(void)
{
    /* Member names are kept exactly as they appear between the quotes, for
       values, objects and arrays alike, however long or short they are. */
    size_t nLongName = 5000;
    size_t nSize = nLongName + 512;
    char *pData = (char*)malloc(nSize);
    char *pLongName = (char*)malloc(nLongName + 1);
    CHECK(pData != NULL && pLongName != NULL, "Allocate the document");

    for (size_t i = 0; i < nLongName; i++) pLongName[i] = (char)('a' + i % 26);
    pLongName[nLongName] = '\0';

    int nLength = snprintf(pData, nSize, "{\"%s\":1,\"\":{\"\":[]},\"a\\\"b\":\"x\",\"\\u00e9\":true,"
        "\"nested\":{\"inner\":{\"deep\":[{\"x\":null}]}},\"k\":[1,{\"k\":2}]}", pLongName);
    CHECK(nLength > 0 && (size_t)nLength < nSize, "Build the document");

    for (int nPool = 0; nPool < 2; nPool++)
    {
        xpool_t *pPool = nPool ? XPool_Create(1024) : NULL;
        CHECK(!nPool || pPool != NULL, "Create a pool for the second pass");

        xjson_t json;
        CHECK(XJSON_Parse(&json, pPool, pData, (size_t)nLength) == XJSON_SUCCESS, "The document parses");
        xjson_obj_t *pRoot = json.pRootObj;

        xjson_obj_t *pLong = XJSON_GetObject(pRoot, pLongName);
        CHECK(pLong != NULL && XJSON_GetInt(pLong) == 1, "A very long name is found");
        CHECK(pLong->pName != NULL && strcmp(pLong->pName, pLongName) == 0, "And is kept whole");

        xjson_obj_t *pEmpty = XJSON_GetObject(pRoot, "");
        CHECK(pEmpty != NULL && pEmpty->nType == XJSON_TYPE_OBJECT, "An empty name is a name");
        xjson_obj_t *pInner = XJSON_GetObject(pEmpty, "");
        CHECK(pInner != NULL && pInner->nType == XJSON_TYPE_ARRAY && XJSON_GetArrayLength(pInner) == 0,
            "Even for a nested array");

        CHECK(strcmp(XJSON_GetString(XJSON_GetObject(pRoot, "a\\\"b")), "x") == 0, "An escaped quote stays in the name");
        CHECK(XJSON_GetBool(XJSON_GetObject(pRoot, "\\u00e9")) == 1, "A unicode escape stays in the name as written");

        xjson_obj_t *pDeep = XJSON_GetObject(XJSON_GetObject(XJSON_GetObject(pRoot, "nested"), "inner"), "deep");
        CHECK(pDeep != NULL && XJSON_GetArrayLength(pDeep) == 1, "Nested names are found at every level");
        xjson_obj_t *pItem = XJSON_GetArrayItem(pDeep, 0);
        CHECK(pItem != NULL && pItem->pName == NULL, "An array item has no name");
        CHECK(XJSON_GetObject(pItem, "x") != NULL && XJSON_GetObject(pItem, "x")->nType == XJSON_TYPE_NULL,
            "A member of an object inside an array keeps its name");

        xjson_obj_t *pK = XJSON_GetObject(pRoot, "k");
        CHECK(pK != NULL && XJSON_GetInt(XJSON_GetObject(XJSON_GetArrayItem(pK, 1), "k")) == 2,
            "The same name at different levels names different members");

        /* What is written back parses to the same names */
        size_t nDumped = 0;
        char *pDump = XJSON_DumpObj(pRoot, 0, &nDumped);
        CHECK(pDump != NULL && nDumped > nLongName, "The document dumps");

        xjson_t again;
        CHECK(XJSON_Parse(&again, NULL, pDump, nDumped) == XJSON_SUCCESS, "The dump parses");
        CHECK(XJSON_GetInt(XJSON_GetObject(again.pRootObj, pLongName)) == 1, "The long name survives the round trip");
        CHECK(strcmp(XJSON_GetString(XJSON_GetObject(again.pRootObj, "a\\\"b")), "x") == 0, "So does the escaped one");
        XJSON_Destroy(&again);
        if (!nPool) free(pDump);

        XJSON_Destroy(&json);
        XPool_Destroy(pPool);
    }

    /* The same name twice in one object is refused, wherever it is */
    const char *pDuplicates[] = { "{\"a\":1,\"a\":2}", "{\"o\":{\"b\":[],\"b\":{}}}", "[{\"c\":1,\"d\":2,\"c\":3}]" };
    for (size_t i = 0; i < sizeof(pDuplicates) / sizeof(*pDuplicates); i++)
    {
        xjson_t json;
        CHECK(XJSON_Parse(&json, NULL, pDuplicates[i], strlen(pDuplicates[i])) == XJSON_FAILURE, "A duplicate is refused");
        CHECK(json.nError == XJSON_ERR_EXITS && json.pRootObj == NULL, "As a duplicate, with nothing left behind");

        char sError[128];
        CHECK(XJSON_GetErrorStr(&json, sError, sizeof(sError)) > 0 && strstr(sError, "Duplicate") != NULL,
            "The error names the duplicate");
        XJSON_Destroy(&json);
    }

    free(pLongName);
    free(pData);
    return 0;
}

/* XJSON_ScanFlat against XJSON_Parse on the same bytes. Whatever the scan accepts has to parse, and every field it
   reports has to be the parsed member, by type and by text; a flat object that parses has to scan. Returns 1 when
   the two agree, and fills *pScanned so a caller can count how often the scan took the input. */
static int scan_agrees(const char *pData, size_t nSize, int *pScanned)
{
    xjson_field_t fields[] = {
        { "type", NULL, 0, 0 }, { "sessionId", NULL, 0, 0 }, { "payloadSize", NULL, 0, 0 }, { "encrypted", NULL, 0, 0 },
        { "", NULL, 0, 0 }, { "a", NULL, 0, 0 }, { "a\\\"b", NULL, 0, 0 }, { "missing", NULL, 0, 0 }
    };

    size_t nFields = sizeof(fields) / sizeof(fields[0]);
    xbool_t bScanned = XJSON_ScanFlat(pData, nSize, fields, nFields);
    *pScanned = bScanned ? 1 : 0;

    xjson_t json;
    int nParsed = XJSON_Parse(&json, NULL, pData, nSize);
    xjson_obj_t *pRoot = nParsed ? json.pRootObj : NULL;
    int nAgrees = 1;

    xbool_t bFlat = pRoot != NULL && pRoot->nType == XJSON_TYPE_OBJECT;
    xarray_t *pMembers = bFlat ? XJSON_GetObjects(pRoot) : NULL;
    if (bFlat && (pMembers == NULL || XArray_Used(pMembers) > XJSON_SCAN_MEMBERS)) bFlat = XFALSE;

    for (size_t i = 0; bFlat && i < XArray_Used(pMembers); i++)
    {
        xmap_pair_t *pPair = (xmap_pair_t*)XArray_GetData(pMembers, i);
        xjson_obj_t *pMember = pPair != NULL ? (xjson_obj_t*)pPair->pData : NULL;
        if (pMember == NULL || pMember->nType == XJSON_TYPE_OBJECT || pMember->nType == XJSON_TYPE_ARRAY) bFlat = XFALSE;
    }

    if (bScanned != bFlat) nAgrees = 0;

    for (size_t i = 0; bScanned && nAgrees && i < nFields; i++)
    {
        xjson_obj_t *pMember = XJSON_GetObject(pRoot, fields[i].pName);
        if (fields[i].nType == XJSON_TYPE_INVALID)
        {
            if (pMember != NULL || fields[i].pValue != NULL || fields[i].nLength) nAgrees = 0;
            continue;
        }

        const char *pText = pMember != NULL ? (const char*)pMember->pData : NULL;
        if (pText == NULL || pMember->nType != fields[i].nType || strlen(pText) != fields[i].nLength ||
            memcmp(pText, fields[i].pValue, fields[i].nLength)) nAgrees = 0;
    }

    XArray_Destroy(pMembers);
    XJSON_Destroy(&json);
    return nAgrees;
}

static int XTest_scan_flat(void)
{
    const char sHeader[] = "{\"version\":1,\"type\":\"encrypted\",\"sessionId\":42,\"encrypted\":true,\"payloadSize\":300}";
    xjson_field_t fields[] = {
        { "type", NULL, 0, 0 }, { "sessionId", NULL, 0, 0 }, { "encrypted", NULL, 0, 0 }, { "missing", NULL, 0, 0 }
    };

    CHECK(XJSON_ScanFlat(sHeader, sizeof(sHeader) - 1, fields, 4), "A protocol header scans");
    CHECK(fields[0].nType == XJSON_TYPE_STRING && fields[0].nLength == 9 && !memcmp(fields[0].pValue, "encrypted", 9),
        "A string is reported without its quotes");
    CHECK(fields[1].nType == XJSON_TYPE_NUMBER && fields[1].nLength == 2 && !memcmp(fields[1].pValue, "42", 2),
        "A number is reported as written");
    CHECK(fields[2].nType == XJSON_TYPE_BOOLEAN && fields[2].nLength == 4, "A literal is reported as written");
    CHECK(fields[3].nType == XJSON_TYPE_INVALID && fields[3].pValue == NULL, "A member that is not there is absent");
    CHECK(XJSON_ScanFlat(sHeader, sizeof(sHeader) - 1, NULL, 0), "Nothing has to be asked for");
    CHECK(!XJSON_ScanFlat(NULL, 0, NULL, 0) && !XJSON_ScanFlat(sHeader, sizeof(sHeader) - 1, NULL, 1),
        "Missing input or fields are refused");

    /* Only a flat object is taken. Each of these parses, and is left to the parser. */
    const char *pDeclined[] = { "[]", "[1,2]", "{\"a\":{}}", "{\"a\":[]}", "{\"a\":1,\"b\":{\"c\":2}}" };
    for (size_t i = 0; i < sizeof(pDeclined) / sizeof(*pDeclined); i++)
    {
        int nScanned = 0;
        CHECK(parse_ok(pDeclined[i]) && !XJSON_ScanFlat(pDeclined[i], strlen(pDeclined[i]), NULL, 0),
            "A document with nesting is declined");
        CHECK(scan_agrees(pDeclined[i], strlen(pDeclined[i]), &nScanned) && !nScanned, "And the parser agrees");
    }

    /* A name twice is refused here because the parser refuses it, even when that name is one being asked for */
    const char sTwice[] = "{\"type\":\"encrypted\",\"sessionId\":1,\"type\":\"webrtc\"}";
    CHECK(!XJSON_ScanFlat(sTwice, sizeof(sTwice) - 1, fields, 4), "A duplicate name is refused");

    /* The member limit: one more than XJSON_SCAN_MEMBERS goes to the parser */
    char sMany[2048];
    for (size_t nMembers = XJSON_SCAN_MEMBERS - 1; nMembers <= XJSON_SCAN_MEMBERS + 1; nMembers++)
    {
        size_t nUsed = (size_t)snprintf(sMany, sizeof(sMany), "{");
        for (size_t i = 0; i < nMembers; i++)
            nUsed += (size_t)snprintf(sMany + nUsed, sizeof(sMany) - nUsed, "%s\"m%zu\":%zu", i ? "," : "", i, i);
        nUsed += (size_t)snprintf(sMany + nUsed, sizeof(sMany) - nUsed, "}");

        int nScanned = 0;
        CHECK(scan_agrees(sMany, nUsed, &nScanned), "A long object scans as it parses");
        CHECK(nScanned == (nMembers <= XJSON_SCAN_MEMBERS), "Up to the limit an object is taken, past it it is not");
    }

    /* Every case the parser is held to, valid and invalid, plus every cut of a header */
    const char *pCases[] = { "{}", " \t\r\n{}\n\r\t ", "{\"\":1}", "{\"a\":\"\"}", "{\"a\":-0}", "{\"a\":1.5e-3}",
        "{\"a\":null,\"b\":true,\"c\":false}", "{\"a\\\"b\":\"x\",\"\\u00e9\":1}", "{\"a\":\"\\u0000\\\\\\\"\"}",
        "{\"a\":1,\"a\":2}", "{\"a\":1,}", "{,\"a\":1}", "{\"a\" 1}", "{\"a\":01}", "{\"a\":nul}", "{\"a\":nullx}",
        "{\"a\":\"raw\nline\"}", "{\"a\":1} x", "{\"a\":1}{}", "{\"a\":1", "{\"a\":", "{\"a\"", "{", "", " ", "null",
        "\"a\"", "{\"a\":1}\v", "{\"a\":TRUE}", "{\"sessionId\":99999999999999999999999}", "{\"type\":5}" };

    for (size_t i = 0; i < sizeof(pCases) / sizeof(*pCases); i++)
    {
        int nScanned = 0;
        CHECK(scan_agrees(pCases[i], strlen(pCases[i]), &nScanned), "The scan agrees with the parser on a fixed case");
    }

    for (size_t i = 0; i <= sizeof(sHeader) - 1; i++)
    {
        int nScanned = 0;
        CHECK(scan_agrees(sHeader, i, &nScanned), "The scan agrees with the parser on every cut of a header");
        CHECK(nScanned == (i == sizeof(sHeader) - 1), "Only the whole header is taken");
    }

    /* Mutations of real headers: flips, deletions and insertions of the characters the grammar turns on */
    const char *pSeeds[] = { sHeader, "{\"type\":\"webrtc\",\"sessionId\":7,\"a\":null,\"\":-12.5e+3}",
        "{ \"type\" : \"status\" , \"a\\\"b\" : \"\\u00ff\\n\" }" };
    const char sAlphabet[] = "{}[]:,\"\\ \t\n0129-+.eEtrufalsn\x01\x7f";
    uint32_t nState = 0x2545F491;
    int nTaken = 0;

    for (size_t nRound = 0; nRound < 30000; nRound++)
    {
        char sDoc[256];
        const char *pSeed = pSeeds[nRound % (sizeof(pSeeds) / sizeof(*pSeeds))];
        size_t nLength = strlen(pSeed);
        memcpy(sDoc, pSeed, nLength);

        for (int nEdit = 0; nEdit < 1 + (int)(nRound % 3); nEdit++)
        {
            nState = nState * 1664525u + 1013904223u;
            size_t nAt = (nState >> 8) % (nLength + 1);
            char cWith = sAlphabet[(nState >> 20) % (sizeof(sAlphabet) - 1)];
            int nKind = (int)((nState >> 4) % 3);

            if (nKind == 0 && nAt < nLength) sDoc[nAt] = cWith;
            else if (nKind == 1 && nAt < nLength)
            {
                memmove(sDoc + nAt, sDoc + nAt + 1, nLength - nAt - 1);
                nLength--;
            }
            else if (nLength + 1 < sizeof(sDoc))
            {
                memmove(sDoc + nAt + 1, sDoc + nAt, nLength - nAt);
                sDoc[nAt] = cWith;
                nLength++;
            }
        }

        int nScanned = 0;
        if (!scan_agrees(sDoc, nLength, &nScanned))
        {
            fprintf(stderr, "json_regression: scan and parse disagree on: %.*s\n", (int)nLength, sDoc);
            return 1;
        }

        nTaken += nScanned;
    }

    CHECK(nTaken > 1000, "Enough of the mutations stayed valid for the comparison to mean something");
    return 0;
}

/* Numbers are written without a format pass; the text has to be the one printf gives, digit for digit */
static int number_matches(xpool_t *pPool, uint64_t nValue)
{
    char sWant[32];
    xjson_obj_t *pObj = XJSON_NewU64(pPool, "n", nValue);
    snprintf(sWant, sizeof(sWant), "%" PRIu64, nValue);
    int bSame = pObj != NULL && pObj->nType == XJSON_TYPE_NUMBER && !strcmp((const char*)pObj->pData, sWant);
    XJSON_FreeObject(pObj);

    pObj = XJSON_NewU32(pPool, "n", (uint32_t)nValue);
    snprintf(sWant, sizeof(sWant), "%u", (uint32_t)nValue);
    bSame = bSame && pObj != NULL && pObj->nType == XJSON_TYPE_NUMBER && !strcmp((const char*)pObj->pData, sWant);
    XJSON_FreeObject(pObj);

    pObj = XJSON_NewU16(pPool, NULL, (uint16_t)nValue);
    snprintf(sWant, sizeof(sWant), "%u", (unsigned)(uint16_t)nValue);
    bSame = bSame && pObj != NULL && pObj->nType == XJSON_TYPE_NUMBER && !strcmp((const char*)pObj->pData, sWant);
    XJSON_FreeObject(pObj);

    int nInt = (int)(int32_t)(uint32_t)nValue;
    pObj = XJSON_NewInt(pPool, "i", nInt);
    snprintf(sWant, sizeof(sWant), "%d", nInt);
    bSame = bSame && pObj != NULL && pObj->nType == XJSON_TYPE_NUMBER && !strcmp((const char*)pObj->pData, sWant);
    XJSON_FreeObject(pObj);

    int nNegated = nInt == INT32_MIN ? INT32_MAX : -nInt;
    pObj = XJSON_NewInt(pPool, "i", nNegated);
    snprintf(sWant, sizeof(sWant), "%d", nNegated);
    bSame = bSame && pObj != NULL && !strcmp((const char*)pObj->pData, sWant);
    XJSON_FreeObject(pObj);

    if (!bSame) fprintf(stderr, "json_regression: number text differs for %" PRIu64 "\n", nValue);
    return bSame;
}

static int XTest_number_text(void)
{
    xpool_t *pPool = XPool_Create(4096);
    CHECK(pPool != NULL, "Create a pool");

    for (uint64_t v = 0; v <= 20000; v++) CHECK(number_matches((v & 1) ? pPool : NULL, v), "Every small value");

    /* Each digit count from both sides, and the ends of every width */
    for (uint64_t p = 1, i = 0; i < 20; i++, p *= 10)
    {
        for (uint64_t d = 0; d < 4; d++)
        {
            CHECK(number_matches(NULL, p + d) && number_matches(NULL, p - 1 - d) && number_matches(NULL, UINT64_MAX - p + d),
                "The values around a power of ten");
        }
    }

    for (int b = 0; b < 64; b++) CHECK(number_matches(NULL, (1ULL << b) - 1) && number_matches(NULL, 1ULL << b), "Powers of two");
    CHECK(number_matches(NULL, UINT64_MAX) && number_matches(NULL, UINT32_MAX) && number_matches(NULL, (uint64_t)INT32_MAX + 1),
        "The type limits");

    uint64_t nState = 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < 50000; i++)
    {
        nState ^= nState << 13;
        nState ^= nState >> 7;
        nState ^= nState << 17;
        CHECK(number_matches(NULL, nState >> (nState % 64)), "Random values of every magnitude");
    }

    xjson_obj_t *pInt = XJSON_NewInt(NULL, NULL, INT32_MIN);
    CHECK(pInt != NULL && !strcmp((const char*)pInt->pData, "-2147483648") && XJSON_GetInt(pInt) == INT32_MIN, "INT_MIN");
    XJSON_FreeObject(pInt);

    for (int i = 0; i < 2; i++)
    {
        xjson_obj_t *pTrue = XJSON_NewBool(i ? pPool : NULL, "t", 2);
        xjson_obj_t *pFalse = XJSON_NewBool(i ? pPool : NULL, "f", 0);
        xjson_obj_t *pNull = XJSON_NewNull(i ? pPool : NULL, "z");
        CHECK(pTrue != NULL && pTrue->nType == XJSON_TYPE_BOOLEAN && !strcmp((const char*)pTrue->pData, "true"), "true");
        CHECK(pFalse != NULL && pFalse->nType == XJSON_TYPE_BOOLEAN && !strcmp((const char*)pFalse->pData, "false"), "false");
        CHECK(pNull != NULL && pNull->nType == XJSON_TYPE_NULL && !strcmp((const char*)pNull->pData, "null"), "null");
        XJSON_FreeObject(pTrue);
        XJSON_FreeObject(pFalse);
        XJSON_FreeObject(pNull);
    }

    XPool_Destroy(pPool);
    return 0;
}

/* The escaping the writer does, spelled out on its own: valid escapes are kept, the rest is escaped */
static size_t reference_escape(char *pOut, const char *pIn)
{
    size_t nOut = 0, nLength = strlen(pIn);
    for (size_t i = 0; i < nLength;)
    {
        unsigned char c = (unsigned char)pIn[i];
        size_t nKeep = 0;
        if (c == '\\' && i + 1 < nLength && strchr("\"\\/bfnrt", pIn[i + 1]) != NULL) nKeep = 2;
        else if (c == '\\' && i + 5 < nLength && pIn[i + 1] == 'u')
        {
            nKeep = 6;
            for (size_t j = i + 2; j < i + 6; j++)
                if (!isxdigit((unsigned char)pIn[j])) nKeep = 0;
        }

        if (nKeep)
        {
            memcpy(pOut + nOut, pIn + i, nKeep);
            nOut += nKeep;
            i += nKeep;
            continue;
        }

        i++;
        if (c == '"' || c == '\\') nOut += (size_t)sprintf(pOut + nOut, "\\%c", c);
        else if (c == '\b') nOut += (size_t)sprintf(pOut + nOut, "\\b");
        else if (c == '\f') nOut += (size_t)sprintf(pOut + nOut, "\\f");
        else if (c == '\n') nOut += (size_t)sprintf(pOut + nOut, "\\n");
        else if (c == '\r') nOut += (size_t)sprintf(pOut + nOut, "\\r");
        else if (c == '\t') nOut += (size_t)sprintf(pOut + nOut, "\\t");
        else if (c < 0x20) nOut += (size_t)sprintf(pOut + nOut, "\\u%04x", c);
        else pOut[nOut++] = (char)c;
    }

    pOut[nOut] = '\0';
    return nOut;
}

/* A name and a value written compact, indented and pretty without colours, against the reference escaping */
static int escape_matches(const char *pText)
{
    char sName[1024], sValue[1024], sWant[2200];
    reference_escape(sName, pText);
    reference_escape(sValue, pText);

    xjson_obj_t *pRoot = XJSON_NewObject(NULL, NULL, XFALSE);
    if (pRoot == NULL || XJSON_AddString(pRoot, pText, pText) != XJSON_ERR_NONE)
    {
        XJSON_FreeObject(pRoot);
        return 0;
    }

    xjson_format_t plain;
    memset(&plain, 0, sizeof(plain));
    plain.pNameFmt = plain.pNameClr = plain.pStrFmt = plain.pStrClr = "";
    plain.pNumFmt = plain.pNumClr = plain.pFloatFmt = plain.pFloatClr = "";
    plain.pBoolFmt = plain.pBoolClr = plain.pNullFmt = plain.pNullClr = "";

    size_t nLength = 0;
    char *pCompact = XJSON_DumpObj(pRoot, 0, &nLength);
    snprintf(sWant, sizeof(sWant), "{\"%s\":\"%s\"}", sName, sValue);
    int bSame = pCompact != NULL && nLength == strlen(sWant) && !strcmp(pCompact, sWant);

    char *pTabbed = XJSON_DumpObj(pRoot, 2, &nLength);
    snprintf(sWant, sizeof(sWant), "{\n  \"%s\": \"%s\"\n}", sName, sValue);
    bSame = bSame && pTabbed != NULL && nLength == strlen(sWant) && !strcmp(pTabbed, sWant);

    char *pPretty = XJSON_FormatObj(pRoot, 0, &plain, &nLength);
    snprintf(sWant, sizeof(sWant), "{\"%s" XSTR_FMT_RESET "\":\"%s" XSTR_FMT_RESET "\"}", sName, sValue);
    bSame = bSame && pPretty != NULL && nLength == strlen(sWant) && !strcmp(pPretty, sWant);

    if (!bSame) fprintf(stderr, "json_regression: escaping differs for \"%s\": %s\n", pText, pCompact ? pCompact : "(null)");
    free(pCompact);
    free(pTabbed);
    free(pPretty);
    XJSON_FreeObject(pRoot);
    return bSame;
}

static int XTest_escape_reference(void)
{
    /* Every byte at every position of a run that would otherwise be copied as it is */
    for (int nByte = 1; nByte <= UINT8_MAX; nByte++)
    {
        for (size_t nAt = 0; nAt < 12; nAt++)
        {
            char sText[16];
            memcpy(sText, "abcdefghijkl", 12);
            sText[nAt] = (char)nByte;
            sText[12] = '\0';
            CHECK(escape_matches(sText), "One byte anywhere in a plain run");
            sText[nAt + 1] = '\0';
            CHECK(escape_matches(sText), "One byte at the end");
        }
    }

    static const char *pEdges[] = { "", "a", "\\", "\\\\", "\"", "\\u", "\\u00e", "\\u00e9", "\\u00G9", "x\\u00e9y\\", "\\/",
        "\\x", "plain then \\n escape", "plain then \" quote", "\x01\x02", "tail\x1f", "\x7f\x80\xff", "ქართული", "a\\\"b" };
    for (size_t i = 0; i < sizeof(pEdges) / sizeof(pEdges[0]); i++) CHECK(escape_matches(pEdges[i]), "Escaping edge");

    uint32_t nState = 0x2545F491;
    const char sAlphabet[] = "\\\"u0aF/bfnrt \x01\x1f\x7f\xc3\xa9xyz";
    for (int nRound = 0; nRound < 20000; nRound++)
    {
        char sText[64];
        nState = nState * 1664525u + 1013904223u;
        size_t nLength = (nState >> 8) % 40;
        for (size_t i = 0; i < nLength; i++)
        {
            nState = nState * 1664525u + 1013904223u;
            sText[i] = sAlphabet[(nState >> 16) % (sizeof(sAlphabet) - 1)];
        }
        sText[nLength] = '\0';
        CHECK(escape_matches(sText), "Random text over the characters escaping turns on");
    }

    return 0;
}

/* Brackets and newlines go straight into the output. Each fixed buffer short of the document fails, leaves a
   terminated prefix of it and never writes past its end; any longer one holds exactly the document. */
static int writes_at_every_size(xjson_obj_t *pRoot, size_t nTabSize, int nPretty, const char *pWant)
{
    size_t nWant = strlen(pWant);
    char *pBuffer = (char*)malloc(nWant + 16);
    if (pBuffer == NULL) return 0;

    for (size_t nSize = 1; nSize <= nWant + 4; nSize++)
    {
        memset(pBuffer, 0x5a, nWant + 16);
        xjson_writer_t writer;
        if (!XJSON_InitWriter(&writer, NULL, pBuffer, nSize)) break;
        writer.nTabSize = nTabSize;
        writer.nPretty = (uint8_t)nPretty;
        XJSON_FormatInit(&writer.format);

        int nStatus = XJSON_WriteObject(pRoot, &writer);
        size_t nUsed = strnlen(pBuffer, nSize);
        int bOk = nUsed < nSize && nUsed == writer.nLength && writer.nAvail == nSize - nUsed &&
                  !memcmp(pBuffer, pWant, nUsed) && (unsigned char)pBuffer[nSize] == 0x5a;

        if (nSize > nWant) bOk = bOk && nStatus == XJSON_SUCCESS && nUsed == nWant;
        else bOk = bOk && nStatus == XJSON_FAILURE;

        if (!bOk)
        {
            fprintf(stderr, "json_regression: a %zu byte buffer gave status %d and %zu bytes for: %s\n",
                nSize, nStatus, nUsed, pWant);
            free(pBuffer);
            return 0;
        }
    }

    free(pBuffer);
    return 1;
}

static int XTest_writer_tokens(void)
{
    xjson_obj_t *pRoot = XJSON_NewObject(NULL, NULL, XFALSE);
    CHECK(pRoot != NULL, "Create the root");

    xjson_obj_t *pEmptyObj = XJSON_NewObject(NULL, "eo", XFALSE);
    xjson_obj_t *pEmptyArr = XJSON_NewArray(NULL, "ea", XFALSE);
    xjson_obj_t *pArray = XJSON_NewArray(NULL, "a", XFALSE);
    xjson_obj_t *pInner = XJSON_NewObject(NULL, NULL, XFALSE);
    CHECK(pEmptyObj != NULL && pEmptyArr != NULL && pArray != NULL && pInner != NULL, "Create the members");

    CHECK(XJSON_AddU32(pInner, "k", 1) == XJSON_ERR_NONE && XJSON_AddObject(pArray, pInner) == XJSON_ERR_NONE &&
          XJSON_AddObject(pArray, XJSON_NewArray(NULL, NULL, XFALSE)) == XJSON_ERR_NONE &&
          XJSON_AddObject(pArray, XJSON_NewBool(NULL, NULL, 1)) == XJSON_ERR_NONE &&
          XJSON_AddObject(pRoot, pEmptyObj) == XJSON_ERR_NONE && XJSON_AddObject(pRoot, pEmptyArr) == XJSON_ERR_NONE &&
          XJSON_AddObject(pRoot, pArray) == XJSON_ERR_NONE, "Assemble the document");

    /* Member order is the map's, so the expected text is taken from a dump and checked by shape */
    size_t nLength = 0;
    char *pCompact = XJSON_DumpObj(pRoot, 0, &nLength);
    CHECK(pCompact != NULL && nLength == strlen(pCompact) && parse_bytes_ok(pCompact, nLength), "Dump compact");
    CHECK(strstr(pCompact, "\"eo\":{}") && strstr(pCompact, "\"ea\":[]") && strstr(pCompact, "\"a\":[{\"k\":1},[],true]"),
        "Every bracket is where it belongs");
    CHECK(writes_at_every_size(pRoot, 0, 0, pCompact), "Compact output at every buffer size");

    char *pTabbed = XJSON_DumpObj(pRoot, 4, &nLength);
    const char *pIndented = "    \"a\": [\n        {\n            \"k\": 1\n        },\n        [],\n        true\n    ]";
    CHECK(pTabbed != NULL && strstr(pTabbed, pIndented), "Indented output puts each bracket on its own line");
    CHECK(writes_at_every_size(pRoot, 4, 0, pTabbed), "Indented output at every buffer size");

    xjson_format_t format;
    XJSON_FormatInit(&format);
    char *pPretty = XJSON_FormatObj(pRoot, 2, &format, &nLength);
    CHECK(pPretty != NULL && nLength == strlen(pPretty), "Pretty output");
    CHECK(writes_at_every_size(pRoot, 2, 1, pPretty), "Pretty output at every buffer size");

    /* A parsed document has the linter flags set, which is what indents a bracket of its own */
    xjson_t json;
    CHECK(XJSON_Parse(&json, NULL, pCompact, strlen(pCompact)) == XJSON_SUCCESS, "Parse the dump back");
    char *pParsed = XJSON_Dump(&json, 3, &nLength);
    CHECK(pParsed != NULL && writes_at_every_size(json.pRootObj, 3, 0, pParsed), "A parsed tree at every buffer size");

    free(pParsed);
    XJSON_Destroy(&json);
    free(pCompact);
    free(pTabbed);
    free(pPretty);
    XJSON_FreeObject(pRoot);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(parse_matrix),
    XTEST_CASE(parse_boundaries),
    XTEST_CASE(builders_and_writers),
    XTEST_CASE(generated_string_edges),
    XTEST_CASE(byte_boundaries),
    XTEST_CASE(error_states),
    XTEST_CASE(pool_and_stress),
    XTEST_CASE(root_array_and_api_boundaries),
    XTEST_CASE(empty_objects),
    XTEST_CASE(pair_names),
    XTEST_CASE(scan_flat),
    XTEST_CASE(number_text),
    XTEST_CASE(escape_reference),
    XTEST_CASE(writer_tokens)
)
