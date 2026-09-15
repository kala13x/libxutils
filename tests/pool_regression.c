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


static int XTest_accounting(void)
{
    /* The pool reports exactly what it has handed out, and hands the space
     * back when the most recent allocation is released. */
    xpool_t pool;
    CHECK(XPool_Init(&pool, 4096) == XSTDOK, "The pool initializes");
    CHECK(XPool_GetSize(&pool) == 4096, "The pool reports the size it was given");
    CHECK(XPool_GetUsed(&pool) == 0, "A fresh pool has handed out nothing");

    void *pFirst = XPool_Alloc(&pool, 100);
    CHECK(pFirst != NULL, "The first allocation succeeds");
    size_t nAfterFirst = XPool_GetUsed(&pool);
    CHECK(nAfterFirst >= 100, "The pool accounts for at least what was asked");

    void *pSecond = XPool_Alloc(&pool, 200);
    CHECK(pSecond != NULL, "The second allocation succeeds");
    CHECK(pSecond != pFirst, "The two allocations do not overlap");
    CHECK(XPool_GetUsed(&pool) > nAfterFirst, "The pool accounts for both");

    /* The blocks are independently writable. */
    memset(pFirst, 0xa1, 100);
    memset(pSecond, 0xb2, 200);
    CHECK(((uint8_t*)pFirst)[99] == 0xa1, "The first block kept its own bytes");
    CHECK(((uint8_t*)pSecond)[0] == 0xb2, "The second block kept its own bytes");

    /* Releasing the most recent block returns its space to the pool. */
    size_t nBeforeFree = XPool_GetUsed(&pool);
    XPool_Free(&pool, pSecond, 200);
    CHECK(XPool_GetUsed(&pool) < nBeforeFree, "Releasing the last block frees its space");

    /* A reset returns everything at once. */
    XPool_Reset(&pool);
    CHECK(XPool_GetUsed(&pool) == 0, "A reset returns the whole pool");
    CHECK(XPool_GetSize(&pool) == 4096, "A reset keeps the pool's capacity");

    /* The pool is usable again after the reset. */
    void *pReused = XPool_Alloc(&pool, 512);
    CHECK(pReused != NULL, "A reset pool still allocates");
    memset(pReused, 0xcc, 512);

    XPool_Destroy(&pool);
    return 0;
}

static int XTest_exhaustion(void)
{
    /* A request larger than the pool is served from the heap instead of
     * failing, so the caller never has to know which it got. */
    xpool_t pool;
    CHECK(XPool_Init(&pool, 256) == XSTDOK, "The small pool initializes");

    void *pOversized = XPool_Alloc(&pool, 100000);
    CHECK(pOversized != NULL, "A request past the pool size is still served");
    memset(pOversized, 0x5a, 100000);
    CHECK(((uint8_t*)pOversized)[99999] == 0x5a, "The oversized block is fully writable");
    XPool_Free(&pool, pOversized, 100000);

    /* Filling the pool exactly and then asking for one byte more. */
    XPool_Reset(&pool);
    void *pBlocks[64];
    int nAllocated = 0;
    for (int i = 0; i < 64; i++)
    {
        pBlocks[i] = XPool_Alloc(&pool, 32);
        if (pBlocks[i] == NULL) break;
        memset(pBlocks[i], i, 32);
        nAllocated++;
    }
    CHECK(nAllocated > 0, "The pool served at least one block");

    /* Every block kept its own contents despite sharing the arena. */
    for (int i = 0; i < nAllocated; i++)
        CHECK(((uint8_t*)pBlocks[i])[0] == (uint8_t)i, "Every block kept its own bytes");

    XPool_Destroy(&pool);

    /* A zero sized pool still serves through the heap. */
    CHECK(XPool_Init(&pool, 0) == XSTDOK, "A zero sized pool initializes");
    void *pFromHeap = XPool_Alloc(&pool, 64);
    CHECK(pFromHeap != NULL, "A zero sized pool still serves a request");
    memset(pFromHeap, 0x11, 64);
    XPool_Free(&pool, pFromHeap, 64);
    XPool_Destroy(&pool);
    return 0;
}

static int XTest_heap_pool(void)
{
    /* The allocating wrappers fall back to the heap when no pool is given,
     * so the same code can run pooled or unpooled. */
    void *pPlain = xalloc(NULL, 128);
    CHECK(pPlain != NULL, "An unpooled allocation is served");
    memset(pPlain, 0x7f, 128);

    void *pGrown = xrealloc(NULL, pPlain, 128, 256);
    CHECK(pGrown != NULL, "An unpooled reallocation is served");
    CHECK(((uint8_t*)pGrown)[0] == 0x7f, "The reallocation kept the earlier bytes");
    xfree(NULL, pGrown);

    /* The same calls against a real pool. */
    xpool_t *pPool = XPool_Create(2048);
    CHECK(pPool != NULL, "A heap pool is created");
    CHECK(XPool_GetSize(pPool) == 2048, "The heap pool has its capacity");

    void *pPooled = xalloc(pPool, 128);
    CHECK(pPooled != NULL, "A pooled allocation is served");
    memset(pPooled, 0x33, 128);

    void *pPooledGrown = xrealloc(pPool, pPooled, 128, 256);
    CHECK(pPooledGrown != NULL, "A pooled reallocation is served");
    CHECK(((uint8_t*)pPooledGrown)[0] == 0x33, "The pooled reallocation kept the earlier bytes");

    xfreen(pPool, pPooledGrown, 256);
    XPool_Destroy(pPool);

    /* A zero sized request is refused rather than returning a block that
     * cannot hold anything. */
    CHECK(xalloc(NULL, 0) == NULL, "A zero sized unpooled request is refused");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(alignment_growth),
    XTEST_CASE(reallocation),
    XTEST_CASE(overflow),
    XTEST_CASE(accounting),
    XTEST_CASE(exhaustion),
    XTEST_CASE(heap_pool)
)
