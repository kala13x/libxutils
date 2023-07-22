/*!
 *  @file libxutils/src/crypt/aes.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of Advanced Encryption Standard
 * based on FIPS-197 implementation by Christophe Devine.
 */

#ifndef __XUTILS_AES_H__
#define __XUTILS_AES_H__

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XAES_BLOCK_SIZE     16
#define XAES_RKEY_SIZE      64

typedef struct AESContext {
    uint32_t encKeys[XAES_RKEY_SIZE];   /* Dncryption round keys */
    uint32_t decKeys[XAES_RKEY_SIZE];   /* Decryption round keys */
    uint8_t IV[XAES_BLOCK_SIZE];        /* Initialization vector */
    size_t nRounds;                     /* Number of rounds */
} xaes_context_t;

void XAES_SetKey(xaes_context_t *pCtx, const uint8_t *pKey, size_t nSize, const uint8_t *pIV);
void XAES_EncryptBlock(xaes_context_t *pCtx, uint8_t output[XAES_BLOCK_SIZE], const uint8_t input[XAES_BLOCK_SIZE]);
void XAES_DecryptBlock(xaes_context_t *pCtx, uint8_t output[XAES_BLOCK_SIZE], const uint8_t input[XAES_BLOCK_SIZE]);

uint8_t* XAES_Encrypt(xaes_context_t *pCtx, const uint8_t *pInput, size_t *pLength);
uint8_t* XAES_Decrypt(xaes_context_t *pCtx, const uint8_t *pInput, size_t *pLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_AES_H__ */
