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


static int XTest_operations(void)
{
    /* Entries are keyed by an integer and retrieved by the same one. */
    xhash_t hash;
    XHash_Init(&hash, NULL, NULL);
    CHECK(hash.nPairCount == 0, "A fresh table holds nothing");

    CHECK(XHash_Insert(&hash, (void*)"first", 5, 1) == XSTDOK, "The first entry is inserted");
    CHECK(XHash_Insert(&hash, (void*)"second", 6, 2) == XSTDOK, "The second entry is inserted");
    CHECK(hash.nPairCount == 2, "Both entries are counted");

    CHECK(strcmp((char*)XHash_GetData(&hash, 1), "first") == 0, "The first entry is retrievable");
    CHECK(strcmp((char*)XHash_GetData(&hash, 2), "second") == 0, "The second entry is retrievable");
    CHECK(XHash_GetSize(&hash, 1) == 5, "The stored size comes back with the entry");
    CHECK(XHash_GetSize(&hash, 2) == 6, "Each entry keeps its own size");

    /* A key that was never inserted is not there. */
    CHECK(XHash_GetData(&hash, 99) == NULL, "A key that was never inserted has no data");
    CHECK(XHash_GetPair(&hash, 99) == NULL, "A key that was never inserted has no pair");
    CHECK(XHash_GetNode(&hash, 99) == NULL, "A key that was never inserted has no node");
    CHECK(XHash_GetSize(&hash, 99) <= 0, "A key that was never inserted has no size");

    /* The pair accessor carries both halves. */
    xhash_pair_t *pPair = XHash_GetPair(&hash, 1);
    CHECK(pPair != NULL, "The pair is found");
    CHECK(pPair->nKey == 1, "The pair carries its key");
    CHECK(strcmp((char*)pPair->pData, "first") == 0, "The pair carries its data");

    /* Inserting a key that is already there is refused rather than
     * silently shadowing the first entry. */
    CHECK(XHash_Insert(&hash, (void*)"duplicate", 9, 1) != XSTDOK, "A duplicate key is refused");
    CHECK(strcmp((char*)XHash_GetData(&hash, 1), "first") == 0, "The original entry is untouched");
    CHECK(hash.nPairCount == 2, "A refused insert does not grow the table");

    /* Deleting removes only the named entry. */
    CHECK(XHash_Delete(&hash, 1) == XSTDOK, "An entry is deleted");
    CHECK(XHash_GetData(&hash, 1) == NULL, "The deleted entry is gone");
    CHECK(strcmp((char*)XHash_GetData(&hash, 2), "second") == 0, "The other entry survived");
    CHECK(hash.nPairCount == 1, "The count follows the deletion");

    CHECK(XHash_Delete(&hash, 99) != XSTDOK, "Deleting a missing key is reported");
    CHECK(hash.nPairCount == 1, "A refused delete changes nothing");

    /* The key can be reinserted once it is free again. */
    CHECK(XHash_Insert(&hash, (void*)"again", 5, 1) == XSTDOK, "A freed key can be reused");
    CHECK(strcmp((char*)XHash_GetData(&hash, 1), "again") == 0, "The reused key holds the new entry");

    XHash_Destroy(&hash);
    CHECK(hash.nPairCount == 0, "Destroying empties the table");
    return 0;
}

static int XTest_distribution(void)
{
    /* Many keys have to remain individually retrievable however they land
     * in the fixed number of buckets. */
    xhash_t hash;
    XHash_Init(&hash, NULL, NULL);

    static char values[512][16];
    for (int i = 0; i < 512; i++)
    {
        snprintf(values[i], sizeof(values[i]), "v%d", i);
        CHECK(XHash_Insert(&hash, values[i], strlen(values[i]), i) == XSTDOK, "Every key is inserted");
    }
    CHECK(hash.nPairCount == 512, "Every key is counted");

    for (int i = 0; i < 512; i++)
        CHECK(strcmp((char*)XHash_GetData(&hash, i), values[i]) == 0, "Every key retrieves its own value");

    /* Negative and extreme keys land in the table like any other. */
    const int extremes[] = {-1, -1000, 0x7fffffff, -0x7fffffff};
    for (size_t i = 0; i < sizeof(extremes) / sizeof(*extremes); i++)
    {
        CHECK(XHash_Insert(&hash, (void*)"edge", 4, extremes[i]) == XSTDOK, "An extreme key is inserted");
        CHECK(strcmp((char*)XHash_GetData(&hash, extremes[i]), "edge") == 0, "An extreme key retrieves its value");
    }

    /* Deleting every second key leaves the rest reachable. */
    for (int i = 0; i < 512; i += 2)
        CHECK(XHash_Delete(&hash, i) == XSTDOK, "Every second key is deleted");

    for (int i = 0; i < 512; i++)
    {
        void *pData = XHash_GetData(&hash, i);
        if (i % 2) CHECK(strcmp((char*)pData, values[i]) == 0, "The kept keys are still retrievable");
        else CHECK(pData == NULL, "The deleted keys are gone");
    }

    XHash_Destroy(&hash);
    return 0;
}

static int XTest_pairs(void)
{
    /* A pair can be built by the caller and handed over. */
    xhash_t hash;
    XHash_Init(&hash, NULL, NULL);

    xhash_pair_t *pPair = XHash_NewPair((void*)"handed-over", 11, 7);
    CHECK(pPair != NULL, "A pair is built");
    CHECK(pPair->nKey == 7 && pPair->nSize == 11, "The pair carries what it was built with");

    CHECK(XHash_InsertPair(&hash, pPair) == XSTDOK, "The pair is inserted");
    CHECK(strcmp((char*)XHash_GetData(&hash, 7), "handed-over") == 0, "The inserted pair is retrievable");
    CHECK(hash.nPairCount == 1, "The inserted pair is counted");

    /* Inserting a pair whose key is taken is refused, and the caller keeps
     * responsibility for the pair it built. */
    xhash_pair_t *pDuplicate = XHash_NewPair((void*)"rejected", 8, 7);
    CHECK(pDuplicate != NULL, "A second pair is built");
    CHECK(XHash_InsertPair(&hash, pDuplicate) != XSTDOK, "A pair with a taken key is refused");
    CHECK(strcmp((char*)XHash_GetData(&hash, 7), "handed-over") == 0, "The original pair is untouched");
    free(pDuplicate);

    XHash_Destroy(&hash);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(collisions),
    XTEST_CASE(ownership),
    XTEST_CASE(operations),
    XTEST_CASE(distribution),
    XTEST_CASE(pairs)
)
