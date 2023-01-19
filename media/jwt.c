/*!
 *  @file libxutils/media/jwt.c
 *
 *  This source is part of "libxutils" project
 *  2019-2023  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of JSON Web Tokens
 */

#include "xstd.h"
#include "crypt.h"
#include "xjson.h"
#include "xstr.h"
#include "jwt.h"

#define XJWT_ALGORITHM  "HS256"
#define XJWT_TYPE       "JWT"

static char* XJWT_CreateHeader(const char *pAlgo, const char *pType, size_t *pHdrLen)
{
    XASSERT_RET((pType && pAlgo), NULL);

    xjson_obj_t *pJson = XJSON_NewObject(NULL, 0);
    XJSON_AddObject(pJson, XJSON_NewString("alg", pAlgo));
    XJSON_AddObject(pJson, XJSON_NewString("typ", pType));
    char *pHeader = XJSON_DumpObj(pJson, 0, pHdrLen);

    XJSON_FreeObject(pJson);
    XASSERT_RET(pHeader, NULL);

    char *pEncHeader = XCrypt_Base64Url((const uint8_t*)pHeader, pHdrLen);
    free(pHeader);

    return pEncHeader;
}

static char* XJWT_CreateSignature(
                const char *pHeader,
                size_t nHeaderLength,
                const char *pPayload,
                size_t nPayloadLength,
                const uint8_t *pSecret,
                size_t nSecretLength,
                size_t *pSignatureLength)
{
    size_t nJointSize = nHeaderLength + nPayloadLength + 1;
    char *pJointData = (char*)malloc(nJointSize);

    XASSERT_RET(pJointData, NULL);
    char sHash[XSHA256_LENGTH + 1];

    size_t nJointLength = xstrncpyf(pJointData, nJointSize, "%s.%s", pHeader, pPayload);
    XCrypt_HS256S(sHash, sizeof(sHash), (const uint8_t*)pJointData, nJointLength, pSecret, nSecretLength);
    free(pJointData);

    return XCrypt_Base64Url((const uint8_t*)sHash, pSignatureLength);
}

char* XJWT_Create(const char *pPayload, size_t nPayloadLen, const uint8_t *pSecret, size_t nSecretLen, size_t *pJWTLen)
{
    if (pPayload == NULL || !nPayloadLen) return NULL;
    size_t nHeaderLen = 0, nSigLen = XSHA256_LENGTH;

    char *pEncHeader = XJWT_CreateHeader(XJWT_ALGORITHM, XJWT_TYPE, &nHeaderLen);
    if (pEncHeader == NULL) return NULL;

    char *pEncPayload = XCrypt_Base64Url((const uint8_t*)pPayload, &nPayloadLen);
    if (pEncPayload == NULL) { free(pEncHeader); return NULL; }

    char *pSignature = XJWT_CreateSignature(pEncHeader, nHeaderLen, pEncPayload, nPayloadLen, pSecret, nSecretLen, &nSigLen);
    if (pSignature == NULL) { free(pEncHeader); free(pEncPayload); return NULL; }

    *pJWTLen = nHeaderLen + nPayloadLen + nSigLen + 2;
    char *pJWT = (char*)malloc(*pJWTLen + 1);

    if (pJWT != NULL)
    {
        xstrncpyf(pJWT, *pJWTLen + 1, "%s.%s.%s",
            pEncHeader, pEncPayload, pSignature);
    }

    free(pEncHeader);
    free(pEncPayload);
    free(pSignature);

    return pJWT;
}