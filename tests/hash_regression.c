/* libxutils: collisions, signed keys, duplicate ownership and iterator termination. */
#include "test.h"
#include "hash.h"

static void XTest_Clear(void *pContext, void *pData, int nKey)
{
    (void)nKey;
    (*(int*)pContext)++;
    free(pData);
}

static int XTest_Count(xhash_pair_t *pPair, void *pContext)
{
    (void)pPair;
    (*(int*)pContext)++;
    return 0;
}

static int XTest_Stop(xhash_pair_t *pPair, void *pContext)
{
    XTest_Count(pPair, pContext);
    return 1;
}

static int XTest_collisions(void)
{
    xhash_t hash;
    int values[256], keys[256];
    XHash_Init(&hash, NULL, NULL);
    for (int i = 0, nKey = -10000; i < 256; nKey++)
    {
        if (XHASH_MIX(nKey, XHASH_MODULES) != 0) continue;
        keys[i] = nKey;
        values[i] = i;
        CHECK(XHash_Insert(&hash, &values[i], sizeof(int), keys[i]) == XSTDOK, "Insert into a single collision chain");
        i++;
    }
    for (int i = 0; i < 256; i += 2) CHECK(XHash_Delete(&hash, keys[i]) == XSTDOK, "Delete alternating collisions");
    for (int i = 0; i < 256; i++)
    {
        CHECK(XHash_GetData(&hash, keys[i]) == (i % 2 ? &values[i] : NULL), "Deletion preserves every neighboring key");
    }
    CHECK(hash.nPairCount == 128, "Collision deletion maintains count");
    int nCount = 0;
    XHash_Iterate(&hash, XTest_Count, &nCount);
    CHECK(nCount == 128, "Visit each live entry once");
    nCount = 0;
    XHash_Iterate(&hash, XTest_Stop, &nCount);
    CHECK(nCount == 1, "Stop across collision chains and buckets");
    XHash_Destroy(&hash);
    XHash_Destroy(&hash);
    return 0;
}

static int XTest_ownership(void)
{
    int nCleared = 0;
    xhash_t hash;
    XHash_Init(&hash, XTest_Clear, &nCleared);
    const int keys[] = {INT_MIN, -1, 0, 1, INT_MAX};
    for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); i++)
    {
        int *pValue = (int*)malloc(sizeof(int));
        CHECK(pValue != NULL, "Allocate owned value");
        *pValue = keys[i];
        CHECK(XHash_Insert(&hash, pValue, sizeof(int), keys[i]) == XSTDOK, "Insert signed boundary key");
        CHECK(XHash_Insert(&hash, pValue, sizeof(int), keys[i]) < 0, "Reject duplicate without taking another ownership");
        CHECK(XHash_GetSize(&hash, keys[i]) == sizeof(int), "Retain value size");
    }
    CHECK(nCleared == 0 && hash.nPairCount == 5, "Duplicate rejection must not clear the original");
    CHECK(XHash_Delete(&hash, -1) == XSTDOK && nCleared == 1, "Delete frees once");
    CHECK(XHash_Delete(&hash, -1) < 0 && nCleared == 1, "Repeated delete does not free again");
    XHash_Destroy(&hash);
    CHECK(nCleared == 5 && hash.nPairCount == 0, "Destroy frees all remaining values exactly once");
    return 0;
}

XTEST_MAIN(XTEST_CASE(collisions), XTEST_CASE(ownership))
