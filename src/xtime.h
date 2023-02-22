/*!
 *  @file libxutils/src/xtime.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Get system date, convert between formats,
 * get month days, calculate leap year, and e.t.c.
 */

#ifndef __XUTILS_XTIME_H__
#define __XUTILS_XTIME_H__

#include "xstd.h"

#define XTIME_U64_YEAR(tm)  (int)(tm >> 48)             // 16 bit
#define XTIME_U64_MONTH(tm) (int)((tm >> 40) & 0xFF)    // 8 bit
#define XTIME_U64_DAY(tm)   (int)((tm >> 32) & 0xFF)    // 8 bit
#define XTIME_U64_HOUR(tm)  (int)((tm >> 24) & 0xFF)    // 8 bit
#define XTIME_U64_MIN(tm)   (int)((tm >> 16) & 0xFF)    // 8 bit
#define XTIME_U64_SEC(tm)   (int)((tm >> 8) & 0xFF)     // 8 bit
#define XTIME_U64_FRAQ(tm)  (int)(tm & 0xFF)            // 8 bit

#define XSECS_IN_YEAR       31556926
#define XSECS_IN_MONTH      2629743
#define XSECS_IN_WEEK       604800
#define XSECS_IN_DAY        86400
#define XSECS_IN_HOUR       3600
#define XSECS_IN_MIN        60

#define XTIME_MAX           64

typedef enum {
    XTIME_STR_SIMPLE = 0,
    XTIME_STR_HTTP,
    XTIME_STR_LSTR,
    XTIME_STR_HSTR
} xtime_fmt_t;

typedef enum {
    XTIME_DIFF_YEAR = 0,
    XTIME_DIFF_MONTH,
    XTIME_DIFF_WEEK,
    XTIME_DIFF_DAY,
    XTIME_DIFF_HOUR,
    XTIME_DIFF_MIN,
    XTIME_DIFF_SEC
} xtime_diff_t;

typedef struct XTimeSpec {
    uint64_t nNanoSec;
    time_t nSec;
} xtime_spec_t;

typedef struct XTime {
    uint16_t nYear;
    uint8_t nMonth; 
    uint8_t nDay;
    uint8_t nHour;
    uint8_t nMin;
    uint8_t nSec;
    uint8_t nFraq;
} xtime_t;

#ifdef __cplusplus
extern "C" {
#endif

void XTime_Init(xtime_t *pTime);
void XTime_Get(xtime_t *pTime);
void XTime_GetTm(struct tm *pTm);
uint64_t XTime_GetU64(void);
uint32_t XTime_GetUsec(void);
int XTime_GetClock(xtime_spec_t *pTs);
size_t XTime_GetStr(char *pDst, size_t nSize, xtime_fmt_t eFmt);

int XTime_GetLeapYear(int nYear);
int XTime_GetMonthDays(int nYear, int nMonth);
int XTime_MonthDays(const xtime_t *pTime);
int XTime_LeapYear(const xtime_t *pTime);

void XTime_Make(xtime_t *pSrc);
void XTime_Copy(xtime_t *pDst, const xtime_t *pSrc);

double XTime_DiffSec(const xtime_t *pSrc1, const xtime_t *pSrc2);
double XTime_Diff(const xtime_t *pSrc1, const xtime_t *pSrc2, xtime_diff_t eDiff);

uint64_t XTime_ToU64(const xtime_t *pTime);
time_t XTime_ToEpoch(const xtime_t *pTime);
size_t XTime_ToStr(const xtime_t *pTime, char *pStr, size_t nSize);
size_t XTime_ToHstr(const xtime_t *pTime, char *pStr, size_t nSize);
size_t XTime_ToLstr(const xtime_t *pTime, char *pStr, size_t nSize);
size_t XTime_ToRstr(const xtime_t *pTime, char *pStr, size_t nSize);
size_t XTime_ToHTTP(const xtime_t *pTime, char *pStr, size_t nSize);
void XTime_ToTm(const xtime_t* pTime, struct tm* pTm);

void XTime_FromEpoch(xtime_t *pTime, const time_t nTime);
void XTime_FromU64(xtime_t *pTime, const uint64_t nTime);
void XTime_FromTm(xtime_t *pTime, const struct tm *pTm);
int XTime_FromHstr(xtime_t *pTime, const char *pStr);
int XTime_FromLstr(xtime_t *pTime, const char *pStr);
int XTime_FromRstr(xtime_t *pTime, const char *pStr);
int XTime_FromStr(xtime_t *pTime, const char *pStr);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XTIME_H__ */
