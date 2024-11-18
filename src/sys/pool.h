/*!
 *  @file libxutils/src/sys/pool.h
 *
 *  This source is part of "libxutils" project
 *  2015-2024  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of the memory pool functionality
 */

#ifndef __XUTILS_POOL_H__
#define __XUTILS_POOL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"

typedef struct XPool {
    struct XPool *pNext;
    uint8_t *pData;
    size_t nOffset;
    size_t nUsed;
    size_t nSize;
    xbool_t bAlloc;
} xpool_t;

void* xalloc(xpool_t *pPool, size_t nSize);
void* xrealloc(xpool_t *pPool, void *pData, size_t nOldSize, size_t nSize);

void xfree(xpool_t *pPool, void *pData);
void xfreen(xpool_t *pPool, void *pData, size_t nSize);

XSTATUS XPool_Init(xpool_t *pPool, size_t nSize);
xpool_t* XPool_Create(size_t nSize);

void XPool_Destroy(xpool_t *pPool);
void XPool_Reset(xpool_t *pPool);

void *XPool_Alloc(xpool_t *pPool, size_t nSize);
void XPool_Free(xpool_t *pPool, void *pData, size_t nSize);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_POOL_H__ */
