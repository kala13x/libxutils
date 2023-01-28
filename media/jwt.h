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

#include "xtype.h"

typedef enum {
    XJWT_ALG_INVALID = 0,
    XJWT_ALG_HS256,
    XJWT_ALG_RS256
} xjwt_alg_t;

typedef struct XJWT {
    xjson_obj_t *pHeaderObj;
    xjson_obj_t *pPayloadObj;
    xbool_t bVerified;
} xjwt_t;

xjson_obj_t* XJWT_CreateHeaderObj(xjwt_alg_t eAlg);
char* XJWT_CreateHeader(xjwt_alg_t eAlg, size_t *pHdrLen);

void XJWT_Destroy(xjwt_t *pJWT);

char* XJWT_CreateAdv(
        xjwt_alg_t eAlg,
        const char *pHeader,
        size_t nHeaderLen,
        const char *pPayload,
        size_t nPayloadLen,
        const uint8_t *pSecret,
        size_t nSecretLen,
        size_t *pJWTLen
    );

char* XJWT_Create(
        xjwt_alg_t eAlg,
        const char *pPayload,
        size_t nPayloadLen,
        const uint8_t *pSecret,
        size_t nSecretLen,
        size_t *pJWTLen
    );

XSTATUS XJWT_Parse(xjwt_t *pJWT, const char *pJWTStr, size_t nLength, const uint8_t *pSecret, size_t nSecretLen);
char* XJWT_Dump(xjwt_t *pJWT, const uint8_t *pSecret, size_t nSecretLen, size_t *pJWTLen);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_JWT_H__ */