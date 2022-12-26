/*!
 *  @file libxutils/src/xtype.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
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

#ifdef _WIN32
typedef int                 xsocklen_t;
typedef long                xatomic_t;
typedef int                 xmode_t;
typedef int                 xpid_t;
#else
typedef socklen_t           xsocklen_t;
typedef uint32_t            xatomic_t;
typedef mode_t              xmode_t;
typedef pid_t               xpid_t;
#endif

typedef uint8_t             xbool_t;
#define XTRUE               1
#define XFALSE              0

#define XATOMIC             volatile xatomic_t

#define XCHAR(var,size) char var[size] = {'\0'}
#define XARG_SIZE(val)  val, sizeof(val)

#define XFTON(x) ((x)>=0.0f?(int)((x)+0.5f):(int)((x)-0.5f))

uint32_t XFloatToU32(float fValue);
float XU32ToFloat(uint32_t nValue);

size_t XBytesToUnit(char *pDst, size_t nSize, size_t nBytes, xbool_t bShort);
size_t XKBToUnit(char *pDst, size_t nSize, size_t nKB, xbool_t bShort);
xbool_t XTypeIsPrint(const uint8_t *pData, size_t nSize);

#ifdef __cplusplus
}
#endif


#endif /* __XUTILS_XTYPE_H__ */