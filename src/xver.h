/*!
 *  @file libxutils/src/xver.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Get additional information about library
 */

#ifndef __XUTILS_XLIBVER_H__
#define __XUTILS_XLIBVER_H__

#define XUTILS_VERSION_MAX     2
#define XUTILS_VERSION_MIN     4
#define XUTILS_BUILD_NUMBER    29

#ifdef __cplusplus
extern "C" {
#endif

const char* XUtils_Version(void);
const char* XUtils_VersionShort(void);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XLIBVER_H__ */
