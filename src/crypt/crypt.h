/*!
 *  @file libxutils/src/crypt/crypt.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of the various crypt algorithms
 */

#ifndef __XUTILS_CRYPT_H__
#define __XUTILS_CRYPT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"
#include "xstr.h"

uint8_t* XCrypt_AES(const uint8_t *pInput, size_t *pLength, const uint8_t *pKey, size_t nKeyLen, const uint8_t *pIV);
uint8_t* XDecrypt_AES(const uint8_t *pInput, size_t *pLength, const uint8_t *pKey, size_t nKeyLen, const uint8_t *pIV);

uint8_t* XCrypt_HEX(const uint8_t *pInput, size_t *pLength, const char* pSpace, size_t nColumns, xbool_t bLowCase);
uint8_t* XDecrypt_HEX(const uint8_t *pInput, size_t *pLength, xbool_t bLowCase);

uint8_t* XCrypt_XOR(const uint8_t *pInput, size_t nLength, const uint8_t *pKey, size_t nKeyLen);
char* XCrypt_Reverse(const char *pInput, size_t nLength);

char* XCrypt_Casear(const char *pInput, size_t nLength, size_t nKey);
char *XDecrypt_Casear(const char *pInput, size_t nLength, size_t nKey);

typedef enum
{
    XC_AES = (uint8_t)0,
    XC_HEX,
    XC_XOR,
    XC_MD5,
    XC_MD5U,
#ifdef _XUTILS_USE_SSL
    XC_RSA,
    XC_RSAPR,
    XC_RS256,
#endif
    XC_CRC32,
    XC_CRC32B,
    XC_CASEAR,
    XC_BASE64,
    XC_B64URL,
    XC_SHA1,
    XC_SHA1U,
    XC_SHA256,
    XC_SHA256U,
    XC_HS256,
    XC_HMD5,
    XC_REVERSE,
    XC_MULTY,
    XC_INVALID
} xcrypt_chipher_t;

typedef enum
{
    XCB_INVALID = (uint8_t)0,
    XCB_ERROR,
    XCB_KEY
} xcrypt_cb_type_t;

typedef struct XCryptKey 
{
    xcrypt_chipher_t eCipher;
    char sKey[XSTR_MIN];
    size_t nLength;
} xcrypt_key_t;

typedef xbool_t(*xcrypt_cb_t)(xcrypt_cb_type_t, void*, void*);

typedef struct XCryptCtx
{
    xcrypt_cb_t callback;
    void *pUserPtr;

    xbool_t bDecrypt;
    size_t nColumns;
    char *pCiphers;
} xcrypt_ctx_t;

xcrypt_chipher_t XCrypt_GetCipher(const char *pCipher);
const char* XCrypt_GetCipherStr(xcrypt_chipher_t eCipher);

void XCrypt_Init(xcrypt_ctx_t *pCtx, xbool_t bDecrypt, char *pCiphers, xcrypt_cb_t callback, void *pUser);
uint8_t* XDecrypt_Single(xcrypt_ctx_t *pCtx, xcrypt_chipher_t eCipher, const uint8_t *pInput, size_t *pLength);
uint8_t* XCrypt_Single(xcrypt_ctx_t *pCtx, xcrypt_chipher_t eCipher, const uint8_t *pInput, size_t *pLength);
uint8_t* XCrypt_Multy(xcrypt_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_CRYPT_H__ */