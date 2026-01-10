/*!
 *  @file libxutils/src/crypt/sha1.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief SHA-1 algorithm implementation based on the pseudocode 
 * and description provided in the SHA-1 standard (FIPS PUB 180-1)
 */

#ifndef __XUTILS_SHA1_H__
#define __XUTILS_SHA1_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"

#define XSHA1_DIGEST_SIZE   20
#define XSHA1_BLOCK_SIZE    20
#define XSHA1_LENGTH        40

typedef union {
    uint8_t uBytes[64];
    uint32_t wBytes[16];
} xsha1_block_t;

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buffer[64];
} xsha1_ctx_t;

void XSHA1_Init(xsha1_ctx_t *pCtx);
void XSHA1_Update(xsha1_ctx_t *pCtx, const uint8_t* pData, uint32_t nLength);
void XSHA1_Final(xsha1_ctx_t *pCtx, uint8_t uDigest[XSHA1_DIGEST_SIZE]);
void XSHA1_Transform(uint32_t uState[5], const uint8_t uBuffer[64]);

XSTATUS XSHA1_Compute(uint8_t *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength);
XSTATUS XSHA1_ComputeSum(char *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength);

char* XSHA1_Sum(const uint8_t *pInput, size_t nLength);
uint8_t* XSHA1_Encrypt(const uint8_t *pInput, size_t nLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_SHA1_H__ */
