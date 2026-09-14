/* libxutils: byte-exact framing at length boundaries and hostile/truncated wire input. */
#include "test.h"
#include "ws.h"

static int XTest_lengths(void)
{
    const size_t lengths[] = {0, 1, 124, 125, 126, 127, 65535, 65536, 73636};
    uint8_t *pPayload = (uint8_t*)malloc(73636);
    CHECK(pPayload != NULL, "Allocate a bounded binary payload");
    for (size_t i = 0; i < 73636; i++) pPayload[i] = (uint8_t)(i % 251);
    for (size_t i = 0; i < sizeof(lengths) / sizeof(*lengths); i++)
        for (int nMask = 0; nMask < 2; nMask++)
        {
            size_t nLength = lengths[i];
            xws_frame_t wire, parsed;
            CHECK(XWebFrame_Create(&wire, pPayload, nLength, XWS_BINARY, nMask, XTRUE) == XWS_ERR_NONE, "Create boundary frame");
            size_t nHeader = nLength < 126 ? 2 : (nLength <= 65535 ? 4 : 10);
            CHECK(wire.nHeaderSize == nHeader + (nMask ? 4 : 0), "Wire header must use the smallest valid length encoding");
            CHECK(
                XWebFrame_ParseData(&parsed, wire.buffer.pData, wire.buffer.nUsed) == XWS_FRAME_COMPLETE, "Parse boundary frame");
            CHECK(parsed.eType == XWS_BINARY && parsed.bFin && !parsed.bMask, "Parsing returns a complete unmasked binary frame");
            CHECK(XWebFrame_GetPayloadLength(&parsed) == nLength, "Boundary payload length must survive framing");
            CHECK(!nLength || memcmp(XWebFrame_GetPayload(&parsed), pPayload, nLength) == 0, "Binary payload must remain exact");
            XWebFrame_Clear(&parsed);
            XWebFrame_Clear(&wire);
        }
    free(pPayload);
    return 0;
}

static int XTest_partial(void)
{
    uint8_t payload[137];
    memset(payload, 0xa5, sizeof(payload));
    xws_frame_t wire;
    CHECK(XWebFrame_Create(&wire, payload, sizeof(payload), XWS_BINARY, XTRUE, XTRUE) == XWS_ERR_NONE, "Create masked frame");
    for (size_t nSplit = 1; nSplit < wire.buffer.nUsed; nSplit++)
    {
        xws_frame_t parsed;
        CHECK(
            XWebFrame_ParseData(&parsed, wire.buffer.pData, nSplit) == XWS_FRAME_INCOMPLETE, "Every proper prefix is incomplete");
        CHECK(!parsed.bComplete, "A partial frame must never report completion");
        CHECK(XWebFrame_TryParse(&parsed, wire.buffer.pData + nSplit, wire.buffer.nUsed - nSplit) == XWS_FRAME_COMPLETE,
            "Appending the remaining bytes must complete the frame");
        CHECK(memcmp(XWebFrame_GetPayload(&parsed), payload, sizeof(payload)) == 0, "Fragmented input must decode exactly once");
        XWebFrame_Clear(&parsed);
    }
    XWebFrame_Clear(&wire);
    return 0;
}

static int XTest_coalesced(void)
{
    uint8_t wire[] = {0x82, 3, 'a', 0, 'b', 0x89, 1, 'z'};
    xws_frame_t first, second;
    CHECK(XWebFrame_ParseData(&first, wire, sizeof(wire)) == XWS_FRAME_COMPLETE, "Parse the first coalesced frame");
    CHECK(XWebFrame_GetFrameLength(&first) == 5 && XWebFrame_GetExtraLength(&first) == 3, "Keep trailing frame boundaries exact");
    xbyte_buffer_t extra;
    CHECK(XWebFrame_GetExtraData(&first, &extra, XFALSE) == 3, "Extract the second frame without merging payloads");
    CHECK(XWebFrame_CutExtraData(&first) > 0 && XWebFrame_GetExtraLength(&first) == 0, "Cut only the trailing frame");
    CHECK(XWebFrame_ParseBuff(&second, &extra) == XWS_FRAME_COMPLETE, "Parse extracted control frame");
    CHECK(second.eType == XWS_PING && XWebFrame_GetPayload(&second)[0] == 'z', "Preserve coalesced control payload");
    XWebFrame_Clear(&second);
    XByteBuffer_Clear(&extra);
    XWebFrame_Clear(&first);
    return 0;
}

static int XTest_invalid_wire(void)
{
    const uint8_t invalid[][10] = {{0xc2, 0}, {0x83, 0}, {0x09, 0}, {0x89, 126, 0, 126}, {0x82, 127, 0x80, 0, 0, 0, 0, 0, 0, 0}};
    const size_t sizes[] = {2, 2, 2, 4, 10};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); i++)
    {
        xws_frame_t frame;
        xws_status_t eStatus = XWebFrame_ParseData(&frame, (uint8_t*)invalid[i], sizes[i]);
        CHECK(eStatus == XWS_FRAME_INVALID || eStatus == XWS_INVALID_TYPE || eStatus == XWS_FRAME_TOOBIG,
            "Reject reserved bits/opcodes, fragmented or oversized control frames, and invalid 64-bit lengths");
        CHECK(!frame.bComplete, "Invalid wire data must not be marked complete");
        XWebFrame_Clear(&frame);
    }
    return 0;
}

static int XTest_creation_guards(void)
{
    uint8_t byte = 0;
    size_t nSize = 55;
    CHECK(XWS_CreateFrame(&byte, SIZE_MAX, 2, XTRUE, &nSize) == NULL && nSize == 0, "Reject frame size overflow");
    CHECK(XWS_CreateFrame(NULL, 1, 2, XTRUE, &nSize) == NULL, "Reject nonempty frames without payload storage");
    CHECK(XWS_CreateFrame(&byte, 126, 9, XTRUE, &nSize) == NULL, "Control payloads must fit 125 bytes");
    CHECK(XWS_CreateFrame(&byte, 1, 9, XFALSE, &nSize) == NULL, "Control frames must never be fragmented");
    xws_frame_t *pFrame = XWebFrame_New(&byte, 1, XWS_BINARY, XFALSE, XTRUE);
    CHECK(pFrame != NULL, "Allocate an owned frame");
    XWebFrame_Free(&pFrame);
    CHECK(pFrame == NULL, "Free clears the owning frame pointer");
    XWebFrame_Free(&pFrame);
    return 0;
}

XTEST_MAIN(XTEST_CASE(lengths), XTEST_CASE(partial), XTEST_CASE(coalesced), XTEST_CASE(invalid_wire), XTEST_CASE(creation_guards))
