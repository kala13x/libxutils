/*!
 *  @file libxutils/src/crypt.h
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
#include "xtype.h"

#define XMD5_LENGTH                 32
#define XCHAR_MAP_SIZE              52
#define XCRC32_MAX_SIZE             16
#define XBASE64_TABLE_SIZE          256
#define XSHA256_BLOCK_SIZE          64
#define XSHA256_DIGEST_SIZE         32
#define XSHA256_LENGTH              64

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

char* XCrypt_SHA256(const uint8_t *pInput, size_t nLength);
char* XCrypt_MD5(const uint8_t *pInput, size_t nLength);

char* XCrypt_Reverse(const char *pInput, size_t nLength);
uint8_t* XCrypt_XOR(const uint8_t *pInput, size_t nLength, const uint8_t *pKey, size_t nKeyLen);

uint32_t XCrypt_CRC32(const uint8_t *pInput, size_t nLength);
uint32_t XCrypt_CRC32B(const uint8_t *pInput, size_t nLength);

char* XCrypt_Casear(const char *pInput, size_t nLength, size_t nKey);
char *XDecrypt_Casear(const char *pInput, size_t nLength, size_t nKey);

char* XCrypt_Base64(const uint8_t *pInput, size_t *pLength);
char* XDecrypt_Base64(const uint8_t *pInput, size_t *pLength);

uint8_t* XCrypt_AES(const uint8_t *pInput, size_t *pLength, const uint8_t *pKey, size_t nKeyLen, const uint8_t *pIV);
uint8_t* XDecrypt_AES(const uint8_t *pInput, size_t *pLength, const uint8_t *pKey, size_t nKeyLen, const uint8_t *pIV);

uint8_t* XCrypt_HEX(const uint8_t *pInput, size_t *pLength, const char* pSpace, size_t nColumns, xbool_t bLowCase);
uint8_t* XDecrypt_HEX(const uint8_t *pInput, size_t *pLength, xbool_t bLowCase);

typedef enum
{
    XC_AES = (uint8_t)0,
    XC_HEX,
    XC_XOR,
    XC_MD5,
    XC_CRC32,
    XC_CRC32B,
    XC_CASEAR,
    XC_BASE64,
    XC_SHA256,
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