/*!
 *  @file libxutils/src/xver.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Get additional information about library
 */

#include "xstd.h"
#include "xver.h"
#include "xstr.h"
#include "sync.h"

static XATOMIC g_nHaveVerShort = 0;
static XATOMIC g_nHaveVerLong = 0;
static char g_versionShort[128];
static char g_versionLong[256];

const char* XUtils_Version(void)
{
    if (!XSYNC_ATOMIC_GET(&g_nHaveVerLong))
    {
        xstrncpyf(g_versionLong, sizeof(g_versionLong), "%d.%d build %d (%s)", 
            XUTILS_VERSION_MAX, XUTILS_VERSION_MIN, XUTILS_BUILD_NUMBER, __DATE__);

        XSYNC_ATOMIC_SET(&g_nHaveVerLong, 1);
    }

    return g_versionLong;
}

const char* XUtils_VersionShort(void)
{
    if (!XSYNC_ATOMIC_GET(&g_nHaveVerShort))
    {
        xstrncpyf(g_versionShort, sizeof(g_versionShort), "%d.%d.%d", 
            XUTILS_VERSION_MAX, XUTILS_VERSION_MIN, XUTILS_BUILD_NUMBER);

        XSYNC_ATOMIC_SET(&g_nHaveVerShort, 1);
    }

    return g_versionShort;
}
