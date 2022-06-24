/*!
 *  @file libxutils/src/sock.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Cross-plaform socket operations such as
 * create, bind, connect, listen, select and etc.
 */

#ifdef _XUTILS_USE_GNU
#define _GNU_SOURCE
#endif

#include "sock.h"
#include "xstr.h"
#include "sync.h"
#include "xtype.h"

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
static XATOMIC g_nSSLInit = 0; 

xsock_inaddr_t* XSock_InAddr(xsock_t *pSock) { return &pSock->inAddr; }
xsock_status_t XSock_Status(const xsock_t *pSock) { return pSock->eStatus; }
xsock_type_t XSock_GetType(const xsock_t *pSock) { return pSock->eType; }

XSOCKET XSock_GetFD(const xsock_t *pSock) { return pSock->nFD; }
xbool_t XSock_IsSSL(const xsock_t *pSock) { return (xbool_t)pSock->nSSL; }
xbool_t XSock_IsNB(const xsock_t *pSock) { return (xbool_t)pSock->nNB; }

uint32_t XSock_GetNetAddr(const xsock_t *pSock) { return pSock->nAddr; }
uint16_t XSock_GetPort(const xsock_t *pSock) { return pSock->nPort; }
size_t XSock_GetFDMax(const xsock_t *pSock) { return pSock->nFdMax; }
int XSock_GetSockType(const xsock_t *pSock) { return pSock->nType; }
int XSock_GetProto(const xsock_t *pSock) { return pSock->nProto; }

xbool_t XSockType_IsSSL(xsock_type_t eType)
{
    if (eType == XSOCK_SSL_PREFERED_CLIENT ||
        eType == XSOCK_SSL_PREFERED_SERVER ||
        eType == XSOCK_SSLV2_SERVER ||
        eType == XSOCK_SSLV3_SERVER ||
        eType == XSOCK_SSLV2_CLIENT ||
        eType == XSOCK_SSLV3_CLIENT ||
        eType == XSOCK_SSLV2_PEER ||
        eType == XSOCK_SSLV3_PEER)
            return XTRUE;

    return XFALSE;
}

#ifdef _XUTILS_USE_SSL
typedef struct XSocketPriv {
    xbool_t nShutdown;
    void *pSSLCTX;
    void *pSSL;
} xsock_priv_t;

static xsock_priv_t* XSock_AllocPriv()
{
    xsock_priv_t *pPriv = (xsock_priv_t*)malloc(sizeof(xsock_priv_t));
    if (pPriv == NULL) return NULL;

    pPriv->nShutdown = XFALSE;
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

    pPriv->pSSLCTX = pSSLCTX;
    pSock->nSSL = XTRUE;
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

    pPriv->nShutdown = XTRUE;
    pPriv->pSSL = pSSL;
    pSock->nSSL = XTRUE;
    return pSock->nFD;
}

static void XSock_SetShutdown(xsock_t *pSock, xbool_t nShutdown)
{
    xsock_priv_t *pPriv = (xsock_priv_t*)pSock->pPrivate;
    if (pPriv != NULL) pPriv->nShutdown = nShutdown;
}

static xsock_type_t XSock_GetPrefredSSL(xsock_type_t eType)
{
    if (eType == XSOCK_SSL_PREFERED_CLIENT)
    {
#ifdef SSLv3_client_method
        return XSOCK_SSLV3_CLIENT;
#else
        return XSOCK_SSLV2_CLIENT;
#endif
    }
    else if (eType == XSOCK_SSL_PREFERED_SERVER)
    {
#ifdef SSLv3_server_method
        return XSOCK_SSLV3_SERVER;
#else
        return XSOCK_SSLV2_SERVER;
#endif
    }

    return eType;
}

static const SSL_METHOD* XSock_GetSSLMethod(xsock_t *pSock)
{
    switch (pSock->eType)
    {
        case XSOCK_SSLV2_CLIENT:
#ifdef SSLv23_client_method
            return SSLv23_client_method();          
#else
            break;
#endif
        case XSOCK_SSLV2_SERVER:
#ifdef SSLv23_server_method
            return SSLv23_server_method();          
#else
            break;
#endif
        case XSOCK_SSLV3_CLIENT:
#ifdef SSLv3_client_method
            return SSLv3_client_method();          
#else
            break;
#endif
        case XSOCK_SSLV3_SERVER:
#ifdef SSLv3_server_method
            return SSLv3_server_method();          
#else
            break;
#endif
        default:
            break;
    }

    return NULL;
}
#endif

void XSock_InitSSL(void)
{
#ifdef _XUTILS_USE_SSL
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
#ifdef _XUTILS_USE_SSL
    if (!XSYNC_ATOMIC_GET(&g_nSSLInit)) return;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    ERR_free_strings();
#else
    EVP_PBE_cleanup();
#endif
    EVP_cleanup();
    CRYPTO_cleanup_all_ex_data();

    XSYNC_ATOMIC_SET(&g_nSSLInit, 0);
#endif
}

int XSock_LastSSLError(char *pDst, size_t nSize)
{
    if (pDst == NULL) return XSOCK_NONE;
    int nLength = 0;
    pDst[0] = XSTR_NUL;

#ifdef _XUTILS_USE_SSL
    BIO *pBIO = BIO_new(BIO_s_mem());
    ERR_print_errors(pBIO);

    char *pErrBuff = NULL;
    size_t nErrSize = BIO_get_mem_data(pBIO, &pErrBuff);
    if (!nErrSize || pErrBuff == NULL) return 0;

    nLength = nSize < nErrSize ?
        nSize - 1 : nErrSize - 1;

    strncpy(pDst, pErrBuff, nLength);
    pDst[nLength] = 0;
    BIO_free(pBIO);
#else
    (void)nSize;
#endif

    return nLength;
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
        case XSOCK_ERR_SSLMET:
            return "SSL method is not defined in the SSL library";
        case XSOCK_ERR_SSLERR:
            return "SSL_ERROR_SSL ocurred during SSL read or write";
        case XSOCK_ERR_ALLOC:
            return "Failed to allocate data for private SSL context";
        case XSOCK_ERR_ADDR:
            return "Failed get IP address from hostname";
        case XSOCK_EOF:
            return "Received final packet (FIN)";
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

XSTATUS XSock_SetType(xsock_t *pSock, xsock_type_t eType)
{
    pSock->eStatus = XSOCK_ERR_NONE;
    pSock->eType = eType;

    switch(eType)
    {
        case XSOCK_SSLV2_PEER:
        case XSOCK_SSLV3_PEER:
        case XSOCK_SSLV2_CLIENT:
        case XSOCK_SSLV3_CLIENT:
        case XSOCK_SSLV2_SERVER:
        case XSOCK_SSLV3_SERVER:
        case XSOCK_TCP_CLIENT:
        case XSOCK_TCP_SERVER:
        case XSOCK_TCP_PEER:
            pSock->nProto = IPPROTO_TCP;
            pSock->nType = SOCK_STREAM;
            break;
        case XSOCK_UDP_CLIENT:
        case XSOCK_UDP_BCAST:
        case XSOCK_UDP_MCAST:
        case XSOCK_UDP_UCAST:
            pSock->nProto = IPPROTO_UDP;
            pSock->nType = SOCK_DGRAM;
            break;
        case XSOCK_RAW:
            pSock->nProto = IPPROTO_TCP;
            pSock->nType = SOCK_RAW;
            break;
        case XSOCK_UNDEFINED:
        default:
            pSock->eStatus = XSOCK_ERR_SUPPORT;
            pSock->nProto = XSOCK_ERROR;
            pSock->nType = XSOCK_ERROR;
            return XSOCK_ERROR;
    }

    return XSOCK_SUCCESS;
}

XSTATUS XSock_Init(xsock_t *pSock, xsock_type_t eType, XSOCKET nFD, uint8_t nNB)
{
    memset(&pSock->inAddr, 0, sizeof(pSock->inAddr));
    pSock->pPrivate = NULL;
    pSock->nFdMax = 0;
    pSock->nAddr = 0;
    pSock->nPort = 0;
    pSock->nSSL = 0;
    pSock->nFD = nFD;
    pSock->nNB = nNB;

 #ifdef _XUTILS_USE_SSL
    eType = XSock_GetPrefredSSL(eType);

    if (XSockType_IsSSL(eType))
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

    return XSock_SetType(pSock, eType);
}

void XSock_Close(xsock_t *pSock)
{
 #ifdef _XUTILS_USE_SSL
    if (pSock->pPrivate != NULL)
    {
        xsock_priv_t *pPriv = (xsock_priv_t*)pSock->pPrivate;
        SSL *pSSL = (SSL*)pPriv->pSSL;

        if (pSSL != NULL)
        {
            if (pPriv->nShutdown)
                SSL_shutdown(pSSL);

            SSL_free(pSSL);
        }

        SSL_CTX *pSSLCTX = (SSL_CTX*)pPriv->pSSLCTX;
        if (pSSLCTX != NULL) SSL_CTX_free(pSSLCTX);

        free(pSock->pPrivate);
        pSock->pPrivate = NULL;
    }
#endif

    if (pSock->nFD != XSOCK_INVALID)
    {
        shutdown(pSock->nFD, XSHUT_RDWR);
        xclosesock(pSock->nFD);
        pSock->nFD = XSOCK_INVALID;
    }

    pSock->nSSL = 0;
}

int XSock_SSLRead(xsock_t *pSock, void *pData, size_t nSize, xbool_t nExact)
{
    if (!XSock_Check(pSock)) return XSOCK_ERROR;
    if (!nSize || pData == NULL) return XSOCK_NONE;

#ifdef _XUTILS_USE_SSL
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

    while ((nLeft > 0 && nExact) || !nReceived)
    {
        int nBytes = SSL_read(pSSL, &pBuff[nReceived], nLeft);
        if (nBytes <= 0)
        {
            int nError = SSL_get_error(pSSL, nBytes);
            pSock->eStatus = XSOCK_ERR_SSLREAD;

            if (nError == SSL_ERROR_WANT_READ) continue;
            else if (nError == SSL_ERROR_ZERO_RETURN)
            {
                XSock_SetShutdown(pSock, XFALSE);
                pSock->eStatus = XSOCK_EOF;
            }
            else if (nError == SSL_ERROR_SYSCALL)
            {
                XSock_SetShutdown(pSock, XFALSE);
                if (!nBytes) pSock->eStatus = XSOCK_EOF;
            }
            else if (nError == SSL_ERROR_SSL)
            {
                XSock_SetShutdown(pSock, XFALSE);
                pSock->eStatus = XSOCK_ERR_SSLERR;
            }

            if (pSock->eStatus != XSOCK_EOF)
                nReceived = XSOCK_ERROR;

            XSock_Close(pSock);
            return nReceived;
        }

        nReceived += nBytes;
        nLeft -= nBytes;
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

#ifdef _XUTILS_USE_SSL
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
        int nBytes = SSL_write(pSSL, &pBuff[nSent], nLeft);
        if (nBytes <= 0)
        {
            int nError = SSL_get_error(pSSL, nBytes);
            if (nError == SSL_ERROR_WANT_WRITE) continue;
            else if (nError == SSL_ERROR_SSL ||
                nError == SSL_ERROR_SYSCALL)
                XSock_SetShutdown(pSock, XFALSE);

            pSock->eStatus = XSOCK_ERR_SSLWRITE;
            XSock_Close(pSock);
            return nBytes;
        }

        nSent += nBytes;
        nLeft -= nBytes;
    }

    return nSent;
#else
    pSock->eStatus = XSOCK_ERR_NOSSL;
    XSock_Close(pSock);
    return XSOCK_ERROR;
#endif
}

int XSock_RecvChunk(xsock_t *pSock, void* pData, size_t nSize) 
{
    if (pSock->nSSL) return XSock_SSLRead(pSock, pData, nSize, XTRUE);
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
    if (pSock->nSSL) return XSock_SSLRead(pSock, pData, nSize, XFALSE);
    if (!XSock_Check(pSock)) return XSOCK_ERROR;
    if (!nSize || pData == NULL) return XSOCK_NONE;

    int nRecvSize = 0;
    struct sockaddr_in client;
    xsocklen_t slen = sizeof(client);

    if (pSock->nType != SOCK_DGRAM) nRecvSize = recv(pSock->nFD, pData, (int)nSize, XMSG_NOSIGNAL);
    else nRecvSize = recvfrom(pSock->nFD, pData, (int)nSize, 0, (struct sockaddr*)&client, &slen);

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
    if (pSock->nSSL) return XSock_SSLWrite(pSock, pData, nLength);
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
    if (pSock->nSSL) return XSock_SSLWrite(pSock, pData, nLength);
    if (!XSock_Check(pSock)) return XSOCK_ERROR;
    if (!nLength || pData == NULL) return XSOCK_NONE;
    int nSent = 0;

    if (pSock->nType != SOCK_DGRAM) nSent = send(pSock->nFD, pData, (int)nLength, XMSG_NOSIGNAL);
    else nSent = sendto(pSock->nFD, pData, (int)nLength, XMSG_NOSIGNAL, (struct sockaddr*)&pSock->inAddr, sizeof(pSock->inAddr));

    if (nSent <= 0)
    {
        pSock->eStatus = XSOCK_ERR_SEND;
        XSock_Close(pSock);
    }

    return nSent;
}

int XSock_Read(xsock_t *pSock, void *pData, size_t nSize)
{
    if (pSock->nSSL) return XSock_SSLRead(pSock, pData, nSize, XFALSE);
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
    if (pSock->nSSL) return XSock_SSLWrite(pSock, pData, nLength);
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
    xsocklen_t len = sizeof(pNewSock->inAddr);

    xsock_type_t eType = XSock_IsSSL(pSock) ?
        (pSock->eType == XSOCK_SSLV2_SERVER ?
        XSOCK_SSLV2_PEER : XSOCK_SSLV3_PEER) : pSock->eType;

    if (XSock_Init(pNewSock, eType, XSOCK_INVALID, XFALSE) < 0) return XSOCK_INVALID;
    pNewSock->nFD = accept(pSock->nFD, (struct sockaddr*)&pNewSock->inAddr, &len);

    if (pNewSock->nFD == XSOCK_INVALID)
    {
        pSock->eStatus = XSOCK_ERR_ACCEPT;
        XSock_Close(pNewSock);
        return XSOCK_INVALID;
    }

#ifdef _XUTILS_USE_SSL
    SSL_CTX* pSSLCtx = XSock_GetSSLCTX(pSock);
    if (pSock->nSSL && pSSLCtx != NULL)
    {
        SSL *pSSL = SSL_new(pSSLCtx);
        if (pSSL == NULL)
        {
            XSock_Close(pNewSock);
            pSock->eStatus = XSOCK_ERR_SSLNEW;
            return XSOCK_INVALID;
        }

        SSL_set_accept_state(pSSL);
        SSL_set_fd(pSSL, pNewSock->nFD);

        if (SSL_accept(pSSL) <= 0)
        {
            if (pSSL != NULL) SSL_free(pSSL);
            pSock->eStatus = XSOCK_ERR_SSLACC;

            XSock_Close(pNewSock);
            return XSOCK_INVALID;
        }

        return XSock_SetSSL(pNewSock, pSSL);
    }
#endif

    return pNewSock->nFD;
}

XSOCKET XSock_AcceptNB(xsock_t *pSock)
{
#ifdef _XUTILS_USE_GNU
    if (!XSock_Check(pSock)) return XSOCK_INVALID;
    xsocklen_t len = sizeof(struct sockaddr);
    pSock->nNB = 1;

    XSOCKET nFD = accept4(pSock->nFD, (struct sockaddr *) &pSock->inAddr, &len, pSock->nNB);
    if (nFD < 0) 
    {
        pSock->eStatus = XSOCK_ERR_ACCEPT;
        pSock->nFD = XSOCK_INVALID;
        pSock->nNB = 0;
    }

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

size_t XSock_IPAddr(xsock_t *pSock, char *pAddr, size_t nSize)
{ 
    struct sockaddr_in *pInAddr = &pSock->inAddr;
    return XSock_SinAddr(pInAddr->sin_addr, pAddr, nSize);
}

XSTATUS XSock_AddrInfo(xsock_addr_t *pAddr, xsock_family_t eFam, const char *pHost)
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
    xstrncpy(pAddr->sHost, sizeof(pAddr->sHost), pHost);
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

void XSock_InitAddr(xsock_addr_t *pAddr)
{
    pAddr->sHost[0] = XSTR_NUL;
    pAddr->sName[0] = XSTR_NUL;
    pAddr->sAddr[0] = XSTR_NUL;
    pAddr->nAddr = XSTDNON;
    pAddr->nPort = XSTDNON;
    pAddr->eFamily = XF_UNDEF;
}

XSTATUS XSock_GetAddr(xsock_addr_t *pAddr, const char *pHost)
{
    XSock_InitAddr(pAddr);
    if (pHost == NULL) return XSOCK_ERROR;

    char sHost[XSOCK_INFO_MAX + XSOCK_ADDR_MAX];
    xstrncpy(sHost, sizeof(sHost), pHost);

    char *savePtr = NULL;
    char *ptr = xstrtok(sHost, ":", &savePtr);
    if (ptr == NULL) return XSOCK_ERROR;

    int nStatus = XSock_AddrInfo(pAddr, XF_IPV4, ptr);
    if (nStatus <= 0) return XSOCK_ERROR;

    ptr = xstrtok(NULL, ":", &savePtr);
    if (ptr != NULL) pAddr->nPort = (uint16_t)atoi(ptr);
    return pAddr->nPort ? XSOCK_SUCCESS : XSOCK_NONE;
}

XSTATUS XSock_Addr(xsock_addr_t *pInfo, struct sockaddr_in *pAddr, size_t nSize)
{
    XSock_InitAddr(pInfo);
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

    pSock->nNB = nNonBlock;
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

    if (setsockopt(pSock->nFD, IPPROTO_TCP, TCP_NODELAY, (char*)&nOpt, sizeof(nOpt)) < 0)
    {
        pSock->eStatus = XSOCK_ERR_SETOPT;
        XSock_Close(pSock);
    }

    return pSock->nFD;
}

XSOCKET XSock_Bind(xsock_t *pSock)
{
    if (bind(pSock->nFD, (struct sockaddr*)&(pSock->inAddr), sizeof(pSock->inAddr)) < 0)
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

XSTATUS XSock_LoadPKCS12(xsocket_ssl_cert_t *pCert, const char *p12Path, const char *p12Pass)
{
    pCert->nStatus = 0;
#ifdef _XUTILS_USE_SSL
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

XSOCKET XSock_SetSSLCert(xsock_t *pSock, xsock_cert_t *pCert)
{
#ifdef _XUTILS_USE_SSL
    SSL_CTX *pSSLCtx = XSock_GetSSLCTX(pSock);
    if (pSSLCtx == NULL)
    {
        pSock->eStatus = XSOCK_ERR_SSLINV;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    SSL_CTX_set_ecdh_auto(pSSLCtx, 1);
    if (pCert->nVerifyFlags > 0) SSL_CTX_set_verify(pSSLCtx, pCert->nVerifyFlags, NULL);

    if (pCert->pCaPath != NULL)
    {
        if (SSL_CTX_load_verify_locations(pSSLCtx, pCert->pCaPath, NULL) <= 0)
        {
            pSock->eStatus = XSOCK_ERR_SSLCA;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        SSL_CTX_set_client_CA_list(pSSLCtx, SSL_load_client_CA_file(pCert->pCaPath));
    }

    if (pCert->p12Path != NULL)
    {
        xsocket_ssl_cert_t sslCert;
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
        if (pCert->pCertPath != NULL && SSL_CTX_use_certificate_file(pSSLCtx, pCert->pCertPath, SSL_FILETYPE_PEM) <= 0)
        {
            pSock->eStatus = XSOCK_ERR_SSLCRT;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        if (pCert->pKeyPath != NULL && SSL_CTX_use_PrivateKey_file(pSSLCtx, pCert->pKeyPath, SSL_FILETYPE_PEM) <= 0)
        {
            pSock->eStatus = XSOCK_ERR_SSLKEY;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        if (pCert->pCaPath != NULL && SSL_CTX_use_certificate_chain_file(pSSLCtx, pCert->pCaPath) <= 0)
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

XSOCKET XSock_InitSSLServer(xsock_t *pSock)
{
#ifdef _XUTILS_USE_SSL
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

XSOCKET XSock_InitSSLClient(xsock_t *pSock)
{
#ifdef _XUTILS_USE_SSL
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
    SSL_set_fd(pSSL, pSock->nFD);

    if (SSL_connect(pSSL) < 0)
    {
        if (pSSL != NULL) SSL_free(pSSL);
        pSock->eStatus = XSOCK_ERR_SSLCNT;

        SSL_CTX_free(pSSLCtx);
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    XSock_SetSSLCTX(pSock, pSSLCtx);
    XSock_SetSSL(pSock, pSSL);
    return pSock->nFD;
#endif

    pSock->eStatus = XSOCK_ERR_NOSSL;
    XSock_Close(pSock);
    return XSOCK_INVALID;
}

static XSOCKET XSock_SetupTCP(xsock_t *pSock)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;
    if (pSock->eType == XSOCK_SSLV2_SERVER ||
        pSock->eType == XSOCK_SSLV3_SERVER ||
        pSock->eType == XSOCK_TCP_SERVER)
    {
        if (XSock_Bind(pSock) == XSOCK_INVALID) return XSOCK_INVALID;

        if (listen(pSock->nFD, (int)pSock->nFdMax) < 0)
        {
            pSock->eStatus = XSOCK_ERR_LISTEN;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        if (pSock->eType == XSOCK_SSLV2_SERVER ||
            pSock->eType == XSOCK_SSLV3_SERVER)
                XSock_InitSSLServer(pSock);
    }
    else if (pSock->eType == XSOCK_SSLV2_CLIENT ||
             pSock->eType == XSOCK_SSLV3_CLIENT ||
             pSock->eType == XSOCK_TCP_CLIENT)
    {
        if (connect(pSock->nFD, (struct sockaddr *)&pSock->inAddr, sizeof(pSock->inAddr)) < 0)
        {
            pSock->eStatus = XSOCK_ERR_CONNECT;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        if (pSock->eType == XSOCK_SSLV2_CLIENT ||
            pSock->eType == XSOCK_SSLV3_CLIENT)
                XSock_InitSSLClient(pSock);
    }

    return pSock->nFD;
}

static XSOCKET XSock_SetupUDP(xsock_t *pSock)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;
    int nEnableFlag = 1;

    if (pSock->eType == XSOCK_UDP_BCAST)
    {
        if (setsockopt(pSock->nFD, SOL_SOCKET, SO_BROADCAST, (char*)&nEnableFlag, sizeof nEnableFlag) < 0)
        {
            pSock->eStatus = XSOCK_ERR_SETOPT;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }
    }
    else if (pSock->eType == XSOCK_UDP_CLIENT)
    {
        if (connect(pSock->nFD, (struct sockaddr *)&pSock->inAddr, sizeof(pSock->inAddr)) < 0)
        {
            pSock->eStatus = XSOCK_ERR_CONNECT;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }
    }
    else if (pSock->eType == XSOCK_UDP_MCAST)
    {
        if (XSock_ReuseAddr(pSock, 1) == XSOCK_INVALID) return XSOCK_INVALID;
        else if (XSock_Bind(pSock) == XSOCK_INVALID) return XSOCK_INVALID;
        else if (XSock_AddMembership(pSock, NULL) == XSOCK_INVALID) return XSOCK_INVALID;
    }

    return pSock->nFD;
}

XSOCKET XSock_CreateRAW(xsock_t *pSock)
{
    XSock_Init(pSock, XSOCK_RAW, XSOCK_INVALID, 0);
    pSock->nFD = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (pSock->nFD == XSOCK_INVALID) pSock->eStatus = XSOCK_ERR_CREATE;
    return pSock->nFD;
}

XSOCKET XSock_CreateAdv(xsock_t *pSock, xsock_type_t eType, size_t nFdMax, const char *pAddr, uint16_t nPort)
{
    int nStatus = XSock_Init(pSock, eType, XSOCK_INVALID, 0);
    if (nStatus == XSOCK_ERROR) return XSOCK_INVALID;

    if (pSock->eType != XSOCK_RAW)
    {
        pSock->nFdMax = XSTD_FIRSTOF(nFdMax, XSOCK_FD_MAX);
        pSock->nAddr = XSock_NetAddr(pAddr);
        pSock->nPort = nPort;

        pSock->inAddr.sin_addr.s_addr = pSock->nAddr;
        pSock->inAddr.sin_port = htons(pSock->nPort);
        pSock->inAddr.sin_family = AF_INET;
    }

    pSock->nFD = socket(AF_INET, pSock->nType | FD_CLOEXEC, pSock->nProto);
    if (pSock->nFD == XSOCK_INVALID)
    {
        pSock->eStatus = XSOCK_ERR_CREATE;
        return XSOCK_INVALID;
    }

    if (pSock->nType == SOCK_STREAM) XSock_SetupTCP(pSock);
    else if (pSock->nType == SOCK_DGRAM) XSock_SetupUDP(pSock);

    return pSock->nFD;
}

XSOCKET XSock_Create(xsock_t *pSock, xsock_type_t eType, const char *pAddr, uint16_t nPort)
{
    return XSock_CreateAdv(pSock, eType, 0, pAddr, nPort);
}

XSOCKET XSock_Open(xsock_t *pSock, xsock_type_t eType, xsock_addr_t *pAddr)
{
    if (pAddr->sAddr[0] == XSTR_NUL || !pAddr->nPort)
    {
        pSock->eStatus = XSOCK_ERR_CREATE;
        pSock->nFD = XSOCK_INVALID;
        return XSOCK_INVALID;
    }

    return XSock_Create(pSock, eType, pAddr->sAddr, pAddr->nPort);
}

XSOCKET XSock_Setup(xsock_t *pSock, xsock_type_t eType, const char *pAddr)
{
    xsock_addr_t addrInfo;

    if (XSock_GetAddr(&addrInfo, pAddr) <= 0)
    {
        pSock->eStatus = XSOCK_ERR_ADDR;
        pSock->nFD = XSOCK_INVALID;
        return XSOCK_INVALID;
    }

    return XSock_Open(pSock, eType, &addrInfo);
}

xsock_t* XSock_Alloc(xsock_type_t eType, const char *pAddr, uint16_t nPort)
{
    xsock_t *pSock = (xsock_t*)malloc(sizeof(xsock_t));
    if (!pSock) return NULL;

    XSock_Create(pSock, eType, pAddr, nPort);
    return pSock;
}

xsock_t* XSock_New(xsock_type_t eType, xsock_addr_t *pAddr)
{
    if (strlen(pAddr->sAddr) <= 0 || pAddr->nPort == 0) return NULL;
    return XSock_Alloc(eType, pAddr->sAddr, pAddr->nPort);
}

void XSock_Free(xsock_t *pSock)
{
    if (pSock != NULL)
    {
        XSock_Close(pSock);
        free(pSock);
    }
}
