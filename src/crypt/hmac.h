/*!
 *  @file libxutils/src/crypt/hmac.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief HMAC SHA-256 algorithm implementation for C/C++ based
 * on MD5 sample code from https://www.rfc-editor.org/rfc/rfc2104
 */

#ifndef __XUTILS_HMAC_H__
#define __XUTILS_HMAC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"

XSTATUS XHMAC_SHA256(uint8_t *pOutput, size_t nSize, const uint8_t* pData, size_t nLength, const uint8_t* pKey, size_t nKeyLen);
XSTATUS XHMAC_SHA256_HEX(char *pOutput, size_t nSize, const uint8_t* pData, size_t nLength, const uint8_t* pKey, size_t nKeyLen);
char* XHMAC_SHA256_B64(const uint8_t* pData, size_t nLength, const uint8_t* pKey, size_t nKeyLen, size_t *pOutLen);
char* XHMAC_SHA256_NEW(const uint8_t* pData, size_t nLength, const uint8_t* pKey, size_t nKeyLen);

XSTATUS XHMAC_MD5(char *pOutput, size_t nSize, const uint8_t* pData, size_t nLength, const uint8_t* pKey, size_t nKeyLen);
char* XHMAC_MD5_NEW(const uint8_t* pData, size_t nLength, const uint8_t* pKey, size_t nKeyLen);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_HMAC_H__ */
