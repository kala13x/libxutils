# xtime.c

## Purpose

Time parsing, formatting, serialization, epoch conversion and current-clock helpers.

## API Reference

### Basic struct helpers

#### `void XTime_Init(xtime_t *pTime)`

- Arguments:
  - time struct.
- Does:
  - zeroes all fields.
- Returns:
  - no return value.

#### `void XTime_Copy(xtime_t *pDst, const xtime_t *pSrc)`

- Arguments:
  - destination and source time structs.
- Does:
  - copies the whole struct when both pointers are valid.
- Returns:
  - no return value.

#### `void XTime_Make(xtime_t *pTime)`

- Arguments:
  - time struct.
- Does:
  - normalizes the fields through `mktime()` and writes the normalized values back.
- Returns:
  - no return value.

#### `void XTime_ToTm(const xtime_t *pTime, struct tm *pTm)`

#### `void XTime_FromTm(xtime_t *pTime, const struct tm *pTm)`

- Arguments:
  - `xtime_t` and `struct tm` objects.
- Does:
  - converts between the project struct and `struct tm`.
- Returns:
  - no return value.

### Current time helpers

#### `void XTime_GetTm(struct tm *pTm)`

- Arguments:
  - destination `struct tm`.
- Does:
  - gets current local time and stores it as `struct tm`.
- Returns:
  - no return value.

#### `int XTime_GetClock(xtime_spec_t *pTs)`

- Arguments:
  - destination clock struct containing seconds and nanoseconds.
- Does:
  - reads the current real-time clock with:
    - `clock_gettime(CLOCK_REALTIME)` on Linux when available
    - Windows system time APIs
    - `gettimeofday()` elsewhere
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` on clock read failure.

#### `uint32_t XTime_Get(xtime_t *pTime)`

- Arguments:
  - destination `xtime_t`.
- Does:
  - reads current time, converts epoch seconds to local time and stores hundredths of a second in `nFraq`.
- Returns:
  - current microseconds component.

#### `uint32_t XTime_GetUsec(void)`

#### `uint64_t XTime_GetMs(void)`

#### `uint64_t XTime_GetStamp(void)`

#### `uint64_t XTime_GetU64(void)`

#### `uint64_t XTime_Serialized(void)`

- Arguments:
  - none.
- Does:
  - returns the current time as:
    - microseconds component only
    - epoch milliseconds
    - epoch microseconds
    - union-packed `xtime_t`
    - manually serialized `uint64_t`
- Returns:
  - the requested numeric representation.

#### `size_t XTime_GetStr(char *pDst, size_t nSize, xtime_fmt_t eFmt)`

- Arguments:
  - destination buffer, size and desired format enum.
- Does:
  - gets current time and formats it using one of the `XTime_To*()` helpers.
- Returns:
  - written length.
  - `0` for invalid args or unknown format.

### Calendar helpers

#### `int XTime_GetLeapYear(int nYear)`

#### `int XTime_LeapYear(const xtime_t *pTime)`

- Arguments:
  - raw year or `xtime_t`.
- Does:
  - checks leap-year rules.
- Returns:
  - `1` for leap year.
  - `0` otherwise.

#### `int XTime_GetMonthDays(int nYear, int nMonth)`

#### `int XTime_MonthDays(const xtime_t *pTime)`

- Arguments:
  - raw year/month or `xtime_t`.
- Does:
  - returns the number of days in the requested month.
- Returns:
  - `28`, `29`, `30` or `31`.
- Caveat:
  - current February logic is reversed: leap years return `28` and non-leap years return `29`.

### Difference helpers

#### `double XTime_DiffSec(const xtime_t *pSrc1, const xtime_t *pSrc2)`

#### `double XTime_Diff(const xtime_t *pSrc1, const xtime_t *pSrc2, xtime_diff_t eDiff)`

- Arguments:
  - two time values and an optional scaling unit.
- Does:
  - computes difference through `mktime()` and `difftime()`.
  - optionally rescales the result to years, months, weeks, days, hours or minutes using fixed constants.
- Returns:
  - floating-point difference value.

### Serialization and epoch conversion

#### `uint64_t XTime_Serialize(const xtime_t *pTime)`

#### `void XTime_Deserialize(xtime_t *pTime, const uint64_t nTime)`

- Arguments:
  - time struct and serialized integer.
- Does:
  - manually packs or unpacks fields into a `uint64_t`.
- Returns:
  - serializer returns the packed value.
  - deserializer has no return value.

#### `uint64_t XTime_ToU64(const xtime_t *pTime)`

#### `void XTime_FromU64(xtime_t *pTime, const uint64_t nTime)`

- Arguments:
  - time struct and union-packed integer.
- Does:
  - reinterprets the struct through `xtimeu_t`.
- Returns:
  - packed integer from `ToU64()`.
  - no return value from `FromU64()`.

#### `uint64_t XTime_ToEpochLocal(const xtime_t *pTime)`

#### `uint64_t XTime_ToEpochUTC(const xtime_t *pTime)`

#### `void XTime_FromEpoch(xtime_t *pTime, const time_t nTime)`

#### `uint64_t XTime_ISOToEpochUTC(const char *pStr)`

- Arguments:
  - time struct, epoch value or ISO string.
- Does:
  - converts between `xtime_t` and local/UTC epoch seconds.
  - `FromEpoch()` uses local time.
  - `ISOToEpochUTC()` parses ISO then converts to UTC epoch seconds.
- Returns:
  - epoch value for converters.
  - `0` from `ISOToEpochUTC()` when parsing fails.

### Formatting helpers

#### `size_t XTime_ToStr(const xtime_t *pTime, char *pStr, size_t nSize)`

#### `size_t XTime_ToHstr(const xtime_t *pTime, char *pStr, size_t nSize)`

#### `size_t XTime_ToLstr(const xtime_t *pTime, char *pStr, size_t nSize)`

#### `size_t XTime_ToRstr(const xtime_t *pTime, char *pStr, size_t nSize)`

#### `size_t XTime_ToHTTP(const xtime_t *pTime, char *pStr, size_t nSize)`

#### `size_t XTime_ToISO(const xtime_t *pTime, char *pStr, size_t nSize)`

#### `size_t XTime_ToISO8601(const xtime_t *pTime, char *pStr, size_t nSize)`

- Arguments:
  - time struct, destination buffer and size.
- Does:
  - writes one of the supported text forms:
    - compact numeric
    - dotted human format
    - slash-separated format
    - US-style format
    - HTTP GMT string
    - ISO
    - ISO8601 UTC-with-`.000Z`
- Returns:
  - written length from `xstrncpyf()` or `strftime()`.

### Parse helpers

#### `int XTime_FromStr(xtime_t *pTime, const char *pStr)`

#### `int XTime_FromLstr(xtime_t *pTime, const char *pStr)`

#### `int XTime_FromRstr(xtime_t *pTime, const char *pStr)`

#### `int XTime_FromISO(xtime_t *pTime, const char *pStr)`

- Arguments:
  - destination time struct and source string.
- Does:
  - clears the destination and parses the string with `sscanf()`.
- Returns:
  - `sscanf()` field count.
  - `0` when the source string is empty.

#### `int XTime_FromHstr(xtime_t *pTime, const char *pStr)`

- Header status:
  - declared in `xtime.h`.
- Implementation status:
  - the source file implements `XTime_FromHStr()` with a capital `S`, not `XTime_FromHstr()`.
- Actual behavior of the implemented function:
  - clears the destination and parses `%04d.%02d.%02d-%02d:%02d:%02d.%02d`.
  - returns the `sscanf()` field count or `0` for empty input.

## Important Notes

- `FromEpoch()` uses local time, not UTC.
- `XTime_GetMonthDays()` has reversed February leap-year logic in the current source.
- `xtime.h` and `xtime.c` disagree on `XTime_FromHstr` vs `XTime_FromHStr`.

- Decide explicitly whether your caller wants local time or UTC before mixing `FromEpoch()`, `ToEpochUTC()` and HTTP/ISO helpers.
