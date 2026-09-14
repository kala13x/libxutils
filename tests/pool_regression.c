/* libxutils: pool alignment, growth, lifetime and overflow. */
#include "test.h"
#include "pool.h"

static int XTest_alignment_growth(void)
{
    xpool_t pool;
    CHECK(XPool_Init(&pool, 17) == XSTDOK, "Initialize an odd-sized pool");
    uint8_t *blocks[64];
    for (size_t i = 0; i < 64; i++)
    {
        blocks[i] = (uint8_t*)XPool_Alloc(&pool, i + 1);
        CHECK(blocks[i] && (uintptr_t)blocks[i] % XPOOL_ALIGNMENT == 0, "Every allocation respects pool alignment");
        memset(blocks[i], (int)i, i + 1);
    }
    for (size_t i = 0; i < 64; i++)
        for (size_t j = 0; j <= i; j++) CHECK(blocks[i][j] == i, "Growth never overlaps or invalidates an earlier allocation");
    size_t nSize = XPool_GetSize(&pool);
    CHECK(nSize >= XPool_GetUsed(&pool) && pool.pNext, "Account for chained storage");
    XPool_Reset(&pool);
    CHECK(XPool_GetUsed(&pool) == 0 && XPool_GetSize(&pool) == nSize, "Reset retains allocated chunks for reuse");
    CHECK(XPool_Alloc(&pool, 16) != NULL && XPool_GetSize(&pool) == nSize, "Reuse avoids unnecessary growth");
    XPool_Destroy(&pool);
    XPool_Destroy(&pool);
    return 0;
}

static int XTest_reallocation(void)
{
    xpool_t pool;
    CHECK(XPool_Init(&pool, 128) == XSTDOK, "Initialize reallocation fixture");
    uint8_t *pFirst = (uint8_t*)XPool_Alloc(&pool, 9);
    CHECK(pFirst != NULL, "Allocate a tail block");
    memset(pFirst, 0x5a, 9);
    CHECK(XPool_Realloc(&pool, pFirst, 9, 31) == pFirst, "A tail allocation grows in place when space exists");
    CHECK(XPool_Alloc(&pool, 8) != NULL, "Place another live block after the first");
    uint8_t *pMoved = (uint8_t*)XPool_Realloc(&pool, pFirst, 31, 257);
    CHECK(pMoved && pMoved != pFirst, "A middle allocation grows into independent storage");
    CHECK(memcmp(pMoved, "ZZZZZZZZZ", 9) == 0, "Moved growth retains old bytes");
    size_t nUsed = XPool_GetUsed(&pool);
    int foreign;
    XPool_Free(&pool, &foreign, sizeof(foreign));
    CHECK(XPool_GetUsed(&pool) == nUsed, "Foreign pointers do not affect pool accounting");
    XPool_Free(&pool, pMoved, 257);
    CHECK(XPool_GetUsed(&pool) < nUsed, "Freeing the tail reclaims its space");
    XPool_Destroy(&pool);
    return 0;
}

static int XTest_overflow(void)
{
    xpool_t *pPool = XPool_Create(16);
    CHECK(pPool != NULL, "Allocate owned pool");
    CHECK(XPool_Alloc(pPool, SIZE_MAX) == NULL && XPool_Alloc(pPool, 0) == NULL, "Reject alignment overflow and zero allocation");
    CHECK(XPool_GetUsed(pPool) == 0, "Rejected allocations leave the pool untouched");
    CHECK(XPool_Create(SIZE_MAX) == NULL, "Reject impossible pool capacity");
    XPool_Destroy(pPool);
    uint8_t *pHeap = (uint8_t*)xalloc(NULL, 8);
    CHECK(pHeap != NULL, "NULL-pool allocation uses normal heap ownership");
    memset(pHeap, 0x7a, 8);
    pHeap = (uint8_t*)xrealloc(NULL, pHeap, 8, 64);
    CHECK(pHeap && pHeap[7] == 0x7a, "NULL-pool resize retains bytes");
    xfreen(NULL, pHeap, 64);
    return 0;
}

XTEST_MAIN(XTEST_CASE(alignment_growth), XTEST_CASE(reallocation), XTEST_CASE(overflow))
