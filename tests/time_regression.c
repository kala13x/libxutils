/* libxutils: calendar boundaries, stable wire dates and reversible serialization. */
#include "test.h"
#include "xtime.h"

static int XTest_calendar(void)
{
    CHECK(!XTime_GetLeapYear(1900) && XTime_GetLeapYear(2000) && !XTime_GetLeapYear(2100), "Gregorian century leap rule");
    CHECK(XTime_GetMonthDays(2024, 2) == 29 && XTime_GetMonthDays(2023, 2) == 28, "February follows leap status");
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int i = 0; i < 12; i++) CHECK(XTime_GetMonthDays(2023, i + 1) == days[i], "Each calendar month has its correct length");
    return 0;
}

static int XTest_serialization(void)
{
    xtime_t original = {2024, 2, 29, 23, 59, 58, 99}, parsed;
    uint64_t nSerialized = XTime_Serialize(&original);
    XTime_Deserialize(&parsed, nSerialized);
    CHECK(XTime_Serialize(&parsed) == nSerialized, "Explicit serialization retains every field");
    CHECK(XTIME_U64_YEAR(nSerialized) == 2024 && XTIME_U64_FRAQ(nSerialized) == 99, "Wire layout retains year and fraction");
    char sDate[64];
    CHECK(XTime_ToISO(&original, sDate, sizeof(sDate)) > 0 && strcmp(sDate, "2024-02-29T23:59:58") == 0,
        "Format leap-day ISO time");
    CHECK(XTime_FromISO(&parsed, sDate) > 0 && parsed.nYear == 2024 && parsed.nSec == 58, "Parse ISO fields");
    xtime_t epoch = {1970, 1, 1, 0, 0, 0, 0};
    CHECK(XTime_ToEpochUTC(&epoch) == 0, "UTC epoch conversion does not depend on local timezone");
    epoch.nDay = 2;
    CHECK(XTime_ToEpochUTC(&epoch) == 86400, "UTC conversion advances one day exactly");
    return 0;
}

static int XTest_http_year(void)
{
    xtime_t date = {2021, 1, 1, 0, 0, 0, 0};
    char sDate[64];
    CHECK(XTime_ToHTTP(&date, sDate, sizeof(sDate)) > 0, "Format an HTTP date at the week-year boundary");
    CHECK(strstr(sDate, "01 Jan 2021") != NULL, "HTTP uses the calendar year, not the ISO week year");
    date.nYear = 2018;
    date.nMonth = 12;
    date.nDay = 31;
    CHECK(XTime_ToHTTP(&date, sDate, sizeof(sDate)) > 0 && strstr(sDate, "31 Dec 2018"), "December retains its calendar year");
    return 0;
}

XTEST_MAIN(XTEST_CASE(calendar), XTEST_CASE(serialization), XTEST_CASE(http_year))
