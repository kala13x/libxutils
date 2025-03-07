/*!
 *  @file libxutils/src/data/array.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Dynamically allocated data holder 
 * with some sorting and search algorithms.
 */

#include <string.h>
#include "array.h"

xarray_data_t *XArray_NewData(xarray_t *pArr, void *pData, size_t nSize, uint32_t nKey)
{
    xarray_data_t *pNewData = (xarray_data_t *)xalloc(pArr->pPool, sizeof(xarray_data_t));
    if (pNewData == NULL) return NULL;

    if (pData != NULL && nSize > 0) 
    {
        pNewData->pData = xalloc(pArr->pPool, nSize);
        if (pNewData->pData == NULL)
        {
            xfreen(pArr->pPool, pNewData, sizeof(xarray_data_t));
            return NULL;
        }

        memcpy(pNewData->pData, pData, nSize);
        pNewData->nSize = nSize;
    }
    else
    {
        pNewData->pData = pData;
        pNewData->nSize = 0;
    }

    pNewData->pPool = pArr->pPool;
    pNewData->nKey = nKey;
    return pNewData;
}

void XArray_FreeData(xarray_data_t *pArrData)
{
    if (pArrData != NULL)
    {
        if (pArrData->pData && pArrData->nSize)
            xfreen(pArrData->pPool, pArrData->pData, pArrData->nSize);

        xfreen(pArrData->pPool, pArrData, sizeof(xarray_data_t));
    }
}

void XArray_ClearData(xarray_t *pArr, xarray_data_t *pArrData)
{
    if (pArr != NULL && 
        pArrData != NULL && 
        pArr->clearCb != NULL) 
    {
        pArr->clearCb(pArrData);
        pArrData->pData = NULL;
        pArrData->nSize = 0;
    }

    XArray_FreeData(pArrData);
}

void* XArray_Init(xarray_t *pArr, xpool_t *pPool, size_t nSize, uint8_t nFixed)
{
    pArr->eStatus = XARRAY_STATUS_EMPTY;
    pArr->clearCb = NULL;
    pArr->pData = NULL;
    pArr->nSize = nSize;
    pArr->nFixed = nFixed;
    pArr->nAlloc = 0;
    pArr->nUsed = 0;
    pArr->pPool = pPool;
    pArr->nHasPool = 0;

    if (nSize)
    {
        pArr->pData = (xarray_data_t**)xalloc(pPool, nSize * sizeof(xarray_data_t*));
        if (pArr->pData == NULL) return NULL;
    }

    size_t i;
    for (i = 0; i < nSize; i++)
        pArr->pData[i] = NULL;

    return pArr->pData;
}

void* XArray_InitPool(xarray_t *pArr, size_t nPoolSize, size_t nSize, uint8_t nFixed)
{
    xpool_t *pPool = XPool_Create(nPoolSize);
    if (pPool == NULL) return NULL;

    void *pData = XArray_Init(pArr, pPool, nSize, nFixed);
    if (nSize && pData == NULL)
    {
        XPool_Destroy(pPool);
        return NULL;
    }

    pArr->nHasPool = 1;
    return pData;
}

xarray_t* XArray_New(xpool_t *pPool, size_t nSize, uint8_t nFixed)
{
    xarray_t *pArr = (xarray_t*)xalloc(pPool, sizeof(xarray_t));
    if (pArr == NULL) return NULL;

    if (XArray_Init(pArr, pPool, nSize, nFixed) == NULL && nSize)
    {
        xfreen(pPool, pArr, sizeof(xarray_t));
        return NULL;
    }

    pArr->nAlloc = 1;
    return pArr;
}

xarray_t* XArray_NewPool(size_t nPoolSize, size_t nSize, uint8_t nFixed)
{
    xpool_t *pPool = XPool_Create(nPoolSize);
    if (pPool == NULL) return NULL;

    xarray_t *pArr = XArray_New(pPool, nSize, nFixed);
    if (nSize && pArr == NULL)
    {
        XPool_Destroy(pPool);
        return NULL;
    }

    pArr->nHasPool = 1;
    return pArr;
}

void XArray_Clear(xarray_t *pArr)
{
    if (pArr->pData != NULL)
    {
        size_t i;
        for (i = 0; i < pArr->nSize; i++) 
        {
            XArray_ClearData(pArr, pArr->pData[i]);
            pArr->pData[i] = NULL;
        }
    }

    if (pArr->nHasPool) XPool_Reset(pArr->pPool);
    pArr->eStatus = XARRAY_STATUS_EMPTY;
    pArr->nUsed = 0;
}

void XArray_Destroy(xarray_t *pArr)
{
    XArray_Clear(pArr);

    xpool_t *pPool = pArr->pPool;
    uint8_t nAlloc = pArr->nAlloc;
    uint8_t nHasPool = pArr->nHasPool;

    xfree(pPool, pArr->pData);
    pArr->pData = NULL;
    pArr->pPool = NULL;
    pArr->nSize = 0;
    pArr->nFixed = 0;
    pArr->nAlloc = 0;
    pArr->nHasPool = 0;

    if (nAlloc) xfreen(pPool, pArr, sizeof(xarray_t));
    if (nHasPool) XPool_Destroy(pPool);
}

void XArray_Free(xarray_t **ppArr)
{
    if (ppArr == NULL ||
        *ppArr == NULL) return;

    xarray_t *pArr = *ppArr;
    XArray_Destroy(pArr);
    *ppArr = NULL;
}

uint8_t XArray_Contains(xarray_t *pArr, size_t nIndex)
{
    if (pArr == NULL || nIndex >= pArr->nSize) return 0;
    return pArr->pData[nIndex] != NULL ? 1 : 0;
}

size_t XArray_Realloc(xarray_t *pArr)
{
    if (pArr == NULL) return 0;
    if (pArr->nFixed) return pArr->nSize;

    xpool_t *pPool = pArr->pPool;
    size_t nSize = 0, nUsed = pArr->nUsed;
    float fQuotient = (float)nUsed / (float)pArr->nSize;

    if (nUsed && nUsed == pArr->nSize) nSize = pArr->nSize * 2;
    else if (nUsed && fQuotient < 0.25) nSize = pArr->nSize / 2;

    if (nSize)
    {
        xarray_data_t **pData = (xarray_data_t**)xalloc(pPool, sizeof(xarray_data_t*) * nSize);
        if (pData == NULL)
        {
            pArr->eStatus = XARRAY_STATUS_NO_MEMORY;
            return 0;
        }

        if (pArr->pData != NULL && pArr->nUsed < nSize)
        {
            memcpy(pData, pArr->pData, sizeof(xarray_data_t*) * pArr->nUsed);
            xfreen(pPool, pArr->pData, sizeof(xarray_data_t*) * pArr->nSize);
        }

        pArr->pData = pData;
        pArr->nSize = nSize;

        size_t i;
        for (i = nUsed; i < nSize; i++) 
            pArr->pData[i] = NULL;
    }

    return pArr->nSize;
}

size_t XArray_CheckSpace(xarray_t *pArr)
{
    if (pArr == NULL) return 0;

    if (pArr->pData == NULL)
    {
        uint8_t nAlloc = pArr->nAlloc;
        uint8_t nHasPool = pArr->nHasPool;
        xarray_clear_cb_t clearCb = pArr->clearCb;

        XArray_Init(pArr, pArr->pPool, XARRAY_INITIAL_SIZE, 0);
        pArr->nHasPool = nHasPool;
        pArr->clearCb = clearCb;
        pArr->nAlloc = nAlloc;
    }
    else if (pArr->nUsed >= pArr->nSize)
    {
        return XArray_Realloc(pArr);
    }

    return pArr->pData == NULL ? 0 : 1;
}

int XArray_Add(xarray_t *pArr, xarray_data_t *pNewData)
{
    if (!XArray_CheckSpace(pArr))
    {
        XArray_ClearData(pArr, pNewData);
        return XARRAY_FAILURE;
    }

    pArr->pData[pArr->nUsed++] = pNewData;
    return (int)pArr->nUsed - 1;
}

int XArray_AddData(xarray_t *pArr, void *pData, size_t nSize)
{
    if (pArr == NULL) return XARRAY_FAILURE;
    xarray_data_t *pNewData = XArray_NewData(pArr, pData, nSize, 0);

    if (pNewData == NULL) 
    {
        pArr->eStatus = XARRAY_STATUS_NO_MEMORY;
        return XARRAY_FAILURE;
    }

    return XArray_Add(pArr, pNewData);
}

int XArray_PushData(xarray_t *pArr, void *pData, size_t nSize)
{
    xarray_data_t *pNewData = XArray_NewData(pArr, pData, 0, 0);
    if (pNewData == NULL) 
    {
        pArr->eStatus = XARRAY_STATUS_NO_MEMORY;
        return XARRAY_FAILURE;
    }

    pNewData->nSize = nSize;
    return XArray_Add(pArr, pNewData);
}

int XArray_AddDataKey(xarray_t *pArr, void *pData, size_t nSize, uint32_t nKey)
{
    xarray_data_t *pNewData = XArray_NewData(pArr, pData, nSize, nKey);

    if (pNewData == NULL)
    {
        pArr->eStatus = XARRAY_STATUS_NO_MEMORY;
        return XARRAY_FAILURE;
    }

    return XArray_Add(pArr, pNewData);
}

xarray_data_t* XArray_Get(xarray_t *pArr, size_t nIndex)
{
    if (nIndex >= pArr->nSize) return NULL;
    return pArr->pData[nIndex];
}

void* XArray_GetData(xarray_t *pArr, size_t nIndex)
{
    if (nIndex >= pArr->nSize) return NULL;
    xarray_data_t *pArrData = pArr->pData[nIndex];
    return pArrData ? pArrData->pData : NULL;
}

void* XArray_GetDataOr(xarray_t *pArr, size_t nIndex, void *pRet)
{
    if (nIndex >= pArr->nSize) return pRet;
    xarray_data_t *pArrData = pArr->pData[nIndex];
    return pArrData ? pArrData->pData : pRet;
}

size_t XArray_GetSize(xarray_t *pArr, size_t nIndex)
{
    if (nIndex >= pArr->nSize) return 0;
    xarray_data_t *pArrData = pArr->pData[nIndex];
    return pArrData ? pArrData->nSize : 0;
}

uint32_t XArray_GetKey(xarray_t *pArr, size_t nIndex)
{
    if (nIndex >= pArr->nSize) return 0;
    xarray_data_t *pArrData = pArr->pData[nIndex];
    return pArrData ? pArrData->nKey : 0;
}

xarray_data_t* XArray_Remove(xarray_t *pArr, size_t nIndex)
{
    xarray_data_t *pData = XArray_Get(pArr, nIndex);
    if (pData == NULL) return NULL;

    size_t i;
    for (i = nIndex; i < pArr->nUsed; i++)
    {
        if ((i + 1) >= pArr->nUsed) break;
        pArr->pData[i] = pArr->pData[i+1];
    }

    pArr->pData[--pArr->nUsed] = NULL;
    XArray_Realloc(pArr);
    return pData;
}

void XArray_Delete(xarray_t *pArr, size_t nIndex)
{
    xarray_data_t *pData = XArray_Get(pArr, nIndex);
    if (pData != NULL) XArray_ClearData(pArr, pData);

    size_t i;
    for (i = nIndex; i < pArr->nUsed; i++)
    {
        if ((i + 1) >= pArr->nUsed) break;
        pArr->pData[i] = pArr->pData[i+1];
    }

    pArr->pData[--pArr->nUsed] = NULL;
    XArray_Realloc(pArr);
}

xarray_data_t* XArray_Set(xarray_t *pArr, size_t nIndex, xarray_data_t *pNewData)
{
    xarray_data_t *pOldData = NULL;
    if (nIndex < pArr->nSize)
    {
        pOldData = pArr->pData[nIndex];
        pArr->pData[nIndex] = pNewData;
    }

    return pOldData;
}

xarray_data_t* XArray_SetData(xarray_t *pArr, size_t nIndex, void *pData, size_t nSize)
{
    xarray_data_t *pNewData = XArray_NewData(pArr, pData, nSize, 0);
    if (pNewData == NULL)
    {
        pArr->eStatus = XARRAY_STATUS_NO_MEMORY;
        return NULL;
    }

    xarray_data_t *pOldData = XArray_Set(pArr, nIndex, pNewData);
    return pOldData;
}

xarray_data_t* XArray_Insert(xarray_t *pArr, size_t nIndex, xarray_data_t *pData)
{
    if (!XArray_CheckSpace(pArr)) return NULL;

    xarray_data_t *pOldData = XArray_Set(pArr, nIndex, pData);
    if (pOldData == NULL) return NULL;

    size_t i, nNextIndex = nIndex + 1;

    for (i = nNextIndex; i < pArr->nUsed; i++)
        pOldData = XArray_Set(pArr, i, pOldData);
    
    XArray_Add(pArr, pOldData);
    return pArr->pData[nNextIndex];
}

xarray_data_t* XArray_InsertData(xarray_t *pArr, size_t nIndex, void *pData, size_t nSize)
{
    if (!XArray_CheckSpace(pArr)) return NULL;

    xarray_data_t *pNewData = XArray_NewData(pArr, pData, nSize, 0);
    if (pNewData == NULL)
    {
        pArr->eStatus = XARRAY_STATUS_NO_MEMORY;
        return NULL;
    }

    return XArray_Insert(pArr, nIndex, pNewData);
}

void XArray_Swap(xarray_t *pArr, size_t nIndex1, size_t nIndex2)
{
    if (nIndex1 >= pArr->nUsed ||
        nIndex2 >= pArr->nUsed) return;

    xarray_data_t *pData1 = pArr->pData[nIndex1];
    pArr->pData[nIndex1] = pArr->pData[nIndex2];
    pArr->pData[nIndex2] = pData1;
}

static int XArray_CompareSize(const void *pData1, const void *pData2, void *pCtx)
{
    (void)pCtx;
    xarray_data_t *pFirst = (xarray_data_t*)pData1;
    xarray_data_t *pSecond = (xarray_data_t*)pData2;
    return (int)pFirst->nSize - (int)pSecond->nSize;
}

static int XArray_CompareKey(const void *pData1, const void *pData2, void *pCtx)
{
    (void)pCtx;
    xarray_data_t *pFirst = (xarray_data_t*)pData1;
    xarray_data_t *pSecond = (xarray_data_t*)pData2;
    return (int)pFirst->nKey - (int)pSecond->nKey;
}

int XArray_Partitioning(xarray_t *pArr, xarray_comparator_t compare, void *pCtx, int nStart, int nFinish)
{
    int nPivot = nStart;
    while(1)
    {
        while (compare((void*)pArr->pData[nStart], (void*)pArr->pData[nPivot], pCtx) < 0) nStart++;
        while (compare((void*)pArr->pData[nFinish], (void*)pArr->pData[nPivot], pCtx) > 0) nFinish--;
        if (!compare((void*)pArr->pData[nStart], (void*)pArr->pData[nFinish], pCtx)) nFinish--;
        if (nStart >= nFinish) return nStart;
        XArray_Swap(pArr, nStart, nFinish);
    }

    return nStart;
}

void XArray_QuickSort(xarray_t *pArr, xarray_comparator_t compare, void *pCtx, int nStart, int nFinish)
{
    if (nStart < nFinish)
    {
        int nPartitioning = XArray_Partitioning(pArr, compare, pCtx, nStart, nFinish);
        XArray_QuickSort(pArr, compare, pCtx, nStart, nPartitioning);
        XArray_QuickSort(pArr, compare, pCtx, nPartitioning+1, nFinish);
    }
}

void XArray_Sort(xarray_t *pArr, xarray_comparator_t compare, void *pCtx)
{
    if (pArr == NULL || !pArr->nUsed) return;
    XArray_QuickSort(pArr, compare, pCtx, 0, (int)pArr->nUsed-1);
}

void XArray_SortBy(xarray_t *pArr, int nSortBy)
{
    if (nSortBy == XARRAY_SORTBY_SIZE) 
        XArray_Sort(pArr, XArray_CompareSize, NULL);
    else if (nSortBy == XARRAY_SORTBY_KEY)
        XArray_Sort(pArr, XArray_CompareKey, NULL);
}

void XArray_BubbleSort(xarray_t *pArr, xarray_comparator_t compare, void *pCtx)
{
    if (pArr == NULL || !pArr->nUsed) return;
    size_t i, j;

    for (i = 0; i < pArr->nUsed-1; i++) 
    {
        for (j = 0 ; j < pArr->nUsed-i-1; j++) 
        {
            if (compare((void*)pArr->pData[j], (void*)pArr->pData[j+1], pCtx))
            {
                xarray_data_t *pData = pArr->pData[j];
                pArr->pData[j] = pArr->pData[j+1];
                pArr->pData[j+1] = pData;
            }
        }
    }
}

int XArray_LinearSearch(xarray_t *pArr, uint32_t nKey)
{
    if (pArr == NULL || !pArr->nUsed) return XARRAY_FAILURE;
    size_t i = 0;

    for (i = 0; i < pArr->nUsed; i++)
    {
        xarray_data_t *pData = pArr->pData[i];
        if (nKey == pData->nKey) return (int)i;
    }

    return XARRAY_FAILURE;
}

int XArray_SentinelSearch(xarray_t *pArr, uint32_t nKey)
{
    if (pArr == NULL || !pArr->nUsed) return XARRAY_FAILURE;
    int i, nRet = 0, nLast = (int)pArr->nUsed - 1;

    xarray_data_t *pLast = pArr->pData[nLast];
    if (pLast->nKey == nKey) return nLast;

    xarray_data_t term = {.nKey = nKey, .nSize = 0, .pData = NULL};
    pArr->pData[nLast] = &term;

    for (i = 0;; i++)
    {
        xarray_data_t *pData = pArr->pData[i];
        if (nKey == pData->nKey)
        {
            pArr->pData[nLast] = pLast;
            nRet = (i < nLast) ? i : -1;
            break;
        }
    }

    return nRet;
}

int XArray_DoubleSearch(xarray_t *pArr, uint32_t nKey)
{
    if (pArr == NULL || !pArr->nUsed) return XARRAY_FAILURE;
    int nFront = 0, nBack = (int)pArr->nUsed - 1;

    while (nFront <= nBack)
    {
        xarray_data_t *pData = pArr->pData[nFront];
        if (nKey == pData->nKey) return nFront;

        pData = pArr->pData[nBack];
        if (nKey == pData->nKey) return nBack;
    
        nFront++;
        nBack--;
    }

    return XARRAY_FAILURE;
}

int XArray_BinarySearch(xarray_t *pArr, uint32_t nKey)
{
    if (pArr == NULL || !pArr->nUsed) return XARRAY_FAILURE;
    int nLeft = 0, nRight = (int)pArr->nUsed - 1;

    xarray_data_t *pData = pArr->pData[nLeft];
    if (pData->nKey == nKey) return nLeft;

    while (nLeft <= nRight)
    {
        int nMiddle = nLeft + (nRight - nLeft) / 2;
        pData = pArr->pData[nMiddle];

        if (pData->nKey < nKey) nLeft = nMiddle + 1;
        else if (pData->nKey == nKey) return nMiddle;
        else nRight = nMiddle - 1;
    }

    return XARRAY_FAILURE;
}

size_t XArray_Used(xarray_t *pArr)
{
    if (pArr == NULL) return 0;
    return pArr->nUsed;
}

size_t XArray_Size(xarray_t *pArr)
{
    if (pArr == NULL) return 0;
    return pArr->nSize;
}
