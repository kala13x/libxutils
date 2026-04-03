# thread.c

## Purpose

Thread helpers plus a detached repeating-task wrapper.

## API Reference

#### `void XThread_Init(xthread_t *pThread)`

- Arguments:
  - thread object.
- Does:
  - sets default stack size and clears callback, argument, detach and status fields.
- Returns:
  - no return value.

#### `int XThread_Run(xthread_t *pThread)`

- Arguments:
  - initialized thread object with `functionCb` and `pArgument` set.
- Does:
  - creates the OS thread.
  - uses detached thread creation on POSIX when `nDetached` is true.
  - closes the Windows handle immediately for detached threads.
- Returns:
  - `XSTDOK` on success.
  - `XSTDNON` when `functionCb == NULL`.
  - `XSTDERR` on thread-creation failure.

#### `int XThread_Create(xthread_t *pThread, xthread_cb_t func, void *pArg, uint8_t nDtch)`

- Arguments:
  - thread object, callback, callback argument and detached flag.
- Does:
  - initializes the object, stores parameters and runs the thread.
- Returns:
  - forwarded result from `XThread_Run()`.

#### `void *XThread_Join(xthread_t *pThread)`

- Arguments:
  - thread object.
- Does:
  - waits for a non-detached thread to finish.
- Returns:
  - `NULL` for detached threads.
  - joined thread return value on successful POSIX `pthread_join()`.
  - `NULL` on join failure.
- Caveat:
  - Windows path always returns `NULL` even after a successful wait because the implementation does not preserve a thread return value.

### Task helpers

#### `int XTask_Start(xtask_t *pTask, xtask_cb_t callback, void *pContext, uint32_t nIntervalU)`

- Arguments:
  - task object, callback, callback context and per-iteration sleep interval in microseconds.
- Does:
  - stores callback and context.
  - sets action to `XTASK_CTRL_RELEASE`.
  - spawns a detached worker thread.
- Returns:
  - `0` when `callback == NULL`.
  - otherwise the thread-creation status from `XThread_Create()`.

#### `uint32_t XTask_Hold(xtask_t *pTask, int nIntervalU)`

#### `uint32_t XTask_Release(xtask_t *pTask, int nIntervalU)`

#### `uint32_t XTask_Stop(xtask_t *pTask, int nIntervalU)`

- Arguments:
  - task object and poll sleep interval used while waiting for the target state.
- Does:
  - updates the control action to pause, release or stop.
  - waits until worker status becomes `XTASK_STAT_PAUSED`, `XTASK_STAT_ACTIVE` or `XTASK_STAT_STOPPED`.
- Returns:
  - approximate waited microseconds.

## Worker Semantics

- the worker thread is internal but important:
  - sets status to `XTASK_STAT_ACTIVE` on start
  - `XTASK_CTRL_PAUSE` switches status to `XTASK_STAT_PAUSED` and sleeps
  - `XTASK_CTRL_RELEASE` resumes active callback execution
  - callback return `< 0` stops the worker
  - stop sets status to `XTASK_STAT_STOPPED`

## Important Notes

- tasks are detached by design; `Stop()` is the intended shutdown path.
- wait helpers can busy-spin when their interval argument is `0`.
