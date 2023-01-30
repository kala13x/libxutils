/*!
 *  @file libxutils/media/jwt.h
 *
 *  This source is part of "libxutils" project
 *  2019-2023  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of JSON Web Tokens
 */

#ifndef __XUTILS_JWT_H__
#define __XUTILS_JWT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "crypt.h"
#include "xtype.h"

typedef enum {
    XJWT_ALG_INVALID = 0,
    XJWT_ALG_HS256,
#ifdef XCRYPT_USE_SSL
    XJWT_ALG_RS256
#endif
} xjwt_alg_t;

typedef struct XJWT {
    char *pHeader;
    size_t nHeaderLen;
    xjson_obj_t *pHeaderObj;

    char *pPayload;
    size_t nPayloadLen;
    xjson_obj_t *pPayloadObj;

    char *pSignature;
    size_t nSignatureLen;

    xjwt_alg_t eAlgorithm;
    xbool_t bVerified;
} xjwt_t;

const char* XJWT_GetAlgStr(xjwt_alg_t eAlg);
xjwt_alg_t XJWT_GetAlg(const char *pAlgStr);

void XJWT_Init(xjwt_t *pJWT, xjwt_alg_t eAlg);
void XJWT_Destroy(xjwt_t *pJWT);

XSTATUS XJWT_AddPayload(xjwt_t *pJWT, const char *pPayload, size_t nPayloadLen, xbool_t bIsEncoded);
char* XJWT_GetPayload(xjwt_t *pJWT, xbool_t bDecode, size_t *pPayloadLen);
xjson_obj_t* XJWT_GetPayloadObj(xjwt_t *pJWT);

XSTATUS XJWT_AddHeader(xjwt_t *pJWT, const char *pHeader, size_t nHeaderLen, xbool_t bIsEncoded);
char* XJWT_GetHeader(xjwt_t *pJWT, xbool_t bDecode, size_t *pHeaderLen);
xjson_obj_t* XJWT_CreateHeaderObj(xjwt_alg_t eAlg);
xjson_obj_t* XJWT_GetHeaderObj(xjwt_t *pJWT);

char* XJWT_Create(xjwt_t *pJWT, const uint8_t *pSecret, size_t nSecretLen, size_t *pJWTLen);
XSTATUS XJWT_Parse(xjwt_t *pJWT, const char *pJWTStr, size_t nLength, const uint8_t *pSecret, size_t nSecretLen);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_JWT_H__ */