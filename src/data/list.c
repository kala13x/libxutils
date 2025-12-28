/*!
 *  @file libxutils/src/data/list.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of linked list
 */

#include "list.h"

void XListData_Init(xlist_data_t *pListData, void *pData, size_t nSize, xlist_cb_t onClear, void *pCbCtx)
{
    pListData->pData = pData;
    pListData->nSize = nSize;
    pListData->onClear = onClear;
    pListData->pClearCtx = pCbCtx;
}

void XListData_MergeCtx(xlist_data_t *pNext, xlist_data_t *pPrev)
{
    if (pNext != NULL && pPrev != NULL)
    {
        if (pNext->pClearCtx == NULL)
            pNext->pClearCtx = pPrev->pClearCtx;

        if (pNext->onClear == NULL)
            pNext->onClear = pPrev->onClear;
    }
}

void XList_Init(xlist_t *pList, void *pData, size_t nSize, xlist_cb_t onClear, void *pCtx)
{
    XListData_Init(&pList->data, pData, nSize, onClear, pCtx);
    pList->pNext = NULL;
    pList->pPrev = NULL;
    pList->nIsAlloc = 0;
}

xlist_t *XList_New(void *pData, size_t nSize, xlist_cb_t onClear, void *pCtx)
{
    xlist_t *pList = (xlist_t*)malloc(sizeof(xlist_t));
    if (pList == NULL) return NULL;

    XList_Init(pList, pData, nSize, onClear, pCtx);
    pList->nIsAlloc = 1;
    return pList;
}

void XList_Free(xlist_t *pList)
{
    if (pList != NULL)
    {
        xlist_data_t *pListData = &pList->data;
        if (pListData->pData != NULL)
        {
            if (pListData->onClear != NULL)
            {
                pListData->onClear(pListData->pClearCtx, pListData->pData);
                pListData->pData = NULL;
            }
            else if (pListData->nSize > 0)
            {
                free(pListData->pData);
                pListData->pData = NULL;
            }
        }

        if (pList->nIsAlloc) free(pList);
        else XList_Init(pList, NULL, 0, NULL, NULL);
    }
}

void XList_Detach(xlist_t *pList)
{
    if (pList == NULL) return;
    xlist_t *pPrev = pList->pPrev;
    xlist_t *pNext = pList->pNext;

    if (pPrev != NULL) pPrev->pNext = pNext;
    if (pNext != NULL) pNext->pPrev = pPrev;

    pList->pPrev = NULL;
    pList->pNext = NULL;
}

xlist_t* XList_Unlink(xlist_t *pList)
{
    if (pList == NULL) return NULL;
    xlist_t *pPrev = pList->pPrev;
    xlist_t *pNext = pList->pNext;

    XList_Detach(pList);
    XList_Free(pList);

    return pNext ? pNext : pPrev;
}

xlist_t* XList_RemoveHead(xlist_t *pList)
{
    xlist_t *pHead = XList_GetHead(pList);
    return XList_Unlink(pHead);
}

xlist_t* XList_RemoveTail(xlist_t *pList)
{
    xlist_t *pTail = XList_GetTail(pList);
    return XList_Unlink(pTail);
}

xlist_t* XList_GetHead(xlist_t *pList)
{
    if (pList == NULL) return NULL;

    while (pList->pPrev != NULL)
        pList = pList->pPrev;

    return pList;
}

xlist_t* XList_GetTail(xlist_t *pList)
{
    if (pList == NULL) return NULL;

    while (pList->pNext != NULL)
        pList = pList->pNext;

    return pList;
}

xlist_t* XList_MakeRing(xlist_t *pList)
{
    if (pList == NULL) return NULL;
    xlist_t *pHead = XList_GetHead(pList);
    xlist_t *pTail = XList_GetTail(pList);
    pHead->pPrev = pTail;
    pTail->pNext = pHead;
    return pHead;
}

uint8_t XList_IsRing(xlist_t *pList)
{
    if (pList == NULL) return 0;
    xlist_t *pNode = pList->pNext;

    while (pNode != NULL &&
           pNode != pList)
            pNode = pNode->pNext;

    return (pNode == pList);
}

void XList_Clear(xlist_t *pList)
{
    while (pList != NULL)
        pList = XList_Unlink(pList);
}

xlist_t* XList_InsertPrev(xlist_t *pList, xlist_t *pNode)
{
    if (pList == NULL || pNode == NULL) return NULL;
    XListData_MergeCtx(&pNode->data, &pList->data);

    xlist_t *pPrev = pList->pPrev;
    pList->pPrev = pNode;
    pNode->pPrev = pPrev;
    pNode->pNext = pList;

    if (pPrev != NULL)
    {
        pPrev->pNext = pNode;
        return pPrev;
    }

    return pNode;
}

xlist_t* XList_InsertNext(xlist_t *pList, xlist_t *pNode)
{
    if (pList == NULL || pNode == NULL) return NULL;
    XListData_MergeCtx(&pNode->data, &pList->data);

    xlist_t *pNext = pList->pNext;
    pList->pNext = pNode;
    pNode->pNext = pNext;
    pNode->pPrev = pList;

    if (pNext != NULL)
    {
        pNext->pPrev = pNode;
        return pNext;
    }

    return pNode;
}

xlist_t* XList_InsertHead(xlist_t *pList, xlist_t *pNode)
{
    if (pList == NULL || pNode == NULL) return NULL;

    xlist_t *pHead = XList_GetHead(pList);
    XListData_MergeCtx(&pNode->data, &pHead->data);

    pHead->pPrev = pNode;
    pNode->pNext = pHead;
    pNode->pPrev = NULL;
    return pNode;
}

xlist_t* XList_InsertTail(xlist_t *pList, xlist_t *pNode)
{
    if (pList == NULL || pNode == NULL) return NULL;

    xlist_t *pTail = XList_GetTail(pList);
    XListData_MergeCtx(&pNode->data, &pTail->data);

    pTail->pNext = pNode;
    pNode->pPrev = pTail;
    pNode->pNext = NULL;
    return pNode;
}

xlist_t* XList_PushPrev(xlist_t *pList, void *pData, size_t nSize)
{
    if (pList == NULL) return NULL;
    xlist_t *pNode = XList_New(pData, nSize, pList->data.onClear, pList->data.pClearCtx);
    return pNode != NULL ? XList_InsertPrev(pList, pNode) : NULL;
}

xlist_t* XList_PushNext(xlist_t *pList, void *pData, size_t nSize)
{
    if (pList == NULL) return NULL;
    xlist_t *pNode = XList_New(pData, nSize, pList->data.onClear, pList->data.pClearCtx);
    return pNode != NULL ? XList_InsertNext(pList, pNode) : NULL;
}

xlist_t* XList_PushFront(xlist_t *pList, void *pData, size_t nSize)
{
    if (pList == NULL) return NULL;
    xlist_t *pNode = XList_New(pData, nSize, pList->data.onClear, pList->data.pClearCtx);
    return pNode != NULL ? XList_InsertHead(pList, pNode) : NULL;
}

xlist_t* XList_PushBack(xlist_t *pList, void *pData, size_t nSize)
{
    if (pList == NULL) return NULL;
    xlist_t *pNode = XList_New(pData, nSize, pList->data.onClear, pList->data.pClearCtx);
    return pNode != NULL ? XList_InsertTail(pList, pNode) : NULL;
}

xlist_t* XList_Search(xlist_t *pList, void *pUserPtr, xlist_cmp_t compare)
{
    if (pList == NULL || compare == NULL) return NULL;
    xlist_t *pHead = XList_GetHead(pList);

    while (pHead != NULL)
    {
        int nRet = compare(pUserPtr, pHead);
        if (nRet > 0) return pHead;
        else if (nRet < 0) break;
        pHead = pHead->pNext;
    }

    return NULL;
}

xlist_t* XList_Remove(xlist_t *pList, void *pUserPtr, xlist_cmp_t compare)
{
    xlist_t *pNode = XList_Search(pList, pUserPtr, compare);
    return (pNode != NULL) ? XList_Unlink(pNode) : NULL;
}
