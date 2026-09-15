/* libxutils: deterministic allocation failures; GNU/Clang ELF linker wrapping only. */
#include "test.h"
#include "json.h"
#include "http.h"
#include "ws.h"
#include "map.h"
#include "api.h"
#include <fcntl.h>

void *__real_malloc(size_t nSize);
void *__real_calloc(size_t nCount, size_t nSize);
void *__real_realloc(void *pData, size_t nSize);
static size_t g_nFailAt;
static size_t g_nCalls;

static int XTest_Fail(void)
{
    return g_nFailAt && ++g_nCalls == g_nFailAt;
}

void *__wrap_malloc(size_t nSize)
{
    return XTest_Fail() ? NULL : __real_malloc(nSize);
}

void *__wrap_calloc(size_t nCount, size_t nSize)
{
    return XTest_Fail() ? NULL : __real_calloc(nCount, nSize);
}

void *__wrap_realloc(void *pData, size_t nSize)
{
    return XTest_Fail() ? NULL : __real_realloc(pData, nSize);
}

static int XTest_json_cleanup(void)
{
    const char data[] = "{\"one\":[1,2,3],\"two\":{\"text\":\"value\",\"flag\":true}}";
    size_t nFailures = 0;
    for (size_t i = 1; i <= 96; i++)
    {
        xjson_t json;
        g_nCalls = 0;
        g_nFailAt = i;
        int nStatus = XJSON_Parse(&json, NULL, data, sizeof(data) - 1);
        g_nFailAt = 0;
        if (!nStatus)
        {
            nFailures++;
            CHECK(
                json.pRootObj == NULL && json.nError != XJSON_ERR_NONE, "Allocation failure must discard the partial JSON tree");
        }
        else
            CHECK(XJSON_GetArrayLength(XJSON_GetObject(json.pRootObj, "one")) == 3, "Successful parse retains complete data");
        XJSON_Destroy(&json);
    }
    CHECK(nFailures > 10, "Exercise allocation failures at distinct tree construction stages");
    return 0;
}

static int XTest_http_header(void)
{
    char value[4097];
    memset(value, 'x', sizeof(value) - 1);
    value[sizeof(value) - 1] = 0;
    size_t nFailures = 0;
    for (size_t i = 1; i <= 12; i++)
    {
        xhttp_t http;
        CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/", "1.1") > 0, "Initialize failure fixture");
        CHECK(XHTTP_AddHeader(&http, "X-Existing", "retained") > 0, "Populate unrelated header");
        g_nCalls = 0;
        g_nFailAt = i;
        int nStatus = XHTTP_AddHeader(&http, "Authorization", "Bearer %s", value);
        g_nFailAt = 0;
        if (nStatus < 0) nFailures++;
        const char *pHeader = XHTTP_GetHeader(&http, "Authorization");
        CHECK(nStatus < 0 ? pHeader == NULL : pHeader && strlen(pHeader) == 4103,
            "An allocation failure never emits a truncated credential");
        CHECK(strcmp(XHTTP_GetHeader(&http, "X-Existing"), "retained") == 0, "Failure preserves unrelated headers");
        XHTTP_Clear(&http);
    }
    CHECK(nFailures >= 3, "Fail format, key and value allocations");
    return 0;
}

static int XTest_buffer_preservation(void)
{
    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, 0, XFALSE);
    CHECK(XByteBuffer_Add(&buffer, (const uint8_t*)"abc", 3) == 3, "Initialize owned buffer");
    uint8_t *pOriginal = buffer.pData;
    g_nCalls = 0;
    g_nFailAt = 1;
    int nStatus = XByteBuffer_Add(&buffer, (const uint8_t*)"more-data", 9);
    g_nFailAt = 0;
    CHECK(nStatus < 0 && buffer.pData == pOriginal && buffer.nUsed == 3, "Failed reallocation retains original storage and size");
    CHECK(memcmp(buffer.pData, "abc", 3) == 0, "Failed reallocation retains original content");
    XByteBuffer_Clear(&buffer);
    return 0;
}

static int XTest_endpoint_ownership(void)
{
    xapi_t api;
    CHECK(XAPI_Init(&api, NULL, NULL) == XSTDOK, "Initialize API without callbacks");
    XSOCKET pair[2];
    CHECK(XSock_CreatePair(pair) == XSTDOK, "Create owned endpoint descriptor");
    xapi_endpoint_t endpoint;
    XAPI_InitEndpoint(&endpoint);
    endpoint.eRole = XAPI_PEER;
    endpoint.eType = XAPI_SOCK;
    endpoint.nFD = pair[0];
    g_nCalls = 0;
    g_nFailAt = 1;
    int nStatus = XAPI_AddEvent(&api, &endpoint);
    g_nFailAt = 0;
    CHECK(nStatus < 0 && XAPI_GetEventCount(&api) == 0, "Fail session allocation before registration");
    CHECK(fcntl(pair[0], F_GETFD) < 0 && errno == EBADF, "Ownership must not leak on the first allocation failure");
    close(pair[1]);
    XAPI_Destroy(&api);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(json_cleanup),
    XTEST_CASE(http_header),
    XTEST_CASE(buffer_preservation),
    XTEST_CASE(endpoint_ownership)
)
