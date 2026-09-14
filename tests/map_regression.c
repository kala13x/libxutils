#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "array.h"
#include "map.h"

#include "test.h"

static int count_it(xmap_pair_t *pPair, void *pContext)
{
    int *pCount = (int*)pContext;
    if (pPair == NULL || pPair->pKey == NULL) return XMAP_OINV;
    (*pCount)++;
    return XMAP_OK;
}

static int stop_it(xmap_pair_t *pPair, void *pContext)
{
    (void)pPair;
    (void)pContext;
    return XMAP_STOP;
}

static int XTest_lifecycle(void)
{
    xmap_t map;
    CHECK(XMap_Init(NULL, NULL, 1) == XMAP_OINV, "map rejects NULL init");
    CHECK(XMap_Init(&map, NULL, 0) == XMAP_OK, "map lazy init");
    CHECK(XMap_UsedSize(&map) == 0, "map starts empty");
    CHECK(XMap_Iterate(&map, count_it, NULL) == XMAP_EINIT, "unallocated map iteration");
    CHECK(XMap_Put(NULL, "x", "y") == XMAP_OINV, "map rejects NULL put");
    CHECK(XMap_Put(&map, NULL, "y") == XMAP_OINV, "map rejects NULL key");

    char keys[128][16];
    int values[128];
    for (int i = 0; i < 128; i++)
    {
        snprintf(keys[i], sizeof(keys[i]), "key-%03d", i);
        values[i] = i * 3;
        CHECK(XMap_Put(&map, keys[i], &values[i]) == XMAP_OK, "map bulk put");
    }
    CHECK(map.nCount == 128 && map.nTableSize >= 128, "map grows");
    for (int i = 0; i < 128; i++) CHECK(XMap_Get(&map, keys[i]) == &values[i], "map bulk get");

    int replacement = 999;
    CHECK(XMap_Put(&map, keys[5], &replacement) == XMAP_OK, "map update");
    CHECK(XMap_Get(&map, keys[5]) == &replacement && map.nCount == 128, "map update retains count");

    map.bAllowUpdate = XFALSE;
    CHECK(XMap_Put(&map, keys[5], &values[5]) == XMAP_EEXIST, "map update disabled");
    CHECK(XMap_Get(&map, keys[5]) == &replacement, "failed update preserves value");
    map.bAllowUpdate = XTRUE;

    for (int i = 0; i < 48; i++) CHECK(XMap_Remove(&map, keys[i]) == XMAP_OK, "map remove");
    CHECK(XMap_Remove(&map, "missing") == XMAP_MISSING, "map remove missing");
    CHECK(map.nCount == 80 && XMap_Get(&map, keys[0]) == NULL, "map removal state");

    for (int i = 0; i < 48; i++) CHECK(XMap_Put(&map, keys[i], &values[i]) == XMAP_OK, "map tombstone reuse");
    CHECK(map.nCount == 128, "map count after tombstone reuse");

    int nCount = 0;
    CHECK(XMap_Iterate(&map, count_it, &nCount) == XMAP_OK && nCount == 128, "map iteration");
    CHECK(XMap_Iterate(&map, stop_it, NULL) == XMAP_STOP, "map iteration stop");

    XMap_Reset(&map);
    CHECK(map.nCount == 0 && XMap_Iterate(&map, count_it, &nCount) == XMAP_EMPTY, "map reset");
    XMap_Destroy(&map);
    CHECK(map.pPairs == NULL && map.nTableSize == 0, "map destroy");
    return 0;
}

static int XTest_reference_model(void)
{
    xmap_t map;
    char keys[97][16];
    int values[97], present[97] = {0};
    uint32_t nSeed = 0x5813f7;
    CHECK(XMap_Init(&map, NULL, 16) == XMAP_OK, "Initialize reference-model map");
    for (int i = 0; i < 97; i++) snprintf(keys[i], sizeof(keys[i]), "prefix-%d", i);
    for (int nStep = 0; nStep < 10000; nStep++)
    {
        nSeed = nSeed * 1664525u + 1013904223u;
        unsigned int nKey = (nSeed >> 8) % 97;
        if (nSeed & 1)
        {
            values[nKey] = nStep;
            CHECK(XMap_Put(&map, keys[nKey], &values[nKey]) == XMAP_OK, "Reference-model insert or replacement");
            present[nKey] = 1;
        }
        else
        {
            int nExpected = present[nKey] ? XMAP_OK : XMAP_MISSING;
            CHECK(XMap_Remove(&map, keys[nKey]) == nExpected, "Reference-model removal status");
            present[nKey] = 0;
        }
        unsigned int nCount = 0;
        for (unsigned int i = 0; i < 97; i++)
        {
            CHECK(XMap_Get(&map, keys[i]) == (present[i] ? &values[i] : NULL), "Every entry agrees after interleaved mutations");
            nCount += present[i];
        }
        CHECK(map.nCount == nCount, "Count agrees with the independent presence table");
    }
    XMap_Destroy(&map);
    return 0;
}

XTEST_MAIN(XTEST_CASE(lifecycle), XTEST_CASE(reference_model))
