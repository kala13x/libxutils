# event.c

## Purpose

Cross-platform event loop over `epoll`, `poll` or `WSAPoll`, with timer support.

## API Reference

### Status / byte helpers

#### `const char *XEvents_GetStatusStr(xevent_status_t status)`

- Returns static text for event status code.

#### `int XEvent_WriteByte(xevent_data_t *pData, const char cVal)`

#### `int XEvent_ReadByte(xevent_data_t *pData, char *pVal)`

#### `int XEvent_ReadU64(xevent_data_t *pData, uint64_t *pVal)`

- Read/write eventfd/timerfd-style payload bytes.
- Return OS read/write status or negative error.

### Lifecycle

#### `xevent_status_t XEvents_Create(xevents_t *pEvents, uint32_t nMax, void *pUser, xevent_cb_t callBack, xbool_t bUseHash)`

- Initializes event arrays, callback and optional fd hash map.
- Returns `XEVENTS_SUCCESS` or failure status.

#### `void XEvents_Destroy(xevents_t *pEvents)`

- Frees arrays, timers and registered-event state.

### Event data creation

#### `xevent_data_t *XEvents_NewData(void *pCtx, XSOCKET nFd, int nType)`

- Allocates one event-data object.
- Returns object or `NULL`.

#### `xevent_data_t *XEvents_GetData(xevents_t *pEvents, XSOCKET nFd)`

- Returns registered data for fd or `NULL`.

#### `xevent_data_t *XEvents_CreateEvent(xevents_t *pEvents, void *pCtx)`

- Creates a manual/eventfd-style event source when supported.
- Returns event data or `NULL`.

#### `xevent_data_t *XEvents_AddTimer(xevents_t *pEvents, void *pContext, int nTimeoutMs)`

- Creates and registers timer event.
- Returns timer handle or `NULL`.

#### `xevent_status_t XEvents_ExtendTimer(xevents_t *pEvents, xevent_data_t *pTimer, int nTimeoutMs)`

- Rearms timer.
- Returns success/failure status.

### Register / modify / delete

#### `xevent_data_t *XEvents_RegisterEvent(xevents_t *pEvents, void *pCtx, XSOCKET nFd, int nEvents, int nType)`

- Allocates event data and registers it.
- Returns event handle or `NULL`.

#### `xevent_status_t XEvents_Add(xevents_t *pEvents, xevent_data_t *pData, int nEvents)`

#### `xevent_status_t XEvents_Modify(xevents_t *pEvents, xevent_data_t *pData, int nEvents)`

#### `xevent_status_t XEvents_Delete(xevents_t *pEvents, xevent_data_t *pData)`

- Add/change/remove watched fd or timer.
- Return `XEVENTS_SUCCESS` or specific failure status.

### Service loop

#### `xevent_status_t XEvents_Service(xevents_t *pEvents, int nTimeoutMs)`

- Waits for events, dispatches callbacks and processes timer expiry.
- Returns:
  - `XEVENTS_SUCCESS` on a normal cycle, including plain timeout/no events.
  - `XEVENTS_EINTR`-family behavior only via callback/interrupt path.
  - other `xevent_status_t` values on backend or callback-driven termination.
