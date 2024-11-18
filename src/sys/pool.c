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
    pPool->nUsed = 0;
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
    XASSERT_VOID(pPool);

    if (pPool->pData)
    {
        free(pPool->pData);
        pPool->pData = NULL;
    }

    pPool->nOffset = 0;
    pPool->nUsed = 0;
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
    XASSERT_VOID(pPool);
    pPool->nOffset = 0;
    pPool->nUsed = 0;
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
    pPool->nUsed += nSize;

    return pRet;
}

void XPool_Free(xpool_t *pPool, void *pData, size_t nSize)
{
    XASSERT_VOID(pPool);
    XASSERT_VOID(pData);

    xpool_t *pCur = pPool;
    while (pCur != NULL)
    {
        if (pCur->pData != NULL &&
            (uint8_t *)pData >= pCur->pData &&
            (uint8_t *)pData < pCur->pData + pCur->nSize)
        {
            size_t nOffset = (uint8_t *)pData - pCur->pData;
            uint8_t *pTail = pCur->pData + nOffset + nSize;
            size_t nTailSize = pCur->nSize - nOffset - nSize;

            memmove(pCur->pData, pTail, nTailSize);
            pCur->nOffset -= nSize;
            pCur->nUsed -= nSize;

            return;
        }

        pCur = pCur->pNext;
    }
}

void* xalloc(xpool_t *pPool, size_t nSize)
{
    XASSERT(nSize, NULL);
    if (!pPool) return malloc(nSize);
    return XPool_Alloc(pPool, nSize);
} 

void xfree(xpool_t *pPool, void *pData)
{
    XASSERT_VOID(pData);
    if (!pPool) free(pData);
}

void xfreen(xpool_t *pPool, void *pData, size_t nSize)
{
    XASSERT_VOID(pData);
    if (nSize == 0) xfree(pPool, pData);
    else XPool_Free(pPool, pData, nSize);
}