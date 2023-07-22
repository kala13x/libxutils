/*!
 *  @file libxutils/src/data/list.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of linked list
 */

#include "list.h"

void XList_Init(xlist_t *pList, void *pData, size_t nSize, xlist_cb_t clearCb, void *pCtx)
{
    pList->clearCb = clearCb;
    pList->pCbCtx = pCtx;
    pList->pData = pData;
    pList->nSize = nSize;
    pList->pNext = NULL;
    pList->pPrev = NULL;
    pList->nAlloc = 0;
    pList->nID = 0;
}

xlist_t *XList_New(void *pData, size_t nSize, xlist_cb_t clearCb, void *pCtx)
{
    xlist_t *pList = (xlist_t*)malloc(sizeof(xlist_t));
    if (pList == NULL) return NULL;

    XList_Init(pList, pData, nSize, clearCb, pCtx);
    pList->nAlloc = 1;
    return pList;
}

void XList_Free(xlist_t *pList)
{
    if (pList != NULL)
    {
        if (pList->pData != NULL)
        {
            if (pList->clearCb != NULL)
                pList->clearCb(pList->pCbCtx, pList->pData);
            else if (pList->nSize > 0) 
                free(pList->pData);
        }

        if (pList->nAlloc) free(pList);
        else XList_Init(pList, NULL, 0, NULL, NULL);
    }
}

xlist_t* XList_Unlink(xlist_t *pList)
{
    if (pList == NULL) return NULL;
    xlist_t *pPrev = pList->pPrev;
    xlist_t *pNext = pList->pNext;

    if (pPrev != NULL) pPrev->pNext = pNext;
    if (pNext != NULL) pNext->pPrev = pPrev;

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

    while (pNode != NULL && pNode != pList)
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

    if (pNode->pCbCtx == NULL)
        pNode->pCbCtx = pList->pCbCtx;

    if (pNode->clearCb == NULL)
        pNode->clearCb = pList->clearCb;

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

    if (pNode->pCbCtx == NULL)
        pNode->pCbCtx = pList->pCbCtx;

    if (pNode->clearCb == NULL)
        pNode->clearCb = pList->clearCb;

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

    if (pNode->pCbCtx == NULL)
        pNode->pCbCtx = pHead->pCbCtx;

    if (pNode->clearCb == NULL)
        pNode->clearCb = pHead->clearCb;

    pHead->pPrev = pNode;
    pNode->pNext = pHead;
    pNode->pPrev = NULL;
    return pNode;
}

xlist_t* XList_InsertTail(xlist_t *pList, xlist_t *pNode)
{
    if (pList == NULL || pNode == NULL) return NULL;
    xlist_t *pTail = XList_GetTail(pList);

    if (pNode->pCbCtx == NULL)
        pNode->pCbCtx = pTail->pCbCtx;

    if (pNode->clearCb == NULL)
        pNode->clearCb = pTail->clearCb;

    pTail->pNext = pNode;
    pNode->pPrev = pTail;
    pNode->pNext = NULL;
    return pNode;
}

xlist_t* XList_InsertSorted(xlist_t *pList, xlist_t *pNode)
{
    if (pList == NULL || pNode == NULL) return NULL;

    while (pNode->nID < pList->nID)
    {
        if (pList->pPrev == NULL) break;
        xlist_t *pPrev = pList->pPrev;

        if (pNode->nID > pPrev->nID) break;
        pList = pPrev;
    }

    while (pNode->nID > pList->nID)
    {
        if (pList->pNext == NULL) break;
        xlist_t *pNext = pList->pNext;

        if (pNode->nID < pNext->nID) break;
        pList = pNext;
    }

    return !pList->nID || pNode->nID > pList->nID ?
        XList_InsertNext(pList, pNode):
        XList_InsertPrev(pList, pNode);
}

xlist_t* XList_PushPrev(xlist_t *pList, void *pData, size_t nSize)
{
    if (pList == NULL) return NULL;
    xlist_t *pNode = XList_New(pData, nSize, pList->clearCb, pList->pCbCtx);
    return pNode != NULL ? XList_InsertPrev(pList, pNode) : NULL;
}

xlist_t* XList_PushNext(xlist_t *pList, void *pData, size_t nSize)
{
    if (pList == NULL) return NULL;
    xlist_t *pNode = XList_New(pData, nSize, pList->clearCb, pList->pCbCtx);
    return pNode != NULL ? XList_InsertNext(pList, pNode) : NULL;
}

xlist_t* XList_PushFront(xlist_t *pList, void *pData, size_t nSize)
{
    if (pList == NULL) return NULL;
    xlist_t *pNode = XList_New(pData, nSize, pList->clearCb, pList->pCbCtx);
    return pNode != NULL ? XList_InsertHead(pList, pNode) : NULL;
}

xlist_t* XList_PushBack(xlist_t *pList, void *pData, size_t nSize)
{
    if (pList == NULL) return NULL;
    xlist_t *pNode = XList_New(pData, nSize, pList->clearCb, pList->pCbCtx);
    return pNode != NULL ? XList_InsertTail(pList, pNode) : NULL;
}

xlist_t* XList_PushSorted(xlist_t *pList, void *pData, size_t nSize, uint32_t nID)
{
    if (pList == NULL) return NULL;

    xlist_t *pNode = XList_New(
        pData, nSize,
        pList->clearCb,
        pList->pCbCtx
    );

    if (pNode == NULL) return NULL;
    pNode->nID = nID;

    return XList_InsertSorted(pList, pNode);
}

xlist_t* XList_Search(xlist_t *pList, void *pUserPtr, xlist_comparator_t compare)
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

xlist_t* XList_Remove(xlist_t *pList, void *pUserPtr, xlist_comparator_t compare)
{
    xlist_t *pNode = XList_Search(pList, pUserPtr, compare);
    return (pNode != NULL) ? XList_Unlink(pNode) : NULL;
}
