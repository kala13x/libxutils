/* libxutils: HTTP fragmentation, message boundaries, credential capacity and reuse. */
#include "test.h"
#include "http.h"
#include "base64.h"
#include "sock.h"

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


static int XTest_code_strings(void)
{
    /* Every status code the library names has to render, and an unknown
     * one has to fall through rather than reading past its table. */
    const struct { int nCode; const char *pText; } known[] = {
        {100, "Continue"}, {200, "OK"}, {201, "Created"}, {204, "No Content"},
        {301, "Moved Permanently"}, {302, "Found"}, {400, "Bad Request"},
        {401, "Unauthorized"}, {403, "Forbidden"}, {404, "Not Found"},
        {405, "Method Not Allowed"}, {408, "Request Timeout"},
        {500, "Internal Server Error"}, {502, "Bad Gateway"}, {503, "Service Unavailable"}
    };

    for (size_t i = 0; i < sizeof(known) / sizeof(*known); i++)
        CHECK(strcmp(XHTTP_GetCodeStr(known[i].nCode), known[i].pText) == 0,
            "Every known status code renders its own reason phrase");

    CHECK(strncmp(XHTTP_GetCodeStr(999), "Unknown", 7) == 0, "An unknown code falls through");
    CHECK(strncmp(XHTTP_GetCodeStr(0), "Unknown", 7) == 0, "A zero code falls through");
    CHECK(strncmp(XHTTP_GetCodeStr(-1), "Unknown", 7) == 0, "A negative code falls through");

    /* Methods round trip between their name and their enum. */
    const struct { xhttp_method_t eMethod; const char *pName; } methods[] = {
        {XHTTP_PUT, "PUT"}, {XHTTP_GET, "GET"}, {XHTTP_POST, "POST"},
        {XHTTP_DELETE, "DELETE"}, {XHTTP_OPTIONS, "OPTIONS"}
    };

    for (size_t i = 0; i < sizeof(methods) / sizeof(*methods); i++)
    {
        CHECK(strcmp(XHTTP_GetMethodStr(methods[i].eMethod), methods[i].pName) == 0,
            "Every method renders its own name");
        CHECK(XHTTP_GetMethodType(methods[i].pName) == methods[i].eMethod,
            "Every method name parses back to its own value");
    }

    CHECK(strcmp(XHTTP_GetMethodStr(XHTTP_DUMMY), "DUMMY") == 0, "The placeholder method is named");
    CHECK(strcmp(XHTTP_GetMethodStr((xhttp_method_t)99), "UNKNOWN") == 0, "An out of range method is named");
    CHECK(XHTTP_GetMethodType("HEAD") == XHTTP_DUMMY, "An unsupported method is not invented");
    CHECK(XHTTP_GetMethodType("nonsense") == XHTTP_DUMMY, "An unknown method name is rejected");
    CHECK(XHTTP_GetMethodType("") == XHTTP_DUMMY, "An empty method name is rejected");

    /* Every status value has a description. */
    const xhttp_status_t states[] = {
        XHTTP_NONE, XHTTP_INVALID, XHTTP_EINIT, XHTTP_ELINK, XHTTP_EAUTH, XHTTP_EREAD,
        XHTTP_EWRITE, XHTTP_EPROTO, XHTTP_ETIMEO, XHTTP_EALLOC, XHTTP_ESETHDR,
        XHTTP_EFDMODE, XHTTP_EEXISTS, XHTTP_ECONNECT, XHTTP_ERESOLVE, XHTTP_EASSEMBLE,
        XHTTP_TERMINATED, XHTTP_INCOMPLETE, XHTTP_CONNECTED, XHTTP_RESOLVED,
        XHTTP_COMPLETE, XHTTP_BIGCNT, XHTTP_BIGHDR, XHTTP_PARSED
    };

    for (size_t i = 0; i < sizeof(states) / sizeof(*states); i++)
    {
        const char *pText = XHTTP_GetStatusStr(states[i]);
        CHECK(pText != NULL && *pText != '\0', "Every status has a description");
    }
    return 0;
}

static int XTest_content_length(void)
{
    /* A content length is a plain run of digits. Anything else is not a
     * length, and must not become one: a negative value would wrap into a
     * size the peer can never satisfy, and a trailing suffix is how a
     * proxy and an origin end up disagreeing on the message boundary. */
    const char *pRejected[] = {
        "GET / HTTP/1.1\r\nContent-Length: -5\r\n\r\n",
        "GET / HTTP/1.1\r\nContent-Length: 5x\r\n\r\nabcde",
        "GET / HTTP/1.1\r\nContent-Length: abc\r\n\r\n",
        "GET / HTTP/1.1\r\nContent-Length: +5\r\n\r\nabcde",
        "GET / HTTP/1.1\r\nContent-Length: 0x10\r\n\r\n",
        "GET / HTTP/1.1\r\nContent-Length: \r\n\r\n"
    };

    for (size_t i = 0; i < sizeof(pRejected) / sizeof(*pRejected); i++)
    {
        xhttp_t http;
        xhttp_status_t eStatus = XHTTP_ParseData(&http, (uint8_t*)pRejected[i], strlen(pRejected[i]));
        CHECK(eStatus == XHTTP_COMPLETE, "A malformed length still completes the message");
        CHECK(http.nContentLength == 0, "A malformed length announces no body at all");
        XHTTP_Clear(&http);
    }

    /* A well formed length is taken at face value, whatever the spacing. */
    const char *pAccepted[] = {
        "GET / HTTP/1.1\r\nContent-Length: 5\r\n\r\nabcde",
        "GET / HTTP/1.1\r\nContent-Length:5\r\n\r\nabcde",
        "GET / HTTP/1.1\r\nContent-Length:   5\r\n\r\nabcde"
    };

    for (size_t i = 0; i < sizeof(pAccepted) / sizeof(*pAccepted); i++)
    {
        xhttp_t http;
        CHECK(XHTTP_ParseData(&http, (uint8_t*)pAccepted[i], strlen(pAccepted[i])) == XHTTP_COMPLETE,
            "A well formed length parses");
        CHECK(http.nContentLength == 5, "The announced length is taken whatever the spacing");
        CHECK(XHTTP_GetBodySize(&http) == 5, "The body is the announced length");
        CHECK(memcmp(XHTTP_GetBody(&http), "abcde", 5) == 0, "The body is the announced bytes");
        XHTTP_Clear(&http);
    }

    /* A zero length announces an empty body, not a missing one. */
    const char empty[] = "POST / HTTP/1.1\r\nContent-Length: 0\r\n\r\n";
    xhttp_t http;
    CHECK(XHTTP_ParseData(&http, (uint8_t*)empty, sizeof(empty) - 1) == XHTTP_COMPLETE,
        "A zero length request completes");
    CHECK(http.nContentLength == 0 && XHTTP_GetBodySize(&http) == 0, "A zero length body is empty");
    XHTTP_Clear(&http);
    return 0;
}

static int XTest_start_line(void)
{
    /* A request start line yields the method, the target and the version. */
    const char request[] = "DELETE /a/b/c?x=1&y=2 HTTP/1.1\r\nHost: h\r\n\r\n";
    xhttp_t http;
    CHECK(XHTTP_ParseData(&http, (uint8_t*)request, sizeof(request) - 1) == XHTTP_COMPLETE,
        "A request start line parses");
    CHECK(http.eType == XHTTP_REQUEST, "The message is classified as a request");
    CHECK(http.eMethod == XHTTP_DELETE, "The method is taken from the start line");
    CHECK(strcmp(http.sUri, "/a/b/c?x=1&y=2") == 0, "The whole target including the query is kept");
    CHECK(strcmp(http.sVersion, "1.1") == 0, "The version is taken from the start line");
    CHECK(http.nStatusCode == 0, "A request carries no status code");
    XHTTP_Clear(&http);

    /* A response start line yields the code and the version. */
    const char response[] = "HTTP/1.0 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n";
    CHECK(XHTTP_ParseData(&http, (uint8_t*)response, sizeof(response) - 1) == XHTTP_COMPLETE,
        "A response start line parses");
    CHECK(http.eType == XHTTP_RESPONSE, "The message is classified as a response");
    CHECK(http.nStatusCode == 503, "The status code is taken from the start line");
    CHECK(strcmp(http.sVersion, "1.0") == 0, "The response version is taken from the start line");
    CHECK(XHTTP_IsSuccessCode(&http) == XFALSE, "A server error is not a success code");
    XHTTP_Clear(&http);

    /* Success is the 2xx range and nothing else. */
    const struct { int nCode; xbool_t bSuccess; } codes[] = {
        {200, XTRUE}, {201, XTRUE}, {204, XTRUE}, {299, XTRUE},
        {100, XFALSE}, {199, XFALSE}, {300, XFALSE}, {404, XFALSE}, {500, XFALSE}
    };

    for (size_t i = 0; i < sizeof(codes) / sizeof(*codes); i++)
    {
        char sWire[128];
        snprintf(sWire, sizeof(sWire), "HTTP/1.1 %d X\r\nContent-Length: 0\r\n\r\n", codes[i].nCode);
        CHECK(XHTTP_ParseData(&http, (uint8_t*)sWire, strlen(sWire)) == XHTTP_COMPLETE, "Every response parses");
        CHECK(XHTTP_IsSuccessCode(&http) == codes[i].bSuccess, "Only the 2xx range counts as success");
        XHTTP_Clear(&http);
    }

    /* Anything that is not a start line is rejected outright. A response
     * line with no status code is the important one: the version cut would
     * otherwise run on into the headers, and XHTTP_Assemble() writes the
     * version back out verbatim, so a version holding a CRLF would split
     * the assembled message in two. */
    const char *pInvalid[] = {
        "not http at all\r\n\r\n",
        "\r\n\r\n",
        "GET\r\n\r\n",
        "HTTP/1.1\r\n\r\n",
        "HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1\r\nX: y\r\n\r\n",
        "HTTP/1.1 \r\n\r\n",
        "HTTP/1.1 99 Too Small\r\n\r\n",
        "HTTP/1.1 600 Too Big\r\n\r\n",
        "HTTP/1.1 abc Not A Code\r\n\r\n"
    };

    for (size_t i = 0; i < sizeof(pInvalid) / sizeof(*pInvalid); i++)
    {
        xhttp_status_t eStatus = XHTTP_ParseData(&http, (uint8_t*)pInvalid[i], strlen(pInvalid[i]));
        CHECK(eStatus != XHTTP_COMPLETE, "A malformed start line does not complete");
        CHECK(strchr(http.sVersion, '\r') == NULL && strchr(http.sVersion, '\n') == NULL,
            "A rejected start line never leaves a line break in the version");
        XHTTP_Clear(&http);
    }

    /* Every code in the valid range is accepted, and the version stays clean. */
    const int accepted[] = {100, 200, 301, 404, 500, 599};
    for (size_t i = 0; i < sizeof(accepted) / sizeof(*accepted); i++)
    {
        char sWire[96];
        snprintf(sWire, sizeof(sWire), "HTTP/1.1 %d R\r\nContent-Length: 0\r\n\r\n", accepted[i]);
        CHECK(XHTTP_ParseData(&http, (uint8_t*)sWire, strlen(sWire)) == XHTTP_COMPLETE,
            "Every code in range completes");
        CHECK(http.nStatusCode == (uint16_t)accepted[i], "The code is taken from the start line");
        CHECK(strcmp(http.sVersion, "1.1") == 0, "The version is just the version");
        XHTTP_Clear(&http);
    }
    return 0;
}

static int XTest_assemble(void)
{
    /* A request built by the library has to parse back as what was built. */
    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_POST, "/submit", "1.1") > 0, "A request initializes");
    CHECK(XHTTP_AddHeader(&http, "Host", "example.com") > 0, "A header is added");
    CHECK(XHTTP_AddHeader(&http, "X-Count", "%d", 42) > 0, "A formatted header is added");
    CHECK(XHTTP_AddHeader(&http, "Content-Type", "text/plain") > 0, "A content type is added");

    const uint8_t body[] = {'p', 'a', 'y', 0x00, 0xff, 'd'};
    xbyte_buffer_t *pWire = XHTTP_Assemble(&http, body, sizeof(body));
    CHECK(pWire != NULL && pWire->nUsed > 0, "The request assembles");

    xhttp_t parsed;
    CHECK(XHTTP_ParseData(&parsed, pWire->pData, pWire->nUsed) == XHTTP_COMPLETE,
        "The assembled request parses back");
    CHECK(parsed.eMethod == XHTTP_POST, "The method survives the round trip");
    CHECK(strcmp(parsed.sUri, "/submit") == 0, "The target survives the round trip");
    CHECK(strcmp(parsed.sVersion, "1.1") == 0, "The version survives the round trip");
    CHECK(strcmp(XHTTP_GetHeader(&parsed, "Host"), "example.com") == 0, "A header survives the round trip");
    CHECK(strcmp(XHTTP_GetHeader(&parsed, "X-Count"), "42") == 0, "A formatted header survives the round trip");
    CHECK(XHTTP_GetBodySize(&parsed) == sizeof(body), "The body length survives the round trip");
    CHECK(memcmp(XHTTP_GetBody(&parsed), body, sizeof(body)) == 0, "Binary body bytes survive the round trip");

    /* The assembler announces the length it was handed. */
    CHECK(strcmp(XHTTP_GetHeader(&parsed, "Content-Length"), "6") == 0, "The assembler announces the body length");
    XHTTP_Clear(&parsed);
    XHTTP_Clear(&http);

    /* A response assembles the same way. */
    CHECK(XHTTP_InitResponse(&http, 201, "1.1") > 0, "A response initializes");
    CHECK(XHTTP_AddHeader(&http, "Location", "/created/1") > 0, "A location header is added");

    pWire = XHTTP_Assemble(&http, NULL, 0);
    CHECK(pWire != NULL, "A body-less response assembles");

    CHECK(XHTTP_ParseData(&parsed, pWire->pData, pWire->nUsed) == XHTTP_COMPLETE,
        "The assembled response parses back");
    CHECK(parsed.eType == XHTTP_RESPONSE, "The round trip is still a response");
    CHECK(parsed.nStatusCode == 201, "The status code survives the round trip");
    CHECK(strcmp(XHTTP_GetHeader(&parsed, "Location"), "/created/1") == 0, "The location survives the round trip");
    CHECK(XHTTP_GetBodySize(&parsed) == 0, "A body-less response has no body");
    XHTTP_Clear(&parsed);
    XHTTP_Clear(&http);

    /* Every method assembles into a start line the parser recognises. */
    const xhttp_method_t methods[] = {XHTTP_GET, XHTTP_PUT, XHTTP_POST, XHTTP_DELETE, XHTTP_OPTIONS};
    for (size_t i = 0; i < sizeof(methods) / sizeof(*methods); i++)
    {
        CHECK(XHTTP_InitRequest(&http, methods[i], "/x", "1.1") > 0, "Every method initializes");
        pWire = XHTTP_Assemble(&http, NULL, 0);
        CHECK(pWire != NULL, "Every method assembles");
        CHECK(XHTTP_ParseData(&parsed, pWire->pData, pWire->nUsed) == XHTTP_COMPLETE, "Every method parses back");
        CHECK(parsed.eMethod == methods[i], "Every method survives the round trip");
        XHTTP_Clear(&parsed);
        XHTTP_Clear(&http);
    }
    return 0;
}

static int XTest_header_lookup(void)
{
    /* Header names are matched without regard to case, and a name that is
     * a prefix of another must not match it. */
    const char wire[] =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Type: application/json\r\n"
        "X-Long-Header-Name: value\r\n"
        "Empty-Header: \r\n"
        "Content-Length: 0\r\n\r\n";

    xhttp_t http;
    CHECK(XHTTP_ParseData(&http, (uint8_t*)wire, sizeof(wire) - 1) == XHTTP_COMPLETE, "The request parses");

    /* A header with an empty value carries nothing, so it is not stored and
     * not counted; the other four are. */
    CHECK(http.nHeaderCount == 4, "Every header with a value is counted");
    CHECK(XHTTP_GetHeader(&http, "Empty-Header") == NULL, "An empty valued header is not stored");

    CHECK(strcmp(XHTTP_GetHeader(&http, "Host"), "example.com") == 0, "A header is found by its exact name");
    CHECK(strcmp(XHTTP_GetHeader(&http, "host"), "example.com") == 0, "A header is found in lower case");
    CHECK(strcmp(XHTTP_GetHeader(&http, "HOST"), "example.com") == 0, "A header is found in upper case");
    CHECK(strcmp(XHTTP_GetHeader(&http, "HoSt"), "example.com") == 0, "A header is found in mixed case");

    CHECK(strcmp(XHTTP_GetHeader(&http, "X-Long-Header-Name"), "value") == 0, "A long header name is found");
    CHECK(strcmp(XHTTP_GetHeader(&http, "Content-Type"), "application/json") == 0, "A content type is found");

    /* A prefix of a real header name is not that header. */
    CHECK(XHTTP_GetHeader(&http, "Content") == NULL, "A prefix of a header name does not match it");
    CHECK(XHTTP_GetHeader(&http, "X-Long") == NULL, "A prefix of a long header name does not match it");
    CHECK(XHTTP_GetHeader(&http, "Absent") == NULL, "A header that is not there is not found");
    CHECK(XHTTP_GetHeader(&http, "") == NULL, "An empty header name finds nothing");

    /* The raw header block is available as written. */
    char *pRaw = XHTTP_GetHeaderRaw(&http);
    CHECK(pRaw != NULL, "The raw header block is available");
    CHECK(strstr(pRaw, "Host: example.com") != NULL, "The raw block holds the headers as written");
    free(pRaw);

    CHECK(XHTTP_GetHeader(&http, NULL) == NULL, "A missing header name finds nothing");
    XHTTP_Clear(&http);
    return 0;
}

/* Writes a whole message into one end of a socket pair and receives it
 * through the HTTP layer at the other, which is where the size limits and
 * the incremental readers live. */
static xhttp_status_t http_receive(xhttp_t *pHttp, const char *pWire, size_t nSize,
    size_t nHeaderMax, size_t nContentMax)
{
    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    if (XSock_CreatePair(pair) != XSTDOK) return XHTTP_EINIT;

    xsock_t writer, reader;
    XSock_Init(&writer, XSOCK_TCP_PEER, pair[0]);
    XSock_Init(&reader, XSOCK_TCP_PEER, pair[1]);
    XSock_TimeOutR(&reader, 2, 0);

    XSock_Write(&writer, pWire, nSize);
    XSock_Close(&writer);

    XHTTP_InitParser(pHttp, NULL, 0);
    pHttp->nHeaderMax = nHeaderMax;
    pHttp->nContentMax = nContentMax;

    xhttp_status_t eStatus = XHTTP_Receive(pHttp, &reader);
    XSock_Close(&reader);
    return eStatus;
}

static int XTest_limits(void)
{
    /* A body larger than the configured maximum is refused while it is
     * still arriving, rather than being buffered to completion first. */
    char sBody[4096];
    memset(sBody, 'b', sizeof(sBody));

    char sWire[8192];
    size_t nUsed = (size_t)snprintf(sWire, sizeof(sWire),
        "POST / HTTP/1.1\r\nContent-Length: %zu\r\n\r\n", sizeof(sBody));
    memcpy(&sWire[nUsed], sBody, sizeof(sBody));
    nUsed += sizeof(sBody);

    xhttp_t http;
    CHECK(http_receive(&http, sWire, nUsed, 0, 64) == XHTTP_BIGCNT,
        "A body past the content maximum is refused");
    XHTTP_Clear(&http);

    /* The same message inside the limit is received whole. */
    CHECK(http_receive(&http, sWire, nUsed, 0, 0) == XHTTP_COMPLETE,
        "The same message with no limit is received");
    CHECK(XHTTP_GetBodySize(&http) == sizeof(sBody), "The whole body arrived");
    CHECK(memcmp(XHTTP_GetBody(&http), sBody, sizeof(sBody)) == 0, "Every body byte arrived unchanged");
    CHECK(http.eMethod == XHTTP_POST, "The received message keeps its method");
    XHTTP_Clear(&http);

    /* A header block past the maximum is refused before the body. */
    char sBigHdr[8192];
    nUsed = (size_t)snprintf(sBigHdr, sizeof(sBigHdr), "GET / HTTP/1.1\r\n");
    while (nUsed < sizeof(sBigHdr) - 64)
        nUsed += (size_t)snprintf(&sBigHdr[nUsed], sizeof(sBigHdr) - nUsed, "X-Pad-%04zu: padding\r\n", nUsed);
    nUsed += (size_t)snprintf(&sBigHdr[nUsed], sizeof(sBigHdr) - nUsed, "\r\n");

    CHECK(http_receive(&http, sBigHdr, nUsed, 256, 0) == XHTTP_BIGHDR,
        "A header block past the maximum is refused");
    XHTTP_Clear(&http);

    /* Inside the limits the same headers are all accepted and counted. */
    CHECK(http_receive(&http, sBigHdr, nUsed, sizeof(sBigHdr) * 2, 0) == XHTTP_COMPLETE,
        "The same headers inside the limit are accepted");
    CHECK(http.nHeaderCount > 100, "Every padding header was counted");
    XHTTP_Clear(&http);

    /* A peer that closes without sending anything is not a message. */
    XSOCKET pair[2] = {XSOCK_INVALID, XSOCK_INVALID};
    CHECK(XSock_CreatePair(pair) == XSTDOK, "A socket pair is created");

    xsock_t writer, reader;
    XSock_Init(&writer, XSOCK_TCP_PEER, pair[0]);
    XSock_Init(&reader, XSOCK_TCP_PEER, pair[1]);
    XSock_TimeOutR(&reader, 2, 0);
    XSock_Close(&writer);

    CHECK(XHTTP_InitParser(&http, NULL, 0) > 0, "The parser initializes");
    CHECK(XHTTP_Receive(&http, &reader) != XHTTP_COMPLETE, "An immediate close is not a message");
    XSock_Close(&reader);
    XHTTP_Clear(&http);
    return 0;
}

/* Records the callback types a parse drives through. */
typedef struct {
    int nStatusCalls;
    int nHeaderCalls;
    int nContentCalls;
    int nErrorCalls;
    xhttp_status_t eLastStatus;
    int nTerminateAfter;
} http_cb_test_t;

static int http_callback(xhttp_t *pHttp, xhttp_ctx_t *pCtx)
{
    http_cb_test_t *pTest = (http_cb_test_t*)pHttp->pUserCtx;

    if (pCtx->eCbType == XHTTP_STATUS) { pTest->nStatusCalls++; pTest->eLastStatus = pCtx->eStatus; }
    else if (pCtx->eCbType == XHTTP_READ_HDR) pTest->nHeaderCalls++;
    else if (pCtx->eCbType == XHTTP_READ_CNT) pTest->nContentCalls++;
    else if (pCtx->eCbType == XHTTP_ERROR) pTest->nErrorCalls++;

    if (pTest->nTerminateAfter > 0 && pTest->nStatusCalls >= pTest->nTerminateAfter)
        return XSTDERR;

    return XSTDOK;
}

static int XTest_callbacks(void)
{
    /* The parser reports its progress through the callback, and a callback
     * that refuses stops the parse instead of being ignored. */
    const char wire[] = "POST /x HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc";

    http_cb_test_t test;
    memset(&test, 0, sizeof(test));

    xhttp_t http;
    CHECK(XHTTP_InitParser(&http, NULL, 0) > 0, "The parser initializes");
    CHECK(XHTTP_SetCallback(&http, http_callback, &test, XHTTP_STATUS | XHTTP_ERROR) == XSTDOK,
        "The callback is installed");
    CHECK(XHTTP_AppendData(&http, (uint8_t*)wire, sizeof(wire) - 1) > 0, "The message is appended");
    CHECK(XHTTP_Parse(&http) == XHTTP_COMPLETE, "The message parses");
    CHECK(test.nStatusCalls > 0, "The parse reported its progress");
    XHTTP_Clear(&http);

    /* A callback that refuses terminates the parse. */
    memset(&test, 0, sizeof(test));
    test.nTerminateAfter = 1;

    CHECK(XHTTP_InitParser(&http, NULL, 0) > 0, "The parser initializes");
    CHECK(XHTTP_SetCallback(&http, http_callback, &test, XHTTP_STATUS | XHTTP_ERROR) == XSTDOK,
        "The refusing callback is installed");
    CHECK(XHTTP_AppendData(&http, (uint8_t*)wire, sizeof(wire) - 1) > 0, "The message is appended");
    CHECK(XHTTP_Parse(&http) == XHTTP_TERMINATED, "A refusing callback terminates the parse");
    XHTTP_Clear(&http);
    return 0;
}

static int XTest_lifecycle(void)
{
    /* The heap handle owns itself and clears the caller pointer. */
    xhttp_t *pHttp = XHTTP_Alloc(XHTTP_GET, 0);
    CHECK(pHttp != NULL, "A heap handle is allocated");
    CHECK(pHttp->eMethod == XHTTP_GET, "The heap handle keeps its method");
    CHECK(pHttp->nAllocated == XTRUE, "The heap handle is marked as owned");
    XHTTP_Free(&pHttp);
    CHECK(pHttp == NULL, "Freeing clears the caller pointer");

    /* A stack handle is not freed by the same call. */
    xhttp_t http;
    CHECK(XHTTP_Init(&http, XHTTP_PUT, 0) >= 0, "A stack handle initializes");
    CHECK(http.nAllocated == XFALSE, "A stack handle is not marked as owned");
    xhttp_t *pStack = &http;
    XHTTP_Free(&pStack);
    CHECK(pStack == &http, "A non-owned handle pointer is left alone");

    /* A soft reset keeps the configuration; a hard reset clears it. */
    CHECK(XHTTP_Init(&http, XHTTP_POST, 0) >= 0, "The handle initializes");
    CHECK(XHTTP_AddHeader(&http, "X-Kept", "yes") > 0, "A header is added");
    http.nContentMax = 1234;

    XHTTP_Reset(&http, XFALSE);
    CHECK(http.nContentMax == 1234, "A soft reset keeps the configured limits");

    XHTTP_Reset(&http, XTRUE);
    CHECK(http.rawData.nUsed == 0, "A hard reset empties the buffer");

    /* Copying duplicates the message rather than aliasing it. */
    CHECK(XHTTP_Init(&http, XHTTP_GET, 0) >= 0, "The source initializes");
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/source", "1.1") > 0, "The source is a request");
    CHECK(XHTTP_AddHeader(&http, "X-Source", "original") > 0, "The source gets a header");

    xhttp_t copy;
    CHECK(XHTTP_Init(&copy, XHTTP_DUMMY, 0) >= 0, "The destination initializes");
    CHECK(XHTTP_Copy(&copy, &http) == XSTDOK, "The message is copied");
    CHECK(copy.eMethod == XHTTP_GET, "The copy keeps the method");
    CHECK(strcmp(copy.sUri, "/source") == 0, "The copy keeps the target");
    CHECK(strcmp(XHTTP_GetHeader(&copy, "X-Source"), "original") == 0, "The copy keeps the headers");

    /* Clearing the source leaves the copy intact. */
    XHTTP_Clear(&http);
    CHECK(strcmp(XHTTP_GetHeader(&copy, "X-Source"), "original") == 0, "The copy outlives its source");
    XHTTP_Clear(&copy);

    /* Clearing twice is safe. */
    XHTTP_Clear(&copy);
    XHTTP_Free(NULL);
    return 0;
}

static int XTest_unix_and_auth(void)
{
    /* A unix socket target is recorded alongside the ordinary fields. */
    xhttp_t http;
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/status", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_SetUnixAddr(&http, "/var/run/app.sock") > 0, "The unix address is set");
    CHECK(strcmp(http.sUnixAddr, "/var/run/app.sock") == 0, "The unix address is recorded");
    XHTTP_Clear(&http);

    /* The token builder produces the bare base64 of user:password; the
     * scheme name is added by the header setter. */
    char sToken[256];
    CHECK(XHTTP_GetAuthToken(sToken, sizeof(sToken), "user", "pass") > 0, "The auth token is built");
    CHECK(strcmp(sToken, "dXNlcjpwYXNz") == 0, "The token is the base64 of the credentials");

    size_t nDecodedLen = strlen(sToken);
    char *pDecoded = XBase64_Decrypt((const uint8_t*)sToken, &nDecodedLen);
    CHECK(pDecoded != NULL && nDecodedLen == 9, "The token decodes to the credential pair");
    CHECK(memcmp(pDecoded, "user:pass", 9) == 0, "The decoded token is user and password joined by a colon");
    free(pDecoded);

    /* The header setter wraps the same token in the basic scheme. */
    CHECK(XHTTP_InitRequest(&http, XHTTP_GET, "/", "1.1") > 0, "The request initializes");
    CHECK(XHTTP_SetAuthBasic(&http, "user", "pass") > 0, "Basic auth is set on the request");

    const char *pHeader = XHTTP_GetHeader(&http, "Authorization");
    CHECK(pHeader != NULL, "The authorization header is present");
    CHECK(strncmp(pHeader, "Basic ", 6) == 0, "The header announces the basic scheme");
    CHECK(strcmp(&pHeader[6], sToken) == 0, "The header carries the token the builder produced");

    /* The credential survives assembly and reparsing unchanged. */
    xbyte_buffer_t *pWire = XHTTP_Assemble(&http, NULL, 0);
    CHECK(pWire != NULL, "The authenticated request assembles");

    xhttp_t parsed;
    CHECK(XHTTP_ParseData(&parsed, pWire->pData, pWire->nUsed) == XHTTP_COMPLETE,
        "The authenticated request parses back");
    CHECK(strcmp(XHTTP_GetHeader(&parsed, "Authorization"), pHeader) == 0,
        "The authorization header survives the round trip");
    XHTTP_Clear(&parsed);
    XHTTP_Clear(&http);

    /* Empty credentials still produce the base64 of a bare colon. */
    CHECK(XHTTP_GetAuthToken(sToken, sizeof(sToken), "", "") > 0, "An empty credential pair still builds a token");
    CHECK(strcmp(sToken, "Og==") == 0, "An empty credential pair encodes a bare colon");
    CHECK(XHTTP_GetAuthToken(NULL, 16, "u", "p") == 0, "A missing destination builds nothing");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(partial),
    XTEST_CASE(pipeline),
    XTEST_CASE(headers),
    XTEST_CASE(basic_auth),
    XTEST_CASE(long_basic_auth),
    XTEST_CASE(reuse),
    XTEST_CASE(empty_body),
    XTEST_CASE(code_strings),
    XTEST_CASE(content_length),
    XTEST_CASE(start_line),
    XTEST_CASE(assemble),
    XTEST_CASE(header_lookup),
    XTEST_CASE(limits),
    XTEST_CASE(callbacks),
    XTEST_CASE(lifecycle),
    XTEST_CASE(unix_and_auth)
)