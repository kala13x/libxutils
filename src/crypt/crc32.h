/*!
 *  @file libxutils/src/crypt/crc32.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of CRC32 computing algorithms.
 */

#ifndef __XUTILS_CRC32_H__
#define __XUTILS_CRC32_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"

uint32_t XCRC32_Compute(const uint8_t *pInput, size_t nLength);
uint32_t XCRC32_ComputeB(const uint8_t *pInput, size_t nLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_CRC32_H__ */