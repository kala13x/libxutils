/*!
 *  @file libxutils/src/net/rtp.c
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of RTP packet parser functionality
 */

#include "xstd.h"
#include "rtp.h"

#define XRTP_PACKET_SIZE 1500
#define XRTP_HEADER_SIZE 12

/* Project specific payload header following the RTP header and CSRC list */
#define XRTP_PAYLOAD_HDR_SIZE 4

uint32_t XRTP_GetTimestamp(float fRate)
{
    static uint32_t nRTPTime = 0;
    nRTPTime += (uint32_t)(90000/fRate);
    return nRTPTime;
}

int XRTP_ParseHeader(xrtp_header_t *pHeader, const uint8_t *pData, size_t nLength)
{
    /* Check correct rtp version */
    if (pHeader == NULL || pData == NULL || nLength < XRTP_HEADER_SIZE ||
        (pData[0] & 0xc0) != (2 << 6)) return XSTDERR;

    uint16_t nSequence = 0;
    uint32_t nTimeStamp = 0;
    uint32_t nSSRC = 0;

    /* Packet comes from the wire and can have any alignment */
    memcpy(&nSequence, &pData[2], sizeof(nSequence));
    memcpy(&nTimeStamp, &pData[4], sizeof(nTimeStamp));
    memcpy(&nSSRC, &pData[8], sizeof(nSSRC));

    /* Parse RTP header */
    pHeader->nVersion = (pData[0] & 0xc0) >> 6;
    pHeader->nPadding = (pData[0] & 0x20) >> 5;
    pHeader->nExtension = (pData[0] & 0x10) >> 4;
    pHeader->nSCRCCount = (pData[0] & 0x0f);
    pHeader->nMarkerBit = (pData[1] & 0x80) >> 7;
    pHeader->nPayloadType = (pData[1] & 0x7F);
    pHeader->nSequence = ntohs(nSequence);
    pHeader->nTimeStamp = ntohl(nTimeStamp);
    pHeader->nSSRC = ntohl(nSSRC);

    uint32_t i;
    for (i = 0; i < SCRC_MAX; i++) pHeader->SCRC[i] = 0;
    if (!pHeader->nSCRCCount) return XRTP_HEADER_SIZE;

    /* Every announced CSRC entry must be present in the buffer */
    size_t nCSRCSize = (size_t)pHeader->nSCRCCount * 4;
    if (nLength - XRTP_HEADER_SIZE < nCSRCSize) return XSTDERR;

    for (i = 0; i < pHeader->nSCRCCount && i < SCRC_MAX; i++)
    {
        uint32_t nSCRC = 0;
        memcpy(&nSCRC, &pData[XRTP_HEADER_SIZE + i * 4], sizeof(nSCRC));
        pHeader->SCRC[i] = ntohl(nSCRC);
    }

    /* Offset to pPayload */
    return (int)(XRTP_HEADER_SIZE + nCSRCSize);
}

int XRTP_ParsePacket(xrtp_packet_t *pPacket, uint8_t *pData, size_t nLength)
{
    if (pPacket == NULL || pData == NULL) return XSTDERR;

    /* Parse RTP header */
    int nHeaderSize = XRTP_ParseHeader(&pPacket->rtpHeader, pData, nLength);
    if (nHeaderSize < 0) return XSTDERR;

    /* The payload header is mandatory and must fit in the buffer */
    size_t nOffset = (size_t)nHeaderSize;
    if (nLength - nOffset < XRTP_PAYLOAD_HDR_SIZE) return XSTDERR;

    /* Parse pPayload header */
    pPacket->pPayload = &pData[nOffset];
    pPacket->nIdent = (uint32_t)pData[nOffset] << 16;
    pPacket->nIdent += (uint32_t)pData[nOffset + 1] << 8;
    pPacket->nIdent += pData[nOffset + 2];
    pPacket->nFragType = (pData[nOffset + 3] & 0xc0) >> 6;
    pPacket->nDataType = (pData[nOffset + 3] & 0x30) >> 4;
    pPacket->nPackets = (pData[nOffset + 3] & 0x0F);
    pPacket->nPayloadSize = (int)(nLength - nOffset);

    nOffset += XRTP_PAYLOAD_HDR_SIZE;
    pPacket->nLength = 0;
    int i;

    /* Get data bytes from the blocks */
    for (i = 0; i < pPacket->nPackets; i++)
    {
        /* Corrupt packet (?) */
        if (nLength - nOffset < 2) return XSTDERR;
        pPacket->nLength = pData[nOffset] << 8;
        pPacket->nLength += pData[nOffset + 1];
        nOffset += 2;

        /* Block claims more data than the packet carries */
        if ((size_t)pPacket->nLength > nLength - nOffset) return XSTDERR;
        nOffset += (size_t)pPacket->nLength;
    }

    /* Get data bytes from the fragment */
    if (pPacket->nPackets == 0)
    {
        if (nLength - nOffset < 2) return XSTDERR;
        pPacket->nLength = pData[nOffset] << 8;
        pPacket->nLength += pData[nOffset + 1];
        nOffset += 2;

        if ((size_t)pPacket->nLength > nLength - nOffset) return XSTDERR;
        nOffset += (size_t)pPacket->nLength;
    }

    /* Unused bytes in the packet */
    pPacket->nUnusedBytes = (int)(nLength - nOffset);

    return (int)nOffset;
}

uint8_t *XRTP_AssemblePacket(xrtp_header_t *pHeader, const uint8_t *pData, size_t nLength)
{
    if (pHeader == NULL) return NULL;

    uint8_t *pPacket = (uint8_t*)malloc(XRTP_PACKET_SIZE);
    if (pPacket == NULL) return NULL;

    /* Get Ready */
    uint16_t nSequenceNumber = htons(pHeader->nSequence);
    uint32_t nTimeStamp = htonl(pHeader->nTimeStamp);
    uint32_t nVideoHeader = htonl(0x00000000);
    uint16_t nNetworkHeader = htons(0x8020);

    /* Assemble packet */
    memcpy(&pPacket[0], &nNetworkHeader, 2);
    memcpy(&pPacket[2], &nSequenceNumber, 2);
    memcpy(&pPacket[4], &nTimeStamp, 4);
    memcpy(&pPacket[8], &pHeader->nSSRC, 4);
    memcpy(&pPacket[12], &nVideoHeader, 4);

    size_t nCopySize = XSTD_MIN((size_t)XRTP_PACKET_SIZE - 16, nLength);
    if (pData != NULL && nCopySize) memcpy(&pPacket[16], pData, nCopySize);

    return pPacket;
}
