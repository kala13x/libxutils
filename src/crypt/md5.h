/*!
 *  @file libxutils/src/crypt/md5.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief MD5 algorithm implementation for C/C++
 */

#ifndef __XUTILS_MD5_H__
#define __XUTILS_MD5_H__

#ifdef __cplusplus
extern "C" {
#endif

#define XMD5_DIGEST_SIZE            16
#define XMD5_BLOCK_SIZE             64
#define XMD5_LENGTH                 32

#include "xstd.h"

XSTATUS XMD5_Compute(uint8_t *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength);
uint8_t* XMD5_Encrypt(const uint8_t *pInput, size_t nLength);
char* XMD5_EncryptHex(const uint8_t *pInput, size_t nLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_MD5_H__ */