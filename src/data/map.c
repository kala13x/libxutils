/*!
 *  @file libxutils/src/data/xmap.c
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of dynamically allocated hash map
 */

#include <string.h>
#include "map.h"

#ifdef _XMAP_USE_CRYPT
#include "crc32.h"
#include "sha256.h"
#endif

#define XFNV_OFFSET_32 2166136261
#define XFNV_PRIME_32  16777619

static int XMap_Alloc(xmap_t *pMap, xpool_t *pPool, size_t nSize)
{
    XASSERT_RET(pMap, XMAP_OINV);
    if (!nSize) return XMAP_OK;

    pMap->pPairs = (xmap_pair_t*)xalloc(pPool, nSize * sizeof(xmap_pair_t));
    if (pMap->pPairs == NULL) return XMAP_OMEM;

    size_t i;
    for (i = 0; i < pMap->nTableSize; i++)
    {
        pMap->pPairs[i].nUsed = 0;
        pMap->pPairs[i].pData = NULL;
        pMap->pPairs[i].pKey = NULL;
    }

    return XMAP_OK;
}

int XMap_Init(xmap_t *pMap, xpool_t *pPool, size_t nSize)
{
    XASSERT_RET(pMap, XMAP_OINV);

    pMap->nTableSize = nSize;
    pMap->clearCb = NULL;
    pMap->pPairs = NULL;
    pMap->nAlloc = 0;
    pMap->nUsed = 0;
    pMap->pPool = pPool;
    pMap->eHashType = XMAP_HASH_FNV;

    return XMap_Alloc(pMap, pPool, nSize);
}

xmap_t *XMap_New(xpool_t *pPool, size_t nSize)
{
    xmap_t *pMap = (xmap_t*)xalloc(pPool, sizeof(xmap_t));
    if(pMap == NULL) return NULL;

    if (XMap_Init(pMap, pPool, nSize) < 0)
    {
        xfree(pPool, pMap);
        return NULL;
    }

    pMap->nAlloc = 1;
    return pMap;
}

void XMap_Free(xmap_t *pMap)
{
    if (pMap != NULL)
    {
        xpool_t *pPool = pMap->pPool;

        if (pMap->pPairs != NULL)
        {
             xfree(pPool, pMap->pPairs);
             pMap->pPairs = NULL;
        }

        if (pMap->nAlloc)
        {
            xfreen(pPool, pMap, sizeof(xmap_t));
        }
        else
        {
            pMap->clearCb = NULL;
            pMap->nUsed = 0;
        }
    }
}

static int XMap_ClearIt(xmap_pair_t *pPair, void *pCtx)
{
    xmap_t *pMap = (xmap_t*)pCtx;
    if (pMap->clearCb != NULL)
        pMap->clearCb(pPair);

    pPair->nUsed = 0;
    pPair->pData = NULL;
    pPair->pKey = NULL;
    return XMAP_OK;
}

int XMap_Iterate(xmap_t *pMap, xmap_iterator_t itfunc, void *pCtx)
{
    if (!XMap_UsedSize(pMap)) return XMAP_EMPTY;
    size_t i;

    for (i = 0; i < pMap->nTableSize; i++)
    {
        if (pMap->pPairs[i].nUsed)
        {
            int nStatus = itfunc(&pMap->pPairs[i], pCtx);
            if (nStatus != XMAP_OK) return nStatus;
        }
    }

    return XMAP_OK;
}

void XMap_Reset(xmap_t *pMap)
{
    if (pMap == NULL || pMap->pPairs == NULL) return;
    XMap_Iterate(pMap, XMap_ClearIt, pMap);
}

void XMap_Destroy(xmap_t *pMap)
{
    XMap_Reset(pMap);
    XMap_Free(pMap);
}

#ifdef _XMAP_USE_CRYPT
int XMap_HashMIX(xmap_t *pMap, const char *pStr)
{
    if (!pMap->nTableSize) return XMAP_EINIT;
    uint32_t nHash = XCRC32_Compute((unsigned char*)(pStr), strlen(pStr));

    /* Robert Jenkins' 32 bit Mix Function */
    nHash += (nHash << 12);
    nHash ^= (nHash >> 22);
    nHash += (nHash << 4);
    nHash ^= (nHash >> 9);
    nHash += (nHash << 10);
    nHash ^= (nHash >> 2);
    nHash += (nHash << 7);
    nHash ^= (nHash >> 12);

    /* Knuth's Multiplicative Method */
    nHash = (nHash >> 3) * 2654435761;
    return nHash % pMap->nTableSize;
}

int XMap_HashCRC32(xmap_t *pMap, const char *pStr)
{
    if (!pMap->nTableSize) return XMAP_EINIT;
    uint32_t nHash = XCRC32_Compute((unsigned char*)(pStr), strlen(pStr));

    nHash = ((nHash >> 16) ^ nHash) * 0x45d9f3b;
    nHash = ((nHash >> 16) ^ nHash) * 0x45d9f3b;
    nHash = (nHash >> 16) ^ nHash;
    return nHash % pMap->nTableSize;
}

int XMap_HashSHA256(xmap_t *pMap, const char *pStr)
{
    if (!pMap->nTableSize) return XMAP_EINIT;

    unsigned char hash[XSHA256_LENGTH + 1];
    XSHA256_ComputeSum((char*)hash, sizeof(hash), (const uint8_t*)pStr, strlen(pStr));

    uint32_t nHash = 0;
    for (int i = 0; i < XSHA256_LENGTH; i++)
    {
        nHash ^= hash[i];
        nHash += (nHash << 10);
        nHash ^= (nHash >> 6);
    }

    nHash += (nHash << 3);
    nHash ^= (nHash >> 11);
    nHash += (nHash << 15);

    return nHash % pMap->nTableSize;
}
#endif /* _XMAP_USE_CRYPT */

int XMap_HashFNV(xmap_t *pMap, const char *pStr)
{
    if (!pMap->nTableSize) return XMAP_EINIT;
    uint32_t nHash = XFNV_OFFSET_32;

    while (*pStr)
    {
        nHash ^= *pStr++;
        nHash *= XFNV_PRIME_32;
    }

    return nHash % pMap->nTableSize;
}

int XMap_Hash(xmap_t *pMap, const char *pStr)
{
    if (pMap == NULL || pStr == NULL) return XMAP_EINIT;

    switch (pMap->eHashType)
    {
        case XMAP_HASH_FNV:
            return XMap_HashFNV(pMap, pStr);
#ifdef _XMAP_USE_CRYPT
        case XMAP_HASH_MIX:
            return XMap_HashMIX(pMap, pStr);
        case XMAP_HASH_CRC32:
            return XMap_HashCRC32(pMap, pStr);
        case XMAP_HASH_SHA256:
            return XMap_HashSHA256(pMap, pStr);
#endif
        default:
            break;
    }

    return XMap_HashFNV(pMap, pStr);
}

int XMap_GetHash(xmap_t *pMap, const char* pKey)
{
    if (pMap->nUsed >= pMap->nTableSize) return XMAP_FULL;
    int i, nIndex = XMap_Hash(pMap, pKey);

    for (i = 0; i < XMAP_CHAIN_LENGTH; i++)
    {
        if (!pMap->pPairs[nIndex].nUsed) return nIndex;
        else if (!strcmp(pMap->pPairs[nIndex].pKey, pKey)) return nIndex;
        nIndex = (nIndex + 1) % (int)pMap->nTableSize;
    }

    return XMAP_FULL;
}

int XMap_Realloc(xmap_t *pMap)
{
    if (pMap == NULL) return XMAP_OINV;
    xpool_t *pPool = pMap->pPool;
    int nStatus = XMAP_OK;

    size_t i, nNewSize = pMap->nTableSize ? pMap->nTableSize * 2 : XMAP_INITIAL_SIZE;
    xmap_pair_t *pPairs = (xmap_pair_t*)xalloc(pPool, nNewSize * sizeof(xmap_pair_t));
    if (pPairs == NULL) return XMAP_OMEM;

    for (i = 0; i < nNewSize; i++)
    {
        pPairs[i].nUsed = 0;
        pPairs[i].pData = NULL;
        pPairs[i].pKey = NULL;
    }

    xmap_pair_t* pOldPairs = pMap->pPairs;
    size_t nOldSize = pMap->nTableSize;
    size_t nUsed = pMap->nUsed;

    pMap->nUsed = 0;
    pMap->pPairs = pPairs;
    pMap->nTableSize = nNewSize;
    if (pOldPairs == NULL) return nStatus;

    for(i = 0; i < nOldSize; i++)
    {
        if (!pOldPairs[i].nUsed) continue;
        nStatus = XMap_Put(pMap, pOldPairs[i].pKey, pOldPairs[i].pData);

        if (nStatus != XMAP_OK)
        {
            pMap->nTableSize = nOldSize;
            pMap->pPairs = pOldPairs;
            pMap->nUsed = nUsed;

            xfree(pPool, pPairs);
            return nStatus;
        }
    }

    xfree(pPool, pOldPairs);
    return nStatus;
}

int XMap_Put(xmap_t *pMap, char* pKey, void *pValue)
{
    if (pMap == NULL || pKey == NULL) return XMAP_OINV;
    int nHash = XMap_GetHash(pMap, pKey);

    while (nHash == XMAP_FULL)
    {
        int nStatus = XMap_Realloc(pMap);
        if (nStatus < 0) return nStatus;
        nHash = XMap_GetHash(pMap, pKey);
    }

    /* Set the data */
    pMap->pPairs[nHash].pData = pValue;
    pMap->pPairs[nHash].pKey = pKey;
    pMap->pPairs[nHash].nUsed = 1;
    pMap->nUsed++;
    return XMAP_OK;
}

int XMap_PutPair(xmap_t *pMap, xmap_pair_t *pPair)
{
    if (pMap == NULL || pPair == NULL) return XMAP_OINV;
    return XMap_Put(pMap, pPair->pKey, pPair->pData);
}

int XMap_Update(xmap_t *pMap, int nHash,  char *pKey, void *pValue)
{
    if ((size_t)nHash >= pMap->nTableSize) return XMAP_MISSING;
    pMap->pPairs[nHash].pData = pValue;
    pMap->pPairs[nHash].pKey = pKey;
    pMap->pPairs[nHash].nUsed = 1;
    return XMAP_OK;
}

xmap_pair_t *XMap_GetPair(xmap_t *pMap, const char* pKey)
{
    if (pMap == NULL || pKey == NULL) return NULL;

    int nIndex = -1;
    int i = 0;

    nIndex = XMap_Hash(pMap, pKey);
    if (nIndex == XMAP_EINIT) return NULL;

    for (i = 0; i < XMAP_CHAIN_LENGTH; i++)
    {
        if (pMap->pPairs[nIndex].nUsed)
        {
            if (!strcmp(pMap->pPairs[nIndex].pKey, pKey))
                return &pMap->pPairs[nIndex];
        }

        nIndex = (nIndex + 1) % (int)pMap->nTableSize;
    }

    return NULL;
}

void* XMap_GetIndex(xmap_t *pMap, const char* pKey, int *pIndex)
{
    if (pMap == NULL || pKey == NULL) return NULL;

    *pIndex = -1;
    int i = 0;

    *pIndex = XMap_Hash(pMap, pKey);
    if (*pIndex == XMAP_EINIT) return NULL;

    for (i = 0; i < XMAP_CHAIN_LENGTH; i++)
    {
        if (pMap->pPairs[*pIndex].nUsed)
        {
            if (!strcmp(pMap->pPairs[*pIndex].pKey, pKey))
                return pMap->pPairs[*pIndex].pData;
        }

        *pIndex = (*pIndex + 1) % (int)pMap->nTableSize;
    }

    return NULL;
}

void* XMap_Get(xmap_t *pMap, const char* pKey)
{
    int nDummy = 0;
    return XMap_GetIndex(pMap, pKey, &nDummy);
}

int XMap_Remove(xmap_t *pMap, const char* pKey)
{
    if (pMap == NULL || pKey == NULL) return XMAP_OINV;
    int i, nIndex = XMap_Hash(pMap, pKey);
    if (nIndex == XMAP_EINIT) return nIndex;

    for (i = 0; i < XMAP_CHAIN_LENGTH; i++)
    {
        if (pMap->pPairs[nIndex].nUsed)
        {
            if (!strcmp(pMap->pPairs[nIndex].pKey, pKey))
            {
                xmap_pair_t *pPair = &pMap->pPairs[nIndex];
                if (pMap->nUsed) pMap->nUsed--;

                if (pMap->clearCb != NULL) 
                    pMap->clearCb(pPair);

                pPair->nUsed = 0;
                pPair->pData = NULL;
                pPair->pKey = NULL;

                return XMAP_OK;
            }
        }

        nIndex = (nIndex + 1) % (int)pMap->nTableSize;
    }

    return XMAP_MISSING;
}

int XMap_UsedSize(xmap_t *pMap)
{
    if(pMap == NULL) return XMAP_OINV;
    return (int)pMap->nUsed;
}