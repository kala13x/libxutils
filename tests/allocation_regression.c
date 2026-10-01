/* libxutils: deterministic allocation failures; GNU/Clang ELF linker wrapping only. */
#include "test.h"
#include "json.h"
#include "http.h"
#include "ws.h"
#include "map.h"
#include "api.h"
#include "mdtp.h"
#include "str.h"
#include "srch.h"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

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

static int XTest_mdtp_assemble(void)
{
    /* Assembling either fails or gives the whole packet. A failed append
     * went unnoticed and left a packet without its header behind, and a
     * failure after the header was written leaked the header writer's buffer
     * (which the leak checkers of the sanitizer and valgrind lanes see). */
    uint8_t payload[64];
    memset(payload, 'p', sizeof(payload));
    size_t nFailures = 0;

    for (size_t i = 1; i <= 64; i++)
    {
        xpacket_t *pPacket = XPacket_New(payload, sizeof(payload));
        CHECK(pPacket != NULL, "Create a packet");
        pPacket->header.eType = XPACKET_TYPE_DATA;

        g_nCalls = 0;
        g_nFailAt = i;
        xbyte_buffer_t *pBuffer = XPacket_Assemble(pPacket);
        g_nFailAt = 0;

        if (pBuffer == NULL || !pBuffer->nUsed) nFailures++;
        else CHECK(pBuffer->nUsed > sizeof(payload), "An assembled packet carries its header and payload");

        XPacket_Free(&pPacket);
    }

    CHECK(nFailures > 3, "Exercise allocation failures while assembling");
    return 0;
}

static int XTest_map_rehash_failure(void)
{
    /* An insertion that has stored its pair succeeds even when the tombstone
       cleanup after it cannot allocate: the map keeps pointing at the value,
       and a caller told otherwise would free memory the map still uses. */
    xmap_t map;
    CHECK(XMap_Init(&map, NULL, 64) == XMAP_OK, "Create a map with room for every key");

    char sKeys[48][16];
    for (int i = 0; i < 48; i++) snprintf(sKeys[i], sizeof(sKeys[i]), "key-%d", i);
    for (int i = 0; i < 40; i++) CHECK(XMap_Put(&map, sKeys[i], sKeys[i]) == XMAP_OK, "Fill the map");
    for (int i = 0; i < 30; i++) CHECK(XMap_Remove(&map, sKeys[i]) == XMAP_OK, "Leave tombstones behind");
    CHECK(map.nDeleted > map.nTableSize / 4, "The tombstones are past the rehash threshold");

    for (int i = 40; i < 48; i++)
    {
        g_nCalls = 0;
        g_nFailAt = 1;
        int nStatus = XMap_Put(&map, sKeys[i], sKeys[i]);
        g_nFailAt = 0;

        void *pStored = XMap_Get(&map, sKeys[i]);
        CHECK(nStatus == XMAP_OK ? pStored == sKeys[i] : pStored == NULL, "A put succeeds exactly when the pair is stored");
        CHECK(nStatus == XMAP_OK, "A failed tombstone cleanup does not fail the insertion");
    }

    for (int i = 30; i < 48; i++) CHECK(XMap_Get(&map, sKeys[i]) == sKeys[i], "Every live key is still found");
    for (int i = 0; i < 30; i++) CHECK(XMap_Get(&map, sKeys[i]) == NULL, "No removed key comes back");
    CHECK(map.nCount == 18, "The count matches the live keys");

    /* With allocation working again the next insertion clears the tombstones */
    CHECK(XMap_Put(&map, (char*)"late", (void*)"late") == XMAP_OK, "A later insertion succeeds");
    CHECK(map.nDeleted <= map.nTableSize / 4, "And the tombstones are finally rehashed away");
    for (int i = 30; i < 48; i++) CHECK(XMap_Get(&map, sKeys[i]) == sKeys[i], "Every key survives the rehash");

    XMap_Destroy(&map);
    return 0;
}

static int XTest_http_parse_failure(void)
{
    /* Every allocation the header parser makes can fail. The parse then
       reports it, leaves no partial header behind, and leaves the caller's
       bytes as they were. */
    const char wire[] = "GET /x HTTP/1.1\r\nHost: a\r\nX-One: 1\r\nX-Two: 2\r\nX-Three: 3\r\n\r\n";
    size_t nFailures = 0;

    for (size_t i = 1; i <= 16; i++)
    {
        uint8_t sCopy[sizeof(wire)];
        memcpy(sCopy, wire, sizeof(wire));

        xhttp_t http;
        XHTTP_Init(&http, XHTTP_DUMMY, XSTDNON);
        XByteBuffer_SetData(&http.rawData, sCopy, sizeof(wire) - 1);

        g_nCalls = 0;
        g_nFailAt = i;
        xhttp_status_t eStatus = XHTTP_Parse(&http);
        g_nFailAt = 0;

        CHECK(eStatus == XHTTP_COMPLETE || eStatus == XHTTP_EALLOC, "A parse either completes or reports the failure");
        CHECK(memcmp(sCopy, wire, sizeof(wire)) == 0, "The caller's bytes are unchanged");
        if (eStatus == XHTTP_EALLOC) nFailures++;
        else CHECK(http.nHeaderCount == 4, "A complete parse has every header");

        XHTTP_Clear(&http);
    }

    CHECK(nFailures >= 8, "Allocation failures are reached at every header");
    return 0;
}

static int XTest_format_failure(void)
{
    /* The formatting helpers report an allocation failure rather than
       returning something short, whichever path the length takes them down. */
    char sLong[2048];
    memset(sLong, 'l', sizeof(sLong) - 1);
    sLong[sizeof(sLong) - 1] = '\0';

    const char *pArgs[] = { "short", sLong };
    for (size_t a = 0; a < 2; a++)
    {
        for (size_t i = 1; i <= 3; i++)
        {
            size_t nLength = 1;
            g_nCalls = 0;
            g_nFailAt = i;
            char *pOut = xstracpyn(&nLength, "<%s>", pArgs[a]);
            g_nFailAt = 0;

            if (pOut == NULL) CHECK(nLength == 0, "A failed format reports no length");
            else CHECK(nLength == strlen(pArgs[a]) + 2 && strlen(pOut) == nLength, "A successful format is whole");
            free(pOut);

            xbyte_buffer_t buffer;
            XByteBuffer_Init(&buffer, 0, XFALSE);
            CHECK(XByteBuffer_Add(&buffer, (const uint8_t*)"head", 4) == 4, "Start with some data");
            uint8_t *pBefore = buffer.pData;

            g_nCalls = 0;
            g_nFailAt = i;
            int nStatus = XByteBuffer_AddFmt(&buffer, "[%s]", pArgs[a]);
            g_nFailAt = 0;

            if (nStatus < 0) CHECK(buffer.nUsed == 4 && buffer.pData == pBefore, "A failed append keeps what was there");
            else CHECK(buffer.nUsed == 6 + strlen(pArgs[a]), "A successful append adds all of it");
            CHECK(memcmp(buffer.pData, "head", 4) == 0, "The data already there is untouched");
            XByteBuffer_Clear(&buffer);
        }
    }

    return 0;
}

/* Keeps every match, and reads every message it is given */
static int alloc_search_keep(xsearch_t *pSearch, xsearch_entry_t *pEntry, const char *pMsg)
{
    (void)pSearch;
    if (pMsg != NULL) return strlen(pMsg) > 0 ? XSTDNON : XSTDERR;
    return pEntry != NULL ? XSTDOK : XSTDERR;
}

static int XTest_search_append_failure(void)
{
    /* A search that cannot keep a match fails, keeps every match it did keep
       intact and releases the one it could not: nothing is freed twice or
       read after it was freed, which the sanitizer lanes check. There are
       enough matches for the result list to outgrow its pool, so every kind
       of allocation a kept match needs is failed at some point. */
    enum { SEARCH_FILES = 200 };
    char sRoot[] = "/tmp/xutils-alloc-srch-XXXXXX";
    CHECK(mkdtemp(sRoot) != NULL, "Create the search directory");

    for (int i = 0; i < SEARCH_FILES; i++)
    {
        char sPath[128];
        snprintf(sPath, sizeof(sPath), "%s/file-%03d.txt", sRoot, i);
        FILE *pFile = fopen(sPath, "w");
        CHECK(pFile != NULL, "Create a file to find");
        fclose(pFile);
    }

    /* Learn how many allocations a clean search makes */
    xsearch_t search;
    XSearch_Init(&search, "*.txt");
    search.callback = alloc_search_keep;
    g_nCalls = 0;
    g_nFailAt = SIZE_MAX;
    CHECK(XSearch(&search, sRoot) == XSTDOK, "A clean search succeeds");
    size_t nAllocations = g_nCalls;
    g_nFailAt = 0;
    CHECK(XArray_Used(&search.fileArray) == SEARCH_FILES, "A clean search finds every file");
    XSearch_Destroy(&search);

    size_t nFailures = 0;
    for (size_t i = 1; i <= nAllocations; i++)
    {
        XSearch_Init(&search, "*.txt");
        search.callback = alloc_search_keep;

        g_nCalls = 0;
        g_nFailAt = i;
        int nStatus = XSearch(&search, sRoot);
        g_nFailAt = 0;

        size_t nFound = XArray_Used(&search.fileArray);
        if (nStatus < 0) nFailures++;
        CHECK(nStatus < 0 ? nFound < SEARCH_FILES : nFound == SEARCH_FILES, "A search finds everything or reports failure");

        for (size_t j = 0; j < nFound; j++)
        {
            xsearch_entry_t *pEntry = XSearch_GetEntry(&search, (int)j);
            CHECK(pEntry != NULL && strlen(pEntry->sName) == 12 && !strncmp(pEntry->sName, "file-", 5),
                "Every kept match is intact");
        }

        XSearch_Destroy(&search);
    }

    for (int i = 0; i < SEARCH_FILES; i++)
    {
        char sPath[128];
        snprintf(sPath, sizeof(sPath), "%s/file-%03d.txt", sRoot, i);
        unlink(sPath);
    }

    rmdir(sRoot);
    CHECK(nFailures >= SEARCH_FILES, "An allocation failure is reached for every kept match");
    return 0;
}

typedef struct {
    xapi_session_t *pSession;
    xbyte_buffer_t received;
    int nRead;
    int nErrors;
    int nLastError;
    int nClosed;
} alloc_ws_t;

static int alloc_ws_callback(xapi_ctx_t *pContext, xapi_session_t *pSession)
{
    alloc_ws_t *pTest = (alloc_ws_t*)pContext->pApi->pUserCtx;
    if (pContext->eCbType == XAPI_CB_REGISTERED)
        pTest->pSession = pSession;
    else if (pContext->eCbType == XAPI_CB_CLOSED)
    {
        pTest->nClosed++;
        pTest->pSession = NULL;
    }
    else if (pContext->eCbType == XAPI_CB_ERROR)
    {
        pTest->nLastError = pContext->nStatus;
        pTest->nErrors++;
    }
    else if (pContext->eCbType == XAPI_CB_READ)
    {
        xws_frame_t *pFrame = (xws_frame_t*)pSession->pPacket;
        if (XByteBuffer_Add(&pTest->received, XWebFrame_GetPayload(pFrame), XWebFrame_GetPayloadLength(pFrame)) <= 0)
            return XAPI_DISCONNECT;
        pTest->nRead++;
    }

    return XAPI_CONTINUE;
}

static int XTest_ws_fragment_failure(void)
{
    /* A fragmented message that can not be kept or put together ends the session, reported as an allocation
       failure, with nothing of it delivered. Otherwise it is delivered whole. The sanitizer lanes check that
       nothing of it is left behind either way. */
    xbyte_buffer_t wire;
    XByteBuffer_Init(&wire, 0, XFALSE);
    CHECK(XWS_AppendFrame(&wire, (const uint8_t*)"frag", 4, XWS_TEXT, XTRUE, XFALSE) == XWS_ERR_NONE, "Open a message");
    CHECK(XWS_AppendFrame(&wire, (const uint8_t*)"ment", 4, XWS_CONTINUATION, XTRUE, XFALSE) == XWS_ERR_NONE, "Continue");
    CHECK(XWS_AppendFrame(&wire, (const uint8_t*)"ed", 2, XWS_CONTINUATION, XTRUE, XTRUE) == XWS_ERR_NONE, "End it");

    size_t nAllocations = 0, nFailures = 0;
    for (size_t i = 0; i <= nAllocations; i++)
    {
        alloc_ws_t test;
        memset(&test, 0, sizeof(test));
        CHECK(XByteBuffer_Init(&test.received, 64, XFALSE) >= 0 && test.received.nSize >= 64,
            "Reserve room for the message, so that only the session allocates");

        xapi_t api;
        CHECK(XAPI_Init(&api, alloc_ws_callback, &test) == XSTDOK, "Initialize API");
        XSOCKET pair[2];
        CHECK(XSock_CreatePair(pair) == XSTDOK, "Create a transport");
        xapi_endpoint_t endpoint;
        XAPI_InitEndpoint(&endpoint);
        endpoint.eRole = XAPI_PEER;
        endpoint.eType = XAPI_WS;
        endpoint.nFD = pair[0];
        CHECK(XAPI_AddEndpoint(&api, &endpoint) == XSTDOK && test.pSession != NULL, "Register the session");
        test.pSession->bHandshakeDone = XTRUE;
        CHECK(XByteBuffer_AddBuff(&test.pSession->rxBuffer, &wire) > 0, "Buffer the fragments");

        /* The first pass counts the allocations, every later one fails one of them */
        g_nCalls = 0;
        g_nFailAt = i ? i : SIZE_MAX;
        int nStatus = XAPI_ProcessBuffered(test.pSession);
        g_nFailAt = 0;
        if (!i) nAllocations = g_nCalls;

        if (nStatus == XAPI_CONTINUE)
        {
            CHECK(test.nRead == 1 && test.received.nUsed == 10 && !memcmp(test.received.pData, "fragmented", 10),
                "A message that could be kept is delivered whole");
            CHECK(!test.nErrors && !test.pSession->rxBuffer.nUsed, "Without an error, and with every frame consumed");
        }
        else
        {
            nFailures++;
            CHECK(test.nRead == 0 && test.nErrors == 1 && test.nLastError == XWS_ERR_ALLOC,
                "A message that could not be kept is reported once, as an allocation failure");
            CHECK(!test.pSession->bWSFragStart && !test.pSession->wsBuffer.nUsed, "And dropped");
        }

        XAPI_Destroy(&api);
        close(pair[1]);
        XByteBuffer_Clear(&test.received);
    }

    /* Three fragments kept and one message put together. Releasing the consumed bytes needs no allocation to succeed. */
    CHECK(nFailures >= 4 && nFailures <= nAllocations, "Every allocation the message needs ends it when it fails");
    XByteBuffer_Clear(&wire);
    return 0;
}

static int XTest_tls_context_failure(void)
{
#ifndef XSOCK_USE_SSL
    return 77;
#else
    for (int nClient = 0; nClient < 2; nClient++)
    {
        int pair[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "Create a transport before TLS is installed");
        xsock_t sock;
        CHECK(XSock_Init(&sock, XSOCK_TCP_PEER | XSOCK_ASYNC, pair[0]) == XSOCK_SUCCESS, "Wrap the plaintext transport");
        g_nCalls = 0;
        g_nFailAt = 1;
        XSOCKET nResult = nClient ? XSock_InitSSLClient(&sock, "localhost") : XSock_InitSSLServer(&sock, 0);
        g_nFailAt = 0;
        xbool_t bClosed = fcntl(pair[0], F_GETFD) < 0 && errno == EBADF;
        xsock_status_t eStatus = sock.eStatus;
        xbool_t bEmpty = sock.pPrivate == NULL && XSock_GetSSLCTX(&sock) == NULL && XSock_GetSSL(&sock) == NULL;
        XSock_Close(&sock);
        close(pair[1]);
        CHECK(g_nCalls == 1 && nResult == XSOCK_INVALID && eStatus == XSOCK_ERR_ALLOC, "TLS reports its allocation failure");
        CHECK(bClosed && bEmpty, "A failed TLS upgrade releases its transport and all TLS state");
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "Create a fresh transport for the same object");
        CHECK(XSock_Init(&sock, XSOCK_TCP_PEER | XSOCK_ASYNC, pair[0]) == XSOCK_SUCCESS, "Reinitialize after the failure");
        CHECK(XSock_NonBlock(&sock, XTRUE) >= 0, "The retry cannot block on the remote peer");
        nResult = nClient ? XSock_InitSSLClient(&sock, "localhost") : XSock_InitSSLServer(&sock, 0);
        xbool_t bReady = nResult >= 0 && XSock_GetSSLCTX(&sock) != NULL;
        XSock_Close(&sock);
        close(pair[1]);
        CHECK(bReady, "The TLS context can be installed after memory is available again");
    }
    return 0;
#endif
}

static int XTest_borrowed_string_failure(void)
{
    char source[] = "borrowed";
    xstring_t string;
    XString_Init(&string, 0, XFALSE);
    XString_Set(&string, source, sizeof(source) - 1);
    g_nCalls = 0;
    g_nFailAt = 1;
    int nStatus = XString_Resize(&string, 64);
    g_nFailAt = 0;
    CHECK(nStatus == XSTDERR, "A failed ownership transfer reports allocation failure");
    CHECK(string.pData == source && string.nLength == sizeof(source) - 1 && !string.nSize,
        "Allocation failure preserves the borrowed data and its explicit length");
    CHECK(XString_Resize(&string, 64) == 64, "The same string can be resized after memory becomes available");
    CHECK(string.pData != source && !strcmp(string.pData, source), "The successful retry owns the complete copy");
    XString_Clear(&string);
    return 0;
}

static int XTest_split_failure(void)
{
    size_t nFailures = 0;
    for (int nFlags = 0; nFlags < 4; nFlags++)
    {
        for (size_t nAt = 1; nAt <= 32; nAt++)
        {
            xarray_t tokens;
            XArray_Init(&tokens, NULL, 1, XFALSE);
            CHECK(XArray_AddData(&tokens, "existing", 9) == 0, "Start with a caller-owned token");
            g_nCalls = 0;
            g_nFailAt = nAt;
            size_t nCount = xstrsplita(",a,,b", ",", &tokens, nFlags & 1, nFlags & 2);
            g_nFailAt = 0;
            int nCorrect = !strcmp(XArray_GetData(&tokens, 0), "existing");
            if (g_nCalls >= nAt)
            {
                nFailures++;
                nCorrect = nCorrect && nCount == 0 && tokens.nUsed == 1;
            }
            else nCorrect = nCorrect && nCount == (size_t)3 + (nFlags & 1 ? 2 : 0) + (nFlags & 2 ? 2 : 0);
            XArray_Destroy(&tokens);
            CHECK(nCorrect, "A split either appends all fields or preserves the original token array");
        }
    }
    CHECK(nFailures > 20, "Fail token, payload and growing-array allocations");
    return 0;
}

static int XTest_long_token_failure(void)
{
    char source[4110];
    memcpy(source, "first,", 6);
    memset(source + 6, 'x', 4096);
    memcpy(source + 4102, ",last", 6);
    size_t nFailures = 0;
    for (size_t nAt = 1; nAt < 64; nAt++)
    {
        g_nCalls = 0;
        g_nFailAt = nAt;
        xarray_t *pTokens = XString_Split(source, ",");
        g_nFailAt = 0;
        int nCorrect = 1;
        if (pTokens == NULL) nFailures++;
        else
        {
            xstring_t *pFirst = XArray_GetData(pTokens, 0);
            xstring_t *pLarge = XArray_GetData(pTokens, 1);
            xstring_t *pLast = XArray_GetData(pTokens, 2);
            nCorrect = pTokens->nUsed == 3 && pFirst && pLarge && pLast &&
                pFirst->nLength == 5 && !strcmp(pFirst->pData, "first") &&
                pLarge->nLength == 4096 && !memcmp(pLarge->pData, source + 6, 4096) &&
                pLast->nLength == 4 && !strcmp(pLast->pData, "last");
        }
        XArray_Destroy(pTokens);
        CHECK(nCorrect, "A failed token growth cannot appear as a successful partial split");
    }
    CHECK(nFailures > 6, "The split refuses failures at different ownership transitions");
    return 0;
}

static int XTest_api_response_failure(void)
{
    const struct { int nCode; xapi_status_t eStatus; const char *pBody; } cases[] = {
        {401, XAPI_MISSING_TOKEN, "{\"status\": \"Missing auth basic header\"}"},
        {200, XAPI_STATUS_OK, "{\"status\": \"Unknown status\"}"}, {404, XAPI_UNKNOWN, "{\"status\": \"Not Found\"}"}
    };
    size_t nFailures = 0;
    for (size_t c = 0; c < sizeof(cases) / sizeof(*cases); c++)
    {
        size_t nAllocations = 0;
        for (size_t i = 0; i <= nAllocations; i++)
        {
            alloc_ws_t test = {0};
            xapi_t api;
            int pair[2];
            CHECK(XAPI_Init(&api, alloc_ws_callback, &test) == XSTDOK && !socketpair(AF_UNIX, SOCK_STREAM, 0, pair),
                "Create a response fixture before failing allocations");
            xapi_endpoint_t endpoint;
            XAPI_InitEndpoint(&endpoint);
            endpoint.eType = XAPI_HTTP;
            endpoint.eRole = XAPI_PEER;
            endpoint.nEvents = XPOLLIN;
            endpoint.nFD = pair[0];
            CHECK(XAPI_AddEvent(&api, &endpoint) == XSTDOK && test.pSession, "Register the response owner");
            api.events.nEventMax = 4;
            g_nCalls = 0;
            g_nFailAt = i ? i : SIZE_MAX;
            int nStatus = XAPI_RespondHTTP(test.pSession, cases[c].nCode, cases[c].eStatus);
            g_nFailAt = 0;
            if (!i) nAllocations = g_nCalls;
            if (nStatus == XEVENTS_CONTINUE)
            {
                xhttp_t response;
                CHECK(XHTTP_ParseData(&response, test.pSession->txBuffer.pData, test.pSession->txBuffer.nUsed) == XHTTP_COMPLETE,
                    "A successful response contains a complete HTTP message");
                const char *pType = XHTTP_GetHeader(&response, "Content-Type");
                const char *pAuth = XHTTP_GetHeader(&response, "WWW-Authenticate");
                CHECK(response.nStatusCode == cases[c].nCode && pType && !strcmp(pType, "application/json"),
                    "The intended status and content type survive every optional allocation failure");
                CHECK(cases[c].eStatus == XAPI_MISSING_TOKEN ? pAuth && !strcmp(pAuth, "Basic realm=\"XAPI\"") : !pAuth,
                    "Only the missing-token response carries the authentication challenge");
                CHECK(!strcmp(response.sVersion, "1.0") && XHTTP_GetBodySize(&response) == strlen(cases[c].pBody) &&
                    !memcmp(XHTTP_GetBody(&response), cases[c].pBody, strlen(cases[c].pBody)), "The version and body are exact");
                XHTTP_Clear(&response);
            }
            else
            {
                nFailures++;
                CHECK(i && test.nErrors && !test.pSession->txBuffer.nUsed,
                    "A required allocation failure reports an error without queuing a partial response");
            }
            XAPI_Destroy(&api);
            CHECK(fcntl(pair[0], F_GETFD) < 0 && errno == EBADF, "Every response owner relinquishes its descriptor");
            close(pair[1]);
        }
    }
    CHECK(nFailures > 20, "Fail distinct header, assembly and outbound-buffer allocations");
    return 0;
}

static int XTest_mdtp_header_failure(void)
{
    uint8_t payload[] = {0, 0xff, 'p', 'a', 'y'};
    size_t nAllocations = 0, nFailures = 0;
    for (size_t i = 0; i <= nAllocations; i++)
    {
        xpacket_t packet, parsed;
        CHECK(XPacket_Init(&packet, payload, sizeof(payload)) == XPACKET_ERR_NONE, "Initialize the MDTP header owner");
        XPacket_Clear(&packet);
        packet.header.eType = XPACKET_TYPE_DATA;
        packet.header.nSessionID = 0xfedcba98;
        packet.header.nTimeStamp = 1700000123;
        packet.header.nPacketID = 12345;
        packet.header.bEncrypted = XTRUE;
        strcpy(packet.header.sVersion, "1.2");
        strcpy(packet.header.sPayloadType, "application/octet-stream");
        strcpy(packet.header.sTime, "2026-10-01T12:34:56");
        strcpy(packet.header.sTZ, "UTC");
        g_nCalls = 0;
        g_nFailAt = i ? i : SIZE_MAX;
        int nStatus = XPacket_UpdateHeader(&packet);
        g_nFailAt = 0;
        if (!i) nAllocations = g_nCalls;
        if (nStatus != XPACKET_ERR_NONE)
        {
            nFailures++;
            CHECK(i && nStatus == XPACKET_ERR_ALLOC, "Every failed header allocation returns the allocation status");
            packet.header.eType = XPACKET_TYPE_DATA;
            CHECK(XPacket_UpdateHeader(&packet) == XPACKET_ERR_NONE, "Retry completes a partially constructed header");
        }
        xbyte_buffer_t *pWire = XPacket_Assemble(&packet);
        CHECK(pWire && XPacket_Parse(&parsed, pWire->pData, pWire->nUsed) == XPACKET_COMPLETE,
            "The completed header and binary payload form a complete packet");
        CHECK(parsed.header.eType == XPACKET_TYPE_DATA && parsed.header.nSessionID == 0xfedcba98 &&
            parsed.header.nTimeStamp == 1700000123 && parsed.header.nPacketID == 12345 && parsed.header.bEncrypted &&
            !strcmp(parsed.header.sVersion, "1.2") && !strcmp(parsed.header.sPayloadType, "application/octet-stream") &&
            !strcmp(parsed.header.sTime, "2026-10-01T12:34:56") && !strcmp(parsed.header.sTZ, "UTC"),
            "Every optional header field survives failure and retry with its exact value");
        CHECK(parsed.header.nPayloadSize == sizeof(payload) && !memcmp(XPacket_GetPayload(&parsed), payload, sizeof(payload)),
            "Header allocation failures never alter the caller's binary payload");
        XPacket_Clear(&parsed);
        XPacket_Clear(&packet);
    }
    CHECK(nFailures > 30, "Fail allocations throughout metadata, extra fields and encrypted payload metadata");
    return 0;
}

static int XTest_http_copy_failure(void)
{
    const uint8_t body[] = {0, 'c', 'o', 'p', 'y', 0xff};
    size_t nAllocations = 0, nFailures = 0;
    for (size_t i = 0; i <= nAllocations; i++)
    {
        xhttp_t source, copy;
        CHECK(XHTTP_InitRequest(&source, XHTTP_POST, "/copy?exact=1", "1.1") > 0 &&
            XHTTP_AddHeader(&source, "X-One", "first") > 0 && XHTTP_AddHeader(&source, "X-Two", "second") > 0 &&
            XHTTP_Assemble(&source, body, sizeof(body)), "Assemble the original HTTP message before copying");
        source.nTimeout = 4321;
        source.nContentMax = 98765;
        source.nHeaderMax = 5432;
        source.pUserCtx = &nFailures;
        strcpy(source.sUnixAddr, "/tmp/copy.sock");
        g_nCalls = 0;
        g_nFailAt = i ? i : SIZE_MAX;
        int nStatus = XHTTP_Copy(&copy, &source);
        g_nFailAt = 0;
        if (!i) nAllocations = g_nCalls;
        if (nStatus < 0) nFailures++;
        CHECK(!strcmp(XHTTP_GetHeader(&source, "X-One"), "first") &&
            !strcmp(XHTTP_GetHeader(&source, "X-Two"), "second") && XHTTP_GetBodySize(&source) == sizeof(body) &&
            !memcmp(XHTTP_GetBody(&source), body, sizeof(body)), "Copy failure preserves the original headers and binary body");
        if (nStatus > 0)
        {
            CHECK(copy.rawData.nUsed == source.rawData.nUsed &&
                !memcmp(copy.rawData.pData, source.rawData.pData, source.rawData.nUsed),
                "A successful copy preserves every wire byte");
            CHECK(copy.nTimeout == 4321 && copy.nContentMax == 98765 && copy.nHeaderMax == 5432 && copy.pUserCtx == &nFailures &&
                !strcmp(copy.sUnixAddr, "/tmp/copy.sock") && !strcmp(copy.sUri, "/copy?exact=1"),
                "A successful copy preserves limits, callback context and endpoint metadata");
            XHTTP_Clear(&source);
            CHECK(!strcmp(XHTTP_GetHeader(&copy, "X-One"), "first") && !strcmp(XHTTP_GetHeader(&copy, "X-Two"), "second") &&
                !memcmp(XHTTP_GetBody(&copy), body, sizeof(body)), "The copy owns its data independently of the original");
        }
        else XHTTP_Clear(&source);
        XHTTP_Clear(&copy);
    }
    CHECK(nFailures >= 6, "Fail payload, map, header-key and header-value allocations independently");
    return 0;
}

static int XTest_ws_request_failure(void)
{
    size_t nFailures = 0;
    for (int nDefaultPort = 0; nDefaultPort < 2; nDefaultPort++)
    {
        size_t nAllocations = 0;
        for (size_t i = 0; i <= nAllocations; i++)
        {
            alloc_ws_t test = {0};
            xapi_t api;
            int pair[2];
            CHECK(XAPI_Init(&api, alloc_ws_callback, &test) == XSTDOK && !socketpair(AF_UNIX, SOCK_STREAM, 0, pair),
                "Create a websocket client before injecting failures");
            xapi_endpoint_t endpoint;
            XAPI_InitEndpoint(&endpoint);
            endpoint.eType = XAPI_WS;
            endpoint.eRole = XAPI_CLIENT;
            endpoint.nEvents = XPOLLOUT;
            endpoint.nPort = nDefaultPort ? 80 : 4321;
            endpoint.pUri = "/socket?exact=1";
            endpoint.nFD = pair[0];
            CHECK(XAPI_AddEvent(&api, &endpoint) == XSTDOK && test.pSession, "Register the websocket client");
            strcpy(test.pSession->sAddr, "example.test");
            strcpy(test.pSession->sUserAgent, "allocation-test");
            CHECK(XSock_NonBlock(&test.pSession->sock, XTRUE) >= 0, "Handshake writes cannot block");
            api.events.nEventMax = 4;
            g_nCalls = 0;
            g_nFailAt = i ? i : SIZE_MAX;
            int nStatus = XAPI_Service(&api, 100);
            g_nFailAt = 0;
            if (!i) nAllocations = g_nCalls;
            CHECK(nStatus == XEVENTS_SUCCESS, "Handshake failures are contained to their session");
            uint8_t wire[4096];
            ssize_t nRead = recv(pair[1], wire, sizeof(wire), MSG_DONTWAIT);
            if (test.nClosed)
            {
                nFailures++;
                CHECK(i && test.nClosed == 1 && !test.pSession && nRead == 0 && test.nErrors,
                    "Failed handshake allocations close the client without transmitting a partial HTTP request");
            }
            else
            {
                xhttp_t request;
                CHECK(nRead > 0 && XHTTP_ParseData(&request, wire, nRead) == XHTTP_COMPLETE,
                    "A successful client sends a complete HTTP upgrade request");
                CHECK(request.eType == XHTTP_REQUEST && request.eMethod == XHTTP_GET &&
                    !strcmp(request.sUri, "/socket?exact=1") && !strcmp(request.sVersion, "1.1") && !XHTTP_GetBodySize(&request),
                    "Every successful request preserves its exact method, URI, version and empty body");
                const char *pNames[] = {"Upgrade", "Connection", "Sec-WebSocket-Version", "User-Agent", "Host",
                    "Sec-WebSocket-Key"};
                const char *pValues[] = {"websocket", "Upgrade", "13", "allocation-test",
                    nDefaultPort ? "example.test" : "example.test:4321", test.pSession->sKey};
                for (size_t j = 0; j < sizeof(pNames) / sizeof(*pNames); j++)
                {
                    const char *pValue = XHTTP_GetHeader(&request, pNames[j]);
                    CHECK(pValue && !strcmp(pValue, pValues[j]), "Every upgrade header has its intended exact value");
                }
                CHECK(strlen(test.pSession->sKey) == 24 && test.pSession->bHandshakeStart && !test.pSession->bHandshakeDone &&
                    !test.nErrors && !test.pSession->txBuffer.nUsed, "The client awaits an answer after sending a valid nonce");
                XHTTP_Clear(&request);
            }
            XAPI_Destroy(&api);
            close(pair[1]);
            CHECK(test.nClosed == 1 && fcntl(pair[0], F_GETFD) < 0 && errno == EBADF, "Every client descriptor is released once");
        }
    }
    CHECK(nFailures > 20, "Fail request initialization, nonce encoding, headers, assembly and transmit-buffer allocations");
    return 0;
}

static int XTest_ws_answer_failure(void)
{
    const char request[] = "GET /socket HTTP/1.1\r\nHost: example.test\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    size_t nAllocations = 0, nFailures = 0;
    for (size_t i = 0; i <= nAllocations; i++)
    {
        alloc_ws_t test = {0};
        xapi_t api;
        int pair[2];
        CHECK(XAPI_Init(&api, alloc_ws_callback, &test) == XSTDOK && !socketpair(AF_UNIX, SOCK_STREAM, 0, pair),
            "Create a websocket server before injecting failures");
        xapi_endpoint_t endpoint;
        XAPI_InitEndpoint(&endpoint);
        endpoint.eType = XAPI_WS;
        endpoint.eRole = XAPI_PEER;
        endpoint.nEvents = XPOLLIN;
        endpoint.nFD = pair[0];
        CHECK(XAPI_AddEvent(&api, &endpoint) == XSTDOK && test.pSession, "Register the accepted websocket transport");
        strcpy(test.pSession->sUserAgent, "allocation-server");
        CHECK(XSock_NonBlock(&test.pSession->sock, XTRUE) >= 0 &&
            XByteBuffer_Add(&test.pSession->rxBuffer, (const uint8_t*)request, sizeof(request) - 1) == sizeof(request) - 1,
            "Provide the complete known upgrade request before failing allocations");
        api.events.nEventMax = 4;
        g_nCalls = 0;
        g_nFailAt = i ? i : SIZE_MAX;
        int nStatus = XAPI_ProcessBuffered(test.pSession);
        g_nFailAt = 0;
        if (!i) nAllocations = g_nCalls;
        if (nStatus == XAPI_CONTINUE)
        {
            CHECK(test.pSession->txBuffer.nUsed && !test.pSession->rxBuffer.nUsed && !test.nErrors &&
                test.pSession->bHandshakeStart && !test.pSession->bHandshakeDone, "A successful upgrade queues its full answer");
            CHECK(XAPI_Service(&api, 100) == XEVENTS_SUCCESS && test.pSession && test.pSession->bHandshakeDone &&
                !test.pSession->txBuffer.nUsed, "The server completes its handshake only after sending the answer");
            uint8_t wire[4096];
            ssize_t nRead = recv(pair[1], wire, sizeof(wire), MSG_DONTWAIT);
            xhttp_t response;
            CHECK(nRead > 0 && XHTTP_ParseData(&response, wire, nRead) == XHTTP_COMPLETE,
                "The peer receives a complete HTTP upgrade response");
            CHECK(response.eType == XHTTP_RESPONSE && response.nStatusCode == 101 && !strcmp(response.sVersion, "1.1") &&
                !XHTTP_GetBodySize(&response), "Every successful upgrade has status 101, HTTP/1.1 and no unexpected body");
            const char *pNames[] = {"Upgrade", "Connection", "Sec-WebSocket-Accept", "Server"};
            const char *pValues[] = {"websocket", "Upgrade", "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "allocation-server"};
            for (size_t j = 0; j < sizeof(pNames) / sizeof(*pNames); j++)
            {
                const char *pValue = XHTTP_GetHeader(&response, pNames[j]);
                CHECK(pValue && !strcmp(pValue, pValues[j]), "The answer contains the exact headers and known accept digest");
            }
            XHTTP_Clear(&response);
        }
        else
        {
            nFailures++;
            CHECK(i && nStatus == XAPI_DISCONNECT && test.nErrors && !test.pSession->txBuffer.nUsed && !test.nClosed,
                "A failed buffered upgrade reports its failure, queues no partial answer and leaves cleanup to its caller");
        }
        CHECK(!test.pSession->pPacket, "No borrowed handshake pointer escapes the callback");
        XAPI_Destroy(&api);
        close(pair[1]);
        CHECK(test.nClosed == 1 && fcntl(pair[0], F_GETFD) < 0 && errno == EBADF, "Every server transport is released once");
    }
    CHECK(nFailures > 20, "Fail incoming parsing, response initialization, accept digest, headers and output allocations");
    return 0;
}

typedef struct {
    xapi_session_t *pListener;
    xapi_session_t *pPeer;
    int nErrors;
    int nLastError;
    int nAccepted;
    int nClosed;
    uint8_t received[16];
    size_t nReceived;
} alloc_accept_t;

static int alloc_accept_cb(xapi_ctx_t *pCtx, xapi_session_t *pSession)
{
    alloc_accept_t *pTest = pCtx->pApi->pUserCtx;
    if (pCtx->eCbType == XAPI_CB_REGISTERED) pTest->pListener = pSession;
    if (pCtx->eCbType == XAPI_CB_ERROR) { pTest->nErrors++; pTest->nLastError = pCtx->nStatus; }
    if (pCtx->eCbType == XAPI_CB_CLOSED)
    {
        pTest->nClosed++;
        if (pSession == pTest->pListener) pTest->pListener = NULL;
        if (pSession == pTest->pPeer) pTest->pPeer = NULL;
    }
    if (pCtx->eCbType == XAPI_CB_ACCEPTED)
    {
        pTest->pPeer = pSession;
        pTest->nAccepted++;
        return XAPI_SetEvents(pSession, XPOLLIN) > 0 ? XAPI_CONTINUE : XAPI_DISCONNECT;
    }
    if (pCtx->eCbType == XAPI_CB_READ)
    {
        xbyte_buffer_t *pData = pSession->pPacket;
        if (!pData || pData->nUsed > sizeof(pTest->received) - pTest->nReceived) return XAPI_DISCONNECT;
        memcpy(pTest->received + pTest->nReceived, pData->pData, pData->nUsed);
        pTest->nReceived += pData->nUsed;
    }
    return XAPI_CONTINUE;
}

static int XTest_api_accept_failure(void)
{
    size_t nAllocations = 0, nFailures = 0;
    for (size_t i = 0; i <= nAllocations; i++)
    {
        int nListener = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
        socklen_t nSize = sizeof(addr);
        CHECK(nListener >= 0 && !bind(nListener, (struct sockaddr*)&addr, nSize) && !listen(nListener, 8) &&
            !getsockname(nListener, (struct sockaddr*)&addr, &nSize), "Reserve a private TCP listener on an ephemeral port");
        alloc_accept_t test = {0};
        xapi_t api;
        CHECK(XAPI_Init(&api, alloc_accept_cb, &test) == XSTDOK, "Initialize the accepting API");
        xapi_endpoint_t endpoint;
        XAPI_InitEndpoint(&endpoint);
        endpoint.eType = XAPI_SOCK;
        endpoint.eRole = XAPI_SERVER;
        endpoint.nEvents = XPOLLIN;
        endpoint.nFD = nListener;
        CHECK(XAPI_AddEvent(&api, &endpoint) == XSTDOK && test.pListener &&
            XSock_NonBlock(&test.pListener->sock, XTRUE) >= 0, "Register the nonblocking listener before allocation failures");
        api.events.nEventMax = 8;
        int nClient = socket(AF_INET, SOCK_STREAM, 0);
        CHECK(nClient >= 0 && !connect(nClient, (struct sockaddr*)&addr, nSize), "Queue a real TCP client for acceptance");
        g_nCalls = 0;
        g_nFailAt = i ? i : SIZE_MAX;
        int nStatus = XAPI_Service(&api, 100);
        g_nFailAt = 0;
        if (!i) nAllocations = g_nCalls;
        CHECK(nStatus == XEVENTS_SUCCESS && test.pListener && test.pListener->sock.nFD == nListener,
            "An accept allocation failure never removes the listening session");
        if (test.nErrors)
        {
            nFailures++;
            CHECK(i && test.nErrors == 1 && !test.nAccepted && !test.pPeer && XAPI_GetEventCount(&api) == 1,
                "An incomplete accepted session is never published or left registered");
            if (test.nLastError == XAPI_ERR_REGISTER)
            {
                uint8_t byte;
                struct pollfd ready = {.fd = nClient, .events = POLLIN};
                CHECK(poll(&ready, 1, 5000) == 1 && recv(nClient, &byte, 1, MSG_DONTWAIT) == 0,
                    "Registration failure closes the newly accepted transport");
                close(nClient);
                nClient = socket(AF_INET, SOCK_STREAM, 0);
                CHECK(nClient >= 0 && !connect(nClient, (struct sockaddr*)&addr, nSize), "Connect another client after cleanup");
            }
            else CHECK(test.nLastError == XAPI_ERR_ALLOC, "A session allocation failure keeps the original client pending");
            CHECK(XAPI_Service(&api, 100) == XEVENTS_SUCCESS, "Retry acceptance after allocations recover");
        }
        CHECK(test.pPeer && test.nAccepted == 1 && XAPI_GetEventCount(&api) == 2, "Exactly one complete peer is published");
        int nStatusFlags = fcntl(test.pPeer->sock.nFD, F_GETFL), nFDFlags = fcntl(test.pPeer->sock.nFD, F_GETFD);
        CHECK(nStatusFlags >= 0 && nFDFlags >= 0 && (nStatusFlags & O_NONBLOCK) && (nFDFlags & FD_CLOEXEC),
            "The accepted descriptor is live, nonblocking and close-on-exec");
        const uint8_t request[] = {0, 'r', 0xff}, response[] = {'o', 0, 'k', 0x81};
        CHECK(write(nClient, request, sizeof(request)) == sizeof(request) && XAPI_Service(&api, 100) == XEVENTS_SUCCESS &&
            test.nReceived == sizeof(request) && !memcmp(test.received, request, sizeof(request)),
            "The recovered server receives the exact binary request");
        CHECK(XByteBuffer_Add(&test.pPeer->txBuffer, response, sizeof(response)) == sizeof(response) &&
            XAPI_EnableEvent(test.pPeer, XPOLLOUT) > 0 && XAPI_Service(&api, 100) == XEVENTS_SUCCESS,
            "Send the intended response through the recovered API session");
        uint8_t received[16];
        struct pollfd ready = {.fd = nClient, .events = POLLIN};
        CHECK(poll(&ready, 1, 5000) == 1 && recv(nClient, received, sizeof(received), MSG_DONTWAIT) == sizeof(response) &&
            !memcmp(received, response, sizeof(response)), "The actual TCP client receives the exact response");
        XAPI_Destroy(&api);
        close(nClient);
        CHECK(test.nClosed == 2 && !test.pPeer && !test.pListener && fcntl(nListener, F_GETFD) < 0 && errno == EBADF,
            "The listener and successfully accepted peer close exactly once");
    }
    CHECK(nFailures >= 3, "Fail the accepted session, event data and map registration allocations independently");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(api_accept_failure),
    XTEST_CASE(ws_answer_failure),
    XTEST_CASE(ws_request_failure),
    XTEST_CASE(http_copy_failure),
    XTEST_CASE(mdtp_header_failure),
    XTEST_CASE(api_response_failure),
    XTEST_CASE(tls_context_failure),
    XTEST_CASE(borrowed_string_failure),
    XTEST_CASE(split_failure),
    XTEST_CASE(long_token_failure),
    XTEST_CASE(json_cleanup),
    XTEST_CASE(http_header),
    XTEST_CASE(buffer_preservation),
    XTEST_CASE(endpoint_ownership),
    XTEST_CASE(mdtp_assemble),
    XTEST_CASE(map_rehash_failure),
    XTEST_CASE(http_parse_failure),
    XTEST_CASE(format_failure),
    XTEST_CASE(search_append_failure),
    XTEST_CASE(ws_fragment_failure)
)
