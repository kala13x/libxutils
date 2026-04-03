# str.c

## Purpose

General string utility module plus dynamic `XString`.

## Important Convention

- `xstrcmp`, `xstrncmp`, `xstrncmpn`, `xstrncasecmp` return `XTRUE` on equality and `XFALSE` on mismatch. They are boolean helpers, not `strcmp`-style ordering APIs.

## C-string API Reference

### Tokenization / splitting

#### `char *xstrtok(char *pString, const char *pDelimiter, char **pContext)`

- Thread-safe `strtok_r`/`strtok_s` wrapper.
- Returns next token or `NULL`.

#### `size_t xstrsplita(const char *pString, const char *pDlmt, xarray_t *pTokens, xbool_t bIncludeDlmt, xbool_t bIncludeEmpty)`

- Splits into caller-provided token array.
- Returns number of produced tokens.

#### `xarray_t *xstrsplit / xstrsplitd / xstrsplite(const char *pString, const char *pDlmt)`

- Allocate a pool-backed `xarray_t` of duplicated token strings.
- `splitd`: includes delimiters.
- `splite`: includes empty tokens.
- Return array or `NULL`.

### Allocation / duplication / formatting

#### `char *xstrdup(const char *pStr)`

#### `char *xstrpdup(xpool_t *pPool, const char *pStr)`

- Duplicate string into heap or pool.
- Return new string or `NULL`.

#### `char *xstralloc(size_t nSize)`

- Allocates zero-terminated empty string buffer of exact size.
- Returns buffer or `NULL`.

#### `size_t xstrarglen(const char *pFmt, va_list args)`

- Estimates formatted output length.
- Returns required size excluding terminator in the normal path.

#### `int xstrncpyarg(char *pDest, size_t nSize, const char *pFmt, va_list args)`

- Formats into caller buffer.
- Returns written length or `XSTDERR`.

#### `char *xstracpyarg / xstracpyargs(...)`

#### `char *xstrpcpyargs(...)`

#### `char *xstracpy(const char *pFmt, ...)`

#### `char *xstracpyn(size_t *nSize, const char *pFmt, ...)`

- Allocate formatted string on heap or pool.
- Return string or `NULL`.

#### `char *xstrxcpy(const char *pFmt, ...)`

#### `size_t xstrxcpyf(char **pDst, const char *pFmt, ...)`

- GNU `vasprintf`-based helpers.
- Return allocated string / byte count on GNU builds.
- Return `NULL` / `0` when GNU support is unavailable.

### Copy / concat / fill

#### `size_t xstrncpy / xstrncpys(...)`

- Safe copy helpers.
- Return copied length.

#### `size_t xstrncpyf / xstrncpyfl / xstrnlcpyf(...)`

- Format into caller buffer.
- Return written length.

#### `size_t xstrncat / xstrncatf / xstrncatsf(...)`

- Append formatted text to existing string.
- Return written length or remaining space depending on variant:
  - `xstrncat`: bytes appended
  - `xstrncatf`: new remaining available space
  - `xstrncatsf`: remaining available space

#### `size_t xstrnfill(char *pDst, size_t nSize, size_t nLength, char cFill)`

- Fills destination with repeated `cFill`.
- Returns fill length.

#### `char *xstrfill(size_t nLength, char cFill)`

- Returns pointer to static buffer filled to requested length.
- Caveat:
  - current implementation ignores `cFill` and always uses spaces.

### Matching / searching

#### `xbool_t xstrmatch / xstrnmatch(...)`

- Wildcard matching with `*` and `?`.

#### `xbool_t xstrmatchm(const char *pStr, size_t nLength, const char *pPattern, const char *pDelimiter)`

- Multi-pattern wildcard match; delimiter defaults to `;`.

#### `xbool_t xstrregex(const char *pStr, size_t nLength, const char *pPattern)`

- Lightweight `*`-token matcher, not a full regex engine.

#### `int xstrsrc / xstrnsrc / xstrsrcb / xstrsrcp(...)`

- Search for substring in full string, from offset, in bounded buffer, or from position.
- Return found offset or `XSTDERR` when missing.

### Case conversion

#### `size_t xstrcase(char *pSrc, xstr_case_t eCase)`

- Converts string in place.
- Returns converted length.
- Caveat:
  - current guard is inverted and can return `0` for valid non-empty input.

#### `size_t xstrncase / xstrncases(...)`

- Convert into caller buffer.
- Return copied length.

#### `char *xstracase / xstracasen(...)`

- Allocate converted copy.
- Return string or `NULL`.

### Extract / replace / remove

#### `char *xstrcut(char *pData, const char *pStart, const char *pEnd)`

- In-place token-style cutter over mutable string.
- Returns pointer into original storage or `NULL`.

#### `size_t xstrncuts(char *pDst, size_t nSize, const char *pSrc, const char *pStart, const char *pEnd)`

- Copies substring between markers.
- Returns copied length or `0`.

#### `size_t xstrncut(char *pDst, size_t nDstSize, const char *pSrc, size_t nPosit, size_t nSize)`

#### `char *xstracut(const char *pSrc, size_t nPosit, size_t nSize)`

- Copy/cut fixed substring by position and size.
- Return copied length or allocated substring.

#### `size_t xstrnrm(char *pStr, size_t nPosit, size_t nSize)`

- Removes a range in place.
- Returns new string length.

#### `char *xstrrep(const char *pOrig, const char *pRep, const char *pWith)`

#### `int xstrnrep(char *pDst, size_t nSize, const char *pOrig, const char *pRep, const char *pWith)`

- Replace all occurrences of `pRep` with `pWith`.
- Return allocated result or `XSTDOK`/`XSTDNON`.

### Miscellaneous helpers

#### `size_t xstrrand(char *pDst, size_t nSize, size_t nLength, xbool_t bUpper, xbool_t bNumbers)`

- Generates random alnum-ish text.
- Returns written length.

#### `size_t xstrextra(const char *pStr, size_t nLength, size_t nMaxChars, size_t *pChars, size_t *pPosit)`

- Counts ANSI escape-sequence bytes that do not consume visible width.
- Returns extra-byte count.

#### `size_t xstrisextra(const char *pOffset)`

- Returns length of recognized ANSI sequence at current offset or `0`.

#### `size_t xstrnclr(char *pDst, size_t nSize, const char *pClr, const char *pStr, ...)`

- Formats text and wraps it with color code + reset.
- Returns written length.

#### `size_t xstrnrgb / xstrnyuv(...)`

- Write ANSI RGB color escape code into caller buffer.
- Return written length.

#### `char *xstrrgb / xstryuv(...)`

- Allocate ANSI RGB/YUV-derived color escape string.
- Return string or `NULL`.

#### `int xstrntok(char *pDst, size_t nSize, const char *pStr, size_t nPosit, const char *pDlmt)`

- Extract next token from position.
- Returns next position, `0` on final token, or `XSTDERR`.

#### `void xstrnull(char *pString, size_t nLength)`

#### `void xstrnul(char *pString)`

#### `xbool_t xstrused(const char *pStr)`

- Clear memory, set first char to NUL, or test for non-empty string.

## `XString_*` API Reference

### Lifecycle

#### `xstring_t *XString_New(size_t nSize, uint8_t nFastAlloc)`

#### `xstring_t *XString_From(const char *pData, size_t nLength)`

#### `xstring_t *XString_FromFmt(const char *pFmt, ...)`

#### `xstring_t *XString_FromStr(xstring_t *pString)`

- Allocate dynamic string from capacity, bytes, formatted text, or another `XString`.
- Return new string or `NULL`.

#### `int XString_Init(xstring_t *pString, size_t nSize, uint8_t nFastAlloc)`

#### `int XString_InitFrom(xstring_t *pStr, const char *pFmt, ...)`

- Initialize stack `xstring_t`.
- Return length/capacity status or `XSTDERR`.

#### `void XString_Clear(xstring_t *pString)`

- Frees owned storage; frees struct too when heap-allocated.

#### `int XString_Status(xstring_t *pString)`

- Returns `XSTDERR` if string is in error state, otherwise current length.

### Capacity / ownership

#### `int XString_Resize(xstring_t *pString, size_t nSize)`

#### `int XString_Increase(xstring_t *pString, size_t nSize)`

- Explicit resize or ensure-growth.
- Return new size or `XSTDERR`.

#### `int XString_Set(xstring_t *pString, char *pData, size_t nLength)`

- Attaches external storage without owned-capacity semantics.
- Returns attached length.

### Append / insert / copy

#### `int XString_Add(xstring_t *pString, const char *pData, size_t nLength)`

#### `int XString_Append(xstring_t *pString, const char *pFmt, ...)`

#### `int XString_AddString(xstring_t *pString, xstring_t *pSrc)`

#### `int XString_Copy(xstring_t *pString, xstring_t *pSrc)`

- Append raw bytes, formatted text, another string, or deep-copy another string.
- Return new length or `XSTDERR`.

#### `int XString_Insert(xstring_t *pString, size_t nPosit, const char *pData, size_t nLength)`

#### `int XString_InsertFmt(xstring_t *pString, size_t nPosit, const char *pFmt, ...)`

- Insert at position or append when position is beyond end.
- Return new length or `XSTDERR`.

### Remove / slice / search

#### `int XString_Remove / XString_Delete / XString_Advance(...)`

- Remove range without shrink, remove with exact shrink, or delete from front.
- Return new length or `XSTDERR`.

#### `int XString_Search(xstring_t *pString, size_t nPos, const char *pSearch)`

- Returns relative substring position from `nPos`, or `XSTDERR`.

#### `int XString_Sub / XString_SubStr(...)`

#### `xstring_t *XString_SubNew(...)`

- Copy substring into caller buffer, destination `XString`, or new `XString`.
- Return copied length/new object or `XSTDERR`/`NULL`.

#### `int XString_Cut / XString_CutSub(...)`

#### `xstring_t *XString_CutNew(...)`

- Extract substring between two markers.
- Return copied length/new object or `XSTDERR`/`NULL`.

### Case / color / replace

#### `int XString_Case / XString_ChangeCase(...)`

- Change case over a range or whole string.
- Return current length or `XSTDERR`.

#### `int XString_Color / XString_ChangeColor(...)`

- Wrap a range or whole string in color code + reset.
- Return current length or `XSTDERR`.

#### `int XString_Replace(xstring_t *pString, const char *pRep, const char *pWith)`

- Replace all matches in-place.
- Return current length or `XSTDERR`.

### Tokenization / splitting

#### `int XString_Tokenize(xstring_t *pString, char *pDst, size_t nSize, size_t nPosit, const char *pDlmt)`

#### `int XString_Token(xstring_t *pString, xstring_t *pDst, size_t nPosit, const char *pDlmt)`

- Extract next token starting from position.
- Return next position, `0` on final token, or `XSTDERR`.

#### `xarray_t *XString_SplitStr(xstring_t *pString, const char *pDlmt)`

#### `xarray_t *XString_Split(const char *pCStr, const char *pDlmt)`

- Split into array of owned `xstring_t *` tokens.
- Return array or `NULL`.

## Notes

- `XString_Split*` installs its own array clear callback to free token strings.
- Many helpers preserve a trailing NUL even for binary-ish manipulations, but `XString` is fundamentally text-oriented.
