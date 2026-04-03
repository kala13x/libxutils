# cli.c

## Purpose

Terminal input helpers, line-buffered window rendering and CLI progress-bar output.

## API Reference

### Input helpers

#### `XSTATUS XCLI_SetInputMode(void *pAttributes)`

- Arguments:
  - `pAttributes`: caller storage for the previous terminal attributes, expected to be `struct termios *` on Linux.
- Does:
  - on Linux, switches stdin to non-canonical, no-echo mode and stores the old settings.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` when stdin is not a tty or `termios` calls fail.
  - `XSTDNON` on unsupported platforms.

#### `XSTATUS XCLI_RestoreAttributes(void *pAttributes)`

- Arguments:
  - `pAttributes`: saved attribute block from `XCLI_SetInputMode()`.
- Does:
  - restores the previous terminal mode on Linux.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` for `NULL` attributes on Linux.
  - `XSTDNON` on unsupported platforms.

#### `XSTATUS XCLI_ReadStdin(char *pBuffer, size_t nSize, xbool_t bAsync)`

- Arguments:
  - `pBuffer`: destination buffer.
  - `nSize`: buffer size.
  - `bAsync`: whether to force `O_NONBLOCK` on Linux before reading.
- Does:
  - reads raw bytes from stdin.
  - NUL-terminates the buffer when `nSize > 1`.
- Returns:
  - positive byte count on success.
  - `XSTDINV` for invalid args.
  - `XSTDNON` when async mode would block.
  - `XSTDERR` on I/O failure.

#### `XSTATUS XCLI_GetChar(char *pChar, xbool_t bAsync)`

- Arguments:
  - `pChar`: single-byte destination.
  - `bAsync`: pass-through async flag.
- Does:
  - reads one character from stdin on Linux by forwarding to `XCLI_ReadStdin()`.
- Returns:
  - forwarded read status.
  - `XSTDERR` for `NULL` output on Linux.
  - `XSTDNON` on unsupported platforms.

#### `XSTATUS XCLI_GetPass(const char *pText, char *pPass, size_t nSize)`

- Arguments:
  - `pText`: optional prompt.
  - `pPass`: destination buffer.
  - `nSize`: destination size.
- Does:
  - prints the prompt if provided.
  - reads one line from stdin.
  - disables terminal echo on non-Windows builds while reading.
  - strips the trailing newline.
- Returns:
  - entered length on success.
  - `XSTDERR` on invalid args or terminal/read failure.

#### `XSTATUS XCLI_GetInput(const char *pText, char *pInput, size_t nSize, xbool_t bCutNewLine)`

- Arguments:
  - prompt string, destination buffer, size and newline-trim flag.
- Does:
  - prints the prompt.
  - reads one line with `fgets()`.
  - optionally removes the trailing newline.
- Returns:
  - `XSTDOK` on successful read.
  - `XSTDNON` when the resulting input is empty.
  - `XSTDINV` for invalid destination.
  - `XSTDERR` when `fgets()` fails.

#### `XSTATUS XCLI_GetWindowSize(xcli_size_t *pSize)`

- Arguments:
  - `pSize`: output rows/columns struct.
- Does:
  - queries terminal size from the OS.
- Returns:
  - `XSTDOK` when both rows and columns are non-zero.
  - `XSTDERR` otherwise.

#### `size_t XCLI_CountFormat(xcli_size_t *pSize, const char *pLine, size_t nLength, size_t *pPosit)`

- Header status:
  - declared in `cli.h`.
- Implementation status:
  - no implementation exists in `cli.c`.
- Guidance:
  - do not rely on this symbol unless the library adds it.

### Window helpers

#### `void XCLIWin_Init(xcli_win_t *pWin, xbool_t bAscii)`

- Arguments:
  - `pWin`: window object.
  - `bAscii`: whether clear-screen should use ANSI escape output instead of `system("clear"/"cls")`.
- Does:
  - initializes the line array.
  - sets render mode to `XCLI_RENDER_FRAME`.
- Returns:
  - no return value.

#### `void XCLIWin_Destroy(xcli_win_t *pWin)`

- Arguments:
  - `pWin`: window object.
- Does:
  - destroys the internal line array.
- Returns:
  - no return value.

#### `XSTATUS XCLIWin_UpdateSize(xcli_win_t *pWin)`

- Arguments:
  - `pWin`: window object.
- Does:
  - refreshes terminal dimensions.
  - subtracts one row when display mode is `XCLI_LINE_BY_LINE`.
- Returns:
  - `XSTDOK` or `XSTDERR`.

#### `int XCLIWin_ClearScreen(xbool_t bAscii)`

- Arguments:
  - `bAscii`: whether to print ANSI clear escape directly.
- Does:
  - clears the terminal using ANSI escape codes or `system("clear"/"cls")`.
- Returns:
  - `XSTDNON` on the ANSI path.
  - shell command return code on the `system()` path.

#### `XSTATUS XCLIWin_AddLine(xcli_win_t *pWin, char *pLine, size_t nLength)`

- Arguments:
  - `pWin`: window.
  - `pLine`: line bytes to copy.
  - `nLength`: number of bytes to copy.
- Does:
  - refreshes window size.
  - appends a copied line into the internal array.
- Returns:
  - `XSTDOK` on success.
  - `XSTDNON` when the current row budget is already full.
  - `XSTDERR` on size-query or allocation failure.

#### `XSTATUS XCLIWin_AddLineFmt(xcli_win_t *pWin, const char *pFmt, ...)`

- Arguments:
  - `pWin`: window.
  - `pFmt`: printf-style line format.
- Does:
  - formats a heap string and pushes ownership into the internal array.
- Returns:
  - `XSTDOK`, `XSTDNON` or `XSTDERR` with the same capacity/error rules as `AddLine()`.

#### `XSTATUS XCLIWin_AddEmptyLine(xcli_win_t *pWin)`

- Arguments:
  - `pWin`: window.
- Does:
  - creates one line filled with spaces up to terminal width and appends it.
- Returns:
  - result of `XCLIWin_AddLine()`.

#### `XSTATUS XCLIWin_AddAligned(xcli_win_t *pWin, const char *pInput, const char *pFmt, uint8_t nAlign)`

- Arguments:
  - `pWin`: window.
  - `pInput`: visible text.
  - `pFmt`: optional leading ANSI/style prefix.
  - `nAlign`: `XCLI_LEFT`, `XCLI_RIGHT` or `XCLI_CENTER`.
- Does:
  - pads the input to the full window width.
  - optionally wraps the line between `pFmt` and `XSTR_FMT_RESET`.
- Returns:
  - `XSTDERR` on empty input or size failure.
  - otherwise the result of `XCLIWin_AddLineFmt()`.

#### `XSTATUS XCLIWin_GetFrame(xcli_win_t *pWin, xbyte_buffer_t *pFrameBuff)`

- Arguments:
  - `pWin`: window.
  - `pFrameBuff`: output byte buffer.
- Does:
  - initializes the output buffer.
  - pads missing rows with empty lines.
  - renders each stored line to full width and concatenates them into one frame buffer.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` on any render/append failure.

#### `XSTATUS XCLIWin_Display(xcli_win_t *pWin)`

- Arguments:
  - `pWin`: window.
- Does:
  - for `XCLI_LINE_BY_LINE`, prints each rendered line followed by enough blank lines to fill the visible rows.
  - for `XCLI_RENDER_FRAME`, builds a full frame and prints it after clearing the screen.
- Returns:
  - `XSTDOK` on successful render.
  - `XSTDERR` on render failure.
  - `XSTDNON` when the display type is not handled.

#### `XSTATUS XCLIWin_Flush(xcli_win_t *pWin)`

- Arguments:
  - `pWin`: window.
- Does:
  - displays current content and then clears the internal line array.
- Returns:
  - result of `XCLIWin_Display()`.

### Progress bar helpers

#### `void XProgBar_GetDefaults(xcli_bar_t *pCtx)`

- Arguments:
  - `pCtx`: progress-bar context.
- Does:
  - sets default interval, glyphs, flags and empty prefix/suffix strings.
  - refreshes the current window size.
- Returns:
  - no return value.

#### `XSTATUS XProgBar_UpdateWindowSize(xcli_bar_t *pCtx)`

- Arguments:
  - `pCtx`: progress-bar context.
- Does:
  - refreshes `pCtx->frame`.
- Returns:
  - `XSTDOK` or `XSTDERR`.

#### `xbool_t XProgBar_CalculateBounds(xcli_bar_t *pCtx)`

- Arguments:
  - `pCtx`: progress-bar context.
- Does:
  - clamps `fPercent` into `[0, 100]`.
  - builds the percent label.
  - computes visible bar width and used width.
  - decides whether the percent label must be hidden to fit the terminal width.
- Returns:
  - `XTRUE` when the percent label should be hidden.
  - `XFALSE` otherwise.

#### `size_t XProgBar_GetOutputAdv(xcli_bar_t *pCtx, char *pDst, size_t nSize, char *pProgress, xbool_t bHidePct)`

- Arguments:
  - `pCtx`: progress-bar context.
  - `pDst`, `nSize`: destination buffer.
  - `pProgress`: optional prebuilt progress string.
  - `bHidePct`: whether percent text should be omitted.
- Does:
  - renders a formatted progress bar string with ANSI bold/reset codes.
  - if `pProgress == NULL`, builds progress from `nBarUsed`.
- Returns:
  - written length.

#### `size_t XProgBar_GetOutput(xcli_bar_t *pCtx, char *pDst, size_t nSize)`

- Arguments:
  - same as `GetOutputAdv()`, minus custom progress input.
- Does:
  - calls `XProgBar_CalculateBounds()` and then renders the output.
- Returns:
  - written length.

#### `void XProgBar_MakeMove(xcli_bar_t *pCtx)`

- Arguments:
  - `pCtx`: progress-bar context, typically with `fPercent < 0`.
- Does:
  - prints an indeterminate moving bar directly to stdout.
  - updates `nPosition`, `bReverse` and `nLastTime`.
- Returns:
  - no return value.

#### `void XProgBar_Update(xcli_bar_t *pCtx)`

- Arguments:
  - `pCtx`: progress-bar context.
- Does:
  - if `fPercent < 0`, forwards to `XProgBar_MakeMove()`.
  - otherwise renders a determinate bar and prints it with trailing `\r`.
  - calls `XProgBar_Finish()` automatically when `fPercent == 100`.
- Returns:
  - no return value.

#### `void XProgBar_Finish(xcli_bar_t *pCtx)`

- Arguments:
  - `pCtx`: progress-bar context.
- Does:
  - when `bKeepBar` is true, only prints a newline.
  - otherwise prints a blanked-out final line and a newline.
- Returns:
  - no return value.

## Important Notes

- `XCLI_CountFormat()` is declared but not implemented.
- Most render helpers print directly to stdout; they do not return the rendered text unless you use `XCLIWin_GetFrame()` or `XProgBar_GetOutput*()`.
- Indeterminate progress mode is driven by `fPercent < 0`.

- Always restore terminal attributes after raw input mode.
- The window renderer is stateful and tied to current terminal size; update size before assuming cached geometry is still valid.
