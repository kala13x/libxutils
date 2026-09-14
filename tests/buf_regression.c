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

XTEST_MAIN(XTEST_CASE(aliasing), XTEST_CASE(borrowed), XTEST_CASE(pointer_ownership), XTEST_CASE(ring_model))
