# sock.c

## Purpose

Cross-platform socket wrapper covering TCP, UDP, Unix sockets, socket options and optional OpenSSL integration.

## General Return Convention

- Functions returning `XSOCKET` usually return a valid file descriptor/handle on success and `XSOCK_INVALID` on failure.
- Functions returning `int` usually return transferred byte count, `XSOCK_NONE` for no-op/empty input and `XSOCK_ERROR` on failure.
- Most failure paths set `pSock->eStatus`.
- Many fatal failures also close the socket immediately.

## API Reference

### Status and property helpers

#### `const char *XSock_GetStatusStr(xsock_status_t eStatus)`

#### `const char *XSock_ErrStr(xsock_t *pSock)`

- Arguments:
  - status enum or socket object.
- Does:
  - converts socket status to text.
- Returns:
  - static string pointer.
  - `XSock_ErrStr()` returns an empty string for `NULL`.

#### `SSL_CTX *XSock_GetSSLCTX(xsock_t *pSock)`

#### `SSL *XSock_GetSSL(xsock_t *pSock)`

- Arguments:
  - socket object.
- Does:
  - exposes the internal OpenSSL context or SSL handle.
- Returns:
  - internal pointer when SSL private state exists.
  - `NULL` for `NULL` sockets, sockets without SSL state or no-SSL builds.

#### `xsock_status_t XSock_Status(const xsock_t *pSock)`

#### `uint32_t XSock_GetFlags(const xsock_t *pSock)`

#### `uint32_t XSock_GetNetAddr(const xsock_t *pSock)`

#### `uint16_t XSock_GetPort(const xsock_t *pSock)`

#### `int XSock_GetSockType(const xsock_t *pSock)`

#### `int XSock_GetProto(const xsock_t *pSock)`

#### `XSOCKET XSock_GetFD(const xsock_t *pSock)`

#### `xbool_t XSock_IsSSL(const xsock_t *pSock)`

#### `xbool_t XSock_IsNB(const xsock_t *pSock)`

#### `xbool_t XFlags_IsSSL(uint32_t nFlags)`

- Arguments:
  - socket pointer or raw flags.
- Does:
  - returns current status/flags/descriptor/transport metadata.
- Returns:
  - the requested field or `XTRUE`/`XFALSE`.

#### `xbool_t XSock_IsSSLError(xsock_status_t eStatus)`

- Arguments:
  - socket status enum.
- Does:
  - checks whether the status belongs to the SSL-related error subset.
- Returns:
  - `XTRUE` or `XFALSE`.

#### `xsockaddr_t *XSock_GetSockAddr(xsock_t *pSock)`

#### `xsocklen_t XSock_GetAddrLen(xsock_t *pSock)`

- Arguments:
  - `pSock`: socket object.
- Does:
  - exposes the active address union member and its correct length for IPv4 or Unix-domain sockets.
- Returns:
  - internal address pointer or address length.

### Basic lifecycle

#### `XSTATUS XSock_Init(xsock_t *pSock, uint32_t nFlags, XSOCKET nFD)`

- Arguments:
  - `pSock`: socket object to initialize.
  - `nFlags`: socket role/type flags.
  - `nFD`: existing fd or `XSOCK_INVALID`.
- Does:
  - clears the socket object.
  - normalizes flags:
    - SSLv2/SSLv3 imply `XSOCK_SSL`
    - broadcast/multicast/unicast imply `XSOCK_UDP`
  - selects domain/protocol/type.
  - allocates SSL private state when needed.
- Returns:
  - `XSOCK_SUCCESS` on success.
  - `XSOCK_ERROR` when flags are invalid/unsupported or SSL-private allocation fails.

#### `XSTATUS XSock_IsOpen(xsock_t *pSock)`

#### `XSTATUS XSock_Check(xsock_t *pSock)`

- Arguments:
  - `pSock`: socket object.
- Does:
  - `XSock_IsOpen()` only checks whether `nFD` is valid.
  - `XSock_Check()` additionally resets `eStatus` to `XSOCK_ERR_NONE` when the socket is valid.
- Returns:
  - `XSOCK_SUCCESS` when open.
  - `XSOCK_NONE` when closed/invalid.

#### `int xclosesock(XSOCKET nFd)`

- Arguments:
  - raw socket handle/fd.
- Does:
  - calls `closesocket()` on Windows or `close()` elsewhere.
- Returns:
  - OS return code.

#### `void XSock_Close(xsock_t *pSock)`

- Arguments:
  - `pSock`: socket object.
- Does:
  - shuts down and closes the fd if open.
  - frees SSL object/context/private storage when present.
- Returns:
  - no return value.

### SSL helpers

#### `void XSock_InitSSL(void)`

#### `void XSock_DeinitSSL(void)`

- Arguments:
  - none.
- Does:
  - initializes or tears down OpenSSL global state when SSL support is compiled in.
  - becomes a no-op without SSL support.
- Returns:
  - no return value.

#### `int XSock_LastSSLError(char *pDst, size_t nSize)`

- Arguments:
  - `pDst`: destination buffer.
  - `nSize`: destination size.
- Does:
  - serializes the OpenSSL error queue into caller memory.
- Returns:
  - copied byte count.
  - `0` when there is no text or SSL support is absent.
  - `XSOCK_NONE` when `pDst == NULL`.

#### `XSTATUS XSock_LoadPKCS12(xsock_ssl_cert_t *pCert, const char *p12Path, const char *p12Pass)`

- Arguments:
  - `pCert`: destination certificate bundle.
  - `p12Path`, `p12Pass`: PKCS#12 file and password.
- Does:
  - loads and parses a PKCS#12 bundle into OpenSSL certificate/key/CA pointers.
- Returns:
  - `XSOCK_SUCCESS` on success.
  - `XSOCK_ERROR` on file/parse failure.
  - `XSOCK_NONE` when SSL support is not compiled in.

#### `void XSock_InitCert(xsock_cert_t *pCert)`

- Arguments:
  - `pCert`: certificate config object.
- Does:
  - clears all path/verify fields.
- Returns:
  - no return value.

#### `XSOCKET XSock_SetSSLCert(xsock_t *pSock, xsock_cert_t *pCert)`

- Arguments:
  - `pSock`: socket whose SSL context/object already exists.
  - `pCert`: certificate/CA/hostname settings.
- Does:
  - configures CA locations, hostname verification, PEM or PKCS#12 certificates and private keys.
  - may call `SSL_set_tlsext_host_name()` for clients.
- Returns:
  - current socket fd on success.
  - `XSOCK_INVALID` on invalid SSL state, OpenSSL failure or no-SSL builds.

#### `XSOCKET XSock_InitSSLServer(xsock_t *pSock, int nVerifyFlags)`

#### `XSOCKET XSock_InitSSLClient(xsock_t *pSock, const char *pAddr)`

- Arguments:
  - socket object plus verify flags or SNI hostname.
- Does:
  - creates SSL context and SSL object for server or client mode.
  - server helper immediately stores the context.
  - client helper also creates the SSL object, binds it to the fd and starts the connect handshake.
- Returns:
  - fd on success or non-blocking WANT state.
  - `XSOCK_INVALID` on failure.

#### `XSOCKET XSock_SSLConnect(xsock_t *pSock)`

#### `XSOCKET XSock_SSLAccept(xsock_t *pSock)`

- Arguments:
  - socket with a valid SSL object.
- Does:
  - performs the SSL connect or accept handshake.
- Returns:
  - fd on success.
  - fd with `eStatus == XSOCK_WANT_READ` or `XSOCK_WANT_WRITE` for non-blocking retry cases.
  - `XSOCK_INVALID` on terminal failure.

#### `int XSock_SSLRead(xsock_t *pSock, void *pData, size_t nSize, xbool_t nExact)`

#### `int XSock_SSLWrite(xsock_t *pSock, const void *pData, size_t nLength)`

- Arguments:
  - `pSock`: SSL socket.
  - buffer pointer and requested size.
  - `nExact`: for reads, whether to keep reading until exactly `nSize` bytes are collected.
- Does:
  - performs SSL I/O.
  - maps `SSL_ERROR_WANT_READ` / `WANT_WRITE` to socket status for event-driven retry.
  - closes the socket on terminal SSL errors or EOF.
- Returns:
  - transferred byte count on success.
  - partial byte count in some EOF/error paths after data was already read.
  - `XSOCK_ERROR` on immediate failure.

### Data I/O helpers

#### `int XSock_Read(xsock_t *pSock, void *pData, size_t nSize)`

#### `int XSock_Write(xsock_t *pSock, const void *pData, size_t nLength)`

- Arguments:
  - `pSock`: socket.
  - buffer pointer and requested size.
- Does:
  - stream-style `read()` / `write()` wrappers.
  - forward to SSL helpers when the socket has `XSOCK_SSL`.
  - close the socket on EOF or write/read error.
- Returns:
  - transferred byte count.
  - `XSOCK_NONE` for zero-length or `NULL` buffer requests.
  - `XSOCK_ERROR` / `XSOCK_INVALID` on failure depending on the path.

#### `int XSock_Recv(xsock_t *pSock, void *pData, size_t nSize)`

#### `int XSock_Send(xsock_t *pSock, const void *pData, size_t nLength)`

- Arguments:
  - same shape as `Read/Write`.
- Does:
  - `recv/recvfrom` and `send/sendto` wrappers.
  - use datagram address semantics for `SOCK_DGRAM`.
  - close the socket on error or EOF.
- Returns:
  - transferred byte count, `XSOCK_NONE`, or `XSOCK_ERROR`.

#### `int XSock_RecvChunk(xsock_t *pSock, void *pData, size_t nSize)`

#### `int XSock_SendChunk(xsock_t *pSock, void *pData, size_t nLength)`

- Arguments:
  - socket plus buffer and exact byte count.
- Does:
  - loops in `32 KiB` chunks until the exact size is received or sent.
  - forward to SSL helpers when SSL is enabled.
- Returns:
  - total transferred bytes.
  - partial count or `XSOCK_ERROR` on failure.

#### `int XSock_WriteBuff(xsock_t *pSock, xbyte_buffer_t *pBuffer)`

#### `int XSock_SendBuff(xsock_t *pSock, xbyte_buffer_t *pBuffer)`

- Arguments:
  - socket plus byte buffer.
- Does:
  - forwards buffer contents to `XSock_Write()` or `XSock_Send()`.
- Returns:
  - transfer result.
  - `XSOCK_NONE` when `pBuffer == NULL`.

#### `XSTATUS XSock_MsgPeek(xsock_t *pSock)`

- Arguments:
  - socket.
- Does:
  - attempts a one-byte non-blocking `MSG_PEEK`.
- Returns:
  - `XSOCK_SUCCESS` when data is immediately peekable.
  - `XSOCK_NONE` when the peek fails.
  - `XSOCK_ERROR` when the socket is invalid.

### Accept and resolver helpers

#### `XSOCKET XSock_Accept(xsock_t *pSock, xsock_t *pNewSock)`

- Arguments:
  - `pSock`: listening socket.
  - `pNewSock`: destination peer socket object.
- Does:
  - derives peer flags from the listener, clearing `SERVER` and `NB`, setting `PEER`.
  - accepts the connection and, for SSL listeners, creates a per-peer SSL object and performs/starts SSL accept.
- Returns:
  - accepted fd on success.
  - `XSOCK_INVALID` on failure.

#### `XSOCKET XSock_AcceptNB(xsock_t *pSock)`

- Arguments:
  - listening socket.
- Does:
  - Linux/GNU-only `accept4()` helper for non-blocking accept.
- Returns:
  - accepted fd or `XSOCK_INVALID`.

#### `uint32_t XSock_NetAddr(const char *pAddr)`

- Arguments:
  - IPv4 dotted string or `NULL`.
- Does:
  - converts IPv4 text to network-order address.
- Returns:
  - parsed IPv4 address.
  - `htonl(INADDR_ANY)` for `NULL`.
  - `0` on parse failure.

#### `size_t XSock_IPStr(const uint32_t nAddr, char *pStr, size_t nSize)`

#### `size_t XSock_SinAddr(const struct in_addr inAddr, char *pAddr, size_t nSize)`

#### `size_t XSock_IPAddr(const xsock_t *pSock, char *pAddr, size_t nSize)`

- Arguments:
  - raw IPv4 integer, `in_addr`, or socket object.
- Does:
  - formats IPv4 addresses into dotted-decimal text.
- Returns:
  - copied string length.
  - `XSOCK_NONE` for Unix sockets in `XSock_IPAddr()`.

#### `void XSock_InitInfo(xsock_info_t *pAddr)`

- Arguments:
  - address-info struct.
- Does:
  - clears fields and resets family/port/address.
- Returns:
  - no return value.

#### `XSTATUS XSock_AddrInfo(xsock_info_t *pAddr, xsock_family_t eFam, const char *pHost)`

- Arguments:
  - `pAddr`: output info.
  - `eFam`: desired family, typically `XF_IPV4`.
  - `pHost`: host name.
- Does:
  - resolves the host with `getaddrinfo()`.
  - fills canonical name, text address and network-order IPv4 address when a matching family is found.
- Returns:
  - `XSOCK_SUCCESS` when the requested family is resolved.
  - `XSOCK_NONE` when name resolution produced some text but not the requested family.
  - `XSOCK_ERROR` on resolver failure.

#### `XSTATUS XSock_GetAddrInfo(xsock_info_t *pAddr, const char *pHost)`

- Arguments:
  - `pAddr`: output info.
  - `pHost`: `"host:port"` string.
- Does:
  - splits the input on `:`.
  - resolves the host as IPv4.
  - parses the optional port.
- Returns:
  - `XSOCK_SUCCESS` when host resolves and a non-zero port is present.
  - `XSOCK_NONE` when host resolves but the port is missing/zero.
  - `XSOCK_ERROR` on parse or resolve failure.

#### `XSTATUS XSock_GetAddr(xsock_info_t *pInfo, struct sockaddr_in *pAddr, size_t nSize)`

- Arguments:
  - `pInfo`: output info.
  - `pAddr`: IPv4 socket address.
  - `nSize`: address byte size for `gethostbyaddr()`.
- Does:
  - reverse-resolves the host name when possible and always formats the IPv4 text address.
- Returns:
  - `XSOCK_SUCCESS` when reverse lookup returns a host name.
  - `XSOCK_NONE` when only the IP string is available.

### Socket option helpers

#### `XSOCKET XSock_NonBlock(xsock_t *pSock, xbool_t nNonBlock)`

#### `XSOCKET XSock_TimeOutR(xsock_t *pSock, int nSec, int nUsec)`

#### `XSOCKET XSock_TimeOutS(xsock_t *pSock, int nSec, int nUsec)`

#### `XSOCKET XSock_ReuseAddr(xsock_t *pSock, xbool_t nEnabled)`

#### `XSOCKET XSock_Oobinline(xsock_t *pSock, xbool_t nEnabled)`

#### `XSOCKET XSock_NoDelay(xsock_t *pSock, xbool_t nEnabled)`

#### `XSOCKET XSock_Linger(xsock_t *pSock, int nSec)`

- Arguments:
  - socket plus option value.
- Does:
  - configures non-blocking mode or standard socket options.
  - closes the socket on option failure.
- Returns:
  - fd on success.
  - `XSOCK_INVALID` on failure.

#### `XSOCKET XSock_Bind(xsock_t *pSock)`

- Arguments:
  - socket prepared with an address.
- Does:
  - binds the socket.
  - if `XSOCK_UNIX | XSOCK_FORCE` is set, removes an existing Unix socket path first.
- Returns:
  - fd on success.
  - `XSOCK_INVALID` on bind failure.

#### `XSOCKET XSock_AddMembership(xsock_t *pSock, const char *pGroup)`

- Arguments:
  - multicast socket.
  - `pGroup`: multicast address string.
- Does:
  - joins the multicast group.
- Returns:
  - fd on success.
  - `XSOCK_INVALID` on failure.

### High-level open/create helpers

#### `XSOCKET XSock_CreateAdv(xsock_t *pSock, uint32_t nFlags, size_t nFdMax, const char *pAddr, uint16_t nPort)`

- Arguments:
  - socket object, flags, listen backlog and address/port.
- Does:
  - initializes the socket object.
  - creates the OS socket.
  - prepares the address, optional `SO_REUSEADDR`, bind/connect/listen and SSL startup.
  - applies non-blocking mode at the end when requested.
- Returns:
  - fd on success.
  - `XSOCK_INVALID` on invalid args or any setup failure.

#### `XSOCKET XSock_Create(xsock_t *pSock, uint32_t nFlags, const char *pAddr, uint16_t nPort)`

- Arguments:
  - same as `CreateAdv`, without explicit backlog.
- Does:
  - forwards to `XSock_CreateAdv(..., 0, ...)`.
- Returns:
  - forwarded result.

#### `XSOCKET XSock_Open(xsock_t *pSock, uint32_t nFlags, xsock_info_t *pAddr)`

- Arguments:
  - socket object, flags and resolved address info.
- Does:
  - validates the resolved address and port and forwards to `XSock_Create()`.
- Returns:
  - fd or `XSOCK_INVALID`.

#### `XSOCKET XSock_Setup(xsock_t *pSock, uint32_t nFlags, const char *pAddr)`

- Arguments:
  - socket object, flags and raw address string.
- Does:
  - for Unix sockets, forwards directly to `XSock_Create()`.
  - otherwise resolves `host:port` with `XSock_GetAddrInfo()` and opens the socket.
- Returns:
  - fd or `XSOCK_INVALID`.

#### `xsock_t *XSock_Alloc(uint32_t nFlags, const char *pAddr, uint16_t nPort)`

#### `xsock_t *XSock_New(uint32_t nFlags, xsock_info_t *pAddr)`

- Arguments:
  - socket creation parameters.
- Does:
  - heap-allocate a socket object and create/open it immediately.
- Returns:
  - allocated socket object even if later open failed in `XSock_Alloc()`.
  - `NULL` on allocation failure.
  - `XSock_New()` additionally validates that address and port are present.

#### `void XSock_Free(xsock_t *pSock)`

- Arguments:
  - heap-allocated socket object.
- Does:
  - closes it and frees the structure.
- Returns:
  - no return value.

## Important Notes

- Treat `XSOCK_WANT_READ` and `XSOCK_WANT_WRITE` as flow-control states for non-blocking SSL I/O, not terminal failures.
- Many failure paths close the socket immediately, so inspect `eStatus` before reusing the object.
- `XSock_Alloc()` does not return `NULL` when socket creation fails after allocation; callers must still inspect `nFD` / `eStatus`.
