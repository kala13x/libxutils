# http.c

## Purpose

HTTP request/response object, parser, assembler and simple blocking client helpers.

## Callback Semantics

- `XHTTP_SetCallback()` installs an optional callback plus a bitmask of enabled callback kinds.
- Read helpers interpret callback return values specially:
  - `XSTDERR`: terminate parsing and return `XHTTP_TERMINATED`.
  - `XSTDNON`: mark the message complete immediately.
  - `XSTDOK`: treat bytes as consumed but do not append them to `rawData`.
  - any other value: append/read normally.

## API Reference

### Enum and status helpers

#### `const char *XHTTP_GetStatusStr(xhttp_status_t eStatus)`

#### `const char *XHTTP_GetCodeStr(int nCode)`

#### `const char *XHTTP_GetMethodStr(xhttp_method_t eMethod)`

#### `xhttp_method_t XHTTP_GetMethodType(const char *pData)`

- Arguments:
  - enum/code or method prefix string.
- Does:
  - converts HTTP status, status code and method enums to text, or parses a request-line method prefix.
- Returns:
  - static string pointers or a method enum.
  - `XHTTP_DUMMY` for unknown method text.

#### `xbool_t XHTTP_IsSuccessCode(xhttp_t *pHandle)`

- Arguments:
  - `pHandle`: HTTP object.
- Does:
  - checks whether `nStatusCode` is in `[200, 299]`.
- Returns:
  - `XTRUE` or `XFALSE`.

### Object lifecycle

#### `int XHTTP_SetCallback(xhttp_t *pHttp, xhttp_cb_t callback, void *pCbCtx, uint16_t nCbTypes)`

- Arguments:
  - `pHttp`: HTTP object.
  - `callback`: callback function, may be `NULL`.
  - `pCbCtx`: caller context saved into the object.
  - `nCbTypes`: enabled callback bitmask.
- Does:
  - stores callback configuration.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` when `pHttp == NULL`.

#### `int XHTTP_Init(xhttp_t *pHttp, xhttp_method_t eMethod, size_t nSize)`

- Arguments:
  - `pHttp`: object to initialize.
  - `eMethod`: initial method.
  - `nSize`: initial raw-buffer capacity.
- Does:
  - resets all counters and flags.
  - initializes the header map and raw byte buffer.
  - sets default header/content limits.
- Returns:
  - result of `XByteBuffer_Init()`.

#### `int XHTTP_InitRequest(xhttp_t *pHttp, xhttp_method_t eMethod, const char *pUri, const char *pVer)`

- Arguments:
  - `pHttp`: request object.
  - `eMethod`: HTTP method.
  - `pUri`: request URI, defaults to `"\\"` when `NULL`.
  - `pVer`: HTTP version, defaults to `XHTTP_VER_DEFAULT`.
- Does:
  - initializes the object and marks it as a request.
- Returns:
  - init status or `XSTDERR`.

#### `int XHTTP_InitResponse(xhttp_t *pHttp, uint16_t nStatusCode, const char *pVer)`

- Arguments:
  - `pHttp`: response object.
  - `nStatusCode`: HTTP code.
  - `pVer`: version string, defaults to `XHTTP_VER_DEFAULT`.
- Does:
  - initializes the object and marks it as a response.
- Returns:
  - init status on success.
  - `XSTDERR` when base init fails.

#### `size_t XHTTP_SetUnixAddr(xhttp_t *pHttp, const char *pUnixAddr)`

- Arguments:
  - `pHttp`: HTTP object.
  - `pUnixAddr`: Unix-domain socket path.
- Does:
  - stores the Unix socket path in `sUnixAddr`.
- Returns:
  - copied length.
  - `XSTDERR` when arguments are invalid.

#### `void XHTTP_Reset(xhttp_t *pHttp, xbool_t bHard)`

- Arguments:
  - `pHttp`: object to reset.
  - `bHard`: whether to fully destroy/recreate internal storage.
- Does:
  - hard reset:
    - clears and reinitializes the raw buffer and header map.
  - soft reset:
    - preserves allocations but resets used data and map entries.
- Returns:
  - no return value.

#### `xhttp_t *XHTTP_Alloc(xhttp_method_t eMethod, size_t nDataSize)`

- Arguments:
  - `eMethod`: initial method.
  - `nDataSize`: initial buffer size.
- Does:
  - heap-allocates and initializes an `xhttp_t`.
- Returns:
  - allocated object on success.
  - `NULL` on allocation/init failure.

#### `int XHTTP_Copy(xhttp_t *pDst, xhttp_t *pSrc)`

- Arguments:
  - `pDst`: destination object.
  - `pSrc`: source object.
- Does:
  - deep-copies raw bytes, header map, metadata and callback configuration.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` on invalid args, buffer-copy failure or header-map copy failure.

#### `void XHTTP_Clear(xhttp_t *pHttp)`

#### `void XHTTP_Free(xhttp_t **pHttp)`

- Arguments:
  - `pHttp`: object or pointer-to-pointer for owned objects.
- Does:
  - destroys header-map contents and raw buffer.
  - `XHTTP_Free()` additionally frees heap storage when `nAllocated` is set.
- Returns:
  - no return value.

### Header and auth helpers

#### `int XHTTP_AddHeader(xhttp_t *pHttp, const char *pHeader, const char *pStr, ...)`

- Arguments:
  - `pHttp`: HTTP object.
  - `pHeader`: header key.
  - `pStr`: printf-style value format string.
- Does:
  - formats the header value.
  - if the key already exists with the same value, leaves it unchanged.
  - if the key exists with a different value, updates it only when `nAllowUpdate` is true.
- Returns:
  - current header count on success.
  - `XSTDEXC` when a different value already exists and updates are disallowed.
  - `XSTDERR` on formatting/allocation/map failure.

#### `size_t XHTTP_GetAuthToken(char *pToken, size_t nSize, const char *pUser, const char *pPass)`

- Arguments:
  - `pToken`: destination buffer.
  - `nSize`: caller-provided destination size.
  - `pUser`, `pPass`: credential strings.
- Does:
  - builds `user:pass`, base64-encodes it and copies the encoded token into `pToken`.
- Returns:
  - copied byte count.
  - `XSTDNON` when base64 encoding fails.
- Caveat:
  - the current implementation ignores `nSize` when copying and instead passes the encoded length to `xstrncpy()`. Treat this helper carefully.

#### `int XHTTP_SetAuthBasic(xhttp_t *pHttp, const char *pUser, const char *pPwd)`

- Arguments:
  - `pHttp`: HTTP object.
  - `pUser`, `pPwd`: credentials.
- Does:
  - base64-encodes `user:password` and inserts `Authorization: Basic ...`.
  - temporarily enables header updates while doing so.
- Returns:
  - `XSTDNON` when either credential is empty.
  - `XSTDERR` on encode failure.
  - otherwise the return from `XHTTP_AddHeader()`.

#### `const char *XHTTP_GetHeader(xhttp_t *pHttp, const char *pHeader)`

- Arguments:
  - `pHttp`: HTTP object.
  - `pHeader`: header name to look up.
- Does:
  - lowercases the lookup key and probes the header map.
- Returns:
  - stored header value pointer or `NULL`.
- Caveat:
  - parsed packets store keys lowercased, so lookup is reliable there.
  - `XHTTP_AddHeader()` stores keys as provided, so for manually assembled objects the safest approach is to use lowercase keys consistently.

### Assembly and access helpers

#### `xbyte_buffer_t *XHTTP_Assemble(xhttp_t *pHttp, const uint8_t *pContent, size_t nLength)`

- Arguments:
  - `pHttp`: request or response object.
  - `pContent`: optional body.
  - `nLength`: body length.
- Does:
  - serializes the start line and headers.
  - auto-adds `Content-Length` when a body exists.
  - auto-adds `Connection: keep-alive` when `nKeepAlive` is set and that header is absent.
  - appends the body and marks the packet complete.
  - reuses `rawData` without rebuilding when `nComplete` is already set.
- Returns:
  - pointer to the internal `rawData` buffer.
  - `NULL` on serialization or allocation failure.

#### `char *XHTTP_GetHeaderRaw(xhttp_t *pHttp)`

- Arguments:
  - `pHttp`: parsed or assembled object.
- Does:
  - copies only the header portion into a new heap string.
- Returns:
  - allocated NUL-terminated header copy.
  - `NULL` when header data is unavailable or allocation fails.

#### `const uint8_t *XHTTP_GetBody(xhttp_t *pHttp)`

#### `size_t XHTTP_GetBodySize(xhttp_t *pHttp)`

- Arguments:
  - `pHttp`: parsed or assembled object.
- Does:
  - exposes the bytes after `nHeaderLength`.
- Returns:
  - body pointer or body size.
  - `NULL` / `0` when no complete body is present.

#### `const uint8_t *XHTTP_GetExtraData(xhttp_t *pHttp)`

#### `size_t XHTTP_GetExtraSize(xhttp_t *pHttp)`

- Arguments:
  - `pHttp`: parsed object whose raw buffer may contain more than one packet.
- Does:
  - exposes bytes after the first complete packet.
- Returns:
  - trailing data pointer or size.
  - `NULL` / `0` when there is no extra buffered data.

#### `size_t XHTTP_GetPacketSize(xhttp_t *pHttp)`

- Arguments:
  - `pHttp`: parsed object.
- Does:
  - computes `rawData.nUsed - extraSize`.
- Returns:
  - size of the first complete packet.
  - `0` when `pHttp == NULL`.

### Parse helpers

#### `int XHTTP_AppendData(xhttp_t *pHttp, uint8_t *pData, size_t nSize)`

- Arguments:
  - `pHttp`: HTTP object.
  - `pData`, `nSize`: bytes to append.
- Does:
  - appends raw bytes into `rawData`.
- Returns:
  - `XByteBuffer_Add()` result.

#### `int XHTTP_InitParser(xhttp_t *pHttp, uint8_t *pData, size_t nSize)`

- Arguments:
  - `pHttp`: parser object.
  - `pData`, `nSize`: initial bytes.
- Does:
  - initializes the object and appends the supplied bytes.
- Returns:
  - `XSTDOK` when initialization plus append succeeds.
  - `XSTDERR` when append fails for non-zero input.

#### `xhttp_status_t XHTTP_Parse(xhttp_t *pHttp)`

- Arguments:
  - `pHttp`: object whose `rawData` already contains input.
- Does:
  - detects header boundary.
  - parses type, version, status/method, URI and headers.
  - computes `Content-Length`, keep-alive state and completeness.
  - emits `XHTTP_PARSED`/error status callbacks when configured.
- Returns:
  - `XHTTP_INCOMPLETE` when the header/body is not complete yet.
  - `XHTTP_PARSED` when the header is valid but the body is still incomplete.
  - `XHTTP_COMPLETE` when a full packet is present.
  - specific error status on invalid or allocation-failure paths.
  - `XHTTP_TERMINATED` if a callback aborts parsing.

#### `xhttp_status_t XHTTP_ParseData(xhttp_t *pHttp, uint8_t *pData, size_t nSize)`

#### `xhttp_status_t XHTTP_ParseBuff(xhttp_t *pHttp, xbyte_buffer_t *pBuffer)`

- Arguments:
  - parser object plus input bytes or an existing buffer.
- Does:
  - initializes the parser from raw input and forwards to `XHTTP_Parse()`.
- Returns:
  - parse status.
  - `XHTTP_EALLOC` when initialization/append fails.

### Socket read helpers

#### `xhttp_status_t XHTTP_ReadHeader(xhttp_t *pHttp, xsock_t *pSock)`

- Arguments:
  - `pHttp`: parser object.
  - `pSock`: socket to read from.
- Does:
  - reads until the header becomes parsed/complete, an error occurs, limits are exceeded, or a non-blocking socket would block.
  - emits optional read-header and immediate body callbacks.
- Returns:
  - `XHTTP_PARSED` or `XHTTP_COMPLETE` when header handling succeeds.
  - `XHTTP_INCOMPLETE` for non-blocking partial reads.
  - `XHTTP_BIGHDR`, `XHTTP_EREAD`, `XHTTP_EALLOC`, `XHTTP_TERMINATED`, etc. on failure.

#### `xhttp_status_t XHTTP_ReadContent(xhttp_t *pHttp, xsock_t *pSock)`

- Arguments:
  - `pHttp`: object whose header is already parsed.
  - `pSock`: socket to keep reading from.
- Does:
  - if `Content-Length` exists, reads until that many body bytes are present.
  - otherwise, when a `Content-Type` header exists, keeps reading until EOF or a callback stops it.
  - honors the callback special return values documented above.
- Returns:
  - `XHTTP_COMPLETE` when the body is complete or EOF finishes a body-without-length.
  - `XHTTP_INCOMPLETE` for partial non-blocking reads.
  - error status on allocation/read/callback failures.

#### `xhttp_status_t XHTTP_Receive(xhttp_t *pHttp, xsock_t *pSock)`

- Arguments:
  - `pHttp`: response/request object.
  - `pSock`: socket.
- Does:
  - runs `XHTTP_ReadHeader()` and, only when that returns `XHTTP_PARSED`, continues with `XHTTP_ReadContent()`.
- Returns:
  - forwarded status from the two helpers.

### Blocking client helpers

#### `xhttp_status_t XHTTP_Exchange(xhttp_t *pRequest, xhttp_t *pResponse, xsock_t *pSock)`

- Arguments:
  - `pRequest`: already assembled request object.
  - `pResponse`: destination response object.
  - `pSock`: connected blocking socket.
- Does:
  - writes `pRequest->rawData` as-is.
  - forwards callback configuration from request to response.
  - receives the response.
- Returns:
  - `XHTTP_EFDMODE` for non-blocking sockets.
  - `XHTTP_EWRITE` on write failure.
  - `XHTTP_TERMINATED` when the write callback aborts.
  - otherwise the receive status.

#### `xhttp_status_t XHTTP_Connect(xhttp_t *pHttp, xsock_t *pSock, xlink_t *pLink)`

- Arguments:
  - `pHttp`: request object, mainly for callback/error reporting and optional Unix socket path.
  - `pSock`: destination socket object.
  - `pLink`: parsed link info.
- Does:
  - defaults missing protocol to `http`.
  - supports `http`, `https` and Unix-domain sockets via `pHttp->sUnixAddr`.
  - resolves the host, applies timeout, optionally inserts Basic auth from the link and opens the socket.
- Returns:
  - `XHTTP_CONNECTED` on success.
  - `XHTTP_EPROTO`, `XHTTP_ERESOLVE`, `XHTTP_EAUTH`, `XHTTP_ECONNECT`, `XHTTP_ETIMEO`, etc. on failure.

#### `xhttp_status_t XHTTP_LinkExchange(xhttp_t *pRequest, xhttp_t *pResponse, xlink_t *pLink)`

#### `xhttp_status_t XHTTP_EasyExchange(xhttp_t *pRequest, xhttp_t *pResponse, const char *pLink)`

- Arguments:
  - request/response objects plus a parsed or raw link.
- Does:
  - opens a temporary connection, exchanges the request and closes the socket.
- Returns:
  - exchange status.
  - `XHTTP_ELINK` when the raw link cannot be parsed.

#### `xhttp_status_t XHTTP_Perform(xhttp_t *pHttp, xsock_t *pSock, const uint8_t *pBody, size_t nLength)`

- Arguments:
  - `pHttp`: request object reused as the response object afterward.
  - `pSock`: connected blocking socket.
  - `pBody`, `nLength`: optional body to assemble.
- Does:
  - assembles the request.
  - writes it.
  - soft-resets the same object.
  - receives the response back into that object.
- Returns:
  - `XHTTP_EFDMODE`, `XHTTP_EASSEMBLE`, `XHTTP_EWRITE`, `XHTTP_TERMINATED`, or receive status.

#### `xhttp_status_t XHTTP_LinkPerform(xhttp_t *pHttp, xlink_t *pLink, const uint8_t *pBody, size_t nLength)`

#### `xhttp_status_t XHTTP_EasyPerform(xhttp_t *pHttp, const char *pLink, const uint8_t *pBody, size_t nLength)`

- Arguments:
  - request/response object plus parsed or raw link and optional body.
- Does:
  - wraps `XHTTP_Connect()` + `XHTTP_Perform()` + socket close.
- Returns:
  - perform status.
  - `XHTTP_ELINK` for invalid raw links.

#### `xhttp_status_t XHTTP_SoloPerform(xhttp_t *pHttp, xhttp_method_t eMethod, const char *pLink, const uint8_t *pBody, size_t nLength)`

- Arguments:
  - `pHttp`: object to initialize and reuse.
  - `eMethod`: request method.
  - `pLink`: raw URL.
  - `pBody`, `nLength`: optional body.
- Does:
  - parses the link.
  - initializes the request with the link URI.
  - auto-adds `Host` and `User-Agent: xutils/<version>`.
  - performs the request.
- Returns:
  - `XHTTP_EINIT` when request init fails.
  - `XHTTP_ESETHDR` on header insertion failure.
  - `XHTTP_EEXISTS` if `Host` or `User-Agent` already exists with a conflicting value.
  - otherwise the `LinkPerform()` status.
