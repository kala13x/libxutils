/*!
 *  @file libxutils/media/jwt.c
 *
 *  This source is part of "libxutils" project
 *  2019-2023  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of JSON Web Tokens
 */

#include "xstd.h"
#include "array.h"
#include "crypt.h"
#include "xjson.h"
#include "xstr.h"
#include "jwt.h"

#define XJWT_ALGORITHM  "HS256"
#define XJWT_TYPE       "JWT"

#ifdef XSHA256_DIGEST_SIZE
#define XJWT_HASH_LENGTH XSHA256_DIGEST_SIZE
#else
#define XJWT_HASH_LENGTH 32
#endif

xjson_obj_t* XJWT_CreateHeaderObj(const char *pAlgo)
{
    XASSERT_RET(pAlgo, NULL);
    xjson_obj_t *pJson = XJSON_NewObject(NULL, 0);
    XJSON_AddObject(pJson, XJSON_NewString("alg", pAlgo));
    XJSON_AddObject(pJson, XJSON_NewString("typ", XJWT_TYPE));
    return pJson;
}

char* XJWT_CreateHeader(const char *pAlgo, size_t *pHdrLen)
{
    xjson_obj_t *pJson = XJWT_CreateHeaderObj(pAlgo);
    char *pHeader = XJSON_DumpObj(pJson, 0, pHdrLen);
    XJSON_FreeObject(pJson);
    return pHeader;
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
    size_t nJointSize = nHeaderLength + nPayloadLength + 2;
    char *pJointData = (char*)malloc(nJointSize);
    if (pJointData == NULL) return NULL;

    uint8_t hash[XJWT_HASH_LENGTH];
    *pSignatureLength = sizeof(hash);

    size_t nJointLength = xstrncpyf(pJointData, nJointSize, "%s.%s", pHeader, pPayload);
    XCrypt_HS256U(hash, sizeof(hash), (const uint8_t*)pJointData, nJointLength, pSecret, nSecretLength);

    free(pJointData);
    return XCrypt_Base64Url(hash, pSignatureLength);
}

char* XJWT_CreateAdv(
        const char *pHeader,
        size_t nHeaderLen,
        const char *pPayload,
        size_t nPayloadLen,
        const uint8_t *pSecret,
        size_t nSecretLen,
        size_t *pJWTLen)
{
    if (pJWTLen != NULL) *pJWTLen = 0;
    size_t nSigLen = 0;

    if (pHeader == NULL || pPayload == NULL || pSecret == NULL ||
        !nHeaderLen || !nPayloadLen || !nSecretLen) return NULL;

    char *pEncHeader = XCrypt_Base64Url((const uint8_t*)pHeader, &nHeaderLen);
    if (pEncHeader == NULL) return NULL;

    char *pEncPayload = XCrypt_Base64Url((const uint8_t*)pPayload, &nPayloadLen);
    if (pEncPayload == NULL) { free(pEncHeader); return NULL; }

    char *pSignature = XJWT_CreateSignature(
        pEncHeader,
        nHeaderLen,
        pEncPayload,
        nPayloadLen,
        pSecret,
        nSecretLen,
        &nSigLen
    );

    if (pSignature == NULL)
    {
        free(pEncHeader);
        free(pEncPayload);
        return NULL;
    }

    size_t nJWTLength = nHeaderLen + nPayloadLen + nSigLen + 2;
    char *pJWT = (char*)malloc(nJWTLength + 1);

    if (pJWT != NULL)
    {
        xstrncpyf(pJWT, nJWTLength + 1, "%s.%s.%s",
            pEncHeader, pEncPayload, pSignature);
    }

    free(pEncHeader);
    free(pEncPayload);
    free(pSignature);

    if (pJWTLen != NULL)
        *pJWTLen = nJWTLength;

    return pJWT;
}

char* XJWT_Create(const char *pPayload, size_t nPayloadLen, const uint8_t *pSecret, size_t nSecretLen, size_t *pJWTLen)
{
    if (pJWTLen != NULL) *pJWTLen = 0;
    size_t nHeaderLen = 0;

    if (pPayload == NULL || pSecret == NULL ||
        !nPayloadLen || !nSecretLen) return NULL;

    char *pHeader = XJWT_CreateHeader(XJWT_ALGORITHM, &nHeaderLen);
    if (pHeader == NULL) return NULL;

    char *pJWT = XJWT_CreateAdv(
        pHeader,
        nHeaderLen,
        pPayload,
        nPayloadLen,
        pSecret,
        nSecretLen,
        pJWTLen
    );

    free(pHeader);
    return pJWT;
}

char* XJWT_Dump(xjwt_t *pJWT, const uint8_t *pSecret, size_t nSecretLen, size_t *pJWTLen)
{
    if (pJWTLen != NULL) *pJWTLen = 0;
    if (pJWT == NULL) return NULL;

    size_t nHeaderLen = 0;
    size_t nPayloadLen = 0;

    char *pHeader = XJSON_DumpObj(pJWT->pHeaderObj, 0, &nHeaderLen);
    if (pHeader == NULL) return NULL;

    char *pPayload = XJSON_DumpObj(pJWT->pPayloadObj, 0, &nPayloadLen);
    if (pPayload == NULL) { free(pHeader); return NULL; }

    char *pJWTStr = XJWT_CreateAdv(
        pHeader,
        nHeaderLen,
        pPayload,
        nPayloadLen,
        pSecret,
        nSecretLen,
        pJWTLen
    );

    free(pHeader);
    free(pPayload);

    return pJWTStr;
}

XSTATUS XJWT_Parse(xjwt_t *pJWT, const char *pJWTStr, size_t nLength, const uint8_t *pSecret, size_t nSecretLen)
{
    if (pJWT == NULL) return XSTDINV;
    pJWT->pHeaderObj = NULL;
    pJWT->pPayloadObj = NULL;

    if (pJWTStr == NULL || pSecret == NULL ||
        !nSecretLen || !nLength) return XSTDINV;

    xarray_t *pArray = xstrsplit(pJWTStr, ".");
    if (pArray == NULL) return XSTDERR;

    const char *pHeader = (const char *)XArray_GetData(pArray, 0);
    const char *pPayload = (const char *)XArray_GetData(pArray, 1);
    const char *pSignature = (const char *)XArray_GetData(pArray, 2);

    if (pHeader == NULL || pPayload == NULL || pSignature == NULL)
    {
        XArray_Destroy(pArray);
        return XSTDERR;
    }

    size_t nHeaderLen = strlen(pHeader);
    size_t nPayloadLen = strlen(pPayload);
    size_t nSignatureLen = 0;

    char *pTmpSignature = XJWT_CreateSignature(
        pHeader,
        nHeaderLen,
        pPayload,
        nPayloadLen,
        pSecret,
        nSecretLen,
        &nSignatureLen
    );

    if (pTmpSignature == NULL)
    {
        XArray_Destroy(pArray);
        return XSTDERR;
    }

    xbool_t bValid = !strncmp(pSignature, pTmpSignature, nSignatureLen);
    free(pTmpSignature);

    if (!bValid)
    {
        XArray_Destroy(pArray);
        return XSTDNON;
    }

    char *pHeaderRaw = XDecrypt_Base64Url((uint8_t*)pHeader, &nHeaderLen);
    if (pHeaderRaw == NULL)
    {
        XArray_Destroy(pArray);
        return XSTDNON;
    }

    char *pPayloadRaw = XDecrypt_Base64Url((uint8_t*)pPayload, &nPayloadLen);
    if (pPayloadRaw == NULL)
    {
        XArray_Destroy(pArray);
        free(pHeaderRaw);
        return XSTDNON;
    }

    XArray_Destroy(pArray);
    xjson_t jsonParser;

    if (!XJSON_Parse(&jsonParser, pHeaderRaw, nHeaderLen))
    {
        free(pPayloadRaw);
        free(pHeaderRaw);
        return XSTDERR;
    }

    pJWT->pHeaderObj = jsonParser.pRootObj;
    XJSON_Init(&jsonParser);
    free(pHeaderRaw);

    if (!XJSON_Parse(&jsonParser, pPayloadRaw, nPayloadLen))
    {
        XJSON_FreeObject(pJWT->pHeaderObj);
        pJWT->pHeaderObj = NULL;

        free(pPayloadRaw);
        return XSTDERR;
    }

    pJWT->pPayloadObj = jsonParser.pRootObj;
    free(pPayloadRaw);
    return XSTDOK;
}

void XJWT_Destroy(xjwt_t *pJWT)
{
    if (pJWT == NULL) return;
    XJSON_FreeObject(pJWT->pHeaderObj);
    XJSON_FreeObject(pJWT->pPayloadObj);
}