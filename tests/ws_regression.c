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


static int XTest_frame_types(void)
{
    /* Every frame type and its opcode are the same number on the wire, so
     * the two conversions have to agree in both directions. */
    for (int nType = XWS_CONTINUATION; nType < XWS_INVALID; nType++)
    {
        uint8_t nOpCode = XWS_OpCode((xws_frame_type_t)nType);
        CHECK(nOpCode == (uint8_t)nType, "Every type maps to its own opcode");
        CHECK(XWS_FrameType(nOpCode) == (xws_frame_type_t)nType, "Every opcode maps back to its own type");

        const char *pName = XWS_FrameTypeStr((xws_frame_type_t)nType);
        CHECK(pName != NULL && *pName != '\0', "Every type has a name");
    }

    /* The named types are the ones a caller actually writes. */
    CHECK(strcmp(XWS_FrameTypeStr(XWS_TEXT), "text") == 0, "The text type is named");
    CHECK(strcmp(XWS_FrameTypeStr(XWS_BINARY), "binary") == 0, "The binary type is named");
    CHECK(strcmp(XWS_FrameTypeStr(XWS_CLOSE), "close") == 0, "The close type is named");
    CHECK(strcmp(XWS_FrameTypeStr(XWS_PING), "ping") == 0, "The ping type is named");
    CHECK(strcmp(XWS_FrameTypeStr(XWS_PONG), "pong") == 0, "The pong type is named");
    CHECK(strcmp(XWS_FrameTypeStr(XWS_CONTINUATION), "continuation") == 0, "The continuation type is named");

    /* An opcode outside the four bit field is not a frame type. */
    CHECK(XWS_FrameType(16) == XWS_INVALID, "An opcode past the four bit field is invalid");
    CHECK(XWS_FrameType(255) == XWS_INVALID, "A byte sized opcode is invalid");

    /* Every status has a description. */
    const xws_status_t states[] = {
        XWS_ERR_ALLOC, XWS_ERR_SIZE, XWS_INVALID_ARGS, XWS_INVALID_TYPE,
        XWS_INVALID_REQUEST, XWS_INVALID_RESPONSE, XWS_INVALID_SEC_KEY,
        XWS_MISSING_SEC_KEY, XWS_MISSING_PAYLOAD, XWS_FRAME_TOOBIG,
        XWS_FRAME_PARSED, XWS_FRAME_INVALID, XWS_FRAME_COMPLETE,
        XWS_FRAME_INCOMPLETE, XWS_ERR_RANDOM
    };

    for (size_t i = 0; i < sizeof(states) / sizeof(*states); i++)
    {
        const char *pText = XWebSock_GetStatusStr(states[i]);
        CHECK(pText != NULL && *pText != '\0', "Every status has a description");
    }
    return 0;
}

static int XTest_masking(void)
{
    /* A masked frame carries a key and the payload is obscured on the wire,
     * but the parser has to hand back the original bytes. */
    const uint8_t payload[] = {'m', 'a', 's', 'k', 0x00, 0xff, 'e', 'd'};

    xws_frame_t *pFrame = XWebFrame_New(payload, sizeof(payload), XWS_BINARY, XTRUE, XTRUE);
    CHECK(pFrame != NULL, "A masked frame is built");

    xbyte_buffer_t *pWire = XWebFrame_GetBuffer(pFrame);
    CHECK(pWire != NULL && pWire->nUsed > sizeof(payload), "The wire form is longer than the payload");
    CHECK((pWire->pData[1] & 0x80) != 0, "The mask bit is set on the wire");

    /* The masked payload on the wire differs from the plain bytes. */
    size_t nHeader = pFrame->nHeaderSize;
    CHECK(memcmp(&pWire->pData[nHeader], payload, sizeof(payload)) != 0,
        "The payload is obscured on the wire");

    /* Parsing recovers the original bytes. */
    xws_frame_t parsed;
    CHECK(XWebFrame_ParseData(&parsed, pWire->pData, pWire->nUsed) == XWS_FRAME_COMPLETE,
        "The masked frame parses");
    CHECK(parsed.eType == XWS_BINARY, "The frame type survives masking");
    CHECK(XWebFrame_GetPayloadLength(&parsed) == sizeof(payload), "The payload length survives masking");
    CHECK(memcmp(XWebFrame_GetPayload(&parsed), payload, sizeof(payload)) == 0,
        "The payload bytes are recovered from the mask");
    XWebFrame_Clear(&parsed);
    XWebFrame_Free(&pFrame);

    /* An unmasked frame carries the same payload in the clear. */
    pFrame = XWebFrame_New(payload, sizeof(payload), XWS_BINARY, XFALSE, XTRUE);
    CHECK(pFrame != NULL, "An unmasked frame is built");

    pWire = XWebFrame_GetBuffer(pFrame);
    CHECK((pWire->pData[1] & 0x80) == 0, "The mask bit is clear on the wire");
    CHECK(memcmp(&pWire->pData[pFrame->nHeaderSize], payload, sizeof(payload)) == 0,
        "An unmasked payload is on the wire in the clear");

    CHECK(XWebFrame_ParseData(&parsed, pWire->pData, pWire->nUsed) == XWS_FRAME_COMPLETE,
        "The unmasked frame parses");
    CHECK(memcmp(XWebFrame_GetPayload(&parsed), payload, sizeof(payload)) == 0,
        "The unmasked payload is unchanged");
    XWebFrame_Clear(&parsed);
    XWebFrame_Free(&pFrame);

    /* Masking is keyed, so two frames of the same payload differ on the
     * wire but both recover the same bytes. */
    xws_frame_t *pFirst = XWebFrame_New(payload, sizeof(payload), XWS_TEXT, XTRUE, XTRUE);
    xws_frame_t *pSecond = XWebFrame_New(payload, sizeof(payload), XWS_TEXT, XTRUE, XTRUE);
    CHECK(pFirst != NULL && pSecond != NULL, "Two masked frames are built");

    xbyte_buffer_t *pFirstWire = XWebFrame_GetBuffer(pFirst);
    xbyte_buffer_t *pSecondWire = XWebFrame_GetBuffer(pSecond);
    CHECK(pFirstWire->nUsed == pSecondWire->nUsed, "Both frames are the same length");

    for (int i = 0; i < 2; i++)
    {
        xbyte_buffer_t *pAny = i ? pSecondWire : pFirstWire;
        CHECK(XWebFrame_ParseData(&parsed, pAny->pData, pAny->nUsed) == XWS_FRAME_COMPLETE,
            "Each masked frame parses");
        CHECK(memcmp(XWebFrame_GetPayload(&parsed), payload, sizeof(payload)) == 0,
            "Each masked frame recovers the same payload");
        XWebFrame_Clear(&parsed);
    }

    XWebFrame_Free(&pSecond);
    XWebFrame_Free(&pFirst);
    return 0;
}

static int XTest_control_frames(void)
{
    /* Control frames carry a short payload and a type the peer acts on. */
    const struct { xws_frame_type_t eType; const char *pPayload; } controls[] = {
        {XWS_PING, "ping-body"},
        {XWS_PONG, "pong-body"},
        {XWS_CLOSE, "\x03\xe8" "bye"}
    };

    for (size_t i = 0; i < sizeof(controls) / sizeof(*controls); i++)
    {
        size_t nLength = strlen(controls[i].pPayload);
        xws_frame_t *pFrame = XWebFrame_New((const uint8_t*)controls[i].pPayload, nLength,
            controls[i].eType, XFALSE, XTRUE);
        CHECK(pFrame != NULL, "Every control frame is built");

        xbyte_buffer_t *pWire = XWebFrame_GetBuffer(pFrame);
        CHECK((pWire->pData[0] & 0x0f) == XWS_OpCode(controls[i].eType),
            "The control opcode is on the wire");
        CHECK((pWire->pData[0] & 0x80) != 0, "A control frame is final");

        xws_frame_t parsed;
        CHECK(XWebFrame_ParseData(&parsed, pWire->pData, pWire->nUsed) == XWS_FRAME_COMPLETE,
            "Every control frame parses");
        CHECK(parsed.eType == controls[i].eType, "The control type survives the round trip");
        CHECK(parsed.bFin == XTRUE, "The final bit survives the round trip");
        CHECK(XWebFrame_GetPayloadLength(&parsed) == nLength, "The control payload length survives");
        XWebFrame_Clear(&parsed);
        XWebFrame_Free(&pFrame);
    }

    /* A control frame with no payload at all is still a frame. */
    xws_frame_t *pEmpty = XWebFrame_New(NULL, 0, XWS_PING, XFALSE, XTRUE);
    CHECK(pEmpty != NULL, "An empty ping is built");

    xbyte_buffer_t *pWire = XWebFrame_GetBuffer(pEmpty);
    CHECK(pWire->nUsed == 2, "An empty unmasked frame is just its two header bytes");

    xws_frame_t parsed;
    CHECK(XWebFrame_ParseData(&parsed, pWire->pData, pWire->nUsed) == XWS_FRAME_COMPLETE,
        "The empty ping parses");
    CHECK(parsed.eType == XWS_PING, "The empty ping keeps its type");
    CHECK(XWebFrame_GetPayloadLength(&parsed) == 0, "The empty ping has no payload");
    XWebFrame_Clear(&parsed);
    XWebFrame_Free(&pEmpty);

    /* A non-final frame is a fragment, and the flag has to survive. */
    xws_frame_t *pFragment = XWebFrame_New((const uint8_t*)"part", 4, XWS_TEXT, XFALSE, XFALSE);
    CHECK(pFragment != NULL, "A fragment is built");

    pWire = XWebFrame_GetBuffer(pFragment);
    CHECK((pWire->pData[0] & 0x80) == 0, "A fragment is not final on the wire");

    CHECK(XWebFrame_ParseData(&parsed, pWire->pData, pWire->nUsed) == XWS_FRAME_COMPLETE,
        "The fragment parses");
    CHECK(parsed.bFin == XFALSE, "The fragment is not final after parsing");
    XWebFrame_Clear(&parsed);
    XWebFrame_Free(&pFragment);
    return 0;
}

static int XTest_extra_data(void)
{
    /* Two frames in one buffer: the first parse has to report the second
     * as extra rather than swallowing or dropping it. */
    xws_frame_t *pFirst = XWebFrame_New((const uint8_t*)"first", 5, XWS_TEXT, XFALSE, XTRUE);
    xws_frame_t *pSecond = XWebFrame_New((const uint8_t*)"second", 6, XWS_BINARY, XFALSE, XTRUE);
    CHECK(pFirst != NULL && pSecond != NULL, "Both frames are built");

    xbyte_buffer_t stream;
    XByteBuffer_Init(&stream, 0, 0);
    CHECK(XByteBuffer_AddBuff(&stream, XWebFrame_GetBuffer(pFirst)) > 0, "The first frame is queued");
    CHECK(XByteBuffer_AddBuff(&stream, XWebFrame_GetBuffer(pSecond)) > 0, "The second frame is queued");

    size_t nFirstLen = XWebFrame_GetFrameLength(pFirst);
    size_t nSecondLen = XWebFrame_GetFrameLength(pSecond);
    XWebFrame_Free(&pSecond);
    XWebFrame_Free(&pFirst);

    xws_frame_t parsed;
    CHECK(XWebFrame_ParseData(&parsed, stream.pData, stream.nUsed) == XWS_FRAME_COMPLETE,
        "The first frame parses out of the stream");
    CHECK(XWebFrame_GetFrameLength(&parsed) == nFirstLen, "The first frame reports its own length");
    CHECK(XWebFrame_GetExtraLength(&parsed) == nSecondLen, "The rest of the stream is reported as extra");
    CHECK(memcmp(XWebFrame_GetPayload(&parsed), "first", 5) == 0, "The first payload is the first one");

    /* The extra bytes can be taken out and parsed on their own. */
    xbyte_buffer_t extra;
    CHECK(XWebFrame_GetExtraData(&parsed, &extra, XFALSE) > 0, "The extra bytes are handed over");
    CHECK(extra.nUsed == nSecondLen, "The extra bytes are the whole second frame");

    xws_frame_t next;
    CHECK(XWebFrame_ParseData(&next, extra.pData, extra.nUsed) == XWS_FRAME_COMPLETE,
        "The second frame parses from the extra bytes");
    CHECK(next.eType == XWS_BINARY, "The second frame keeps its own type");
    CHECK(memcmp(XWebFrame_GetPayload(&next), "second", 6) == 0, "The second payload is the second one");
    CHECK(XWebFrame_GetExtraLength(&next) == 0, "There is nothing after the second frame");
    XWebFrame_Clear(&next);
    XByteBuffer_Clear(&extra);

    /* Cutting the extra data leaves the frame holding only itself. */
    CHECK(XWebFrame_CutExtraData(&parsed) == XSTDOK, "The extra bytes are cut away");
    CHECK(XWebFrame_GetExtraLength(&parsed) == 0, "Nothing is left over after the cut");
    CHECK(memcmp(XWebFrame_GetPayload(&parsed), "first", 5) == 0, "The cut left the payload alone");
    XWebFrame_Clear(&parsed);

    XByteBuffer_Clear(&stream);
    return 0;
}

static int XTest_allocation(void)
{
    /* The allocating helpers hand back a frame the caller frees, and the
     * free clears the caller's pointer. */
    xws_frame_t *pFrame = XWebFrame_Alloc(XWS_TEXT, 128);
    CHECK(pFrame != NULL, "A frame is allocated");
    CHECK(pFrame->eType == XWS_TEXT, "The allocated frame keeps its type");
    CHECK(pFrame->bAlloc == XTRUE, "The allocated frame is marked as owned");

    XWebFrame_Free(&pFrame);
    CHECK(pFrame == NULL, "Freeing clears the caller pointer");
    XWebFrame_Free(&pFrame);
    XWebFrame_Free(NULL);

    /* A stack frame is initialized and cleared without being freed. */
    xws_frame_t frame;
    XWebFrame_Init(&frame);
    CHECK(frame.bAlloc == XFALSE, "A stack frame is not marked as owned");
    CHECK(frame.eType == XWS_INVALID || frame.nPayloadLength == 0, "A stack frame starts empty");

    CHECK(XWebFrame_Create(&frame, (const uint8_t*)"body", 4, XWS_TEXT, XFALSE, XTRUE) == XWS_ERR_NONE,
        "A stack frame is filled in");
    CHECK(XWebFrame_GetPayloadLength(&frame) == 4, "The stack frame holds its payload");

    /* Resetting empties the frame for reuse without releasing it. */
    XWebFrame_Reset(&frame);
    CHECK(XWebFrame_GetPayloadLength(&frame) == 0, "A reset frame holds nothing");

    CHECK(XWebFrame_Create(&frame, (const uint8_t*)"again", 5, XWS_BINARY, XFALSE, XTRUE) == XWS_ERR_NONE,
        "A reset frame is reusable");
    CHECK(XWebFrame_GetPayloadLength(&frame) == 5, "The reused frame holds its new payload");

    XWebFrame_Clear(&frame);
    XWebFrame_Clear(&frame);
    CHECK(XWebFrame_GetPayloadLength(&frame) == 0, "A repeatedly cleared frame stays empty");

    /* The raw frame builder produces the same bytes as the object one. */
    size_t nRawSize = 0;
    uint8_t *pRaw = XWS_CreateFrame((const uint8_t*)"raw", 3, XWS_OpCode(XWS_TEXT), XTRUE, &nRawSize);
    CHECK(pRaw != NULL && nRawSize == 5, "The raw builder produces an unmasked text frame");
    CHECK(pRaw[0] == 0x81, "The raw frame is a final text frame");
    CHECK(pRaw[1] == 3, "The raw frame announces its payload length");
    CHECK(memcmp(&pRaw[2], "raw", 3) == 0, "The raw frame carries its payload");
    free(pRaw);

    CHECK(XWS_CreateFrame(NULL, 4, XWS_OpCode(XWS_TEXT), XTRUE, &nRawSize) == NULL,
        "The raw builder refuses a missing payload with a length");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(lengths),
    XTEST_CASE(partial),
    XTEST_CASE(coalesced),
    XTEST_CASE(invalid_wire),
    XTEST_CASE(creation_guards),
    XTEST_CASE(frame_types),
    XTEST_CASE(masking),
    XTEST_CASE(control_frames),
    XTEST_CASE(extra_data),
    XTEST_CASE(allocation)
)