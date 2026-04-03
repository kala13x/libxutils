# sig.c

## Purpose

Signal registration, crash backtrace and daemonization helpers.

## API Reference

#### `void XUtils_Backtrace(void)`

- Arguments:
  - none.
- Does:
  - on Linux, captures up to `XUTILS_BACKTRACE_SIZE` frames and logs them.
  - on other platforms, effectively does nothing.
- Returns:
  - no return value.

#### `void XUtils_ErrExit(const char *pFmt, ...)`

- Arguments:
  - optional printf-style error message.
- Does:
  - logs the formatted message when provided.
  - exits the process with `EXIT_FAILURE`.
- Returns:
  - no return value because the process terminates.

#### `int XUtils_Daemonize(int bNoChdir, int bNoClose)`

- Arguments:
  - `bNoChdir`: keep current directory when non-zero.
  - `bNoClose`: keep stdin/stdout/stderr open when non-zero.
- Does:
  - Linux: calls `daemon()`.
  - Windows: starts a detached process and optionally changes directory/closes std handles.
  - other POSIX: double-forks, creates a new session, optionally `chdir("/")` and redirects stdio to `/dev/null`.
- Returns:
  - OS success code, usually `0` on success.
  - `-1` on failure.

#### `int XSig_Register(int *pSignals, size_t nCount, xsig_cb_t callback)`

- Arguments:
  - signal-number array, count and handler callback.
- Does:
  - installs the given handler for each listed signal.
- Returns:
  - `0` on success.
  - on POSIX, the first signal number whose `sigaction()` registration failed.

#### `int XSig_RegExitSigs(void)`

- Arguments:
  - none.
- Does:
  - registers the built-in exit callback for:
    - `SIGINT`
    - `SIGILL`
    - `SIGSEGV`
    - `SIGTERM`
    - `SIGBUS` on Linux
- Returns:
  - forwarded result from `XSig_Register()`.

## Built-In Handler Behavior

- `XSig_Callback()` is internal but important:
  - crash-like signals (`SIGSEGV`, `SIGILL`, `SIGBUS`) trigger `XUtils_Backtrace()`
  - `SIGINT` and `SIGTERM` log an informational message
  - every handled signal ends with `exit(EXIT_FAILURE)`

## Important Notes

- This module is intentionally exit-oriented; it is not suitable for recoverable signal handling.
