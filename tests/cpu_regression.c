/* libxutils: CPU count detection and scheduler affinity.
 *
 * Affinity is applied to the calling thread, and every case restores the
 * full mask afterwards so a narrowed affinity cannot leak into the rest of
 * the suite. Machines with a single core skip the manipulation cases: there
 * is no second CPU to move to, and clearing the only one is rejected by the
 * kernel rather than by the library.
 */

#include "test.h"
#include "cpu.h"

/* Puts every CPU back in the mask so later tests are not pinned. */
static int cpu_restore(int nCount)
{
    int *pAll = (int*)malloc(sizeof(int) * (size_t)nCount);
    if (pAll == NULL) return XSTDERR;
    for (int i = 0; i < nCount; i++) pAll[i] = i;

    int nStatus = XCPU_SetAffinity(pAll, (size_t)nCount, XCPU_CALLER_PID);
    free(pAll);
    return nStatus;
}

static int XTest_count(void)
{
    int nFirst = XCPU_GetCount();
    CHECK(nFirst > 0, "The machine reports at least one CPU");

    /* The count is cached after the first query, so it must not drift. */
    for (int i = 0; i < 16; i++)
        CHECK(XCPU_GetCount() == nFirst, "The CPU count is stable across calls");
    return 0;
}

static int XTest_affinity(void)
{
    int nCount = XCPU_GetCount();
    CHECK(nCount > 0, "The machine reports at least one CPU");

    if (nCount < 2)
    {
        printf("Single CPU machine, skipping affinity manipulation\n");
        return 77;
    }

    /* Pinning to one CPU, then widening and narrowing the mask again. */
    CHECK(XCPU_SetSingle(0, XCPU_CALLER_PID) == 0, "The thread pins to a single CPU");

    CHECK(XCPU_AddAffinity(1, XCPU_CALLER_PID) == 0, "A CPU outside the mask is added");
    CHECK(XCPU_AddAffinity(1, XCPU_CALLER_PID) == XSTDNON, "Adding a CPU already in the mask changes nothing");

    CHECK(XCPU_DelAffinity(1, XCPU_CALLER_PID) == 0, "A CPU inside the mask is removed");
    CHECK(XCPU_DelAffinity(1, XCPU_CALLER_PID) == XSTDNON, "Removing a CPU already out of the mask changes nothing");

    /* An explicit list of CPUs is applied as a whole. */
    int pair[] = {0, 1};
    CHECK(XCPU_SetAffinity(pair, 2, XCPU_CALLER_PID) == 0, "An explicit CPU list is applied");
    CHECK(XCPU_DelAffinity(1, XCPU_CALLER_PID) == 0, "The second CPU of the pair was really set");

    CHECK(cpu_restore(nCount) == 0, "The full affinity mask is restored");
    return 0;
}

static int XTest_guards(void)
{
    int nCount = XCPU_GetCount();
    CHECK(nCount > 0, "The machine reports at least one CPU");

    CHECK(XCPU_SetAffinity(NULL, 4, XCPU_CALLER_PID) == XSTDERR, "A missing CPU list is rejected");
    int single[] = {0};
    CHECK(XCPU_SetAffinity(single, 0, XCPU_CALLER_PID) == XSTDERR, "An empty CPU list is rejected");

    /* CPU indices outside the machine are refused before any syscall. */
    CHECK(XCPU_AddAffinity(-1, XCPU_CALLER_PID) == XSTDERR, "A negative CPU index is rejected");
    CHECK(XCPU_AddAffinity(nCount, XCPU_CALLER_PID) == XSTDERR, "A CPU index past the last CPU is rejected");
    CHECK(XCPU_DelAffinity(-1, XCPU_CALLER_PID) == XSTDERR, "A negative CPU index is rejected on removal");
    CHECK(XCPU_DelAffinity(nCount, XCPU_CALLER_PID) == XSTDERR, "A CPU index past the last CPU is rejected on removal");

    /* A list of only out of range CPUs builds an empty mask, which the
     * kernel refuses; the caller must see the failure. */
    int outOfRange[] = {nCount + 1, nCount + 2};
    CHECK(XCPU_SetAffinity(outOfRange, 2, XCPU_CALLER_PID) < 0, "An entirely out of range list fails");
    CHECK(XCPU_SetSingle(nCount + 5, XCPU_CALLER_PID) < 0, "Pinning to a CPU that does not exist fails");

    /* An out of range entry mixed with a valid one is dropped, not fatal. */
    int mixed[] = {0, nCount + 3};
    CHECK(XCPU_SetAffinity(mixed, 2, XCPU_CALLER_PID) == 0, "A valid entry survives an out of range neighbour");

    CHECK(cpu_restore(nCount) == 0, "The full affinity mask is restored");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(count),
    XTEST_CASE(affinity),
    XTEST_CASE(guards)
)
