/*!
 *  @file libxutils/src/data/jwt.c
 *
 *  This source is part of "libxutils" project
 *  2019-2023  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of JSON Web Tokens
 */

#include "jwt.h"
#include "str.h"
#include "array.h"
#include "hmac.h"
#include "base64.h"

#ifdef XSHA256_DIGEST_SIZE
#define XJWT_HASH_LENGTH XSHA256_DIGEST_SIZE
#else
#define XJWT_HASH_LENGTH 32
#endif

const char* XJWT_GetAlgStr(xjwt_alg_t eAlg)
{
    switch (eAlg)
    {
#ifdef XCRYPT_USE_SSL
        case XJWT_ALG_RS256: return "RS256";
#endif
        case XJWT_ALG_HS256: return "HS256";
        case XJWT_ALG_INVALID:
        default: break;
    }

    return NULL;
}

xjwt_alg_t XJWT_GetAlg(const char *pAlgStr)
{
    XCHECK(pAlgStr, XJWT_ALG_INVALID);
#ifdef XCRYPT_USE_SSL
    if (!strncmp(pAlgStr, "RS256", 5)) return XJWT_ALG_RS256;
#endif
    if (!strncmp(pAlgStr, "HS256", 5)) return XJWT_ALG_HS256;
    return XJWT_ALG_INVALID;
}

xjson_obj_t* XJWT_CreateHeaderObj(xjwt_alg_t eAlg)
{
    const char *pAlgo = XJWT_GetAlgStr(eAlg);
    XCHECK(pAlgo, NULL);

    xjson_obj_t *pJson = XJSON_NewObject(NULL, NULL, XFALSE);
    XJSON_AddObject(pJson, XJSON_NewString(NULL, "alg", pAlgo));
    XJSON_AddObject(pJson, XJSON_NewString(NULL, "typ", "JWT"));
    return pJson;
}

void XJWT_Destroy(xjwt_t *pJWT)
{
    XCHECK_VOID(pJWT);
    pJWT->bVerified = XFALSE;
    pJWT->eAlgorithm = XJWT_ALG_INVALID;

    XJSON_FreeObject(pJWT->pHeaderObj);
    pJWT->pHeaderObj = NULL;

    free(pJWT->pHeader);
    pJWT->pHeader = NULL;
    pJWT->nHeaderLen = 0;

    XJSON_FreeObject(pJWT->pPayloadObj);
    pJWT->pPayloadObj = NULL;

    free(pJWT->pPayload);
    pJWT->pPayload = NULL;
    pJWT->nPayloadLen = 0;

    free(pJWT->pSignature);
    pJWT->pSignature = NULL;
    pJWT->nSignatureLen = 0;
}

void XJWT_Init(xjwt_t *pJWT, xjwt_alg_t eAlg)
{
    XCHECK_VOID(pJWT);
    pJWT->eAlgorithm = eAlg;
    pJWT->bVerified = XFALSE;

    pJWT->pHeader = NULL;
    pJWT->pPayload = NULL;
    pJWT->pSignature = NULL;
    pJWT->pHeaderObj = NULL;
    pJWT->pPayloadObj = NULL;

    pJWT->nHeaderLen = 0;
    pJWT->nPayloadLen = 0;
    pJWT->nSignatureLen = 0;
}

XSTATUS XJWT_AddPayload(xjwt_t *pJWT, const char *pPayload, size_t nPayloadLen, xbool_t bIsEncoded)
{
    XCHECK((pJWT && pPayload && nPayloadLen), XSTDINV);
    free(pJWT->pPayload);
    pJWT->nPayloadLen = 0;

    if (bIsEncoded)
    {
        pJWT->pPayload = (char*)malloc(nPayloadLen + 1);
        XCHECK(pPayload, XSTDERR);

        memcpy(pJWT->pPayload, pPayload, nPayloadLen);
        pJWT->pPayload[nPayloadLen] = '\0';
        pJWT->nPayloadLen = nPayloadLen;

        return XSTDOK;
    }

    pJWT->pPayload = XBase64_UrlEncrypt((uint8_t*)pPayload, &nPayloadLen);
    pJWT->nPayloadLen = nPayloadLen;
    XCHECK(pJWT->pPayload, XSTDERR);

    return XSTDOK;
}

char* XJWT_GetPayload(xjwt_t *pJWT, xbool_t bDecode, size_t *pPayloadLen)
{
    XCHECK(pJWT, NULL);
    if (pPayloadLen) *pPayloadLen = 0;

    if (pJWT->pPayload == NULL)
    {
        pJWT->pPayload = XJSON_DumpObj(pJWT->pPayloadObj, 0, &pJWT->nPayloadLen);
        XCHECK((pJWT->pPayload && pJWT->nPayloadLen), NULL);
    }

    if (bDecode)
    {
        size_t nPayloadLen = pJWT->nPayloadLen;
        char *pPayloadRaw = XBase64_UrlDecrypt((uint8_t*)pJWT->pPayload, &nPayloadLen);

        if (pPayloadLen) *pPayloadLen = nPayloadLen;
        return pPayloadRaw;
    }

    if (pPayloadLen) *pPayloadLen = pJWT->nPayloadLen;
    return pJWT->pPayload;
}

xjson_obj_t* XJWT_GetPayloadObj(xjwt_t *pJWT)
{
    XCHECK(pJWT, NULL);
    if (pJWT->pPayloadObj != NULL) return pJWT->pPayloadObj;

    size_t nPayloadLen = 0;
    char *pPayloadRaw = XJWT_GetPayload(pJWT, XTRUE, &nPayloadLen);
    XCHECK(pPayloadRaw, NULL);

    xjson_t json;
    int nStatus = XJSON_Parse(&json, NULL, pPayloadRaw, nPayloadLen);
    free(pPayloadRaw);

    XCHECK((nStatus == XJSON_SUCCESS), NULL);
    pJWT->pPayloadObj = json.pRootObj;

    return pJWT->pPayloadObj;
}

XSTATUS XJWT_AddHeader(xjwt_t *pJWT, const char *pHeader, size_t nHeaderLen, xbool_t bIsEncoded)
{
    XCHECK((pJWT && pHeader && nHeaderLen), XSTDINV);
    free(pJWT->pHeader);
    pJWT->nHeaderLen = 0;

    if (bIsEncoded)
    {
        pJWT->pHeader = (char*)malloc(nHeaderLen + 1);
        XCHECK(pJWT->pHeader, XSTDERR);

        memcpy(pJWT->pHeader, pHeader, nHeaderLen);
        pJWT->pHeader[nHeaderLen] = '\0';
        pJWT->nHeaderLen = nHeaderLen;

        return XSTDOK;
    }

    pJWT->pHeader = XBase64_UrlEncrypt((const uint8_t*)pHeader, &nHeaderLen);
    pJWT->nHeaderLen = nHeaderLen;
    XCHECK(pJWT->pHeader, XSTDERR);

    return XSTDOK;
}

char* XJWT_GetHeader(xjwt_t *pJWT, xbool_t bDecode, size_t *pHeaderLen)
{
    XCHECK(pJWT, NULL);
    if (pHeaderLen) *pHeaderLen = 0;

    if (pJWT->pHeader == NULL)
    {
        if (pJWT->pHeaderObj != NULL)
        {
            pJWT->pHeader = XJSON_DumpObj(pJWT->pHeaderObj, 0, &pJWT->nHeaderLen);
            XCHECK((pJWT->pHeader && pJWT->nHeaderLen), NULL);
        }
        else
        {
            size_t nRawHeaderLen = 0;
            pJWT->pHeaderObj = XJWT_CreateHeaderObj(pJWT->eAlgorithm);
            char *pHeaderRaw = XJSON_DumpObj(pJWT->pHeaderObj, XFALSE, &nRawHeaderLen);
            XCHECK((pHeaderRaw && nRawHeaderLen), NULL);

            pJWT->nHeaderLen = nRawHeaderLen;
            pJWT->pHeader = XBase64_UrlEncrypt((const uint8_t*)pHeaderRaw, &pJWT->nHeaderLen);

            if (bDecode)
            {
                if (pHeaderLen) *pHeaderLen = nRawHeaderLen;
                return pHeaderRaw;
            }

            free(pHeaderRaw);
            if (pHeaderLen) *pHeaderLen = pJWT->nHeaderLen;
            return pJWT->pHeader;
        }
    }

    if (bDecode)
    {
        size_t nHeaderLen = pJWT->nHeaderLen;
        char *nHeaderRaw = XBase64_UrlDecrypt((uint8_t*)pJWT->pHeader, &nHeaderLen);

        if (pHeaderLen) *pHeaderLen = nHeaderLen;
        return nHeaderRaw;
    }

    if (pHeaderLen) *pHeaderLen = pJWT->nHeaderLen;
    return pJWT->pHeader;
}

xjson_obj_t* XJWT_GetHeaderObj(xjwt_t *pJWT)
{
    XCHECK(pJWT, NULL);
    if (pJWT->pHeaderObj != NULL) return pJWT->pHeaderObj;

    size_t nHeaderLen = 0;
    char *nHeaderRaw = XJWT_GetHeader(pJWT, XTRUE, &nHeaderLen);
    XCHECK(nHeaderRaw, NULL);

    xjson_t json;
    int nStatus = XJSON_Parse(&json, NULL, nHeaderRaw, nHeaderLen);
    free(nHeaderRaw);

    XCHECK((nStatus == XJSON_SUCCESS), NULL);
    pJWT->pHeaderObj = json.pRootObj;

    return pJWT->pHeaderObj;
}

xjwt_alg_t XJWT_GetAlgorithm(xjwt_t *pJWT)
{
    XCHECK(pJWT, XJWT_ALG_INVALID);
    if (pJWT->eAlgorithm != XJWT_ALG_INVALID) return pJWT->eAlgorithm;

    xjson_obj_t *pHeaderObj = XJWT_GetHeaderObj(pJWT);
    XCHECK(pHeaderObj, XJWT_ALG_INVALID);

    xjson_obj_t *pAlgObj  = XJSON_GetObject(pHeaderObj, "alg");
    XCHECK(pAlgObj, XJWT_ALG_INVALID);

    const char *pAlgStr = XJSON_GetString(pAlgObj);
    XCHECK(pAlgStr, XJWT_ALG_INVALID);

    pJWT->eAlgorithm = XJWT_GetAlg(pAlgStr);
    return pJWT->eAlgorithm;
}

char *XJWT_CreateJoint(xjwt_t *pJWT, size_t *pOutLen)
{
    if (pOutLen) *pOutLen = 0;
    XCHECK(pJWT, NULL);

    XCHECK(XJWT_GetHeader(pJWT, XFALSE, NULL), NULL);
    XCHECK(XJWT_GetPayload(pJWT, XFALSE, NULL), NULL);

    size_t nJointSize = pJWT->nHeaderLen + pJWT->nPayloadLen + 2;
    char *pJointData = (char*)malloc(nJointSize);
    XCHECK(pJointData, NULL);

    size_t nJointLen = xstrncpyf(pJointData, nJointSize,
        "%s.%s", pJWT->pHeader, pJWT->pPayload);

    if (pOutLen) *pOutLen = nJointLen;
    return pJointData;
}

XSTATUS XJWT_CreateSignature(xjwt_t *pJWT, const uint8_t *pSecret, size_t nSecretLen)
{
    XCHECK(pJWT, XSTDINV);
    free(pJWT->pSignature);

    pJWT->pSignature = NULL;
    pJWT->nSignatureLen = 0;
    size_t nJointLength = 0;

    char *pJointData = XJWT_CreateJoint(pJWT, &nJointLength);
    XCHECK((pJointData && nJointLength), XSTDERR);
    XCHECK((XJWT_GetAlgorithm(pJWT) != XJWT_ALG_INVALID), XSTDERR);

    size_t nOutLen = XJWT_HASH_LENGTH;
    uint8_t hash[XJWT_HASH_LENGTH];
    uint8_t *pSignature = NULL;

    if (pJWT->eAlgorithm == XJWT_ALG_HS256)
    {
        XHMAC_SHA256(hash, sizeof(hash), (uint8_t*)pJointData, nJointLength, (uint8_t*)pSecret, nSecretLen);
        pSignature = hash;
    }
#ifdef XCRYPT_USE_SSL
    else if (pJWT->eAlgorithm == XJWT_ALG_RS256)
    {
        pSignature = XCrypt_RS256((uint8_t*)pJointData, nJointLength, (char*)pSecret, nSecretLen, &nOutLen);
        XCHECK_FREE(pSignature, pJointData, XSTDERR);
    }
#endif
    else
    {
        free(pJointData);
        return XSTDEXC;
    }

    pJWT->pSignature = XBase64_UrlEncrypt(pSignature, &nOutLen);
    pJWT->nSignatureLen = nOutLen;

#ifdef XCRYPT_USE_SSL
    if (pJWT->eAlgorithm == XJWT_ALG_RS256) free(pSignature);
#endif

    free(pJointData);
    return pJWT->pSignature ? XSTDOK : XSTDERR;
}

char* XJWT_GetSignature(xjwt_t *pJWT, const uint8_t *pSecret, size_t nSecretLen, xbool_t bDecode, size_t *pSignatureLen)
{
    XCHECK(pJWT, NULL);
    if (pSignatureLen) *pSignatureLen = 0;

    if (pSecret != NULL && nSecretLen > 0)
    {
        XCHECK((XJWT_CreateSignature(pJWT, pSecret, nSecretLen) == XSTDOK), NULL);
        if (pSignatureLen) *pSignatureLen = pJWT->nSignatureLen;
    }

    if (bDecode)
    {
        size_t nSignatureLen = pJWT->nSignatureLen;
        char *pSignatureRaw = XBase64_UrlDecrypt((uint8_t*)pJWT->pSignature, &nSignatureLen);

        if (pSignatureLen) *pSignatureLen = nSignatureLen;
        return pSignatureRaw;
    }

    if (pSignatureLen) *pSignatureLen = pJWT->nSignatureLen;
    return pJWT->pSignature;
}

char* XJWT_Create(xjwt_t *pJWT, const uint8_t *pSecret, size_t nSecretLen, size_t *pJWTLen)
{
    if (pJWTLen != NULL) *pJWTLen = 0;
    XCHECK((pJWT && pSecret && nSecretLen), NULL);

    XCHECK(XJWT_GetHeader(pJWT, XFALSE, NULL), NULL);
    XCHECK(XJWT_GetPayload(pJWT, XFALSE, NULL), NULL);
    XCHECK((XJWT_GetAlgorithm(pJWT) != XJWT_ALG_INVALID), NULL);
    XCHECK(XJWT_GetSignature(pJWT, pSecret, nSecretLen, XFALSE, NULL), NULL);

    size_t nJWTLength = pJWT->nHeaderLen + pJWT->nPayloadLen + pJWT->nSignatureLen + 2;
    char *pJWTStr = (char*)malloc(nJWTLength + 1);
    XCHECK(pJWTStr, NULL);

    size_t nBytes = xstrncpyf(pJWTStr, nJWTLength + 1, "%s.%s.%s",
        pJWT->pHeader, pJWT->pPayload, pJWT->pSignature);

    if (pJWTLen != NULL) *pJWTLen = nBytes;
    return pJWTStr;
}

XSTATUS XJWT_VerifyHS256(xjwt_t *pJWT, const char *pSignature, size_t nSignatureLen, const uint8_t *pSecret, size_t nSecretLen)
{
    XCHECK(pJWT, XSTDINV);
    pJWT->bVerified = XFALSE;

    XCHECK((pSignature && nSignatureLen &&  pSecret && nSecretLen), XSTDINV);
    XCHECK(XJWT_GetSignature(pJWT, pSecret, nSecretLen, XFALSE, NULL), XSTDERR);
    pJWT->bVerified = !strncmp(pJWT->pSignature, pSignature, pJWT->nSignatureLen);
    return pJWT->bVerified ? XSTDOK : XSTDNON;
}

#ifdef XCRYPT_USE_SSL
XSTATUS XJWT_VerifyRS256(xjwt_t *pJWT, const char *pSignature, size_t nSignatureLen, const char *pPubKey, size_t nKeyLen)
{
    XCHECK(pJWT, XSTDINV);
    pJWT->bVerified = XFALSE;

    XSTATUS nStatus = XSTDERR;
    size_t nJointLen = 0;

    XCHECK((pSignature && nSignatureLen && pPubKey && nKeyLen), XSTDINV);
    uint8_t *pRawSignature = (uint8_t*)XBase64_UrlDecrypt((uint8_t*)pSignature, &nSignatureLen);
    XCHECK(pRawSignature, nStatus);

    uint8_t *pJoint = (uint8_t*)XJWT_CreateJoint(pJWT, &nJointLen);
    XCHECK_FREE(pJoint, pRawSignature, nStatus);

    nStatus = XCrypt_VerifyRS256(pRawSignature, nSignatureLen, pJoint, nJointLen, pPubKey, nKeyLen);
    pJWT->bVerified = (nStatus == XSTDOK) ? XTRUE : XFALSE;

    free(pRawSignature);
    free(pJoint);
    return nStatus;
}
#endif

XSTATUS XJWT_Verify(xjwt_t *pJWT, const char *pSignature, size_t nSignatureLen, const uint8_t *pSecret, size_t nSecretLen)
{
    XCHECK(pJWT, XSTDINV);

    if (pJWT->eAlgorithm == XJWT_ALG_HS256)
        return XJWT_VerifyHS256(pJWT, pSignature, nSignatureLen, pSecret, nSecretLen);
#ifdef XCRYPT_USE_SSL
    else if (pJWT->eAlgorithm == XJWT_ALG_RS256)
        return XJWT_VerifyRS256(pJWT, pSignature, nSignatureLen, (char*)pSecret, nSecretLen);
#endif

    return XSTDNON;
}

XSTATUS XJWT_Parse(xjwt_t *pJWT, const char *pJWTStr, size_t nLength, const uint8_t *pSecret, size_t nSecretLen)
{
    XCHECK(pJWT, XSTDINV);
    XJWT_Init(pJWT, XJWT_ALG_INVALID);
    XCHECK((pJWTStr && nLength), XSTDINV);

    xarray_t *pArray = xstrsplit(pJWTStr, ".");
    XCHECK(pArray, XSTDERR);

    const char *pHeader = (const char *)XArray_GetData(pArray, 0);
    const char *pPayload = (const char *)XArray_GetData(pArray, 1);
    const char *pSignature = (const char *)XArray_GetData(pArray, 2);
    XCHECK_CALL((pHeader && pPayload && pSignature), XArray_Destroy, pArray, XSTDERR);

    XSTATUS nStatus = XJWT_AddHeader(pJWT, pHeader, strlen(pHeader), XTRUE);
    XCHECK_CALL((nStatus == XSTDOK), XArray_Destroy, pArray, XSTDERR);

    nStatus = XJWT_AddPayload(pJWT, pPayload, strlen(pPayload), XTRUE);
    XCHECK_CALL((nStatus == XSTDOK), XArray_Destroy, pArray, XSTDERR);

    XCHECK_CALL((XJWT_GetHeader(pJWT, XFALSE, NULL)), XArray_Destroy, pArray, XSTDERR);
    XCHECK_CALL((XJWT_GetPayload(pJWT, XFALSE, NULL)), XArray_Destroy, pArray, XSTDERR);
    XCHECK_CALL((XJWT_GetAlgorithm(pJWT) != XJWT_ALG_INVALID), XArray_Destroy, pArray, XSTDERR);

    if (pSecret != NULL && nSecretLen > 0)
    {
        size_t nSignatureSize = XArray_GetSize(pArray, 2);
        XCHECK_CALL(nSignatureSize, XArray_Destroy, pArray, XSTDERR);
        nStatus = XJWT_Verify(pJWT, pSignature, nSignatureSize, pSecret, nSecretLen);
    }

    XArray_Destroy(pArray);
    return nStatus;
}
