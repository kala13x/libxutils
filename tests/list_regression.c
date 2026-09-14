/* libxutils: bidirectional links, detached ownership and ring cleanup. */
#include "test.h"
#include "list.h"

static void XTest_Clear(void *pContext, void *pData)
{
    (void)pData;
    (*(int*)pContext)++;
}

static int XTest_Find(void *pContext, xlist_t *pNode)
{
    return pNode->data.pData == pContext;
}

static int XTest_links(void)
{
    int values[4] = {0, 1, 2, 3}, nCleared = 0;
    xlist_t *pHead = XList_New(&values[0], 0, XTest_Clear, &nCleared);
    CHECK(pHead != NULL, "Allocate list head");
    for (int i = 1; i < 4; i++) CHECK(XList_PushBack(pHead, &values[i], 0) != NULL, "Append node");
    xlist_t *pTail = XList_GetTail(pHead), *pNode = pHead;
    for (int i = 0; i < 4; i++, pNode = pNode->pNext)
    {
        CHECK(pNode && pNode->data.pData == &values[i], "Forward traversal retains order");
        CHECK(!i || pNode->pPrev->pNext == pNode, "Backward link agrees with forward link");
    }
    CHECK(pNode == NULL && XList_GetHead(pTail) == pHead, "Locate both ends from either direction");
    pNode = XList_Search(pTail, &values[2], XTest_Find);
    CHECK(pNode != NULL, "Search from the tail visits the whole list");
    XList_Detach(pNode);
    CHECK(!pNode->pNext && !pNode->pPrev && nCleared == 0, "Detach transfers ownership without cleanup");
    XList_Free(pNode);
    CHECK(nCleared == 1 && pHead->pNext->pNext == pTail, "Detached node cleanup leaves neighbors linked");
    pHead = XList_RemoveHead(pHead);
    CHECK(nCleared == 2 && pHead->data.pData == &values[1], "Remove head returns the surviving list");
    XList_Clear(pHead);
    CHECK(nCleared == 4, "Clear visits each remaining node once");
    return 0;
}

static int XTest_rings(void)
{
    for (int nCount = 1; nCount <= 8; nCount++)
    {
        int values[8] = {0}, nCleared = 0;
        xlist_t *pHead = XList_New(&values[0], 0, XTest_Clear, &nCleared);
        CHECK(pHead != NULL, "Allocate ring head");
        for (int i = 1; i < nCount; i++) CHECK(XList_PushBack(pHead, &values[i], 0) != NULL, "Append ring node");
        CHECK(!XList_IsRing(pHead), "A linear list is not a ring");
        CHECK(XList_MakeRing(pHead) == pHead && XList_IsRing(pHead), "Join both ends into a ring");
        XList_Clear(pHead);
        CHECK(nCleared == nCount, "Ring cleanup must terminate and free each node exactly once");
    }
    return 0;
}

static int XTest_stack_head(void)
{
    int value = 1, nCleared = 0;
    xlist_t head;
    XList_Init(&head, &value, 0, XTest_Clear, &nCleared);
    CHECK(XList_PushNext(&head, &value, 0) != NULL, "Mix a stack head with heap nodes");
    XList_Clear(&head);
    CHECK(nCleared == 2 && !head.pNext && !head.pPrev && !head.data.pData, "Clear resets but never frees a stack head");
    XList_Clear(&head);
    CHECK(nCleared == 2, "Repeated clear is harmless");
    return 0;
}

XTEST_MAIN(XTEST_CASE(links), XTEST_CASE(rings), XTEST_CASE(stack_head))
