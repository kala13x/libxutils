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

    char *pSignature = (char*)malloc(XSHA256_LENGTH * 2 + 1);
    if (pSignature == NULL) { free(pJoined); return NULL; }

    size_t nRawSigLength = xstrncpyf(pJoined, nRawSigSize, "%s.%s", pHeader, pData);
    char *pCripted = XCrypt_HS256((uint8_t*)pJoined, nRawSigLength, pKey, nKeyLen);

    free(pJoined);
    return pCripted;
}

char* XJWT_Create(const char *pPayload, size_t nLength, const uint8_t *pKey, size_t nKeyLen, size_t *pJWTLen)
{
    if (pPayload == NULL || !nLength) return NULL;
    size_t nHeaderLen = 0;

    char *pHeader = XJWT_CreateHeader("HS256", "JWT", &nHeaderLen);
    if (pHeader == NULL) return NULL;

    char *pSignature = XJWT_CreateSignature(pHeader, nHeaderLen, pPayload, nLength, pKey, nKeyLen);
    if (pSignature == NULL) { free(pHeader); return NULL; }

    size_t nJWTSize = nHeaderLen + nLength + strlen(pSignature) + 2;
    char *pJWT = (char*)malloc(nJWTSize);

    if (pJWT == NULL)
    {
        free(pHeader);
        return NULL;
    }

    *pJWTLen = xstrncpyf(pJWT, nJWTSize, "%s.%s.%s",
        pHeader, pPayload, pSignature);

    free(pSignature);
    free(pHeader);
    return pJWT;
}