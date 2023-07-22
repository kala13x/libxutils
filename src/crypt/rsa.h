/*!
 *  @file libxutils/src/crypt/rsa.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief RSA implementation with openssl library
 */

#ifndef __XUTILS_RSA_H__
#define __XUTILS_RSA_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _XUTILS_USE_SSL
#include "xstd.h"
#include "sha256.h"

#define OPENSSL_API_COMPAT XSSL_MINIMAL_API
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#define XCRYPT_USE_SSL      XTRUE
#define XRSA_KEY_SIZE       2048
#define XRSA_PUB_EXP        65537

char* XSSL_LastErrors(size_t *pOutLen);

typedef struct XRSAKeys {
    RSA *pKeyPair;
    uint8_t nPadding;

    char *pPrivateKey;
    size_t nPrivKeyLen;

    char *pPublicKey;
    size_t nPubKeyLen;

    char *pErrorStr;
    size_t nErrorLen;
} xrsa_ctx_t;

void XRSA_Init(xrsa_ctx_t *pCtx);
void XRSA_Destroy(xrsa_ctx_t *pCtx);

uint8_t* XRSA_Crypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength);
uint8_t* XRSA_Decrypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength);

uint8_t* XRSA_PrivCrypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength);
uint8_t* XRSA_PubDecrypt(xrsa_ctx_t *pCtx, const uint8_t *pData, size_t nLength, size_t *pOutLength);

XSTATUS XRSA_GenerateKeys(xrsa_ctx_t *pCtx, size_t nKeyLength, size_t nPubKeyExp);
XSTATUS XRSA_LoadKeyFiles(xrsa_ctx_t *pCtx, const char *pPrivPath, const char *pPubPath);
XSTATUS XRSA_LoadPrivKeyFile(xrsa_ctx_t *pCtx, const char *pPath);
XSTATUS XRSA_LoadPubKeyFile(xrsa_ctx_t *pCtx, const char *pPath);
XSTATUS XRSA_LoadPrivKey(xrsa_ctx_t *pCtx);
XSTATUS XRSA_LoadPubKey(xrsa_ctx_t *pCtx);

XSTATUS XRSA_SetPubKey(xrsa_ctx_t *pCtx, const char *pPubKey, size_t nLength);
XSTATUS XRSA_SetPrivKey(xrsa_ctx_t *pCtx, const char *pPrivKey, size_t nLength);

uint8_t* XCrypt_RSA(const uint8_t *pInput, size_t nLength, const char *pPubKey, size_t nKeyLen, size_t *pOutLen);
uint8_t* XCrypt_PrivRSA(const uint8_t *pInput, size_t nLength, const char *pPrivKey, size_t nKeyLen, size_t *pOutLen);

uint8_t* XDecrypt_RSA(const uint8_t *pInput, size_t nLength, const char *pPrivKey, size_t nKeyLen, size_t *pOutLen);
uint8_t* XDecrypt_PubRSA(const uint8_t *pInput, size_t nLength, const char *pPubKey, size_t nKeyLen, size_t *pOutLen);

uint8_t* XCrypt_RS256(const uint8_t *pInput, size_t nLength, const char *pPrivKey, size_t nKeyLen, size_t *pOutLen);
XSTATUS XCrypt_VerifyRS256(const uint8_t *pSignature, size_t nSignatureLen, const uint8_t *pData, size_t nLength, const char *pPubKey, size_t nKeyLen);

#endif /* XCRYPT_USE_SSL */

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_RSA_H__ */