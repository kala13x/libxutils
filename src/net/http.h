/*!
 *  @file libxutils/src/net/http.h
 *
 *  This source is part of "libxutils" project
 *  2018-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief This source includes implementation of 
 * the HTTP request/response parser and assembler.
 * 
 */

#ifndef __XUTILS_HTTP_H__
#define __XUTILS_HTTP_H__

#include "xstd.h"
#include "addr.h"
#include "sock.h"
#include "map.h"
#include "xbuf.h"
#include "xtype.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XHTTP_CHECK_FLAG(c, f) (((c) & (f)) == (f))

#define XHTTP_VER_DEFAULT       "1.0"
#define XHTTP_PACKAGE_MAX       5000 * 1024
#define XHTTP_HEADER_MAX        32 * 1024
#define XHTTP_HEADER_SIZE       4096
#define XHTTP_OPTION_MAX        1024
#define XHTTP_FIELD_MAX         128
#define XHTTP_ADDR_MAX          256
#define XHTTP_URL_MAX           2048
#define XHTTP_RX_SIZE           4096

#define XHTTP_SSL_PORT          443
#define XHTTP_DEF_PORT          80

typedef enum {
    XHTTP_DUMMY = 0,
    XHTTP_PUT,
    XHTTP_GET,
    XHTTP_POST,
    XHTTP_DELETE,
    XHTTP_OPTIONS
} xhttp_method_t;

typedef enum {
    XHTTP_INITIAL = 0,
    XHTTP_REQUEST,
    XHTTP_RESPONSE
} xhttp_type_t;

typedef enum {
    XHTTP_NONE = (uint8_t)0,
    XHTTP_INVALID,
    XHTTP_ERRINIT,
    XHTTP_ERRLINK,
    XHTTP_ERRAUTH,
    XHTTP_ERRREAD,
    XHTTP_ERRWRITE,
    XHTTP_ERRPROTO,
    XHTTP_ERRTIMEO,
    XHTTP_ERRALLOC,
    XHTTP_ERRSETHDR,
    XHTTP_ERRFDMODE,
    XHTTP_ERREXISTS,
    XHTTP_ERRCONNECT,
    XHTTP_ERRRESOLVE,
    XHTTP_ERRASSEMBLE,
    XHTTP_TERMINATED,
    XHTTP_INCOMPLETE,
    XHTTP_CONNECTED,
    XHTTP_RESOLVED,
    XHTTP_COMPLETE,
    XHTTP_BIGCNT,
    XHTTP_BIGHDR,
    XHTTP_PARSED
} xhttp_status_t;

typedef enum
{
    XHTTP_OTHER = (1 << 0),
    XHTTP_WRITE = (1 << 1),
    XHTTP_ERROR = (1 << 2),
    XHTTP_STATUS = (1 << 3),
    XHTTP_READ_HDR = (1 << 4),
    XHTTP_READ_CNT = (1 << 5)
} xhttp_cbtype_t;

typedef struct xhttp_ctx_ {
    xhttp_status_t eStatus;
    xhttp_cbtype_t eCbType;
    const void* pData;
    size_t nLength;
} xhttp_ctx_t;

struct xhttp_;
typedef struct xhttp_ xhttp_t;
typedef int(*xhttp_cb_t)(xhttp_t *pHttp, xhttp_ctx_t *pCbCtx);

struct xhttp_ {
    xhttp_cb_t callback;
    uint16_t nCbTypes;
    void *pUserCtx;

    xhttp_method_t eMethod;
    xbyte_buffer_t rawData;
    xhttp_type_t eType;
    xmap_t headerMap;

    uint16_t nHeaderCount;
    uint16_t nStatusCode;
    size_t nContentLength;
    size_t nHeaderLength;

    size_t nContentMax;
    size_t nHeaderMax;
    size_t nTimeout;

    xbool_t nAllowUpdate;
    xbool_t nAllocated;
    xbool_t nComplete;

    char sUnixAddr[XHTTP_ADDR_MAX];
    char sVersion[XHTTP_FIELD_MAX];
    char sUri[XHTTP_URL_MAX];
};

xbool_t XHTTP_IsSuccessCode(xhttp_t *pHandle);
const char* XHTTP_GetCodeStr(int nCode);
const char* XHTTP_GetMethodStr(xhttp_method_t eMethod);
const char* XHTTP_GetStatusStr(xhttp_status_t eStatus);
xhttp_method_t XHTTP_GetMethodType(const char *pData);

void XHTTP_Clear(xhttp_t *pHttp);
void XHTTP_Free(xhttp_t **pHttp);
void XHTTP_Reset(xhttp_t *pHttp, xbool_t bHard);
xhttp_t *XHTTP_Alloc(xhttp_method_t eMethod, size_t nDataSize);

int XHTTP_Copy(xhttp_t *pDst, xhttp_t *pSrc);
int XHTTP_Init(xhttp_t *pHttp, xhttp_method_t eMethod, size_t nSize);

int XHTTP_InitRequest(xhttp_t *pHttp, xhttp_method_t eMethod, const char *pUri, const char *pVer);
int XHTTP_InitResponse(xhttp_t *pHttp, uint16_t nStatusCode, const char *pVer);

size_t XHTTP_SetUnixAddr(xhttp_t *pHttp, const char *pUnixAddr);
size_t XHTTP_GetAuthToken(char *pToken, size_t nSize, const char *pUser, const char *pPass);
int XHTTP_SetAuthBasic(xhttp_t *pHttp, const char *pUser, const char *pPwd);
int XHTTP_AddHeader(xhttp_t *pHttp, const char *pHeader, const char *pStr, ...);
xbyte_buffer_t* XHTTP_Assemble(xhttp_t *pHttp, const uint8_t *pContent, size_t nLength);

const char* XHTTP_GetHeader(xhttp_t *pHttp, const char* pHeader);
const uint8_t* XHTTP_GetExtraData(xhttp_t *pHttp);
const uint8_t* XHTTP_GetBody(xhttp_t *pHttp);
char* XHTTP_GetHeaderRaw(xhttp_t *pHttp);
size_t XHTTP_GetBodySize(xhttp_t *pHttp);
size_t XHTTP_GetExtraSize(xhttp_t *pHttp);
size_t XHTTP_GetPacketSize(xhttp_t *pHttp);

int XHTTP_InitParser(xhttp_t *pHttp, uint8_t* pData, size_t nSize);
int XHTTP_AppendData(xhttp_t *pHttp, uint8_t* pData, size_t nSize);
xhttp_status_t XHTTP_ParseData(xhttp_t *pHttp, uint8_t* pData, size_t nSize);
xhttp_status_t XHTTP_ParseBuff(xhttp_t *pHttp, xbyte_buffer_t *pBuffer);
xhttp_status_t XHTTP_Parse(xhttp_t *pHttp);

int XHTTP_SetCallback(xhttp_t *pHttp, xhttp_cb_t callback, void *pCbCtx, uint16_t nCbTypes);
xhttp_status_t XHTTP_ReadHeader(xhttp_t *pHttp, xsock_t *pSock);
xhttp_status_t XHTTP_ReadContent(xhttp_t *pHttp, xsock_t *pSock);
xhttp_status_t XHTTP_Receive(xhttp_t *pHttp, xsock_t *pSock);

xhttp_status_t XHTTP_Connect(xhttp_t *pHttp, xsock_t *pSock, xlink_t *pLink);
xhttp_status_t XHTTP_Exchange(xhttp_t *pRequest, xhttp_t *pResponse, xsock_t *pSock);
xhttp_status_t XHTTP_LinkExchange(xhttp_t *pRequest, xhttp_t *pResponse, xlink_t *pLink);
xhttp_status_t XHTTP_EasyExchange(xhttp_t *pRequest, xhttp_t *pResponse, const char *pLink);

xhttp_status_t XHTTP_Perform(xhttp_t *pHttp, xsock_t *pSock, const uint8_t *pBody, size_t nLength);
xhttp_status_t XHTTP_LinkPerform(xhttp_t *pHttp, xlink_t *pLink, const uint8_t *pBody, size_t nLength);
xhttp_status_t XHTTP_EasyPerform(xhttp_t *pHttp, const char *pLink, const uint8_t *pBody, size_t nLength);
xhttp_status_t XHTTP_SoloPerform(xhttp_t *pHttp, xhttp_method_t eMethod, const char *pLink, const uint8_t *pBody, size_t nLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_HTTP_H__ */
