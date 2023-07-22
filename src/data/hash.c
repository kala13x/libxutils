/*!
 *  @file libxutils/src/data/hash.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of hash tables
 */

#include "hash.h"

typedef struct XHashIterator {
    xhash_itfunc_t itfunc;
    void *pUserCtx;
} xhash_iterator_t;

static int XHash_IteratorCb(void *pCtx, xlist_t *pNode)
{
    xhash_iterator_t *pIter = (xhash_iterator_t*)pCtx;
    if (pIter != NULL && pIter->itfunc != NULL) return XSTDERR;
    xhash_pair_t *pPair = (xhash_pair_t*)pNode->pData;
    return pPair != NULL ? pIter->itfunc(pPair, pIter->pUserCtx) : 0;
}

static int XHash_SearchCb(void *pKey, xlist_t *pNode)
{
    xhash_pair_t *pPair = (xhash_pair_t*)pNode->pData;
    return (pPair != NULL && *(int*)pKey == pPair->nKey) ? XTRUE : XFALSE;
}

static void XHash_ClearCb(void *pCbCtx, void *pData)
{
    xhash_pair_t* pPair = (xhash_pair_t*)pData;
    xhash_t *pHash = (xhash_t*)pCbCtx;

    if (pHash == NULL ||
        pPair == NULL ||
        pPair->pData == NULL) return;

    if (pHash->clearCb != NULL)
        pHash->clearCb(pHash->pUserContext, pPair->pData, pPair->nKey);

    free(pPair);
}

xhash_pair_t* XHash_NewPair(void *pData, size_t nSize, int nKey)
{
    xhash_pair_t* pPair = (xhash_pair_t*)malloc(sizeof(xhash_pair_t));
    if (pPair == NULL) return NULL;

    pPair->pData = pData;
    pPair->nSize = nSize;
    pPair->nKey = nKey;
    return pPair;
}

int XHash_Init(xhash_t *pHash, xhash_clearcb_t clearCb, void *pCtx)
{
    pHash->pUserContext = pCtx;
    pHash->clearCb = clearCb;
    pHash->nPairCount = 0;
    unsigned int i;

    for (i = 0; i < XHASH_MODULES; i++)
    {
        xlist_t *pTable = (xlist_t*)&pHash->tables[i];
        XList_Init(pTable, NULL, 0, XHash_ClearCb, pHash);
    }

    return XSTDOK;
}

void XHash_Destroy(xhash_t *pHash)
{
    unsigned int i;
    for (i = 0; i < XHASH_MODULES; i++)
        XList_Clear(&pHash->tables[i]);

    pHash->pUserContext = NULL;
    pHash->clearCb = NULL;
    pHash->nPairCount = 0;
}

xlist_t* XHash_GetNode(xhash_t *pHash, int nKey)
{
    uint32_t nHash = XHASH_MIX(nKey, XHASH_MODULES);
    xlist_t *pTable = (xlist_t*)&pHash->tables[nHash];
    return XList_Search(pTable, (void*)&nKey, XHash_SearchCb);
}

xhash_pair_t* XHash_GetPair(xhash_t *pHash, int nKey)
{
    xlist_t *pNode = XHash_GetNode(pHash, nKey);
    return pNode != NULL ? (xhash_pair_t*)pNode->pData : NULL;
}

void* XHash_GetData(xhash_t *pHash, int nKey)
{
    xhash_pair_t *pPair = XHash_GetPair(pHash, nKey);
    return pPair != NULL ? pPair->pData : NULL;
}

int XHash_GetSize(xhash_t *pHash, int nKey)
{
    xhash_pair_t *pPair = XHash_GetPair(pHash, nKey);
    return pPair != NULL ? (int)pPair->nSize : XSTDERR;
}

int XHash_InsertPair(xhash_t *pHash, xhash_pair_t *pData)
{
    uint32_t nHash = XHASH_MIX(pData->nKey, XHASH_MODULES);
    xlist_t *pTable = (xlist_t*)&pHash->tables[nHash];

    if (XList_PushNext(pTable, pData, 0) != NULL)
    {
        pHash->nPairCount++;
        return XSTDOK;
    }

    return XSTDERR;
}

int XHash_Insert(xhash_t *pHash, void *pData, size_t nSize, int nKey)
{
    xhash_pair_t *pPair = XHash_NewPair(pData, nSize, nKey);
    if (pPair == NULL) return XSTDERR;
    int nStatus = XHash_InsertPair(pHash, pPair);
    if (nStatus < 0) free(pPair);
    return nStatus;
}

int XHash_Delete(xhash_t *pHash, int nKey)
{
    xlist_t *pNode = XHash_GetNode(pHash, nKey);
    if (pNode == NULL) return XSTDERR;
    XList_Unlink(pNode);
    pHash->nPairCount--;
    return XSTDOK;
}

void XHash_Iterate(xhash_t *pHash, xhash_itfunc_t itfunc, void *pCtx)
{
    xhash_iterator_t iter;
    iter.pUserCtx = pCtx; 
    iter.itfunc = itfunc;
    unsigned int i;

    for (i = 0; i < XHASH_MODULES; i++)
    {
        xlist_t *pTable = (xlist_t*)&pHash->tables[i];
        if (XList_Search(pTable, &iter, XHash_IteratorCb) == NULL) break;
    }
}
