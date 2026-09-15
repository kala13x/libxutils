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


static int XTest_formats(void)
{
    /* Every writer/reader pair has to agree on its own format, or a date
     * written by the library cannot be read back by it. */
    const xtime_t original = {2024, 3, 15, 14, 30, 45, 25};
    char sDate[128];
    xtime_t parsed;

    CHECK(XTime_ToStr(&original, sDate, sizeof(sDate)) == 16, "The compact form is sixteen digits");
    CHECK(strcmp(sDate, "2024031514304525") == 0, "The compact form is zero padded and unseparated");
    CHECK(XTime_FromStr(&parsed, sDate) == 7, "The compact form parses all seven fields");
    CHECK(parsed.nYear == 2024 && parsed.nMonth == 3 && parsed.nDay == 15, "The compact date round trips");
    CHECK(parsed.nHour == 14 && parsed.nMin == 30 && parsed.nSec == 45, "The compact time round trips");
    CHECK(parsed.nFraq == 25, "The compact fraction round trips");

    CHECK(XTime_ToLstr(&original, sDate, sizeof(sDate)) == 19, "The slashed form has a fixed width");
    CHECK(strcmp(sDate, "2024/03/15/14/30/45") == 0, "The slashed form separates every field");
    CHECK(XTime_FromLstr(&parsed, sDate) == 6, "The slashed form parses its six fields");
    CHECK(parsed.nYear == 2024 && parsed.nSec == 45, "The slashed form round trips");

    CHECK(XTime_ToRstr(&original, sDate, sizeof(sDate)) == 19, "The american form has a fixed width");
    CHECK(strcmp(sDate, "03/15/2024 14:30:45") == 0, "The american form leads with the month");
    CHECK(XTime_FromRstr(&parsed, sDate) > 0, "The american form parses what it wrote");
    CHECK(parsed.nYear == 2024 && parsed.nMonth == 3 && parsed.nDay == 15, "The american date round trips");
    CHECK(parsed.nHour == 14 && parsed.nMin == 30 && parsed.nSec == 45, "The american time round trips");

    /* An american date that does carry a fraction is still accepted. */
    CHECK(XTime_FromRstr(&parsed, "03/15/2024 14:30:45.25") == 7, "A fraction is accepted when present");
    CHECK(parsed.nFraq == 25, "The supplied fraction is kept");

    CHECK(XTime_ToHstr(&original, sDate, sizeof(sDate)) == 22, "The human form has a fixed width");
    CHECK(strcmp(sDate, "2024.03.15-14:30:45.25") == 0, "The human form carries the fraction");

    /* The human form is the one this library writes into its own log lines
     * and file names, so it has to be readable back: without the round trip
     * a timestamp it produced could not be parsed by anything using it. */
    memset(&parsed, 0, sizeof(parsed));
    CHECK(XTime_FromHstr(&parsed, sDate) == 7, "The human form parses all seven fields");
    CHECK(parsed.nYear == 2024 && parsed.nMonth == 3 && parsed.nDay == 15, "The human date round trips");
    CHECK(parsed.nHour == 14 && parsed.nMin == 30 && parsed.nSec == 45, "The human time round trips");
    CHECK(parsed.nFraq == 25, "The human fraction round trips");

    /* Unlike the american form, this one requires the fraction: XTime_ToHstr
     * always writes it, so anything short of all seven fields did not come
     * out of this library and is refused rather than silently zero filled. */
    memset(&parsed, 0, sizeof(parsed));
    CHECK(XTime_FromHstr(&parsed, "2024.03.15-14:30:45") == 0, "The human form requires its fraction");
    CHECK(XTime_FromHstr(&parsed, "2024.03.15") == 0, "A bare date is not a human timestamp");

    /* Every field the writer produces survives, including the extremes. */
    const xtime_t edges[] = {
        {2024,  1,  1,  0,  0,  0,   0},
        {2024, 12, 31, 23, 59, 59, XTIME_FRAQ_MAX},
        {2024,  2, 29, 12,  0,  0,  99},   /* a leap day */
        {1970,  1,  1,  0,  0,  0,   0}
    };

    for (size_t i = 0; i < sizeof(edges) / sizeof(*edges); i++)
    {
        char sEdge[64];
        CHECK(XTime_ToHstr(&edges[i], sEdge, sizeof(sEdge)) > 0, "The edge timestamp is written");

        xtime_t back;
        memset(&back, 0, sizeof(back));
        CHECK(XTime_FromHstr(&back, sEdge) == 7, "The edge timestamp parses back");
        CHECK(back.nYear == edges[i].nYear && back.nMonth == edges[i].nMonth &&
              back.nDay == edges[i].nDay && back.nHour == edges[i].nHour &&
              back.nMin == edges[i].nMin && back.nSec == edges[i].nSec &&
              back.nFraq == edges[i].nFraq, "Every field of the edge timestamp round trips");
    }

    /* A non-leap year has no twenty ninth of February. */
    CHECK(XTime_FromHstr(&parsed, "2023.02.29-00:00:00.00") == 0, "A leap day outside a leap year is rejected");
    CHECK(XTime_FromHstr(&parsed, "2024.02.29-00:00:00.00") == 7, "A leap day inside one is accepted");

    /* And the parser rejects what it cannot make a date out of. */
    CHECK(XTime_FromHstr(&parsed, "not a timestamp") == 0, "Text is not a human timestamp");
    CHECK(XTime_FromHstr(&parsed, "2024.13.15-14:30:45") == 0, "A thirteenth month is rejected");
    CHECK(XTime_FromHstr(&parsed, "2024.03.32-14:30:45") == 0, "A thirty second day is rejected");
    CHECK(XTime_FromHstr(&parsed, "2024.03.15-25:30:45") == 0, "A twenty fifth hour is rejected");
    CHECK(XTime_FromHstr(&parsed, "") == 0, "An empty string is rejected");
    CHECK(XTime_FromHstr(&parsed, NULL) == 0, "A missing string is rejected");
    CHECK(XTime_FromHstr(NULL, sDate) == 0, "A missing destination is rejected");

    CHECK(XTime_ToISO(&original, sDate, sizeof(sDate)) == 19, "The ISO form has a fixed width");
    CHECK(strcmp(sDate, "2024-03-15T14:30:45") == 0, "The ISO form uses the T separator");
    CHECK(XTime_FromISO(&parsed, sDate) == 6, "The ISO form parses its six fields");
    CHECK(parsed.nYear == 2024 && parsed.nMin == 30, "The ISO form round trips");

    CHECK(XTime_ToISO8601(&original, sDate, sizeof(sDate)) == 24, "The ISO8601 form has a fixed width");
    CHECK(strcmp(sDate, "2024-03-15T14:30:45.000Z") == 0, "The ISO8601 form is explicitly UTC");

    CHECK(XTime_ToHTTP(&original, sDate, sizeof(sDate)) == 29, "The HTTP form has a fixed width");
    CHECK(strcmp(sDate, "Fri, 15 Mar 2024 14:30:45 GMT") == 0, "The HTTP form names the weekday and month");
    return 0;
}

static int XTest_parse_guards(void)
{
    xtime_t parsed;

    /* A field outside its range is rejected rather than stored. */
    CHECK(XTime_FromISO(&parsed, "2024-13-15T00:00:00") == 0, "A thirteenth month is rejected");
    CHECK(XTime_FromISO(&parsed, "2024-00-15T00:00:00") == 0, "A zeroth month is rejected");
    CHECK(XTime_FromISO(&parsed, "2024-03-32T00:00:00") == 0, "A thirty second day is rejected");
    CHECK(XTime_FromISO(&parsed, "2024-03-00T00:00:00") == 0, "A zeroth day is rejected");
    CHECK(XTime_FromISO(&parsed, "2024-03-15T24:00:00") == 0, "A twenty fourth hour is rejected");
    CHECK(XTime_FromISO(&parsed, "2024-03-15T00:60:00") == 0, "A sixtieth minute is rejected");
    CHECK(XTime_FromISO(&parsed, "2024-03-15T00:00:60") == 0, "A sixtieth second is rejected");
    CHECK(XTime_FromISO(&parsed, "0000-03-15T00:00:00") == 0, "Year zero is rejected");

    /* The day is validated against the actual month length. */
    CHECK(XTime_FromISO(&parsed, "2023-02-29T00:00:00") == 0, "February 29 is rejected in a common year");
    CHECK(XTime_FromISO(&parsed, "2024-02-29T00:00:00") > 0, "February 29 is accepted in a leap year");
    CHECK(XTime_FromISO(&parsed, "2024-04-31T00:00:00") == 0, "April 31 is rejected");
    CHECK(XTime_FromISO(&parsed, "2024-04-30T00:00:00") > 0, "April 30 is accepted");

    /* Malformed and missing input. */
    CHECK(XTime_FromISO(&parsed, "not a date") == 0, "Unparsable text is rejected");
    CHECK(XTime_FromISO(&parsed, "") == 0, "An empty string is rejected");
    CHECK(XTime_FromISO(&parsed, "2024-03-15") == 0, "A date without a time is rejected");
    CHECK(XTime_FromISO(NULL, "2024-03-15T00:00:00") == 0, "A missing output is rejected");
    CHECK(XTime_FromStr(&parsed, "") == 0, "An empty compact string is rejected");
    CHECK(XTime_FromLstr(&parsed, "nonsense") == 0, "An unparsable slashed string is rejected");
    CHECK(XTime_FromRstr(&parsed, "nonsense") == 0, "An unparsable american string is rejected");

    /* A rejected parse leaves the output cleared rather than half filled. */
    XTime_FromISO(&parsed, "2024-13-15T00:00:00");
    CHECK(parsed.nYear == 0 && parsed.nMonth == 0, "A rejected parse leaves nothing behind");
    return 0;
}

static int XTest_conversions(void)
{
    const xtime_t original = {2024, 3, 15, 14, 30, 45, 25};

    /* There are two 64 bit forms and they are not interchangeable: ToU64 is
     * a raw reinterpretation of the struct for in-process use, while
     * Serialize builds the portable layout the XTIME_U64 macros decode. */
    uint64_t nPacked = XTime_ToU64(&original);
    xtime_t unpacked;
    XTime_FromU64(&unpacked, nPacked);
    CHECK(unpacked.nYear == original.nYear && unpacked.nMonth == original.nMonth, "The packed date round trips");
    CHECK(unpacked.nDay == original.nDay && unpacked.nHour == original.nHour, "The packed day and hour round trip");
    CHECK(unpacked.nMin == original.nMin && unpacked.nSec == original.nSec, "The packed time round trips");
    CHECK(unpacked.nFraq == original.nFraq, "The packed fraction round trips");

    uint64_t nWire = XTime_Serialize(&original);
    CHECK(XTIME_U64_YEAR(nWire) == 2024, "The year sits in the documented bits");
    CHECK(XTIME_U64_MONTH(nWire) == 3, "The month sits in the documented bits");
    CHECK(XTIME_U64_DAY(nWire) == 15, "The day sits in the documented bits");
    CHECK(XTIME_U64_HOUR(nWire) == 14, "The hour sits in the documented bits");
    CHECK(XTIME_U64_MIN(nWire) == 30, "The minute sits in the documented bits");
    CHECK(XTIME_U64_SEC(nWire) == 45, "The second sits in the documented bits");
    CHECK(XTIME_U64_FRAQ(nWire) == 25, "The fraction sits in the documented bits");

    xtime_t deserialized;
    XTime_Deserialize(&deserialized, nWire);
    CHECK(memcmp(&deserialized, &original, sizeof(deserialized)) == 0, "The wire form round trips every field");

    /* The struct tm bridge keeps the calendar fields. */
    struct tm tmValue;
    XTime_ToTm(&original, &tmValue);
    CHECK(tmValue.tm_year == 2024 - 1900, "The tm year is offset from 1900");
    CHECK(tmValue.tm_mon == 2, "The tm month is zero based");
    CHECK(tmValue.tm_mday == 15 && tmValue.tm_hour == 14, "The tm day and hour are copied");

    xtime_t fromTm;
    XTime_FromTm(&fromTm, &tmValue);
    CHECK(fromTm.nYear == 2024 && fromTm.nMonth == 3 && fromTm.nDay == 15, "The tm bridge round trips the date");
    CHECK(fromTm.nHour == 14 && fromTm.nMin == 30 && fromTm.nSec == 45, "The tm bridge round trips the time");

    /* Epoch conversion is anchored at the UTC epoch. */
    const xtime_t epoch = {1970, 1, 1, 0, 0, 0, 0};
    CHECK(XTime_ToEpochUTC(&epoch) == 0, "The epoch converts to zero");
    CHECK(XTime_ToEpochUTC(&original) == 1710513045u, "A known date converts to its known epoch");
    CHECK(XTime_ISOToEpochUTC("2024-03-15T14:30:45") == 1710513045u, "The ISO shortcut agrees with the struct path");
    CHECK(XTime_ISOToEpochUTC("not a date") == 0, "An unparsable ISO string converts to zero");

    /* XTime_FromEpoch renders local time, so it inverts ToEpochLocal rather
     * than ToEpochUTC. Asserting the round trip keeps this independent of
     * whichever zone the test machine happens to be in. */
    xtime_t fromEpoch;
    XTime_FromEpoch(&fromEpoch, (time_t)XTime_ToEpochLocal(&original));
    CHECK(fromEpoch.nYear == original.nYear && fromEpoch.nMonth == original.nMonth, "The local epoch round trips the date");
    CHECK(fromEpoch.nDay == original.nDay && fromEpoch.nHour == original.nHour, "The local epoch round trips the day and hour");
    CHECK(fromEpoch.nMin == original.nMin && fromEpoch.nSec == original.nSec, "The local epoch round trips the time");
    CHECK(fromEpoch.nFraq == 0, "The epoch carries no sub-second fraction");

    /* The two epoch views differ by exactly the local offset. */
    uint64_t nUTC = XTime_ToEpochUTC(&original);
    uint64_t nLocal = XTime_ToEpochLocal(&original);
    CHECK(nUTC != 0 && nLocal != 0, "Both epoch views are computed");
    CHECK((nUTC > nLocal ? nUTC - nLocal : nLocal - nUTC) % 900 == 0,
        "The two epoch views differ by a whole timezone offset");

    /* Copying and initializing. */
    xtime_t copy;
    XTime_Copy(&copy, &original);
    CHECK(memcmp(&copy, &original, sizeof(copy)) == 0, "A copy matches the source field for field");

    xtime_t cleared;
    XTime_Init(&cleared);
    CHECK(cleared.nYear == 0 && cleared.nMonth == 0 && cleared.nSec == 0, "An initialized time is cleared");
    return 0;
}

static int XTest_differences(void)
{
    const xtime_t start = {2024, 1, 1, 0, 0, 0, 0};
    const xtime_t end = {2025, 1, 1, 0, 0, 0, 0};

    /* 2024 is a leap year, so the span is 366 days. */
    double fSeconds = XTime_DiffSec(&end, &start);
    CHECK(fSeconds == 366.0 * 86400.0, "A leap year spans 366 days of seconds");
    CHECK(XTime_Diff(&end, &start, XTIME_DIFF_SEC) == fSeconds, "The second unit matches the direct difference");
    CHECK(XTime_Diff(&end, &start, XTIME_DIFF_DAY) == 366.0, "The day unit counts whole days");
    CHECK(XTime_Diff(&end, &start, XTIME_DIFF_HOUR) == 366.0 * 24.0, "The hour unit counts whole hours");
    CHECK(XTime_Diff(&end, &start, XTIME_DIFF_MIN) == 366.0 * 1440.0, "The minute unit counts whole minutes");
    CHECK(XTime_Diff(&end, &start, XTIME_DIFF_WEEK) > 52.0, "The week unit counts more than fifty two weeks");
    CHECK(XTime_Diff(&end, &start, XTIME_DIFF_MONTH) > 11.0, "The month unit counts about twelve months");
    CHECK(XTime_Diff(&end, &start, XTIME_DIFF_YEAR) > 0.99, "The year unit counts about one year");

    /* The difference is signed: the order of the arguments matters. */
    CHECK(XTime_DiffSec(&start, &end) == -fSeconds, "Reversing the arguments negates the difference");
    CHECK(XTime_DiffSec(&start, &start) == 0.0, "A time differs from itself by nothing");

    /* A one second and one day step. */
    const xtime_t second = {2024, 1, 1, 0, 0, 1, 0};
    CHECK(XTime_DiffSec(&second, &start) == 1.0, "One second apart is one second");
    const xtime_t day = {2024, 1, 2, 0, 0, 0, 0};
    CHECK(XTime_DiffSec(&day, &start) == 86400.0, "One day apart is eighty six thousand four hundred seconds");
    return 0;
}

static int XTest_normalize(void)
{
    /* Out of range fields are carried into the next unit rather than kept. */
    xtime_t overflow = {2024, 12, 31, 23, 59, 59, 0};
    overflow.nSec = 60;
    XTime_Make(&overflow);
    CHECK(overflow.nYear == 2025 && overflow.nMonth == 1 && overflow.nDay == 1, "A rolled over second advances the year");
    CHECK(overflow.nHour == 0 && overflow.nMin == 0 && overflow.nSec == 0, "The rolled over time wraps to midnight");

    /* The calendar helpers agree with the struct forms. */
    const xtime_t leap = {2024, 2, 1, 0, 0, 0, 0};
    CHECK(XTime_LeapYear(&leap) == 1, "2024 is reported as a leap year");
    CHECK(XTime_MonthDays(&leap) == 29, "February has 29 days in a leap year");

    const xtime_t common = {2023, 2, 1, 0, 0, 0, 0};
    CHECK(XTime_LeapYear(&common) == 0, "2023 is not reported as a leap year");
    CHECK(XTime_MonthDays(&common) == 28, "February has 28 days in a common year");

    /* Month lengths across a whole leap and common year. */
    const int leapDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int i = 0; i < 12; i++)
        CHECK(XTime_GetMonthDays(2024, i + 1) == leapDays[i], "Every leap year month has its correct length");

    /* The month is not validated here: anything that is not February or a
     * thirty day month falls through to 31, so callers have to range check
     * the month themselves before trusting the answer. XTime_FromISO and
     * friends do exactly that before consulting this. */
    CHECK(XTime_GetMonthDays(2024, 0) == 31, "An out of range month falls through to 31");
    CHECK(XTime_GetMonthDays(2024, 13) == 31, "A month past December falls through to 31");
    CHECK(XTime_FromISO(&overflow, "2024-13-31T00:00:00") == 0,
        "The parsers range check the month before consulting the month length");

    /* The century rule, across the boundaries that get it wrong. */
    CHECK(XTime_GetLeapYear(2000) == 1, "A year divisible by 400 is a leap year");
    CHECK(XTime_GetLeapYear(1900) == 0, "A century not divisible by 400 is not a leap year");
    CHECK(XTime_GetLeapYear(2024) == 1, "A year divisible by 4 is a leap year");
    CHECK(XTime_GetLeapYear(2023) == 0, "A year not divisible by 4 is not a leap year");
    CHECK(XTime_GetLeapYear(2100) == 0, "2100 is not a leap year");
    CHECK(XTime_GetLeapYear(2400) == 1, "2400 is a leap year");
    return 0;
}

static int XTest_clock(void)
{
    /* The clock sources have to advance and agree with each other. */
    xtime_t now;
    CHECK(XTime_Get(&now) > 0, "The wall clock is readable");
    CHECK(now.nYear >= 2020, "The wall clock is past the release date of this test");
    CHECK(now.nMonth >= 1 && now.nMonth <= 12, "The wall clock month is in range");
    CHECK(now.nDay >= 1 && now.nDay <= 31, "The wall clock day is in range");
    CHECK(now.nHour <= 23 && now.nMin <= 59 && now.nSec <= 60, "The wall clock time is in range");

    uint64_t nFirstMs = XTime_GetMs();
    CHECK(nFirstMs > 0, "The millisecond clock is readable");

    xtime_spec_t spec;
    CHECK(XTime_GetClock(&spec) >= 0, "The high resolution clock is readable");
    CHECK(spec.nNanoSec < 1000000000u, "The nanosecond remainder is under one second");

    /* The monotonic clock must not go backwards. */
    uint64_t nSecondMs = XTime_GetMs();
    CHECK(nSecondMs >= nFirstMs, "The millisecond clock never goes backwards");

    CHECK(XTime_GetUsec() < 1000000u, "The microsecond remainder is under one second");
    CHECK(XTime_GetStamp() > 0, "The timestamp is readable");
    CHECK(XTime_Serialized() > 0, "The serialized now is readable");
    CHECK(XTime_GetU64() > 0, "The packed now is readable");

    struct tm tmNow;
    XTime_GetTm(&tmNow);
    CHECK(tmNow.tm_year > 100, "The tm clock is past the year 2000");

    /* Each rendered form of "now" has its documented width. */
    char sNow[128];
    CHECK(XTime_GetStr(sNow, sizeof(sNow), XTIME_STR_SIMPLE) == 16, "The compact now has its fixed width");
    CHECK(XTime_GetStr(sNow, sizeof(sNow), XTIME_STR_HTTP) == 29, "The HTTP now has its fixed width");
    CHECK(XTime_GetStr(sNow, sizeof(sNow), XTIME_STR_ISO) == 19, "The ISO now has its fixed width");
    CHECK(XTime_GetStr(sNow, sizeof(sNow), XTIME_STR_ISO8601) == 24, "The ISO8601 now has its fixed width");
    CHECK(XTime_GetStr(sNow, sizeof(sNow), XTIME_STR_LSTR) == 19, "The slashed now has its fixed width");
    CHECK(XTime_GetStr(sNow, sizeof(sNow), XTIME_STR_HSTR) == 22, "The human now has its fixed width");
    return 0;
}


static int XTest_fraction_bounds(void)
{
    /* The fraction is hundredths of a second and every string format writes
     * it as two digits. A packed timestamp arrives over the wire with eight
     * raw bits in that field, so a peer can put 255 there: the formats then
     * ran past their documented widths and the value read back as a
     * different time. The domain is enforced where the value enters. */
    xtime_t now;
    XTime_Get(&now);
    CHECK(now.nFraq <= XTIME_FRAQ_MAX, "The clock never produces an out of range fraction");

    /* Straight off the wire, with the fraction field maxed out. */
    uint64_t nWire = ((uint64_t)2024 << 48) | ((uint64_t)3 << 40) | ((uint64_t)15 << 32) |
                     ((uint64_t)14 << 24) | ((uint64_t)30 << 16) | ((uint64_t)45 << 8) | 255;

    xtime_t wire;
    memset(&wire, 0, sizeof(wire));
    XTime_Deserialize(&wire, nWire);

    CHECK(wire.nFraq <= XTIME_FRAQ_MAX, "A hostile fraction is bounded on deserialize");
    CHECK(wire.nYear == 2024 && wire.nMonth == 3 && wire.nDay == 15, "The rest of the timestamp survives");
    CHECK(wire.nHour == 14 && wire.nMin == 30 && wire.nSec == 45, "Including the time of day");

    /* Which means the documented widths hold for anything off the wire. */
    char sCompact[64], sHuman[64];
    size_t nCompact = XTime_ToStr(&wire, sCompact, sizeof(sCompact));
    size_t nHuman = XTime_ToHstr(&wire, sHuman, sizeof(sHuman));

    CHECK(nCompact == 16, "The compact form stays sixteen characters");
    CHECK(nHuman == 22, "The human form stays twenty two characters");

    /* And it round trips to itself rather than to something else. */
    xtime_t back;
    memset(&back, 0, sizeof(back));
    int nParsed = XTime_FromStr(&back, sCompact);

    CHECK(nParsed == 7, "The compact form parses back");
    CHECK(back.nFraq == wire.nFraq, "The fraction survives the round trip");
    CHECK(back.nSec == wire.nSec && back.nMin == wire.nMin, "So does the rest of it");

    /* Every fraction the domain allows survives both string forms. */
    for (unsigned nFraq = 0; nFraq <= XTIME_FRAQ_MAX; nFraq++)
    {
        xtime_t one = {2024, 3, 15, 14, 30, 45, (uint8_t)nFraq};
        char sOne[64];

        CHECK(XTime_ToStr(&one, sOne, sizeof(sOne)) == 16, "Every fraction writes a fixed width");

        xtime_t parsedOne;
        memset(&parsedOne, 0, sizeof(parsedOne));
        CHECK(XTime_FromStr(&parsedOne, sOne) == 7, "Every fraction parses back");
        CHECK(parsedOne.nFraq == nFraq, "Every fraction round trips to itself");

        char sTwo[64];
        CHECK(XTime_ToHstr(&one, sTwo, sizeof(sTwo)) == 22, "The human form is fixed width too");

        memset(&parsedOne, 0, sizeof(parsedOne));
        CHECK(XTime_FromHstr(&parsedOne, sTwo) == 7, "And parses back");
        CHECK(parsedOne.nFraq == nFraq, "To the same fraction");
    }

    /* A written fraction of three digits is not a timestamp this library
     * produces, so the parsers do not have to accept it - what they must
     * not do is read it as a different, plausible looking time. */
    xtime_t wide;
    memset(&wide, 0, sizeof(wide));
    XTime_FromStr(&wide, "20240315143045100");
    CHECK(wide.nFraq <= XTIME_FRAQ_MAX, "Whatever is made of an overlong fraction stays in range");

    /* The serialize and deserialize pair agree with each other for every
     * value the domain allows. */
    for (unsigned nFraq = 0; nFraq <= XTIME_FRAQ_MAX; nFraq += 7)
    {
        xtime_t one = {2024, 3, 15, 14, 30, 45, (uint8_t)nFraq};
        /* Serialize is the packed bit form Deserialize reads. ToU64/FromU64
         * are a separate pair: a raw reinterpret of the struct's bytes,
         * which is why they are not mixed with these two. */
        uint64_t nPacked = XTime_Serialize(&one);

        xtime_t two;
        memset(&two, 0, sizeof(two));
        XTime_Deserialize(&two, nPacked);

        CHECK(two.nYear == one.nYear && two.nMonth == one.nMonth && two.nDay == one.nDay &&
              two.nHour == one.nHour && two.nMin == one.nMin && two.nSec == one.nSec &&
              two.nFraq == one.nFraq, "The packed form round trips every field");
    }

    /* The union pair round trips a struct through raw bytes, which is a
     * different contract: it copies whatever is there rather than packing
     * fields, so it must be given a timestamp this library made and never
     * bytes off the wire. It still has to round trip its own output. */
    xtime_t source = {2024, 6, 1, 8, 15, 30, 42};
    uint64_t nRaw = XTime_ToU64(&source);

    xtime_t raw;
    memset(&raw, 0, sizeof(raw));
    XTime_FromU64(&raw, nRaw);

    CHECK(raw.nYear == source.nYear && raw.nMonth == source.nMonth && raw.nDay == source.nDay &&
          raw.nHour == source.nHour && raw.nMin == source.nMin && raw.nSec == source.nSec &&
          raw.nFraq == source.nFraq, "The raw pair round trips its own output");
    CHECK(XTime_ToU64(NULL) == 0, "A missing timestamp has no raw form");

    return 0;
}

XTEST_MAIN(
    XTEST_CASE(calendar),
    XTEST_CASE(serialization),
    XTEST_CASE(http_year),
    XTEST_CASE(formats),
    XTEST_CASE(parse_guards),
    XTEST_CASE(conversions),
    XTEST_CASE(differences),
    XTEST_CASE(normalize),
    XTEST_CASE(clock),
    XTEST_CASE(fraction_bounds)
)
