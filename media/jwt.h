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

char* XJWT_Create(const char *pPayload, size_t nPayloadLen, const uint8_t *pSecret, size_t nSecretLen, size_t *pJWTLen);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_JWT_H__ */