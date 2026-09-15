/* libxutils: aliasing, fixed storage, pointer ownership and queue wraparound. */
#include "test.h"
#include "buf.h"

static int XTest_aliasing(void)
{
    for (int nFast = 0; nFast < 2; nFast++)
    {
        xbyte_buffer_t buffer;
        XByteBuffer_Init(&buffer, 0, nFast);
        CHECK(XByteBuffer_Add(&buffer, (const uint8_t*)"abcd", 4) == 4, "Initialize aliased buffer");
        CHECK(XByteBuffer_Resize(&buffer, 5) == 5, "Force the next append to grow");
        CHECK(XByteBuffer_Add(&buffer, buffer.pData + 1, 3) == 7, "Append an interior slice while reallocating");
        CHECK(buffer.nUsed == 7 && memcmp(buffer.pData, "abcdbcd", 7) == 0, "Aliased append preserves the original slice");
        CHECK(XByteBuffer_Insert(&buffer, 2, buffer.pData + 1, 4) > 0, "Insert a slice across the insertion point");
        CHECK(buffer.nUsed == 11, "Insertion has the expected length");
        CHECK(memcmp(buffer.pData, "abbcdbcdbcd", 11) == 0, "Overlapping insertion copies the original bytes");
        CHECK(XByteBuffer_Add(&buffer, buffer.pData, SIZE_MAX) < 0, "Reject addition overflow before reading source");
        CHECK(buffer.nUsed == 11, "Failed growth preserves used length");
        CHECK(XByteBuffer_Remove(&buffer, 2, SIZE_MAX) == 9 && buffer.nUsed == 2, "Oversized removal clamps without overflow");
        XByteBuffer_Clear(&buffer);
    }
    return 0;
}

static int XTest_borrowed(void)
{
    uint8_t data[4] = {'a', 0, 'b', 0x5a};
    xbyte_buffer_t borrowed;
    XByteBuffer_Init(&borrowed, 0, XFALSE);
    CHECK(XByteBuffer_SetData(&borrowed, data, 3) > 0, "Borrow exact binary storage");
    CHECK(XByteBuffer_Terminate(&borrowed, 3) < 0 && data[3] == 0x5a, "Termination must not overrun borrowed capacity");
    XByteBuffer_Clear(&borrowed);
    CHECK(memcmp(data, "a\0bZ", sizeof(data)) == 0, "Clearing a view does not free or mutate the source");
    CHECK(XByteData_Dup(data, SIZE_MAX) == NULL, "Reject impossible duplicate length");
    return 0;
}

static int nCleared;
static void XTest_ClearData(void *pData)
{
    if (pData != NULL) nCleared++;
    free(pData);
}

static int XTest_pointer_ownership(void)
{
    xdata_buffer_t buffer;
    CHECK(XDataBuffer_Init(&buffer, 2, XTRUE) > 0, "Initialize fixed pointer storage");
    buffer.clearCb = XTest_ClearData;
    nCleared = 0;
    int *pFirst = (int*)malloc(sizeof(int)), *pSecond = (int*)malloc(sizeof(int));
    CHECK(pFirst && pSecond, "Allocate owned pointers");
    CHECK(XDataBuffer_Add(&buffer, pFirst) == 0 && XDataBuffer_Add(&buffer, pSecond) == 1, "Fill fixed pointer buffer");
    CHECK(XDataBuffer_Add(&buffer, pFirst) < 0 && buffer.nUsed == 2, "Full buffer rejects additional ownership");
    CHECK(XDataBuffer_Set(&buffer, UINT_MAX, pFirst) == NULL && buffer.nUsed == 2, "Invalid index preserves the container");
    CHECK(XDataBuffer_Pop(&buffer, 0) == pFirst && nCleared == 0, "Pop transfers ownership without cleanup");
    free(pFirst);
    CHECK(XDataBuffer_Get(&buffer, 0) == pSecond, "Pop compacts subsequent pointers");
    XDataBuffer_Destroy(&buffer);
    CHECK(nCleared == 1, "Destroy clears only the remaining owned pointer");
    CHECK(XDataBuffer_Init(&buffer, SIZE_MAX, 0) < 0, "Reject pointer-allocation multiplication overflow");
    XDataBuffer_Destroy(&buffer);
    return 0;
}

static int XTest_ring_model(void)
{
    for (size_t nCapacity = 1; nCapacity <= 7; nCapacity++)
    {
        xring_buffer_t ring;
        CHECK(XRingBuffer_Init(&ring, nCapacity) == (int)nCapacity, "Initialize a bounded ring");
        uint8_t reference[7];
        size_t nUsed = 0;
        uint32_t nSeed = 76531;
        for (size_t nStep = 0; nStep < 1000; nStep++)
        {
            nSeed = nSeed * 1664525u + 1013904223u;
            uint8_t nByte = (uint8_t)(nSeed >> 16), output = 0;
            if (nSeed % 3)
            {
                CHECK(XRingBuffer_AddDataAdv(&ring, &nByte, 1) == 1, "Advancing push accepts one packet");
                if (nUsed == nCapacity)
                {
                    nUsed--;
                    memmove(reference, reference + 1, nUsed);
                }
                reference[nUsed++] = nByte;
            }
            else
            {
                CHECK(XRingBuffer_Pop(&ring, &output, 1) == (nUsed ? 1 : 0), "Pop status agrees with queue model");
                if (nUsed)
                {
                    CHECK(output == reference[0], "Wraparound retains FIFO order");
                    nUsed--;
                    memmove(reference, reference + 1, nUsed);
                }
            }
            CHECK(ring.nUsed == nUsed, "Ring occupancy agrees after every operation");
        }
        XRingBuffer_Destroy(&ring);
    }
    return 0;
}


static int XTest_growth(void)
{
    /* A buffer grows on demand and keeps a spare byte for the terminator,
     * so the stored bytes are always usable as a C string too. */
    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, 0, 0);
    CHECK(XByteBuffer_HasData(&buffer) == XFALSE, "A fresh buffer holds nothing");
    CHECK(buffer.nUsed == 0 && buffer.pData == NULL, "A fresh buffer has no storage");

    CHECK(XByteBuffer_Add(&buffer, (const uint8_t*)"0123456789", 10) == 10, "The add reports the new length");
    CHECK(buffer.nUsed == 10, "The buffer holds what was added");
    CHECK(buffer.nSize > buffer.nUsed, "The capacity leaves room for a terminator");
    CHECK(buffer.pData[buffer.nUsed] == '\0', "The stored bytes are terminated");
    CHECK(XByteBuffer_HasData(&buffer) == XTRUE, "A filled buffer reports its data");

    /* Repeated appends accumulate in order. */
    for (int i = 0; i < 100; i++)
        CHECK(XByteBuffer_AddByte(&buffer, (uint8_t)('a' + i % 26)) > 0, "Every byte appends");
    CHECK(buffer.nUsed == 110, "Every appended byte is counted");
    CHECK(memcmp(buffer.pData, "0123456789", 10) == 0, "Growth preserves the earlier bytes");
    CHECK(buffer.pData[10] == 'a' && buffer.pData[11] == 'b', "Growth preserves the append order");

    /* Reserving grows the capacity without changing the contents. */
    size_t nBefore = buffer.nUsed;
    CHECK(XByteBuffer_Reserve(&buffer, 4096) > 0, "The buffer reserves more room");
    CHECK(buffer.nSize >= nBefore + 4096, "The reservation is at least what was asked for");
    CHECK(buffer.nUsed == nBefore, "Reserving does not change the length");

    /* Resizing down to the used length keeps the contents. */
    CHECK(XByteBuffer_Resize(&buffer, buffer.nUsed + 1) > 0, "The buffer resizes down");
    CHECK(buffer.nUsed == nBefore, "Resizing down to the length keeps every byte");
    CHECK(memcmp(buffer.pData, "0123456789", 10) == 0, "Resizing down keeps the contents");

    /* A reset empties the buffer but keeps its storage; a clear releases it. */
    size_t nCapacity = buffer.nSize;
    XByteBuffer_Reset(&buffer);
    CHECK(buffer.nUsed == 0, "A reset empties the buffer");
    CHECK(buffer.nSize == nCapacity && buffer.pData != NULL, "A reset keeps the storage");

    XByteBuffer_Clear(&buffer);
    CHECK(buffer.nUsed == 0 && buffer.nSize == 0, "A clear releases the storage");
    CHECK(buffer.pData == NULL, "A cleared buffer has no storage");

    /* Clearing twice is safe. */
    XByteBuffer_Clear(&buffer);
    CHECK(buffer.pData == NULL, "A repeatedly cleared buffer stays empty");
    return 0;
}

static int XTest_edit(void)
{
    /* Inserting in the middle shifts the tail; inserting past the end is
     * an append rather than a gap. */
    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, 0, 0);
    CHECK(XByteBuffer_Add(&buffer, (const uint8_t*)"0123456789", 10) == 10, "The buffer is filled");

    CHECK(XByteBuffer_Insert(&buffer, 5, (const uint8_t*)"XY", 2) == 12, "An insert reports the new length");
    CHECK(memcmp(buffer.pData, "01234XY56789", 12) == 0, "The insert shifts the tail out of the way");

    CHECK(XByteBuffer_Insert(&buffer, 0, (const uint8_t*)"<", 1) == 13, "An insert at the start succeeds");
    CHECK(buffer.pData[0] == '<', "The insert at the start lands first");

    CHECK(XByteBuffer_Insert(&buffer, 999, (const uint8_t*)">", 1) == 14, "An insert past the end appends");
    CHECK(buffer.pData[13] == '>', "The out of range insert landed at the end");

    /* Removing takes bytes out and reports how many it took. */
    CHECK(XByteBuffer_Remove(&buffer, 0, 1) == 1, "A removal reports what it took");
    CHECK(buffer.pData[0] == '0', "The removal closed the gap at the start");

    CHECK(XByteBuffer_Remove(&buffer, 5, 2) == 2, "A middle removal reports what it took");
    CHECK(memcmp(buffer.pData, "0123456789>", 11) == 0, "The middle removal closed the gap");

    /* A removal longer than the remainder is clamped to it. */
    CHECK(XByteBuffer_Remove(&buffer, 8, 999) == 3, "An oversized removal is clamped to the remainder");
    CHECK(buffer.nUsed == 8, "The clamped removal took only what was there");
    CHECK(buffer.pData[buffer.nUsed] == '\0', "The shortened buffer is still terminated");

    /* A removal outside the buffer takes nothing. */
    CHECK(XByteBuffer_Remove(&buffer, 999, 1) == 0, "A removal past the end takes nothing");
    CHECK(XByteBuffer_Remove(&buffer, 0, 0) == 0, "A zero length removal takes nothing");
    CHECK(buffer.nUsed == 8, "A refused removal leaves the length alone");

    /* Advancing drops bytes from the front and shrinks the storage. */
    CHECK(XByteBuffer_Advance(&buffer, 3) == 5, "Advancing reports the remaining length");
    CHECK(memcmp(buffer.pData, "34567", 5) == 0, "Advancing drops the leading bytes");

    CHECK(XByteBuffer_Advance(&buffer, 999) == 0, "Advancing past the end empties the buffer");
    CHECK(buffer.nUsed == 0, "The emptied buffer has no length");

    /* Terminating cuts the buffer at a position. */
    CHECK(XByteBuffer_Add(&buffer, (const uint8_t*)"abcdef", 6) == 6, "The buffer is refilled");
    CHECK(XByteBuffer_Terminate(&buffer, 3) > 0, "The buffer terminates at a position");
    CHECK(buffer.nUsed == 3, "Terminating cuts the length");
    CHECK(memcmp(buffer.pData, "abc", 3) == 0, "Terminating keeps the leading bytes");
    CHECK(buffer.pData[3] == '\0', "The cut buffer is terminated");

    CHECK(XByteBuffer_NullTerm(&buffer) > 0, "The buffer null terminates");
    CHECK(buffer.pData[buffer.nUsed] == '\0', "The terminator is past the last byte");

    XByteBuffer_Clear(&buffer);
    return 0;
}

static int XTest_accessors(void)
{
    /* Reading a byte inside the buffer returns it; outside returns zero
     * rather than whatever is next in memory. */
    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, 0, 0);
    CHECK(XByteBuffer_Add(&buffer, (const uint8_t*)"ABC", 3) == 3, "The buffer is filled");

    CHECK(XByteBuffer_GetByte(&buffer, 0) == 'A', "The first byte is readable");
    CHECK(XByteBuffer_GetByte(&buffer, 2) == 'C', "The last byte is readable");
    CHECK(XByteBuffer_GetByte(&buffer, 3) == 0, "The byte past the end reads as zero");
    CHECK(XByteBuffer_GetByte(&buffer, 999) == 0, "A byte far past the end reads as zero");

    /* The formatted append builds onto what is already there. */
    CHECK(XByteBuffer_AddFmt(&buffer, "-%d-%s", 42, "x") > 0, "A formatted append succeeds");
    CHECK(memcmp(buffer.pData, "ABC-42-x", 8) == 0, "The formatted append lands after the existing bytes");

    /* Appending another buffer copies its bytes. */
    xbyte_buffer_t other;
    XByteBuffer_Init(&other, 0, 0);
    CHECK(XByteBuffer_Add(&other, (const uint8_t*)"tail", 4) == 4, "The other buffer is filled");
    CHECK(XByteBuffer_AddBuff(&buffer, &other) > 0, "One buffer appends onto another");
    CHECK(buffer.nUsed == 12, "The appended length is the sum");
    CHECK(memcmp(&buffer.pData[8], "tail", 4) == 0, "The appended bytes are the other buffer's");

    /* The source is untouched by the append. */
    CHECK(other.nUsed == 4, "Appending leaves the source alone");
    XByteBuffer_Clear(&other);
    CHECK(memcmp(&buffer.pData[8], "tail", 4) == 0, "The destination outlives the source");

    /* Rejected arguments write nothing. */
    size_t nBefore = buffer.nUsed;
    CHECK(XByteBuffer_Add(&buffer, NULL, 5) == 0, "A missing source adds nothing");
    CHECK(XByteBuffer_Add(&buffer, (const uint8_t*)"x", 0) == 0, "A zero length source adds nothing");
    CHECK(XByteBuffer_Insert(&buffer, 0, NULL, 5) == 0, "A missing insert source adds nothing");
    CHECK(buffer.nUsed == nBefore, "A rejected call leaves the length alone");

    XByteBuffer_Clear(&buffer);
    return 0;
}

static int XTest_ownership_transfer(void)
{
    /* Set points the destination at the source's bytes without copying,
     * so the source keeps them and the destination owns no storage. */
    xbyte_buffer_t source, target;
    XByteBuffer_Init(&source, 0, 0);
    CHECK(XByteBuffer_Add(&source, (const uint8_t*)"payload", 7) == 7, "The source is filled");

    XByteBuffer_Init(&target, 0, 0);
    CHECK(XByteBuffer_Set(&target, &source) == 7, "Set reports the length it points at");
    CHECK(target.pData == source.pData, "Set aliases the source rather than copying");
    CHECK(target.nSize == 0, "An aliasing target owns no storage of its own");
    CHECK(source.nUsed == 7, "Set leaves the source holding its bytes");

    /* Own moves the storage across and leaves the source empty. */
    xbyte_buffer_t owner;
    XByteBuffer_Init(&owner, 0, 0);
    uint8_t *pMoved = source.pData;
    CHECK(XByteBuffer_Own(&owner, &source) > 0, "Own takes the storage");
    CHECK(owner.pData == pMoved, "The moved storage is the same allocation");
    CHECK(owner.nUsed == 7 && owner.nSize > 0, "The new owner holds the bytes and the capacity");
    CHECK(source.pData == NULL && source.nUsed == 0, "The source gave up its storage");

    CHECK(memcmp(owner.pData, "payload", 7) == 0, "The moved bytes are unchanged");
    XByteBuffer_Clear(&owner);

    /* Owning a caller allocation makes the buffer responsible for it. */
    uint8_t *pRaw = (uint8_t*)malloc(16);
    CHECK(pRaw != NULL, "A caller allocation is made");
    memcpy(pRaw, "owned", 5);

    xbyte_buffer_t adopted;
    XByteBuffer_Init(&adopted, 0, 0);
    CHECK(XByteBuffer_OwnData(&adopted, pRaw, 5) > 0, "The caller allocation is adopted");
    CHECK(adopted.pData == pRaw, "The adopted buffer points at the caller allocation");
    CHECK(memcmp(adopted.pData, "owned", 5) == 0, "The adopted bytes are readable");
    XByteBuffer_Clear(&adopted);

    /* Duplicating bytes produces an independent allocation. */
    const uint8_t original[] = {'d', 'u', 'p', 0x00, 0xff};
    uint8_t *pDup = XByteData_Dup(original, sizeof(original));
    CHECK(pDup != NULL && pDup != original, "The duplicate is a separate allocation");
    CHECK(memcmp(pDup, original, sizeof(original)) == 0, "The duplicate holds every byte including the NUL");
    free(pDup);

    CHECK(XByteData_Dup(NULL, 8) == NULL, "A missing source duplicates to nothing");
    CHECK(XByteData_Dup(original, 0) == NULL, "A zero length source duplicates to nothing");
    return 0;
}

static int XTest_heap_buffer(void)
{
    /* The heap buffer owns itself and clears the caller pointer. */
    xbyte_buffer_t *pBuffer = XByteBuffer_New(64, 0);
    CHECK(pBuffer != NULL, "A heap buffer is allocated");
    CHECK(pBuffer->nSize >= 64, "The heap buffer starts at the requested capacity");
    CHECK(XByteBuffer_Add(pBuffer, (const uint8_t*)"heap", 4) == 4, "The heap buffer accepts data");

    XByteBuffer_Free(&pBuffer);
    CHECK(pBuffer == NULL, "Freeing clears the caller pointer");

    XByteBuffer_Free(&pBuffer);
    XByteBuffer_Free(NULL);

    /* A zero sized heap buffer is still usable. */
    pBuffer = XByteBuffer_New(0, 0);
    CHECK(pBuffer != NULL, "A zero sized heap buffer is allocated");
    CHECK(XByteBuffer_Add(pBuffer, (const uint8_t*)"grown", 5) == 5, "The zero sized buffer grows on demand");
    XByteBuffer_Free(&pBuffer);
    return 0;
}

static int XTest_data_buffer(void)
{
    /* The pointer buffer stores what it is given without copying. */
    xdata_buffer_t buffer;
    CHECK(XDataBuffer_Init(&buffer, 4, 0) > 0, "The pointer buffer initializes");

    int values[6] = {10, 20, 30, 40, 50, 60};
    for (size_t i = 0; i < 3; i++)
        CHECK(XDataBuffer_Add(&buffer, &values[i]) == (int)i, "Each pointer is added at the next index");
    CHECK(buffer.nUsed == 3, "Every added pointer is counted");

    CHECK(XDataBuffer_Get(&buffer, 0) == &values[0], "The first pointer is returned as stored");
    CHECK(XDataBuffer_Get(&buffer, 2) == &values[2], "The last pointer is returned as stored");
    CHECK(XDataBuffer_Get(&buffer, 3) == NULL, "A pointer past the end is not there");
    CHECK(XDataBuffer_Get(&buffer, 999) == NULL, "A pointer far past the end is not there");

    /* Growing past the initial capacity keeps the earlier pointers. */
    for (size_t i = 3; i < 6; i++)
        CHECK(XDataBuffer_Add(&buffer, &values[i]) >= 0, "The buffer grows past its initial size");
    CHECK(buffer.nUsed == 6, "Every pointer survived the growth");
    CHECK(XDataBuffer_Get(&buffer, 0) == &values[0], "Growth preserves the first pointer");
    CHECK(XDataBuffer_Get(&buffer, 5) == &values[5], "Growth preserves the last pointer");

    /* Replacing returns what was there before. */
    CHECK(XDataBuffer_Set(&buffer, 0, &values[5]) == &values[0], "Replacing returns the previous pointer");
    CHECK(XDataBuffer_Get(&buffer, 0) == &values[5], "The replacement is now stored");

    /* Popping removes and returns. */
    void *pPopped = XDataBuffer_Pop(&buffer, 0);
    CHECK(pPopped == &values[5], "Popping returns the pointer at that index");
    CHECK(buffer.nUsed == 5, "Popping shortens the buffer");

    XDataBuffer_Clear(&buffer);
    CHECK(buffer.nUsed == 0, "Clearing empties the buffer");

    XDataBuffer_Destroy(&buffer);
    return 0;
}

static int XTest_ring_boundaries(void)
{
    /* The ring holds whole chunks in slots, not a stream of bytes: each
     * add occupies one slot and each pop consumes one, whatever fraction
     * of it the caller asked for. */
    xring_buffer_t ring;
    CHECK(XRingBuffer_Init(&ring, 4) > 0, "The ring initializes");

    CHECK(XRingBuffer_AddData(&ring, (const uint8_t*)"first", 5) > 0, "The first chunk is added");
    CHECK(XRingBuffer_AddData(&ring, (const uint8_t*)"second", 6) > 0, "The second chunk is added");
    CHECK(ring.nUsed == 2, "Each chunk occupies one slot");

    /* The view is of the front chunk only. */
    uint8_t *pData = NULL;
    size_t nSize = 0;
    CHECK(XRingBuffer_GetData(&ring, &pData, &nSize) > 0, "The ring hands back its front chunk");
    CHECK(nSize == 5 && memcmp(pData, "first", 5) == 0, "The front chunk is the one added first");

    /* Popping copies from the front chunk and then discards all of it. */
    uint8_t sPopped[16];
    memset(sPopped, 0, sizeof(sPopped));
    CHECK(XRingBuffer_Pop(&ring, sPopped, 3) == 3, "A partial pop reports what it copied");
    CHECK(memcmp(sPopped, "fir", 3) == 0, "The pop copies from the front of the chunk");
    CHECK(ring.nUsed == 1, "A pop consumes the whole chunk, not just what it copied");

    CHECK(XRingBuffer_GetData(&ring, &pData, &nSize) > 0, "The next chunk is now at the front");
    CHECK(nSize == 6 && memcmp(pData, "second", 6) == 0, "The next chunk is the one added second");

    /* Advancing discards the front chunk without copying it. */
    XRingBuffer_Advance(&ring);
    CHECK(ring.nUsed == 0, "Advancing consumed the last chunk");
    CHECK(XRingBuffer_GetData(&ring, &pData, &nSize) == 0, "An empty ring hands back nothing");
    CHECK(XRingBuffer_Pop(&ring, sPopped, sizeof(sPopped)) == 0, "An empty ring pops nothing");

    /* Filling every slot and then one more: the ring refuses rather than
     * silently dropping a chunk the caller believes it queued. */
    for (int i = 0; i < 4; i++)
        CHECK(XRingBuffer_AddData(&ring, (const uint8_t*)"x", 1) > 0, "Every slot accepts a chunk");
    CHECK(ring.nUsed == 4, "Every slot is occupied");
    CHECK(XRingBuffer_AddData(&ring, (const uint8_t*)"overflow", 8) <= 0, "A full ring refuses another chunk");
    CHECK(ring.nUsed == 4, "The refused chunk did not displace anything");

    /* Draining and refilling wraps around the slot array. */
    for (int i = 0; i < 4; i++) XRingBuffer_Advance(&ring);
    CHECK(ring.nUsed == 0, "The ring drains");

    for (int i = 0; i < 4; i++)
    {
        char sChunk[8];
        snprintf(sChunk, sizeof(sChunk), "w%d", i);
        CHECK(XRingBuffer_AddData(&ring, (const uint8_t*)sChunk, strlen(sChunk)) > 0,
            "The ring accepts chunks after wrapping");
    }

    for (int i = 0; i < 4; i++)
    {
        char sExpect[8];
        snprintf(sExpect, sizeof(sExpect), "w%d", i);
        CHECK(XRingBuffer_GetData(&ring, &pData, &nSize) > 0, "Every wrapped chunk is readable");
        CHECK(nSize == strlen(sExpect) && memcmp(pData, sExpect, nSize) == 0,
            "Wrapping preserves the order the chunks were added in");
        XRingBuffer_Advance(&ring);
    }

    /* Rejected arguments queue nothing. */
    CHECK(XRingBuffer_AddData(&ring, NULL, 4) <= 0, "A missing chunk is refused");
    CHECK(XRingBuffer_AddData(&ring, (const uint8_t*)"x", 0) <= 0, "An empty chunk is refused");
    CHECK(ring.nUsed == 0, "A refused chunk leaves the ring empty");

    XRingBuffer_Destroy(&ring);
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(aliasing),
    XTEST_CASE(borrowed),
    XTEST_CASE(pointer_ownership),
    XTEST_CASE(ring_model),
    XTEST_CASE(growth),
    XTEST_CASE(edit),
    XTEST_CASE(accessors),
    XTEST_CASE(ownership_transfer),
    XTEST_CASE(heap_buffer),
    XTEST_CASE(data_buffer),
    XTEST_CASE(ring_boundaries)
)
