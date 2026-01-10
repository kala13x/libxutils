/*!
 *  @file libxutils/src/xver.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Get additional information about library
 */

#include "str.h"
#include "xver.h"

static const char g_VersionShort[] =
    XSTRFY(XUTILS_VERSION_MAX) "."
    XSTRFY(XUTILS_VERSION_MIN) "."
    XSTRFY(XUTILS_BUILD_NUMBER);

static const char g_VersionLong[] =
    XSTRFY(XUTILS_VERSION_MAX) "."
    XSTRFY(XUTILS_VERSION_MIN) " build "
    XSTRFY(XUTILS_BUILD_NUMBER)
    " (" XUTILS_RELEASE_DATE ")";

const char* XUtils_Version(void) { return g_VersionLong; }
const char* XUtils_VersionShort(void) { return g_VersionShort; }
