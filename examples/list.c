/*
 *  examples/list.c
 * 
 *  Copyleft (C) 2020  Sun Dro (a.k.a. kala13x)
 *
 * Example file for working with linked list.
 */

#include "list.h"
#include "log.h"

typedef struct {
    char *pName;
    int nNumber;
} UserData;

void clearCallback(void *pUser, void *pData)
{
    xlogd("clearing: %s", (char*)pData);
    free(pData);
}

int searchCallback(void *pUserPtr, xlist_t *pNode)
{
    char *pString = (char*)pNode->data.pData;
    char *pSearch = (char*)pUserPtr;
    return !strncmp(pSearch, pString, strlen(pSearch));
}

int costumOpreation(void *pUserPtr, xlist_t *pNode)
{
    UserData *pUsrData = (UserData*)pUserPtr;
    char *pString = (char*)pNode->data.pData;

    if (!strncmp(pUsrData->pName, pString, strlen(pUsrData->pName)))
    {
        xlogd("renaming: %s -> %d", pString, pUsrData->nNumber);
        snprintf(pString, pNode->data.nSize, "%d", pUsrData->nNumber);
        return -1; /* Stop search*/
    }

    return 0;
}

void displayAllNodes(xlist_t *pNode)
{
    xlist_t *pHead = XList_GetHead(pNode);
    while (pHead != NULL)
    {
        xlogi("node: %s", (const char*)pHead->data.pData);
        pHead = pHead->pNext;
    }
}

int main(int argc, char *argv[])
{
    xlog_defaults();
    xlog_timing(XLOG_TIME);
    xlog_enable(XLOG_DEBUG | XLOG_INFO);
    xlog_separator("|");
    xlog_indent(1);

    char *pFirst = strdup("first node");
    char *pSecond = strdup("second node");
    char *pThird = strdup("third node");
    char *pFourth = strdup("fourth node");
    char *pFifth = strdup("fifth node");

    /* Create linked list and append new nodes */
    xlist_t *pNode = XList_New(pSecond, strlen(pSecond), clearCallback, NULL);
    pNode = XList_PushBack(pNode, pThird, strlen(pThird));
    pNode = XList_PushBack(pNode, pFourth, strlen(pFourth));
    pNode = XList_PushBack(pNode, pFifth, strlen(pFifth));
    XList_PushFront(pNode, pFirst, strlen(pFirst));

    /* Search third node */
    xlist_t *pFound = XList_Search(pNode, (void*)"third", searchCallback);
    if (pFound != NULL) xlogd("found node: %s", (const char*)pFound->data.pData);

    /* Iterate while list and print data */
    displayAllNodes(pNode);

    /* Search and remove fourth node */
    XList_Remove(pNode, (void*)"fourth", searchCallback);

    /* Search and rename nodes */
    UserData userData;
    userData.pName = "first";
    userData.nNumber = 1;
    XList_Search(pNode, &userData, costumOpreation);
    userData.pName = "second";
    userData.nNumber = 2;
    XList_Search(pNode, &userData, costumOpreation);
    userData.pName = "third";
    userData.nNumber = 3;
    XList_Search(pNode, &userData, costumOpreation);
    userData.pName = "fifth";
    userData.nNumber = 5;
    XList_Search(pNode, &userData, costumOpreation);

    /* Iterate while list and print data */
    displayAllNodes(pNode);

    /* Free all allocated data */
    XList_Clear(pNode);
    return 0;
}