# api.c

## Purpose

High-level event/runtime wrapper over `event.c`, `sock.c`, `http.c`, `mdtp.c` and `ws.c`.

## Callback Contract

- `XAPI_CB_INTERRUPT` is only the underlying event-loop interrupt path, typically `EINTR`/signal related.
- `XAPI_CB_TICK` runs once after every successful `XAPI_Service()` cycle.
- `XAPI_CB_USER` can be triggered explicitly when the callback returns `XAPI_USER_CB`.
- Return mapping is:
  - `XAPI_CONTINUE` / `XAPI_NO_ACTION` or any non-negative non-special value: keep servicing.
  - `< XAPI_NO_ACTION`: disconnect.
  - `XAPI_RELOOP`: request `XEVENTS_RELOOP`.
  - `XAPI_USER_CB`: immediately dispatch `XAPI_CB_USER` and re-map that callback result.
- During `XAPI_CB_TICK`, a disconnect-style result becomes `XEVENTS_EBREAK`.

## API Reference

### Status and type helpers

#### `const char *XAPI_GetStatusStr(xapi_status_t eStatus)`

- Arguments:
  - `eStatus`: `XAPI_*` status enum.
- Does:
  - converts API-local status to a human-readable string.
- Returns:
  - pointer to a static string.

#### `const char *XAPI_GetTypeStr(xapi_type_t eType)`

- Arguments:
  - `eType`: `XAPI_SELF`, `XAPI_EVENT`, `XAPI_HTTP`, `XAPI_MDTP`, `XAPI_SOCK` or `XAPI_WS`.
- Does:
  - converts the status source/type enum to text.
- Returns:
  - pointer to a static string.

#### `const char *XAPI_GetStatus(xapi_ctx_t *pCtx)`

- Arguments:
  - `pCtx`: callback context produced by the runtime.
- Does:
  - dispatches to the correct status-string helper based on `pCtx->eStatType`.
- Returns:
  - the resolved status string.
  - `"Invalid API context"` when `pCtx == NULL`.

#### `xbool_t XAPI_IsDestroyEvent(xapi_ctx_t *pCtx)`

- Arguments:
  - `pCtx`: callback context.
- Does:
  - checks whether the callback describes runtime destruction.
- Returns:
  - `XTRUE` only for `eStatType == XAPI_SELF` and `nStatus == XAPI_DESTROY`.
  - `XFALSE` for `NULL` or any other event.

### Buffer access

#### `xbyte_buffer_t *XAPI_GetTxBuff(xapi_session_t *pSession)`

#### `xbyte_buffer_t *XAPI_GetRxBuff(xapi_session_t *pSession)`

- Arguments:
  - `pSession`: active API session.
- Does:
  - returns the session-owned TX or RX buffer.
- Returns:
  - internal buffer pointer.
  - `NULL` when `pSession == NULL`.

#### `XSTATUS XAPI_PutTxBuff(xapi_session_t *pSession, xbyte_buffer_t *pBuffer)`

- Arguments:
  - `pSession`: destination session.
  - `pBuffer`: source buffer whose used bytes should be appended.
- Does:
  - appends `pBuffer->pData[0..nUsed)` into the session TX buffer.
  - emits `XAPI_ERR_ALLOC` through the error callback if the append leaves the destination empty.
- Returns:
  - `XSTDOK` on success.
  - `XSTDNON` when the source buffer has no used bytes.
  - `XSTDINV` on `NULL` arguments.
  - `XSTDERR` on allocation/append failure.

### Runtime lifecycle

#### `XSTATUS XAPI_Init(xapi_t *pApi, xapi_cb_t callback, void *pUserCtx)`

- Arguments:
  - `pApi`: runtime object to initialize.
  - `callback`: application callback, may be `NULL`.
  - `pUserCtx`: opaque caller context stored on the runtime.
- Does:
  - resets runtime fields, enables hashmap-backed events by default, disables missing-WebSocket-key mode and sets the default RX cap to `5000 * 1024`.
- Returns:
  - `XSTDOK` on success.
  - `XSTDINV` when `pApi == NULL`.

#### `XSTATUS XAPI_SetRxSize(xapi_t *pApi, size_t nSize)`

- Arguments:
  - `pApi`: runtime.
  - `nSize`: maximum buffered RX bytes before protocol handlers disconnect incomplete packets.
- Does:
  - overrides the RX ceiling.
  - restores the built-in default when `nSize == 0`.
- Returns:
  - `XSTDOK` or `XSTDINV`.

#### `void XAPI_Destroy(xapi_t *pApi)`

- Arguments:
  - `pApi`: runtime.
- Does:
  - destroys the underlying `XEvents` instance if it exists.
  - session cleanup happens through event clear callbacks.
- Returns:
  - no return value.

#### `size_t XAPI_GetEventCount(xapi_t *pApi)`

- Arguments:
  - `pApi`: runtime.
- Does:
  - returns the current event count from `pApi->events`.
- Returns:
  - event count.
  - `0` when `pApi == NULL`.

### Session and timer control

#### `XSTATUS XAPI_Disconnect(xapi_session_t *pSession)`

- Arguments:
  - `pSession`: registered session.
- Does:
  - requests deletion of the session's event from the underlying event loop.
- Returns:
  - `XSTDOK` when there is no event handle or deletion succeeds.
  - `XSTDINV` when the session or owning API is missing.
  - `XSTDERR` when `XEvents_Delete()` fails.

#### `XSTATUS XAPI_DeleteTimer(xapi_session_t *pSession)`

- Arguments:
  - `pSession`: session that may own a timer.
- Does:
  - deletes the session timer event.
  - later clear-path cleanup emits `XAPI_TIMER_DESTROY`.
- Returns:
  - `XSTDOK` when no timer exists or deletion succeeds.
  - `XSTDINV` on invalid session/API.
  - `XSTDERR` on event-layer failure.

#### `XSTATUS XAPI_AddTimer(xapi_session_t *pSession, int nTimeoutMs)`

- Arguments:
  - `pSession`: session that should own the timer.
  - `nTimeoutMs`: positive timeout in milliseconds.
- Does:
  - creates a new timer event.
  - if the session already has a timer, forwards to `XAPI_ExtendTimer()`.
- Returns:
  - `XSTDOK` on success.
  - `XSTDNON` when `nTimeoutMs <= 0`.
  - `XSTDINV` on invalid session/API.
  - `XSTDERR` when timer registration fails.

#### `XSTATUS XAPI_ExtendTimer(xapi_session_t *pSession, int nTimeoutMs)`

- Arguments:
  - `pSession`: session with an existing timer.
  - `nTimeoutMs`: new timeout value.
- Does:
  - re-arms the timer via `XEvents_ExtendTimer()`.
  - emits an `XAPI_EVENT` error callback if re-arm fails.
- Returns:
  - `XSTDOK` on success.
  - `XSTDNON` when `nTimeoutMs <= 0`.
  - `XSTDINV` when the session/API/timer is missing.
  - `XSTDERR` on event-layer failure.

#### `XSTATUS XAPI_SetEvents(xapi_session_t *pSession, int nEvents)`

- Arguments:
  - `pSession`: registered session.
  - `nEvents`: new poll mask.
- Does:
  - modifies the live event mask through `XEvents_Modify()`.
  - updates `pSession->nEvents` only after successful modification.
- Returns:
  - `XSTDOK` on success.
  - `XSTDINV` for missing session/API/event handle.
  - `XSTDERR` when no events backend exists or modification fails.

#### `XSTATUS XAPI_EnableEvent(xapi_session_t *pSession, int nEvent)`

#### `XSTATUS XAPI_DisableEvent(xapi_session_t *pSession, int nEvent)`

- Arguments:
  - `pSession`: registered session.
  - `nEvent`: bit to enable or disable.
- Does:
  - updates the session poll mask only when the bit state actually changes.
- Returns:
  - `XSTDOK` if no change is needed or the update succeeds.
  - otherwise the result of `XAPI_SetEvents()`.

### HTTP helpers

#### `XSTATUS XAPI_RespondHTTP(xapi_session_t *pSession, int nCode, xapi_status_t eStatus)`

- Arguments:
  - `pSession`: HTTP session.
  - `nCode`: HTTP response code, typically `401`, `403`, `500`, etc.
  - `eStatus`: optional API status to expose in the JSON body.
- Does:
  - builds a JSON body of the form `{"status": "..."}`
  - sets `WWW-Authenticate` for `XAPI_MISSING_TOKEN`
  - queues the serialized HTTP response into the session TX buffer
  - enables `XPOLLOUT`
  - sets `bCancel` on assembly failure
- Returns:
  - a mapped event-style result, not just plain `XSTD*`.
  - typically `XEVENTS_CONTINUE` when queuing succeeds.
  - `XEVENTS_DISCONNECT` when response assembly or queueing fails.

#### `XSTATUS XAPI_AuthorizeHTTP(xapi_session_t *pSession, const char *pToken, const char *pKey)`

- Arguments:
  - `pSession`: session whose `pPacket` currently points at an `xhttp_t`.
  - `pToken`: expected HTTP Basic token payload after `Basic `, may be `NULL`.
  - `pKey`: expected `X-API-KEY` value, may be `NULL`.
- Does:
  - validates the request packet against the provided token and/or key.
  - sends a `401` JSON response through `XAPI_RespondHTTP()` on failure.
- Returns:
  - `XSTDOK` when no credentials are required or the request matches.
  - `XSTDINV` when session or packet is missing.
  - otherwise the event-style return from `XAPI_RespondHTTP()`.

### Endpoint helpers

#### `void XAPI_InitEndpoint(xapi_endpoint_t *pEndpt)`

- Arguments:
  - `pEndpt`: endpoint descriptor.
- Does:
  - zeroes fields, initializes certificate settings, clears role/type and sets `nFD` to `XSOCK_INVALID`.
- Returns:
  - no return value.

#### `XSTATUS XAPI_Listen(xapi_t *pApi, xapi_endpoint_t *pEndpt)`

- Arguments:
  - `pApi`: runtime.
  - `pEndpt`: listener descriptor.
- Does:
  - allocates a server session.
  - creates a non-blocking TCP or Unix listener, optionally TLS-enabled.
  - registers it in the event loop and emits `XAPI_CB_LISTENING`.
- Returns:
  - `XSTDOK` on success.
  - `XSTDINV` for invalid endpoint fields.
  - `XSTDERR` on allocation, socket, event-registration or callback failure.

#### `XSTATUS XAPI_Connect(xapi_t *pApi, xapi_endpoint_t *pEndpt)`

- Arguments:
  - `pApi`: runtime.
  - `pEndpt`: client descriptor.
- Does:
  - allocates a client session.
  - creates a non-blocking TCP or Unix client socket, optionally TLS-enabled.
  - registers it in the event loop and emits `XAPI_CB_CONNECTED`.
- Returns:
  - `XSTDOK` on success.
  - `XSTDINV` for invalid args.
  - `XSTDERR` on allocation, socket, registration or callback failure.

#### `XSTATUS XAPI_AddEvent(xapi_t *pApi, xapi_endpoint_t *pEndpt)`

- Arguments:
  - `pApi`: runtime.
  - `pEndpt`: descriptor containing an already-open `nFD`.
- Does:
  - wraps an existing file descriptor into an API session with role `SERVER`, `CLIENT`, `PEER` or `CUSTOM`.
  - marks the socket as `XSOCK_EVENT | XSOCK_NB` plus protocol bits.
  - emits `XAPI_CB_REGISTERED`.
- Returns:
  - `XSTDOK` on success.
  - `XSTDINV` when `nFD` or required endpoint fields are invalid.
  - `XSTDERR` on allocation, event creation or callback failure.

#### `XSTATUS XAPI_AddPeer(xapi_t *pApi, xapi_endpoint_t *pEndpt)`

- Arguments:
  - same as `XAPI_AddEvent`.
- Does:
  - forces `pEndpt->eRole = XAPI_PEER` and forwards to `XAPI_AddEvent()`.
- Returns:
  - forwarded status.

#### `XSTATUS XAPI_AddEndpoint(xapi_t *pApi, xapi_endpoint_t *pEndpt)`

- Arguments:
  - `pApi`: runtime.
  - `pEndpt`: endpoint descriptor.
- Does:
  - dispatches by role:
    - `XAPI_SERVER` -> `XAPI_Listen()`
    - `XAPI_CLIENT` -> `XAPI_Connect()`
    - `XAPI_PEER` / `XAPI_CUSTOM` -> `XAPI_AddEvent()`
- Returns:
  - forwarded status from the chosen helper.
  - `XSTDERR` for unsupported roles.

### Event loop

#### `xevent_status_t XAPI_Service(xapi_t *pApi, int nTimeoutMs)`

- Arguments:
  - `pApi`: initialized runtime with an active events backend.
  - `nTimeoutMs`: poll timeout passed to `XEvents_Service()`.
- Does:
  - services the underlying event loop.
  - if that cycle succeeds, emits `XAPI_CB_TICK`.
- Returns:
  - the raw `XEvents_Service()` status when the event loop itself fails or times out.
  - `XEVENTS_SUCCESS` when servicing and tick both succeed.
  - `XEVENTS_EBREAK` when the tick callback requests disconnect/break behavior.

## Important Notes

- `XAPI_CB_TICK` is the periodic per-cycle hook. `XAPI_CB_INTERRUPT` is not.
- `XAPI_RespondHTTP()` and `XAPI_AuthorizeHTTP()` return event-style values even though their signature uses `XSTATUS`.
- For raw sockets, `pSession->pPacket` is the RX buffer itself and `bKeepRxBuffer` decides whether the runtime clears it after `XAPI_CB_READ`.
- Protocol handlers recurse through buffered extra packets, so one read event can dispatch multiple `XAPI_CB_READ` callbacks before returning.
