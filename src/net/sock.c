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
#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif

#define XSOCK_MIN(a,b) (((a)<(b))?(a):(b))

/*
    Winsock does not report errors through errno: every failed socket call
    must be checked with WSAGetLastError() instead. XSOCK_ERRNO() abstracts
    that so the shared logic below stays identical on both platforms.
*/
#ifdef _WIN32
#define XSOCK_ERRNO() WSAGetLastError()
#define XSOCK_WOULDBLOCK(err) (err == WSAEWOULDBLOCK || err == WSAECONNABORTED)
#else
#define XSOCK_ERRNO() errno
#define XSOCK_WOULDBLOCK(err) (err == EAGAIN || err == EWOULDBLOCK || err == ECONNABORTED)
#endif

#ifdef _WIN32
/*
    Winsock requires a successful WSAStartup() call before any socket or
    name resolution function is used. It is invoked lazily (and exactly
    once, thread-safe) from every libxutils entry point that first touches
    the socket API. WSACleanup() is intentionally not called: the library
    keeps Winsock alive for the whole process lifetime and the OS releases
    it at process exit.
*/
static BOOL CALLBACK XSock_WinsockInitCb(PINIT_ONCE pInitOnce, PVOID pParam, PVOID *ppContext)
{
    (void)pInitOnce;
    (void)pParam;
    (void)ppContext;

    WSADATA wsaData;
    return (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) ? TRUE : FALSE;
}

xbool_t XSock_WinsockInit(void)
{
    static INIT_ONCE winsockInitOnce = INIT_ONCE_STATIC_INIT;
    return InitOnceExecuteOnce(&winsockInitOnce, XSock_WinsockInitCb, NULL, NULL) ? XTRUE : XFALSE;
}
#endif

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

#ifdef _WIN32
/*
    AF_UNIX stream sockets exist on Windows since 10 1803 (build 17134).
    Unlike the loopback TCP emulation, an AF_UNIX pair is not addressable
    from the network stack at all and its bind node lives in the calling
    user's private temp directory. There is still no socketpair(), so the
    listener/connect/accept dance remains, and the accepted endpoint is
    verified: SIO_AF_UNIX_GETPEERPID must report the current process,
    otherwise the pair is rejected. Any failure (pre-1803 system, temp
    path too deep for sun_path, hijacked accept) makes the caller fall
    back to the TCP emulation below.
*/
static XSTATUS XSock_CreatePairUnix(XSOCKET aPair[2])
{
    XSOCKET nListener = XSOCK_INVALID;
    XSOCKET nClient = XSOCK_INVALID;
    XSOCKET nAccepted = XSOCK_INVALID;

    struct sockaddr_un addr;
    char sTempDir[MAX_PATH];
    xbool_t bBound = XFALSE;
    int nAttempt = 0;

    nListener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (nListener == XSOCK_INVALID) return XSTDERR;

    DWORD nDirLen = GetTempPathA(sizeof(sTempDir), sTempDir);
    if (nDirLen == 0 || nDirLen >= sizeof(sTempDir))
    {
        closesocket(nListener);
        return XSTDERR;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    /* The name only needs to be unique, not secret: a squatted path makes
       bind() fail with WSAEADDRINUSE and a fresh suffix is tried. */
    for (nAttempt = 0; nAttempt < 16; nAttempt++)
    {
        LARGE_INTEGER perfCnt;
        QueryPerformanceCounter(&perfCnt);

        int nPathLen = snprintf(addr.sun_path, sizeof(addr.sun_path),
            "%sxsp-%lx-%llx-%x.sock", sTempDir,
            (unsigned long)GetCurrentProcessId(),
            (unsigned long long)perfCnt.QuadPart,
            (unsigned)nAttempt);

        /* Temp directory too deep for sun_path: AF_UNIX unusable here */
        if (nPathLen <= 0 || (size_t)nPathLen >= sizeof(addr.sun_path))
        {
            closesocket(nListener);
            return XSTDERR;
        }

        if (bind(nListener, (struct sockaddr*)&addr, (int)sizeof(addr)) == 0)
        {
            bBound = XTRUE;
            break;
        }

        if (WSAGetLastError() != WSAEADDRINUSE) break;
    }

    if (!bBound || listen(nListener, 1) != 0)
    {
        closesocket(nListener);
        if (bBound) DeleteFileA(addr.sun_path);
        return XSTDERR;
    }

    nClient = socket(AF_UNIX, SOCK_STREAM, 0);
    if (nClient == XSOCK_INVALID ||
        connect(nClient, (struct sockaddr*)&addr, (int)sizeof(addr)) != 0)
    {
        if (nClient != XSOCK_INVALID) closesocket(nClient);
        closesocket(nListener);
        DeleteFileA(addr.sun_path);
        return XSTDERR;
    }

    nAccepted = accept(nListener, NULL, NULL);
    closesocket(nListener);

    /* One-shot node: remove it before anyone else can dial it */
    DeleteFileA(addr.sun_path);

    if (nAccepted == XSOCK_INVALID)
    {
        closesocket(nClient);
        return XSTDERR;
    }

    /* Anti-hijack: the accepted endpoint must belong to this process */
    DWORD nPeerPid = 0;
    DWORD nIoBytes = 0;

    if (WSAIoctl(nAccepted, SIO_AF_UNIX_GETPEERPID, NULL, 0,
            &nPeerPid, sizeof(nPeerPid), &nIoBytes, NULL, NULL) != 0 ||
        nPeerPid != GetCurrentProcessId())
    {
        closesocket(nClient);
        closesocket(nAccepted);
        return XSTDERR;
    }

    /* Keep the pair private: never inherited by child processes */
    SetHandleInformation((HANDLE)nClient, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation((HANDLE)nAccepted, HANDLE_FLAG_INHERIT, 0);

    aPair[0] = nClient;
    aPair[1] = nAccepted;
    return XSTDOK;
}

/*
    Loopback TCP fallback for systems without AF_UNIX support. The pair
    must be safe against a local port-hijack race (another process
    connecting to our listener first), so after accept() the endpoints
    are cross-checked: the address the client socket was auto-bound to
    must be exactly the peer address of the accepted socket. On mismatch
    the pair is rejected.
*/
static XSTATUS XSock_CreatePairInet(XSOCKET aPair[2])
{
    XSOCKET nListener = XSOCK_INVALID;
    XSOCKET nClient = XSOCK_INVALID;
    XSOCKET nAccepted = XSOCK_INVALID;

    struct sockaddr_in listenAddr, clientAddr, peerAddr;
    int nAddrLen = (int)sizeof(struct sockaddr_in);
    int nOpt = 1;

    nListener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (nListener == XSOCK_INVALID) return XSTDERR;

    /* Refuse to share the port with any other local socket */
    setsockopt(nListener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&nOpt, sizeof(nOpt));

    memset(&listenAddr, 0, sizeof(listenAddr));
    listenAddr.sin_family = AF_INET;
    listenAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listenAddr.sin_port = 0;

    if (bind(nListener, (struct sockaddr*)&listenAddr, sizeof(listenAddr)) != 0 ||
        getsockname(nListener, (struct sockaddr*)&listenAddr, &nAddrLen) != 0 ||
        listen(nListener, 1) != 0)
    {
        closesocket(nListener);
        return XSTDERR;
    }

    nClient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (nClient == XSOCK_INVALID)
    {
        closesocket(nListener);
        return XSTDERR;
    }

    if (connect(nClient, (struct sockaddr*)&listenAddr, sizeof(listenAddr)) != 0)
    {
        closesocket(nListener);
        closesocket(nClient);
        return XSTDERR;
    }

    nAddrLen = (int)sizeof(clientAddr);
    if (getsockname(nClient, (struct sockaddr*)&clientAddr, &nAddrLen) != 0)
    {
        closesocket(nListener);
        closesocket(nClient);
        return XSTDERR;
    }

    nAddrLen = (int)sizeof(peerAddr);
    nAccepted = accept(nListener, (struct sockaddr*)&peerAddr, &nAddrLen);
    closesocket(nListener);

    if (nAccepted == XSOCK_INVALID)
    {
        closesocket(nClient);
        return XSTDERR;
    }

    /* Anti-hijack: the accepted peer must be our own client socket */
    if (peerAddr.sin_family != AF_INET ||
        peerAddr.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
        clientAddr.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
        peerAddr.sin_port != clientAddr.sin_port)
    {
        closesocket(nClient);
        closesocket(nAccepted);
        return XSTDERR;
    }

    /* Keep the pair private: never inherited by child processes */
    SetHandleInformation((HANDLE)nClient, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation((HANDLE)nAccepted, HANDLE_FLAG_INHERIT, 0);

    /* Notification channels carry single bytes: latency over batching */
    setsockopt(nClient, IPPROTO_TCP, TCP_NODELAY, (const char*)&nOpt, sizeof(nOpt));
    setsockopt(nAccepted, IPPROTO_TCP, TCP_NODELAY, (const char*)&nOpt, sizeof(nOpt));

    aPair[0] = nClient;
    aPair[1] = nAccepted;
    return XSTDOK;
}
#endif /* _WIN32 */

XSTATUS XSock_CreatePair(XSOCKET aPair[2])
{
    XCHECK_NL((aPair != NULL), XSTDERR);
    aPair[0] = aPair[1] = XSOCK_INVALID;

#ifndef _WIN32
    int nFds[2] = { XSOCK_INVALID, XSOCK_INVALID };

#if defined(SOCK_CLOEXEC)
    int nStatus = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, nFds);
#else
    int nStatus = socketpair(AF_UNIX, SOCK_STREAM, 0, nFds);
#endif
    XCHECK_NL((nStatus == 0), XSTDERR);

#if !defined(SOCK_CLOEXEC) && defined(FD_CLOEXEC)
    fcntl(nFds[0], F_SETFD, FD_CLOEXEC);
    fcntl(nFds[1], F_SETFD, FD_CLOEXEC);
#endif

    aPair[0] = nFds[0];
    aPair[1] = nFds[1];
    return XSTDOK;
#else
    XCHECK_NL(XSock_WinsockInit(), XSTDERR);

    /* AF_UNIX first: the pair is never addressable from the network
       stack. Falls back to the loopback TCP emulation on pre-1803
       systems or when the temp path does not fit in sun_path. */
    if (XSock_CreatePairUnix(aPair) == XSTDOK) return XSTDOK;

    aPair[0] = aPair[1] = XSOCK_INVALID;
    return XSock_CreatePairInet(aPair);
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
        case XSOCK_ERR_NAME:
            return "Can not rename the unix socket";
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

xbool_t XSock_IsSSLError(xsock_status_t eStatus)
{
    return (eStatus == XSOCK_ERR_SSLACC ||
            eStatus == XSOCK_ERR_SSLCNT ||
            eStatus == XSOCK_ERR_SSLREAD ||
            eStatus == XSOCK_ERR_SSLWRITE ||
            eStatus == XSOCK_ERR_SYSCALL ||
            eStatus == XSOCK_ERR_SSLERR ||
            eStatus == XSOCK_ERR_INVSSL ||
            eStatus == XSOCK_ERR_PKCS12 ||
            eStatus == XSOCK_ERR_SSLKEY ||
            eStatus == XSOCK_ERR_SSLCRT ||
            eStatus == XSOCK_ERR_SSLCA);
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
    XCHECK_NL((pSock != NULL), XSOCK_ERROR);

#ifdef _WIN32
    if (!XSock_WinsockInit())
    {
        pSock->eStatus = XSOCK_ERR_CREATE;
        pSock->nFD = XSOCK_INVALID;
        return XSOCK_ERROR;
    }
#endif

    memset(&pSock->sockAddr, 0, sizeof(pSock->sockAddr));

    pSock->sName[0] = XSTR_NUL;
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

#ifdef XSOCK_USE_SSL
/* Non-blocking senders queue outbound bytes in a growable buffer and retry the
   write when the socket becomes writable again. By default OpenSSL forbids
   exactly that: after SSL_write() reports WANT_WRITE, the retry must repeat the
   *identical* pointer and length, or the call fails with SSL_R_BAD_WRITE_RETRY
   and the connection is torn down. Since more data can be appended (and the
   buffer reallocated) between the two calls, that failure is a matter of
   timing, not of anything being wrong on the wire.

   ACCEPT_MOVING_WRITE_BUFFER permits the retry to point somewhere else as long
   as the pending prefix is unchanged, and ENABLE_PARTIAL_WRITE lets a large
   record drain in pieces rather than all-or-nothing. Together they make the
   buffered non-blocking write loop legal. */
static void XSock_ApplySSLModes(SSL *pSSL)
{
    if (pSSL == NULL) return;

    long nModes = 0;
#ifdef SSL_MODE_ENABLE_PARTIAL_WRITE
    nModes |= SSL_MODE_ENABLE_PARTIAL_WRITE;
#endif
#ifdef SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER
    nModes |= SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER;
#endif
    if (nModes) SSL_set_mode(pSSL, nModes);
}
#endif

size_t XSock_Pending(xsock_t *pSock)
{
#ifdef XSOCK_USE_SSL
    if (pSock == NULL || !XSock_IsSSL(pSock)) return XSTDNON;
    if (pSock->nFD == XSOCK_INVALID) return XSTDNON;

    SSL *pSSL = XSock_GetSSL(pSock);
    if (pSSL == NULL) return XSTDNON;

    int nPending = SSL_pending(pSSL);
    return nPending > 0 ? (size_t)nPending : XSTDNON;
#else
    (void)pSock;
    return XSTDNON;
#endif
}

int XSock_SSLRead(xsock_t *pSock, void *pData, size_t nSize, xbool_t nExact)
{
    if (!XSock_Check(pSock)) return XSOCK_ERROR;
    XCHECK_NL((nSize && pData), XSOCK_NONE);

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
            else if (nError == SSL_ERROR_SYSCALL)
            {
                pSock->eStatus = XSOCK_ERR_SYSCALL;
                XSock_SSLConnected(pSock, XFALSE);
            }
            else if (nError == SSL_ERROR_SSL)
            {
                pSock->eStatus = XSOCK_ERR_SSLERR;
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
        int nRecvSize = recv(pSock->nFD, (char*)&pBuff[nReceived], nChunk, XMSG_NOSIGNAL);

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
        int nSent = send(pSock->nFD, (const char*)&pBuff[nDone], nChunk, XMSG_NOSIGNAL);

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

    else if (!XSock_Check(pSock)) return XSOCK_ERROR;
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
        if (XSOCK_WOULDBLOCK(XSOCK_ERRNO())) pSock->eStatus = XSOCK_WANT_READ;
        else pSock->eStatus = XSOCK_ERR_ACCEPT;

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
        XSock_ApplySSLModes(pSSL);

        XSOCKET nFD = XSock_SetSSL(pNewSock, pSSL);
        XCHECK((nFD != XSOCK_INVALID), XSOCK_INVALID);

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
    if (nFD < 0)
    {
        if (XSOCK_WOULDBLOCK(XSOCK_ERRNO())) pSock->eStatus = XSOCK_WANT_READ;
        else pSock->eStatus = XSOCK_ERR_ACCEPT;
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
    int nByte = recv(pSock->nFD, (char*)&buf, 1, nFlags);
    return nByte < 0 ? XSOCK_NONE : XSOCK_SUCCESS;
}

uint32_t XSock_NetAddr(const char *pAddr)
{
#ifdef _WIN32
    if (!XSock_WinsockInit()) return 0;
#endif

    if (!xstrused(pAddr)) return htonl(INADDR_ANY);
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
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp = NULL;
    int nRetVal = XSOCK_ERROR;
    int nErr = 0;

    if (pAddr == NULL || !xstrused(pHost))
        return XSOCK_ERROR;

#ifdef _WIN32
    if (!XSock_WinsockInit()) return XSOCK_ERROR;
#endif

    memset(pAddr, 0, sizeof(*pAddr));
    pAddr->eFamily = XF_UNDEF;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    nErr = getaddrinfo(pHost, NULL, &hints, &res);
    if (nErr != 0) return XSOCK_ERROR;

    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        void *pRawAddr = NULL;

        if (eFam == XF_IPV4 && rp->ai_family == AF_INET)
        {
            struct sockaddr_in *pSin = (struct sockaddr_in *)rp->ai_addr;
            pRawAddr = &pSin->sin_addr;
            pAddr->eFamily = XF_IPV4;
        }
#ifndef _WIN32
        else if (eFam == XF_IPV6 && rp->ai_family == AF_INET6)
        {
            struct sockaddr_in6 *pSin6 = (struct sockaddr_in6 *)rp->ai_addr;
            pRawAddr = &pSin6->sin6_addr;
            pAddr->eFamily = XF_IPV6;
        }
#endif
        else
        {
            continue;
        }

        if (inet_ntop(rp->ai_family, pRawAddr, pAddr->sAddr, sizeof(pAddr->sAddr)) == NULL)
            continue;

        /* Bind the resolved record to the literal host the caller requested,
           not the DNS-supplied canonical name. Using ai_canonname here would let
           an attacker who can spoof DNS steer TLS hostname verification onto a
           name they hold a valid certificate for. */
        xstrncpy(pAddr->sName, sizeof(pAddr->sName), pHost);

        pAddr->nAddr = XSock_NetAddr(pAddr->sAddr);
        pAddr->nAddrLen = (xsocklen_t)rp->ai_addrlen;
        pAddr->nPort = 0;

        nRetVal = XSOCK_SUCCESS;
        break;
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
    pAddr->nAddrLen = XSTDNON;
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
    char sUnixTmpPath[sizeof(pSock->sockAddr.unAddr.sun_path)] = { 0 };
    char sUnixFinalPath[sizeof(pSock->sockAddr.unAddr.sun_path)] = { 0 };
    xbool_t bUnixAtomic = XFALSE;

    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_UNIX | XSOCK_FORCE))
    {
        const char *pPath = pSock->sockAddr.unAddr.sun_path;
        xstrncpy(sUnixFinalPath, sizeof(sUnixFinalPath), pPath);

#ifndef _WIN32
        xstrncpyf(sUnixTmpPath, sizeof(sUnixTmpPath), "%s.%d.tmp", pPath, (int)getpid());
#else
        xstrncpyf(sUnixTmpPath, sizeof(sUnixTmpPath), "%s.tmp", pPath);
#endif

        xstrncpy(pSock->sockAddr.unAddr.sun_path,
            sizeof(pSock->sockAddr.unAddr.sun_path),
            sUnixTmpPath);

        XPath_Remove(sUnixTmpPath);
        bUnixAtomic = XTRUE;
    }

    xsockaddr_t *pSockAddr = XSock_GetSockAddr(pSock);
    xsocklen_t nAddrLen = XSock_GetAddrLen(pSock);

    if (bind(pSock->nFD, pSockAddr, nAddrLen) < 0)
    {
        pSock->eStatus = XSOCK_ERR_BIND;
        if (bUnixAtomic) XPath_Remove(sUnixTmpPath);

        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    if (bUnixAtomic)
    {
        if (rename(sUnixTmpPath, sUnixFinalPath) < 0)
        {
            pSock->eStatus = XSOCK_ERR_NAME;
            XPath_Remove(sUnixTmpPath);
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }

        xstrncpy(pSock->sockAddr.unAddr.sun_path,
            sizeof(pSock->sockAddr.unAddr.sun_path),
            sUnixFinalPath);
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
    pCert->pHostName = NULL;
    pCert->p12Path = NULL;
    pCert->p12Pass = NULL;
    pCert->nVerifyFlags = 0;
}

XSOCKET XSock_SetSSLCert(xsock_t *pSock, xsock_cert_t *pCert)
{
    if (!XSock_Check(pSock)) return XSOCK_INVALID;

#ifdef XSOCK_USE_SSL
    SSL_CTX *pSSLCtx = XSock_GetSSLCTX(pSock);
    SSL *pSSL = XSock_GetSSL(pSock);
    if (pSSLCtx == NULL)
    {
        pSock->eStatus = XSOCK_ERR_SSLINV;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    (void)SSL_CTX_set_ecdh_auto(pSSLCtx, 1);
    if (pCert->nVerifyFlags > 0) SSL_CTX_set_verify(pSSLCtx, pCert->nVerifyFlags, NULL);

#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
    if (pSSL != NULL &&
        xstrused(pCert->pHostName) &&
        SSL_set_tlsext_host_name(pSSL, pCert->pHostName) != 1)
    {
        pSock->eStatus = XSOCK_ERR_SSLCNT;
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }
#endif

#if defined(X509_VERIFY_PARAM_set1_host)
    if (pSSL != NULL &&
        xstrused(pCert->pHostName) &&
        pCert->nVerifyFlags > 0)
    {
        X509_VERIFY_PARAM *pParam = SSL_get0_param(pSSL);
        if (pParam == NULL ||
            X509_VERIFY_PARAM_set1_host(pParam, pCert->pHostName, 0) != 1)
        {
            pSock->eStatus = XSOCK_ERR_SSLCNT;
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }
    }
#endif

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
        memset(&sslCert, 0, sizeof(xsock_ssl_cert_t));

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

        if (xstrused(pCert->pCaPath) &&
            (xstrused(pCert->pCertPath) || xstrused(pCert->pKeyPath)) &&
            SSL_CTX_use_certificate_chain_file(pSSLCtx, pCert->pCaPath) <= 0)
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

XSOCKET XSock_InitSSLServer(xsock_t *pSock, int nVerifyFlags)
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

    int nVerify = nVerifyFlags > XSTDNON ?
        nVerifyFlags : SSL_VERIFY_NONE;

    SSL_CTX_set_verify(pSSLCtx, nVerify, NULL);
    return XSock_SetSSLCTX(pSock, pSSLCtx);
#endif

    pSock->eStatus = XSOCK_ERR_NOSSL;
    XSock_Close(pSock);
    return XSOCK_INVALID;
}

#if defined(_WIN32) && defined(XSOCK_USE_SSL)
/*
    OpenSSL ships no trust anchors on Windows and its compiled-in
    OPENSSLDIR points at the build prefix, so set_default_verify_paths()
    loads an empty store and SSL_VERIFY_PEER rejects every connection.
    Import the system roots from the Windows certificate store instead,
    so verification uses the same trust the operating system itself does.
*/
static void XSock_LoadWinRootCerts(SSL_CTX *pSSLCtx)
{
    HCERTSTORE hStore = CertOpenSystemStoreA(0, "ROOT");
    if (hStore == NULL) return;

    X509_STORE *pStore = SSL_CTX_get_cert_store(pSSLCtx);
    PCCERT_CONTEXT pWinCert = NULL;

    if (pStore != NULL)
    {
        while ((pWinCert = CertEnumCertificatesInStore(hStore, pWinCert)) != NULL)
        {
            const unsigned char *pEncoded = pWinCert->pbCertEncoded;
            X509 *pCert = d2i_X509(NULL, &pEncoded, (long)pWinCert->cbCertEncoded);

            if (pCert != NULL)
            {
                X509_STORE_add_cert(pStore, pCert);
                X509_free(pCert);
            }
        }
    }

    CertCloseStore(hStore, 0);
}
#endif /* _WIN32 && XSOCK_USE_SSL */

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

#ifdef SSL_VERIFY_PEER
    /* Verify the server certificate chain against the system trust store.
       Guarded by SSL_VERIFY_PEER so prehistoric OpenSSL builds that lack the
       flag still compile, falling back to the old unverified behaviour. */
    SSL_CTX_set_default_verify_paths(pSSLCtx);
#ifdef _WIN32
    XSock_LoadWinRootCerts(pSSLCtx);
#endif
    SSL_CTX_set_verify(pSSLCtx, SSL_VERIFY_PEER, NULL);
#else
    SSL_CTX_set_verify(pSSLCtx, SSL_VERIFY_NONE, NULL);
#endif

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
    XSock_ApplySSLModes(pSSL);

#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
    if (xstrused(pAddr))
    {
        xbool_t bIsIPAddr = XSock_NetAddr(pAddr) > 0 ? XTRUE : XFALSE;
        if (!bIsIPAddr && SSL_set_tlsext_host_name(pSSL, pAddr) != 1)
        {
            pSock->eStatus = XSOCK_ERR_SSLCNT;
            SSL_free(pSSL);
            SSL_CTX_free(pSSLCtx);
            XSock_Close(pSock);
            return XSOCK_INVALID;
        }
    }
#endif

#if defined(SSL_VERIFY_PEER) && OPENSSL_VERSION_NUMBER >= 0x10002000L
    /* Bind verification to the host we actually dialed, so a certificate that
       is validly signed but issued for a different domain is still rejected.
       Without this, SSL_VERIFY_PEER alone does not stop a MITM that holds any
       CA-trusted cert. set1_host needs OpenSSL >= 1.0.2; older toolchains keep
       chain-only verification. */
    if (xstrused(pAddr))
    {
        X509_VERIFY_PARAM *pParam = SSL_get0_param(pSSL);
        if (pParam != NULL)
        {
            int nHostOk = (XSock_NetAddr(pAddr) > 0) ?
                X509_VERIFY_PARAM_set1_ip_asc(pParam, pAddr) :
                X509_VERIFY_PARAM_set1_host(pParam, pAddr, 0);

            if (nHostOk != 1)
            {
                pSock->eStatus = XSOCK_ERR_SSLCNT;
                SSL_free(pSSL);
                SSL_CTX_free(pSSLCtx);
                XSock_Close(pSock);
                return XSOCK_INVALID;
            }
        }
    }
#endif

#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
    long nOpts = (long)SSL_get_options(pSSL);
    nOpts |= SSL_OP_IGNORE_UNEXPECTED_EOF;
    SSL_set_options(pSSL, nOpts);
#endif

    XCHECK_CALL2((XSock_SetSSLCTX(pSock, pSSLCtx) >= 0),
        SSL_free, pSSL, SSL_CTX_free, pSSLCtx, XSOCK_INVALID);

    XCHECK_CALL2((XSock_SetSSL(pSock, pSSL) >= 0),
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
            XSock_InitSSLServer(pSock, XSTDNON);
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
            XSock_InitSSLClient(pSock, pSock->sName);
    }

    return pSock->nFD;
}

static XSOCKET XSock_SetupDgram(xsock_t *pSock, xbool_t bReuseAddr)
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
        if (!bReuseAddr && XSock_ReuseAddr(pSock, 1) == XSOCK_INVALID)
            return XSOCK_INVALID;

        if (XSock_Bind(pSock) == XSOCK_INVALID)
            return XSOCK_INVALID;

        if (XSock_AddMembership(pSock, NULL) == XSOCK_INVALID)
            return XSOCK_INVALID;
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

XSOCKET XSock_CreateAdv(xsock_t *pSock, uint32_t nFlags, size_t nFdMax, const char *pAddr, uint16_t nPort, const char *pName)
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

    /* Close-on-exec from the first instant: SOCK_CLOEXEC is a type flag
       (Linux/BSD); FD_CLOEXEC is an fcntl() flag and must NOT be OR-ed
       into the socket type (it would turn SOCK_DGRAM into SOCK_RAW).
       Platforms without SOCK_CLOEXEC (macOS) fall back to fcntl() right
       after creation, same as XSock_CreatePair above. */
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
    nType |= SOCK_CLOEXEC;
#endif

    pSock->nFD = socket(pSock->nDomain, nType, pSock->nProto);
    if (pSock->nFD == XSOCK_INVALID)
    {
        pSock->eStatus = XSOCK_ERR_CREATE;
        return XSOCK_INVALID;
    }

#if !defined(_WIN32) && !defined(SOCK_CLOEXEC) && defined(FD_CLOEXEC)
    fcntl(pSock->nFD, F_SETFD, FD_CLOEXEC);
#endif

    if (XSock_SetupAddr(pSock, pAddr, nPort) < 0)
    {
        XSock_Close(pSock);
        return XSOCK_INVALID;
    }

    if (xstrused(pName)) xstrncpy(pSock->sName, sizeof(pSock->sName), pName);
    xbool_t bReuseAddr = XFLAGS_CHECK(pSock->nFlags, XSOCK_REUSEADDR);

    if (pSock->nType == SOCK_STREAM) XSock_SetupStream(pSock, pAddr, nFdMax);
    else if (pSock->nType == SOCK_DGRAM) XSock_SetupDgram(pSock, bReuseAddr);

    if (XFLAGS_CHECK(pSock->nFlags, XSOCK_NB))
        return XSock_NonBlock(pSock, XTRUE);

    return pSock->nFD;
}

XSOCKET XSock_Create(xsock_t *pSock, uint32_t nFlags, const char *pAddr, uint16_t nPort)
{
    return XSock_CreateAdv(pSock, nFlags, 0, pAddr, nPort, NULL);
}

XSOCKET XSock_Open(xsock_t *pSock, uint32_t nFlags, xsock_info_t *pAddr)
{
    if (!xstrused(pAddr->sAddr) || (!pAddr->nPort && !XFLAGS_CHECK(nFlags, XSOCK_UNIX)))
    {
        pSock->eStatus = XSOCK_ERR_ARGS;
        pSock->nFD = XSOCK_INVALID;
        return XSOCK_INVALID;
    }

    return XSock_CreateAdv(pSock, nFlags, 0, pAddr->sAddr, pAddr->nPort, pAddr->sName);
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
