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

char* XJWT_Create(const char *pPayload, size_t nLength, const uint8_t *pKey, size_t nKeyLen, size_t *pJWTLen);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_JWT_H__ */