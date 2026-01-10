/*!
 *  @file libxutils/src/xver.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Get additional information about library
 */

#ifndef __XUTILS_VER_H__
#define __XUTILS_VER_H__

#define XUTILS_VERSION_MAX      2
#define XUTILS_VERSION_MIN      7
#define XUTILS_BUILD_NUMBER     19
#define XUTILS_RELEASE_DATE     "11Jan2025"

#ifdef __cplusplus
extern "C" {
#endif

const char* XUtils_Version(void);
const char* XUtils_VersionShort(void);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_VER_H__ */
