/* libxutils: RTP header/packet parser and assembler boundaries.
 *
 * The parser consumes untrusted wire bytes, so every announced length
 * (CSRC count, block sizes, fragment size) has to be validated against
 * what the buffer actually carries before it is used as an offset. */

#include "test.h"
#include "rtp.h"

#define XRTP_HEADER_SIZE 12

/* Writes a bare 12 byte RTP header with the requested bit fields. */
static void rtp_header(uint8_t *pWire, uint8_t nPadding, uint8_t nExt, uint8_t nCSRC,
    uint8_t nMarker, uint8_t nPayloadType, uint16_t nSeq, uint32_t nTime, uint32_t nSSRC)
{
    pWire[0] = (uint8_t)(0x80 | (nPadding << 5) | (nExt << 4) | (nCSRC & 0x0f));
    pWire[1] = (uint8_t)((nMarker << 7) | (nPayloadType & 0x7f));
    pWire[2] = (uint8_t)(nSeq >> 8);
    pWire[3] = (uint8_t)(nSeq & 0xff);
    pWire[4] = (uint8_t)(nTime >> 24);
    pWire[5] = (uint8_t)((nTime >> 16) & 0xff);
    pWire[6] = (uint8_t)((nTime >> 8) & 0xff);
    pWire[7] = (uint8_t)(nTime & 0xff);
    pWire[8] = (uint8_t)(nSSRC >> 24);
    pWire[9] = (uint8_t)((nSSRC >> 16) & 0xff);
    pWire[10] = (uint8_t)((nSSRC >> 8) & 0xff);
    pWire[11] = (uint8_t)(nSSRC & 0xff);
}

static int XTest_header_fields(void)
{
    uint8_t wire[XRTP_HEADER_SIZE];
    xrtp_header_t header;

    rtp_header(wire, 1, 1, 0, 1, 0x7f, 0xbeef, 0xdeadbeefu, 0x01020304u);
    CHECK(XRTP_ParseHeader(&header, wire, sizeof(wire)) == XRTP_HEADER_SIZE,
        "A CSRC-less header ends after the fixed 12 bytes");
    CHECK(header.nVersion == 2, "Version bits decode to 2");
    CHECK(header.nPadding == 1 && header.nExtension == 1, "Padding and extension bits are independent");
    CHECK(header.nSCRCCount == 0, "The low nibble of the first byte is the CSRC count");
    CHECK(header.nMarkerBit == 1 && header.nPayloadType == 0x7f, "Marker bit does not bleed into the payload type");
    CHECK(header.nSequence == 0xbeef, "Sequence is decoded from network order");
    CHECK(header.nTimeStamp == 0xdeadbeefu, "Timestamp is decoded from network order");
    CHECK(header.nSSRC == 0x01020304u, "SSRC is decoded from network order");
    for (size_t i = 0; i < SCRC_MAX; i++) CHECK(header.SCRC[i] == 0, "An empty CSRC list is zeroed out");

    /* The complementary bit pattern must not leak into the neighbouring fields. */
    rtp_header(wire, 0, 0, 0, 0, 0, 0, 0, 0);
    CHECK(XRTP_ParseHeader(&header, wire, sizeof(wire)) == XRTP_HEADER_SIZE, "An all-zero body still parses");
    CHECK(!header.nPadding && !header.nExtension && !header.nMarkerBit && !header.nPayloadType,
        "Cleared flag bits stay cleared");
    return 0;
}

static int XTest_header_guards(void)
{
    uint8_t wire[XRTP_HEADER_SIZE];
    xrtp_header_t header;
    rtp_header(wire, 0, 0, 0, 0, 96, 1, 2, 3);

    CHECK(XRTP_ParseHeader(NULL, wire, sizeof(wire)) == XSTDERR, "A missing output header is rejected");
    CHECK(XRTP_ParseHeader(&header, NULL, sizeof(wire)) == XSTDERR, "A missing buffer is rejected");
    for (size_t n = 0; n < XRTP_HEADER_SIZE; n++)
        CHECK(XRTP_ParseHeader(&header, wire, n) == XSTDERR, "Every short buffer is rejected before any read");

    /* Only RTP version 2 is accepted; the other three encodings are refused. */
    for (uint8_t nVersion = 0; nVersion < 4; nVersion++)
    {
        wire[0] = (uint8_t)(nVersion << 6);
        int nResult = XRTP_ParseHeader(&header, wire, sizeof(wire));
        CHECK(nVersion == 2 ? nResult == XRTP_HEADER_SIZE : nResult == XSTDERR,
            "Only version 2 passes the version gate");
    }
    return 0;
}

static int XTest_csrc(void)
{
    uint8_t wire[XRTP_HEADER_SIZE + SCRC_MAX * 4];
    xrtp_header_t header;

    for (uint8_t nCount = 1; nCount <= SCRC_MAX; nCount++)
    {
        size_t nCSRCSize = (size_t)nCount * 4;
        rtp_header(wire, 0, 0, nCount, 0, 8, 7, 9, 11);
        for (uint8_t i = 0; i < nCount; i++)
        {
            wire[XRTP_HEADER_SIZE + i * 4 + 0] = 0xaa;
            wire[XRTP_HEADER_SIZE + i * 4 + 1] = 0xbb;
            wire[XRTP_HEADER_SIZE + i * 4 + 2] = 0xcc;
            wire[XRTP_HEADER_SIZE + i * 4 + 3] = i;
        }

        int nOffset = XRTP_ParseHeader(&header, wire, XRTP_HEADER_SIZE + nCSRCSize);
        CHECK(nOffset == (int)(XRTP_HEADER_SIZE + nCSRCSize), "The payload offset skips the whole CSRC list");
        CHECK(header.nSCRCCount == nCount, "The announced CSRC count is reported back");
        for (uint8_t i = 0; i < nCount; i++)
            CHECK(header.SCRC[i] == (0xaabbcc00u | i), "Each CSRC entry is decoded from network order");
        for (size_t i = nCount; i < SCRC_MAX; i++)
            CHECK(header.SCRC[i] == 0, "Unused CSRC slots are cleared, not left stale");

        /* One byte short of the announced list must not read past the buffer. */
        CHECK(XRTP_ParseHeader(&header, wire, XRTP_HEADER_SIZE + nCSRCSize - 1) == XSTDERR,
            "A truncated CSRC list is rejected");
        CHECK(XRTP_ParseHeader(&header, wire, XRTP_HEADER_SIZE) == XSTDERR,
            "An announced but absent CSRC list is rejected");
    }
    return 0;
}

static int XTest_packet_blocks(void)
{
    /* 12 byte header, 4 byte payload header announcing two blocks, then
     * two length-prefixed blocks and two trailing unused bytes. */
    uint8_t wire[] = {
        0x80, 0x60, 0x00, 0x2a, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x07,
        0x12, 0x34, 0x56, 0xa2,             /* ident 0x123456, frag 0, type 2, 2 blocks */
        0x00, 0x03, 'a', 'b', 'c',
        0x00, 0x01, 'd',
        0xee, 0xff                          /* unused */
    };

    xrtp_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    int nParsed = XRTP_ParsePacket(&packet, wire, sizeof(wire));
    CHECK(nParsed == (int)(sizeof(wire) - 2), "Parsing stops after the last announced block");
    CHECK(packet.nIdent == 0x123456u, "The 24 bit identifier is big endian");
    CHECK(packet.nFragType == 2 && packet.nDataType == 2 && packet.nPackets == 2,
        "Fragment type, data type and block count share the fourth byte");
    CHECK(packet.pPayload == &wire[XRTP_HEADER_SIZE], "The payload points into the caller buffer");
    CHECK(packet.nPayloadSize == (int)(sizeof(wire) - XRTP_HEADER_SIZE), "Payload size covers the payload header");
    CHECK(packet.nLength == 1, "nLength holds the last block length");
    CHECK(packet.nUnusedBytes == 2, "Trailing bytes are reported as unused");

    /* Single fragment form: nPackets == 0 still consumes one length prefix. */
    uint8_t fragment[] = {
        0x80, 0x60, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x01, 0x40,             /* ident 1, frag 1, type 0, 0 blocks */
        0x00, 0x04, 'w', 'x', 'y', 'z'
    };
    memset(&packet, 0, sizeof(packet));
    CHECK(XRTP_ParsePacket(&packet, fragment, sizeof(fragment)) == (int)sizeof(fragment),
        "A zero-block packet consumes its single fragment");
    CHECK(packet.nPackets == 0 && packet.nFragType == 1, "The fragment flag survives the zero-block path");
    CHECK(packet.nLength == 4 && packet.nUnusedBytes == 0, "The fragment length is fully consumed");

    /* A CSRC list shifts the payload header; the offsets must follow it. */
    uint8_t csrc[] = {
        0x81, 0x60, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x09,             /* one CSRC entry */
        0x00, 0x00, 0x02, 0x00,             /* payload header, zero blocks */
        0x00, 0x02, 'h', 'i'
    };
    memset(&packet, 0, sizeof(packet));
    CHECK(XRTP_ParsePacket(&packet, csrc, sizeof(csrc)) == (int)sizeof(csrc),
        "The payload header is located after the CSRC list");
    CHECK(packet.nIdent == 2 && packet.nLength == 2, "CSRC entries do not shift the decoded payload fields");
    CHECK(packet.pPayload == &csrc[16], "The payload pointer skips the CSRC list");
    return 0;
}

static int XTest_packet_guards(void)
{
    uint8_t wire[] = {
        0x80, 0x60, 0x00, 0x2a, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x07,
        0x12, 0x34, 0x56, 0xa1,             /* one block */
        0x00, 0x03, 'a', 'b', 'c'
    };
    xrtp_packet_t packet;

    CHECK(XRTP_ParsePacket(NULL, wire, sizeof(wire)) == XSTDERR, "A missing packet is rejected");
    CHECK(XRTP_ParsePacket(&packet, NULL, sizeof(wire)) == XSTDERR, "A missing buffer is rejected");
    CHECK(XRTP_ParsePacket(&packet, wire, XRTP_HEADER_SIZE - 1) == XSTDERR, "A short RTP header is rejected");

    /* Every truncation of a valid packet must be refused, never over-read. */
    for (size_t n = XRTP_HEADER_SIZE; n < sizeof(wire); n++)
    {
        memset(&packet, 0, sizeof(packet));
        CHECK(XRTP_ParsePacket(&packet, wire, n) == XSTDERR, "Truncated packets are rejected at every length");
    }
    CHECK(XRTP_ParsePacket(&packet, wire, sizeof(wire)) == (int)sizeof(wire), "The untruncated packet still parses");

    /* A block that claims more data than the buffer carries. */
    uint8_t lying[] = {
        0x80, 0x60, 0x00, 0x2a, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x01,
        0xff, 0xff, 'a', 'b'
    };
    memset(&packet, 0, sizeof(packet));
    CHECK(XRTP_ParsePacket(&packet, lying, sizeof(lying)) == XSTDERR, "A block length past the buffer end is rejected");

    /* The second of two blocks runs past the end. */
    uint8_t lying_tail[] = {
        0x80, 0x60, 0x00, 0x2a, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x07,
        0x00, 0x00, 0x00, 0x02,
        0x00, 0x01, 'a',
        0x00, 0x09, 'b'
    };
    memset(&packet, 0, sizeof(packet));
    CHECK(XRTP_ParsePacket(&packet, lying_tail, sizeof(lying_tail)) == XSTDERR,
        "A later block overrunning the buffer is rejected");
    return 0;
}

static int XTest_assemble(void)
{
    xrtp_header_t header;
    memset(&header, 0, sizeof(header));
    header.nSequence = 0x1234;
    header.nTimeStamp = 0xa1b2c3d4u;
    header.nSSRC = 0x5a5a5a5au;

    const uint8_t payload[] = {'p', 'a', 'y', 0x00, 0xff};
    uint8_t *pWire = XRTP_AssemblePacket(&header, payload, sizeof(payload));
    CHECK(pWire != NULL, "Assembly returns a packet buffer");
    CHECK(pWire[0] == 0x80 && pWire[1] == 0x20, "The fixed network header is written in network order");
    CHECK(pWire[2] == 0x12 && pWire[3] == 0x34, "The sequence number is written in network order");
    CHECK(pWire[4] == 0xa1 && pWire[5] == 0xb2 && pWire[6] == 0xc3 && pWire[7] == 0xd4,
        "The timestamp is written in network order");
    CHECK(pWire[12] == 0 && pWire[13] == 0 && pWire[14] == 0 && pWire[15] == 0,
        "The payload header slot is zeroed");
    CHECK(memcmp(&pWire[16], payload, sizeof(payload)) == 0, "Binary payload bytes are copied verbatim");

    /* The assembled prefix must satisfy the parser it is paired with. */
    xrtp_header_t parsed;
    CHECK(XRTP_ParseHeader(&parsed, pWire, 16 + sizeof(payload)) == XRTP_HEADER_SIZE,
        "An assembled packet parses back as RTP version 2");
    CHECK(parsed.nSequence == header.nSequence && parsed.nTimeStamp == header.nTimeStamp,
        "Sequence and timestamp survive the round trip");
    CHECK(parsed.nPayloadType == 0x20 && parsed.nSCRCCount == 0, "The fixed header announces no CSRC entries");
    free(pWire);

    /* An oversized payload is clamped to the packet size instead of overflowing. */
    uint8_t *pBig = (uint8_t*)malloc(4096);
    CHECK(pBig != NULL, "Test allocation");
    memset(pBig, 0x5c, 4096);
    pWire = XRTP_AssemblePacket(&header, pBig, 4096);
    CHECK(pWire != NULL, "An oversized payload still assembles");
    CHECK(pWire[1499] == 0x5c, "The payload fills the packet up to the wire limit");
    free(pWire);
    free(pBig);

    /* A NULL payload leaves the header intact and copies nothing. */
    pWire = XRTP_AssemblePacket(&header, NULL, 32);
    CHECK(pWire != NULL, "A header-only packet assembles");
    CHECK(pWire[0] == 0x80 && pWire[2] == 0x12, "The header is written even without a payload");
    free(pWire);

    pWire = XRTP_AssemblePacket(&header, payload, 0);
    CHECK(pWire != NULL, "A zero length payload assembles");
    free(pWire);

    CHECK(XRTP_AssemblePacket(NULL, payload, sizeof(payload)) == NULL, "A missing header is rejected");
    return 0;
}

static int XTest_timestamp(void)
{
    /* The generator is a running clock: each call advances by 90kHz/rate. */
    uint32_t nFirst = XRTP_GetTimestamp(30.0f);
    uint32_t nSecond = XRTP_GetTimestamp(30.0f);
    CHECK(nSecond - nFirst == 3000, "A 30fps tick advances the 90kHz clock by 3000");

    uint32_t nThird = XRTP_GetTimestamp(90000.0f);
    CHECK(nThird - nSecond == 1, "A 90kHz tick advances the clock by one unit");

    uint32_t nFourth = XRTP_GetTimestamp(25.0f);
    CHECK(nFourth - nThird == 3600, "A 25fps tick advances the clock by 3600");
    return 0;
}

XTEST_MAIN(
    XTEST_CASE(header_fields),
    XTEST_CASE(header_guards),
    XTEST_CASE(csrc),
    XTEST_CASE(packet_blocks),
    XTEST_CASE(packet_guards),
    XTEST_CASE(assemble),
    XTEST_CASE(timestamp)
)
