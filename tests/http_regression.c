/* libxutils: HTTP fragmentation, message boundaries, credential capacity and reuse. */
#include "test.h"
#include "http.h"
#include "base64.h"

static int XTest_partial(void)
{
    const char wire[] = "POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\na\0b\r\n";
    const size_t nSize = sizeof(wire) - 1;
    for (size_t nSplit = 0; nSplit < nSize; nSplit++)
    {
        xhttp_t http;
        CHECK(XHTTP_InitParser(&http, (uint8_t*)wire, nSplit) > 0, "Initialize a bounded request prefix");
        xhttp_status_t eStatus = XHTTP_Parse(&http);
        CHECK(eStatus == XHTTP_INCOMPLETE || eStatus == XHTTP_PARSED, "Proper request prefixes must not complete");
        CHECK(XHTTP_AppendData(&http, (uint8_t*)wire + nSplit, nSize - nSplit) > 0, "Append the rest of the request");
        CHECK(XHTTP_Parse(&http) == XHTTP_COMPLETE, "Complete request must parse at every split boundary");
        CHECK(http.eMethod == XHTTP_POST && strcmp(http.sUri, "/upload") == 0, "Fragmentation preserves the start line");
        CHECK(
            XHTTP_GetBodySize(&http) == 5 && memcmp(XHTTP_GetBody(&http), "a\0b\r\n", 5) == 0, "Body bytes must be binary-safe");
        XHTTP_Clear(&http);
    }
    return 0;
}

static int XTest_pipeline(void)
{
    const char wire[] = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\noneHTTP/1.1 204 No Content\r\n\r\n";
    xhttp_t first, second;
    CHECK(XHTTP_ParseData(&first, (uint8_t*)wire, sizeof(wire) - 1) == XHTTP_COMPLETE, "Parse first pipelined response");
    CHECK(first.nContentLength == 3 && memcmp(XHTTP_GetBody(&first), "one", 3) == 0, "Keep the first response body bounded");
    size_t nExtra = XHTTP_GetExtraSize(&first);
    CHECK(nExtra > 0 && XHTTP_GetPacketSize(&first) + nExtra == sizeof(wire) - 1, "Preserve the next response as extra data");
    CHECK(XHTTP_ParseData(&second, (uint8_t*)XHTTP_GetExtraData(&first), nExtra) == XHTTP_COMPLETE, "Parse the next response");
    CHECK(second.nStatusCode == 204 && XHTTP_GetExtraSize(&second) == 0, "Pipelining must preserve status and boundaries");
    XHTTP_Clear(&second);
    XHTTP_Clear(&first);
    return 0;
}

static int XTest_headers(void)
{
    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/", "1.1") > 0, "Initialize request builder");
    CHECK(XHTTP_AddHeader(&http, "X-Test", "one") > 0, "Insert a named header");
    CHECK(strcmp(XHTTP_GetHeader(&http, "x-TEST"), "one") == 0, "Header lookups are case-insensitive");
    http.nAllowUpdate = XTRUE;
    CHECK(XHTTP_AddHeader(&http, "x-test", "two") > 0, "Update header under alternate casing");
    CHECK(
        http.headerMap.nCount == 1 && strcmp(XHTTP_GetHeader(&http, "X-Test"), "two") == 0, "Update must not duplicate a header");
    char value[4097];
    memset(value, 'x', sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    CHECK(XHTTP_AddHeader(&http, "Authorization", "Bearer %s", value) > 0, "Add credentials larger than the stack format buffer");
    CHECK(strlen(XHTTP_GetHeader(&http, "authorization")) == 7 + strlen(value), "Credentials must never be silently truncated");
    CHECK(XHTTP_AddHeader(&http, "X-Bad\r\nInjected", "x") < 0, "Header names must reject line injection");
    CHECK(XHTTP_AddHeader(&http, "X-Bad", "x\r\nInjected: yes") < 0, "Header values must reject line injection");
    xbyte_buffer_t *pWire = XHTTP_Assemble(&http, NULL, 0);
    CHECK(pWire && strstr((char*)pWire->pData, "\r\nAuthorization: Bearer "), "Assembly preserves the supplied header spelling");
    CHECK(strstr((char*)pWire->pData, "\r\nX-Test: two\r\n"), "Case-insensitive update preserves the first header spelling");
    XHTTP_Clear(&http);
    return 0;
}

static int XTest_basic_auth(void)
{
    char token[64];
    CHECK(XHTTP_GetAuthToken(token, sizeof(token), "Aladdin", "open sesame") == 28,
        "Basic auth must preserve the complete base64 value");
    CHECK(strcmp(token, "QWxhZGRpbjpvcGVuIHNlc2FtZQ==") == 0, "Basic auth must match the published example");
    struct
    {
        char sToken[4];
        char sGuard[8];
    } bounded;
    memset(&bounded, 0x5a, sizeof(bounded));
    CHECK(XHTTP_GetAuthToken(bounded.sToken, sizeof(bounded.sToken), "Aladdin", "open sesame") == 0 && !bounded.sToken[0],
        "Insufficient auth capacity fails without emitting truncated credentials");
    CHECK(memcmp(bounded.sGuard, "ZZZZZZZZ", 8) == 0, "The auth helper must honor caller capacity");
    return 0;
}

static int XTest_long_basic_auth(void)
{
    char password[2049], token[4096];
    memset(password, 'x', sizeof(password) - 1);
    password[sizeof(password) - 1] = 0;
    size_t nLength = XHTTP_GetAuthToken(token, sizeof(token), "user", password);
    CHECK(nLength > sizeof(password), "Encode credentials beyond the old fixed stack limit");
    char *pDecoded = XBase64_Decrypt((const uint8_t*)token, &nLength);
    CHECK(pDecoded && nLength == 2053 && memcmp(pDecoded, "user:", 5) == 0 && strcmp(pDecoded + 5, password) == 0,
        "Long credentials retain every byte");
    free(pDecoded);
    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/", "1.1") > 0, "Initialize auth request");
    CHECK(XHTTP_SetAuthBasic(&http, "user", password) > 0, "Build long Basic auth header");
    const char *pHeader = XHTTP_GetHeader(&http, "Authorization");
    CHECK(pHeader && strncmp(pHeader, "Basic ", 6) == 0 && strcmp(pHeader + 6, token) == 0,
        "Header builder uses complete credentials");
    XHTTP_Clear(&http);
    return 0;
}

static int XTest_reuse(void)
{
    for (int nHard = 0; nHard < 2; nHard++)
    {
        xhttp_t http;
        CHECK(XHTTP_InitRequest(&http, XHTTP_POST, "/first", "1.1") > 0, "Create reusable request");
        CHECK(XHTTP_AddHeader(&http, "X-Old", "old") > 0 && XHTTP_Assemble(&http, (uint8_t*)"abc", 3) != NULL,
            "Assemble the first message");
        XHTTP_Reset(&http, nHard);
        CHECK(XHTTP_GetHeader(&http, "X-Old") == NULL && http.nContentLength == 0 && http.rawData.nUsed == 0,
            "Reset clears headers and body metadata for both allocation modes");
        http.eType = XHTTP_RESPONSE;
        http.nStatusCode = 200;
        CHECK(XHTTP_AddHeader(&http, "X-New", "new") > 0 && XHTTP_Assemble(&http, (uint8_t*)"z", 1) != NULL,
            "Reuse must assemble a fresh message");
        CHECK(XHTTP_GetHeader(&http, "X-Old") == NULL && http.nContentLength == 1,
            "Old state must not contaminate reused messages");
        XHTTP_Clear(&http);
    }
    return 0;
}

static int XTest_empty_body(void)
{
    const char wire[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Type: application/json\r\n\r\n";
    xhttp_t http;
    CHECK(XHTTP_ParseData(&http, (uint8_t*)wire, sizeof(wire) - 1) == XHTTP_COMPLETE,
        "Explicit zero content length completes regardless of content type");
    CHECK(http.nContentLength == 0 && XHTTP_GetBodySize(&http) == 0, "Empty body must not invent data");
    XHTTP_Clear(&http);
    return 0;
}

XTEST_MAIN(XTEST_CASE(partial), XTEST_CASE(pipeline), XTEST_CASE(headers), XTEST_CASE(basic_auth), XTEST_CASE(long_basic_auth),
    XTEST_CASE(reuse), XTEST_CASE(empty_body))
