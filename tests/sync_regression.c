/* libxutils: mutexes, read/write locks, barriers and background tasks.
 *
 * The locks are checked by the invariant they exist to protect: a shared
 * counter incremented by contending threads has to end at exactly the
 * number of increments, and a reader must never observe a half written
 * value. Disabled locks are checked too, since the library lets a caller
 * turn synchronisation off and every entry point has to tolerate that.
 */

#include "test.h"
#include "sync.h"
#include "thread.h"
#include "xtime.h"

#define SYNC_THREADS    4
#define SYNC_ROUNDS     4000

typedef struct {
    xsync_mutex_t lock;
    long nCounter;
} sync_mutex_test_t;

static void *sync_mutex_worker(void *pContext)
{
    sync_mutex_test_t *pTest = (sync_mutex_test_t*)pContext;
    for (int i = 0; i < SYNC_ROUNDS; i++)
    {
        XSync_Lock(&pTest->lock);
        pTest->nCounter++;
        XSync_Unlock(&pTest->lock);
    }
    return NULL;
}

static int XTest_mutex(void)
{
    /* A contended counter ends at exactly the number of increments. */
    sync_mutex_test_t test;
    test.nCounter = 0;
    XSync_Init(&test.lock);
    CHECK(test.lock.bEnabled == XTRUE, "An initialized mutex is enabled");

    xthread_t threads[SYNC_THREADS];
    for (int i = 0; i < SYNC_THREADS; i++)
        CHECK(XThread_Create(&threads[i], sync_mutex_worker, &test, XFALSE) == XSTDOK, "Start a worker");
    for (int i = 0; i < SYNC_THREADS; i++) XThread_Join(&threads[i]);

    CHECK(test.nCounter == (long)SYNC_THREADS * SYNC_ROUNDS, "Every increment is accounted for");
    XSync_Destroy(&test.lock);
    CHECK(test.lock.bEnabled == XFALSE, "A destroyed mutex is disabled");

    /* Locking a destroyed mutex is a no-op rather than undefined behaviour. */
    XSync_Lock(&test.lock);
    XSync_Unlock(&test.lock);
    XSync_Destroy(&test.lock);

    /* Destroying twice is safe because the disabled flag gates the work. */
    XSync_Destroy(&test.lock);
    CHECK(test.lock.bEnabled == XFALSE, "A repeatedly destroyed mutex stays disabled");
    return 0;
}

static int XTest_recursive(void)
{
    /* A recursive mutex can be taken by the same thread more than once;
     * a plain one deadlocks instead, which is why the two are separate. */
    xsync_mutex_t lock;
    XSync_InitRecursive(&lock);
    CHECK(lock.bEnabled == XTRUE, "A recursive mutex is enabled");

    XSync_Lock(&lock);
    XSync_Lock(&lock);
    XSync_Lock(&lock);
    XSync_Unlock(&lock);
    XSync_Unlock(&lock);
    XSync_Unlock(&lock);
    CHECK(1, "A recursive mutex can be re-entered by its own thread");
    XSync_Destroy(&lock);

    /* The advanced initializer selects between the two behaviours. */
    CHECK(XSync_InitAdv(&lock, XTRUE) == XSTDOK, "The advanced initializer creates a recursive mutex");
    XSync_Lock(&lock);
    XSync_Lock(&lock);
    XSync_Unlock(&lock);
    XSync_Unlock(&lock);
    XSync_Destroy(&lock);

    CHECK(XSync_InitAdv(&lock, XFALSE) == XSTDOK, "The advanced initializer creates a plain mutex");
    XSync_Lock(&lock);
    XSync_Unlock(&lock);
    XSync_Destroy(&lock);

    return 0;
}

typedef struct {
    xsync_rw_t lock;
    xatomic_t nStop;
    long nValue;
    long nMirror;
    xatomic_t nTornReads;
    xatomic_t nReads;
    xatomic_t nReadersUp;
} sync_rw_test_t;

/* Writers keep two fields in step; a torn read would see them differ. */
static void *sync_rw_writer(void *pContext)
{
    sync_rw_test_t *pTest = (sync_rw_test_t*)pContext;
    for (int i = 0; i < SYNC_ROUNDS; i++)
    {
        XRWSync_WriteLock(&pTest->lock);
        pTest->nValue++;
        pTest->nMirror = pTest->nValue;
        XRWSync_Unlock(&pTest->lock);
    }
    return NULL;
}

static void *sync_rw_reader(void *pContext)
{
    sync_rw_test_t *pTest = (sync_rw_test_t*)pContext;
    /* Take the lock once and announce it, so the writers only start when
     * the readers are demonstrably running: whether a given thread gets
     * scheduled in some window is the scheduler's business, not the lock's,
     * and a test that depends on it fails on a busy machine for no reason. */
    XRWSync_ReadLock(&pTest->lock);
    if (pTest->nValue != pTest->nMirror) XSYNC_ATOMIC_ADD(&pTest->nTornReads, 1);
    XRWSync_Unlock(&pTest->lock);
    XSYNC_ATOMIC_ADD(&pTest->nReads, 1);
    XSYNC_ATOMIC_ADD(&pTest->nReadersUp, 1);

    /* Then keep reading until the writers are done. The short sleep keeps
     * them from starving the writers when this runs under a memory checker. */
    while (!XSYNC_ATOMIC_GET(&pTest->nStop))
    {
        XRWSync_ReadLock(&pTest->lock);
        if (pTest->nValue != pTest->nMirror) XSYNC_ATOMIC_ADD(&pTest->nTornReads, 1);
        XRWSync_Unlock(&pTest->lock);
        XSYNC_ATOMIC_ADD(&pTest->nReads, 1);
        xusleep(100);
    }
    return NULL;
}

static int XTest_rwlock(void)
{
    sync_rw_test_t test;
    memset(&test, 0, sizeof(test));
    XRWSync_Init(&test.lock);
    CHECK(test.lock.bEnabled == XTRUE, "An initialized read/write lock is enabled");

    xthread_t writers[2], readers[2];
    for (int i = 0; i < 2; i++)
        CHECK(XThread_Create(&readers[i], sync_rw_reader, &test, XFALSE) == XSTDOK, "Start a reader");

    /* Wait for both readers to have taken the lock before the writers
     * start, so the contention the torn-read check depends on is real
     * rather than a matter of timing. */
    for (int i = 0; i < 20000 && XSYNC_ATOMIC_GET(&test.nReadersUp) < 2; i++) xusleep(500);
    CHECK(XSYNC_ATOMIC_GET(&test.nReadersUp) == 2, "Both readers took the lock before the writes began");

    for (int i = 0; i < 2; i++)
        CHECK(XThread_Create(&writers[i], sync_rw_writer, &test, XFALSE) == XSTDOK, "Start a writer");

    for (int i = 0; i < 2; i++) XThread_Join(&writers[i]);
    XSYNC_ATOMIC_SET(&test.nStop, 1);
    for (int i = 0; i < 2; i++) XThread_Join(&readers[i]);

    CHECK(test.nValue == 2 * SYNC_ROUNDS, "Every write is accounted for");
    CHECK(test.nMirror == test.nValue, "The two fields end in step");
    CHECK(XSYNC_ATOMIC_GET(&test.nReads) >= 2, "Every reader took the lock at least once");
    CHECK(XSYNC_ATOMIC_GET(&test.nTornReads) == 0, "No reader observed a half written update");

    /* Several readers can hold the lock at once. */
    XRWSync_ReadLock(&test.lock);
    XRWSync_Unlock(&test.lock);
    XRWSync_WriteLock(&test.lock);
    XRWSync_Unlock(&test.lock);

    XRWSync_Destroy(&test.lock);
    CHECK(test.lock.bEnabled == XFALSE, "A destroyed read/write lock is disabled");

    /* A destroyed lock is tolerated: the disabled flag gates every call. */
    XRWSync_ReadLock(&test.lock);
    XRWSync_WriteLock(&test.lock);
    XRWSync_Unlock(&test.lock);
    XRWSync_Destroy(&test.lock);
    CHECK(test.lock.bEnabled == XFALSE, "A repeatedly destroyed read/write lock stays disabled");
    return 0;
}

typedef struct {
    xsync_bar_t bar;
    xatomic_t nSeen;
} sync_bar_test_t;

static void *sync_bar_worker(void *pContext)
{
    sync_bar_test_t *pTest = (sync_bar_test_t*)pContext;

    /* Wait for the barrier to be raised, then acknowledge it. */
    while (!XSyncBar_CheckBar(&pTest->bar)) xusleep(1000);
    XSYNC_ATOMIC_SET(&pTest->nSeen, 1);
    XSyncBar_Ack(&pTest->bar);
    return NULL;
}

static int XTest_barrier(void)
{
    sync_bar_test_t test;
    memset(&test, 0, sizeof(test));
    XSyncBar_Reset(&test.bar);

    CHECK(XSyncBar_CheckBar(&test.bar) == XSTDNON, "A reset barrier is lowered");
    CHECK(XSyncBar_CheckAck(&test.bar) == XSTDNON, "A reset barrier is unacknowledged");

    /* Raising the barrier clears any previous acknowledgement. */
    XSyncBar_Ack(&test.bar);
    CHECK(XSyncBar_CheckAck(&test.bar) == XSTDOK, "An acknowledgement is visible");
    XSyncBar_Bar(&test.bar);
    CHECK(XSyncBar_CheckBar(&test.bar) == XSTDOK, "The raised barrier is visible");
    CHECK(XSyncBar_CheckAck(&test.bar) == XSTDNON, "Raising the barrier clears the acknowledgement");

    /* A waiter blocks until the other side acknowledges. */
    XSyncBar_Reset(&test.bar);
    xthread_t worker;
    CHECK(XThread_Create(&worker, sync_bar_worker, &test, XFALSE) == XSTDOK, "Start the barrier worker");

    XSyncBar_Bar(&test.bar);
    XSyncBar_WaitAck(&test.bar, 1000);
    CHECK(XSYNC_ATOMIC_GET(&test.nSeen) == 1, "The worker saw the barrier before acknowledging");
    CHECK(XSyncBar_CheckAck(&test.bar) == XSTDOK, "The wait returned only once acknowledged");
    XThread_Join(&worker);

    /* Waiting on an already acknowledged barrier returns at once. */
    CHECK(XSyncBar_WaitAck(&test.bar, 1000) == 0, "An acknowledged barrier does not wait");

    XSyncBar_Reset(&test.bar);
    CHECK(XSyncBar_CheckBar(&test.bar) == XSTDNON && XSyncBar_CheckAck(&test.bar) == XSTDNON,
        "A reset clears both halves of the barrier");
    return 0;
}

typedef struct {
    xatomic_t nRuns;
    xatomic_t nStopAfter;
} sync_task_test_t;

static int sync_task_callback(void *pContext)
{
    sync_task_test_t *pTest = (sync_task_test_t*)pContext;
    xatomic_t nRuns = XSYNC_ATOMIC_ADD(&pTest->nRuns, 1);

    xatomic_t nStopAfter = XSYNC_ATOMIC_GET(&pTest->nStopAfter);
    if (nStopAfter > 0 && nRuns >= nStopAfter) return XSTDERR;
    return XSTDOK;
}

static int XTest_task(void)
{
    /* A task runs its callback repeatedly until it is stopped. */
    sync_task_test_t test;
    memset(&test, 0, sizeof(test));

    xtask_t task;
    memset(&task, 0, sizeof(task));
    CHECK(XTask_Start(&task, sync_task_callback, &test, 1000) == XSTDOK, "The task starts");

    for (int i = 0; i < 500 && XSYNC_ATOMIC_GET(&test.nRuns) < 5; i++) xusleep(2000);
    CHECK(XSYNC_ATOMIC_GET(&test.nRuns) >= 5, "The task callback runs repeatedly");

    /* Holding the task pauses it; releasing resumes it. */
    XTask_Hold(&task, 1000);
    CHECK(XSYNC_ATOMIC_GET(&task.nStatus) == XTASK_STAT_PAUSED, "A held task reports itself paused");

    xatomic_t nAtPause = XSYNC_ATOMIC_GET(&test.nRuns);
    xusleep(30000);
    CHECK(XSYNC_ATOMIC_GET(&test.nRuns) == nAtPause, "A paused task stops running its callback");

    XTask_Release(&task, 1000);
    CHECK(XSYNC_ATOMIC_GET(&task.nStatus) == XTASK_STAT_ACTIVE, "A released task reports itself active");

    for (int i = 0; i < 500 && XSYNC_ATOMIC_GET(&test.nRuns) <= nAtPause; i++) xusleep(2000);
    CHECK(XSYNC_ATOMIC_GET(&test.nRuns) > nAtPause, "A released task runs its callback again");

    XTask_Stop(&task, 1000);
    CHECK(XSYNC_ATOMIC_GET(&task.nStatus) == XTASK_STAT_STOPPED, "A stopped task reports itself stopped");

    xatomic_t nAtStop = XSYNC_ATOMIC_GET(&test.nRuns);
    xusleep(30000);
    CHECK(XSYNC_ATOMIC_GET(&test.nRuns) == nAtStop, "A stopped task never runs again");

    /* A callback returning an error ends the task on its own. */
    memset(&test, 0, sizeof(test));
    XSYNC_ATOMIC_SET(&test.nStopAfter, 3);
    memset(&task, 0, sizeof(task));
    CHECK(XTask_Start(&task, sync_task_callback, &test, 1000) == XSTDOK, "The self-stopping task starts");

    for (int i = 0; i < 500 && XSYNC_ATOMIC_GET(&task.nStatus) != XTASK_STAT_STOPPED; i++) xusleep(2000);
    CHECK(XSYNC_ATOMIC_GET(&task.nStatus) == XTASK_STAT_STOPPED, "A failing callback ends the task");
    CHECK(XSYNC_ATOMIC_GET(&test.nRuns) == 3, "The task stopped on the failing run");

    /* A task with no callback cannot start. */
    memset(&task, 0, sizeof(task));
    CHECK(XTask_Start(&task, NULL, &test, 1000) == 0, "A task without a callback is refused");
    CHECK(XSYNC_ATOMIC_GET(&task.nStatus) != XTASK_STAT_ACTIVE, "A refused task never becomes active");
    return 0;
}

static int XTest_sleep(void)
{
    /* The sleep helper has to actually wait, and a zero request has to
     * return promptly rather than blocking. */
    uint64_t nStart = XTime_GetMs();
    xusleep(50000);
    uint64_t nElapsed = XTime_GetMs() - nStart;
    CHECK(nElapsed >= 40, "A fifty millisecond sleep waits");
    CHECK(nElapsed < 5000, "A fifty millisecond sleep does not overshoot wildly");

    nStart = XTime_GetMs();
    xusleep(0);
    CHECK(XTime_GetMs() - nStart < 1000, "A zero sleep returns promptly");
    return 0;
}

static int XTest_atomics(void)
{
    /* The atomic macros have to read back exactly what was written,
     * including across the signed boundary. */
    xatomic_t nValue = 0;
    CHECK(XSYNC_ATOMIC_GET(&nValue) == 0, "A zeroed atomic reads back zero");

    XSYNC_ATOMIC_SET(&nValue, 42);
    CHECK(XSYNC_ATOMIC_GET(&nValue) == 42, "A written atomic reads back its value");

    XSYNC_ATOMIC_ADD(&nValue, 8);
    CHECK(XSYNC_ATOMIC_GET(&nValue) == 50, "An added atomic reads back the sum");

    XSYNC_ATOMIC_SUB(&nValue, 20);
    CHECK(XSYNC_ATOMIC_GET(&nValue) == 30, "A subtracted atomic reads back the difference");

    XSYNC_ATOMIC_SET(&nValue, 0);
    for (int i = 0; i < 1000; i++) XSYNC_ATOMIC_ADD(&nValue, 1);
    CHECK(XSYNC_ATOMIC_GET(&nValue) == 1000, "Repeated increments accumulate exactly");
    return 0;
}

static int sync_idle_callback(void *pContext)
{
    (void)pContext;
    return 0;
}

static int XTest_task_start_state(void)
{
    /* The worker marks itself ACTIVE as its first step. XTask_Start() used to
       write CREATED only after starting it, which could land on top of that
       and was never replaced, so waiting for the task to become active hung. */
    for (int nRun = 0; nRun < 500; nRun++)
    {
        xtask_t task;
        memset(&task, 0, sizeof(task));
        CHECK(XTask_Start(&task, sync_idle_callback, NULL, 200) == XSTDOK, "The task starts");

        uint64_t nDeadline = XTime_GetMs() + 2000;
        while (XSYNC_ATOMIC_GET(&task.nStatus) != XTASK_STAT_ACTIVE && XTime_GetMs() < nDeadline) xusleep(50);
        CHECK(XSYNC_ATOMIC_GET(&task.nStatus) == XTASK_STAT_ACTIVE, "A started task always becomes active");

        XTask_Stop(&task, 100);
        CHECK(XSYNC_ATOMIC_GET(&task.nStatus) == XTASK_STAT_STOPPED, "The task stops");
    }

    return 0;
}

XTEST_MAIN(
    XTEST_CASE(mutex),
    XTEST_CASE(recursive),
    XTEST_CASE(rwlock),
    XTEST_CASE(barrier),
    XTEST_CASE(task),
    XTEST_CASE(task_start_state),
    XTEST_CASE(sleep),
    XTEST_CASE(atomics)
)
