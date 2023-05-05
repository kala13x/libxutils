/*!
 *  @file libxutils/src/crypt/hmac.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief HMAC SHA-256 and MD5 algorithm implementation for C/C++
 * based on sample code from https://www.rfc-editor.org/rfc/rfc2104
 */

#include "hmac.h"
#include "sha256.h"
#include "base64.h"
#include "md5.h"
#include "xstr.h"

XSTATUS XHMAC_SHA256(uint8_t *pOutput, size_t nSize, const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen)
{
    XASSERT((nSize >= XSHA256_DIGEST_SIZE &&
             nLength && nKeyLen && pOutput &&
             pData && pKey),
            XSTDINV);

    uint8_t i, digest[XSHA256_DIGEST_SIZE];
    uint8_t kIpad[XSHA256_BLOCK_SIZE] = {0};
    uint8_t kOpad[XSHA256_BLOCK_SIZE] = {0};
    xsha256_t xsha;

    /* if key is longer than 64 bytes reset it to key=SHA256(key) */
    if (nKeyLen > XSHA256_BLOCK_SIZE)
    {
        uint8_t key[XSHA256_DIGEST_SIZE] = {0};
        XSHA256_Compute(key, sizeof(key), pKey, nKeyLen);

        memcpy(kIpad, pKey, XSHA256_DIGEST_SIZE);
        memcpy(kOpad, pKey, XSHA256_DIGEST_SIZE);
    }
    else
    {
        memcpy(kIpad, pKey, nKeyLen);
        memcpy(kOpad, pKey, nKeyLen);
    }

    /* XOR key with ipad and opad values */
    for (i = 0; i < XSHA256_BLOCK_SIZE; i++)
    {
        kIpad[i] ^= 0x36;
        kOpad[i] ^= 0x5c;
    }

    /* Perform inner SHA256 */
    XSHA256_Init(&xsha);
    XSHA256_Update(&xsha, kIpad, sizeof(kIpad));
    XSHA256_Update(&xsha, pData, nLength);
    XSHA256_Final(&xsha, digest);

    /* Perform outer SHA256 */
    XSHA256_Init(&xsha);
    XSHA256_Update(&xsha, kOpad, sizeof(kOpad));
    XSHA256_Update(&xsha, digest, sizeof(digest));
    XSHA256_Final(&xsha, pOutput);

    return XSTDOK;
}

XSTATUS XHMAC_SHA256_HEX(char *pOutput, size_t nSize, const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen)
{
    if (pOutput == NULL || nSize < XSHA256_LENGTH + 1)
        return XSTDERR;
    uint8_t i, hash[XSHA256_DIGEST_SIZE];

    XHMAC_SHA256(hash, sizeof(hash), pData, nLength, pKey, nKeyLen);
    for (i = 0; i < sizeof(hash); i++)
        xstrncpyf(pOutput + i * 2, 3, "%02x", (unsigned int)hash[i]);

    pOutput[XSHA256_LENGTH] = '\0';
    return XSTDOK;
}

char *XHMAC_SHA256_Base64(const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen, size_t *pOutLen)
{
    uint8_t hash[XSHA256_DIGEST_SIZE];
    *pOutLen = sizeof(hash);

    XHMAC_SHA256(hash, sizeof(hash), pData, nLength, pKey, nKeyLen);
    return XBase64_UrlEncrypt(hash, pOutLen);
}

char *XHMAC_SHA256_Crypt(const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen)
{
    size_t nSize = XSHA256_LENGTH + 1;
    char *pOutput = (char *)malloc(nSize);
    if (pOutput == NULL)
        return NULL;

    XHMAC_SHA256_HEX(pOutput, nSize, pData, nLength, pKey, nKeyLen);
    return pOutput;
}

XSTATUS XHMAC_MD5(char *pOutput, size_t nSize, const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen)
{
    XASSERT((nSize >= XMD5_LENGTH + 1 &&
             nLength && nKeyLen && pOutput &&
             pData && pKey),
            XSTDINV);

    uint8_t i, digest[XMD5_DIGEST_SIZE];
    uint8_t hash[XMD5_DIGEST_SIZE] = {0};
    uint8_t kIpad[XMD5_BLOCK_SIZE] = {0};
    uint8_t kOpad[XMD5_BLOCK_SIZE] = {0};

    /* if key is longer than 64 bytes reset it to key=MD5(key) */
    if (nKeyLen > XMD5_BLOCK_SIZE)
    {
        uint8_t key[XMD5_DIGEST_SIZE] = {0};
        XMD5_Compute(key, sizeof(key), pKey, nKeyLen);

        memcpy(kIpad, pKey, XMD5_DIGEST_SIZE);
        memcpy(kOpad, pKey, XMD5_DIGEST_SIZE);
    }
    else
    {
        memcpy(kIpad, pKey, nKeyLen);
        memcpy(kOpad, pKey, nKeyLen);
    }

    /* XOR key with ipad and opad values */
    for (i = 0; i < XMD5_BLOCK_SIZE; i++)
    {
        kIpad[i] ^= 0x36;
        kOpad[i] ^= 0x5c;
    }

    /* Perform inner MD5 */
    size_t nBufLen = sizeof(kIpad) + nLength;
    uint8_t *pPadBuf = (uint8_t *)malloc(nBufLen);
    XASSERT(pPadBuf, XSTDERR);

    memcpy(pPadBuf, kIpad, sizeof(kIpad));
    memcpy(&pPadBuf[sizeof(kIpad)], pData, nLength);

    XMD5_Compute(digest, sizeof(digest), pPadBuf, nBufLen);
    free(pPadBuf);

    /* Perform outer MD5 */
    nBufLen = sizeof(kOpad) + sizeof(digest);
    pPadBuf = (uint8_t *)malloc(nBufLen);
    XASSERT(pPadBuf, XSTDERR);

    memcpy(pPadBuf, kOpad, sizeof(kOpad));
    memcpy(&pPadBuf[sizeof(kOpad)], digest, sizeof(digest));

    XMD5_Compute(hash, sizeof(hash), pPadBuf, nBufLen);
    free(pPadBuf);

    xstrncpyf(pOutput, nSize,
              "%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x"
              "%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x",
              hash[0], hash[1], hash[2], hash[3],
              hash[4], hash[5], hash[6], hash[7],
              hash[8], hash[9], hash[10], hash[11],
              hash[12], hash[13], hash[14], hash[15]);

    pOutput[XMD5_LENGTH] = '\0';
    return XSTDOK;
}

char *XHMAC_MD5_Crypt(const uint8_t *pData, size_t nLength, const uint8_t *pKey, size_t nKeyLen)
{
    size_t nSize = XMD5_LENGTH + 1;
    char *pOutput = (char *)malloc(nSize);
    if (pOutput == NULL)
        return NULL;

    XHMAC_MD5(pOutput, nSize, pData, nLength, pKey, nKeyLen);
    return pOutput;
}