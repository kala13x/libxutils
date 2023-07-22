/*!
 *  @file libxutils/src/crypt/base64.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of Base64 encrypt/decrypt functions.
 */

#ifndef __XUTILS_BASE64_H__
#define __XUTILS_BASE64_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"

#define XBASE64_TABLE_SIZE          256

char* XBase64_Encrypt(const uint8_t *pInput, size_t *pLength);
char* XBase64_Decrypt(const uint8_t *pInput, size_t *pLength);

char *XBase64_UrlEncrypt(const uint8_t *pInput, size_t *pLength);
char *XBase64_UrlDecrypt(const uint8_t *pInput, size_t *pLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_BASE64_H__ */