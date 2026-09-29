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
} alloc_ws_t;

static int alloc_ws_callback(xapi_ctx_t *pContext, xapi_session_t *pSession)
{
    alloc_ws_t *pTest = (alloc_ws_t*)pContext->pApi->pUserCtx;
    if (pContext->eCbType == XAPI_CB_REGISTERED)
        pTest->pSession = pSession;
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

XTEST_MAIN(
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
