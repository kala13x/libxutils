/*!
 *  @file libxutils/src/net/sock.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Cross-plaform socket operations such as
 * create, bind, connect, listen, select and etc.
 */

#ifdef _XUTILS_USE_GNU
#define _GNU_SOURCE
#endif

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#include "sock.h"
#include "sync.h"
#include "str.h"
#include "xfs.h"

/*
  S.K. >> Note:
    Disable deprecated warnings for gethostbyaddr() function.
    Library already have safer implementation of getaddrinfo()
    but also supporting old implementation for legacy devices.
*/
#ifdef _WIN32
#pragma warning(disable : 4996)
#endif

#define XSOCK_MIN(a,b) (((a)<(b))?(a):(b))

xsock_addr_t* XSock_InAddr(xsock_t *pSock) { return &pSock->sockAddr; }
xsock_status_t XSock_Status(const xsock_t *pSock) { return pSock->eStatus; }
uint32_t XSock_GetFlags(const xsock_t *pSock) { return pSock->nFlags; }

XSOCKET XSock_GetFD(const xsock_t *pSock) { return pSock->nFD; }
xbool_t XSock_IsSSL(const xsock_t *pSock) { return XFLAGS_CHECK(pSock->nFlags, XSOCK_SSL); }
xbool_t XSock_IsNB(const xsock_t *pSock) { return XFLAGS_CHECK(pSock->nFlags, XSOCK_NB); }

uint32_t XSock_GetNetAddr(const xsock_t *pSock) { return pSock->nAddr; }
uint16_t XSock_GetPort(const xsock_t *pSock) { return pSock->nPort; }
int XSock_GetSockType(const xsock_t *pSock) { return pSock->nType; }
int XSock_GetProto(const xsock_t *pSock) { return pSock->nProto; }

xbool_t XFlags_IsSSL(uint32_t nFlags)
{
    if (XFLAGS_CHECK(nFlags, XSOCK_SSL) ||
        XFLAGS_CHECK(nFlags, XSOCK_SSLV2) ||
        XFLAGS_CHECK(nFlags, XSOCK_SSLV3))
            return XTRUE;

    return XFALSE;
}

static uint32_t XFlags_Adjust(uint32_t nFlags)
{
    if (XFLAGS_CHECK(nFlags, XSOCK_SSLV2) ||
        XFLAGS_CHECK(nFlags, XSOCK_SSLV3))
    {
        nFlags |= XSOCK_SSL;
    }

    if (XFLAGS_CHECK(nFlags, XSOCK_BROADCAST) ||
        XFLAGS_CHECK(nFlags, XSOCK_MULTICAST) ||
        XFLAGS_CHECK(nFlags, XSOCK_UNICAST))
    {
        nFlags |= XSOCK_UDP;
    }

    return nFlags;
}

#ifdef XSOCK_USE_SSL
static xatomic_t g_nSSLInit = 0;

typedef struct XSocketPriv {
    xbool_t bConnected;
    void *pSSLCTX;
    void *pSSL;
} xsock_priv_t;

static xsock_priv_t* XSock_AllocPriv()
{
    xsock_priv_t *pPriv = (xsock_priv_t*)malloc(sizeof(xsock_priv_t));
    if (pPriv == NULL) return NULL;

    pPriv->bConnected = XFALSE;
    pPriv->pSSLCTX = NULL;
    pPriv->pSSL = NULL;
    return pPriv;
}

static xsock_priv_t* XSock_GetOrAllocPriv(xsock_t *pSock)
{
    if (pSock == NULL) return NULL;
    else if (pSock->pPrivate == NULL)
        pSock->pPrivate = XSock_AllocPriv();
    return (xsock_priv_t*)pSock->pPrivate;
}

SSL_CTX* XSock_GetSSLCTX(xsock_t *pSock)
{
    if (pSock == NULL || pSock->pPrivate == NULL) return NULL;
    xsock_priv_t *pPriv = (xsock_priv_t*)pSock->pPrivate;
    return (SSL_CTX*)pPriv->pSSLCTX;
}

SSL* XSock_GetSSL(xsock_t *pSock)
{
    if (pSock == NULL || pSock->pPrivate == NULL) return NULL;
    xsock_priv_t *pPriv = (xsock_priv_t*)pSock->pPrivate;
    return (SSL*)pPriv->pSSL;
}

static XSOCKET XSock_SetSSLCTX(xsock_t *pSock, SSL_CTX *pSSLCTX)
{
    xsock_priv_t *pPriv = XSock_GetOrAllocPriv(pSock);
    if (pPriv == NULL)
    {
        pSock->eStatus = XSOCK_ERR_ALLOC;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    XFLAGS_ENABLE(pSock->nFlags, XSOCK_SSL);
    pPriv->pSSLCTX = pSSLCTX;
    return pSock->nFD;
}

static XSOCKET XSock_SetSSL(xsock_t *pSock, SSL *pSSL)
{
    xsock_priv_t *pPriv = XSock_GetOrAllocPriv(pSock);
    if (pPriv == NULL)
    {
        pSock->eStatus = XSOCK_ERR_ALLOC;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    XFLAGS_ENABLE(pSock->nFlags, XSOCK_SSL);
    pPriv->pSSL = pSSL;
    return pSock->nFD;
}

static void XSock_SSLConnected(xsock_t *pSock, xbool_t bConnected)
{
    xsock_priv_t *pPriv = (xsock_priv_t*)pSock->pPrivate;
    if (pPriv != NULL) pPriv->bConnected = bConnected;
}

static uint32_t XSock_GetPrefredSSL(uint32_t nFlags)
{
    if (!XFLAGS_CHECK(nFlags, XSOCK_SSL))
        return nFlags;

    if (XFLAGS_CHECK(nFlags, XSOCK_CLIENT))
    {
#ifdef SSLv3_client_method
        nFlags |= XSOCK_SSLV3;
#else
        nFlags |= XSOCK_SSLV2;
#endif
    }
    else if (XFLAGS_CHECK(nFlags, XSOCK_SERVER))
    {
#ifdef SSLv3_server_method
        nFlags |= XSOCK_SSLV3;
#else
        nFlags |= XSOCK_SSLV2;
#endif
    }

    return nFlags;
}

static const SSL_METHOD* XSock_GetSSLMethod(xsock_t *pSock)
{
    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_CLIENT))
    {
        if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSLV3))
        {
#ifdef SSLv3_client_method
            return SSLv3_client_method();
#else
            return NULL;
#endif
        }
        else if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSLV2))
        {
#ifdef SSLv23_client_method
            return SSLv23_client_method();
#else
            return NULL;
#endif
        }
    }
    else if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SERVER))
    {
        if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSLV3))
        {
#ifdef SSLv3_server_method
            return SSLv3_server_method();
#else
            return NULL;
#endif
        }
        else if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSLV2))
        {
#ifdef SSLv23_server_method
            return SSLv23_server_method();
#else
            return NULL;
#endif
        }
    }

    return NULL;
}
#endif

void XSock_InitSSL(void)
{
#ifdef XSOCK_USE_SSL
    if (XSYNC_ATOMIC_GET(&g_nSSLInit)) return;

#if OPENSSLVERSION_NUMBER < 0x10100000L
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
#else
    OPENSSL_init_ssl(0, NULL);
#endif

    XSYNC_ATOMIC_SET(&g_nSSLInit, 1);
#endif
}

void XSock_DeinitSSL(void)
{
#ifdef XSOCK_USE_SSL
    if (!XSYNC_ATOMIC_GET(&g_nSSLInit)) return;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    EVP_cleanup();
    ERR_free_strings();
    CRYPTO_cleanup_all_ex_data();
#else
    OPENSSL_cleanup();
#endif

    XSYNC_ATOMIC_SET(&g_nSSLInit, 0);
#endif
}

int XSock_LastSSLError(char *pDst, size_t nSize)
{
    if (pDst == NULL) return XSOCK_NONE;
    size_t nLength = 0;
    pDst[0] = XSTR_NUL;

#ifdef XSOCK_USE_SSL
    BIO *pBIO = BIO_new(BIO_s_mem());
    if (pBIO == NULL) return 0;

    ERR_print_errors(pBIO);
    char *pErrBuff = NULL;

    int nErrSize = BIO_get_mem_data(pBIO, &pErrBuff);
    if (nErrSize <= 0 && pErrBuff == NULL)
    {
        BIO_free(pBIO);
        return 0;
    }

    nLength = nSize < (size_t)nErrSize ?
        nSize - 1 : (size_t)nErrSize - 1;

    strncpy(pDst, pErrBuff, nLength);
    pDst[nLength] = 0;
    BIO_free(pBIO);
#else
    (void)nSize;
#endif

    return (int)nLength;
}

int xclosesock(XSOCKET nFd)
{
#ifdef _WIN32
    return closesocket(nFd);
#else
    return close(nFd);
#endif
}

const char* XSock_GetStatusStr(xsock_status_t eStatus)
{
    switch(eStatus)
    {
        case XSOCK_ERR_NONE:
            return "No error was identified";
        case XSOCK_ERR_BIND:
            return "Can not bind the socket";
        case XSOCK_ERR_JOIN:
            return "Can not join to the socket";
        case XSOCK_ERR_SEND:
            return "Can not send data with the socket";
        case XSOCK_ERR_RECV:
            return "Can not receive data from the socket";
        case XSOCK_ERR_READ:
            return "Can not read data from the socket";
        case XSOCK_ERR_WRITE:
            return "Can not write data fo the socket";
        case XSOCK_ERR_SETFL:
            return "Can not set flags to the socket";
        case XSOCK_ERR_GETFL:
            return "Can not get flags from the socket";
        case XSOCK_ERR_ACCEPT:
            return "Can not accept to the socket";
        case XSOCK_ERR_ARGS:
            return "Invalid arguments for the socket";
        case XSOCK_ERR_CONNECT:
            return "Can not connect to the socket";
        case XSOCK_ERR_LISTEN:
            return "Can not listen to the socket";
        case XSOCK_ERR_SETOPT:
            return "Can not set options to the socket";
        case XSOCK_ERR_CREATE:
            return "Can not create the socket";
        case XSOCK_ERR_INVALID:
            return "Socket is not open";
        case XSOCK_ERR_SUPPORT:
            return "Unsupported socket type";
        case XSOCK_ERR_SSLACC:
            return "Can not accept SSL connection";
        case XSOCK_ERR_SSLCNT:
            return "Can not connect to SSL server";
        case XSOCK_ERR_NOSSL:
            return "No SSL (OpenSSL) support";
        case XSOCK_ERR_SSLCTX:
            return "Can not create SSL context";
        case XSOCK_ERR_SSLKEY:
            return "Can not set SSL key file";
        case XSOCK_ERR_SSLCRT:
            return "Can not set SSL sert file";
        case XSOCK_ERR_PKCS12:
            return "Failed to load PKCS12 file";
        case XSOCK_ERR_SSLCA:
            return "Can not set SSL CA file";
        case XSOCK_ERR_SSLINV:
            return "Invalid SSL object or context";
        case XSOCK_ERR_SSLNEW:
            return "Failed to create new SSL object";
        case XSOCK_ERR_SSLREAD:
            return "Can not read from SSL socket";
        case XSOCK_ERR_SSLWRITE:
            return "Can not write to SSL socket";
        case XSOCK_ERR_FLAGS:
            return "Invalid or empty socket flags";
        case XSOCK_ERR_INVSSL:
            return "Invalid SSL or SSL context";
        case XSOCK_ERR_SYSCALL:
            return "SSL operation failed in syscall";
        case XSOCK_WANT_READ:
            return "Wait for read event for non-blocking operation";
        case XSOCK_WANT_WRITE:
            return "Wait for write event for non-blocking operation";
        case XSOCK_ERR_SSLMET:
            return "SSL method is not defined in the SSL library";
        case XSOCK_ERR_SSLERR:
            return "SSL_ERROR_SSL ocurred during SSL read or write";
        case XSOCK_ERR_ALLOC:
            return "Failed to allocate data for private SSL context";
        case XSOCK_ERR_ADDR:
            return "Failed get IP address from hostname";
        case XSOCK_EOF:
            return "Received FIN from the remote side";
        default:
            break;
    }

    return "Undefined error";
}

const char* XSock_ErrStr(xsock_t *pSock)
{
    if (pSock == NULL) return XSTR_EMPTY;
    return XSock_GetStatusStr(pSock->eStatus);
}

XSTATUS XSock_IsOpen(xsock_t *pSock)
{
    return pSock->nFD != XSOCK_INVALID ?
            XSOCK_SUCCESS : XSOCK_NONE;
}

XSTATUS XSock_Check(xsock_t *pSock)
{
    if (pSock->nFD == XSOCK_INVALID)
    {
        if (pSock->eStatus == XSOCK_ERR_NONE)
            pSock->eStatus = XSOCK_ERR_INVALID;
        return XSOCK_NONE;
    }

    pSock->eStatus = XSOCK_ERR_NONE;
    return XSOCK_SUCCESS;
}

xsocklen_t XSock_GetAddrLen(xsock_t *pSock)
{
    return XFLAGS_CHECK(pSock->nFlags, XSOCK_UNIX) ?
        sizeof(pSock->sockAddr.unAddr) :
        sizeof(pSock->sockAddr.inAddr);
}

xsockaddr_t* XSock_GetSockAddr(xsock_t *pSock)
{
    return XFLAGS_CHECK(pSock->nFlags, XSOCK_UNIX) ?
        (xsockaddr_t*)&pSock->sockAddr.unAddr :
        (xsockaddr_t*)&pSock->sockAddr.inAddr;
}

static XSTATUS XSock_SetFlags(xsock_t *pSock, uint32_t nFlags)
{
    pSock->eStatus = XSOCK_ERR_NONE;
    pSock->nFlags = nFlags;

    if (XFLAGS_CHECK(nFlags, XSOCK_UNIX))
    {
        pSock->nDomain = AF_UNIX;
        pSock->nProto = XSOCK_NONE;
        pSock->nType = SOCK_STREAM;

        if (XFLAGS_CHECK(nFlags, XSOCK_UDP))
            pSock->nType = SOCK_DGRAM;
    }
    else if (XFLAGS_CHECK(nFlags, XSOCK_TCP))
    {
        pSock->nDomain = AF_INET;
        pSock->nProto = IPPROTO_TCP;
        pSock->nType = SOCK_STREAM;
    }
    else if (XFLAGS_CHECK(nFlags, XSOCK_UDP))
    {
        pSock->nDomain = AF_INET;
        pSock->nProto = IPPROTO_UDP;
        pSock->nType = SOCK_DGRAM;
    }
    else if (XFLAGS_CHECK(nFlags, XSOCK_RAW))
    {
        pSock->nDomain = AF_INET;
        pSock->nProto = IPPROTO_RAW;
        pSock->nType = SOCK_RAW;
    }
    else
    {
        pSock->eStatus = XSOCK_ERR_SUPPORT;
        pSock->nDomain = XSOCK_ERROR;
        pSock->nProto = XSOCK_ERROR;
        pSock->nType = XSOCK_ERROR;
        return XSOCK_ERROR;
    }

    return XSOCK_SUCCESS;
}

XSTATUS XSock_Init(xsock_t *pSock, uint32_t nFlags, XSOCKET nFD)
{
    XASSERT_RET((pSock != NULL), XSOCK_ERROR);
    memset(&pSock->sockAddr, 0, sizeof(pSock->sockAddr));

    pSock->pPrivate = NULL;
    pSock->nDomain = 0;
    pSock->nProto = 0;
    pSock->nType = 0;
    pSock->nAddr = 0;
    pSock->nPort = 0;
    pSock->nFD = nFD;

    nFlags = XFlags_Adjust(nFlags);
    if (nFlags == XSOCK_UNDEFINED)
    {
        pSock->eStatus = XSOCK_ERR_FLAGS;
        return XSOCK_ERROR;
    }

 #ifdef XSOCK_USE_SSL
    nFlags = XSock_GetPrefredSSL(nFlags);

    if (XFlags_IsSSL(nFlags))
    {
        pSock->pPrivate = XSock_AllocPriv();
        if (pSock->pPrivate == NULL)
        {
            pSock->eStatus = XSOCK_ERR_ALLOC;
            pSock->nFD = XSOCK_INVALID;
            return XSOCK_ERROR;
        }
    }
 #endif

    return XSock_SetFlags(pSock, nFlags);
}

void XSock_Close(xsock_t *pSock)
{
 #ifdef XSOCK_USE_SSL
    if (pSock && pSock->pPrivate != NULL)
    {
        xsock_priv_t *pPriv = (xsock_priv_t*)pSock->pPrivate;
        SSL *pSSL = (SSL*)pPriv->pSSL;

        if (pSSL != NULL)
        {
            if (pPriv->bConnected)
                SSL_shutdown(pSSL);

            SSL_free(pSSL);
        }

        SSL_CTX *pSSLCTX = (SSL_CTX*)pPriv->pSSLCTX;
        if (pSSLCTX != NULL) SSL_CTX_free(pSSLCTX);

        free(pSock->pPrivate);
        pSock->pPrivate = NULL;
    }
#endif

    if (pSock && pSock->nFD != XSOCK_INVALID)
    {
        shutdown(pSock->nFD, XSHUT_RDWR);
        xclosesock(pSock->nFD);
        pSock->nFD = XSOCK_INVALID;
    }
}

int XSock_SSLRead(xsock_t *pSock, void *pData, size_t nSize, xbool_t nExact)
{
    if (!XSock_Check(pSock)) return XSOCK_ERROR;
    XASSERT_RET((nSize && pData), XSOCK_NONE);

#ifdef XSOCK_USE_SSL
    SSL *pSSL = XSock_GetSSL(pSock);
    if (pSSL == NULL)
    {
        pSock->eStatus = XSOCK_ERR_SSLINV;
        XSock_Close(pSock);
        return XSOCK_ERROR;
    }

    uint8_t *pBuff = (uint8_t*)pData;
    int nLeft = (int)nSize;
    int nReceived = 0;

    while (nLeft > 0 && (nExact || !nReceived || SSL_pending(pSSL)))
    {
        int nBytes = SSL_read(pSSL, &pBuff[nReceived], nLeft);
        if (nBytes <= 0)
        {
            int nError = SSL_get_error(pSSL, nBytes);
            pSock->eStatus = XSOCK_ERR_SSLREAD;

            if (nError == SSL_ERROR_ZERO_RETURN)
            {
                pSock->eStatus = XSOCK_EOF;
                XSock_SSLConnected(pSock, XFALSE);
            }
            else if (nError == SSL_ERROR_SYSCALL)
            {
                if (!nBytes) pSock->eStatus = XSOCK_EOF;
                XSock_SSLConnected(pSock, XFALSE);
            }
            else if (nError == SSL_ERROR_SSL)
            {
                pSock->eStatus = XSOCK_ERR_SSLERR;
                XSock_SSLConnected(pSock, XFALSE);
            }
            else if (nError == SSL_ERROR_WANT_READ)
            {
                pSock->eStatus = XSOCK_WANT_READ;
                if (!XSock_IsNB(pSock)) continue;
                break;
            }
            else if (nError == SSL_ERROR_WANT_WRITE)
            {
                pSock->eStatus = XSOCK_WANT_WRITE;
                break;
            }

            if (pSock->eStatus != XSOCK_EOF)
                nReceived = XSOCK_ERROR;

            XSock_Close(pSock);
            return nReceived;
        }

        nReceived += nBytes;
        nLeft -= nBytes;

        /* Wait for read event if non-blocking, but drain SSL internal buffer first */
        if (XSock_IsNB(pSock) && !SSL_pending(pSSL)) break;
    }

    return nReceived;
#else
    (void)nExact;
    pSock->eStatus = XSOCK_ERR_NOSSL;
    XSock_Close(pSock);
    return XSOCK_ERROR;
#endif
}

int XSock_SSLWrite(xsock_t *pSock, const void *pData, size_t nLength)
{
    if (!XSock_Check(pSock)) return XSOCK_ERROR;
    if (!nLength || pData == NULL) return XSOCK_NONE;

#ifdef XSOCK_USE_SSL
    SSL *pSSL = XSock_GetSSL(pSock);
    if (pSSL == NULL)
    {
        pSock->eStatus = XSOCK_ERR_SSLINV;
        XSock_Close(pSock);
        return XSOCK_ERROR;
    }

    uint8_t *pBuff = (uint8_t*)pData;
    ssize_t nLeft = nLength;
    size_t nSent = 0;

    while (nLeft > 0)
    {
        int nBytes = SSL_write(pSSL, &pBuff[nSent], (int)nLeft);
        if (nBytes <= 0)
        {
            int nError = SSL_get_error(pSSL, nBytes);
            pSock->eStatus = XSOCK_ERR_SSLWRITE;

            if (nError == SSL_ERROR_WANT_READ)
            {
                pSock->eStatus = XSOCK_WANT_READ;
                break;
            }
            else if (nError == SSL_ERROR_WANT_WRITE)
            {
                pSock->eStatus = XSOCK_WANT_WRITE;
                if (!XSock_IsNB(pSock)) continue;
                break;
            }
            else if (nError == SSL_ERROR_SSL ||
                     nError == SSL_ERROR_SYSCALL)
            {
                pSock->eStatus = XSOCK_ERR_SYSCALL;
                XSock_SSLConnected(pSock, XFALSE);
            }

            XSock_Close(pSock);
            return nBytes;
        }

        nSent += nBytes;
        nLeft -= nBytes;

        /* Wait for write event if non-blocking */
        if (XSock_IsNB(pSock)) break;
    }

    return (int)nSent;
#else
    pSock->eStatus = XSOCK_ERR_NOSSL;
    XSock_Close(pSock);
    return XSOCK_ERROR;
#endif
}

int XSock_RecvChunk(xsock_t *pSock, void* pData, size_t nSize)
{
    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSL))
        return XSock_SSLRead(pSock, pData, nSize, XTRUE);

    if (!XSock_Check(pSock)) return XSOCK_ERROR;
    if (!nSize || pData == NULL) return XSOCK_NONE;

    uint8_t* pBuff = (uint8_t*)pData;
    int nReceived = 0;

    while((size_t)nReceived < nSize)
    {
        int nChunk = XSOCK_MIN((int)nSize - nReceived, XSOCK_CHUNK_MAX);
        int nRecvSize = recv(pSock->nFD, &pBuff[nReceived], nChunk, XMSG_NOSIGNAL);

        if (nRecvSize <= 0)
        {
            pSock->eStatus = XSOCK_EOF;
            XSock_Close(pSock);

            if (nRecvSize < 0)
            {
                pSock->eStatus = XSOCK_ERR_RECV;
                nReceived = XSOCK_ERROR;
            }

            return nReceived;
        }

        nReceived += nRecvSize;
    }

    return nReceived;
}

int XSock_Recv(xsock_t *pSock, void* pData, size_t nSize)
{
    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSL))
        return XSock_SSLRead(pSock, pData, nSize, XFALSE);

    if (!XSock_Check(pSock)) return XSOCK_ERROR;
    if (!nSize || pData == NULL) return XSOCK_NONE;

    int nRecvSize = 0;
    xsockaddr_t* pSockAddr = XSock_GetSockAddr(pSock);
    xsocklen_t nSockAddrLen = XSock_GetAddrLen(pSock);

    if (pSock->nType != SOCK_DGRAM) nRecvSize = recv(pSock->nFD, pData, (int)nSize, XMSG_NOSIGNAL);
    else nRecvSize = recvfrom(pSock->nFD, pData, (int)nSize, 0, pSockAddr, &nSockAddrLen);

    if (nRecvSize <= 0)
    {
        if (!nRecvSize) pSock->eStatus = XSOCK_EOF;
        else pSock->eStatus = XSOCK_ERR_RECV;
        XSock_Close(pSock);
    }

    return nRecvSize;
}

int XSock_SendChunk(xsock_t *pSock, void *pData, size_t nLength)
{
    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSL))
        return XSock_SSLWrite(pSock, pData, nLength);

    if (!XSock_Check(pSock)) return XSOCK_ERROR;
    if (!nLength || pData == NULL) return XSOCK_NONE;

    uint8_t* pBuff =(uint8_t*)pData;
    int nDone = 0;

    while((size_t)nDone < nLength)
    {
        int nChunk = XSOCK_MIN((int)nLength - nDone, XSOCK_CHUNK_MAX);
        int nSent = send(pSock->nFD, &pBuff[nDone], nChunk, XMSG_NOSIGNAL);

        if (nSent <= 0)
        {
            pSock->eStatus = XSOCK_ERR_SEND;
            XSock_Close(pSock);
            return nSent;
        }

        nDone += nSent;
    }

    return nDone;
}

int XSock_Send(xsock_t *pSock, const void *pData, size_t nLength)
{
    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSL))
        return XSock_SSLWrite(pSock, pData, nLength);

    if (!XSock_Check(pSock)) return XSOCK_ERROR;
    if (!nLength || pData == NULL) return XSOCK_NONE;

    xsockaddr_t* pSockAddr = XSock_GetSockAddr(pSock);
    xsocklen_t nAddrLen = XSock_GetAddrLen(pSock);
    int nSent = 0;

    if (pSock->nType != SOCK_DGRAM) nSent = send(pSock->nFD, pData, (int)nLength, XMSG_NOSIGNAL);
    else nSent = sendto(pSock->nFD, pData, (int)nLength, XMSG_NOSIGNAL, pSockAddr, nAddrLen);

    if (nSent <= 0)
    {
        pSock->eStatus = XSOCK_ERR_SEND;
        XSock_Close(pSock);
    }

    return nSent;
}

int XSock_Read(xsock_t *pSock, void *pData, size_t nSize)
{
    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSL))
        return XSock_SSLRead(pSock, pData, nSize, XFALSE);

    else if (!XSock_Check(pSock)) return XSOCK_ERROR;
    else if (!nSize || pData == NULL) return XSOCK_NONE;
    int nReadSize = 0;

#ifdef _WIN32
    (void)nReadSize;
    return XSock_Recv(pSock, pData, nSize);
#elif EINTR
    do nReadSize = read(pSock->nFD, pData, nSize);
    while (nReadSize < 0 && errno == EINTR);
#else
    nReadSize = read(pSock->nFD, pData, nSize);
#endif

    if (nReadSize <= 0)
    {
        if (!nReadSize) pSock->eStatus = XSOCK_EOF;
        else pSock->eStatus = XSOCK_ERR_READ;
        XSock_Close(pSock);
    }

    return nReadSize;
}

int XSock_Write(xsock_t *pSock, const void *pData, size_t nLength)
{
    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSL))
        return XSock_SSLWrite(pSock, pData, nLength);

    else if (!XSock_Check(pSock)) return XSOCK_INVALID;
    else if (!nLength || pData == NULL) return XSOCK_NONE;
    int nBytes = 0;

#ifdef _WIN32
    nBytes = XSock_Send(pSock, pData, nLength);
#else
    nBytes = write(pSock->nFD, pData, nLength);
    if (nBytes <= 0)
    {
        pSock->eStatus = XSOCK_ERR_WRITE;
        XSock_Close(pSock);
    }
#endif

    return nBytes;
}

int XSock_WriteBuff(xsock_t *pSock, xbyte_buffer_t *pBuffer)
{
    if (pBuffer == NULL) return XSOCK_NONE;
    return XSock_Write(pSock, pBuffer->pData, pBuffer->nUsed);
}

int XSock_SendBuff(xsock_t *pSock, xbyte_buffer_t *pBuffer)
{
    if (pBuffer == NULL) return XSOCK_NONE;
    return XSock_Send(pSock, pBuffer->pData, pBuffer->nUsed);
}

XSOCKET XSock_Accept(xsock_t *pSock, xsock_t *pNewSock)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;

    uint32_t nFlags = pSock->nFlags;
    XFLAGS_DISABLE(nFlags, XSOCK_SERVER);
    XFLAGS_DISABLE(nFlags, XSOCK_NB);
    XFLAGS_ENABLE(nFlags, XSOCK_PEER);

    XSTATUS nStatus = XSock_Init(pNewSock, nFlags, XSOCK_INVALID);
    if (nStatus < 0) return XSOCK_INVALID;

    xsockaddr_t* pSockAddr = XSock_GetSockAddr(pNewSock);
    xsocklen_t nAddrLen = XSock_GetAddrLen(pNewSock);

    pNewSock->nFD = accept(pSock->nFD, pSockAddr, &nAddrLen);
    if (pNewSock->nFD == XSOCK_INVALID)
    {
        pSock->eStatus = XSOCK_ERR_ACCEPT;
        XSock_Close(pNewSock);
        return XSOCK_INVALID;
    }

#ifdef XSOCK_USE_SSL
    SSL_CTX* pSSLCtx = XSock_GetSSLCTX(pSock);
    if (XSock_IsSSL(pSock) && pSSLCtx != NULL)
    {
        SSL *pSSL = SSL_new(pSSLCtx);
        if (pSSL == NULL)
        {
            XSock_Close(pNewSock);
            pSock->eStatus = XSOCK_ERR_SSLNEW;
            return XSOCK_INVALID;
        }

        SSL_set_accept_state(pSSL);
        SSL_set_fd(pSSL, (int)pNewSock->nFD);

        XSOCKET nFD = XSock_SetSSL(pNewSock, pSSL);
        XASSERT((nFD != XSOCK_INVALID), XSOCK_INVALID);

#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
        long nOpts = (long)SSL_get_options(pSSL);
        nOpts |= SSL_OP_IGNORE_UNEXPECTED_EOF;
        SSL_set_options(pSSL, nOpts);
#endif

        return XSock_SSLAccept(pNewSock);
    }
#endif

    return pNewSock->nFD;
}

XSOCKET XSock_AcceptNB(xsock_t *pSock)
{
#if defined(_XUTILS_USE_GNU) && defined(__linux__)
    if (!XSock_Check(pSock)) return XSOCK_INVALID;

    xsockaddr_t* pSockAddr = XSock_GetSockAddr(pSock);
    xsocklen_t nAddrLen = XSock_GetAddrLen(pSock);

    XSOCKET nFD = accept4(pSock->nFD, pSockAddr, &nAddrLen, 1);
    if (nFD < 0) pSock->eStatus = XSOCK_ERR_ACCEPT;

    return nFD;
#endif

    (void)pSock;
    return XSOCK_INVALID;
}

XSTATUS XSock_MsgPeek(xsock_t *pSock)
{
    if (!XSock_Check(pSock)) return XSOCK_ERROR;
    unsigned char buf;
    int nFlags = MSG_PEEK | XMSG_DONTWAIT;
    int nByte = recv(pSock->nFD, &buf, 1, nFlags);
    return nByte < 0 ? XSOCK_NONE : XSOCK_SUCCESS;
}

uint32_t XSock_NetAddr(const char *pAddr)
{
    if (pAddr == NULL) return htonl(INADDR_ANY);
    struct in_addr addr;
    int nStatus = inet_pton(AF_INET, pAddr, &addr);
    return (nStatus <= 0) ? 0 : (uint32_t)addr.s_addr;
}

size_t XSock_IPStr(const uint32_t nAddr, char *pStr, size_t nSize)
{
    return xstrncpyf(pStr, nSize, "%d.%d.%d.%d",
        (int)((nAddr & 0x000000FF)),
        (int)((nAddr & 0x0000FF00)>>8),
        (int)((nAddr & 0x00FF0000)>>16),
        (int)((nAddr & 0xFF000000)>>24));
}

size_t XSock_SinAddr(const struct in_addr inAddr, char *pAddr, size_t nSize)
{
    return XSock_IPStr(inAddr.s_addr, pAddr, nSize);
}

size_t XSock_IPAddr(const xsock_t *pSock, char *pAddr, size_t nSize)
{
    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_UNIX)) return XSOCK_NONE;
    const struct sockaddr_in *pInAddr = &pSock->sockAddr.inAddr;
    return XSock_SinAddr(pInAddr->sin_addr, pAddr, nSize);
}

XSTATUS XSock_AddrInfo(xsock_info_t *pAddr, xsock_family_t eFam, const char *pHost)
{
    struct addrinfo hints, *rp, *res = NULL;
    int nRetVal = XSOCK_ERROR;
    void *ptr = NULL;

    memset(&hints, 0, sizeof (hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = PF_UNSPEC;
    hints.ai_flags |= AI_CANONNAME;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    if (getaddrinfo(pHost, NULL, &hints, &res)) return nRetVal;
    pAddr->eFamily = XF_UNDEF;

    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        inet_ntop(res->ai_family, res->ai_addr->sa_data, pAddr->sAddr, sizeof(pAddr->sAddr));
        if (xstrused(pAddr->sAddr)) nRetVal = XSOCK_NONE;

        if (eFam == XF_IPV4 && rp->ai_family == AF_INET)
        {
            ptr = &((struct sockaddr_in*) res->ai_addr)->sin_addr;
            pAddr->eFamily = XF_IPV4;
        }
#ifndef _WIN32
        else if (eFam == XF_IPV6 && rp->ai_family == AF_INET6)
        {
            ptr = &((struct sockaddr_in6*)res->ai_addr)->sin6_addr;
            pAddr->eFamily = XF_IPV6;
        }
#endif

        if (ptr != NULL)
        {
            if (inet_ntop(res->ai_family, ptr, pAddr->sAddr, sizeof(pAddr->sAddr)) == NULL)
            {
                freeaddrinfo(res);
                return nRetVal;
            }

            xstrncpy(pAddr->sName, sizeof(pAddr->sName), res->ai_canonname);
            pAddr->nAddr = XSock_NetAddr(pAddr->sAddr);
            pAddr->nPort = 0;

            nRetVal = XSOCK_SUCCESS;
            break;
        }
    }

    freeaddrinfo(res);
    return nRetVal;
}

void XSock_InitInfo(xsock_info_t *pAddr)
{
    pAddr->sName[0] = XSTR_NUL;
    pAddr->sAddr[0] = XSTR_NUL;
    pAddr->nAddr = XSTDNON;
    pAddr->nPort = XSTDNON;
    pAddr->eFamily = XF_UNDEF;
}

XSTATUS XSock_GetAddrInfo(xsock_info_t *pAddr, const char *pHost)
{
    XSock_InitInfo(pAddr);
    if (pHost == NULL) return XSOCK_ERROR;

    char sHost[XSOCK_INFO_MAX + XSOCK_ADDR_MAX];
    xstrncpy(sHost, sizeof(sHost), pHost);

    char *savePtr = NULL;
    char *ptr = xstrtok(sHost, ":", &savePtr);
    if (ptr == NULL) return XSOCK_ERROR;
    xstrncpy(pAddr->sName, sizeof(pAddr->sName), ptr);

    int nStatus = XSock_AddrInfo(pAddr, XF_IPV4, ptr);
    if (nStatus <= 0) return XSOCK_ERROR;

    ptr = xstrtok(NULL, ":", &savePtr);
    if (ptr != NULL) pAddr->nPort = (uint16_t)atoi(ptr);
    return pAddr->nPort ? XSOCK_SUCCESS : XSOCK_NONE;
}

XSTATUS XSock_GetAddr(xsock_info_t *pInfo, struct sockaddr_in *pAddr, size_t nSize)
{
    XSock_InitInfo(pInfo);
    pInfo->eFamily = XF_IPV4;

    struct hostent *pHostInfo = gethostbyaddr((char*)&pAddr->sin_addr.s_addr, (int)nSize, AF_INET);
    if (pHostInfo != NULL) xstrncpy(pInfo->sName, sizeof(pInfo->sName), pHostInfo->h_name);

    XSock_IPStr(pAddr->sin_addr.s_addr, pInfo->sAddr, sizeof(pInfo->sAddr));
    return (pHostInfo != NULL) ? XSOCK_SUCCESS : XSOCK_NONE;
}

XSOCKET XSock_NonBlock(xsock_t *pSock, xbool_t nNonBlock)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;

#ifdef _WIN32
    unsigned long nOpt = (unsigned long)nNonBlock;
    int nRes = ioctlsocket(pSock->nFD, FIONBIO, &nOpt);
    if (nRes != NO_ERROR)
    {
        pSock->eStatus = XSOCK_ERR_SETFL;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }
#else
    /* Get flags */
    int fl = fcntl(pSock->nFD, F_GETFL);
    if (fl < 0)
    {
        pSock->eStatus = XSOCK_ERR_GETFL;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    if (nNonBlock)
    {
        /* Set flag */
        fl = fcntl(pSock->nFD, F_SETFL, fl | O_NONBLOCK);
        if (fl < 0)
        {
            pSock->eStatus = XSOCK_ERR_SETFL;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }
    }
    else
    {
        fl = fcntl(pSock->nFD, F_SETFL, fl & (~O_NONBLOCK));
        if (fl < 0)
        {
            pSock->eStatus = XSOCK_ERR_SETFL;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }
    }
#endif

    if (nNonBlock) XFLAGS_ENABLE(pSock->nFlags, XSOCK_NB);
    else XFLAGS_DISABLE(pSock->nFlags, XSOCK_NB);

    return pSock->nFD;
}

XSOCKET XSock_TimeOutR(xsock_t *pSock, int nSec, int nUsec)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;
    struct timeval tmout;
    tmout.tv_sec = nSec;
    tmout.tv_usec = nUsec;

#ifdef _WIN32
    if (setsockopt(pSock->nFD, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmout, sizeof(tmout)) < 0)
#else
    if (setsockopt(pSock->nFD, SOL_SOCKET, SO_RCVTIMEO, (struct timeval*)&tmout, sizeof(tmout)) < 0)
#endif
    {
        pSock->eStatus = XSOCK_ERR_SETOPT;
        XSock_Close(pSock);
    }

    return pSock->nFD;
}

XSOCKET XSock_TimeOutS(xsock_t *pSock, int nSec, int nUsec)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;
    struct timeval tmout;
    tmout.tv_sec = nSec;
    tmout.tv_usec = nUsec;

#ifdef _WIN32
    if (setsockopt(pSock->nFD, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tmout, sizeof(tmout)) < 0)
#else
    if (setsockopt(pSock->nFD, SOL_SOCKET, SO_SNDTIMEO, (struct timeval*)&tmout, sizeof(tmout)) < 0)
#endif
    {
        pSock->eStatus = XSOCK_ERR_SETOPT;
        XSock_Close(pSock);
    }

    return pSock->nFD;
}

XSOCKET XSock_ReuseAddr(xsock_t *pSock, xbool_t nEnabled)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;
    unsigned int nOpt = (unsigned int)nEnabled;

    if (setsockopt(pSock->nFD, SOL_SOCKET, SO_REUSEADDR, (char*)&nOpt, sizeof(nOpt)) < 0)
    {
        pSock->eStatus = XSOCK_ERR_SETOPT;
        XSock_Close(pSock);
    }

    return pSock->nFD;
}

XSOCKET XSock_Linger(xsock_t *pSock, int nSec)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;
    struct linger lopt;
    lopt.l_linger = nSec;
    lopt.l_onoff = 1;

#ifdef _WIN32
    if (setsockopt(pSock->nFD, SOL_SOCKET, SO_LINGER, (const char*)&lopt, sizeof(lopt)) < 0)
#else
    if (setsockopt(pSock->nFD, SOL_SOCKET, SO_LINGER, (struct linger*)&lopt, sizeof(lopt)) < 0)
#endif
    {
        pSock->eStatus = XSOCK_ERR_SETOPT;
        XSock_Close(pSock);
    }

    return pSock->nFD;
}

XSOCKET XSock_Oobinline(xsock_t *pSock, xbool_t nEnabled)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;
    unsigned int nOpt = (unsigned int)nEnabled;

    if (setsockopt(pSock->nFD, SOL_SOCKET, SO_OOBINLINE, (char*)&nOpt, sizeof nOpt) < 0)
    {
        pSock->eStatus = XSOCK_ERR_SETOPT;
        XSock_Close(pSock);
    }

    return pSock->nFD;
}

XSOCKET XSock_NoDelay(xsock_t *pSock, xbool_t nEnabled)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;
    unsigned int nOpt = (unsigned int)nEnabled;

    if (setsockopt(pSock->nFD, pSock->nProto, TCP_NODELAY, (char*)&nOpt, sizeof(nOpt)) < 0)
    {
        pSock->eStatus = XSOCK_ERR_SETOPT;
        XSock_Close(pSock);
    }

    return pSock->nFD;
}

XSOCKET XSock_Bind(xsock_t *pSock)
{
    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_UNIX | XSOCK_FORCE))
    {
        const char *pPath = pSock->sockAddr.unAddr.sun_path;
        if (XPath_Exists(pPath)) XPath_Remove(pPath);
    }

    xsockaddr_t* pSockAddr = XSock_GetSockAddr(pSock);
    xsocklen_t nAddrLen = XSock_GetAddrLen(pSock);

    if (bind(pSock->nFD, pSockAddr, nAddrLen) < 0)
    {
        pSock->eStatus = XSOCK_ERR_BIND;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    return pSock->nFD;
}

XSOCKET XSock_AddMembership(xsock_t* pSock, const char* pGroup)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;

    struct ip_mreq mreq;
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    mreq.imr_multiaddr.s_addr = XSock_NetAddr(pGroup);

    /* Join to multicast group */
#ifdef _WIN32
    if (setsockopt(pSock->nFD, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq)) < 0)
#else
    if (setsockopt(pSock->nFD, IPPROTO_IP, IP_ADD_MEMBERSHIP, (struct ip_mreq*)&mreq, sizeof(mreq)) < 0)
#endif
    {
        pSock->eStatus = XSOCK_ERR_SETOPT;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    return pSock->nFD;
}

XSTATUS XSock_LoadPKCS12(xsock_ssl_cert_t *pCert, const char *p12Path, const char *p12Pass)
{
    pCert->nStatus = 0;
#ifdef XSOCK_USE_SSL
    FILE *p12File = fopen(p12Path, "rb");
    if (p12File == NULL) return XSOCK_ERROR;

    PKCS12 *p12 = d2i_PKCS12_fp(p12File, NULL);
    fclose(p12File);
    if (p12 == NULL) return XSOCK_ERROR;

    STACK_OF(X509) *pCa = NULL;
    EVP_PKEY *pKey = NULL;
    X509 *pXCert = NULL;

    if (!PKCS12_parse(p12, p12Pass, &pKey, &pXCert, &pCa))
    {
        PKCS12_free(p12);
        return XSOCK_ERROR;
    }

    pCert->pCert = pXCert;
    pCert->pKey = pKey;
    pCert->pCa = pCa;

    PKCS12_free(p12);
    pCert->nStatus = 1;
    return XSOCK_SUCCESS;
#else
    (void)p12Path;
    (void)p12Pass;
#endif

    return XSOCK_NONE;
}

void XSock_InitCert(xsock_cert_t *pCert)
{
    pCert->pCertPath = NULL;
    pCert->pKeyPath = NULL;
    pCert->pCaPath = NULL;
    pCert->p12Path = NULL;
    pCert->p12Pass = NULL;
    pCert->nVerifyFlags = 0;
}

XSOCKET XSock_SetSSLCert(xsock_t *pSock, xsock_cert_t *pCert)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;

#ifdef XSOCK_USE_SSL
    SSL_CTX *pSSLCtx = XSock_GetSSLCTX(pSock);
    if (pSSLCtx == NULL)
    {
        pSock->eStatus = XSOCK_ERR_SSLINV;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    SSL_CTX_set_ecdh_auto(pSSLCtx, 1);
    if (pCert->nVerifyFlags > 0) SSL_CTX_set_verify(pSSLCtx, pCert->nVerifyFlags, NULL);

    if (xstrused(pCert->pCaPath))
    {
        if (SSL_CTX_load_verify_locations(pSSLCtx, pCert->pCaPath, NULL) <= 0)
        {
            pSock->eStatus = XSOCK_ERR_SSLCA;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        SSL_CTX_set_client_CA_list(pSSLCtx, SSL_load_client_CA_file(pCert->pCaPath));
    }

    if (xstrused(pCert->p12Path))
    {
        xsock_ssl_cert_t sslCert;
        if (!XSock_LoadPKCS12(&sslCert, pCert->p12Path, pCert->p12Pass))
        {
            pSock->eStatus = XSOCK_ERR_PKCS12;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        EVP_PKEY *pKey = (EVP_PKEY *)sslCert.pKey;
        X509 *pXCert = (X509*)sslCert.pCert;

        if (pXCert != NULL && SSL_CTX_use_certificate(pSSLCtx, pXCert) <= 0)
        {
            pSock->eStatus = XSOCK_ERR_SSLCRT;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        if (pKey != NULL && SSL_CTX_use_PrivateKey(pSSLCtx, pKey) <= 0)
        {
            pSock->eStatus = XSOCK_ERR_SSLKEY;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }
    }
    else
    {
        if (xstrused(pCert->pCertPath) && SSL_CTX_use_certificate_file(pSSLCtx, pCert->pCertPath, SSL_FILETYPE_PEM) <= 0)
        {
            pSock->eStatus = XSOCK_ERR_SSLCRT;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        if (xstrused(pCert->pKeyPath) && SSL_CTX_use_PrivateKey_file(pSSLCtx, pCert->pKeyPath, SSL_FILETYPE_PEM) <= 0)
        {
            pSock->eStatus = XSOCK_ERR_SSLKEY;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        if (xstrused(pCert->pCaPath) && SSL_CTX_use_certificate_chain_file(pSSLCtx, pCert->pCaPath) <= 0)
        {
            pSock->eStatus = XSOCK_ERR_SSLCA;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }
    }

    return pSock->nFD;
#else
    (void)pCert;
#endif

    pSock->eStatus = XSOCK_ERR_NOSSL;
    XSock_Close(pSock);
    return XSOCK_INVALID;
}

XSOCKET XSock_SSLConnect(xsock_t *pSock)
{
#ifdef XSOCK_USE_SSL
    SSL *pSSL = XSock_GetSSL(pSock);
    if (pSSL == NULL)
    {
        pSock->eStatus = XSOCK_ERR_INVSSL;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    int nStatus = SSL_connect(pSSL);
    if (nStatus <= 0)
    {
        if (XSock_IsNB(pSock))
        {
            int nError = SSL_get_error(pSSL, nStatus);
            if (nError == SSL_ERROR_WANT_READ)
            {
                pSock->eStatus = XSOCK_WANT_READ;
                return pSock->nFD;
            }
            else if (nError == SSL_ERROR_WANT_WRITE)
            {
                pSock->eStatus = XSOCK_WANT_WRITE;
                return pSock->nFD;
            }
        }

        pSock->eStatus = XSOCK_ERR_SSLCNT;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    XSock_SSLConnected(pSock, XTRUE);
    return pSock->nFD;
#endif

    pSock->eStatus = XSOCK_ERR_NOSSL;
    XSock_Close(pSock);
    return XSOCK_INVALID;
}

XSOCKET XSock_SSLAccept(xsock_t *pSock)
{
#ifdef XSOCK_USE_SSL
    SSL *pSSL = XSock_GetSSL(pSock);
    if (pSSL == NULL)
    {
        pSock->eStatus = XSOCK_ERR_INVSSL;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    int nStatus = SSL_accept(pSSL);
    if (nStatus <= 0)
    {
        if (XSock_IsNB(pSock))
        {
            int nError = SSL_get_error(pSSL, nStatus);
            if (nError == SSL_ERROR_WANT_READ)
            {
                pSock->eStatus = XSOCK_WANT_READ;
                return pSock->nFD;
            }
            else if (nError == SSL_ERROR_WANT_WRITE)
            {
                pSock->eStatus = XSOCK_WANT_WRITE;
                return pSock->nFD;
            }
        }

        pSock->eStatus = XSOCK_ERR_SSLACC;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    XSock_SSLConnected(pSock, XTRUE);
    return pSock->nFD;
#endif

    pSock->eStatus = XSOCK_ERR_NOSSL;
    XSock_Close(pSock);
    return XSOCK_INVALID;
}

XSOCKET XSock_InitSSLServer(xsock_t *pSock)
{
#ifdef XSOCK_USE_SSL
    const SSL_METHOD *pMethod = XSock_GetSSLMethod(pSock);
    if (pMethod == NULL)
    {
        pSock->eStatus = XSOCK_ERR_SSLMET;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    SSL_CTX *pSSLCtx = SSL_CTX_new(pMethod);
    if (pSSLCtx == NULL)
    {
        pSock->eStatus = XSOCK_ERR_SSLCTX;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    SSL_CTX_set_verify(pSSLCtx, SSL_VERIFY_NONE, NULL);
    return XSock_SetSSLCTX(pSock, pSSLCtx);
#endif

    pSock->eStatus = XSOCK_ERR_NOSSL;
    XSock_Close(pSock);
    return XSOCK_INVALID;
}

XSOCKET XSock_InitSSLClient(xsock_t *pSock, const char *pAddr)
{
#ifdef XSOCK_USE_SSL
    const SSL_METHOD *pMethod = XSock_GetSSLMethod(pSock);
    if (pMethod == NULL)
    {
        pSock->eStatus = XSOCK_ERR_SSLMET;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    SSL_CTX *pSSLCtx = SSL_CTX_new(pMethod);
    if (pSSLCtx == NULL)
    {
        pSock->eStatus = XSOCK_ERR_SSLCTX;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    SSL_CTX_set_verify(pSSLCtx, SSL_VERIFY_NONE, NULL);
    SSL *pSSL = SSL_new(pSSLCtx);

    if (pSSL == NULL)
    {
        pSock->eStatus = XSOCK_ERR_SSLNEW;
        SSL_CTX_free(pSSLCtx);
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    SSL_set_connect_state(pSSL);
    SSL_set_fd(pSSL, (int)pSock->nFD);

#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
    if (xstrused(pAddr) && SSL_set_tlsext_host_name(pSSL, pAddr) != 1)
    {
        pSock->eStatus = XSOCK_ERR_SSLCNT;
        SSL_free(pSSL);
        SSL_CTX_free(pSSLCtx);
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }
#endif

#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
    long nOpts = (long)SSL_get_options(pSSL);
    nOpts |= SSL_OP_IGNORE_UNEXPECTED_EOF;
    SSL_set_options(pSSL, nOpts);
#endif

    XASSERT_CALL2((XSock_SetSSLCTX(pSock, pSSLCtx) >= 0),
        SSL_free, pSSL, SSL_CTX_free, pSSLCtx, XSOCK_INVALID);

    XASSERT_CALL2((XSock_SetSSL(pSock, pSSL) >= 0),
        SSL_free, pSSL, SSL_CTX_free, pSSLCtx, XSOCK_INVALID);

    return XSock_SSLConnect(pSock);
#endif

    pSock->eStatus = XSOCK_ERR_NOSSL;
    XSock_Close(pSock);
    return XSOCK_INVALID;
}

static XSOCKET XSock_SetupStream(xsock_t *pSock, const char *pAddr, size_t nFdMax)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;

    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SERVER))
    {
        if (XSock_Bind(pSock) == XSOCK_INVALID) return XSOCK_INVALID;
        nFdMax = XSTD_FIRSTOF(nFdMax, XSOCK_FD_MAX);

        if (listen(pSock->nFD, (int)nFdMax) < 0)
        {
            pSock->eStatus = XSOCK_ERR_LISTEN;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSL))
            XSock_InitSSLServer(pSock);
    }
    else if (XFLAGS_CHECK(pSock->nFlags, XSOCK_CLIENT))
    {
        xsockaddr_t* pSockAddr = XSock_GetSockAddr(pSock);
        xsocklen_t nAddrLen = XSock_GetAddrLen(pSock);

        if (connect(pSock->nFD, pSockAddr, nAddrLen) < 0)
        {
            pSock->eStatus = XSOCK_ERR_CONNECT;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        if (XFLAGS_CHECK(pSock->nFlags, XSOCK_SSL))
            XSock_InitSSLClient(pSock, pAddr);
    }

    return pSock->nFD;
}

static XSOCKET XSock_SetupDgram(xsock_t *pSock)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;
    int nEnabled = 1;

    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_CLIENT))
    {
        xsockaddr_t* pSockAddr = XSock_GetSockAddr(pSock);
        xsocklen_t nAddrLen = XSock_GetAddrLen(pSock);

        if (connect(pSock->nFD, pSockAddr, nAddrLen) < 0)
        {
            pSock->eStatus = XSOCK_ERR_CONNECT;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }
    }
    else if (XFLAGS_CHECK(pSock->nFlags, XSOCK_BROADCAST))
    {
        if (setsockopt(pSock->nFD, SOL_SOCKET, SO_BROADCAST, (char*)&nEnabled, sizeof nEnabled) < 0)
        {
            pSock->eStatus = XSOCK_ERR_SETOPT;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }
    }
    else if (XFLAGS_CHECK(pSock->nFlags, XSOCK_MULTICAST))
    {
        if (XSock_ReuseAddr(pSock, 1) == XSOCK_INVALID) return XSOCK_INVALID;
        else if (XSock_Bind(pSock) == XSOCK_INVALID) return XSOCK_INVALID;
        else if (XSock_AddMembership(pSock, NULL) == XSOCK_INVALID) return XSOCK_INVALID;
    }

    return pSock->nFD;
}

static int XSock_SetupAddr(xsock_t *pSock, const char *pAddr, uint16_t nPort)
{
    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_UNIX))
    {
        pSock->sockAddr.unAddr.sun_family = AF_UNIX;
        size_t nPathMax = sizeof(pSock->sockAddr.unAddr.sun_path);
        xstrncpy(pSock->sockAddr.unAddr.sun_path, nPathMax, pAddr);
    }
    else if (!XFLAGS_CHECK(pSock->nFlags, XSOCK_RAW))
    {
        xsock_info_t addrInfo;
        xbool_t bIsIPAddr;

        bIsIPAddr = XSock_NetAddr(pAddr) > 0 ? XTRUE : XFALSE;
        if (!bIsIPAddr && XSock_GetAddrInfo(&addrInfo, pAddr) < 0)
        {
            pSock->eStatus = XSOCK_ERR_ADDR;
            return XSTDERR;
        }

        const char *pAddrStr = bIsIPAddr ? pAddr : addrInfo.sAddr;
        pSock->nAddr = XSock_NetAddr(pAddrStr);
        pSock->nPort = nPort;

        pSock->sockAddr.inAddr.sin_addr.s_addr = pSock->nAddr;
        pSock->sockAddr.inAddr.sin_port = htons(pSock->nPort);
        pSock->sockAddr.inAddr.sin_family = AF_INET;
    }

    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_REUSEADDR))
        XSock_ReuseAddr(pSock, XTRUE);

    return XSTDOK;
}

XSOCKET XSock_CreateAdv(xsock_t *pSock, uint32_t nFlags, size_t nFdMax, const char *pAddr, uint16_t nPort)
{
    XSTATUS nStatus = XSock_Init(pSock, nFlags, XSOCK_INVALID);
    if (nStatus == XSOCK_ERROR) return XSOCK_INVALID;

    if (!xstrused(pAddr) || (!nPort && !XFLAGS_CHECK(nFlags, XSOCK_UNIX)))
    {
        pSock->eStatus = XSOCK_ERR_ARGS;
        pSock->nFD = XSOCK_INVALID;
        return XSOCK_INVALID;
    }

    int nType = pSock->nType;
#ifndef _WIN32
    nType |= FD_CLOEXEC;
#endif

    pSock->nFD = socket(pSock->nDomain, nType, pSock->nProto);
    if (pSock->nFD == XSOCK_INVALID)
    {
        pSock->eStatus = XSOCK_ERR_CREATE;
        return XSOCK_INVALID;
    }

    if (XSock_SetupAddr(pSock, pAddr, nPort) < 0)
    {
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    if (pSock->nType == SOCK_STREAM) XSock_SetupStream(pSock, pAddr, nFdMax);
    else if (pSock->nType == SOCK_DGRAM) XSock_SetupDgram(pSock);

    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_NB))
        return XSock_NonBlock(pSock, XTRUE);

    return pSock->nFD;
}

XSOCKET XSock_Create(xsock_t *pSock, uint32_t nFlags, const char *pAddr, uint16_t nPort)
{
    return XSock_CreateAdv(pSock, nFlags, 0, pAddr, nPort);
}

XSOCKET XSock_Open(xsock_t *pSock, uint32_t nFlags, xsock_info_t *pAddr)
{
    if (!xstrused(pAddr->sAddr) || (!pAddr->nPort && !XFLAGS_CHECK(nFlags, XSOCK_UNIX)))
    {
        pSock->eStatus = XSOCK_ERR_ARGS;
        pSock->nFD = XSOCK_INVALID;
        return XSOCK_INVALID;
    }

    return XSock_Create(pSock, nFlags, pAddr->sAddr, pAddr->nPort);
}

XSOCKET XSock_Setup(xsock_t *pSock, uint32_t nFlags, const char *pAddr)
{
    if (XFLAGS_CHECK(nFlags, XSOCK_UNIX))
        return XSock_Create(pSock, nFlags, pAddr, 0);

    xsock_info_t addrInfo;
    if (XSock_GetAddrInfo(&addrInfo, pAddr) <= 0)
    {
        pSock->eStatus = XSOCK_ERR_ADDR;
        pSock->nFD = XSOCK_INVALID;
        return XSOCK_INVALID;
    }

    return XSock_Open(pSock, nFlags, &addrInfo);
}

xsock_t* XSock_Alloc(uint32_t nFlags, const char *pAddr, uint16_t nPort)
{
    xsock_t *pSock = (xsock_t*)malloc(sizeof(xsock_t));
    if (!pSock) return NULL;

    XSock_Create(pSock, nFlags, pAddr, nPort);
    return pSock;
}

xsock_t* XSock_New(uint32_t nFlags, xsock_info_t *pAddr)
{
    if (XFLAGS_CHECK(nFlags, XSOCK_UNIX))
    {
        if (!xstrused(pAddr->sAddr)) return NULL;
        return XSock_Alloc(nFlags, pAddr->sAddr, 0);
    }

    if (!xstrused(pAddr->sAddr) || !pAddr->nPort) return NULL;
    return XSock_Alloc(nFlags, pAddr->sAddr, pAddr->nPort);
}

void XSock_Free(xsock_t *pSock)
{
    if (pSock != NULL)
    {
        XSock_Close(pSock);
        free(pSock);
    }
}
