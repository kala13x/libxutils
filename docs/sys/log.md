# log.c

## Purpose

Process-global singleton logger with screen, file and callback output.

## Initialization Defaults

`XLog_Init()` starts with:

- `XLOG_COLORING_TAG`
- time disabled
- screen enabled
- file disabled
- rotate enabled
- keep-open enabled
- heap formatting disabled
- indent disabled
- flush disabled

## API Reference

#### `xbool_t XLog_IsInit(void)`

- Arguments:
  - none.
- Does:
  - returns the global logger init flag.
- Returns:
  - `XTRUE` or `XFALSE`.

#### `void XLog_Init(const char *pName, uint16_t nFlags, xbool_t bTdSafe)`

- Arguments:
  - `pName`: file/logger name, defaults to `xlog`.
  - `nFlags`: enabled log-level mask.
  - `bTdSafe`: whether to initialize the internal mutex.
- Does:
  - initializes global config and file context.
  - enables Windows VT color support when possible.
  - creates the mutex only when `bTdSafe` is true.
- Returns:
  - no return value.

#### `void XLog_Destroy(void)`

- Arguments:
  - none.
- Does:
  - closes the log file.
  - clears config.
  - destroys the mutex and resets the init flag.
- Returns:
  - no return value.

#### `void XLog_ConfigGet(struct XLogConfig *pCfg)`

#### `void XLog_ConfigSet(struct XLogConfig *pCfg)`

- Arguments:
  - config struct pointer.
- Does:
  - `Get` copies the current config.
  - `Set` replaces the config and closes the active file handle if file path/name/output mode changed.
- Returns:
  - no return value.
- Note:
  - both functions do nothing when the logger is not initialized.

#### `void XLog_FlagEnable(xlog_flag_t eFlag)`

#### `void XLog_FlagDisable(xlog_flag_t eFlag)`

- Arguments:
  - log flag bit.
- Does:
  - enables or disables log levels in the global flag mask.
  - `FlagEnable(XLOG_NONE)` and `FlagEnable(XLOG_ALL)` replace the whole mask rather than OR-ing.
- Returns:
  - no return value.

#### `void XLog_CallbackSet(xlog_cb_t callback, void *pContext)`

- Arguments:
  - `callback`: output hook, may be `NULL`.
  - `pContext`: callback context.
- Does:
  - stores the callback and context.
- Returns:
  - no return value.

#### `void XLog_SeparatorSet(const char *pSeparator)`

- Arguments:
  - separator text placed between log info and the message body.
- Does:
  - stores `" <sep> "` in the config.
  - falls back to a single space when formatting fails.
- Returns:
  - no return value.

#### `void XLog_ColorFormatSet(xlog_coloring_t eFmt)`

#### `void XLog_TimeFormatSet(xlog_timing_t eFmt)`

#### `void XLog_IndentSet(xbool_t bEnable)`

#### `void XLog_ScreenLogSet(xbool_t bEnable)`

#### `void XLog_FileLogSet(xbool_t bEnable)`

#### `void XLog_FlushSet(xbool_t bEnable)`

#### `void XLog_TraceTid(xbool_t bEnable)`

#### `void XLog_UseHeap(xbool_t bEnable)`

- Arguments:
  - one config flag or enum.
- Does:
  - updates the corresponding global logger setting.
  - `FileLogSet(XFALSE)` also closes the current file handle.
- Returns:
  - no return value.

#### `void XLog_FlagsSet(uint16_t nFlags)`

#### `uint16_t XLog_FlagsGet(void)`

- Arguments:
  - flag mask for setter.
- Does:
  - sets or returns the enabled log-level mask.
- Returns:
  - setter: no return value.
  - getter: current mask, or `XSTDNON` when the logger is not initialized.

#### `size_t XLog_PathSet(const char *pPath)`

#### `size_t XLog_NameSet(const char *pName)`

- Arguments:
  - destination log directory or base file name.
- Does:
  - stores the new path or name.
  - closes the active file handle if the value changed.
- Returns:
  - copied length.
  - `XSTDNON` when the logger is not initialized.

#### `void XLog_Display(xlog_flag_t eFlag, xbool_t bNewLine, const char *pFmt, ...)`

- Arguments:
  - log level, newline flag and printf-style format string.
- Does:
  - locks the logger when thread safety is enabled.
  - captures current time.
  - formats the message using stack or heap mode.
  - routes the final output to:
    - callback
    - stdout
    - log file
  - rotates the file daily when enabled.
- Returns:
  - no return value.
- Callback routing rule:
  - callback return `> 0`: screen/file output continues normally.
  - callback return `0`: screen output is suppressed, file output still continues.
  - callback return `< 0`: file output is suppressed too.

#### `XSTATUS XLog_Throw(int nRetVal, const char *pFmt, ...)`

- Arguments:
  - return value to pass through.
  - optional format string.
- Does:
  - logs the message.
  - uses `XLOG_ERROR` when `nRetVal <= 0`, otherwise `XLOG_NONE`.
  - when `pFmt == NULL`, logs `XSTRERR`.
- Returns:
  - always returns `nRetVal`.

#### `XSTATUS XLog_Throwe(int nRetVal, const char *pFmt, ...)`

- Arguments:
  - same shape as `XLog_Throw()`.
- Does:
  - logs the message and appends `(<errno string>)` when a format is provided.
- Returns:
  - always returns `nRetVal`.

#### `void *XLog_ThrowPtr(void *pRetVal, const char *pFmt, ...)`

- Arguments:
  - pointer value to pass through.
  - optional format string.
- Does:
  - logs an error message.
- Returns:
  - always returns `pRetVal`.

## Important Notes

- The logger is singleton/global; all APIs mutate one shared state object.
