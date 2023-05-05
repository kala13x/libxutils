/*!
 *  @file libxutils/src/crypt/sha256.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief SHA-256 computing implementation for C/C++ based
 * on pseudocode for the SHA-256 algorithm from Wikipedia.
 */

#ifndef __XUTILS_SHA256_H__
#define __XUTILS_SHA256_H__

#ifdef __cplusplus
extern "C" {
#endif

#define XSHA256_BLOCK_SIZE          64
#define XSHA256_DIGEST_SIZE         32
#define XSHA256_PADDING_SIZE        19
#define XSHA256_LENGTH              64

#include "xstd.h"

typedef union
{
    uint32_t hBytes[8];
    uint8_t digest[32];
} xsha256_digest_t;

typedef union
{
    uint32_t wBytes[16];
    uint8_t block[64];
} xsha256_block_t;

typedef struct XSHA256
{
    xsha256_digest_t uDigest;
    xsha256_block_t uBlock;
    size_t nTotalSize;
    size_t nSize;
} xsha256_t;

void XSHA256_Init(xsha256_t *pSha);
void XSHA256_ProcessBlock(xsha256_t *pSha);
void XSHA256_Final(xsha256_t *pSha, uint8_t *pDigest);
void XSHA256_FinalRaw(xsha256_t *pSha, uint8_t *pDigest);
void XSHA256_Update(xsha256_t *pSha, const uint8_t *pData, size_t nLength);

XSTATUS XSHA256_Compute(uint8_t *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength);
XSTATUS XSHA256_ComputeHex(char *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength);

uint8_t* XSHA256_Encrypt(const uint8_t *pInput, size_t nLength);
char* XSHA256_EncryptHex(const uint8_t *pInput, size_t nLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_SHA256_H__ */