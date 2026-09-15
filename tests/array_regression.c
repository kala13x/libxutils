#include "test.h"
#include "array.h"

static int XTest_lifecycle(void)
{
    xarray_t arr;
    CHECK(XArray_Init(&arr, NULL, 0, 0) == NULL, "array lazy init");
    CHECK(XArray_Used(NULL) == 0 && XArray_Size(NULL) == 0, "array NULL sizes");
    CHECK(XArray_AddData(NULL, NULL, 0) == XARRAY_FAILURE, "array rejects NULL add");

    int values[] = {50, 10, 40, 20, 30};
    uint32_t keys[] = {5, 1, 4, 2, 3};
    for (size_t i = 0; i < 5; i++)
        CHECK(XArray_AddDataKey(&arr, &values[i], sizeof(values[i]), keys[i]) == (int)i, "array keyed add");
    CHECK(arr.nUsed == 5 && arr.nSize >= 5, "array lazy growth");
    CHECK(*(int*)XArray_GetData(&arr, 0) == 50, "array copied data");
    values[0] = -1;
    CHECK(*(int*)XArray_GetData(&arr, 0) == 50, "array owns copy");
    CHECK(XArray_GetData(&arr, 100) == NULL && XArray_GetSize(&arr, 100) == 0, "array out of range");

    XArray_SortBy(&arr, XARRAY_SORTBY_KEY);
    for (size_t i = 0; i < 5; i++) CHECK(XArray_GetKey(&arr, i) == i + 1, "array key sort");
    CHECK(XArray_LinearSearch(&arr, 4) == 3, "array linear search");
    CHECK(XArray_SentinelSearch(&arr, 4) == 3, "array sentinel search");
    CHECK(XArray_DoubleSearch(&arr, 4) == 3, "array double search");
    CHECK(XArray_BinarySearch(&arr, 4) == 3, "array binary search");
    CHECK(XArray_BinarySearch(&arr, 99) == XARRAY_FAILURE, "array search missing");

    xarray_data_t *pRemoved = XArray_Remove(&arr, 1);
    CHECK(pRemoved != NULL && pRemoved->nKey == 2 && arr.nUsed == 4, "array remove");
    XArray_FreeData(pRemoved);

    int inserted = 77;
    CHECK(XArray_InsertData(&arr, 1, &inserted, sizeof(inserted)) != NULL, "array insert");
    CHECK(*(int*)XArray_GetData(&arr, 1) == inserted && arr.nUsed == 5, "array insert position");

    int replacement = 88;
    xarray_data_t *pOld = XArray_SetData(&arr, 0, &replacement, sizeof(replacement));
    CHECK(pOld != NULL && *(int*)XArray_GetData(&arr, 0) == replacement, "array set");
    XArray_FreeData(pOld);

    XArray_Swap(&arr, 0, arr.nUsed - 1);
    CHECK(*(int*)XArray_GetData(&arr, arr.nUsed - 1) == replacement, "array swap");
    XArray_Delete(&arr, arr.nUsed - 1);
    CHECK(arr.nUsed == 4, "array delete");
    XArray_Clear(&arr);
    CHECK(arr.nUsed == 0, "array clear");
    XArray_Destroy(&arr);
    CHECK(arr.pData == NULL && arr.nSize == 0, "array destroy");

    xarray_t fixed;
    CHECK(XArray_Init(&fixed, NULL, 1, 1) != NULL, "fixed array init");
    CHECK(XArray_AddData(&fixed, &replacement, sizeof(replacement)) == 0, "fixed array first add");
    CHECK(XArray_AddData(&fixed, &replacement, sizeof(replacement)) == XARRAY_FAILURE, "fixed array rejects overflow");
    XArray_Destroy(&fixed);
    return 0;
}

static int XTest_reference_model(void)
{
    xarray_t array;
    int reference[128];
    size_t nUsed = 0;
    uint32_t nSeed = 0xd335;
    XArray_Init(&array, NULL, 0, 0);
    for (int nStep = 0; nStep < 5000; nStep++)
    {
        nSeed = nSeed * 1664525u + 1013904223u;
        size_t nIndex = nUsed ? (nSeed >> 8) % nUsed : 0;
        if (!nUsed || ((nSeed & 1) && nUsed < 128))
        {
            CHECK(XArray_InsertData(&array, nIndex, &nStep, sizeof(nStep)) != NULL, "Insert at a modeled position");
            memmove(reference + nIndex + 1, reference + nIndex, (nUsed - nIndex) * sizeof(int));
            reference[nIndex] = nStep;
            nUsed++;
        }
        else
        {
            xarray_data_t *pRemoved = XArray_Remove(&array, nIndex);
            CHECK(pRemoved && *(int*)pRemoved->pData == reference[nIndex], "Removal returns the modeled value");
            XArray_FreeData(pRemoved);
            nUsed--;
            memmove(reference + nIndex, reference + nIndex + 1, (nUsed - nIndex) * sizeof(int));
        }
        CHECK(array.nUsed == nUsed, "Array count agrees after mutation");
        for (size_t i = 0; i < nUsed; i++)
            CHECK(*(int*)XArray_GetData(&array, i) == reference[i], "Array order agrees with model");
    }
    XArray_Destroy(&array);
    return 0;
}

static int XTest_search_boundaries(void)
{
    xarray_t array;
    XArray_Init(&array, NULL, 0, 0);
    CHECK(XArray_BinarySearch(&array, 0) == XARRAY_FAILURE, "Searching an empty array terminates");
    XArray_Delete(&array, 0);
    CHECK(array.nUsed == 0, "Deleting from an empty array cannot underflow its count");
    for (uint32_t i = 0; i < 128; i++)
    {
        uint32_t nKey = i * 2;
        CHECK(XArray_AddDataKey(&array, &nKey, sizeof(nKey), nKey) >= 0, "Insert sorted even keys");
    }
    for (uint32_t i = 0; i < 256; i++)
    {
        int nExpected = i % 2 ? XARRAY_FAILURE : (int)(i / 2);
        CHECK(XArray_BinarySearch(&array, i) == nExpected, "Binary search finds endpoints and rejects gaps");
        CHECK(XArray_LinearSearch(&array, i) == nExpected, "Linear search agrees with independent even-key model");
        CHECK(XArray_SentinelSearch(&array, i) == nExpected, "Sentinel search restores the final entry");
    }
    CHECK(XArray_GetKey(&array, 127) == 254, "Sentinel search must not corrupt the last key");
    XArray_Delete(&array, SIZE_MAX);
    CHECK(array.nUsed == 128, "An invalid delete leaves every live element intact");
    CHECK(XArray_InsertData(&array, SIZE_MAX, &array, sizeof(array)) == NULL, "Reject invalid insertion before allocation");
    XArray_Destroy(&array);
    return 0;
}


/* The comparator receives the entries themselves, not pointers to them. */
static int XTest_CompareKey(const void *pFirst, const void *pSecond, void *pCtx)
{
    (void)pCtx;
    const xarray_data_t *pLeft = (const xarray_data_t*)pFirst;
    const xarray_data_t *pRight = (const xarray_data_t*)pSecond;
    return (int)pLeft->nKey - (int)pRight->nKey;
}

static int XTest_CompareKeyDesc(const void *pFirst, const void *pSecond, void *pCtx)
{
    return -XTest_CompareKey(pFirst, pSecond, pCtx);
}

/* Fills an array with the given keys in the given order. */
static int array_fill(xarray_t *pArr, const uint32_t *pKeys, size_t nCount)
{
    for (size_t i = 0; i < nCount; i++)
        if (XArray_AddDataKey(pArr, (void*)"v", 0, pKeys[i]) < 0) return XSTDERR;
    return XSTDOK;
}

static int XTest_sorting(void)
{
    const uint32_t keys[] = {5, 1, 4, 2, 3};

    /* Each sort entry point puts the same keys in the same order. */
    xarray_t arr;
    XArray_Init(&arr, NULL, 0, 0);
    CHECK(XArray_Used(&arr) == 0, "A zero sized array starts empty and allocates on demand");
    CHECK(array_fill(&arr, keys, 5) == XSTDOK, "The array is filled");

    XArray_Sort(&arr, XTest_CompareKey, NULL);
    for (size_t i = 0; i < 5; i++)
        CHECK(XArray_GetKey(&arr, i) == (uint32_t)(i + 1), "Sorting orders the entries by key");
    XArray_Destroy(&arr);

    XArray_Init(&arr, NULL, 0, 0);
    CHECK(XArray_Used(&arr) == 0, "A zero sized array starts empty and allocates on demand");
    CHECK(array_fill(&arr, keys, 5) == XSTDOK, "The array is filled");
    XArray_BubbleSort(&arr, XTest_CompareKey, NULL);
    for (size_t i = 0; i < 5; i++)
        CHECK(XArray_GetKey(&arr, i) == (uint32_t)(i + 1), "The bubble sort orders the entries by key");
    XArray_Destroy(&arr);

    XArray_Init(&arr, NULL, 0, 0);
    CHECK(XArray_Used(&arr) == 0, "A zero sized array starts empty and allocates on demand");
    CHECK(array_fill(&arr, keys, 5) == XSTDOK, "The array is filled");
    XArray_QuickSort(&arr, XTest_CompareKey, NULL, 0, (int)XArray_Used(&arr) - 1);
    for (size_t i = 0; i < 5; i++)
        CHECK(XArray_GetKey(&arr, i) == (uint32_t)(i + 1), "The quick sort orders the entries by key");

    /* A reversing comparator reverses the order. */
    XArray_Sort(&arr, XTest_CompareKeyDesc, NULL);
    for (size_t i = 0; i < 5; i++)
        CHECK(XArray_GetKey(&arr, i) == (uint32_t)(5 - i), "A reversing comparator reverses the order");
    XArray_Destroy(&arr);

    /* The built-in key sort agrees with the comparator one. */
    XArray_Init(&arr, NULL, 0, 0);
    CHECK(XArray_Used(&arr) == 0, "A zero sized array starts empty and allocates on demand");
    CHECK(array_fill(&arr, keys, 5) == XSTDOK, "The array is filled");
    XArray_SortBy(&arr, XARRAY_SORTBY_KEY);
    for (size_t i = 0; i < 5; i++)
        CHECK(XArray_GetKey(&arr, i) == (uint32_t)(i + 1), "The built-in key sort orders by key");

    /* Sorting an already sorted array leaves it sorted. */
    XArray_SortBy(&arr, XARRAY_SORTBY_KEY);
    for (size_t i = 0; i < 5; i++)
        CHECK(XArray_GetKey(&arr, i) == (uint32_t)(i + 1), "Re-sorting a sorted array is stable");
    XArray_Destroy(&arr);

    /* Degenerate sizes must not run off either end. */
    for (size_t nCount = 0; nCount <= 2; nCount++)
    {
        XArray_Init(&arr, NULL, 0, 0);
    CHECK(XArray_Used(&arr) == 0, "A zero sized array starts empty and allocates on demand");
        CHECK(array_fill(&arr, keys, nCount) == XSTDOK, "The short array is filled");
        XArray_Sort(&arr, XTest_CompareKey, NULL);
        XArray_SortBy(&arr, XARRAY_SORTBY_KEY);
        CHECK(XArray_Used(&arr) == nCount, "Sorting a short array keeps every entry");
        XArray_Destroy(&arr);
    }

    /* Duplicate keys all survive the sort. */
    const uint32_t dupes[] = {2, 1, 2, 1, 2};
    XArray_Init(&arr, NULL, 0, 0);
    CHECK(XArray_Used(&arr) == 0, "A zero sized array starts empty and allocates on demand");
    CHECK(array_fill(&arr, dupes, 5) == XSTDOK, "The duplicate keys are filled");
    XArray_SortBy(&arr, XARRAY_SORTBY_KEY);
    CHECK(XArray_Used(&arr) == 5, "Sorting keeps every duplicate");
    for (size_t i = 1; i < 5; i++)
        CHECK(XArray_GetKey(&arr, i) >= XArray_GetKey(&arr, i - 1), "Duplicates come out in order");
    XArray_Destroy(&arr);
    return 0;
}

static int XTest_mutation(void)
{
    /* Inserting shifts the tail; setting replaces in place. */
    xarray_t arr;
    XArray_Init(&arr, NULL, 0, 0);
    CHECK(XArray_Used(&arr) == 0, "A zero sized array starts empty and allocates on demand");
    CHECK(XArray_AddData(&arr, (void*)"a", 2) >= 0, "The first entry is added");
    CHECK(XArray_AddData(&arr, (void*)"c", 2) >= 0, "The second entry is added");

    CHECK(XArray_InsertData(&arr, 1, (void*)"b", 2) != NULL, "An entry is inserted in the middle");
    CHECK(XArray_Used(&arr) == 3, "The insert lengthened the array");
    CHECK(strcmp((char*)XArray_GetData(&arr, 0), "a") == 0, "The entry before the insert stayed put");
    CHECK(strcmp((char*)XArray_GetData(&arr, 1), "b") == 0, "The inserted entry is at its index");
    CHECK(strcmp((char*)XArray_GetData(&arr, 2), "c") == 0, "The entry after the insert shifted along");

    /* Replacing hands the displaced entry back, and the caller owns it. */
    xarray_data_t *pDisplaced = XArray_SetData(&arr, 0, (void*)"A", 2);
    CHECK(pDisplaced != NULL, "Replacing hands the displaced entry back");
    CHECK(strcmp((char*)pDisplaced->pData, "a") == 0, "The displaced entry is the one that was there");
    XArray_FreeData(pDisplaced);

    CHECK(strcmp((char*)XArray_GetData(&arr, 0), "A") == 0, "The replacement is stored");
    CHECK(XArray_Used(&arr) == 3, "Replacing does not change the length");

    /* Swapping exchanges two entries and leaves the rest alone. */
    XArray_Swap(&arr, 0, 2);
    CHECK(strcmp((char*)XArray_GetData(&arr, 0), "c") == 0, "The swap moved the far entry to the front");
    CHECK(strcmp((char*)XArray_GetData(&arr, 2), "A") == 0, "The swap moved the front entry to the far end");
    CHECK(strcmp((char*)XArray_GetData(&arr, 1), "b") == 0, "The swap left the middle alone");

    /* A swap with an out of range index changes nothing. */
    XArray_Swap(&arr, 0, 99);
    CHECK(strcmp((char*)XArray_GetData(&arr, 0), "c") == 0, "An out of range swap changes nothing");

    /* Removing hands the entry back; deleting releases it. */
    xarray_data_t *pRemoved = XArray_Remove(&arr, 0);
    CHECK(pRemoved != NULL, "Removing hands the entry back to the caller");
    CHECK(XArray_Used(&arr) == 2, "Removing shortened the array");
    XArray_FreeData(pRemoved);

    XArray_Delete(&arr, 0);
    CHECK(XArray_Used(&arr) == 1, "Deleting shortened the array");
    CHECK(strcmp((char*)XArray_GetData(&arr, 0), "A") == 0, "The surviving entry is the expected one");

    /* Out of range access is reported rather than guessed at. */
    CHECK(XArray_GetData(&arr, 99) == NULL, "An entry past the end is not there");
    CHECK(XArray_Get(&arr, 99) == NULL, "An entry past the end has no record");
    CHECK(XArray_Remove(&arr, 99) == NULL, "An entry past the end cannot be removed");
    CHECK(XArray_Contains(&arr, 99) == 0, "An index past the end is not contained");
    CHECK(XArray_Contains(&arr, 0) == 1, "An index inside the array is contained");

    /* The defaulting accessor returns the caller's fallback. */
    CHECK(strcmp((char*)XArray_GetDataOr(&arr, 99, (void*)"fallback"), "fallback") == 0,
        "An out of range read returns the caller's fallback");
    CHECK(strcmp((char*)XArray_GetDataOr(&arr, 0, (void*)"fallback"), "A") == 0,
        "An in range read returns the stored entry");

    XArray_Destroy(&arr);
    return 0;
}

static int XTest_fixed_capacity(void)
{
    /* A fixed array refuses to grow past its capacity rather than
     * reallocating out from under the caller. */
    xarray_t arr;
    CHECK(XArray_Init(&arr, NULL, 3, 1) != NULL, "A sized array allocates its storage up front");
    CHECK(XArray_Size(&arr) == 3, "The fixed array has the requested capacity");

    for (int i = 0; i < 3; i++)
        CHECK(XArray_AddData(&arr, (void*)"v", 0) == i, "Every entry up to the capacity is accepted");
    CHECK(XArray_Used(&arr) == 3, "The fixed array is full");

    CHECK(XArray_AddData(&arr, (void*)"v", 0) < 0, "An entry past the capacity is refused");
    CHECK(XArray_Used(&arr) == 3, "The refused entry did not displace anything");
    CHECK(XArray_Size(&arr) == 3, "The refused entry did not grow the array");

    /* Freeing a slot makes room again. */
    XArray_Delete(&arr, 0);
    CHECK(XArray_Used(&arr) == 2, "Deleting freed a slot");
    CHECK(XArray_AddData(&arr, (void*)"v", 0) >= 0, "The freed slot accepts a new entry");

    XArray_Destroy(&arr);

    /* A growing array reallocates instead. */
    CHECK(XArray_Init(&arr, NULL, 2, 0) != NULL, "The growing array allocates its initial storage");
    for (int i = 0; i < 64; i++)
        CHECK(XArray_AddData(&arr, (void*)"v", 0) >= 0, "A growing array keeps accepting entries");
    CHECK(XArray_Used(&arr) == 64, "Every entry was stored");
    CHECK(XArray_Size(&arr) >= 64, "The capacity grew to hold them");
    XArray_Destroy(&arr);
    return 0;
}

static int XTest_search_agreement(void)
{
    /* Every search strategy has to agree on a sorted array, since they are
     * interchangeable from the caller's point of view. */
    xarray_t arr;
    XArray_Init(&arr, NULL, 0, 0);
    CHECK(XArray_Used(&arr) == 0, "A zero sized array starts empty and allocates on demand");

    for (uint32_t nKey = 0; nKey < 32; nKey++)
        CHECK(XArray_AddDataKey(&arr, (void*)"v", 0, nKey * 2) >= 0, "Every key is added");
    XArray_SortBy(&arr, XARRAY_SORTBY_KEY);

    for (uint32_t nKey = 0; nKey < 32; nKey++)
    {
        int nExpected = (int)nKey;
        CHECK(XArray_LinearSearch(&arr, nKey * 2) == nExpected, "The linear search finds every key");
        CHECK(XArray_BinarySearch(&arr, nKey * 2) == nExpected, "The binary search finds every key");
        CHECK(XArray_SentinelSearch(&arr, nKey * 2) == nExpected, "The sentinel search finds every key");
        CHECK(XArray_DoubleSearch(&arr, nKey * 2) == nExpected, "The double search finds every key");
    }

    /* A key that is not there is reported missing by every strategy. */
    const uint32_t absent[] = {1, 3, 63, 64, 1000};
    for (size_t i = 0; i < sizeof(absent) / sizeof(*absent); i++)
    {
        CHECK(XArray_LinearSearch(&arr, absent[i]) < 0, "The linear search reports a missing key");
        CHECK(XArray_BinarySearch(&arr, absent[i]) < 0, "The binary search reports a missing key");
        CHECK(XArray_SentinelSearch(&arr, absent[i]) < 0, "The sentinel search reports a missing key");
        CHECK(XArray_DoubleSearch(&arr, absent[i]) < 0, "The double search reports a missing key");
    }

    XArray_Destroy(&arr);

    /* An empty array finds nothing without reading past its storage. */
    XArray_Init(&arr, NULL, 0, 0);
    CHECK(XArray_Used(&arr) == 0, "The empty array starts empty");
    CHECK(XArray_LinearSearch(&arr, 1) < 0, "The linear search on an empty array finds nothing");
    CHECK(XArray_BinarySearch(&arr, 1) < 0, "The binary search on an empty array finds nothing");
    CHECK(XArray_SentinelSearch(&arr, 1) < 0, "The sentinel search on an empty array finds nothing");
    CHECK(XArray_DoubleSearch(&arr, 1) < 0, "The double search on an empty array finds nothing");
    XArray_Destroy(&arr);
    return 0;
}

static int XTest_heap_array(void)
{
    /* The heap array owns itself and clears the caller pointer. */
    xarray_t *pArr = XArray_New(NULL, 8, 0);
    CHECK(pArr != NULL, "A heap array is allocated");
    CHECK(XArray_AddData(pArr, (void*)"x", 0) >= 0, "The heap array accepts entries");
    CHECK(XArray_Used(pArr) == 1, "The heap array counted the entry");

    XArray_Free(&pArr);
    CHECK(pArr == NULL, "Freeing clears the caller pointer");
    XArray_Free(&pArr);
    XArray_Free(NULL);

    /* A pooled array allocates its entries from its own pool. */
    pArr = XArray_NewPool(4096, 8, 0);
    CHECK(pArr != NULL, "A pooled array is allocated");
    for (int i = 0; i < 32; i++)
        CHECK(XArray_AddData(pArr, (void*)"pooled", 0) >= 0, "The pooled array accepts entries");
    CHECK(XArray_Used(pArr) == 32, "Every pooled entry was stored");
    XArray_Free(&pArr);
    CHECK(pArr == NULL, "Freeing the pooled array clears the caller pointer");

    /* Clearing empties an array without releasing it. */
    xarray_t arr;
    XArray_Init(&arr, NULL, 0, 0);
    CHECK(XArray_Used(&arr) == 0, "A zero sized array starts empty and allocates on demand");
    for (int i = 0; i < 8; i++) CHECK(XArray_AddData(&arr, (void*)"x", 0) >= 0, "Entries are added");

    XArray_Clear(&arr);
    CHECK(XArray_Used(&arr) == 0, "Clearing empties the array");
    CHECK(XArray_AddData(&arr, (void*)"x", 0) >= 0, "A cleared array is still usable");

    XArray_Destroy(&arr);
    XArray_Destroy(&arr);
    CHECK(XArray_Used(&arr) == 0, "A repeatedly destroyed array stays empty");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(lifecycle),
    XTEST_CASE(reference_model),
    XTEST_CASE(search_boundaries),
    XTEST_CASE(sorting),
    XTEST_CASE(mutation),
    XTEST_CASE(fixed_capacity),
    XTEST_CASE(search_agreement),
    XTEST_CASE(heap_array)
)
