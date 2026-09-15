/* libxutils: exact float representation and concurrent conversion independence. */
#include "test.h"
#include "type.h"
#include "thread.h"
#include "xver.h"

typedef struct xtest_type_ {
    uint32_t nBits;
    xbool_t bFailed;
} xtest_type_t;

static void *XTest_Convert(void *pContext)
{
    xtest_type_t *pTest = (xtest_type_t*)pContext;
    for (int i = 0; i < 100000; i++)
    {
        float fValue = XU32ToFloat(pTest->nBits);
        uint32_t nBits;
        memcpy(&nBits, &fValue, sizeof(nBits));
        if (nBits != pTest->nBits || XFloatToU32(fValue) != pTest->nBits) pTest->bFailed = XTRUE;
    }
    return NULL;
}

static int XTest_float_bits(void)
{
    const uint32_t bits[] = {0, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000, 1, 0x7f7fffff};
    for (size_t i = 0; i < sizeof(bits) / sizeof(*bits); i++)
    {
        float fValue = XU32ToFloat(bits[i]);
        uint32_t nActual;
        memcpy(&nActual, &fValue, sizeof(nActual));
        CHECK(nActual == bits[i] && XFloatToU32(fValue) == bits[i], "Conversions retain sign, subnormal and infinity bits");
    }
    return 0;
}

static int XTest_concurrent(void)
{
    xtest_type_t contexts[4] = {{0x3f800000, 0}, {0xbf800000, 0}, {0x41200000, 0}, {0xc1200000, 0}};
    xthread_t threads[4];
    for (int i = 0; i < 4; i++)
        CHECK(XThread_Create(&threads[i], XTest_Convert, &contexts[i], XFALSE) > 0, "Start conversion workers");
    for (int i = 0; i < 4; i++) XThread_Join(&threads[i]);
    for (int i = 0; i < 4; i++) CHECK(!contexts[i].bFailed, "Concurrent callers must not share conversion scratch storage");
    return 0;
}


static int XTest_printable(void)
{
    /* Scanning stops at the first NUL, so trailing binary past a terminator
     * is not what decides printability. */
    const uint8_t printable[] = "plain ascii 123";
    CHECK(XTypeIsPrint(printable, sizeof(printable) - 1) == XTRUE, "Plain ascii is printable");
    CHECK(XTypeIsPrint(printable, 0) == XTRUE, "An empty range is vacuously printable");

    const uint8_t terminated[] = {'o', 'k', 0x00, 0x01, 0x02};
    CHECK(XTypeIsPrint(terminated, sizeof(terminated)) == XTRUE, "Bytes past a terminator are not examined");

    const uint8_t binary[] = {'o', 'k', 0x01, 'z'};
    CHECK(XTypeIsPrint(binary, sizeof(binary)) == XFALSE, "A control byte makes the range unprintable");

    const uint8_t newline[] = {'a', '\n', 'b'};
    CHECK(XTypeIsPrint(newline, sizeof(newline)) == XFALSE, "A newline is not a printable character");

    const uint8_t tab[] = {'a', '\t'};
    CHECK(XTypeIsPrint(tab, sizeof(tab)) == XFALSE, "A tab is not a printable character");

    const uint8_t high[] = {'a', 0xff};
    CHECK(XTypeIsPrint(high, sizeof(high)) == XFALSE, "A high byte is not printable");

    /* Every byte, classified one at a time. */
    for (int i = 1; i < 256; i++)
    {
        uint8_t byte = (uint8_t)i;
        xbool_t bExpected = (i >= 0x20 && i < 0x7f) ? XTRUE : XFALSE;
        CHECK(XTypeIsPrint(&byte, 1) == bExpected, "Printability matches the ascii printable range");
    }
    return 0;
}

static int XTest_byte_units(void)
{
    char sUnit[64];

    /* Each threshold is exclusive, so the boundary value stays in the
     * smaller unit and one byte more crosses over. */
    CHECK(XBytesToUnit(sUnit, sizeof(sUnit), 0, XFALSE) > 0 && strcmp(sUnit, "0.00  B") == 0,
        "Zero bytes render in bytes");
    CHECK(XBytesToUnit(sUnit, sizeof(sUnit), 1024, XFALSE) > 0 && strcmp(sUnit, "1024.00  B") == 0,
        "Exactly one kilobyte still renders in bytes");
    CHECK(XBytesToUnit(sUnit, sizeof(sUnit), 1025, XFALSE) > 0 && strcmp(sUnit, "1.00 KB") == 0,
        "One byte past the threshold renders in kilobytes");
    CHECK(XBytesToUnit(sUnit, sizeof(sUnit), 1048576, XFALSE) > 0 && strstr(sUnit, "KB") != NULL,
        "Exactly one megabyte still renders in kilobytes");
    CHECK(XBytesToUnit(sUnit, sizeof(sUnit), 1048577, XFALSE) > 0 && strcmp(sUnit, "1.00 MB") == 0,
        "One byte past the threshold renders in megabytes");
    CHECK(XBytesToUnit(sUnit, sizeof(sUnit), 1073741825u, XFALSE) > 0 && strcmp(sUnit, "1 GB") == 0,
        "Gigabytes render without a fraction");

    /* The short form drops the space and shortens the unit name. */
    CHECK(XBytesToUnit(sUnit, sizeof(sUnit), 2048, XTRUE) > 0 && strcmp(sUnit, "2.0K") == 0,
        "The short form has no space and a one letter unit");
    CHECK(XBytesToUnit(sUnit, sizeof(sUnit), 5, XTRUE) > 0 && strcmp(sUnit, "5.0B") == 0,
        "Short bytes use a bare B");
    CHECK(XBytesToUnit(sUnit, sizeof(sUnit), 3221225472u, XTRUE) > 0 && strcmp(sUnit, "3G") == 0,
        "Short gigabytes have no fraction");

    /* A destination too small to hold the result truncates rather than
     * overflowing, and a missing destination writes nothing. */
    char sTiny[4];
    size_t nWritten = XBytesToUnit(sTiny, sizeof(sTiny), 1073741825u, XFALSE);
    CHECK(nWritten < sizeof(sTiny), "A short destination truncates");
    /* A rejected destination is reported through the string layer, which
     * hands back the error sentinel widened to size_t rather than a count. */
    CHECK(XBytesToUnit(NULL, 16, 1024, XFALSE) == (size_t)XSTDERR, "A missing destination is rejected");
    CHECK(XBytesToUnit(sUnit, 0, 1024, XFALSE) == (size_t)XSTDERR, "A zero sized destination is rejected");
    return 0;
}

static int XTest_kb_units(void)
{
    char sUnit[64];

    CHECK(XKBToUnit(sUnit, sizeof(sUnit), 0, XFALSE) > 0 && strcmp(sUnit, "0.00  KB") == 0,
        "Zero kilobytes render in kilobytes");
    CHECK(XKBToUnit(sUnit, sizeof(sUnit), 1024, XFALSE) > 0 && strstr(sUnit, "KB") != NULL,
        "Exactly one megabyte of kilobytes still renders in kilobytes");
    CHECK(XKBToUnit(sUnit, sizeof(sUnit), 1025, XFALSE) > 0 && strcmp(sUnit, "1.00  MB") == 0,
        "One kilobyte past the threshold renders in megabytes");
    CHECK(XKBToUnit(sUnit, sizeof(sUnit), 1048577, XFALSE) > 0 && strcmp(sUnit, "1.00  GB") == 0,
        "Past a gigabyte of kilobytes renders in gigabytes");
    CHECK(XKBToUnit(sUnit, sizeof(sUnit), 1073741825u, XFALSE) > 0 && strcmp(sUnit, "1.00  TB") == 0,
        "Past a terabyte of kilobytes renders in terabytes");

    CHECK(XKBToUnit(sUnit, sizeof(sUnit), 2048, XTRUE) > 0 && strcmp(sUnit, "2.0M") == 0,
        "The short form has no space and a one letter unit");
    CHECK(XKBToUnit(sUnit, sizeof(sUnit), 512, XTRUE) > 0 && strcmp(sUnit, "512.0K") == 0,
        "Short kilobytes use a bare K");
    CHECK(XKBToUnit(NULL, 16, 1024, XFALSE) == (size_t)XSTDERR, "A missing destination is rejected");
    CHECK(XKBToUnit(sUnit, 0, 1024, XFALSE) == (size_t)XSTDERR, "A zero sized destination is rejected");
    return 0;
}

static int XTest_version(void)
{
    const char *pShort = XUtils_VersionShort();
    const char *pLong = XUtils_Version();

    CHECK(pShort != NULL && *pShort != '\0', "The short version is reported");
    CHECK(pLong != NULL && *pLong != '\0', "The long version is reported");

    /* The short form is the dotted triple; the long form contains it. */
    int nMajor = -1, nMinor = -1, nBuild = -1;
    CHECK(sscanf(pShort, "%d.%d.%d", &nMajor, &nMinor, &nBuild) == 3, "The short version is a dotted triple");
    CHECK(nMajor == XUTILS_VERSION_MAX, "The major version matches the header");
    CHECK(nMinor == XUTILS_VERSION_MIN, "The minor version matches the header");
    CHECK(nBuild == XUTILS_BUILD_NUMBER, "The build number matches the header");

    CHECK(strstr(pLong, "build") != NULL, "The long version names the build");
    CHECK(strstr(pLong, XUTILS_RELEASE_DATE) != NULL, "The long version carries the release date");

    /* Both are constants, so repeated calls hand back the same storage. */
    CHECK(XUtils_VersionShort() == pShort && XUtils_Version() == pLong,
        "The version strings are static, not rebuilt per call");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(float_bits),
    XTEST_CASE(concurrent),
    XTEST_CASE(printable),
    XTEST_CASE(byte_units),
    XTEST_CASE(kb_units),
    XTEST_CASE(version)
)
