/*
 *  examples/array.c
 *
 *  Copyleft (C) 2018  Sun Dro (a.k.a. kala13x)
 *
 * Test example source of working with arrays
 */

#include <xutils/xstd.h>
#include <xutils/array.h>

typedef struct {
    int key;
    const char* str;
} TestStruct;

void PrintArrayInfo(xarray_t *pArr)
{
    int nArraySize = XArray_Size(pArr);
    int nUsedSize = XArray_Used(pArr);

    printf("Array Size(%d), Used Size(%d)\n", nArraySize, nUsedSize);
    printf("==================================\n\n");
}

void PrintEverything2(xarray_t *pArr)
{
    unsigned int i;
    for (i = 0; i < pArr->nUsed; i++)
    {
        TestStruct *pSt = (TestStruct *)XArray_GetData(pArr, i);
        printf("Element %u: %s k(%d)\n", i, pSt->str, pSt->key);
    }

    PrintArrayInfo(pArr);
}

void PrintEverything(xarray_t *pArr)
{
    unsigned int i;
    for (i = 0; i < pArr->nUsed; i++)
        printf("Element %u: %s\n", i, (char*)XArray_GetData(pArr, i));

    PrintArrayInfo(pArr);
}

int ComparatorStrings(const void *pData1, const void *pData2, void *pCtx)
{
    xarray_data_t *pFirst = (xarray_data_t*)pData1;
    xarray_data_t *pSecond = (xarray_data_t*)pData2;
    return (strcmp((char*)pFirst->pData, (char*)pSecond->pData) > 0);
}

int ComparatorCostum(const void *pData1, const void *pData2, void *pCtx)
{
    xarray_data_t *pFirst = (xarray_data_t*)pData1;
    xarray_data_t *pSecond = (xarray_data_t*)pData2;

    TestStruct *pSt1 = pFirst->pData;
    TestStruct *pSt2 = pSecond->pData;

    return strcmp((char*)pSt1->str, (char*)pSt2->str);
}

int main() 
{
    /* Create the xarray_t */
    xarray_t array;
    XArray_Init(&array, 5, 0);
    printf("Initialized the array\n");
    PrintEverything(&array);

    printf("Adding elements to the array\n");
    XArray_AddData(&array, "first element", strlen("first element") + 1);
    XArray_AddData(&array, "second element", strlen("second element") + 1);
    XArray_AddData(&array, "third element", strlen("third element") + 1);
    PrintEverything(&array);

    printf("Adding another elements to the array\n");
    XArray_AddData(&array, "lorem", strlen("lorem") + 1);
    XArray_AddData(&array, "ipsum", strlen("ipsum") + 1);
    XArray_AddData(&array, "dolor", strlen("dolor") + 1);
    XArray_AddData(&array, "last element", strlen("last element") + 1);
    PrintEverything(&array);

    printf("Inserting elements to the array\n");
    XArray_InsertData(&array, 3, "inserted element 1", strlen("inserted element 1") + 1);
    XArray_InsertData(&array, 4, "inserted element 2", strlen("inserted element 2") + 1);
    XArray_InsertData(&array, 5, "inserted element 3", strlen("inserted element 3") + 1);
    PrintEverything(&array);

    printf("Sorting elements by size\n");
    XArray_SortBy(&array, XARRAY_SORTBY_SIZE);
    PrintEverything(&array);

    printf("Sorting elements by alphabet\n");
    XArray_BubbleSort(&array, ComparatorStrings, NULL);
    PrintEverything(&array);

    printf("Removing elements from the first\n");
    xarray_data_t *pTmpData = XArray_Remove(&array, 0);
    XArray_FreeData(pTmpData);

    pTmpData = XArray_Remove(&array, 0);
    XArray_FreeData(pTmpData);

    pTmpData = XArray_Remove(&array, 0);
    XArray_FreeData(pTmpData);

    PrintEverything(&array);
    XArray_Clear(&array);

    printf("Cleared the array\n");
    PrintEverything(&array);

    TestStruct st1 = { .key = 1, .str = "test1"};
    TestStruct st2 = { .key = 2, .str = "test2"};
    TestStruct st3 = { .key = 4, .str = "test3"};
    TestStruct st4 = { .key = 3, .str = "test4"};
    TestStruct st5 = { .key = 5, .str = "test5"};

    printf("Adding elements to the array\n");
    XArray_AddDataKey(&array, &st1, 0, 1);
    XArray_AddDataKey(&array, &st2, 0, 2);
    XArray_AddDataKey(&array, &st3, 0, 4);
    XArray_AddDataKey(&array, &st4, 0, 3);
    XArray_AddDataKey(&array, &st5, 0, 5);

    PrintEverything2(&array);

    printf("Searching element by key\n");
    int nIndex = XArray_SentinelSearch(&array, 4);
    if (nIndex >= 0)
    {
        TestStruct *pData = (TestStruct*)XArray_GetData(&array, nIndex);
        if (pData) printf("Found element: %s\n", pData->str);
    }

    printf("Sorting elements by alphabet\n");
    XArray_Sort(&array, ComparatorCostum, NULL);
    PrintEverything2(&array);

    printf("Sorting elements by key\n");
    XArray_SortBy(&array, XARRAY_SORTBY_KEY);
    PrintEverything2(&array);

    printf("Removing element from the first\n");
    pTmpData = XArray_Remove(&array, 0);
    XArray_FreeData(pTmpData);
    PrintEverything2(&array);

    /* Clear the array and print info */
    XArray_Clear(&array);
    printf("Cleared the array\n");
    PrintEverything2(&array);

    /* We have not destructor at C, so clean up memory by ourselves */
    XArray_Destroy(&array);

    return 0;
}