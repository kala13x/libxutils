# sync.c

## Purpose

Cross-platform mutexes, rwlocks, atomic helpers and a tiny bar/ack primitive.

## API Reference

#### `void xusleep(uint32_t nUsecs)`

- Arguments:
  - microseconds to sleep.
- Does:
  - sleeps with `usleep()` on POSIX.
  - uses millisecond-granularity `Sleep()` on Windows.
- Returns:
  - no return value.

#### `void XSync_Init(xsync_mutex_t *pSync)`

#### `void XSync_Destroy(xsync_mutex_t *pSync)`

#### `void XSync_Lock(xsync_mutex_t *pSync)`

#### `void XSync_Unlock(xsync_mutex_t *pSync)`

- Arguments:
  - mutex object.
- Does:
  - initializes a recursive mutex / critical section and controls it.
  - operations are skipped when `bEnabled == XFALSE`.
- Returns:
  - no return value.
- Failure behavior:
  - initialization, destroy, lock and unlock errors print to `stderr` and terminate the process.

#### `void XRWSync_Init(xsync_rw_t *pSync)`

#### `void XRWSync_ReadLock(xsync_rw_t *pSync)`

#### `void XRWSync_WriteLock(xsync_rw_t *pSync)`

#### `void XRWSync_Unlock(xsync_rw_t *pSync)`

#### `void XRWSync_Destroy(xsync_rw_t *pSync)`

- Arguments:
  - rwlock object.
- Does:
  - initializes, locks, unlocks and destroys a read/write lock.
  - Windows tracks whether the held lock is exclusive so unlock can release the correct SRW mode.
- Returns:
  - no return value.
- Failure behavior:
  - underlying OS failures print to `stderr` and terminate the process.

#### `void XSyncBar_Bar(xsync_bar_t *pBar)`

#### `void XSyncBar_Ack(xsync_bar_t *pBar)`

#### `void XSyncBar_Reset(xsync_bar_t *pBar)`

- Arguments:
  - bar/ack state object.
- Does:
  - set bar and clear ack, set ack, or clear both flags.
- Returns:
  - no return value.

#### `uint8_t XSyncBar_CheckBar(xsync_bar_t *pBar)`

#### `uint8_t XSyncBar_CheckAck(xsync_bar_t *pBar)`

- Arguments:
  - bar/ack state object.
- Does:
  - reads the atomic flag.
- Returns:
  - `XSTDOK` when the flag is set.
  - `XSTDNON` otherwise.

#### `uint32_t XSyncBar_WaitAck(xsync_bar_t *pBar, uint32_t nSleepUsec)`

- Arguments:
  - bar/ack state object.
  - optional sleep interval between polls.
- Does:
  - busy-waits until ack becomes set.
- Returns:
  - approximate waited microseconds.

## Important Notes

- These primitives are designed for internal runtime code, not for failure-tolerant library boundaries.
- On Windows, `xusleep()` rounds sub-millisecond waits up to at least `1 ms`.
