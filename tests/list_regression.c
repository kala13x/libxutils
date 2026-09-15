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


/* Matches a node whose payload pointer is the one being looked for. */
static int XTest_MatchPtr(void *pUserPtr, xlist_t *pNode)
{
    return pNode->data.pData == pUserPtr;
}

static int XTest_push(void)
{
    /* Pushing to the front and the back builds the list in both
     * directions from one anchor node. */
    int values[5] = {0, 1, 2, 3, 4};
    xlist_t *pAnchor = XList_New(&values[2], 0, NULL, NULL);
    CHECK(pAnchor != NULL, "The anchor node is created");

    CHECK(XList_PushPrev(pAnchor, &values[1], 0) != NULL, "A node is pushed before the anchor");
    CHECK(XList_PushNext(pAnchor, &values[3], 0) != NULL, "A node is pushed after the anchor");

    xlist_t *pHead = XList_GetHead(pAnchor);
    xlist_t *pTail = XList_GetTail(pAnchor);
    CHECK(pHead != NULL && pHead->data.pData == &values[1], "The head is the node pushed before");
    CHECK(pTail != NULL && pTail->data.pData == &values[3], "The tail is the node pushed after");

    CHECK(XList_PushFront(pAnchor, &values[0], 0) != NULL, "A node is pushed to the front");
    CHECK(XList_PushBack(pAnchor, &values[4], 0) != NULL, "A node is pushed to the back");

    pHead = XList_GetHead(pAnchor);
    pTail = XList_GetTail(pAnchor);
    CHECK(pHead->data.pData == &values[0], "The front push reached the head");
    CHECK(pTail->data.pData == &values[4], "The back push reached the tail");

    /* Walking forward sees every value in order. */
    int nSeen = 0;
    for (xlist_t *pNode = pHead; pNode != NULL; pNode = pNode->pNext)
    {
        CHECK(pNode->data.pData == &values[nSeen], "Each node holds the value pushed into its position");
        nSeen++;
    }
    CHECK(nSeen == 5, "Every pushed node is on the list");

    /* Walking backward sees the same values in reverse. */
    nSeen = 5;
    for (xlist_t *pNode = pTail; pNode != NULL; pNode = pNode->pPrev)
    {
        nSeen--;
        CHECK(pNode->data.pData == &values[nSeen], "The reverse walk sees the same order backwards");
    }
    CHECK(nSeen == 0, "The reverse walk reaches the head");

    XList_Clear(pHead);
    return 0;
}

static int XTest_removal(void)
{
    /* Unlink both detaches and frees the node, and returns a neighbour to
     * carry on from. RemoveHead, RemoveTail and Remove are all built on it,
     * so none of them hands the caller a node to free. */
    int values[4] = {10, 20, 30, 40};
    int nCleared = 0;

    xlist_t *pHead = XList_New(&values[0], sizeof(int), XTest_Clear, &nCleared);
    CHECK(pHead != NULL, "The list is created");
    for (int i = 1; i < 4; i++)
        CHECK(XList_PushBack(pHead, &values[i], sizeof(int)) != NULL, "A node is appended");

    CHECK(XList_GetHead(pHead)->data.pData == &values[0], "The head is the first node pushed");

    xlist_t *pRest = XList_RemoveHead(pHead);
    CHECK(pRest != NULL, "Removing the head leaves a list behind");
    CHECK(nCleared == 1, "The removed head's payload was released once");
    CHECK(XList_GetHead(pRest)->data.pData == &values[1], "The second node became the head");
    CHECK(XList_Search(XList_GetHead(pRest), &values[0], XTest_MatchPtr) == NULL,
        "The removed head is off the list");

    CHECK(XList_GetTail(pRest)->data.pData == &values[3], "The tail is the last node pushed");

    pRest = XList_RemoveTail(pRest);
    CHECK(pRest != NULL, "Removing the tail leaves a list behind");
    CHECK(nCleared == 2, "The removed tail's payload was released once");
    CHECK(XList_GetTail(XList_GetHead(pRest))->data.pData == &values[2], "The list ends one node earlier");

    XList_Clear(XList_GetHead(pRest));
    CHECK(nCleared == 4, "Clearing released every remaining payload exactly once");

    /* Unlinking a middle node closes the gap around it. */
    int trio[3] = {1, 2, 3};
    xlist_t *pFirst = XList_New(&trio[0], 0, NULL, NULL);
    CHECK(pFirst != NULL, "A three node list is created");
    for (int i = 1; i < 3; i++) CHECK(XList_PushBack(pFirst, &trio[i], 0) != NULL, "A node is appended");

    xlist_t *pMiddle = XList_Search(pFirst, &trio[1], XTest_MatchPtr);
    CHECK(pMiddle != NULL, "The middle node is found");

    xlist_t *pNeighbour = XList_Unlink(pMiddle);
    CHECK(pNeighbour != NULL, "Unlinking reports a neighbour to carry on from");

    xlist_t *pRemaining = XList_GetHead(pNeighbour);
    CHECK(XList_Search(pRemaining, &trio[1], XTest_MatchPtr) == NULL, "The unlinked node is off the list");
    CHECK(XList_Search(pRemaining, &trio[0], XTest_MatchPtr) != NULL, "The node before it stayed");
    CHECK(XList_Search(pRemaining, &trio[2], XTest_MatchPtr) != NULL, "The node after it stayed");

    /* Removing by predicate takes the matching node off the list. */
    CHECK(XList_Remove(pRemaining, &trio[2], XTest_MatchPtr) != NULL, "The predicate removal reports a neighbour");

    /* A predicate that matches nothing removes nothing. */
    int nAbsent = 999;
    pRemaining = XList_GetHead(pFirst);
    CHECK(XList_Remove(pRemaining, &nAbsent, XTest_MatchPtr) == NULL, "A predicate matching nothing removes nothing");
    CHECK(XList_Search(pRemaining, &nAbsent, XTest_MatchPtr) == NULL, "A predicate matching nothing finds nothing");

    XList_Clear(pRemaining);
    return 0;
}

static int XTest_single_node(void)
{
    /* A list of one is its own head and tail, and removing from it leaves
     * nothing to carry on from. */
    int value = 7;
    xlist_t *pOnly = XList_New(&value, 0, NULL, NULL);
    CHECK(pOnly != NULL, "A single node list is created");
    CHECK(XList_GetHead(pOnly) == pOnly, "The only node is the head");
    CHECK(XList_GetTail(pOnly) == pOnly, "The only node is the tail");
    CHECK(pOnly->pNext == NULL && pOnly->pPrev == NULL, "The only node has no neighbours");
    CHECK(XList_IsRing(pOnly) == 0, "A single node is not a ring by itself");

    CHECK(XList_Search(pOnly, &value, XTest_MatchPtr) == pOnly, "The only node is findable");
    CHECK(XList_Unlink(pOnly) == NULL, "Unlinking the only node reports no neighbour and frees it");

    /* Detaching leaves the node standing alone without freeing it. */
    int pair[2] = {1, 2};
    xlist_t *pFirst = XList_New(&pair[0], 0, NULL, NULL);
    CHECK(pFirst != NULL, "A two node list is created");
    xlist_t *pSecond = XList_PushBack(pFirst, &pair[1], 0);
    CHECK(pSecond != NULL, "The second node is appended");

    XList_Detach(pSecond);
    CHECK(pSecond->pNext == NULL && pSecond->pPrev == NULL, "The detached node has no neighbours");
    CHECK(pFirst->pNext == NULL, "The list it came from no longer points at it");
    XList_Free(pSecond);
    XList_Free(pFirst);
    return 0;
}

static int XTest_insert_nodes(void)
{
    /* The node-taking inserts place an existing node rather than making
     * one, so the caller keeps whatever payload it set up. */
    int values[4] = {0, 1, 2, 3};
    xlist_t *pAnchor = XList_New(&values[1], 0, NULL, NULL);
    CHECK(pAnchor != NULL, "The anchor node is created");

    xlist_t *pBefore = XList_New(&values[0], 0, NULL, NULL);
    xlist_t *pAfter = XList_New(&values[2], 0, NULL, NULL);
    CHECK(pBefore != NULL && pAfter != NULL, "The nodes to insert are created");

    CHECK(XList_InsertPrev(pAnchor, pBefore) == pBefore, "The node is inserted before the anchor");
    CHECK(XList_InsertNext(pAnchor, pAfter) == pAfter, "The node is inserted after the anchor");
    CHECK(pAnchor->pPrev == pBefore && pAnchor->pNext == pAfter, "The anchor sits between the two");

    xlist_t *pNewHead = XList_New(&values[3], 0, NULL, NULL);
    CHECK(pNewHead != NULL, "The head node is created");
    CHECK(XList_InsertHead(pAnchor, pNewHead) == pNewHead, "The node is inserted at the head");
    CHECK(XList_GetHead(pAnchor) == pNewHead, "The inserted node became the head");

    xlist_t *pNewTail = XList_New(&values[3], 0, NULL, NULL);
    CHECK(pNewTail != NULL, "The tail node is created");
    CHECK(XList_InsertTail(pAnchor, pNewTail) == pNewTail, "The node is inserted at the tail");
    CHECK(XList_GetTail(pAnchor) == pNewTail, "The inserted node became the tail");

    XList_Clear(XList_GetHead(pAnchor));
    return 0;
}

static int XTest_ring_walk(void)
{
    /* A ring has no end, so the walk has to stop when it comes back round
     * rather than on a null pointer. */
    int values[4] = {0, 1, 2, 3};
    xlist_t *pHead = XList_New(&values[0], 0, NULL, NULL);
    CHECK(pHead != NULL, "The list is created");
    for (int i = 1; i < 4; i++) CHECK(XList_PushBack(pHead, &values[i], 0) != NULL, "A node is appended");

    CHECK(XList_IsRing(pHead) == 0, "A plain list is not a ring");
    CHECK(XList_MakeRing(pHead) != NULL, "The list is closed into a ring");
    CHECK(XList_IsRing(pHead) != 0, "The closed list reports itself a ring");

    /* Every node in the ring is reachable from every other. */
    int nSteps = 0;
    xlist_t *pNode = pHead;
    do
    {
        CHECK(pNode != NULL, "A ring never walks off the end");
        CHECK(pNode->data.pData == &values[nSteps], "The ring keeps its insertion order");
        pNode = pNode->pNext;
        nSteps++;
    }
    while (pNode != pHead && nSteps < 10);
    CHECK(nSteps == 4, "The ring comes back round after exactly its length");

    /* A ring has neither a head nor a tail, so both walks have to stop when
     * they come back round rather than circling forever. */
    CHECK(XList_GetHead(pHead) == pHead, "The head walk terminates on a ring at its starting node");
    CHECK(XList_GetTail(pHead) == pHead, "The tail walk terminates on a ring at its starting node");

    xlist_t *pThird = pHead->pNext->pNext;
    CHECK(XList_GetHead(pThird) == pThird, "The head walk terminates from any node in the ring");
    CHECK(XList_GetTail(pThird) == pThird, "The tail walk terminates from any node in the ring");

    XList_Clear(pHead);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(links),
    XTEST_CASE(rings),
    XTEST_CASE(stack_head),
    XTEST_CASE(push),
    XTEST_CASE(removal),
    XTEST_CASE(single_node),
    XTEST_CASE(insert_nodes),
    XTEST_CASE(ring_walk)
)
