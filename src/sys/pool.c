/*!
 *  @file libxutils/src/sys/pool.c
 *
 *  This source is part of "libxutils" project
 *  2015-2024  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of the memory pool functionality
 */

#include "pool.h"

XSTATUS XPool_Init(xpool_t *pPool, size_t nSize)
{
    nSize = nSize ? nSize : XPOOL_DEFAULT_SIZE;
    nSize = (nSize + 7) & ~7; // Align to 8 bytes

    pPool->pData = (uint8_t *)malloc(nSize);
    if (!pPool->pData) return XSTDERR;

    pPool->nUsed = XSTDNON;
    pPool->nSize = nSize;
    pPool->pNext = NULL;
    pPool->bAlloc = XFALSE;

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
    XCHECK_VOID_NL(pPool);

    if (pPool->pData)
    {
        free(pPool->pData);
        pPool->pData = NULL;
    }

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
    XCHECK_VOID_NL(pPool);
    pPool->nUsed = 0;
    XPool_Reset(pPool->pNext);
}

void *XPool_Alloc(xpool_t *pPool, size_t nSize)
{
    XCHECK_NL(pPool, NULL);
    XCHECK_NL(nSize, NULL);

    // Find space in current pool
    if (pPool->nUsed + nSize > pPool->nSize)
    {
        // Create new pool if next is not found
        if (pPool->pNext == NULL)
        {
            size_t nNewSize = XSTD_MAX(nSize, pPool->nSize);
            pPool->pNext = XPool_Create(nNewSize);
            if (pPool->pNext == NULL) return NULL;
        }

        // Try to allocate memory in new pool
        return XPool_Alloc(pPool->pNext, nSize);
    }

    void *pRet = pPool->pData + pPool->nUsed;
    pPool->nUsed += nSize;

    return pRet;
}

void *XPool_Realloc(xpool_t *pPool, void *pData, size_t nDataSize, size_t nNewSize)
{
    XCHECK_NL(nNewSize, NULL);
    XCHECK_NL(pPool, NULL);

    void *pNew = XPool_Alloc(pPool, nNewSize);
    if (pNew == NULL) return NULL;

    if (pData != NULL && nDataSize)
    {
        size_t nCopySize = XSTD_MIN(nDataSize, nNewSize);
        memcpy(pNew, pData, nCopySize);
        XPool_Free(pPool, pData, nDataSize);
    }

    return pNew;
}

void XPool_Free(xpool_t *pPool, void *pData, size_t nSize)
{
    XCHECK_VOID_NL(pData);
    XCHECK_VOID_NL(nSize);

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
                nSize >= pCur->nUsed)
            {
                pCur->nUsed = 0;
                return;
            }

            // Check if data is last allocated memory in pool
            uint8_t *pOffset = pCur->pData + pCur->nUsed;
            uint8_t *pDataEnd = (uint8_t *)pData + nSize;
            if (pOffset == pDataEnd) pCur->nUsed -= nSize;

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
    XCHECK_NL(nSize, NULL);
    if (!pPool) return malloc(nSize);
    return XPool_Alloc(pPool, nSize);
}

void* xrealloc(xpool_t *pPool, void *pData, size_t nDataSize, size_t nNewSize)
{
    XCHECK_NL(nNewSize, NULL);
    if (!pPool) return realloc(pData, nNewSize);
    return XPool_Realloc(pPool, pData, nDataSize, nNewSize);
}

void xfree(xpool_t *pPool, void *pData)
{
    XCHECK_VOID_NL(pData);
    if (!pPool) free(pData);
}

void xfreen(xpool_t *pPool, void *pData, size_t nSize)
{
    XCHECK_VOID_NL(pData);
    if (!nSize) xfree(pPool, pData);
    else XPool_Free(pPool, pData, nSize);
}

size_t XPool_GetSize(xpool_t *pPool)
{
    XCHECK_NL(pPool, 0);
    return pPool->nSize;
}

size_t XPool_GetUsed(xpool_t *pPool)
{
    XCHECK_NL(pPool, 0);
    return pPool->nUsed;
}
