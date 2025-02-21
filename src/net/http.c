/*!
 *  @file libxutils/src/net/http.c
 *
 *  This source is part of "libxutils" project
 *  2018-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief This source includes implementation of 
 * the HTTP request/response parser and assembler.
 * 
 */

#include "xstd.h"
#include "xver.h"
#include "str.h"
#include "http.h"
#include "crypt.h"
#include "base64.h"

typedef struct XHTTPCode {
    const int nCode;
    const char* pDesc;
} xhttp_code_t;

static xhttp_code_t g_XTTPCodes[] =
{
    { 100, "Continue" },
    { 101, "Switching Protocol" },
    { 102, "Processing" },
    { 103, "Early Hints" },
    { 200, "OK" },
    { 201, "Created" },
    { 202, "Accepted" },
    { 203, "Non-Authoritative Information" },
    { 204, "No Content" },
    { 205, "Reset Content" },
    { 206, "Partial Content" },
    { 300, "Multiple Choice" },
    { 301, "Moved Permanently" },
    { 302, "Found" },
    { 303, "See Other" },
    { 304, "Not Modified" },
    { 305, "Use Proxy" },
    { 306, "Unused" },
    { 307, "Temporary Redirect" },
    { 308, "Permanent Redirect" },
    { 400, "Bad Request" },
    { 401, "Unauthorized" },
    { 402, "Payment Required" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    { 405, "Method Not Allowed" },
    { 406, "Not Acceptable" },
    { 407, "Proxy Authentication Required" },
    { 408, "Request Timeout" },
    { 409, "Conflict" },
    { 410, "Gone" },
    { 411, "Length Required" },
    { 412, "Precondition Failed" },
    { 413, "Payload Too Large" },
    { 414, "URI Too Long" },
    { 415, "Unsupported Media Type" },
    { 416, "Requested Range Not Satisfiable" },
    { 417, "Expectation Failed" },
    { 418, "I'm a teapot" },
    { 500, "Internal Server Error" },
    { 501, "Not Implemented" },
    { 502, "Bad Gateway" },
    { 503, "Service Unavailable" },
    { 504, "Gateway Timeout" },
    { 505, "HTTP Version Not Supported" },
    { 506, "Variant Also Negotiates" },
    { 507, "Insufficient Storage" },
    { 508, "Loop Detected" },
    { 510, "Not Extended" },
    { 511, "Network Authentication Required" },
    { -1, "Unknown "}
};

const char* XHTTP_GetStatusStr(xhttp_status_t eStatus)
{
    switch (eStatus)
    {
        case XHTTP_ERRINIT:
            return "Failed to init HTTP request";
        case XHTTP_ERRASSEMBLE:
            return "Failed to assemble HTTP request";
        case XHTTP_ERRCONNECT:
            return "Failed to connect remote server";
        case XHTTP_ERRRESOLVE:
            return "Failed to resolve remote address";
        case XHTTP_ERRAUTH:
            return "Failed to setup auth basic header";
        case XHTTP_ERRLINK:
            return "Invalid or unsupported address (link)";
        case XHTTP_ERRPROTO:
            return "Invalid or unsupported protocol in link";
        case XHTTP_ERRWRITE:
            return "Failed to send request to remote server";
        case XHTTP_ERRREAD:
            return "Failed to read HTTP packet from the network";
        case XHTTP_ERRTIMEO:
            return "Failed to set receive timeout on the socket";
        case XHTTP_ERRSETHDR:
            return "Failed to append header field to the request";
        case XHTTP_ERREXISTS:
            return "Header already exists in the HTTP header table";
        case XHTTP_ERRALLOC:
            return "Failed to allocate memory for HTTP packet buffer";
        case XHTTP_ERRFDMODE:
            return "Non-blocking file descriptor is not allowed for this operation";
        case XHTTP_BIGHDR:
            return "HTTP header is not detected in the bytes of active limit";
        case XHTTP_BIGCNT:
            return "HTTP Packet payload is greater than the active limit";
        case XHTTP_INCOMPLETE:
            return "Data does not contain HTTP packet or it is incomplete";
        case XHTTP_TERMINATED:
            return "Termination was requested from the HTTP callback";
        case XHTTP_COMPLETE:
            return "Succesfully parsed HTTP packet header and body";
        case XHTTP_PARSED:
            return "Succesfully parsed HTTP packet header";
        case XHTTP_INVALID:
            return "Invalid or unsupported HTTP packet";
        default:
            break;
    }

    return "Unknown status";
}

const char* XHTTP_GetCodeStr(int nCode)
{
    int i;
    for (i = 0;; i++)
    {
        if (g_XTTPCodes[i].nCode == -1 ||
            g_XTTPCodes[i].nCode == nCode)
                return g_XTTPCodes[i].pDesc;
    }

    return "Unknown";
}

const char *XHTTP_GetMethodStr(xhttp_method_t eMethod)
{
    switch(eMethod)
    {
        case XHTTP_PUT: return "PUT";
        case XHTTP_GET: return "GET";
        case XHTTP_POST: return "POST";
        case XHTTP_DELETE: return "DELETE";
        case XHTTP_OPTIONS: return "OPTIONS";
        case XHTTP_DUMMY: return "DUMMY";
        default: break;
    }

    return "UNKNOWN";
}

xhttp_method_t XHTTP_GetMethodType(const char *pData)
{
    if (!strncmp(pData, "GET", 3)) return XHTTP_GET;
    if (!strncmp(pData, "PUT", 3)) return XHTTP_PUT;
    if (!strncmp(pData, "POST", 4)) return XHTTP_POST;
    if (!strncmp(pData, "DELETE", 6)) return XHTTP_DELETE;
    if (!strncmp(pData, "OPTIONS", 7)) return XHTTP_OPTIONS;
    return XHTTP_DUMMY;
}

xbool_t XHTTP_IsSuccessCode(xhttp_t *pHandle)
{
    return (pHandle->nStatusCode >= 200 &&
            pHandle->nStatusCode < 300) ?
            XTRUE : XFALSE;
}

static int XHTTP_Callback(xhttp_t *pHandle, xhttp_cbtype_t eType, const void *pData, size_t nLength)
{
    if (pData == NULL || !nLength) return XSTDERR;
    else if (pHandle->callback == NULL) return XSTDUSR;
    else if (!XHTTP_CHECK_FLAG(pHandle->nCbTypes, eType)) return XSTDUSR;

    xhttp_ctx_t cbCtx;
    cbCtx.pData = pData;
    cbCtx.nLength = nLength;
    cbCtx.eCbType = eType;
    cbCtx.eStatus = XHTTP_NONE;
    return pHandle->callback(pHandle, &cbCtx);
}

static xhttp_status_t XHTTP_StatusCb(xhttp_t *pHandle, xhttp_status_t eStatus)
{
    xhttp_cbtype_t eType = eStatus < XHTTP_TERMINATED ? XHTTP_ERROR : XHTTP_STATUS;
    if (XHTTP_CHECK_FLAG(pHandle->nCbTypes, eType) && pHandle->callback != NULL)
    {
        char sBuffer[XHTTP_OPTION_MAX];
        const char *pStr = XHTTP_GetStatusStr(eStatus);
        size_t nLength = xstrncpyf(sBuffer, sizeof(sBuffer), "%s", pStr);

        xhttp_ctx_t cbCtx;
        cbCtx.eCbType = eType;
        cbCtx.eStatus = eStatus;
        cbCtx.nLength = nLength;
        cbCtx.pData = sBuffer;

        if (pHandle->callback(pHandle, &cbCtx) < 0)
            eStatus = XHTTP_TERMINATED;
    }

    return eStatus;
}

static void XHTTP_HeaderClearCb(xmap_pair_t *pPair)
{
    if (pPair != NULL)
    {
        if (pPair->pData != NULL) free(pPair->pData);
        if (pPair->pKey != NULL) free(pPair->pKey);
    }
}

static int XHTTP_HeaderWriteCb(xmap_pair_t *pPair, void *pContext)
{
    xhttp_t *pHttp = (xhttp_t*)pContext;
    const char *pHeader = (const char *)pPair->pKey;
    const char *pValue = (const char *)pPair->pData;

    return XByteBuffer_AddFmt(&pHttp->rawData, "%s: %s\r\n",
        pHeader, pValue) == XSTDERR ? XMAP_STOP : XMAP_OK;
}

static int XHTTP_MapIt(xmap_pair_t *pPair, void *pContext)
{
    xmap_t *pDstMap = (xmap_t*)pContext;
    int nStatus = XMap_PutPair(pDstMap, pPair);
    if (nStatus != XMAP_OK) return XMAP_STOP;
    return nStatus;
}

int XHTTP_SetCallback(xhttp_t *pHttp, xhttp_cb_t callback, void *pCbCtx, uint16_t nCbTypes)
{
    if (pHttp == NULL) return XSTDERR;
    pHttp->callback = callback;
    pHttp->nCbTypes = nCbTypes;
    pHttp->pUserCtx = pCbCtx;
    return XSTDOK;
}

int XHTTP_Init(xhttp_t *pHttp, xhttp_method_t eMethod, size_t nSize)
{
    pHttp->nHeaderLength = pHttp->nContentLength = 0;
    pHttp->nStatusCode = pHttp->nHeaderCount = 0;
    pHttp->nAllocated = pHttp->nComplete = 0;
    pHttp->sVersion[0] = pHttp->sUri[0] = '\0';
    pHttp->sUnixAddr[0] = '\0';

    pHttp->callback = NULL;
    pHttp->pUserCtx = NULL;
    pHttp->nAllowUpdate = 0;
    pHttp->nCbTypes = 0;
    pHttp->nTimeout = 0;

    pHttp->nContentMax = XHTTP_PACKAGE_MAX;
    pHttp->nHeaderMax = XHTTP_HEADER_MAX;

    pHttp->eMethod = eMethod;
    pHttp->eType = XHTTP_INITIAL;

    XMap_Init(&pHttp->headerMap, NULL, 0);
    pHttp->headerMap.clearCb = XHTTP_HeaderClearCb;

    xbyte_buffer_t *pBuffer = &pHttp->rawData;
    return XByteBuffer_Init(pBuffer, nSize, XSTDNON);
}

size_t XHTTP_SetUnixAddr(xhttp_t *pHttp, const char *pUnixAddr)
{
    XASSERT_RET((pHttp && xstrused(pUnixAddr)), XSTDERR);
    return xstrncpy(pHttp->sUnixAddr, sizeof(pHttp->sUnixAddr), pUnixAddr);
}

int XHTTP_InitRequest(xhttp_t *pHttp, xhttp_method_t eMethod, const char *pUri, const char *pVer)
{
    int nStatus = XHTTP_Init(pHttp, eMethod, XHTTP_HEADER_SIZE);
    if (nStatus <= 0) return nStatus;

    const char *pVersion = pVer != NULL ? pVer : XHTTP_VER_DEFAULT;
    const char *pFixedUrl = pUri != NULL ? pUri : "\\";

    xstrncpy(pHttp->sVersion, sizeof(pHttp->sVersion), pVersion);
    xstrncpy(pHttp->sUri, sizeof(pHttp->sUri), pFixedUrl);

    pHttp->eType = XHTTP_REQUEST;
    return nStatus;
}

int XHTTP_InitResponse(xhttp_t *pHttp, uint16_t nStatusCode, const char *pVer)
{
    int nStatus = XHTTP_Init(pHttp, XHTTP_DUMMY, XHTTP_HEADER_SIZE);
    if (nStatus <= 0) return XSTDERR;

    const char *pVersion = pVer != NULL ? pVer : XHTTP_VER_DEFAULT;
    xstrncpy(pHttp->sVersion, sizeof(pHttp->sVersion), pVersion);

    pHttp->nStatusCode = nStatusCode;
    pHttp->eType = XHTTP_RESPONSE;
    return nStatus;
}

void XHTTP_Reset(xhttp_t *pHttp, xbool_t bHard)
{
    if (bHard)
    {
        XByteBuffer_Clear(&pHttp->rawData);
        XByteBuffer_Init(&pHttp->rawData, XSTDNON, XFALSE);

        XMap_Destroy(&pHttp->headerMap);
        XMap_Init(&pHttp->headerMap, NULL, XSTDNON);
        pHttp->headerMap.clearCb = XHTTP_HeaderClearCb;
    }
    else
    {
        XByteBuffer_Reset(&pHttp->rawData);
        XMap_Reset(&pHttp->headerMap);
    }

    pHttp->nContentLength = 0;
    pHttp->nHeaderLength = 0;
    pHttp->nHeaderCount = 0;
    pHttp->nStatusCode = 0;
    pHttp->nComplete = 0;
    pHttp->sUri[0] = '\0';

    pHttp->eMethod = XHTTP_DUMMY;
    pHttp->eType = XHTTP_INITIAL;
}

xhttp_t *XHTTP_Alloc(xhttp_method_t eMethod, size_t nDataSize)
{
    xhttp_t *pHeader = (xhttp_t*)malloc(sizeof(xhttp_t));
    if (pHeader == NULL) return NULL;

    XHTTP_Init(pHeader, eMethod, nDataSize);
    xbyte_buffer_t *pBuffer = &pHeader->rawData;

    if (pBuffer->nStatus >= 0)
    {
        pHeader->nAllocated = XTRUE;
        return pHeader;
    }

    free(pHeader);
    return NULL;
}

int XHTTP_Copy(xhttp_t *pDst, xhttp_t *pSrc)
{
    if (pDst == NULL || pSrc == NULL) return XSTDERR;
    XHTTP_Init(pDst, pSrc->eMethod, XSTDNON);

    xbyte_buffer_t *pSrcBuff = &pSrc->rawData;
    xbyte_buffer_t *pDstBuff = &pDst->rawData;

    if (XByteBuffer_Add(pDstBuff, pSrcBuff->pData, pSrcBuff->nUsed) < 0) return XSTDERR;
    xstrncpy(pDst->sUnixAddr, sizeof(pDst->sUnixAddr), pSrc->sUnixAddr);
    xstrncpy(pDst->sVersion, sizeof(pDst->sVersion), pSrc->sVersion);
    xstrncpy(pDst->sUri, sizeof(pDst->sUri), pSrc->sUri);

    xmap_t *pSrcMap = &pSrc->headerMap;
    xmap_t *pDstMap = &pDst->headerMap;
    pDstMap->clearCb = XHTTP_HeaderClearCb;

    if (XMap_Iterate(pSrcMap, XHTTP_MapIt, pDstMap) != XMAP_OK)
    {
        XHTTP_Clear(pDst);
        return XSTDERR;
    }

    pDst->nHeaderCount = (uint16_t)pDstMap->nUsed;
    pDst->nContentLength = pSrc->nContentLength;
    pDst->nHeaderLength = pSrc->nHeaderLength;

    pDst->pUserCtx = pSrc->pUserCtx;
    pDst->callback = pSrc->callback;
    pDst->nCbTypes = pSrc->nCbTypes;

    pDst->nAllowUpdate = pSrc->nAllowUpdate;
    pDst->nContentMax = pSrc->nContentMax;
    pDst->nHeaderMax = pSrc->nHeaderMax;
    pDst->nStatusCode = pSrc->nStatusCode;
    pDst->nComplete = pSrc->nComplete;
    pDst->nTimeout = pSrc->nTimeout;
    pDst->eType = pSrc->eType;

    return XSTDOK;
}

void XHTTP_Clear(xhttp_t *pHttp)
{
    pHttp->headerMap.clearCb = XHTTP_HeaderClearCb;
    pHttp->nComplete = XFALSE;

    XMap_Destroy(&pHttp->headerMap);
    XByteBuffer_Clear(&pHttp->rawData);
}

void XHTTP_Free(xhttp_t **pHttp)
{
    XASSERT_VOID_RET((pHttp && *pHttp));
    xhttp_t *pHandle = *pHttp;

    XHTTP_Clear(pHandle);
    if (pHandle->nAllocated)
    {
        free(pHandle);
        *pHttp = NULL;
    }
}

int XHTTP_AddHeader(xhttp_t *pHttp, const char *pHeader, const char *pStr, ...)
{
    char sOption[XHTTP_OPTION_MAX];
    size_t nLength = 0;
    int nStatus = 0;

    va_list args;
    va_start(args, pStr);
    nLength = xstrncpyarg(sOption, sizeof(sOption), pStr, args);
    va_end(args);

    if (nLength)
    {
        xmap_pair_t *pPair = XMap_GetPair(&pHttp->headerMap, pHeader);
        if (pPair != NULL)
        {
            if (!xstrncmp(pPair->pData, sOption, nLength))
            {
                nStatus = (int)pHttp->headerMap.nUsed;
                return nStatus > 0 ? nStatus : XSTDERR;
            }

            XASSERT(pHttp->nAllowUpdate, XSTDEXC);
            free(pPair->pData);

            pPair->pData = xstrdup(sOption);
            XASSERT(pPair->pData, XSTDERR);
        }
        else
        {
            char *pValue = xstrdup(sOption);
            XASSERT(pValue, XSTDERR);

            char *pKey = xstrdup(pHeader);
            XASSERT_CALL(pKey, free, pValue, XSTDERR);

            nStatus = XMap_Put(&pHttp->headerMap, pKey, pValue);
            if (nStatus != XMAP_OK)
            {
                free(pValue);
                free(pKey);
                return XSTDERR;
            }
        }
    }

    nStatus = (int)pHttp->headerMap.nUsed;
    XASSERT_RET((nStatus > 0), XSTDERR);

    pHttp->nComplete = XFALSE;
    return nStatus;
}

size_t XHTTP_GetAuthToken(char *pToken, size_t nSize, const char *pUser, const char *pPass)
{
    char sToken[XHTTP_OPTION_MAX];
    size_t nLength = xstrncpyf(sToken, sizeof(sToken), "%s:%s", pUser, pPass);

    char *pEncodedToken = XBase64_Encrypt((const uint8_t*)sToken, &nLength);
    if (pEncodedToken == NULL) return XSTDNON;

    size_t nDstBytes = xstrncpy(pToken, nLength, pEncodedToken);
    free(pEncodedToken);
    return nDstBytes;
}

int XHTTP_SetAuthBasic(xhttp_t *pHttp, const char *pUser, const char *pPwd)
{
    if (!xstrused(pUser) || !xstrused(pPwd)) return XSTDNON;
    xbool_t nAllowUpdate = pHttp->nAllowUpdate;
    char sToken[XHTTP_OPTION_MAX];
    int nStatus = 0;

    size_t nLength = xstrncpyf(sToken, sizeof(sToken), "%s:%s", pUser, pPwd);
    char *pEncodedToken = XBase64_Encrypt((const uint8_t*)sToken, &nLength);
    if (pEncodedToken == NULL) return XSTDERR;

    pHttp->nAllowUpdate = XTRUE;
    nStatus = XHTTP_AddHeader(pHttp, "Authorization", "Basic %s", pEncodedToken);

    pHttp->nAllowUpdate = nAllowUpdate;
    free(pEncodedToken);

    return nStatus;
}

xbyte_buffer_t* XHTTP_Assemble(xhttp_t *pHttp, const uint8_t *pContent, size_t nLength)
{
    if (pHttp->nComplete) return &pHttp->rawData;
    nLength = (pContent != NULL) ? nLength : 0;
    xbool_t nAllowUpdate = pHttp->nAllowUpdate;

    xbyte_buffer_t *pBuffer = &pHttp->rawData;
    xmap_t *pHdrMap = &pHttp->headerMap;

    XByteBuffer_Clear(pBuffer);
    pHttp->nHeaderLength = 0;
    pHttp->nHeaderCount = 0;
    int nStatus = 0;

    if (pHttp->eType == XHTTP_REQUEST)
    {
        const char *pMethod = XHTTP_GetMethodStr(pHttp->eMethod);
        nStatus = XByteBuffer_AddFmt(pBuffer, "%s %s HTTP/%s\r\n",
            pMethod, pHttp->sUri, pHttp->sVersion);
    }
    else if (pHttp->eType == XHTTP_RESPONSE)
    {
        const char *pCodeStr = XHTTP_GetCodeStr(pHttp->nStatusCode);
        nStatus = XByteBuffer_AddFmt(pBuffer, "HTTP/%s %d %s\r\n",
            pHttp->sVersion, pHttp->nStatusCode, pCodeStr);
    }

    if (nStatus == XSTDERR) return NULL;

    if (nLength > 0)
    {
        pHttp->nAllowUpdate = XTRUE;
        nStatus = XHTTP_AddHeader(pHttp, "Content-Length", "%zu", nLength);
        if (nStatus <= 0) return NULL;
    }

    if ((pHdrMap->nUsed > 0 &&
        XMap_Iterate(pHdrMap, XHTTP_HeaderWriteCb, pHttp) != XMAP_OK) ||
        XByteBuffer_AddFmt(pBuffer, "%s", "\r\n") == XSTDERR) return NULL;

    pHttp->nHeaderLength = pBuffer->nUsed;
    pHttp->nHeaderCount = (uint16_t)pHdrMap->nUsed;
    if (nLength > 0 && XByteBuffer_Add(pBuffer, pContent, nLength) <= 0) return NULL;

    pHttp->nAllowUpdate = nAllowUpdate;
    pHttp->nContentLength = nLength;
    pHttp->nComplete = 1;
    return pBuffer;
}

const char* XHTTP_GetHeader(xhttp_t *pHttp, const char* pHeader)
{
    char *pKey = xstracase(pHeader, XSTR_LOWER);
    if (pKey == NULL) return NULL;

    xmap_t *pMap = &pHttp->headerMap;
    char *pHdr = (char*)XMap_Get(pMap, pKey);

    free(pKey);
    return pHdr;
}

char* XHTTP_GetHeaderRaw(xhttp_t *pHttp)
{
    if (pHttp == NULL || !pHttp->nHeaderLength) return NULL;
    else if (pHttp->rawData.nUsed < pHttp->nHeaderLength) return NULL;

    char *pHeaderStr = (char*)malloc(pHttp->nHeaderLength + 1);
    if (pHeaderStr == NULL) return NULL;

    size_t nLength = xstrncpys(
        pHeaderStr,
        pHttp->nHeaderLength + 1,
        (char*)pHttp->rawData.pData,
        pHttp->nHeaderLength
    );

    if (!nLength)
    {
        free(pHeaderStr);
        return NULL;
    }

    return pHeaderStr;
}

const uint8_t* XHTTP_GetBody(xhttp_t *pHttp)
{
    XASSERT_RET((pHttp && pHttp->nHeaderLength), NULL);
    xbyte_buffer_t *pBuffer = &pHttp->rawData;

    XASSERT_RET((pBuffer->nUsed > pHttp->nHeaderLength), NULL);
    return &pBuffer->pData[pHttp->nHeaderLength];
}

size_t XHTTP_GetBodySize(xhttp_t *pHttp)
{
    XASSERT_RET((pHttp && pHttp->nHeaderLength), XSTDNON);
    const xbyte_buffer_t *pBuffer = &pHttp->rawData;

    XASSERT_RET((pBuffer->nUsed > pHttp->nHeaderLength), XSTDNON);
    return pBuffer->nUsed - pHttp->nHeaderLength;
}

size_t XHTTP_GetExtraSize(xhttp_t *pHttp)
{
    XASSERT_RET((pHttp != NULL && pHttp->nHeaderLength), XSTDNON);
    size_t nPayloadSize = XHTTP_GetBodySize(pHttp);
    XASSERT_RET(nPayloadSize, XSTDNON);

    const char *pCntType = XHTTP_GetHeader(pHttp, "Content-Type");
    const char *pCntLen = XHTTP_GetHeader(pHttp, "Content-Length");
    XASSERT_RET((pCntType != NULL && pCntLen != NULL), nPayloadSize);

    XASSERT_RET((nPayloadSize > pHttp->nContentLength), XSTDNON);
    return nPayloadSize - pHttp->nContentLength;
}

const uint8_t* XHTTP_GetExtraData(xhttp_t *pHttp)
{
    size_t nExtraSize = XHTTP_GetExtraSize(pHttp);
    XASSERT_RET(nExtraSize, NULL);

    size_t nPacketLen = pHttp->nHeaderLength + pHttp->nContentLength;
    const xbyte_buffer_t *pBuffer = &pHttp->rawData;

    XASSERT_RET((pBuffer->nUsed > nPacketLen), NULL);
    return &pBuffer->pData[nPacketLen];
}

size_t XHTTP_GetPacketSize(xhttp_t *pHttp)
{
    XASSERT_RET((pHttp != NULL), XSTDNON);
    const xbyte_buffer_t *pBuffer = &pHttp->rawData;
    return pBuffer->nUsed - XHTTP_GetExtraSize(pHttp);
}

static int XHTTP_CheckComplete(xhttp_t *pHttp)
{
    const char *pCntType = XHTTP_GetHeader(pHttp, "Content-Type");
    size_t nPayloadSize = XHTTP_GetBodySize(pHttp);

    pHttp->nComplete = ((pHttp->nContentLength && pHttp->nContentLength <= nPayloadSize) ||
                        (!pHttp->nContentLength && !xstrused(pCntType))) ? XSTDOK : XSTDNON;

    return pHttp->nComplete;
}

static xhttp_method_t XHTTP_ParseMethod(xhttp_t *pHttp)
{
    const char *pData = (const char *)pHttp->rawData.pData;
    return XHTTP_GetMethodType(pData);
}

static xhttp_type_t XHTTP_ParseType(xhttp_t *pHttp)
{
    const char *pData = (const char*)pHttp->rawData.pData;
    if (!strncmp(pData, "HTTP", 4)) return XHTTP_RESPONSE;
    return XHTTP_REQUEST;
}

static size_t XHTTP_ParseVersion(xhttp_t *pHttp)
{
    const char *pEndPos = (pHttp->eType == XHTTP_REQUEST) ? "\r" : " ";
    const char *pData = (const char *)pHttp->rawData.pData;
    return xstrncuts(pHttp->sVersion, sizeof(pHttp->sVersion), pData, "HTTP/", pEndPos);
}

static uint16_t XHTTP_ParseCode(xhttp_t *pHttp)
{
    char sField[XHTTP_FIELD_MAX];
    const char *pData = (const char *)pHttp->rawData.pData;
    size_t nSize = xstrncuts(sField, sizeof(sField), pData, "HTTP/", NULL);
    size_t nSkip = strlen(pHttp->sVersion) + 1;
    return (nSize > 0 && nSkip < nSize) ? (uint16_t)atoi(&sField[nSkip]) : 0;
}

static size_t XHTTP_ParseHeaderLength(const char *pHdrStr)
{
    int nPosit = xstrsrc(pHdrStr, "\r\n\r\n");
    if (nPosit <= 0) return 0;
    return (size_t)nPosit + 4;
}

static size_t XHTTP_ParseContentLength(xhttp_t *pHttp)
{
    const char *pHdr = XHTTP_GetHeader(pHttp, "Content-Length");
    return (pHdr != NULL) ? atol(pHdr) : XSTDNON;
}

static size_t XHTTP_ParseUrl(xhttp_t *pHttp)
{
    if (pHttp->eType == XHTTP_RESPONSE) return XSTDOK;
    const char *pHeader = (const char *)pHttp->rawData.pData;
    const char *pMethod = XHTTP_GetMethodStr(pHttp->eMethod); 

    char sTmpUrl[XHTTP_URL_MAX];
    sTmpUrl[0] = XSTR_NUL;
    size_t nPosit = 0;

    size_t nLength = xstrncuts(sTmpUrl, sizeof(sTmpUrl), pHeader, pMethod, "HTTP/");
    if (!nLength) return XSTDNON;
    nLength -= 1;

    while (nPosit < sizeof(sTmpUrl) - 1 && sTmpUrl[nPosit] == XSTR_SPACE_CHAR) nPosit++;
    while (nLength > 0 && sTmpUrl[nLength] == XSTR_SPACE_CHAR) sTmpUrl[nLength--] = XSTR_NUL;
    return xstrncpy(pHttp->sUri, sizeof(pHttp->sUri), &sTmpUrl[nPosit]);
}

static int XHTTP_ParseHeaders(xhttp_t *pHttp)
{
    const char *pHeader = (const char *)pHttp->rawData.pData;
    xarray_t *pArr = xstrsplit(pHeader, "\r\n");
    if (pArr == NULL) return XSTDERR;

    int nStatus = XSTDOK;
    unsigned int i;

    for (i = 0; i < pArr->nUsed; i++)
    {
        char *pData = (char*)XArray_GetData(pArr, i);
        if (pData != NULL)
        {
            int nPosit = xstrsrc(pData, ":");
            if (nPosit <= 0) continue;

            char* pHeaderStr = xstracasen(pData, XSTR_LOWER, nPosit);
            if (pHeaderStr == NULL)
            {
                nStatus = XSTDERR;
                break;
            }

            if (XMap_Get(&pHttp->headerMap, pHeaderStr) != NULL)
            {
                free(pHeaderStr);
                continue;
            }

            while (pData[nPosit] == ' ' || pData[nPosit] == ':') nPosit++;
            char *pValue = xstracut(pData, nPosit, strlen(pData) - nPosit);

            if (pValue == NULL)
            {
                free(pHeaderStr);
                continue;
            }

            if (pValue[0] == XSTR_SPACE_CHAR) xstrnrm(pValue, 0, 1);
            if (XMap_Put(&pHttp->headerMap, pHeaderStr, pValue) != XMAP_OK)
            {
                nStatus = XSTDERR;
                free(pHeaderStr);
                free(pValue);
                break;
            }
        }
    }

    pHttp->nHeaderCount = (uint16_t)pHttp->headerMap.nUsed;
    XArray_Destroy(pArr);
    return nStatus;
}

int XHTTP_AppendData(xhttp_t *pHttp, uint8_t* pData, size_t nSize)
{
    xbyte_buffer_t *pBuffer = &pHttp->rawData;
    return XByteBuffer_Add(pBuffer, pData, nSize);
}

int XHTTP_InitParser(xhttp_t *pHttp, uint8_t* pData, size_t nSize)
{
    XHTTP_Init(pHttp, XHTTP_DUMMY, XSTDNON);
    int nStatus = XHTTP_AppendData(pHttp, pData, nSize);
    return nSize > 0 && nStatus <= 0 ? XSTDERR : XSTDOK;
}

xhttp_status_t XHTTP_Parse(xhttp_t *pHttp)
{
    const char *prawData = (const char*)pHttp->rawData.pData;
    size_t nHeaderLength = XHTTP_ParseHeaderLength(prawData);
    if (!nHeaderLength) return XHTTP_INCOMPLETE;

    pHttp->rawData.pData[nHeaderLength - 1] = '\0';
    pHttp->nHeaderLength = nHeaderLength;
    pHttp->eType = XHTTP_ParseType(pHttp);

    if (!XHTTP_ParseVersion(pHttp))
        return XHTTP_StatusCb(pHttp, XHTTP_INVALID);

    if (pHttp->eType == XHTTP_RESPONSE)
        pHttp->nStatusCode = XHTTP_ParseCode(pHttp);
    else if (pHttp->eType == XHTTP_REQUEST)
        pHttp->eMethod = XHTTP_ParseMethod(pHttp);

    if (!XHTTP_ParseUrl(pHttp))
        return XHTTP_StatusCb(pHttp, XHTTP_INVALID);

    if (XHTTP_ParseHeaders(pHttp) == XSTDERR)
        return XHTTP_StatusCb(pHttp, XHTTP_ERRALLOC);

    pHttp->nContentLength = XHTTP_ParseContentLength(pHttp);
    pHttp->rawData.pData[nHeaderLength - 1] = '\n';

    xhttp_status_t nStatus = XHTTP_StatusCb(pHttp, XHTTP_PARSED);
    if (nStatus == XHTTP_TERMINATED) return XHTTP_TERMINATED;
    else if (XHTTP_CheckComplete(pHttp)) return XHTTP_COMPLETE;

    return nStatus;
}

xhttp_status_t XHTTP_ParseData(xhttp_t *pHttp, uint8_t* pData, size_t nSize)
{
    int nStatus = XHTTP_InitParser(pHttp, pData, nSize);
    return nStatus > 0 ? XHTTP_Parse(pHttp) : XHTTP_ERRALLOC;
}

xhttp_status_t XHTTP_ParseBuff(xhttp_t *pHttp, xbyte_buffer_t *pBuffer)
{
    XHTTP_Init(pHttp, XHTTP_DUMMY, XSTDNON);
    XByteBuffer_Set(&pHttp->rawData, pBuffer);
    return XHTTP_Parse(pHttp);
}

xhttp_status_t XHTTP_ReadHeader(xhttp_t *pHttp, xsock_t *pSock)
{
    xbyte_buffer_t *pBuffer = (xbyte_buffer_t*)&pHttp->rawData;
    xhttp_status_t eStatus = (xhttp_status_t)XHTTP_INCOMPLETE;
    uint8_t sBuffer[XHTTP_RX_SIZE];

    while (eStatus == XHTTP_INCOMPLETE)
    {
        int nBytes = XSock_Read(pSock, sBuffer, sizeof(sBuffer));
        if (nBytes <= 0) return XHTTP_StatusCb(pHttp, XHTTP_ERRREAD);

        if (XByteBuffer_Add(pBuffer, sBuffer, nBytes) <= 0)
            return XHTTP_StatusCb(pHttp, XHTTP_ERRALLOC);

        eStatus = XHTTP_Parse(pHttp);
        if (eStatus < XHTTP_TERMINATED) return eStatus;

        if (pHttp->nHeaderMax &&
            eStatus == XHTTP_INCOMPLETE &&
            pBuffer->nUsed >= pHttp->nHeaderMax)
            return XHTTP_StatusCb(pHttp, XHTTP_BIGHDR);

        if (XSock_IsNB(pSock)) break;
    }

    int nRetVal = XHTTP_Callback(pHttp, XHTTP_READ_HDR, pBuffer->pData, pBuffer->nSize);
    if (nRetVal == XSTDERR) return XHTTP_TERMINATED;
    else if (nRetVal == XSTDNON)
    {
        pHttp->nComplete = 1;
        return XHTTP_COMPLETE;
    }

    if (eStatus != XHTTP_COMPLETE &&
        eStatus != XHTTP_PARSED) return eStatus;

    const uint8_t *pBody = XHTTP_GetBody(pHttp);
    size_t nBodySize = XHTTP_GetBodySize(pHttp);
    if (pBody == NULL || !nBodySize) return eStatus;

    nRetVal = XHTTP_Callback(pHttp, XHTTP_READ_CNT, pBody, nBodySize);
    if (nRetVal == XSTDERR) return XHTTP_TERMINATED;
    else if (nRetVal == XSTDNON)
    {
        pHttp->nComplete = 1;
        return XHTTP_COMPLETE;
    }

    return eStatus;
}

xhttp_status_t XHTTP_ReadContent(xhttp_t *pHttp, xsock_t *pSock)
{
    if (pHttp->nComplete) return XHTTP_COMPLETE;
    xbyte_buffer_t *pBuffer = &pHttp->rawData;

    uint8_t sBuffer[XHTTP_RX_SIZE];
    int nRetVal, nBytes = 0;

    if (pHttp->nContentLength)
    {
        size_t nBodySize = XHTTP_GetBodySize(pHttp);
        while (nBodySize < pHttp->nContentLength)
        {
            nBytes = XSock_Read(pSock, sBuffer, sizeof(sBuffer));
            if (nBytes <= 0) return XHTTP_StatusCb(pHttp, XHTTP_ERRREAD);

            nRetVal = XHTTP_Callback(pHttp, XHTTP_READ_CNT, sBuffer, nBytes);
            if (nRetVal == XSTDERR) return XHTTP_TERMINATED;
            else if (nRetVal == XSTDNON)
            {
                pHttp->nComplete = 1;
                return XHTTP_COMPLETE;
            }
            else if (nRetVal == XSTDOK)
            {
                if (XSock_IsNB(pSock)) break;
                nBodySize += (size_t)nBytes;
                continue;
            }

            if (XByteBuffer_Add(pBuffer, sBuffer, (size_t)nBytes) <= 0)
                return XHTTP_StatusCb(pHttp, XHTTP_ERRALLOC);

            nBodySize = XHTTP_GetBodySize(pHttp);
            if (pSock->eStatus != XSOCK_ERR_NONE || XSock_IsNB(pSock)) break;
            else if (pHttp->nContentMax && pBuffer->nUsed >= pHttp->nContentMax) 
                return XHTTP_StatusCb(pHttp, XHTTP_BIGCNT);
        }

        if (nBodySize >= pHttp->nContentLength)
        {
            pHttp->nComplete = 1;
            return XHTTP_COMPLETE;
        }

        return XHTTP_INCOMPLETE;
    }

    const char *pContentType = XHTTP_GetHeader(pHttp, "Content-Type");
    if (!xstrused(pContentType)) return XHTTP_COMPLETE;

    while (XSock_IsOpen(pSock))
    {
        nBytes = XSock_Read(pSock, sBuffer, sizeof(sBuffer));
        if (nBytes <= 0)
        {
            if ((!pHttp->nContentLength && !XHTTP_GetBodySize(pHttp)) ||
                (pHttp->nContentLength == XHTTP_GetBodySize(pHttp)) ||
                XSock_Status(pSock) == XSOCK_EOF) return XHTTP_COMPLETE;

            return XHTTP_StatusCb(pHttp, XHTTP_ERRREAD);
        }

        nRetVal = XHTTP_Callback(pHttp, XHTTP_READ_CNT, sBuffer, nBytes);
        if (nRetVal == XSTDERR) return XHTTP_TERMINATED;
        else if (nRetVal == XSTDNON)
        {
            pHttp->nComplete = 1;
            return XHTTP_COMPLETE;
        }
        else if (nRetVal == XSTDOK)
        {
            if (XSock_IsNB(pSock)) break;
            continue;
        }

        if (XByteBuffer_Add(pBuffer, sBuffer, (size_t)nBytes) <= 0)
            return XHTTP_StatusCb(pHttp, XHTTP_ERRALLOC);

        if (pSock->eStatus != XSOCK_ERR_NONE || XSock_IsNB(pSock)) break;
        else if (pHttp->nContentMax && pBuffer->nUsed >= pHttp->nContentMax) 
            return XHTTP_StatusCb(pHttp, XHTTP_BIGCNT);
    }

    if (XSock_Status(pSock) == XSOCK_EOF)
    {
        pHttp->nComplete = XTRUE;
        return XHTTP_COMPLETE;
    }

    return XHTTP_INCOMPLETE;
}

xhttp_status_t XHTTP_Receive(xhttp_t *pHttp, xsock_t *pSock)
{
    xhttp_status_t eStatus = XHTTP_ReadHeader(pHttp, pSock);
    if (eStatus != XHTTP_PARSED) return eStatus;
    return XHTTP_ReadContent(pHttp, pSock);
}

xhttp_status_t XHTTP_Exchange(xhttp_t *pRequest, xhttp_t *pResponse, xsock_t *pSock)
{
    if (XSock_IsNB(pSock)) return XHTTP_StatusCb(pRequest, XHTTP_ERRFDMODE);
    XHTTP_Init(pResponse, XHTTP_DUMMY, XSTDNON);
    xbyte_buffer_t *pBuff = &pRequest->rawData;

    int nStatus = XSock_WriteBuff(pSock, pBuff);
    if (nStatus <= 0) return XHTTP_StatusCb(pRequest, XHTTP_ERRWRITE);

    nStatus = XHTTP_Callback(
        pRequest,
        XHTTP_WRITE,
        pBuff->pData,
        pBuff->nUsed
    );

    if (nStatus == XSTDERR)
        return XHTTP_TERMINATED;

    XHTTP_SetCallback(
        pResponse, 
        pRequest->callback, 
        pRequest->pUserCtx, 
        pRequest->nCbTypes
    );

    return XHTTP_Receive(pResponse, pSock);
}

xhttp_status_t XHTTP_Connect(xhttp_t *pHttp, xsock_t *pSock, xlink_t *pLink)
{
    if (!xstrused(pLink->sProtocol)) xstrncpy(pLink->sProtocol, sizeof(pLink->sProtocol), "http");
    if (strncmp(pLink->sProtocol, "http", 4)) return XHTTP_StatusCb(pHttp, XHTTP_ERRPROTO);

    uint32_t nFlags = XSOCK_CLIENT;
    if (!strncmp(pLink->sProtocol, "https", 5))
    {
        nFlags |= XSOCK_SSL;
        XSock_InitSSL();
    }

    xsock_info_t addrInfo;
    XSock_InitInfo(&addrInfo);

    if (!xstrused(pHttp->sUnixAddr))
    {
        if (pLink->nPort <= 0)
        {
            pLink->nPort = XFlags_IsSSL(nFlags) ? XHTTP_SSL_PORT : XHTTP_DEF_PORT;
            xstrncat(pLink->sHost, sizeof(pLink->sHost), ":%d", pLink->nPort);
        }

        if (XSock_GetAddrInfo(&addrInfo, pLink->sHost) < 0)
            return XHTTP_StatusCb(pHttp, XHTTP_ERRRESOLVE);

        if (!addrInfo.nPort) addrInfo.nPort = pLink->nPort;
        nFlags |= XSOCK_TCP;
    }
    else
    {
        xstrncpy(addrInfo.sAddr, sizeof(addrInfo.sAddr), pHttp->sUnixAddr);
        nFlags |= XSOCK_UNIX;
    }

    int nStatus = XHTTP_SetAuthBasic(pHttp, pLink->sUser, pLink->sPass);
    if (nStatus < 0) return XHTTP_StatusCb(pHttp, XHTTP_ERRAUTH);

    if (XHTTP_CHECK_FLAG(pHttp->nCbTypes, XHTTP_STATUS) && pHttp->callback != NULL)
    {
        char sBuffer[XHTTP_OPTION_MAX];
        xhttp_ctx_t cbCtx;

        if (XFLAGS_CHECK(nFlags, XSOCK_UNIX))
            cbCtx.nLength = xstrncpyf(
                sBuffer, sizeof(sBuffer),
                "Connecting to unix server: %s",
                addrInfo.sAddr
            );
        else
            cbCtx.nLength = xstrncpyf(
                sBuffer, sizeof(sBuffer),
                "Connecting to remote server: %s:%d",
                addrInfo.sAddr, addrInfo.nPort
            );

        cbCtx.pData = sBuffer;
        cbCtx.eCbType = XHTTP_STATUS;
        cbCtx.eStatus = XHTTP_RESOLVED;
        pHttp->callback(pHttp, &cbCtx);
    }

    if (XSock_Open(pSock, nFlags, &addrInfo) == XSOCK_INVALID)
        return XHTTP_StatusCb(pHttp, XHTTP_ERRCONNECT);

    if (pHttp->nTimeout)
    {
        XSOCKET nFD = XSock_TimeOutR(pSock, (int)pHttp->nTimeout, 0);
        if (nFD == XSOCK_INVALID) return XHTTP_StatusCb(pHttp, XHTTP_ERRTIMEO);
    }

    return XHTTP_CONNECTED;
}

xhttp_status_t XHTTP_LinkExchange(xhttp_t *pRequest, xhttp_t *pResponse, xlink_t *pLink)
{
    xsock_t sock;
    xhttp_status_t eStatus = XHTTP_Connect(pRequest, &sock, pLink);

    if (eStatus != XHTTP_CONNECTED)
    {
        XSock_Close(&sock);
        return eStatus;
    }

    eStatus = XHTTP_Exchange(pRequest, pResponse, &sock);
    XSock_Close(&sock);
    return eStatus;
}

xhttp_status_t XHTTP_EasyExchange(xhttp_t *pRequest, xhttp_t *pResponse, const char *pLink)
{
    xlink_t link;
    int nStatus = XLink_Parse(&link, pLink);
    if (nStatus < 0) return XHTTP_StatusCb(pRequest, XHTTP_ERRLINK);
    return XHTTP_LinkExchange(pRequest, pResponse, &link);
}

xhttp_status_t XHTTP_Perform(xhttp_t *pHttp, xsock_t *pSock, const uint8_t *pBody, size_t nLength)
{
    if (XSock_IsNB(pSock)) return XHTTP_StatusCb(pHttp, XHTTP_ERRFDMODE);
    xbyte_buffer_t *pBuff = XHTTP_Assemble(pHttp, pBody, nLength);
    if (pBuff == NULL) return XHTTP_StatusCb(pHttp, XHTTP_ERRASSEMBLE);

    int nStatus = XSock_WriteBuff(pSock, pBuff);
    if (nStatus <= 0) return XHTTP_StatusCb(pHttp, XHTTP_ERRWRITE);

    nStatus = XHTTP_Callback(
        pHttp,
        XHTTP_WRITE,
        pBuff->pData,
        pBuff->nUsed
    );

    if (nStatus == XSTDERR)
        return XHTTP_TERMINATED;

    XHTTP_Reset(pHttp, XFALSE);
    return XHTTP_Receive(pHttp, pSock);
}

xhttp_status_t XHTTP_LinkPerform(xhttp_t *pHttp, xlink_t *pLink, const uint8_t *pBody, size_t nLength)
{
    xsock_t sock;
    xhttp_status_t eStatus = XHTTP_Connect(pHttp, &sock, pLink);

    if (eStatus != XHTTP_CONNECTED)
    {
        XSock_Close(&sock);
        return eStatus;
    }

    eStatus = XHTTP_Perform(pHttp, &sock, pBody, nLength);
    XSock_Close(&sock);
    return eStatus;
}

xhttp_status_t XHTTP_EasyPerform(xhttp_t *pHttp, const char *pLink, const uint8_t *pBody, size_t nLength)
{
    xlink_t link;
    if (XLink_Parse(&link, pLink) < 0) return XHTTP_ERRLINK;
    return XHTTP_LinkPerform(pHttp, &link, pBody, nLength);
}

xhttp_status_t XHTTP_SoloPerform(xhttp_t *pHttp, xhttp_method_t eMethod, const char *pLink, const uint8_t *pBody, size_t nLength)
{
    xlink_t link;
    const char *pVer = XUtils_VersionShort();

    if (XLink_Parse(&link, pLink) < 0) return XHTTP_ERRLINK;
    if (XHTTP_InitRequest(pHttp, eMethod, link.sUri, NULL) < 0) return XHTTP_ERRINIT;

    int nStatus = XHTTP_AddHeader(pHttp, "Host", "%s", link.sAddr);
    if (nStatus == XSTDERR) return XHTTP_ERRSETHDR;
    else if (nStatus == XSTDNON) return XHTTP_ERREXISTS;

    nStatus = XHTTP_AddHeader(pHttp, "User-Agent", "xutils/%s", pVer);
    if (nStatus == XSTDERR) return XHTTP_ERRSETHDR;
    else if (nStatus == XSTDNON) return XHTTP_ERREXISTS;

    return XHTTP_LinkPerform(pHttp, &link, pBody, nLength);
}
