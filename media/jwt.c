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
    if (pType == NULL || pAlgo == NULL) return NULL;

    xjson_obj_t *pJson = XJSON_NewObject(NULL, 0);
    XJSON_AddObject(pJson, XJSON_NewString("alg", pAlgo));
    XJSON_AddObject(pJson, XJSON_NewString("typ", pType));
    char *pHeader = XJSON_DumpObj(pJson, 0, pHdrLen);

    XJSON_FreeObject(pJson);
    return pHeader;
}

static char* XJWT_CreateSignature(const char *pHeader, size_t nHeaderLen, const char *pData, size_t nDataLen, const uint8_t *pKey, size_t nKeyLen)
{
    size_t nRawSigSize = nHeaderLen + nDataLen + 1;
    char *pJoined = (char*)malloc(nRawSigSize);
    if (pJoined == NULL) return NULL;

    size_t nRawSigLength = xstrncpyf(pJoined, nRawSigSize, "%s.%s", pHeader, pData);
    char *pCrypted = XCrypt_HS256((const uint8_t*)pJoined, nRawSigLength, pKey, nKeyLen);

    free(pJoined);
    return pCrypted;
}

static char* XJWT_CreateFinal(const char *pHeader, size_t nHdrLen, const char *pPayload, size_t nPayLen, const char *pSignature, size_t nSigLen, size_t *pJWTLength)
{
    char *pBaseHeader = XCrypt_Base64((const uint8_t*)pHeader, &nHdrLen);
    if (pBaseHeader == NULL) return NULL;

    char *pBasePayload = XCrypt_Base64((const uint8_t*)pPayload, &nPayLen);
    if (pBasePayload == NULL) { free(pBaseHeader); return NULL; }

    char *pBaseSignature = XCrypt_Base64((const uint8_t*)pSignature, &nSigLen);
    if (pBaseSignature == NULL) { free(pBaseHeader); free(pBasePayload); return NULL; }

    *pJWTLength = nHdrLen + nPayLen + nSigLen + 2;
    char *pJWT = (char*)malloc(*pJWTLength + 1);

    if (pJWT == NULL)
    {
        free(pBaseHeader);
        free(pBasePayload);
        free(pBaseSignature);
        return NULL;
    }

    xstrncpyf(pJWT, *pJWTLength + 1, "%s.%s.%s",
        pBaseHeader, pBasePayload, pBaseSignature);

    free(pBaseHeader);
    free(pBasePayload);
    free(pBaseSignature);

    return pJWT;
}

char* XJWT_Create(const char *pPayload, size_t nLength, const uint8_t *pKey, size_t nKeyLen, size_t *pJWTLen)
{
    if (pPayload == NULL || !nLength) return NULL;
    size_t nHeaderLen = 0;

    char *pHeader = XJWT_CreateHeader(XJWT_ALGORITHM, XJWT_TYPE, &nHeaderLen);
    if (pHeader == NULL) return NULL;

    char *pSignature = XJWT_CreateSignature(pHeader, nHeaderLen, pPayload, nLength, pKey, nKeyLen);
    if (pSignature == NULL) { free(pHeader); return NULL; }

    char *pJWT = XJWT_CreateFinal(pHeader, nHeaderLen, pPayload, nLength, pSignature, XSHA256_LENGTH, pJWTLen);
    free(pSignature); free(pHeader);

    return pJWT;
}