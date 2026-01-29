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

#define XPOOL_DEFAULT_SIZE  4096

typedef struct XPool {
    struct XPool *pNext;
    uint8_t *pData;
    size_t nUsed;
    size_t nSize;
    xbool_t bAlloc;
} xpool_t;

void* xalloc(xpool_t *pPool, size_t nSize);
void* xrealloc(xpool_t *pPool, void *pData, size_t nDataSize, size_t nNewSize);

void xfree(xpool_t *pPool, void *pData);
void xfreen(xpool_t *pPool, void *pData, size_t nSize);

XSTATUS XPool_Init(xpool_t *pPool, size_t nSize);
xpool_t* XPool_Create(size_t nSize);

void XPool_Destroy(xpool_t *pPool);
void XPool_Reset(xpool_t *pPool);

void *XPool_Alloc(xpool_t *pPool, size_t nSize);
void *XPool_Realloc(xpool_t *pPool, void *pData, size_t nDataSize, size_t nNewSize);
void XPool_Free(xpool_t *pPool, void *pData, size_t nSize);

size_t XPool_GetSize(xpool_t *pPool);
size_t XPool_GetUsed(xpool_t *pPool);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_POOL_H__ */
