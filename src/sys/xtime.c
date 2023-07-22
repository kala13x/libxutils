/*!
 *  @file libxutils/src/sys/xtime.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Get system date, convert between formats,
 * get month days, calculate leap year, and e.t.c.
 */

#include "xtime.h"
#include "xstr.h"

#if defined(__linux__) && defined(CLOCK_REALTIME)
#define XTIME_USE_CLOCK
#endif

void XTime_Init(xtime_t *pTime)
{
    pTime->nYear = pTime->nMonth = 0;
    pTime->nDay = pTime->nHour = 0;
    pTime->nMin = pTime->nSec = 0;
    pTime->nFraq = 0;
}

void XTime_FromTm(xtime_t *pTime, const struct tm *pTm) 
{
    pTime->nYear = (uint16_t)pTm->tm_year+1900;
    pTime->nMonth = (uint8_t)pTm->tm_mon+1;
    pTime->nDay = (uint8_t)pTm->tm_mday;
    pTime->nHour = (uint8_t)pTm->tm_hour;
    pTime->nMin = (uint8_t)pTm->tm_min;
    pTime->nSec = (uint8_t)pTm->tm_sec;
    pTime->nFraq = 0;
}

int XTime_FromStr(xtime_t *pTime, const char *pStr) 
{
    XTime_Init(pTime);
#ifdef _WIN32
    return sscanf_s(pStr, "%04d%02d%02d%02d%02d%02d%02d",
        (int*)&pTime->nYear, (int*)&pTime->nMonth, (int*)&pTime->nDay, 
        (int*)&pTime->nHour, (int*)&pTime->nMin, (int*)&pTime->nSec, 
        (int*)&pTime->nFraq);
#else
    return sscanf(pStr, "%04d%02d%02d%02d%02d%02d%02d",
        (int*)&pTime->nYear, (int*)&pTime->nMonth, (int*)&pTime->nDay,
        (int*)&pTime->nHour, (int*)&pTime->nMin, (int*)&pTime->nSec,
        (int*)&pTime->nFraq);
#endif
}

int XTime_FromHStr(xtime_t *pTime, const char *pStr)
{
    XTime_Init(pTime);
#ifdef _WIN32
    return sscanf_s(pStr, "%04d.%02d.%02d-%02d:%02d:%02d.%02d",
        (int*)&pTime->nYear, (int*)&pTime->nMonth, (int*)&pTime->nDay,
        (int*)&pTime->nHour, (int*)&pTime->nMin, (int*)&pTime->nSec,
        (int*)&pTime->nFraq);
#else
    return sscanf(pStr, "%04d.%02d.%02d-%02d:%02d:%02d.%02d",
        (int*)&pTime->nYear, (int*)&pTime->nMonth, (int*)&pTime->nDay,
        (int*)&pTime->nHour, (int*)&pTime->nMin, (int*)&pTime->nSec,
        (int*)&pTime->nFraq);
#endif
}

int XTime_FromLstr(xtime_t *pTime, const char *pStr)
{
    XTime_Init(pTime);

#ifdef _WIN32
    return sscanf_s(pStr, "%04d/%02d/%02d/%02d/%02d/%02d",
        (int*)&pTime->nYear, (int*)&pTime->nMonth, (int*)&pTime->nDay,
        (int*)&pTime->nHour, (int*)&pTime->nMin, (int*)&pTime->nSec);
#else
    return sscanf(pStr, "%04d/%02d/%02d/%02d/%02d/%02d",
        (int*)&pTime->nYear, (int*)&pTime->nMonth, (int*)&pTime->nDay,
        (int*)&pTime->nHour, (int*)&pTime->nMin, (int*)&pTime->nSec);
#endif
}

int XTime_FromRstr(xtime_t *pTime, const char *pStr)
{
    XTime_Init(pTime);
#ifdef _WIN32
    return sscanf_s(pStr, "%02d/%02d/%04d %02d:%02d:%02d.%02d",
        (int*)&pTime->nMonth, (int*)&pTime->nDay, (int*)&pTime->nYear,
        (int*)&pTime->nHour, (int*)&pTime->nMin, (int*)&pTime->nSec,
        (int*)&pTime->nFraq);
#else
    return sscanf(pStr, "%02d/%02d/%04d %02d:%02d:%02d.%02d",
        (int*)&pTime->nMonth, (int*)&pTime->nDay, (int*)&pTime->nYear,
        (int*)&pTime->nHour, (int*)&pTime->nMin, (int*)&pTime->nSec,
        (int*)&pTime->nFraq);
#endif
}

void XTime_FromEpoch(xtime_t *pTime, const time_t nTime)
{
    struct tm timeinfo;
#ifdef _WIN32
    localtime_s(&timeinfo, &nTime);
#else
    localtime_r(&nTime, &timeinfo);
#endif
    XTime_FromTm(pTime, &timeinfo);
    pTime->nFraq = 0;
}

void XTime_FromU64(xtime_t *pTime, const uint64_t nTime)
{
    pTime->nYear = XTIME_U64_YEAR(nTime);
    pTime->nMonth = XTIME_U64_MONTH(nTime);
    pTime->nDay = XTIME_U64_DAY(nTime);
    pTime->nHour = XTIME_U64_HOUR(nTime);
    pTime->nMin = XTIME_U64_MIN(nTime);
    pTime->nSec = XTIME_U64_SEC(nTime);
    pTime->nFraq = XTIME_U64_FRAQ(nTime);
}

void XTime_ToTm(const xtime_t *pTime, struct tm *pTm)
{
    pTm->tm_year = pTime->nYear - 1900;
    pTm->tm_mon = pTime->nMonth - 1;
    pTm->tm_mday = pTime->nDay;
    pTm->tm_hour = pTime->nHour;
    pTm->tm_min = pTime->nMin;
    pTm->tm_sec = pTime->nSec;
    pTm->tm_isdst = -1;  
}

time_t XTime_ToEpoch(const xtime_t *pTime)
{
    struct tm tminf;
    XTime_ToTm(pTime, &tminf);
    return mktime(&tminf);
}

size_t XTime_ToStr(const xtime_t *pTime, char *pStr, size_t nSize)
{
    return xstrncpyf(pStr, nSize, "%04d%02d%02d%02d%02d%02d%02d",
        pTime->nYear, pTime->nMonth, pTime->nDay, pTime->nHour,
        pTime->nMin, pTime->nSec, pTime->nFraq);
}

size_t XTime_ToHstr(const xtime_t *pTime, char *pStr, size_t nSize)
{
    return xstrncpyf(pStr, nSize, "%04d.%02d.%02d-%02d:%02d:%02d.%02d",
        pTime->nYear, pTime->nMonth, pTime->nDay, pTime->nHour,
        pTime->nMin, pTime->nSec, pTime->nFraq);
}

size_t XTime_ToLstr(const xtime_t *pTime, char *pStr, size_t nSize)
{
    return xstrncpyf(pStr, nSize, "%04d/%02d/%02d/%02d/%02d/%02d",
        pTime->nYear, pTime->nMonth, pTime->nDay,
        pTime->nHour, pTime->nMin, pTime->nSec);
}

size_t XTime_ToRstr(const xtime_t *pTime, char *pStr, size_t nSize)
{
    return xstrncpyf(pStr, nSize, "%02d/%02d/%04d %02d:%02d:%02d",
        pTime->nMonth, pTime->nDay, pTime->nYear,
        pTime->nHour, pTime->nMin, pTime->nSec);
}

size_t XTime_ToHTTP(const xtime_t *pTime, char *pStr, size_t nSize)
{
    struct tm timeinfo;
    time_t rawTime = XTime_ToEpoch(pTime);
#ifdef _WIN32
    gmtime_s(&timeinfo, &rawTime);
#else
    gmtime_r(&rawTime, &timeinfo);
#endif
    return strftime(pStr, nSize, "%a, %d %b %G %H:%M:%S GMT", &timeinfo);
}

uint64_t XTime_ToU64(const xtime_t *pTime)
{
    uint64_t nTime;
    nTime = (uint64_t)pTime->nYear;
    nTime <<= 8;
    nTime += (uint64_t)pTime->nMonth;
    nTime <<= 8;
    nTime += (uint64_t)pTime->nDay;
    nTime <<= 8;
    nTime += (uint64_t)pTime->nHour;
    nTime <<= 8;
    nTime += (uint64_t)pTime->nMin;
    nTime <<= 8;
    nTime += (uint64_t)pTime->nSec;
    nTime <<= 8;
    nTime += (uint64_t)pTime->nFraq;
    return nTime;
}

int XTime_GetLeapYear(int nYear) 
{
    int nRetVal = 0;
    if (nYear % 4 == 0)
    {
        if (nYear % 100 == 0)
        {
            if (nYear % 400 == 0) nRetVal = 1;
            else nRetVal = 0;
        }
        else nRetVal = 1;
    }
    else nRetVal = 0;
    return nRetVal;
}

int XTime_GetMonthDays(int nYear, int nMonth)
{
    int nLeap = XTime_GetLeapYear(nYear);
    if (nMonth == 2) return nLeap ? 28 : 29;

    if (nMonth == 4 || 
        nMonth == 6 || 
        nMonth == 9 || 
        nMonth == 11) 
        return 30;

    return 31;
}

int XTime_MonthDays(const xtime_t *pTime)
{
    return XTime_GetMonthDays(pTime->nYear, pTime->nMonth);
}

int XTime_LeapYear(const xtime_t *pTime)
{
    return XTime_GetLeapYear(pTime->nYear);
}

double XTime_DiffSec(const xtime_t *pSrc1, const xtime_t *pSrc2)
{
    struct tm tm1, tm2;
    XTime_ToTm(pSrc1, &tm1);
    XTime_ToTm(pSrc2, &tm2);
    return difftime(mktime(&tm1), mktime(&tm2));
}

double XTime_Diff(const xtime_t *pSrc1, const xtime_t *pSrc2, xtime_diff_t eDiff)
{
    double fSeconds = XTime_DiffSec(pSrc1, pSrc2);

    switch (eDiff)
    {
        case XTIME_DIFF_YEAR: return fSeconds / XSECS_IN_YEAR;
        case XTIME_DIFF_MONTH: return fSeconds / XSECS_IN_MONTH;
        case XTIME_DIFF_WEEK: return fSeconds / XSECS_IN_WEEK;
        case XTIME_DIFF_DAY: return fSeconds / XSECS_IN_DAY;
        case XTIME_DIFF_HOUR: return fSeconds / XSECS_IN_HOUR;
        case XTIME_DIFF_MIN: return fSeconds / XSECS_IN_MIN;
        case XTIME_DIFF_SEC: return fSeconds;
        default: break;
    }

    return fSeconds;
}

void XTime_Copy(xtime_t *pDst, const xtime_t *pSrc) 
{
    if (pDst == NULL || pSrc == NULL) return;
    XTime_FromU64(pDst, XTime_ToU64(pSrc));
}

void XTime_Make(xtime_t *pSrc)
{
    struct tm timeinfo;
    XTime_ToTm(pSrc, &timeinfo);
    mktime(&timeinfo);
    XTime_FromTm(pSrc, &timeinfo);
}

int XTime_GetClock(xtime_spec_t *pTs)
{
    pTs->nNanoSec = 0;
    pTs->nSec = 0;
#ifdef XTIME_USE_CLOCK
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) return XSTDERR;
    pTs->nNanoSec = ts.tv_nsec;
    pTs->nSec = ts.tv_sec;
#elif _WIN32
    const uint64_t nEpoch = ((uint64_t)116444736000000000ULL);
    SYSTEMTIME  sysTime;
    FILETIME    fileTime;
    uint64_t    nTime;

    GetSystemTime(&sysTime);
    SystemTimeToFileTime(&sysTime, &fileTime);
    nTime = ((uint64_t)fileTime.dwLowDateTime);
    nTime += ((uint64_t)fileTime.dwHighDateTime) << 32;

    pTs->nNanoSec = ((uint64_t)sysTime.wMilliseconds * 1000000);
    pTs->nSec = (time_t)((nTime - nEpoch) / 10000000L);
#else
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) return XSTDERR;
    pTs->nNanoSec = tv.tv_usec * 1000;
    pTs->nSec = tv.tv_sec;
#endif
    return XSTDOK;
}

uint32_t XTime_GetUsec(void)
{
    xtime_spec_t now;
    XTime_GetClock(&now);
    return (uint32_t)(now.nNanoSec / 1000);
}

uint64_t XTime_GetStamp(void)
{
    xtime_spec_t now;
    XTime_GetClock(&now);
    uint64_t nTimeStamp = ((uint64_t)now.nNanoSec / 1000);
    return (uint64_t)now.nSec * 1000000 + nTimeStamp;
}

void XTime_Get(xtime_t *pTime) 
{
    xtime_spec_t now;
    XTime_GetClock(&now);
    XTime_FromEpoch(pTime, now.nSec);
    pTime->nFraq = (uint8_t)(now.nNanoSec / 10000000);
}

size_t XTime_GetStr(char *pDst, size_t nSize, xtime_fmt_t eFmt)
{
    if (pDst == NULL || !nSize) return 0;

    xtime_t date;
    XTime_Get(&date);

    switch (eFmt)
    {
        case XTIME_STR_SIMPLE: return XTime_ToStr(&date, pDst, nSize);
        case XTIME_STR_HTTP: return XTime_ToHTTP(&date, pDst, nSize);
        case XTIME_STR_LSTR: return XTime_ToLstr(&date, pDst, nSize);
        case XTIME_STR_HSTR: return XTime_ToHstr(&date, pDst, nSize);
        default: break;
    }

    pDst[0] = '\0';
    return 0;
}

uint64_t XTime_GetU64(void)
{
    xtime_t xtime;
    XTime_Get(&xtime);
    return XTime_ToU64(&xtime);
}

void XTime_GetTm(struct tm *pTm)
{
    xtime_t xtime;
    XTime_FromEpoch(&xtime, time(NULL));
    XTime_ToTm(&xtime, pTm);
}
