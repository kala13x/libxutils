/*!
 *  @file libxutils/src/data/list.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of linked list
 */

#ifndef __XUTILS_XLIST_H__
#define __XUTILS_XLIST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>

typedef struct xlist xlist_t;
typedef void(*xlist_cb_t)(void *pCbCtx, void *pData);
typedef int(*xlist_cmp_t)(void *pUserPtr, xlist_t *pNode);

typedef struct xlist_data {
    xlist_cb_t onClear;
    void *pClearCtx;
    void *pData;
    size_t nSize;
} xlist_data_t;

void XListData_Init(xlist_data_t *pListData, void *pData, size_t nSize, xlist_cb_t onClear, void *pCbCtx);
void XListData_MergeCtx(xlist_data_t *pNext, xlist_data_t *pPrev);

struct xlist {
    struct xlist *pNext;
    struct xlist *pPrev;
    xlist_data_t data;
    uint8_t nIsAlloc;
};

xlist_t *XList_New(void *pData, size_t nSize, xlist_cb_t onClear, void *pCtx);
void XList_Init(xlist_t *pList, void *pData, size_t nSize, xlist_cb_t onClear, void *pCtx);
void XList_Clear(xlist_t *pList);
void XList_Free(xlist_t *pList);
void XList_Detach(xlist_t *pList);

xlist_t* XList_Unlink(xlist_t *pList);
xlist_t* XList_RemoveHead(xlist_t *pList);
xlist_t* XList_RemoveTail(xlist_t *pList);

xlist_t* XList_GetHead(xlist_t *pList);
xlist_t* XList_GetTail(xlist_t *pList);

xlist_t* XList_InsertPrev(xlist_t *pList, xlist_t *pNode);
xlist_t* XList_InsertNext(xlist_t *pList, xlist_t *pNode);
xlist_t* XList_InsertHead(xlist_t *pList, xlist_t *pNode);
xlist_t* XList_InsertTail(xlist_t *pList, xlist_t *pNode);

xlist_t* XList_PushPrev(xlist_t *pList, void *pData, size_t nSize);
xlist_t* XList_PushNext(xlist_t *pList, void *pData, size_t nSize);
xlist_t* XList_PushFront(xlist_t *pList, void *pData, size_t nSize);
xlist_t* XList_PushBack(xlist_t *pList, void *pData, size_t nSize);

xlist_t* XList_Search(xlist_t *pList, void *pUserPtr, xlist_cmp_t compare);
xlist_t* XList_Remove(xlist_t *pList, void *pUserPtr, xlist_cmp_t compare);

xlist_t* XList_MakeRing(xlist_t *pList);
uint8_t XList_IsRing(xlist_t *pList);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XLIST_H__ */
