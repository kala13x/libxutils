# srch.c

## Purpose

Recursive file/content search with metadata filtering and optional result collection.

## Callback Contract

- search callback receives:
  - `(search, entry, NULL)` for a found match
  - `(search, NULL, message)` for an operational error
- callback return meaning for matches:
  - `> 0`: keep the entry and append it to `fileArray`
  - `0`: do not store the entry
  - `< 0`: interrupt the search

## API Reference

### Entry helpers

#### `void XSearch_InitEntry(xsearch_entry_t *pEntry)`

- Arguments:
  - entry object to clear.
- Does:
  - zeroes textual fields and metadata.
- Returns:
  - no return value.

#### `void XSearch_CreateEntry(xsearch_entry_t *pEntry, const char *pName, const char *pPath, xstat_t *pStat)`

- Arguments:
  - destination entry, optional file name, optional path and optional stat struct.
- Does:
  - initializes the entry and copies name/path/stat-derived metadata.
  - on non-Windows symlinks, also fills `sLink` and `pRealPath`.
- Returns:
  - no return value.

#### `xsearch_entry_t *XSearch_AllocEntry(void)`

#### `xsearch_entry_t *XSearch_NewEntry(const char *pName, const char *pPath, xstat_t *pStat)`

- Arguments:
  - optional entry metadata.
- Does:
  - heap-allocates a new entry and optionally populates it.
- Returns:
  - allocated entry or `NULL`.

#### `void XSearch_FreeEntry(xsearch_entry_t *pEntry)`

- Arguments:
  - heap-allocated entry.
- Does:
  - frees `pRealPath` when present and then frees the entry itself.
- Returns:
  - no return value.

#### `xsearch_entry_t *XSearch_GetEntry(xsearch_t *pSearch, int nIndex)`

- Arguments:
  - search context and result index.
- Does:
  - returns a stored result entry from `fileArray`.
- Returns:
  - entry pointer or `NULL` when the index is out of range.

### Search lifecycle

#### `void XSearch_Init(xsearch_t *pSearch, const char *pFileName)`

- Arguments:
  - search context.
  - file-name pattern string, optionally `;`-separated.
- Does:
  - resets search flags and filter fields.
  - initializes `nameTokens` and `fileArray`.
  - tokenizes multi-name patterns into `nameTokens`.
  - stores the raw pattern in `sName`.
- Returns:
  - no return value.

#### `void XSearch_Destroy(xsearch_t *pSearch)`

- Arguments:
  - search context.
- Does:
  - marks the search as interrupted.
  - destroys the stored result array and token array.
- Returns:
  - no return value.

#### `int XSearch(xsearch_t *pSearch, const char *pDirectory)`

- Arguments:
  - search context.
  - directory path, or `NULL` when searching stdin with `bReadStdin`.
- Does:
  - applies filename, permission, size, link-count and file-type filters.
  - optionally searches file contents for `sText`.
  - optionally recurses into subdirectories.
  - for stdin mode, searches the input buffer instead of traversing directories.
- Returns:
  - `XSTDOK` on successful completion.
  - `XSTDNON` for some no-match content-search paths.
  - `XSTDERR` on hard failure or user interruption.
- Important nuance:
  - some operational filesystem errors are only reported through the callback and do not force a final `XSTDERR` unless the callback interrupts the search.

## Content Search Behavior

- `bInsensitive` lowercases both search strings and loaded data before matching.
- `bSearchLines` reports every matching line separately with `nLineNum` and `sLine`.
- without `bSearchLines`, the code tries to report the nearest line around the first match position.
- binary/non-line-friendly matches are reported as:
  - `"Binary file matches"`
  - `"Binary input matches"`

## Important Notes

- `XSearch_Init()` treats `;` in `pFileName` as a multi-pattern separator.
- Caller-owned result arrays inside `xsearch_t` are destroyed by `XSearch_Destroy()`.
- Permission filtering compares chmod-style numeric permissions.
- Type filtering can include execute permission (`XF_EXEC`) as a separate bit.
