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


static int XTest_CountPairs(xmap_pair_t *pPair, void *pContext)
{
    (void)pPair;
    (*(int*)pContext)++;
    return XMAP_OK;
}

static int XTest_StopAfterOne(xmap_pair_t *pPair, void *pContext)
{
    (void)pPair;
    (*(int*)pContext)++;
    return XMAP_STOP;
}

static int XTest_operations(void)
{
    xmap_t map;
    CHECK(XMap_Init(&map, NULL, 4) == XMAP_OK, "The map initializes");
    CHECK(map.nCount == 0, "A fresh map holds nothing");

    CHECK(XMap_Put(&map, (char*)"alpha", (void*)"1") == XMAP_OK, "The first entry is stored");
    CHECK(XMap_Put(&map, (char*)"beta", (void*)"2") == XMAP_OK, "The second entry is stored");
    CHECK(map.nCount == 2, "Both entries are counted");
    CHECK(XMap_UsedSize(&map) == 2, "The used size agrees with the count");

    CHECK(strcmp((char*)XMap_Get(&map, "alpha"), "1") == 0, "The first entry is retrievable");
    CHECK(strcmp((char*)XMap_Get(&map, "beta"), "2") == 0, "The second entry is retrievable");
    CHECK(XMap_Get(&map, "absent") == NULL, "An entry that was never stored is not found");

    /* Storing an existing key replaces the value rather than duplicating it. */
    CHECK(XMap_Put(&map, (char*)"alpha", (void*)"replaced") == XMAP_OK, "An existing key is stored again");
    CHECK(map.nCount == 2, "Replacing does not grow the map");
    CHECK(strcmp((char*)XMap_Get(&map, "alpha"), "replaced") == 0, "The replacement is what is retrieved");

    /* The pair and index accessors agree with the plain one. */
    int nIndex = -1;
    void *pValue = XMap_GetIndex(&map, "beta", &nIndex);
    CHECK(pValue != NULL && strcmp((char*)pValue, "2") == 0, "The indexed lookup returns the value");
    CHECK(nIndex >= 0, "The indexed lookup reports where the entry lives");

    xmap_pair_t *pPair = XMap_GetPair(&map, "beta");
    CHECK(pPair != NULL, "The pair lookup finds the entry");
    CHECK(strcmp((char*)pPair->pKey, "beta") == 0, "The pair carries its key");
    CHECK(strcmp((char*)pPair->pData, "2") == 0, "The pair carries its value");
    CHECK(XMap_GetPair(&map, "absent") == NULL, "A missing key has no pair");

    /* Updating by hash reaches the same entry. */
    CHECK(XMap_Update(&map, XMap_GetHash(&map, "beta"), (void*)"updated") == XMAP_OK,
        "An entry is updated through its hash");
    CHECK(strcmp((char*)XMap_Get(&map, "beta"), "updated") == 0, "The update is what is retrieved");

    /* Removing takes the entry out and reports a missing one. */
    CHECK(XMap_Remove(&map, "alpha") == XMAP_OK, "An entry is removed");
    CHECK(XMap_Get(&map, "alpha") == NULL, "The removed entry is gone");
    CHECK(strcmp((char*)XMap_Get(&map, "beta"), "updated") == 0, "The other entry survived the removal");
    CHECK(XMap_Remove(&map, "absent") == XMAP_MISSING, "Removing a missing entry is reported");

    /* A reset empties the map but leaves it usable. */
    XMap_Reset(&map);
    CHECK(map.nCount == 0, "A reset empties the map");
    CHECK(XMap_Get(&map, "beta") == NULL, "A reset drops every entry");
    CHECK(XMap_Put(&map, (char*)"after", (void*)"reset") == XMAP_OK, "A reset map still accepts entries");

    XMap_Destroy(&map);
    return 0;
}

static int XTest_iteration(void)
{
    xmap_t map;
    CHECK(XMap_Init(&map, NULL, 8) == XMAP_OK, "The map initializes");

    char sKeys[16][8];
    for (int i = 0; i < 16; i++)
    {
        snprintf(sKeys[i], sizeof(sKeys[i]), "k%d", i);
        CHECK(XMap_Put(&map, sKeys[i], (void*)(intptr_t)(i + 1)) == XMAP_OK, "Every entry is stored");
    }

    /* A full walk visits every entry exactly once. */
    int nVisited = 0;
    CHECK(XMap_Iterate(&map, XTest_CountPairs, &nVisited) == XMAP_OK, "The walk completes");
    CHECK(nVisited == 16, "The walk visited every entry exactly once");

    /* An iterator that stops early stops the walk. */
    nVisited = 0;
    CHECK(XMap_Iterate(&map, XTest_StopAfterOne, &nVisited) == XMAP_STOP, "A stopping walk reports itself");
    CHECK(nVisited == 1, "The walk stopped at the first entry");

    /* Walking an empty map visits nothing. */
    XMap_Reset(&map);
    nVisited = 0;
    XMap_Iterate(&map, XTest_CountPairs, &nVisited);
    CHECK(nVisited == 0, "An empty map has nothing to visit");

    XMap_Destroy(&map);
    return 0;
}

static int XTest_growth(void)
{
    /* A map that starts small has to rehash without losing anything. */
    xmap_t map;
    CHECK(XMap_Init(&map, NULL, 2) == XMAP_OK, "The map initializes small");
    uint32_t nInitialSize = map.nTableSize;

    char sKeys[256][16];
    for (int i = 0; i < 256; i++)
    {
        snprintf(sKeys[i], sizeof(sKeys[i]), "key-%d", i);
        CHECK(XMap_Put(&map, sKeys[i], (void*)(intptr_t)(i + 1)) == XMAP_OK, "Every entry is stored");
    }

    CHECK(map.nCount == 256, "Every entry is counted after the growth");
    CHECK(map.nTableSize > nInitialSize, "The table grew to hold them");

    for (int i = 0; i < 256; i++)
        CHECK((intptr_t)XMap_Get(&map, sKeys[i]) == i + 1, "Every entry survived the rehash");

    /* Removing half leaves the other half intact. */
    for (int i = 0; i < 256; i += 2)
        CHECK(XMap_Remove(&map, sKeys[i]) == XMAP_OK, "Every second entry is removed");

    for (int i = 0; i < 256; i++)
    {
        void *pValue = XMap_Get(&map, sKeys[i]);
        if (i % 2) CHECK((intptr_t)pValue == i + 1, "The kept entries are still retrievable");
        else CHECK(pValue == NULL, "The removed entries are gone");
    }

    XMap_Destroy(&map);
    return 0;
}

static int XTest_hashing(void)
{
    xmap_t map;
    CHECK(XMap_Init(&map, NULL, 64) == XMAP_OK, "The map initializes");

    /* A hash is inside the table, and the same key always hashes the same. */
    const char *pKeys[] = {"", "a", "ab", "abc", "a-much-longer-key-than-the-others", "\x01\x02"};
    for (size_t i = 0; i < sizeof(pKeys) / sizeof(*pKeys); i++)
    {
        int nHash = XMap_Hash(&map, pKeys[i]);
        CHECK(nHash >= 0 && nHash < (int)map.nTableSize, "Every hash lands inside the table");
        CHECK(XMap_Hash(&map, pKeys[i]) == nHash, "The same key always hashes the same");
        CHECK(XMap_HashFNV(&map, pKeys[i]) >= 0, "The named hash is usable directly");
    }

    /* Keys that differ in one byte are still both storable and findable,
     * whether or not they collide. */
    CHECK(XMap_Put(&map, (char*)"collide-a", (void*)"1") == XMAP_OK, "The first key is stored");
    CHECK(XMap_Put(&map, (char*)"collide-b", (void*)"2") == XMAP_OK, "The second key is stored");
    CHECK(strcmp((char*)XMap_Get(&map, "collide-a"), "1") == 0, "The first key keeps its own value");
    CHECK(strcmp((char*)XMap_Get(&map, "collide-b"), "2") == 0, "The second key keeps its own value");

    XMap_Destroy(&map);

    /* A heap map owns itself. */
    xmap_t *pMap = XMap_New(NULL, 16);
    CHECK(pMap != NULL, "A heap map is allocated");
    CHECK(XMap_Put(pMap, (char*)"heap", (void*)"value") == XMAP_OK, "The heap map accepts entries");
    CHECK(strcmp((char*)XMap_Get(pMap, "heap"), "value") == 0, "The heap map retrieves them");
    XMap_Free(pMap);
    return 0;
}


static int XTest_pair_transfer(void)
{
    /* A pair taken out of one map can be put straight into another. That is
     * how a map is copied or split, so the pair the second map ends up with
     * has to be an equal entry rather than an alias of the first map's
     * storage: clearing either one must leave the other usable. */
    xmap_t source, target;
    CHECK(XMap_Init(&source, NULL, 16) == XMAP_OK, "The source map initializes");
    CHECK(XMap_Init(&target, NULL, 16) == XMAP_OK, "The target map initializes");

    char sKeys[4][8];
    int nValues[4] = {10, 20, 30, 40};

    for (int i = 0; i < 4; i++)
    {
        snprintf(sKeys[i], sizeof(sKeys[i]), "k%d", i);
        CHECK(XMap_Put(&source, sKeys[i], &nValues[i]) == XMAP_OK, "An entry goes into the source");
    }

    /* Move every pair across through the pair form. */
    for (int i = 0; i < 4; i++)
    {
        xmap_pair_t *pPair = XMap_GetPair(&source, sKeys[i]);
        CHECK(pPair != NULL, "The pair is found in the source");
        CHECK(XMap_PutPair(&target, pPair) == XMAP_OK, "The pair goes into the target");
    }

    CHECK(target.nCount == 4, "Every pair reached the target");
    CHECK(source.nCount == 4, "The source still holds its own entries");

    for (int i = 0; i < 4; i++)
    {
        int *pFromTarget = (int*)XMap_Get(&target, sKeys[i]);
        CHECK(pFromTarget != NULL, "The value is reachable in the target");
        CHECK(pFromTarget == NULL || *pFromTarget == nValues[i], "It is the value that was stored");
    }

    /* Destroying the source leaves the target's entries intact. */
    XMap_Destroy(&source);

    for (int i = 0; i < 4; i++)
    {
        int *pFromTarget = (int*)XMap_Get(&target, sKeys[i]);
        CHECK(pFromTarget != NULL, "The target survives the source being destroyed");
        CHECK(pFromTarget == NULL || *pFromTarget == nValues[i], "And still holds the right value");
    }

    XMap_Destroy(&target);

    /* Both arguments are checked. */
    xmap_t map;
    CHECK(XMap_Init(&map, NULL, 8) == XMAP_OK, "A map initializes");

    xmap_pair_t pair;
    pair.eStatus = XMAP_PAIR_USED;
    pair.pKey = (char*)"solo";
    pair.pData = &nValues[0];

    CHECK(XMap_PutPair(NULL, &pair) == XMAP_OINV, "A missing map is rejected");
    CHECK(XMap_PutPair(&map, NULL) == XMAP_OINV, "A missing pair is rejected");
    CHECK(map.nCount == 0, "A rejected put stored nothing");

    CHECK(XMap_PutPair(&map, &pair) == XMAP_OK, "A hand built pair is accepted");
    CHECK(XMap_Get(&map, "solo") == &nValues[0], "It is retrievable by its key");

    XMap_Destroy(&map);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(lifecycle),
    XTEST_CASE(reference_model),
    XTEST_CASE(operations),
    XTEST_CASE(iteration),
    XTEST_CASE(growth),
    XTEST_CASE(hashing),
    XTEST_CASE(pair_transfer)
)
