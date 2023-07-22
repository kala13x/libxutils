/*!
 *  @file libxutils/src/crypt/crypt.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of the various crypt algorithms
 */

#include "crypt.h"
#include "xbuf.h"
#include "aes.h"
#include "rsa.h"
#include "md5.h"
#include "hmac.h"
#include "crc32.h"
#include "base64.h"
#include "sha256.h"
#include "sha1.h"

#define XCRC32_MAX_SIZE 16
#define XCHAR_MAP_SIZE  52

static const char g_charMap[XCHAR_MAP_SIZE] =
    { 
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 
        'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 
        'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'
    };

uint8_t* XCrypt_XOR(const uint8_t *pInput, size_t nLength, const uint8_t *pKey, size_t nKeyLen)
{
    if (pInput == NULL || !nLength ||
        pKey == NULL || !nKeyLen) return NULL;

    uint8_t *pCrypted = (uint8_t*)malloc(nLength + 1);
    if (pCrypted == NULL) return NULL;

    size_t i;
    for (i = 0; i < nLength; ++i)
        pCrypted[i] = pInput[i] ^ pKey[i % nKeyLen];

    pCrypted[nLength] = '\0';
    return pCrypted;
}

uint8_t* XCrypt_HEX(const uint8_t *pInput, size_t *pLength, const char* pSpace, size_t nColumns, xbool_t bLowCase)
{
    if (pInput == NULL || pLength == NULL || !(*pLength)) return NULL;
    size_t i, nCount = 0, nLength = *pLength;

    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, nLength, 0);
    if (!buffer.nSize) return NULL;

    for (i = 0; i < nLength; i++)
    {
        const char *pFmt = bLowCase ? "%02x%s" : "%02X%s";
        const char *pDlmt = xstrused(pSpace) ? pSpace : XSTR_EMPTY;

        if (XByteBuffer_AddFmt(&buffer, pFmt, pInput[i], pDlmt) <= 0)
        {
            XByteBuffer_Clear(&buffer);
            *pLength = 0;
            return NULL;
        }

        if (!nColumns) continue;
        nCount++;

        if (nCount == nColumns)
        {
            if (XByteBuffer_AddByte(&buffer, '\n') <= 0)
            {
                XByteBuffer_Clear(&buffer);
                *pLength = 0;
                return NULL;
            }

            nCount = 0;
        }
    }

    *pLength = buffer.nUsed;
    return buffer.pData;
}

uint8_t* XDecrypt_HEX(const uint8_t *pInput, size_t *pLength, xbool_t bLowCase)
{
    if (pInput == NULL || pLength == NULL || !(*pLength)) return NULL;
    const char *pFmt = bLowCase ? " %02x%n" : " %02X%n";
    const char *pData = (const char*)pInput;

    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, *pLength, 0);
    if (!buffer.nSize) return NULL;

    uint8_t nVal = 0;
    int nOffset = 0;

#ifdef _WIN32
    while (sscanf_s(pData, pFmt, (unsigned int*)&nVal, &nOffset) == 1)
#else
    while (sscanf(pData, pFmt, (unsigned int*)&nVal, &nOffset) == 1)
#endif
    {
        if (XByteBuffer_AddByte(&buffer, nVal) <= 0)
        {
            XByteBuffer_Clear(&buffer);
            *pLength = 0;
            return NULL;
        }

        pData += nOffset;
    }

    *pLength = buffer.nUsed;
    return buffer.pData;
}

char* XCrypt_Reverse(const char *pInput, size_t nLength)
{
    if (pInput == NULL || !nLength) return NULL;
    char *pReversed = (char*)malloc(nLength + 1);
    if (pReversed == NULL) return NULL;

    size_t i;
    for (i = 0; i < nLength; i++)
        pReversed[i] = pInput[(nLength - (i + 1))];

    pReversed[nLength] = '\0';
    return pReversed;
}

char *XCrypt_Casear(const char *pInput, size_t nLength, size_t nKey)
{
    if (pInput == NULL || !nLength) return NULL;
    char *pRetVal = (char*)malloc(nLength + 1);
    if (pRetVal == NULL) return NULL;

    size_t nChars = sizeof(g_charMap);
    size_t i, x, nHalf = nChars / 2;

    for (i = 0; i < nLength; i++)
    {
        char cByte = pInput[i];

        for (x = 0; x < nChars; x++)
        {
            size_t nStep = x + nKey;
            if (x < nHalf) while (nStep >= nHalf) nStep -= nHalf;
            else { nStep += nHalf; while (nStep >= nChars) nStep -= nHalf; }
            if (pInput[i] == g_charMap[x]) { cByte = g_charMap[nStep]; break; }
        }

        pRetVal[i] = cByte;
    }

    pRetVal[nLength] = '\0';
    return pRetVal;
}

char *XDecrypt_Casear(const char *pInput, size_t nLength, size_t nKey)
{
    if (pInput == NULL || !nLength) return NULL;
    char *pRetVal = (char*)malloc(nLength + 1);
    if (pRetVal == NULL) return NULL;

    size_t nChars = sizeof(g_charMap);
    size_t i, x, nHalf = nChars / 2;

    for (i = 0; i < nLength; i++)
    {
        char cByte = pInput[i];

        for (x = 0; x < nChars; x++)
        {
            size_t nStep = x - nKey;
            while(nStep < XCHAR_MAP_SIZE) nStep += nHalf;

            if (x < nHalf) while (nStep >= nHalf) nStep -= nHalf;
            else { nStep += nHalf; while (nStep >= nChars) nStep -= nHalf; }
            if (pInput[i] == g_charMap[x]) { cByte = g_charMap[nStep]; break; }
        }

        pRetVal[i] = cByte;
    }

    pRetVal[nLength] = '\0';
    return pRetVal;
}

uint8_t* XCrypt_AES(const uint8_t *pInput, size_t *pLength, const uint8_t *pKey, size_t nKeyLen, const uint8_t *pIV)
{
    if (pInput == NULL || pKey == NULL || !nKeyLen ||
        pLength == NULL || !(*pLength)) return NULL;

    xaes_context_t ctx;
    XAES_SetKey(&ctx, pKey, nKeyLen, pIV);
    return XAES_Encrypt(&ctx, pInput, pLength);
}

uint8_t* XDecrypt_AES(const uint8_t *pInput, size_t *pLength, const uint8_t *pKey, size_t nKeyLen, const uint8_t *pIV)
{
    if (pInput == NULL || pKey == NULL || !nKeyLen ||
        pLength == NULL || !(*pLength)) return NULL;

    xaes_context_t ctx;
    XAES_SetKey(&ctx, pKey, nKeyLen, pIV);
    return XAES_Decrypt(&ctx, pInput, pLength);
}

xcrypt_chipher_t XCrypt_GetCipher(const char *pCipher)
{
    if (!strncmp(pCipher, "aes", 3)) return XC_AES;
    else if (!strncmp(pCipher, "hex", 3)) return XC_HEX;
    else if (!strncmp(pCipher, "xor", 3)) return XC_XOR;
    else if (!strncmp(pCipher, "crc32", 5)) return XC_CRC32;
    else if (!strncmp(pCipher, "crc32b", 6)) return XC_CRC32B;
    else if (!strncmp(pCipher, "casear", 6)) return XC_CASEAR;
    else if (!strncmp(pCipher, "b64url", 6)) return XC_B64URL;
    else if (!strncmp(pCipher, "base64", 6)) return XC_BASE64;
    else if (!strncmp(pCipher, "reverse", 7)) return XC_REVERSE;
    else if (!strncmp(pCipher, "sha1sum", 7)) return XC_SHA1_SUM;
    else if (!strncmp(pCipher, "sha256sum", 9)) return XC_SHA256_SUM;
    else if (!strncmp(pCipher, "md5hmac", 7)) return XC_MD5_HMAC;
    else if (!strncmp(pCipher, "md5sum", 6)) return XC_MD5_SUM;
    else if (!strncmp(pCipher, "sha256", 6)) return XC_SHA256;
    else if (!strncmp(pCipher, "hs256", 5)) return XC_HS256;
    else if (!strncmp(pCipher, "sha1", 4)) return XC_SHA1;
    else if (!strncmp(pCipher, "md5", 3)) return XC_MD5;
#ifdef XCRYPT_USE_SSL
    else if (!strncmp(pCipher, "rs256", 5)) return XC_RS256;
    else if (!strncmp(pCipher, "rsapr", 5)) return XC_RSAPR;
    else if (!strncmp(pCipher, "rsa", 3)) return XC_RSA;
#endif
    return XC_INVALID;
}

const char* XCrypt_GetCipherStr(xcrypt_chipher_t eCipher)
{
    switch (eCipher)
    {
        case XC_AES: return "aes";
        case XC_HEX: return "hex";
        case XC_XOR: return "xor";
        case XC_CRC32: return "crc32";
        case XC_CRC32B: return "crc32b";
        case XC_CASEAR: return "casear";
        case XC_BASE64: return "base64";
        case XC_B64URL: return "b64url";
        case XC_MD5: return "md5";
        case XC_SHA1: return "sha1";
        case XC_HS256: return "h256";
        case XC_SHA256: return "sha256";
        case XC_MD5_SUM: return "md5sum";
        case XC_MD5_HMAC: return "md5hmac";
        case XC_SHA1_SUM: return "sha1sum";
        case XC_SHA256_SUM: return "sha256sum";
        case XC_REVERSE: return "reverse";
        case XC_MULTY: return "multy";
#ifdef XCRYPT_USE_SSL
        case XC_RS256: return "rs256";
        case XC_RSAPR: return "rsapr";
        case XC_RSA: return "rsa";
#endif
        default:
            break;
    }

    return "invalid";
}

static xbool_t XCrypt_NeedsKey(xcrypt_chipher_t eCipher)
{
    switch (eCipher)
    {
        case XC_AES:
        case XC_XOR:
        case XC_CASEAR:
        case XC_HS256:
        case XC_MD5_HMAC:
#ifdef XCRYPT_USE_SSL
        case XC_RS256:
        case XC_RSAPR:
        case XC_RSA:
#endif
            return XTRUE;
        case XC_HEX:
        case XC_CRC32:
        case XC_BASE64:
        case XC_B64URL:
        case XC_MD5:
        case XC_SHA1:
        case XC_SHA256:
        case XC_REVERSE:
        case XC_MD5_SUM:
        case XC_SHA1_SUM:
        case XC_SHA256_SUM:
            return XFALSE;
        default:
            break;
    }

    return XFALSE;
}

void XCrypt_ErrorCallback(xcrypt_ctx_t *pCtx, const char *pStr, ...)
{
    if (pCtx->callback == NULL) return;
    char sBuff[XSTR_MIN];

    va_list args;
    va_start(args, pStr);
    xstrncpyarg(sBuff, sizeof(sBuff), pStr, args);
    va_end(args);

    pCtx->callback(XCB_ERROR, sBuff, pCtx->pUserPtr);
}

xbool_t XCrypt_KeyCallback(xcrypt_ctx_t *pCtx, xcrypt_chipher_t eCipher, xcrypt_key_t *pKey)
{
    memset(pKey, 0, sizeof(xcrypt_key_t));
    pKey->eCipher = eCipher;

    if (!XCrypt_NeedsKey(eCipher) ||
        pCtx->callback == NULL) return XTRUE;

    return pCtx->callback(XCB_KEY, pKey, pCtx->pUserPtr);
}

void XCrypt_Init(xcrypt_ctx_t *pCtx, xbool_t bDecrypt, char *pCiphers, xcrypt_cb_t callback, void *pUser)
{
    pCtx->callback = callback;
    pCtx->pUserPtr = pUser;
    pCtx->bDecrypt = bDecrypt;
    pCtx->pCiphers = pCiphers;
    pCtx->nColumns = 0;
}

uint8_t* XCrypt_Single(xcrypt_ctx_t *pCtx, xcrypt_chipher_t eCipher, const uint8_t *pInput, size_t *pLength)
{
    xcrypt_key_t encKey;
    if (!XCrypt_KeyCallback(pCtx, eCipher, &encKey)) return XFALSE;

    const uint8_t *pKey = (const uint8_t*)encKey.sKey;
    size_t nKeyLength = encKey.nLength;

    uint8_t *pCrypted = NULL;
    uint32_t nCRC32 = 0;

    switch (eCipher)
    {
        case XC_CRC32: nCRC32 = XCRC32_Compute(pInput, *pLength); break;
        case XC_CRC32B: nCRC32 = XCRC32_ComputeB(pInput, *pLength); break;
        case XC_AES: pCrypted = XCrypt_AES(pInput, pLength, pKey, nKeyLength, NULL); break;
        case XC_HEX: pCrypted = XCrypt_HEX(pInput, pLength, XSTR_SPACE, pCtx->nColumns, XFALSE); break;
        case XC_XOR: pCrypted = XCrypt_XOR(pInput, *pLength, pKey, nKeyLength); break;
        case XC_MD5: pCrypted = XMD5_Encrypt(pInput, *pLength); *pLength = XMD5_DIGEST_SIZE; break;
        case XC_SHA1: pCrypted = XSHA1_Encrypt(pInput, *pLength); *pLength = XSHA1_DIGEST_SIZE; break;
        case XC_SHA256: pCrypted = XSHA256_Encrypt(pInput, *pLength); *pLength = XSHA256_DIGEST_SIZE; break;
        case XC_MD5_SUM: pCrypted = (uint8_t*)XMD5_Sum(pInput, *pLength); *pLength = XMD5_LENGTH; break;
        case XC_MD5_HMAC: pCrypted = (uint8_t*)XHMAC_MD5_NEW(pInput, *pLength, pKey, nKeyLength); *pLength = XMD5_LENGTH; break;
        case XC_SHA1_SUM: pCrypted = (uint8_t*)XSHA1_Sum(pInput, *pLength); *pLength = XSHA1_LENGTH; break;
        case XC_SHA256_SUM: pCrypted = (uint8_t*)XSHA256_Sum(pInput, *pLength); *pLength = XSHA256_LENGTH; break;
        case XC_HS256: pCrypted = (uint8_t*)XHMAC_SHA256_NEW(pInput, *pLength, pKey, nKeyLength); *pLength = XSHA256_LENGTH; break;
        case XC_CASEAR: pCrypted = (uint8_t*)XCrypt_Casear((const char*)pInput, *pLength, atoi(encKey.sKey)); break;
        case XC_BASE64: pCrypted = (uint8_t*)XBase64_Encrypt(pInput, pLength); break;
        case XC_B64URL: pCrypted = (uint8_t*)XBase64_UrlEncrypt(pInput, pLength); break;
        case XC_REVERSE: pCrypted = (uint8_t*)XCrypt_Reverse((const char*)pInput, *pLength); break;
#ifdef XCRYPT_USE_SSL
        case XC_RS256: pCrypted = XCrypt_RS256(pInput, *pLength, (const char*)pKey, nKeyLength, pLength); break;
        case XC_RSAPR: pCrypted = XCrypt_PrivRSA(pInput, *pLength, (const char*)pKey, nKeyLength, pLength); break;
        case XC_RSA: pCrypted = XCrypt_RSA(pInput, *pLength, (const char*)pKey, nKeyLength, pLength); break;
#endif
        default: break;
    }

    if (nCRC32 > 0)
    {
        pCrypted = (uint8_t*)malloc(XCRC32_MAX_SIZE);
        if (pCrypted == NULL)
        {
            XCrypt_ErrorCallback(pCtx, "Can not allocate memory for CRC32 buffer");
            return NULL;
        }

        *pLength = xstrncpyf((char*)pCrypted, XCRC32_MAX_SIZE, "%u", nCRC32);
    }

    if (pCrypted == NULL)
    {
        XCrypt_ErrorCallback(pCtx,
            "Failed to encrypt data with cipher: %s",
                XCrypt_GetCipherStr(eCipher));

        return NULL;
    }

    return pCrypted;
}

uint8_t* XDecrypt_Single(xcrypt_ctx_t *pCtx, xcrypt_chipher_t eCipher, const uint8_t *pInput, size_t *pLength)
{
    xcrypt_key_t decKey;
    if (!XCrypt_KeyCallback(pCtx, eCipher, &decKey)) return XFALSE;

    const uint8_t *pKey = (const uint8_t*)decKey.sKey;
    size_t nKeyLength = decKey.nLength;
    uint8_t *pDecrypted = NULL;

    switch (eCipher)
    {
        case XC_HEX: pDecrypted = XDecrypt_HEX(pInput, pLength, XFALSE); break;
        case XC_AES: pDecrypted = XDecrypt_AES(pInput, pLength, pKey, nKeyLength, NULL); break;
        case XC_XOR: pDecrypted = XCrypt_XOR(pInput, *pLength, pKey, nKeyLength); break;
        case XC_CASEAR: pDecrypted = (uint8_t*)XDecrypt_Casear((const char*)pInput, *pLength, atoi(decKey.sKey)); break;
        case XC_BASE64: pDecrypted = (uint8_t*)XBase64_Decrypt(pInput, pLength); break;
        case XC_B64URL: pDecrypted = (uint8_t*)XBase64_UrlDecrypt(pInput, pLength); break;
        case XC_REVERSE: pDecrypted = (uint8_t*)XCrypt_Reverse((const char*)pInput, *pLength); break;
#ifdef XCRYPT_USE_SSL
        case XC_RSAPR: pDecrypted = XDecrypt_PubRSA(pInput, *pLength, (const char*)pKey, nKeyLength, pLength); break;
        case XC_RSA: pDecrypted = XDecrypt_RSA(pInput, *pLength, (const char*)pKey, nKeyLength, pLength); break;
#endif
        default: break;
    }

    if (pDecrypted == NULL)
    {
        XCrypt_ErrorCallback(pCtx,
            "Failed to decrypt data with cipher: %s",
                XCrypt_GetCipherStr(eCipher));

        return NULL;
    }

    return pDecrypted;
}

uint8_t* XCrypt_Multy(xcrypt_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength)
{
    uint8_t* (*XCrypt_Func)(xcrypt_ctx_t*, xcrypt_chipher_t, const uint8_t*, size_t*);
    XCrypt_Func = pCtx->bDecrypt ? XDecrypt_Single : XCrypt_Single;

    xarray_t *pCiphersArr = xstrsplit(pCtx->pCiphers, ":");
    if (pCiphersArr == NULL)
    {
        pCiphersArr = XArray_New(XSTDNON, XFALSE);
        if (pCiphersArr == NULL)
        {
            XCrypt_ErrorCallback(pCtx, "Can not allocate memory for cipher array");
            return NULL;
        }

        XArray_AddData(pCiphersArr, pCtx->pCiphers, strlen(pCtx->pCiphers));
        if (!pCiphersArr->nUsed)
        {
            XCrypt_ErrorCallback(pCtx, "Can append cipher to the array");
            XArray_Destroy(pCiphersArr);
            return NULL;
        }
    }

    uint8_t *pData = XByteData_Dup(pInput, *pLength);
    if (pData == NULL)
    {
        XCrypt_ErrorCallback(pCtx, "Can allocate memory for input buffer");
        XArray_Destroy(pCiphersArr);
        return NULL;
    }

    size_t i, nUsed = XArray_Used(pCiphersArr);
    uint8_t *pRet = NULL;

    for (i = 0; i < nUsed && pData != NULL; i++)
    {
        const char *pCipher = XArray_GetData(pCiphersArr, i);
        if (pCipher == NULL) continue;

        xcrypt_chipher_t eCipher = XCrypt_GetCipher(pCipher);
        if (eCipher == XC_INVALID)
        {
            XCrypt_ErrorCallback(pCtx, 
                "Invalid or unsupported cipher: %s", 
                XCrypt_GetCipherStr(eCipher));

            XArray_Destroy(pCiphersArr);
            free(pData);
            return NULL;
        }

        pRet = XCrypt_Func(pCtx, eCipher, pData, pLength);
        free(pData);
        pData = pRet;
    }

    XArray_Destroy(pCiphersArr);
    return pData;
}
