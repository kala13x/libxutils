/*!
 *  @file libxutils/src/net/sock.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Cross-plaform socket operations such as
 * create, bind, connect, listen, select and etc.
 */

#ifndef __XUTILS_XSOCK_H__
#define __XUTILS_XSOCK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"
#include "xbuf.h"

#ifdef _XUTILS_USE_SSL
#define OPENSSL_API_COMPAT XSSL_MINIMAL_API
#include <openssl/pkcs12.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#define XSOCK_USE_SSL       XTRUE
#endif

/* MacOS Compatibility */
#ifdef DARWIN
#include <fcntl.h>
#define SOCK_NONBLOCK       O_NONBLOCK
#define MSG_NOSIGNAL        SO_NOSIGPIPE
#endif

#ifdef _WIN32
typedef SOCKET              XSOCKET;
#define XSHUT_RDWR          SD_BOTH
#define XSOCK_INVALID       INVALID_SOCKET
#define XMSG_NOSIGNAL       0
#define XMSG_DONTWAIT       0
#else
typedef int                 XSOCKET;
#define XSHUT_RDWR          SHUT_RDWR
#define XMSG_NOSIGNAL       MSG_NOSIGNAL
#define XMSG_DONTWAIT       MSG_DONTWAIT
#define XSOCK_INVALID       XSTDERR
#endif

#define XSOCK_SUCCESS       XSTDOK
#define XSOCK_ERROR         XSTDERR
#define XSOCK_NONE          XSTDNON

/* Limits */
#define XSOCK_CHUNK_MAX     1024 * 32
#define XSOCK_RX_MAX        1024 * 8
#define XSOCK_FD_MAX        120000
#define XSOCK_INFO_MAX      256
#define XSOCK_ADDR_MAX      128

/* Socket errors */
typedef enum {
    XSOCK_ERR_NONE = (uint8_t)0,
    XSOCK_ERR_ALLOC,
    XSOCK_ERR_ARGS,
    XSOCK_ERR_INVALID,
    XSOCK_ERR_SUPPORT,
    XSOCK_ERR_CONNECT,
    XSOCK_ERR_CREATE,
    XSOCK_ERR_ACCEPT,
    XSOCK_ERR_LISTEN,
    XSOCK_ERR_WRITE,
    XSOCK_ERR_READ,
    XSOCK_ERR_SEND,
    XSOCK_ERR_RECV,
    XSOCK_ERR_JOIN,
    XSOCK_ERR_BIND,
    XSOCK_ERR_ADDR,
    XSOCK_ERR_SETFL,
    XSOCK_ERR_GETFL,
    XSOCK_ERR_SETOPT,
    XSOCK_ERR_PKCS12,
    XSOCK_ERR_SSLWRITE,
    XSOCK_ERR_SSLREAD,
    XSOCK_ERR_SSLINV,
    XSOCK_ERR_SSLNEW,
    XSOCK_ERR_SSLCTX,
    XSOCK_ERR_SSLMET,
    XSOCK_ERR_SSLCNT,
    XSOCK_ERR_SSLACC,
    XSOCK_ERR_SSLKEY,
    XSOCK_ERR_SSLCRT,
    XSOCK_ERR_SSLERR,
    XSOCK_ERR_SSLCA,
    XSOCK_ERR_NOSSL,
    XSOCK_ERR_FLAGS,
    XSOCK_ERR_INVSSL,
    XSOCK_ERR_SYSCALL,
    XSOCK_WANT_READ,
    XSOCK_WANT_WRITE,
    XSOCK_EOF
} xsock_status_t;

typedef enum {
    XF_UNDEF = (uint8_t)0,
    XF_IPV4 = 4,
    XF_IPV6 = 6
} xsock_family_t;

/* Supported socket types and roles */
typedef enum {
    XSOCK_TCP = (1 << 0),
    XSOCK_UDP = (1 << 1),
    XSOCK_RAW = (1 << 2),
    XSOCK_UNIX = (1 << 3),

    XSOCK_SSL = (1 << 4),
    XSOCK_SSLV2 = (1 << 5),
    XSOCK_SSLV3 = (1 << 6),

    XSOCK_PEER = (1 << 7),
    XSOCK_CLIENT = (1 << 8),
    XSOCK_SERVER = (1 << 9),

    XSOCK_BROADCAST = (1 << 10),
    XSOCK_MULTICAST = (1 << 11),
    XSOCK_UNICAST = (1 << 12),

    XSOCK_NB = (1 << 13),
    XSOCK_FORCE = (1 << 14),
    XSOCK_UNDEFINED = 0
} xsock_flags_t;

typedef struct XSocketInfo {
    xsock_family_t eFamily;
    uint32_t nAddr;
    uint16_t nPort;
    char sAddr[XSOCK_ADDR_MAX];
    char sHost[XSOCK_INFO_MAX];
    char sName[XSOCK_INFO_MAX];
} xsock_info_t;

typedef struct XSocketSSLCert {
    uint8_t nStatus;
    void *pCert;
    void *pKey;
    void *pCa;
} xsocket_ssl_cert_t;

typedef struct XSocketCert {
    const char *pCertPath;
    const char *pKeyPath;
    const char *pCaPath;
    const char *p12Path;
    const char *p12Pass;
    int nVerifyFlags;
} xsock_cert_t;

typedef union {
    struct sockaddr_in inAddr;
    struct sockaddr_un unAddr;
} xsock_addr_t;

/* XSocket */
typedef struct XSocket {
    xsock_status_t eStatus;
    xsock_addr_t sockAddr;

    uint32_t nAddr;
    uint16_t nPort;

    uint32_t nFlags;
    size_t nFdMax;

    XSOCKET nFD;
    int nDomain;
    int nProto;
    int nType;

    void *pPrivate;
} xsock_t;

// Backward compatibility
#define XSOCK_TCP_PEER (XSOCK_TCP | XSOCK_PEER)
#define XSOCK_TCP_SERVER (XSOCK_TCP | XSOCK_SERVER)
#define XSOCK_TCP_CLIENT (XSOCK_TCP | XSOCK_CLIENT)
#define XSOCK_UNIX_PEER (XSOCK_UNIX | XSOCK_PEER)
#define XSOCK_UNIX_SERVER (XSOCK_UNIX | XSOCK_SERVER)
#define XSOCK_UNIX_CLIENT (XSOCK_UNIX | XSOCK_CLIENT)
#define XSOCK_UDP_CLIENT (XSOCK_UDP | XSOCK_CLIENT)
#define XSOCK_UDP_BCAST (XSOCK_UDP | XSOCK_BROADCAST)
#define XSOCK_UDP_MCAST (XSOCK_UDP | XSOCK_MULTICAST)
#define XSOCK_UDP_UCAST (XSOCK_UDP | XSOCK_UNICAST)

const char* XSock_GetStatusStr(xsock_status_t eStatus);
const char* XSock_ErrStr(xsock_t* pSock);

#ifdef _XSOCK_USE_SSL
SSL_CTX* XSock_GetSSLCTX(xsock_t *pSock);
SSL* XSock_GetSSL(xsock_t *pSock);
#endif

xsock_status_t XSock_Status(const xsock_t *pSock);
uint32_t XSock_GetFlags(const xsock_t *pSock);
xbool_t XFlags_IsSSL(uint32_t nFlags);

xsockaddr_t* XSock_GetSockAddr(xsock_t *pSock);
xsocklen_t XSock_GetAddrLen(xsock_t *pSock);

uint32_t XSock_GetNetAddr(const xsock_t *pSock);
uint16_t XSock_GetPort(const xsock_t *pSock);
size_t XSock_GetFDMax(const xsock_t *pSock);
int XSock_GetSockType(const xsock_t *pSock);
int XSock_GetProto(const xsock_t *pSock);

XSOCKET XSock_GetFD(const xsock_t *pSock);
xbool_t XSock_IsSSL(const xsock_t *pSock);
xbool_t XSock_IsNB(const xsock_t *pSock);

int xclosesock(XSOCKET nFd);
void XSock_Close(xsock_t* pSock);

XSTATUS XSock_MsgPeek(xsock_t* pSock);
XSTATUS XSock_IsOpen(xsock_t* pSock);
XSTATUS XSock_Check(xsock_t* pSock);
XSTATUS XSock_Init(xsock_t* pSock, uint32_t nFlags, XSOCKET nFD);

void XSock_InitSSL(void);
void XSock_DeinitSSL(void);
int XSock_LastSSLError(char* pDst, size_t nSize);

XSTATUS XSock_LoadPKCS12(xsocket_ssl_cert_t* pCert, const char* p12Path, const char* p12Pass);
XSOCKET XSock_SetSSLCert(xsock_t* pSock, xsock_cert_t* pCert);
void XSock_InitCert(xsock_cert_t *pCert);

XSOCKET XSock_InitSSLServer(xsock_t* pSock);
XSOCKET XSock_InitSSLClient(xsock_t* pSock);

XSOCKET XSock_SSLConnect(xsock_t *pSock);
XSOCKET XSock_SSLAccept(xsock_t *pSock);

int XSock_SSLRead(xsock_t* pSock, void* pData, size_t nSize, xbool_t nExact);
int XSock_SSLWrite(xsock_t* pSock, const void* pData, size_t nLength);

int XSock_WriteBuff(xsock_t *pSock, xbyte_buffer_t *pBuffer);
int XSock_SendBuff(xsock_t *pSock, xbyte_buffer_t *pBuffer);
int XSock_SendChunk(xsock_t* pSock, void* pData, size_t nLength);
int XSock_RecvChunk(xsock_t* pSock, void* pData, size_t nSize);
int XSock_Send(xsock_t* pSock, const void* pData, size_t nLength);
int XSock_Write(xsock_t* pSock, const void* pData, size_t nLength);
int XSock_Read(xsock_t* pSock, void* pData, size_t nSize);
int XSock_Recv(xsock_t* pSock, void* pData, size_t nSize);

XSOCKET XSock_Accept(xsock_t* pSock, xsock_t* pNewSock);
XSOCKET XSock_AcceptNB(xsock_t* pSock);

uint32_t XSock_NetAddr(const char* pAddr);
size_t XSock_SinAddr(const struct in_addr inAddr, char* pAddr, size_t nSize);
size_t XSock_IPAddr(xsock_t* pSock, char* pAddr, size_t nSize);
size_t XSock_IPStr(const uint32_t nAddr, char* pStr, size_t nSize);

void XSock_InitAddr(xsock_info_t* pAddr);
XSTATUS XSock_AddrInfo(xsock_info_t* pAddr, xsock_family_t eFam, const char* pHost);
XSTATUS XSock_GetAddr(xsock_info_t* pAddr, const char* pHost);
XSTATUS XSock_Addr(xsock_info_t* pInfo, struct sockaddr_in* pAddr, size_t nSize);

XSOCKET XSock_AddMembership(xsock_t* pSock, const char* pGroup);
XSOCKET XSock_ReuseAddr(xsock_t* pSock, xbool_t nEnabled);
XSOCKET XSock_Oobinline(xsock_t* pSock, xbool_t nEnabled);
XSOCKET XSock_NonBlock(xsock_t* pSock, xbool_t nNonBlock);
XSOCKET XSock_NoDelay(xsock_t* pSock, xbool_t nEnabled);
XSOCKET XSock_TimeOutR(xsock_t* pSock, int nSec, int nUsec);
XSOCKET XSock_TimeOutS(xsock_t* pSock, int nSec, int nUsec);
XSOCKET XSock_Linger(xsock_t* pSock, int nSec);
XSOCKET XSock_Bind(xsock_t *pSock);

XSOCKET XSock_CreateAdv(xsock_t* pSock, uint32_t nFlags, size_t nFdMax, const char* pAddr, uint16_t nPort);
XSOCKET XSock_Create(xsock_t* pSock, uint32_t nFlags, const char* pAddr, uint16_t nPort);
XSOCKET XSock_Open(xsock_t* pSock, uint32_t nFlags, xsock_info_t* pAddr);
XSOCKET XSock_Setup(xsock_t* pSock, uint32_t nFlags, const char* pAddr);

xsock_t* XSock_Alloc(uint32_t nFlags, const char* pAddr, uint16_t nPort);
xsock_t* XSock_New(uint32_t nFlags, xsock_info_t* pAddr);
void XSock_Free(xsock_t* pSock);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XSOCK_H__ */
