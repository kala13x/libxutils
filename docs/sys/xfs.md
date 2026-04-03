# xfs.c

## Purpose

File, path and directory helpers on top of POSIX/CRT primitives.

## File Flag Letters

`XFile_Open*()` parses these flag characters:

- `a`: append
- `c`: create
- `d`: non-delay
- `e`: exclusive
- `n`: non-blocking
- `r`: read only
- `t`: truncate
- `s`: sync
- `w`: write only
- `x`: read/write

## API Reference

### Low-level wrappers

#### `int xstat(const char *pPath, xstat_t *pStat)`

- Arguments:
  - path and destination stat struct.
- Does:
  - zeroes the destination and performs `lstat()` on POSIX or `stat()` on Windows.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` on failure.

#### `int xchmod(const char *pPath, xmode_t nMode)`

#### `int xmkdir(const char *pPath, xmode_t nMode)`

#### `int xunlink(const char *pPath)`

#### `int xrmdir(const char *pPath)`

#### `int xclose(int nFD)`

- Arguments:
  - path/mode or raw fd as appropriate.
- Does:
  - thin OS wrapper around chmod, mkdir, unlink, rmdir and close.
- Returns:
  - raw OS return code.

### File helpers

#### `int XFile_OpenM(xfile_t *pFile, const char *pPath, const char *pFlags, xmode_t nMode)`

- Arguments:
  - file object, path, flag string and numeric mode.
- Does:
  - initializes the file object and opens the path using the parsed flags.
- Returns:
  - file descriptor on success.
  - negative value on failure.

#### `int XFile_Open(xfile_t *pFile, const char *pPath, const char *pFlags, const char *pPerms)`

- Arguments:
  - file object, path, flag string and optional textual permission string.
- Does:
  - converts permissions such as `"rw-r--r--"` to mode bits.
  - defaults permissions to `"rw-r--r--"` when `pPerms == NULL`.
  - opens the file.
- Returns:
  - file descriptor on success.
  - `XSTDERR` on invalid args or permission-conversion failure.

#### `int XFile_Reopen(xfile_t *pFile, const char *pPath, const char *pFlags, const char *pPerms)`

- Arguments:
  - existing file object plus new open parameters.
- Does:
  - closes the current fd and reopens the file.
- Returns:
  - result of `XFile_Open()`.

#### `xfile_t *XFile_Alloc(const char *pPath, const char *pFlags, const char *pPerms)`

- Arguments:
  - open parameters.
- Does:
  - heap-allocates an `xfile_t` and opens it.
- Returns:
  - allocated file object on success.
  - `NULL` on allocation or open failure.

#### `xbool_t XFile_IsOpen(xfile_t *pFile)`

- Arguments:
  - file object.
- Does:
  - checks whether `nFD >= 0`.
- Returns:
  - `XTRUE` or `XFALSE`.

#### `void XFile_Close(xfile_t *pFile)`

#### `void XFile_Destroy(xfile_t *pFile)`

#### `void XFile_Free(xfile_t **ppFile)`

- Arguments:
  - file object or pointer-to-pointer.
- Does:
  - closes the fd.
  - `Destroy()` frees the object when `nAlloc` is set.
  - `Free()` also nulls the caller pointer.
- Returns:
  - no return value.

#### `size_t XFile_Seek(xfile_t *pFile, uint64_t nPosit, int nOffset)`

- Arguments:
  - file object, offset and seek origin.
- Does:
  - forwards to `_lseek()` or `lseek()`.
- Returns:
  - new position.
  - on error the underlying `-1` may appear as a large `size_t` because of the unsigned return type.

#### `int XFile_Write(xfile_t *pFile, const void *pBuff, size_t nSize)`

#### `int XFile_Read(xfile_t *pFile, void *pBuff, size_t nSize)`

- Arguments:
  - open file, buffer and requested size.
- Does:
  - writes or reads raw bytes.
  - `Read()` sets `bEOF` when read returns `<= 0` and `errno != EAGAIN`.
- Returns:
  - transferred byte count or `XSTDERR`.

#### `int XFile_Print(xfile_t *pFile, const char *pFmt, ...)`

- Arguments:
  - file object and printf-style format string.
- Does:
  - formats a temporary heap string and writes it to the file.
- Returns:
  - byte count from `XFile_Write()`.
  - `XSTDERR` on formatting/allocation failure.

#### `int XFile_GetStats(xfile_t *pFile)`

- Arguments:
  - open file object.
- Does:
  - loads `fstat()` data into `nBlockSize`, `nMode` and `nSize`.
- Returns:
  - `XSTDOK` when size is non-zero.
  - `XSTDNON` for zero-size files.
  - `XSTDERR` on failure.

#### `uint8_t *XFile_LoadSize(xfile_t *pFile, size_t nMaxSize, size_t *pSize)`

#### `uint8_t *XFile_Load(xfile_t *pFile, size_t *pSize)`

- Arguments:
  - open file and optional output-size pointer.
  - `nMaxSize`: maximum allowed bytes, or `XSTDNON` in `XFile_Load()` to mean full file.
- Does:
  - requires a regular file with successful stats.
  - reads up to the allowed size into a newly allocated NUL-terminated buffer.
- Returns:
  - allocated buffer on success.
  - `NULL` when stats fail, file is not regular, allocation fails or nothing is read.

#### `int XFile_Copy(xfile_t *pIn, xfile_t *pOut)`

- Arguments:
  - source and destination open files.
- Does:
  - copies in `nBlockSize` chunks from input to output.
- Returns:
  - total written bytes.
  - `XSTDERR` on setup failure.

#### `int XFile_GetLine(xfile_t *pFile, char *pLine, size_t nSize)`

- Arguments:
  - open file, destination buffer and size.
- Does:
  - reads one byte at a time until newline, EOF or buffer exhaustion.
- Returns:
  - bytes stored, including the newline when present.
  - `XSTDINV` for invalid destination args.
  - `XSTDERR` when the file is not open.

#### `int XFile_GetLineCount(xfile_t *pFile)`

- Arguments:
  - open file.
- Does:
  - counts lines from the current file position to EOF.
- Returns:
  - line count.
  - `XSTDERR` on stats/open failure.

#### `int XFile_ReadLine(xfile_t *pFile, char *pLine, size_t nSize, size_t nLineNum)`

- Arguments:
  - open file, destination buffer, size and 1-based line number.
- Does:
  - sequentially reads lines until the requested line number is reached.
- Returns:
  - bytes read for the requested line.
  - `XSTDERR` on EOF or failure before reaching that line.

### File type and permission helpers

#### `xfile_type_t XFile_GetType(xmode_t nMode)`

#### `char XFile_GetTypeChar(xfile_type_t eType)`

#### `char XPath_GetType(xmode_t nMode)`

#### `xbool_t XFile_IsExec(xmode_t nMode)`

- Arguments:
  - mode bits or xfile type enum.
- Does:
  - converts between mode bits and file-type enums/chars and checks execute permission bits.
- Returns:
  - file-type enum, file-type char or `XTRUE`/`XFALSE`.

#### `int XPath_PermToMode(const char *pPerm, xmode_t *pMode)`

#### `int XPath_ModeToPerm(char *pOutput, size_t nSize, xmode_t nMode)`

#### `int XPath_ModeToChmod(char *pOutput, size_t nSize, xmode_t nMode)`

#### `int XPath_SetPerm(const char *pPath, const char *pPerm)`

#### `int XPath_GetPerm(char *pOutput, size_t nSize, const char *pPath)`

- Arguments:
  - textual permissions, mode bits, output buffers and/or path.
- Does:
  - converts permission strings and chmod-style numbers.
  - applies permissions or reads them from a path.
- Returns:
  - `XSTDOK` / `XSTDERR` for setters/converters.
  - written length for string-formatting helpers.

### Path helpers

#### `xbool_t XPath_Exists(const char *pPath)`

- Arguments:
  - path string.
- Does:
  - checks for path existence with `stat()`.
- Returns:
  - `XTRUE` or `XFALSE`.

#### `int XPath_Parse(xpath_t *pPath, const char *pPathStr, xbool_t bStat)`

- Arguments:
  - output `xpath_t`, raw path string and optional stat-driven directory detection flag.
- Does:
  - splits a path into `sPath` and `sFile`.
  - when the path is known to be a directory or ends with `/`, stores the whole string in `sPath`.
- Returns:
  - copied length for the part written.
  - `XSTDINV` for invalid output struct.
  - `XSTDERR` when the input path is empty.
  - `XSTDNON` in some split fallthrough paths.

#### `long XPath_GetSize(const char *pPath)`

- Arguments:
  - path.
- Does:
  - opens the file, loads stats and returns file size.
- Returns:
  - size on success.
  - `XSTDERR` on failure.

#### `uint8_t *XPath_Load(const char *pPath, size_t *pSize)`

#### `uint8_t *XPath_LoadSize(const char *pPath, size_t nMaxSize, size_t *pSize)`

- Arguments:
  - path, optional max size and output-size pointer.
- Does:
  - opens the file, delegates to `XFile_Load*()` and closes it.
- Returns:
  - allocated NUL-terminated buffer or `NULL`.

#### `size_t XPath_LoadBuffer(const char *pPath, xbyte_buffer_t *pBuffer)`

#### `size_t XPath_LoadBufferSize(const char *pPath, xbyte_buffer_t *pBuffer, size_t nMaxSize)`

- Arguments:
  - path plus destination byte buffer.
- Does:
  - initializes the buffer and transfers ownership of the heap-loaded file content into `pBuffer`.
- Returns:
  - loaded byte count.
  - `0` on failure.

#### `int XPath_CopyFile(const char *pSrc, const char *pDst)`

- Arguments:
  - source and destination paths.
- Does:
  - opens both files and copies the source into the destination using create/write/truncate flags.
- Returns:
  - total copied bytes.
  - `XSTDERR` on open/copy failure.

#### `int XPath_Read(const char *pPath, uint8_t *pBuffer, size_t nSize)`

- Arguments:
  - path, destination buffer and requested size.
- Does:
  - opens the file, reads up to `nSize` bytes and writes a trailing NUL byte.
- Returns:
  - read byte count or `XSTDERR`.
- Caveat:
  - caller should reserve at least `nSize + 1` bytes because the function always writes a terminator.

#### `int XPath_Write(const char *pPath, const uint8_t *pData, size_t nSize, const char *pFlags)`

#### `int XPath_WriteBuffer(const char *pPath, xbyte_buffer_t *pBuffer, const char *pFlags)`

- Arguments:
  - path, raw bytes or byte buffer, length/flags.
- Does:
  - opens the destination and writes all bytes in a loop.
  - `WriteBuffer()` forwards buffer contents to `XPath_Write()`.
- Returns:
  - bytes written so far.
  - `XSTDERR` on invalid args or open failure.
- Caveat:
  - current implementation retries short writes with the original total size instead of the remaining size, so it assumes full writes more than it should.

### Directory helpers

#### `int XDir_Open(xdir_t *pDir, const char *pPath)`

- Arguments:
  - directory handle and path.
- Does:
  - opens a directory iterator and resets iterator state.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` on failure.

#### `void XDir_Close(xdir_t *pDir)`

- Arguments:
  - directory handle.
- Does:
  - closes the OS directory handle and resets iterator state.
- Returns:
  - no return value.

#### `int XDir_Read(xdir_t *pDir, char *pFile, size_t nSize)`

- Arguments:
  - open directory handle and optional caller buffer for the entry name.
- Does:
  - advances to the next entry, skipping `.` and `..`.
  - updates `pDir->pCurrEntry`.
- Returns:
  - `XSTDOK` when an entry is produced.
  - `XSTDNON` at end of directory.
  - `XSTDERR` when the directory is not open.

#### `int XDir_Valid(const char *pPath)`

- Arguments:
  - path.
- Does:
  - checks whether the path exists and is a directory.
- Returns:
  - negative `stat()` result on failure.
  - `1` when the path is a directory.
  - `0` when it exists but is not a directory, and sets `errno = ENOTDIR`.

#### `int XDir_Create(const char *pDir, xmode_t mode)`

- Arguments:
  - directory path and mode.
- Does:
  - recursively creates all missing path segments.
- Returns:
  - `1` on success or when the directory already exists.
  - `0` on failure.

#### `int XPath_Remove(const char *pPath)`

- Arguments:
  - path to file or directory.
- Does:
  - removes a regular file with `unlink()` or a directory tree with `XDir_Remove()`.
- Returns:
  - result of `xunlink()` or `XDir_Remove()`.
  - `XSTDERR` when `stat()` fails.

#### `int XDir_Remove(const char *pPath)`

- Arguments:
  - directory path.
- Does:
  - recursively removes directory contents and then calls `rmdir()` on the directory itself.
- Returns:
  - last child-removal status.
  - often `XSTDERR` for empty directories because of the initial default status.

## Important Notes

- `XPath_Parse()` is a pragmatic splitter, not a full path-normalization library.
- `XPath_Write()` assumes full writes more than it should.
- `XDir_Remove()` has rough return semantics for empty directories.
- For robust partial-write handling to unusual file descriptors, prefer a caller-side write loop over `XPath_Write()`.
