/*!
 *  @file libxutils/src/sys/pPool.c
 *
 *  This source is part of "libxutils" project
 *  2015-2024  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of the memory pPool functionality
 */

#include "pool.h"

XSTATUS XPool_Init(xpool_t *pPool, size_t nSize)
{
    // Align to 8 bytes
    nSize = (nSize + 7) & ~7;

    pPool->pData = (uint8_t *)malloc(nSize);
    if (!pPool->pData) return XSTDERR;

    pPool->bAlloc = XFALSE;
    pPool->nOffset = 0;
    pPool->nSize = nSize;
    pPool->pNext = NULL;

    return XSTDOK;
}

xpool_t* XPool_Create(size_t nSize)
{
    xpool_t *pPool = (xpool_t *)malloc(sizeof(xpool_t));
    if (pPool == NULL) return NULL;

    if (XPool_Init(pPool, nSize) != XSTDOK)
    {
        free(pPool);
        return NULL;
    }

    pPool->bAlloc = XTRUE;
    return pPool;
}

void XPool_Destroy(xpool_t *pPool)
{
    XASSERT_VOID_RET(pPool);

    if (pPool->pData)
    {
        free(pPool->pData);
        pPool->pData = NULL;
    }

    pPool->nOffset = 0;
    pPool->nSize = 0;

    if (pPool->pNext)
    {
        XPool_Destroy(pPool->pNext);
        pPool->pNext = NULL;
    }

    if (pPool->bAlloc) free(pPool);
}

void XPool_Reset(xpool_t *pPool)
{
    XASSERT_VOID_RET(pPool);
    pPool->nOffset = 0;
    XPool_Reset(pPool->pNext);
}

void *XPool_Alloc(xpool_t *pPool, size_t nSize)
{
    XASSERT(pPool, NULL);
    XASSERT(nSize, NULL);

    // Find space in current pool
    if (pPool->nOffset + nSize > pPool->nSize)
    {
        // Create new pool if next is not found
        if (pPool->pNext == NULL)
        {
            size_t nNewSize = nSize > pPool->nSize ?
                              nSize : pPool->nSize;

            pPool->pNext = XPool_Create(nNewSize);
            if (pPool->pNext == NULL) return NULL;
        }

        // Try to allocate memory in new pool
        return XPool_Alloc(pPool->pNext, nSize);
    }

    void *pRet = pPool->pData + pPool->nOffset;
    pPool->nOffset += nSize;

    return pRet;
}

void XPool_Free(xpool_t *pPool, void *pData, size_t nSize)
{
    XASSERT_VOID_RET(pPool);
    XASSERT_VOID_RET(pData);
    XASSERT_VOID_RET(nSize);

    xpool_t *pCur = pPool;
    if (pCur == NULL)
    {
        free(pData);
        return;
    }

    while (pCur != NULL)
    {
        if (pCur->pData != NULL &&
            (uint8_t *)pData >= pCur->pData &&
            (uint8_t *)pData < pCur->pData + pCur->nSize)
        {
            // Check if only this data is allocated in pool
            if (pData == pCur->pData &&
                nSize >= pCur->nOffset)
            {
                pCur->nOffset = 0;
                return;
            }

            // Check if data is last allocated memory in pool
            uint8_t *pOffset = pCur->pData + pCur->nOffset;
            uint8_t *pEnd = (uint8_t *)pData + nSize;
            if (pOffset == pEnd) pCur->nOffset -= nSize;

            // If data is in the middle of pool, do nothing
            // to keep memory aligned and avoid fragmentation
            return;
        }

        // Check next pool
        pCur = pCur->pNext;
    }
}

void* xalloc(xpool_t *pPool, size_t nSize)
{
    XASSERT_RET(nSize, NULL);
    if (!pPool) return malloc(nSize);
    return XPool_Alloc(pPool, nSize);
}

void* xrealloc(xpool_t *pPool, void *pData, size_t nOldSize, size_t nSize)
{
    XASSERT_RET(nSize, NULL);
    if (!pPool) return realloc(pData, nSize);

    void *pNew = XPool_Alloc(pPool, nSize);
    if (pNew == NULL) return NULL;

    if (pData != NULL && nOldSize)
    {
        size_t nCopySize = XSTD_MIN(nOldSize, nSize);
        memcpy(pNew, pData, nCopySize);
    }

    return pNew;
}

void xfree(xpool_t *pPool, void *pData)
{
    XASSERT_VOID_RET(pData);
    if (!pPool) free(pData);
}

void xfreen(xpool_t *pPool, void *pData, size_t nSize)
{
    XASSERT_VOID_RET(pData);
    if (nSize == 0) xfree(pPool, pData);
    else XPool_Free(pPool, pData, nSize);
}

size_t XPool_GetSize(xpool_t *pPool)
{
    XASSERT_RET(pPool, 0);
    return pPool->nSize;
}

size_t XPool_GetUsed(xpool_t *pPool)
{
    XASSERT_RET(pPool, 0);
    return pPool->nOffset;
}