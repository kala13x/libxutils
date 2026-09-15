/* libxutils: MDTP packet assembly and parsing.
 *
 * A packet is a little endian header length, a JSON header and an opaque
 * payload. The parser is fed by the network, so a header length that does
 * not match the buffer, or a payload size the sender lied about, has to be
 * reported as incomplete rather than used as an offset. */

#include "test.h"
#include "mdtp.h"
#include "str.h"

static void mdtp_fill_header(xpacket_t *pPacket, const char *pType, uint32_t nPayloadSize)
{
    xpacket_header_t *pHeader = &pPacket->header;
    pHeader->eType = XPacket_GetType(pType);
    pHeader->nPayloadSize = nPayloadSize;
    pHeader->nSessionID = 4242;
    pHeader->nPacketID = 7;
    pHeader->nTimeStamp = 1600000000u;
    xstrncpy(pHeader->sVersion, sizeof(pHeader->sVersion), XPACKET_VERSION_STR);
    xstrncpy(pHeader->sPayloadType, sizeof(pHeader->sPayloadType), "text");
}

static int XTest_type_mapping(void)
{
    /* Every enumerated type must survive the string round trip. */
    const xpacket_type_t types[] = {
        XPACKET_TYPE_DUMMY, XPACKET_TYPE_MULTY, XPACKET_TYPE_ERROR, XPACKET_TYPE_LITE,
        XPACKET_TYPE_DATA, XPACKET_TYPE_PING, XPACKET_TYPE_PONG, XPACKET_TYPE_INFO,
        XPACKET_TYPE_ACK, XPACKET_TYPE_CMD, XPACKET_TYPE_EOS, XPACKET_TYPE_KA
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++)
    {
        const char *pName = XPacket_GetTypeStr(types[i]);
        CHECK(pName != NULL && strcmp(pName, "invalid") != 0, "Every known type has a name");
        CHECK(XPacket_GetType(pName) == types[i], "Type names map back to their enum value");
    }

    CHECK(strcmp(XPacket_GetTypeStr(XPACKET_TYPE_INVALID), "invalid") == 0, "The invalid type is named");
    CHECK(strcmp(XPacket_GetTypeStr(XPACKET_TYPE_INCOMPLETE), "invalid") == 0, "Incomplete has no wire name");
    CHECK(strcmp(XPacket_GetTypeStr((xpacket_type_t)999), "invalid") == 0, "An out of range type is named invalid");
    CHECK(XPacket_GetType("nonsense") == XPACKET_TYPE_INVALID, "An unknown name is rejected");
    CHECK(XPacket_GetType("") == XPACKET_TYPE_INVALID, "An empty name is rejected");

    /* Status strings cover the whole enum plus the unmapped values. */
    const xpacket_status_t states[] = {
        XPACKET_COMPLETE, XPACKET_PARSED, XPACKET_ERR_ALLOC, XPACKET_BIGDATA,
        XPACKET_INCOMPLETE, XPACKET_INVALID_ARGS, XPACKET_INVALID
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(*states); i++)
        CHECK(strcmp(XPacket_GetStatusStr(states[i]), "Unknown status") != 0, "Every status has a description");
    CHECK(strcmp(XPacket_GetStatusStr(XPACKET_ERR_NONE), "Unknown status") == 0, "Unmapped statuses fall through");
    return 0;
}

static int XTest_assemble_parse(void)
{
    uint8_t payload[] = {'h', 'e', 'l', 'l', 0x00, 0xff, 'o'};
    xpacket_t packet;
    CHECK(XPacket_Init(&packet, payload, sizeof(payload)) == XPACKET_ERR_NONE, "A stack packet initializes");
    mdtp_fill_header(&packet, "data", sizeof(payload));

    xbyte_buffer_t *pBuffer = XPacket_Assemble(&packet);
    CHECK(pBuffer != NULL && pBuffer->nUsed > XPACKET_INFO_BYTES, "Assembly produces a wire buffer");

    /* The first four bytes are the little endian JSON header length. */
    uint32_t nHdrLen = (uint32_t)pBuffer->pData[0] | ((uint32_t)pBuffer->pData[1] << 8) |
                       ((uint32_t)pBuffer->pData[2] << 16) | ((uint32_t)pBuffer->pData[3] << 24);
    CHECK(nHdrLen == packet.nHeaderLength, "The info bytes carry the JSON header length");
    CHECK(pBuffer->nUsed == XPACKET_INFO_BYTES + nHdrLen + sizeof(payload), "The wire size is header plus payload");
    CHECK(XPacket_GetHeader(&packet) == &pBuffer->pData[XPACKET_INFO_BYTES], "The header starts after the info bytes");
    CHECK(memcmp(XPacket_GetPayload(&packet), payload, sizeof(payload)) == 0,
        "Binary payload bytes survive assembly untouched");

    /* Parsing the assembled bytes back must recover every header field. */
    xpacket_t parsed;
    CHECK(XPacket_Parse(&parsed, pBuffer->pData, pBuffer->nUsed) == XPACKET_COMPLETE, "The assembled packet parses");
    CHECK(parsed.header.eType == XPACKET_TYPE_DATA, "The packet type round trips");
    CHECK(parsed.header.nSessionID == 4242 && parsed.header.nPacketID == 7, "Session and packet ids round trip");
    CHECK(parsed.header.nTimeStamp == 1600000000u, "The timestamp round trips");
    CHECK(parsed.header.nPayloadSize == sizeof(payload), "The payload size round trips");
    CHECK(strcmp(parsed.header.sPayloadType, "text") == 0, "The payload type round trips");
    CHECK(parsed.pPayload != NULL && memcmp(parsed.pPayload, payload, sizeof(payload)) == 0,
        "The parsed payload points at the caller bytes");
    CHECK(XPacket_GetSize(&parsed) == XPACKET_INFO_BYTES + nHdrLen + sizeof(payload),
        "The reported packet size matches the wire size");
    XPacket_Clear(&parsed);
    XPacket_Clear(&packet);
    return 0;
}

static int XTest_parse_guards(void)
{
    uint8_t payload[] = {1, 2, 3, 4};
    xpacket_t packet;
    CHECK(XPacket_Init(&packet, payload, sizeof(payload)) == XPACKET_ERR_NONE, "Packet init");
    mdtp_fill_header(&packet, "cmd", sizeof(payload));
    xbyte_buffer_t *pBuffer = XPacket_Assemble(&packet);
    CHECK(pBuffer != NULL, "Assembly");

    xpacket_t parsed;
    CHECK(XPacket_Parse(NULL, pBuffer->pData, pBuffer->nUsed) == XPACKET_INVALID_ARGS, "A missing packet is rejected");
    CHECK(XPacket_Parse(&parsed, NULL, 10) == XPACKET_INVALID_ARGS, "A missing buffer is rejected");
    CHECK(XPacket_Parse(&parsed, pBuffer->pData, 0) == XPACKET_INVALID_ARGS, "A zero length buffer is rejected");

    /* Every truncation must report incomplete and leave nothing allocated. */
    for (size_t n = 1; n < pBuffer->nUsed; n++)
    {
        memset(&parsed, 0, sizeof(parsed));
        xpacket_status_t eStatus = XPacket_Parse(&parsed, pBuffer->pData, n);
        CHECK(eStatus == XPACKET_INCOMPLETE, "Every short prefix is reported incomplete");
        CHECK(parsed.header.eType == XPACKET_TYPE_INCOMPLETE, "The header records the incomplete state");
        CHECK(parsed.pHeaderObj == NULL, "An incomplete parse leaves no parsed header behind");
        XPacket_Clear(&parsed);
    }

    /* A header length that overruns the buffer is incomplete, not a read. */
    xbyte_buffer_t lying;
    XByteBuffer_Init(&lying, 0, 0);
    CHECK(XByteBuffer_Add(&lying, pBuffer->pData, pBuffer->nUsed), "Copy the wire bytes");
    lying.pData[0] = 0xff; lying.pData[1] = 0xff; lying.pData[2] = 0x00; lying.pData[3] = 0x00;
    memset(&parsed, 0, sizeof(parsed));
    CHECK(XPacket_Parse(&parsed, lying.pData, lying.nUsed) == XPACKET_INCOMPLETE,
        "A header length past the buffer end is rejected");
    XPacket_Clear(&parsed);

    /* A well sized but non-JSON header is invalid. */
    memcpy(&lying.pData[0], pBuffer->pData, XPACKET_INFO_BYTES);
    memset(&lying.pData[XPACKET_INFO_BYTES], '#', packet.nHeaderLength);
    memset(&parsed, 0, sizeof(parsed));
    CHECK(XPacket_Parse(&parsed, lying.pData, lying.nUsed) == XPACKET_INVALID, "A corrupt JSON header is invalid");
    XPacket_Clear(&parsed);
    XByteBuffer_Clear(&lying);

    /* A zero length header is accepted but yields no parsed object. */
    uint8_t empty[XPACKET_INFO_BYTES] = {0, 0, 0, 0};
    memset(&parsed, 0, sizeof(parsed));
    CHECK(XPacket_Parse(&parsed, empty, sizeof(empty)) == XPACKET_COMPLETE, "A zero length header completes");
    CHECK(parsed.pHeaderObj == NULL, "A zero length header parses to no object");
    CHECK(XPacket_GetHeader(&parsed) == NULL, "There is no header to hand out");
    CHECK(XPacket_GetPayload(&parsed) == NULL, "There is no payload to hand out");
    XPacket_Clear(&parsed);

    XPacket_Clear(&packet);
    return 0;
}

static int XTest_payload_size_lies(void)
{
    /* A header that announces more payload than the wire carries must be
     * reported as incomplete: the payload pointer would otherwise run off
     * the end of the caller's buffer. */
    uint8_t payload[16];
    memset(payload, 0x7e, sizeof(payload));

    xpacket_t packet;
    CHECK(XPacket_Init(&packet, payload, sizeof(payload)) == XPACKET_ERR_NONE, "Packet init");
    mdtp_fill_header(&packet, "data", sizeof(payload));
    xbyte_buffer_t *pBuffer = XPacket_Assemble(&packet);
    CHECK(pBuffer != NULL, "Assembly");

    xpacket_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    CHECK(XPacket_Parse(&parsed, pBuffer->pData, pBuffer->nUsed - 1) == XPACKET_INCOMPLETE,
        "One byte short of the announced payload is incomplete");
    XPacket_Clear(&parsed);

    /* Trailing bytes beyond the packet are left for the next packet. */
    xbyte_buffer_t stream;
    XByteBuffer_Init(&stream, 0, 0);
    CHECK(XByteBuffer_Add(&stream, pBuffer->pData, pBuffer->nUsed), "Copy the packet");
    CHECK(XByteBuffer_Add(&stream, (uint8_t*)"tail", 4), "Append a second packet's bytes");
    memset(&parsed, 0, sizeof(parsed));
    CHECK(XPacket_Parse(&parsed, stream.pData, stream.nUsed) == XPACKET_COMPLETE, "A packet with a tail parses");
    CHECK(XPacket_GetSize(&parsed) == stream.nUsed - 4, "The reported size excludes the trailing bytes");
    XPacket_Clear(&parsed);
    XByteBuffer_Clear(&stream);

    XPacket_Clear(&packet);
    return 0;
}

static int XTest_header_fields(void)
{
    /* Optional sub-objects are only emitted when the matching field is set. */
    xpacket_t packet;
    CHECK(XPacket_Init(&packet, NULL, 0) == XPACKET_ERR_NONE, "A payload-less packet initializes");
    CHECK(packet.header.eType == XPACKET_TYPE_LITE, "A fresh packet defaults to the lite type");

    packet.header.eType = XPACKET_TYPE_PING;
    xstrncpy(packet.header.sVersion, sizeof(packet.header.sVersion), "1.0");
    xstrncpy(packet.header.sTime, sizeof(packet.header.sTime), "2020-01-01 00:00:00");
    xstrncpy(packet.header.sTZ, sizeof(packet.header.sTZ), "+04");

    xbyte_buffer_t *pBuffer = XPacket_Assemble(&packet);
    CHECK(pBuffer != NULL, "A header-only packet assembles");
    CHECK(XPacket_GetPayload(&packet) == NULL, "There is no payload to hand out");

    xpacket_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    CHECK(XPacket_Parse(&parsed, pBuffer->pData, pBuffer->nUsed) == XPACKET_COMPLETE, "The header-only packet parses");
    CHECK(parsed.header.eType == XPACKET_TYPE_PING, "The ping type round trips");
    CHECK(strcmp(parsed.header.sTime, "2020-01-01 00:00:00") == 0, "The extra time field round trips");
    CHECK(strcmp(parsed.header.sTZ, "+04") == 0, "The extra time zone round trips");
    CHECK(parsed.header.nPayloadSize == 0, "No payload is announced");
    XPacket_Clear(&parsed);
    XPacket_Clear(&packet);

    /* The lite type is the wire default and carries no packetType field. */
    CHECK(XPacket_Init(&packet, NULL, 0) == XPACKET_ERR_NONE, "Packet init");
    pBuffer = XPacket_Assemble(&packet);
    CHECK(pBuffer != NULL, "A lite packet assembles");
    const uint8_t *pHeaderBytes = XPacket_GetHeader(&packet);
    CHECK(pHeaderBytes != NULL, "The lite packet still has a JSON header");
    CHECK(xstrsrcb((const char*)pHeaderBytes, packet.nHeaderLength, "packetType") < 0,
        "The lite type is implicit and not written to the wire");
    XPacket_Clear(&packet);

    /* An error type refuses to assemble. */
    CHECK(XPacket_Init(&packet, NULL, 0) == XPACKET_ERR_NONE, "Packet init");
    packet.header.eType = XPACKET_TYPE_ERROR;
    CHECK(XPacket_Assemble(&packet) == NULL, "An error packet does not assemble");
    CHECK(XPacket_UpdateHeader(&packet) == XPACKET_INVALID, "Updating an error header is rejected");
    XPacket_Clear(&packet);

    CHECK(XPacket_UpdateHeader(NULL) == XPACKET_INVALID_ARGS, "A missing packet is rejected");
    return 0;
}

static int XTest_create_guards(void)
{
    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, 0, 0);
    const char *pHeader = "{\"version\":\"1.0\"}";
    uint8_t payload[] = {9, 8, 7};

    CHECK(XPacket_Create(NULL, pHeader, strlen(pHeader), payload, sizeof(payload)) == XPACKET_INVALID_ARGS,
        "A missing buffer is rejected");
    CHECK(XPacket_Create(&buffer, NULL, 4, payload, sizeof(payload)) == XPACKET_INVALID_ARGS,
        "A missing header is rejected");
    CHECK(XPacket_Create(&buffer, pHeader, 0, payload, sizeof(payload)) == XPACKET_INVALID_ARGS,
        "A zero length header is rejected");
    CHECK(buffer.nUsed == 0, "A rejected create writes nothing");

    CHECK(XPacket_Create(&buffer, pHeader, strlen(pHeader), payload, sizeof(payload)) == XPACKET_ERR_NONE,
        "A valid header and payload are framed");
    CHECK(buffer.nUsed == XPACKET_INFO_BYTES + strlen(pHeader) + sizeof(payload), "The framed size is exact");
    CHECK(buffer.pData[0] == (uint8_t)strlen(pHeader), "The low info byte holds the header length");
    XByteBuffer_Clear(&buffer);

    /* A header without a payload is a valid frame. */
    CHECK(XPacket_Create(&buffer, pHeader, strlen(pHeader), NULL, 0) == XPACKET_ERR_NONE,
        "A payload-less frame is created");
    CHECK(buffer.nUsed == XPACKET_INFO_BYTES + strlen(pHeader), "The frame is header only");
    XByteBuffer_Clear(&buffer);

    /* A payload pointer with a zero size adds nothing. */
    CHECK(XPacket_Create(&buffer, pHeader, strlen(pHeader), payload, 0) == XPACKET_ERR_NONE,
        "A zero sized payload is skipped");
    CHECK(buffer.nUsed == XPACKET_INFO_BYTES + strlen(pHeader), "A zero sized payload adds no bytes");
    XByteBuffer_Clear(&buffer);
    return 0;
}

/* Tracks the callback sequence a packet drives through its lifecycle. */
static char g_callbacks[16];
static size_t g_callbackCount = 0;

static void mdtp_callback(xpacket_t *pPacket, uint8_t nCallback)
{
    (void)pPacket;
    if (g_callbackCount < sizeof(g_callbacks)) g_callbacks[g_callbackCount++] = (char)('0' + nCallback);
}

static int XTest_lifecycle(void)
{
    uint8_t payload[] = {'x'};
    xpacket_t *pPacket = XPacket_New(payload, sizeof(payload));
    CHECK(pPacket != NULL, "A heap packet is allocated");
    CHECK(pPacket->nAllocated == 1, "A heap packet is marked as owned");
    CHECK(XPacket_GetSize(pPacket) == 0, "An unassembled packet has no wire size");

    g_callbackCount = 0;
    pPacket->callback = mdtp_callback;
    mdtp_fill_header(pPacket, "ka", sizeof(payload));
    CHECK(XPacket_Assemble(pPacket) != NULL, "The heap packet assembles");
    CHECK(g_callbackCount == 1 && g_callbacks[0] == '0' + XPACKET_CB_UPDATE,
        "Assembly drives exactly one update callback");

    XPacket_Free(&pPacket);
    CHECK(pPacket == NULL, "Freeing clears the caller pointer");
    CHECK(g_callbackCount == 2 && g_callbacks[1] == '0' + XPACKET_CB_CLEAR, "Freeing drives the clear callback");

    /* A second clear on a cleared packet must be a no-op, not a double free. */
    xpacket_t stack;
    CHECK(XPacket_Init(&stack, payload, sizeof(payload)) == XPACKET_ERR_NONE, "Stack packet init");
    CHECK(stack.nAllocated == 0, "A stack packet is not marked as owned");
    XPacket_Clear(&stack);
    XPacket_Clear(&stack);
    CHECK(stack.pHeaderObj == NULL, "Clearing twice leaves no header object");

    /* Freeing a stack packet must not call free() on it. */
    CHECK(XPacket_Init(&stack, payload, sizeof(payload)) == XPACKET_ERR_NONE, "Stack packet init");
    xpacket_t *pStack = &stack;
    XPacket_Free(&pStack);
    CHECK(pStack == &stack, "A non-owned packet pointer is left alone");

    XPacket_Free(NULL);
    XPacket_Clear(NULL);
    XPacket_Free(&pPacket);
    CHECK(XPacket_GetSize(NULL) == 0, "A missing packet has no size");
    CHECK(XPacket_GetHeader(NULL) == NULL, "A missing packet has no header");
    CHECK(XPacket_GetPayload(NULL) == NULL, "A missing packet has no payload");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(type_mapping),
    XTEST_CASE(assemble_parse),
    XTEST_CASE(parse_guards),
    XTEST_CASE(payload_size_lies),
    XTEST_CASE(header_fields),
    XTEST_CASE(create_guards),
    XTEST_CASE(lifecycle)
)
