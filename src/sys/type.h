/*!
 *  @file libxutils/src/sys/type.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of various 
 * types and converting operations.
 */

#ifndef __XUTILS_XTYPE_H__
#define __XUTILS_XTYPE_H__

#include "xstd.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t XFloatToU32(float fValue);
float XU32ToFloat(uint32_t nValue);

size_t XBytesToUnit(char *pDst, size_t nSize, size_t nBytes, xbool_t bShort);
size_t XKBToUnit(char *pDst, size_t nSize, size_t nKB, xbool_t bShort);
xbool_t XTypeIsPrint(const uint8_t *pData, size_t nSize);

#ifdef __cplusplus
}
#endif


#endif /* __XUTILS_XTYPE_H__ */