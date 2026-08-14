/*!
 *  @file libxutils/src/sys/pool.c
 *
 *  This source is part of "libxutils" project
 *  2015-2024  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of the memory pool functionality
 */

#include "pool.h"

/* Round the size up to XPOOL_ALIGNMENT, zero is returned
   if the rounding overflows and can not be represented */
static size_t XPool_AlignSize(size_t nSize)
{
    size_t nAligned = XPOOL_ALIGN(nSize);
    return nAligned < nSize ? 0 : nAligned;
}

static xbool_t XPool_HasSpace(xpool_t *pPool, size_t nSize)
{
    /* nUsed never exceeds nSize, so the subtraction can not wrap */
    return (pPool->pData != NULL &&
            nSize <= pPool->nSize - pPool->nUsed) ? XTRUE : XFALSE;
}

static xpool_t* XPool_FindSpace(xpool_t *pPool, size_t nSize)
{
    while (pPool != NULL)
    {
        if (XPool_HasSpace(pPool, nSize)) return pPool;
        pPool = pPool->pNext;
    }

    return NULL;
}

static xpool_t* XPool_FindOwner(xpool_t *pPool, void *pData)
{
    while (pPool != NULL)
    {
        if (pPool->pData != NULL &&
            (uint8_t *)pData >= pPool->pData &&
            (uint8_t *)pData < pPool->pData + pPool->nSize) return pPool;

        pPool = pPool->pNext;
    }

    return NULL;
}

/* Chase the tail hint to the real end of the chain and refresh it,
   the loop is a no-op while the hint is up to date */
static xpool_t* XPool_GetTail(xpool_t *pPool)
{
    xpool_t *pTail = pPool->pTail != NULL ? pPool->pTail : pPool;
    while (pTail->pNext != NULL) pTail = pTail->pNext;

    pPool->pTail = pTail;
    return pTail;
}

XSTATUS XPool_Init(xpool_t *pPool, size_t nSize)
{
    XCHECK_NL(pPool, XSTDERR);
    nSize = nSize ? nSize : XPOOL_DEFAULT_SIZE;

    nSize = XPool_AlignSize(nSize);
    if (!nSize) return XSTDERR;

    pPool->pData = (uint8_t *)malloc(nSize);
    if (!pPool->pData) return XSTDERR;

    pPool->nUsed = XSTDNON;
    pPool->nSize = nSize;
    pPool->pNext = NULL;
    pPool->pTail = pPool;
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

    while (pPool != NULL)
    {
        xpool_t *pNext = pPool->pNext;

        if (pPool->pData)
        {
            free(pPool->pData);
            pPool->pData = NULL;
        }

        pPool->nUsed = 0;
        pPool->nSize = 0;
        pPool->pNext = NULL;
        pPool->pTail = NULL;

        if (pPool->bAlloc) free(pPool);
        pPool = pNext;
    }
}

void XPool_Reset(xpool_t *pPool)
{
    while (pPool != NULL)
    {
        pPool->nUsed = 0;
        pPool = pPool->pNext;
    }
}

void *XPool_Alloc(xpool_t *pPool, size_t nSize)
{
    XCHECK_NL(pPool, NULL);
    XCHECK_NL(nSize, NULL);

    nSize = XPool_AlignSize(nSize);
    if (!nSize) return NULL;

    /* The tail is the hot chunk, check it before walking
       the whole chain in a search of a leftover space */
    xpool_t *pTail = XPool_GetTail(pPool);
    xpool_t *pCur = XPool_HasSpace(pTail, nSize) ? pTail : XPool_FindSpace(pPool, nSize);

    /* Nothing left in the chain, append a new pool */
    if (pCur == NULL)
    {
        size_t nNewSize = XSTD_MAX(nSize, pTail->nSize);
        pCur = XPool_Create(nNewSize);
        if (pCur == NULL) return NULL;

        pTail->pNext = pCur;
        pPool->pTail = pCur;
    }

    void *pRet = pCur->pData + pCur->nUsed;
    pCur->nUsed += nSize;

    return pRet;
}

void *XPool_Realloc(xpool_t *pPool, void *pData, size_t nDataSize, size_t nNewSize)
{
    XCHECK_NL(nNewSize, NULL);
    XCHECK_NL(pPool, NULL);

    if (pData == NULL || !nDataSize)
        return XPool_Alloc(pPool, nNewSize);

    size_t nOldAligned = XPool_AlignSize(nDataSize);
    size_t nNewAligned = XPool_AlignSize(nNewSize);
    if (!nOldAligned || !nNewAligned) return NULL;

    /* Resize in place if the block is the last allocation in its pool,
       otherwise growing a buffer step by step never reclaims anything */
    xpool_t *pOwner = XPool_FindOwner(pPool, pData);
    if (pOwner != NULL &&
        (uint8_t *)pData + nOldAligned == pOwner->pData + pOwner->nUsed &&
        (nNewAligned <= nOldAligned ||
         nNewAligned - nOldAligned <= pOwner->nSize - pOwner->nUsed))
    {
        pOwner->nUsed = pOwner->nUsed - nOldAligned + nNewAligned;
        return pData;
    }

    void *pNew = XPool_Alloc(pPool, nNewSize);
    if (pNew == NULL) return NULL;

    memcpy(pNew, pData, XSTD_MIN(nDataSize, nNewSize));
    XPool_Free(pPool, pData, nDataSize);

    return pNew;
}

void XPool_Free(xpool_t *pPool, void *pData, size_t nSize)
{
    XCHECK_VOID_NL(pData);
    XCHECK_VOID_NL(nSize);

    if (pPool == NULL)
    {
        free(pData);
        return;
    }

    nSize = XPool_AlignSize(nSize);
    if (!nSize) return;

    /* Pointers that do not belong to the chain are not ours to free */
    xpool_t *pCur = XPool_FindOwner(pPool, pData);
    if (pCur == NULL) return;

    /* Check if only this data is allocated in pool */
    if (pData == pCur->pData && nSize >= pCur->nUsed)
    {
        pCur->nUsed = 0;
        return;
    }

    /* Check if data is last allocated memory in pool */
    if (pCur->pData + pCur->nUsed == (uint8_t *)pData + nSize)
        pCur->nUsed -= nSize;

    /* If data is in the middle of pool, do nothing
       to keep memory aligned and avoid fragmentation */
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
    size_t nSize = 0;

    while (pPool != NULL)
    {
        nSize += pPool->nSize;
        pPool = pPool->pNext;
    }

    return nSize;
}

size_t XPool_GetUsed(xpool_t *pPool)
{
    size_t nUsed = 0;

    while (pPool != NULL)
    {
        nUsed += pPool->nUsed;
        pPool = pPool->pNext;
    }

    return nUsed;
}
