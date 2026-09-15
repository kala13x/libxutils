/* libxutils: every allocation failure, on every path that can take one.
 *
 * Out-of-memory handling is the part of a library that production never
 * exercises until the one day it does, and by then the failure is a crash
 * rather than an error return. The usual review for it is reading the code,
 * which misses exactly the cases that matter: the third allocation inside a
 * parser, the one that grows a buffer half way through, the one after a
 * pointer has been stored but before its owner has been.
 *
 * So this file drives it mechanically. malloc, calloc, realloc and strdup
 * are wrapped by the linker. Each scenario below is run once to learn how
 * many allocations it makes, then run again once per allocation with that
 * one failing, and the assertions after each run are the same every time:
 *
 *   - the process is still alive and the API reported failure rather than
 *     handing back a half built object,
 *   - everything the scenario allocated before the failure was released.
 *
 * The second point is what makes this worth running under the sanitizer and
 * under valgrind, where a leak on an error path becomes a test failure
 * rather than a number nobody reads.
 */

#include "test.h"
#include <execinfo.h>

#include "array.h"
#include "buf.h"
#include "hash.h"
#include "json.h"
#include "jwt.h"
#include "list.h"
#include "map.h"
#include "pool.h"
#include "str.h"

#include "addr.h"
#include "http.h"
#include "mdtp.h"
#include "rtp.h"
#include "sock.h"
#include "ws.h"

#include "base64.h"
#include "crypt.h"
#include "hmac.h"
#include "sha256.h"

#include "xfs.h"
#include "srch.h"
#include "log.h"

#include <unistd.h>

/* ---------------- the injecting allocator ---------------- */

/* Counted from the first allocation after xalloc_arm(). When the counter
 * reaches g_nFailAt the allocation fails and every later one succeeds, so
 * each run tests exactly one failure point. */
/* volatile because the compiler knows malloc() as a builtin that does not
 * touch globals, so a read of these placed after a malloc() call was being
 * folded back to the value from before it. */
static volatile long g_nCount = 0;
static volatile long g_nFailAt = -1;    /* -1 disables injection */
static volatile long g_nLive = 0;       /* Outstanding blocks, for the leak check */
static volatile int g_bArmed = 0;

void *__real_malloc(size_t nSize);
void *__real_calloc(size_t nCount, size_t nSize);
void *__real_realloc(void *pPtr, size_t nSize);
void __real_free(void *pPtr);
char *__real_strdup(const char *pStr);

/* With XALLOC_BACKTRACE set, the injected allocation prints where it was
   about to happen. That turns "scenario X leaks when allocation N fails"
   into the actual call site without a debugger. */
static void xalloc_report(void)
{
    if (getenv("XALLOC_BACKTRACE") == NULL) return;

    void *pFrames[24];
    int nFrames = backtrace(pFrames, (int)(sizeof(pFrames) / sizeof(*pFrames)));

    fprintf(stderr, "--- failing allocation %ld ---\n", g_nCount);
    fflush(stderr);
    backtrace_symbols_fd(pFrames, nFrames, fileno(stderr));
}

static int xalloc_should_fail(void)
{
    if (!g_bArmed) return 0;
    if (++g_nCount != g_nFailAt) return 0;

    xalloc_report();
    return 1;
}

void *__wrap_malloc(size_t nSize)
{
    if (xalloc_should_fail()) return NULL;
    void *p = __real_malloc(nSize);
    if (p != NULL && g_bArmed) g_nLive++;
    return p;
}

void *__wrap_calloc(size_t nCount, size_t nSize)
{
    if (xalloc_should_fail()) return NULL;
    void *p = __real_calloc(nCount, nSize);
    if (p != NULL && g_bArmed) g_nLive++;
    return p;
}

void *__wrap_realloc(void *pPtr, size_t nSize)
{
    if (xalloc_should_fail()) return NULL;

    /* A realloc that grows an existing block keeps the same live count; one
     * that starts from NULL is an allocation and one that shrinks to zero
     * is a free, which is how the C library defines it. */
    void *p = __real_realloc(pPtr, nSize);
    if (!g_bArmed) return p;

    if (p != NULL && pPtr == NULL) g_nLive++;
    else if (p == NULL && nSize == 0 && pPtr != NULL) g_nLive--;
    return p;
}

void __wrap_free(void *pPtr)
{
    if (pPtr != NULL && g_bArmed) g_nLive--;
    __real_free(pPtr);
}

char *__wrap_strdup(const char *pStr)
{
    if (xalloc_should_fail()) return NULL;

    char *p = __real_strdup(pStr);
    if (p != NULL && g_bArmed) g_nLive++;
    return p;
}

/* Starts counting. nFailAt of -1 lets everything through, which is the pass
 * used to learn how many allocations a scenario makes. */
static void xalloc_arm(long nFailAt)
{
    g_nCount = 0;
    g_nLive = 0;
    g_nFailAt = nFailAt;
    g_bArmed = 1;
}

static void xalloc_disarm(void)
{
    g_bArmed = 0;
    g_nFailAt = -1;
}

/* ---------------- the scenarios ---------------- */

/* A scenario builds something and tears it down again. It must return
 * non-zero only if it detected the library handing back a broken object;
 * an allocation failure is an expected outcome, not a failure of the test. */
typedef int (*alloc_scenario_t)(void);

static int scn_json_parse(void)
{
    const char *pText =
        "{\"name\":\"libxutils\",\"tags\":[\"net\",\"crypt\",\"data\"],"
        "\"nested\":{\"a\":1,\"b\":[true,false,null],\"c\":\"\\u00e9\\u00e8\"},"
        "\"number\":-12.5e3,\"flag\":true}";

    xjson_t json;
    if (XJSON_Parse(&json, NULL, pText, strlen(pText)))
    {
        /* A parse that says it succeeded must have a usable root. */
        if (json.pRootObj == NULL) { XJSON_Destroy(&json); return 1; }

        char *pDumped = XJSON_DumpObj(json.pRootObj, 0, NULL);
        free(pDumped);
    }

    XJSON_Destroy(&json);
    return 0;
}

static int scn_json_write(void)
{
    xjson_obj_t *pRoot = XJSON_NewObject(NULL, NULL, 0);
    if (pRoot == NULL) return 0;

    for (int i = 0; i < 8; i++)
    {
        char sKey[16];
        snprintf(sKey, sizeof(sKey), "key%d", i);

        /* A child that could not be added is still the caller's, which is
         * what the array branch of XJSON_AddObject() documents and what the
         * object branch does too. Writing it as
         *
         *     XJSON_AddObject(pRoot, XJSON_NewInt(...));
         *
         * reads naturally and leaks the child the moment the insert fails,
         * so the handle is kept and released here instead. */
        xjson_obj_t *pChild = XJSON_NewInt(NULL, sKey, i);
        if (pChild == NULL) continue;

        if (XJSON_AddObject(pRoot, pChild) != XJSON_ERR_NONE)
            XJSON_FreeObject(pChild);
    }

    xjson_writer_t writer;
    if (XJSON_InitWriter(&writer, NULL, NULL, 64))
    {
        XJSON_WriteObject(pRoot, &writer);
        XJSON_DestroyWriter(&writer);
    }

    XJSON_FreeObject(pRoot);
    return 0;
}

static int scn_http_parse(void)
{
    const char *pWire =
        "POST /path/to/resource?query=1 HTTP/1.1\r\n"
        "Host: example.invalid\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 13\r\n"
        "X-Extra-Header: some value here\r\n"
        "Authorization: Basic dXNlcjpwYXNz\r\n"
        "\r\n"
        "{\"key\":true}\n";

    xhttp_t http;
    XHTTP_Init(&http, XHTTP_DUMMY, 0);
    xhttp_status_t eStatus = XHTTP_ParseData(&http, (uint8_t*)pWire, strlen(pWire));

    if (eStatus == XHTTP_COMPLETE)
    {
        /* A complete parse must be able to answer for its own fields. */
        const char *pHost = XHTTP_GetHeader(&http, "Host");
        if (pHost != NULL && strcmp(pHost, "example.invalid") != 0)
        {
            XHTTP_Clear(&http);
            return 1;
        }
    }

    XHTTP_Clear(&http);
    return 0;
}

static int scn_http_assemble(void)
{
    xhttp_t http;
    if (XHTTP_InitRequest(&http, XHTTP_POST, "/submit", "1.1") > 0)
    {
        XHTTP_AddHeader(&http, "Host", "example.invalid");
        XHTTP_AddHeader(&http, "Content-Type", "application/octet-stream");
        XHTTP_SetAuthBasic(&http, "user", "password");

        xbyte_buffer_t *pWire = XHTTP_Assemble(&http, (const uint8_t*)"body", 4);
        if (pWire != NULL && pWire->nUsed == 0) { XHTTP_Clear(&http); return 1; }
    }

    XHTTP_Clear(&http);
    return 0;
}

static int scn_ws_frame(void)
{
    xws_frame_t frame;
    XWebFrame_Init(&frame);

    const char *pPayload = "a websocket payload long enough to need its own allocation";
    if (XWebFrame_Create(&frame, (const uint8_t*)pPayload, strlen(pPayload),
        XWS_TEXT, XTRUE, XTRUE) == XWS_ERR_NONE)
    {
        xws_frame_t parsed;
        if (XWebFrame_ParseData(&parsed, frame.buffer.pData, frame.buffer.nUsed) == XWS_FRAME_COMPLETE)
        {
            const uint8_t *pBack = XWebFrame_GetPayload(&parsed);
            size_t nBack = XWebFrame_GetPayloadLength(&parsed);

            if (pBack != NULL && nBack != strlen(pPayload))
            {
                XWebFrame_Clear(&parsed);
                XWebFrame_Clear(&frame);
                return 1;
            }
        }
        XWebFrame_Clear(&parsed);
    }

    XWebFrame_Clear(&frame);
    return 0;
}

static int scn_mdtp_packet(void)
{
    const char *pPayload = "mdtp payload";

    xpacket_t packet;
    if (XPacket_Init(&packet, (uint8_t*)pPayload, (uint32_t)strlen(pPayload)) == XPACKET_ERR_NONE)
    {
        packet.header.eType = XPACKET_TYPE_DATA;
        xstrncpy(packet.header.sVersion, sizeof(packet.header.sVersion), XPACKET_VERSION_STR);

        xbyte_buffer_t *pWire = XPacket_Assemble(&packet);
        if (pWire != NULL)
        {
            xpacket_t parsed;
            XPacket_Parse(&parsed, pWire->pData, pWire->nUsed);
            XPacket_Clear(&parsed);
        }
    }

    XPacket_Clear(&packet);
    return 0;
}

static int scn_array_grow(void)
{
    xarray_t *pArray = XArray_New(NULL, 2, XFALSE);
    if (pArray == NULL) return 0;

    for (int i = 0; i < 64; i++)
    {
        char sValue[24];
        snprintf(sValue, sizeof(sValue), "entry-%d", i);
        XArray_AddData(pArray, sValue, strlen(sValue) + 1);
    }

    /* SortBy supplies the comparator; XArray_Sort() needs one of its own. */
    XArray_SortBy(pArray, XARRAY_SORTBY_SIZE);
    XArray_Destroy(pArray);
    return 0;
}

static int scn_map_grow(void)
{
    xmap_t map;
    if (XMap_Init(&map, NULL, 4) != XMAP_OK) return 0;

    char sKeys[64][16];
    for (int i = 0; i < 64; i++)
    {
        snprintf(sKeys[i], sizeof(sKeys[i]), "key-%d", i);
        XMap_Put(&map, sKeys[i], sKeys[i]);
    }

    for (int i = 0; i < 64; i++)
    {
        void *pValue = XMap_Get(&map, sKeys[i]);
        /* Anything the map says it stored must be retrievable. */
        if (pValue != NULL && pValue != sKeys[i]) { XMap_Destroy(&map); return 1; }
    }

    XMap_Destroy(&map);
    return 0;
}

static int scn_list_build(void)
{
    xlist_t *pList = XList_New((void*)"head", 0, NULL, NULL);
    if (pList == NULL) return 0;

    for (int i = 0; i < 32; i++) XList_PushNext(pList, (void*)"node", 0);
    XList_Clear(pList);
    return 0;
}

static int scn_string_build(void)
{
    xstring_t str;
    XString_Init(&str, 4, 0);

    for (int i = 0; i < 40; i++) XString_Append(&str, "chunk-%d;", i);

    XString_Replace(&str, "chunk", "piece");
    XString_Case(&str, XSTR_UPPER, 0, 0);

    xarray_t *pTokens = XString_SplitStr(&str, ";");
    XArray_Destroy(pTokens);

    /* XString_Copy() returns the copied length, not a status, so comparing
     * it against XSTDOK silently skips the cleanup for every string longer
     * than one character. It initializes the destination whenever the
     * source has data, which is what makes the unconditional clear safe. */
    if (str.pData != NULL)
    {
        xstring_t copy;
        int nCopied = XString_Copy(&copy, &str);
        if (nCopied != XSTDERR && (size_t)nCopied != str.nLength) { XString_Clear(&copy); XString_Clear(&str); return 1; }
        XString_Clear(&copy);
    }

    XString_Clear(&str);
    return 0;
}

static int scn_str_helpers(void)
{
    char *pDup = xstracpy("%s-%d", "value", 42);
    free(pDup);

    xarray_t *pParts = xstrsplit("a,b,c,d,e,f", ",");
    XArray_Destroy(pParts);

    char *pReplaced = xstrrep("the quick brown fox", "quick", "slow");
    free(pReplaced);

    size_t nCloneLen = 0;
    char *pClone = xstracpyn(&nCloneLen, "%s", "0123456789");
    if (pClone != NULL && strlen(pClone) != nCloneLen) { free(pClone); return 1; }
    free(pClone);

    return 0;
}

static int scn_buffer_grow(void)
{
    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, 8, 0);

    for (int i = 0; i < 64; i++)
    {
        uint8_t sChunk[64];
        memset(sChunk, (uint8_t)i, sizeof(sChunk));
        XByteBuffer_Add(&buffer, sChunk, sizeof(sChunk));
    }

    XByteBuffer_Insert(&buffer, 4, (const uint8_t*)"xx", 2);
    XByteBuffer_Delete(&buffer, 0, 16);

    xbyte_buffer_t *pHeap = XByteBuffer_New(32, 0);
    if (pHeap != NULL) XByteBuffer_Free(&pHeap);

    XByteBuffer_Clear(&buffer);
    return 0;
}

static int scn_pool(void)
{
    xpool_t *pPool = XPool_Create(256);
    if (pPool == NULL) return 0;

    for (int i = 0; i < 64; i++) xalloc(pPool, 64);

    XPool_Reset(pPool);
    XPool_Destroy(pPool);
    return 0;
}

static int scn_hash(void)
{
    xhash_t hash;
    XHash_Init(&hash, NULL, NULL);

    for (int i = 0; i < 64; i++) XHash_Insert(&hash, (void*)"value", 6, i);

    XHash_Destroy(&hash);
    return 0;
}

static int scn_base64(void)
{
    const uint8_t sInput[] = "a reasonably long input for base64 encoding and back again";
    size_t nLength = sizeof(sInput) - 1;

    char *pEncoded = XBase64_Encrypt(sInput, &nLength);
    if (pEncoded != NULL)
    {
        size_t nBack = nLength;
        char *pDecoded = XBase64_Decrypt((const uint8_t*)pEncoded, &nBack);

        if (pDecoded != NULL && nBack != sizeof(sInput) - 1)
        {
            free(pDecoded);
            free(pEncoded);
            return 1;
        }

        free(pDecoded);
    }

    free(pEncoded);
    return 0;
}

static int scn_crypt(void)
{
    const uint8_t sInput[] = "plaintext for the symmetric round trip";
    size_t nLength = sizeof(sInput) - 1;

    xcrypt_ctx_t ctx;
    XCrypt_Init(&ctx, XTRUE, "crc32", NULL, NULL);

    uint8_t *pCrypted = XCrypt_Multy(&ctx, sInput, &nLength);
    free(pCrypted);

    size_t nHexLength = sizeof(sInput) - 1;
    uint8_t *pHex = XCrypt_HEX(sInput, &nHexLength, " ", 0, XFALSE);
    free(pHex);

    return 0;
}

static int scn_jwt(void)
{
    const uint8_t sSecret[] = "a-shared-secret-value";
    const char *pPayload = "{\"sub\":\"1234567890\",\"name\":\"libxutils\",\"admin\":true}";

    xjwt_t jwt;
    XJWT_Init(&jwt, XJWT_ALG_HS256);

    if (XJWT_AddPayload(&jwt, pPayload, strlen(pPayload), XFALSE) == XSTDOK)
    {
        size_t nTokenLength = 0;
        char *pToken = XJWT_Create(&jwt, sSecret, sizeof(sSecret) - 1, &nTokenLength);

        if (pToken != NULL)
        {
            xjwt_t parsed;
            XJWT_Parse(&parsed, pToken, nTokenLength, sSecret, sizeof(sSecret) - 1);
            XJWT_Destroy(&parsed);
        }

        free(pToken);
    }

    XJWT_Destroy(&jwt);
    return 0;
}

static int scn_link_parse(void)
{
    xlink_t link;
    XLink_Parse(&link, "https://user:password@example.invalid:8443/deep/path/file.json?a=1&b=2#frag");
    XLink_Parse(&link, "unix:///var/run/service.sock:/v1/status");
    return 0;
}

static int scn_sock_create(void)
{
    xsock_t sock;
    if (XSock_Create(&sock, XSOCK_TCP_CLIENT | XSOCK_NB, "127.0.0.1", 9) != XSOCK_INVALID)
        XSock_Close(&sock);
    else
        XSock_Close(&sock);

    xbyte_buffer_t *pBuffer = XByteBuffer_New(128, 0);
    if (pBuffer != NULL) XByteBuffer_Free(&pBuffer);

    return 0;
}

static int scn_rtp(void)
{
    const uint8_t sWire[] =
        "\x80\x60\x00\x2a\x00\x00\x10\x00\x00\x00\x00\x07"
        "\x12\x34\x56\xa1" "\x00\x03" "abc";

    xrtp_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    XRTP_ParsePacket(&packet, (uint8_t*)sWire, sizeof(sWire) - 1);
    return 0;
}


/* ---------------- filesystem, search and logging ---------------- */

/* Every scenario that touches the filesystem works inside this directory,
 * created once and removed at the end, so a failed run leaves nothing. */
static char g_sWorkDir[128];

static int alloc_workdir_begin(void)
{
    snprintf(g_sWorkDir, sizeof(g_sWorkDir), "/tmp/xutils-alloc-XXXXXX");
    return mkdtemp(g_sWorkDir) != NULL ? XSTDOK : XSTDERR;
}

static void alloc_workdir_end(void)
{
    char sPath[256];
    const char *pNames[] = {"a.txt", "b.txt", "copy.txt", "nested/c.txt"};

    for (size_t i = 0; i < sizeof(pNames) / sizeof(*pNames); i++)
    {
        snprintf(sPath, sizeof(sPath), "%s/%s", g_sWorkDir, pNames[i]);
        unlink(sPath);
    }

    snprintf(sPath, sizeof(sPath), "%s/nested", g_sWorkDir);
    rmdir(sPath);
    rmdir(g_sWorkDir);
}

static int scn_file_io(void)
{
    char sPath[256];
    snprintf(sPath, sizeof(sPath), "%s/a.txt", g_sWorkDir);

    xfile_t file;
    if (XFile_Open(&file, sPath, "cwt", NULL) > 0)
    {
        for (int i = 0; i < 16; i++) XFile_Print(&file, "line %d of the fixture\n", i);
        XFile_Close(&file);
    }

    if (XFile_Open(&file, sPath, "r", NULL) > 0)
    {
        size_t nSize = 0;
        uint8_t *pLoaded = XFile_Load(&file, &nSize);
        free(pLoaded);

        XFile_Close(&file);
    }

    xfile_t *pAlloc = XFile_Alloc(sPath, "r", NULL);
    if (pAlloc != NULL) XFile_Free(&pAlloc);

    size_t nPathSize = 0;
    uint8_t *pWhole = XPath_Load(sPath, &nPathSize);
    free(pWhole);

    xbyte_buffer_t buffer;
    if (XPath_LoadBuffer(sPath, &buffer) > 0) XByteBuffer_Clear(&buffer);

    return 0;
}

static int scn_path_parse(void)
{
    xpath_t path;
    XPath_Parse(&path, "/one/two/three/file.txt", XTRUE);
    XPath_Parse(&path, "relative/path/file.tar.gz", XFALSE);
    return 0;
}

static int scn_search(void)
{
    char sPath[256];
    snprintf(sPath, sizeof(sPath), "%s/b.txt", g_sWorkDir);

    xfile_t file;
    if (XFile_Open(&file, sPath, "cwt", NULL) > 0)
    {
        XFile_Print(&file, "needle in the haystack\n");
        XFile_Close(&file);
    }

    xsearch_t search;
    XSearch_Init(&search, "*.txt");
    search.bRecursive = XTRUE;

    if (XSearch(&search, g_sWorkDir) >= 0) { /* entries live in the context */ }
    XSearch_Destroy(&search);
    return 0;
}

static int scn_logger(void)
{
    char sPath[256];
    snprintf(sPath, sizeof(sPath), "%s/log", g_sWorkDir);

    /* The logger is a process wide singleton, so it is left writing
     * nowhere: what is under test is the formatting path it allocates on,
     * not where the line ends up. */
    xlog_cfg_t cfg;
    XLog_ConfigGet(&cfg);

    xbool_t bFile = cfg.bToFile;
    xbool_t bScreen = cfg.bToScreen;

    cfg.bToFile = XFALSE;
    cfg.bToScreen = XFALSE;
    XLog_ConfigSet(&cfg);

    for (int i = 0; i < 8; i++)
        xlogi("a message with a %s and a %d", "string", i);

    cfg.bToFile = bFile;
    cfg.bToScreen = bScreen;
    XLog_ConfigSet(&cfg);
    return 0;
}

/* ---------------- more of the data structures ---------------- */

static int scn_xstring_ops(void)
{
    xstring_t str;
    if (XString_InitFrom(&str, "%s-%d", "seed", 7) < 0) return 0;

    XString_Insert(&str, 2, "INS", 3);
    XString_Advance(&str, 1);
    XString_Remove(&str, 0, 2);

    xstring_t sub;
    if (XString_SubStr(&str, &sub, 0, 3) >= 0) XString_Clear(&sub);

    xstring_t cut;
    if (XString_CutSub(&str, &cut, "-", "7") >= 0) XString_Clear(&cut);

    XString_Clear(&str);
    return 0;
}

static int scn_array_copy(void)
{
    xarray_t *pArray = XArray_New(NULL, 0, XFALSE);
    if (pArray == NULL) return 0;

    for (int i = 0; i < 24; i++) XArray_AddData(pArray, "payload", 8);

    /* Draining through the accessors is what walks the whole table. */
    for (size_t i = 0; i < XArray_Used(pArray); i++) XArray_GetData(pArray, i);

    while (XArray_Used(pArray) > 0)
    {
        xarray_data_t *pItem = XArray_Remove(pArray, 0);
        XArray_FreeData(pItem);
    }

    XArray_Destroy(pArray);
    return 0;
}

static int scn_json_deep(void)
{
    /* Nesting deep enough that the writer has to grow more than once. */
    xstring_t text;
    XString_Init(&text, 0, 0);

    for (int i = 0; i < 24; i++) XString_Append(&text, "{\"level%d\":", i);
    XString_Append(&text, "null");
    for (int i = 0; i < 24; i++) XString_Append(&text, "}");

    if (text.pData != NULL)
    {
        xjson_t json;
        if (XJSON_Parse(&json, NULL, text.pData, text.nLength))
        {
            char *pDumped = XJSON_DumpObj(json.pRootObj, 1, NULL);
            free(pDumped);
        }
        XJSON_Destroy(&json);
    }

    XString_Clear(&text);
    return 0;
}

static int scn_http_headers(void)
{
    /* Enough headers that the map behind them has to grow. */
    xhttp_t http;
    if (XHTTP_InitRequest(&http, XHTTP_GET, "/many", "1.1") > 0)
    {
        for (int i = 0; i < 40; i++)
        {
            char sName[32], sValue[64];
            snprintf(sName, sizeof(sName), "X-Header-%d", i);
            snprintf(sValue, sizeof(sValue), "value number %d for the header", i);
            XHTTP_AddHeader(&http, sName, sValue);
        }

        for (int i = 0; i < 40; i++)
        {
            char sName[32];
            snprintf(sName, sizeof(sName), "X-Header-%d", i);
            XHTTP_GetHeader(&http, sName);
        }
    }

    XHTTP_Clear(&http);
    return 0;
}

static int scn_ws_fragments(void)
{
    /* A fragmented message, which is where the reassembly buffer grows. */
    xws_frame_t frame;
    XWebFrame_Init(&frame);

    const char *pPart = "a fragment of a larger websocket message";
    if (XWebFrame_Create(&frame, (const uint8_t*)pPart, strlen(pPart),
        XWS_TEXT, XTRUE, XFALSE) == XWS_ERR_NONE)
    {
        XWebFrame_AppendData(&frame, (uint8_t*)pPart, strlen(pPart));
        XWebFrame_Unmask(&frame);
    }

    XWebFrame_Clear(&frame);
    return 0;
}

static int scn_hmac(void)
{
    const uint8_t sKey[] = "hmac-key";
    const uint8_t sData[] = "the message being authenticated";

    uint8_t sDigest[XSHA256_DIGEST_SIZE];
    XHMAC_SHA256(sDigest, sizeof(sDigest), sKey, sizeof(sKey) - 1, sData, sizeof(sData) - 1);

    char sHex[XSHA256_LENGTH + 1];
    XHMAC_SHA256_HEX(sHex, sizeof(sHex), sKey, sizeof(sKey) - 1, sData, sizeof(sData) - 1);

    return 0;
}

static int scn_addr_info(void)
{
    xsock_info_t info;
    XSock_AddrInfo(&info, XF_IPV4, "127.0.0.1");

    char sAddr[XSOCK_ADDR_MAX];
    XAddr_GetIFCIP("lo", sAddr, sizeof(sAddr));
    return 0;
}

typedef struct {
    const char *pName;
    alloc_scenario_t run;
} alloc_case_t;

static const alloc_case_t g_scenarios[] = {
    {"json-parse",     scn_json_parse},
    {"json-write",     scn_json_write},
    {"http-parse",     scn_http_parse},
    {"http-assemble",  scn_http_assemble},
    {"ws-frame",       scn_ws_frame},
    {"mdtp-packet",    scn_mdtp_packet},
    {"array-grow",     scn_array_grow},
    {"map-grow",       scn_map_grow},
    {"list-build",     scn_list_build},
    {"string-build",   scn_string_build},
    {"str-helpers",    scn_str_helpers},
    {"buffer-grow",    scn_buffer_grow},
    {"pool",           scn_pool},
    {"hash",           scn_hash},
    {"base64",         scn_base64},
    {"crypt",          scn_crypt},
    {"jwt",            scn_jwt},
    {"link-parse",     scn_link_parse},
    {"sock-create",    scn_sock_create},
    {"rtp",            scn_rtp},
    {"file-io",        scn_file_io},
    {"path-parse",     scn_path_parse},
    {"search",         scn_search},
    {"logger",         scn_logger},
    {"xstring-ops",    scn_xstring_ops},
    {"array-copy",     scn_array_copy},
    {"json-deep",      scn_json_deep},
    {"http-headers",   scn_http_headers},
    {"ws-fragments",   scn_ws_fragments},
    {"hmac",           scn_hmac},
    {"addr-info",      scn_addr_info}
};

/* Runs one scenario once per allocation it makes, failing that one. */
static int alloc_sweep(const alloc_case_t *pCase)
{
    /* Learning pass: how many allocations does a clean run take? */
    if (getenv("XALLOC_TRACE") != NULL)
    {
        printf("%s: learning pass\n", pCase->pName);
        fflush(stdout);
    }

    xalloc_arm(-1);
    int nBroken = pCase->run();
    long nTotal = g_nCount;
    long nLeaked = g_nLive;
    xalloc_disarm();

    if (nBroken)
    {
        printf("%s: the clean run already handed back a broken object\n", pCase->pName);
        return 1;
    }

    if (nLeaked != 0)
    {
        printf("%s: the clean run leaked %ld block(s)\n", pCase->pName, nLeaked);
        return 1;
    }

    if (nTotal == 0) return 0;   /* Nothing to inject into */

    /* One run per allocation, each failing a different one. The count can
     * differ between runs once a failure changes the path, so the sweep is
     * bounded by the clean run's count plus a margin for paths that only
     * exist after a failure. */
    for (long nAt = 1; nAt <= nTotal; nAt++)
    {
        /* Printed before the run and flushed, so a crash names the exact
         * scenario and allocation index rather than leaving it to a guess. */
        if (getenv("XALLOC_TRACE") != NULL)
        {
            printf("  %s: failing allocation %ld of %ld\n", pCase->pName, nAt, nTotal);
            fflush(stdout);
        }

        xalloc_arm(nAt);
        nBroken = pCase->run();
        nLeaked = g_nLive;
        xalloc_disarm();

        if (nBroken)
        {
            printf("%s: allocation %ld of %ld failing left a broken object\n",
                pCase->pName, nAt, nTotal);
            return 1;
        }

        if (nLeaked != 0)
        {
            printf("%s: allocation %ld of %ld failing leaked %ld block(s)\n",
                pCase->pName, nAt, nTotal, nLeaked);
            return 1;
        }
    }

    return 0;
}

static int XTest_sweep(void)
{
    /* The filesystem scenarios need somewhere to work. */
    if (alloc_workdir_begin() != XSTDOK)
    {
        printf("No temporary directory, skipping\n");
        return 77;
    }

    /* Every scenario, every allocation point. A failure names the scenario
     * and the allocation index, which is enough to reproduce it by hand:
     *
     *   XALLOC_ONLY=mdtp-packet XALLOC_AT=18 ./alloc_fail_regression sweep
     *
     * runs that one point on its own, which is what to put under a debugger
     * when the sweep reports a crash rather than a leak. */
    size_t nCount = sizeof(g_scenarios) / sizeof(*g_scenarios);
    const char *pOnly = getenv("XALLOC_ONLY");
    const char *pAt = getenv("XALLOC_AT");
    int nFailed = 0;

    if (pOnly != NULL && pAt != NULL)
    {
        for (size_t i = 0; i < nCount; i++)
        {
            if (strcmp(g_scenarios[i].pName, pOnly) != 0) continue;

            xalloc_arm(atol(pAt));
            int nBroken = g_scenarios[i].run();
            long nLeaked = g_nLive;
            xalloc_disarm();

            printf("%s at %s: broken=%d leaked=%ld\n", pOnly, pAt, nBroken, nLeaked);
            alloc_workdir_end();

            CHECK(nBroken == 0, "The single point handed back no broken object");
            CHECK(nLeaked == 0, "The single point leaked nothing");
            return 0;
        }

        printf("No scenario named %s\n", pOnly);
        alloc_workdir_end();
        return 1;
    }

    for (size_t i = 0; i < nCount; i++)
    {
        if (pOnly != NULL && strcmp(g_scenarios[i].pName, pOnly) != 0) continue;
        if (alloc_sweep(&g_scenarios[i]) != 0) nFailed++;
    }

    alloc_workdir_end();

    CHECK(nFailed == 0, "Every scenario survives a failure at every allocation it makes");
    return 0;
}

static int XTest_accounting(void)
{
    /* The harness itself has to be trustworthy, or a clean sweep proves
     * nothing. These check that the counter sees allocations, that the
     * live count returns to zero, and that injection actually injects. */
    /* volatile so the malloc/free pair cannot be elided as dead: the point
     * is that the call reaches the wrapper, not what the memory holds. */
    xalloc_arm(-1);
    void * volatile pBlock = malloc(64);
    long nAfterAlloc = g_nLive;
    long nCounted = g_nCount;
    free((void*)pBlock);
    long nAfterFree = g_nLive;
    xalloc_disarm();

    CHECK(nCounted >= 1, "The counter sees an allocation");
    CHECK(nAfterAlloc == 1, "An allocation is counted as live");
    CHECK(nAfterFree == 0, "Freeing it brings the live count back to zero");

    /* Injection at the first allocation makes it fail. */
    xalloc_arm(1);
    void * volatile pFailed = malloc(64);
    xalloc_disarm();
    CHECK(pFailed == NULL, "The injected allocation fails");

    /* And only that one: the next succeeds. */
    xalloc_arm(1);
    void * volatile pFirst = malloc(64);
    void * volatile pSecond = malloc(64);
    long nLive = g_nLive;
    xalloc_disarm();

    CHECK(pFirst == NULL, "The first allocation fails");
    CHECK(pSecond != NULL, "The one after it succeeds");
    CHECK(nLive == 1, "Only the successful one is live");
    free((void*)pSecond);

    /* Nothing is counted while the harness is disarmed. */
    long nBefore = g_nCount;
    void * volatile pQuiet = malloc(64);
    free((void*)pQuiet);
    CHECK(g_nCount == nBefore, "Allocations outside a run are not counted");

    /* realloc is tracked through its three shapes. */
    xalloc_arm(-1);
    void * volatile pGrow = realloc(NULL, 16);
    long nAfterGrowAlloc = g_nLive;
    pGrow = realloc((void*)pGrow, 128);
    long nAfterGrow = g_nLive;
    free((void*)pGrow);
    long nAfterGrowFree = g_nLive;
    xalloc_disarm();

    CHECK(nAfterGrowAlloc == 1, "A realloc from nothing is an allocation");
    CHECK(nAfterGrow == 1, "A realloc that grows is not a second one");
    CHECK(nAfterGrowFree == 0, "Freeing the result brings it back to zero");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(accounting),
    XTEST_CASE(sweep)
)
