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

XTEST_MAIN(XTEST_CASE(lifecycle), XTEST_CASE(reference_model), XTEST_CASE(search_boundaries))
