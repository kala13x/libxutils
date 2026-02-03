/*!
 *  @file libxutils/src/crypt/aes.h
 *
 *  This source is part of "libxutils" project
 *  2015-2025  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of AES encryption based on tiny-AES-c project,
 * which was released under The Unlicense (public domain dedication).
 *
 * Modified: Refactored code, adjusted API, and added CBC mode with PKCS#7 padding.
 */

#ifndef __XUTILS_AES_H__
#define __XUTILS_AES_H__

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XAES_RKEY_SIZE      240
#define XAES_BLOCK_SIZE     16

typedef struct AESContext {
    uint8_t roundKey[XAES_RKEY_SIZE];   /* Encrypt/Decrypt round key */
    uint8_t IV[XAES_BLOCK_SIZE];        /* Initialization vector */
    uint8_t nSelfContainedIV;           /* Flag to indicate if IV is self-contained in data */
    size_t nKeySize;                    /* Key size in bits */
    uint8_t nNB;                        /* Number of columns (32-bit words) comprising the State */
    uint8_t nNK;                        /* Number of 32-bit words comprising the Cipher Key */
    uint8_t nNR;                        /* Number of rounds */
} xaes_ctx_t;

int XAES_SetKey(xaes_ctx_t *pCtx, const uint8_t *pKey, size_t nSize, const uint8_t *pIV, uint8_t nSelfContainedIV);
void XAES_EncryptBlock(const xaes_ctx_t* pCtx, uint8_t* pBuffer);
void XAES_DecryptBlock(const xaes_ctx_t* pCtx, uint8_t* pBuffer);

uint8_t* XAES_Encrypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength);
uint8_t* XAES_Decrypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_AES_H__ */
