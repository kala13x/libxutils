/*!
 *  @file libxutils/src/crypt/aes.h
 *
 *  This source is part of "libxutils" project
 *  2015-2025  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of AES encryption based on tiny-AES-c project,
 * which was released under The Unlicense (public domain dedication).
 *
 * Modified for libxutils:
 * - Refactored code, adjusted function API.
 * - Added AES-CBC mode support with PKCS#7 padding.
 * - Added AES-XBC (CBC with random prefix) support to avoid PKCS#7 oracle issues.
 * - Added AES-SIV (RFC 5297) support for deterministic authenticated encryption.
 * - Added AES-CMAC (RFC 4493) and S2V support for synthetic IV derivation.
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
#define XAES_KEY_LENGTH     32
#define XAES_CMAC_RB        0x87  /* Constant for doubling in GF(2^128) */
#define XAES_XBC_HDR_SIZE   4     /* Header size for XBC mode (stores random prefix length) */

typedef enum {
    XAES_MODE_CBC = 0,  /* AES-CBC with PKCS#7 padding (default) */
    XAES_MODE_XBC,      /* AES-XBC: CBC with random prefix instead of PKCS#7 padding */
    XAES_MODE_SIV       /* AES-SIV deterministic authenticated encryption (RFC 5297) */
} xaes_mode_t;

typedef struct XAESKey {
    uint8_t aesKey[XAES_KEY_LENGTH];            /* AES key or CMAC key for SIV mode (RFC 4493) */
    uint8_t ctrKey[XAES_KEY_LENGTH];            /* CTR key for SIV mode (RFC 5297) */
    uint8_t IV[XAES_BLOCK_SIZE];                /* Initialization vector */
    uint8_t nContainIV;                         /* Flag to indicate if IV is self-contained in data */
    size_t nKeySize;                            /* Key size in bits */
} xaes_key_t;

typedef struct AESContext {
    xaes_mode_t eMode;                      /* Encryption mode */
    xaes_key_t key;                         /* AES key context */
    uint8_t CMACRoundKey[XAES_RKEY_SIZE];   /* CMAC round key for SIV mode */
    uint8_t roundKey[XAES_RKEY_SIZE];       /* Encrypt/Decrypt round key (CTR key for SIV) */
    uint8_t nNB;                            /* Number of columns (32-bit words) comprising the State */
    uint8_t nNK;                            /* Number of 32-bit words comprising the Cipher Key */
    uint8_t nNR;                            /* Number of rounds */
} xaes_ctx_t;

void XAES_InitSIVKey(xaes_key_t *pKey, const uint8_t *pMacKey, const uint8_t *pCtrKey, size_t nKeySize);
void XAES_InitKey(xaes_key_t *pKey, const uint8_t *pAESKey, size_t nKeySize, const uint8_t *pIV, uint8_t bContainIV);
int XAES_Init(xaes_ctx_t *pCtx, const xaes_key_t *pKey, xaes_mode_t eMode);

void XAES_ECB_Crypt(const xaes_ctx_t* pCtx, uint8_t* pBuffer);
void XAES_ECB_Decrypt(const xaes_ctx_t* pCtx, uint8_t* pBuffer);

uint8_t* XAES_CBC_Crypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength);
uint8_t* XAES_CBC_Decrypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength);

uint8_t* XAES_SIV_Crypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength);
uint8_t* XAES_SIV_Decrypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength);

uint8_t* XAES_XBC_Crypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength);
uint8_t* XAES_XBC_Decrypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength);

uint8_t* XAES_Encrypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength);
uint8_t* XAES_Decrypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_AES_H__ */
