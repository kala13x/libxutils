/*!
 *  @file libxutils/src/net/rtp.c
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of RTP packet parser functionality
 */

#include "xstd.h"
#include "rtp.h"

#define XRTP_PACKET_SIZE 1500
#define XRTP_HEADER_SIZE 12

uint32_t XRTP_GetTimestamp(float fRate)
{
    static uint32_t nRTPTime = 0;
    nRTPTime += (uint32_t)(90000/fRate);
    return nRTPTime;
}

int XRTP_ParseHeader(xrtp_header_t *pHeader, const uint8_t *pData, size_t nLength)
{
    /* Check correct rtp version */
    if (pData == NULL || nLength < XRTP_HEADER_SIZE ||
        (pData[0] & 0xc0) != (2 << 6)) return XSTDERR;

    /* Parse RTP header */
    pHeader->nVersion = (pData[0] & 0xc0) >> 6;
    pHeader->nPadding = (pData[0] & 0x40) >> 5;
    pHeader->nExtension = (pData[0] & 0x20) >> 4;
    pHeader->nSCRCCount = (pData[0] & 0x0f);
    pHeader->nMarkerBit = (pData[1] & 0x80) >> 7;
    pHeader->nPayloadType = (pData[1] & 0x7F);
    pHeader->nSequence = ntohs(((unsigned short *) pData)[1]);
    pHeader->nTimeStamp = ntohl(((unsigned int *) pData)[1]);
    pHeader->nSSRC = ntohl(((unsigned int *) pData)[2]);

    uint32_t i;
    if (!pHeader->nSCRCCount)
    {
        for (i = 0; i < SCRC_MAX; i++) pHeader->SCRC[i] = 0;
        return XRTP_HEADER_SIZE;
    }

    for (i = 0; i < pHeader->nSCRCCount; i++)
    {
        pHeader->SCRC[i] = ntohl(((unsigned int *) pData)[3 + i]);
        if (i >= SCRC_MAX) break;
    }

    /* Offset to pPayload */
    return XRTP_HEADER_SIZE + pHeader->nSCRCCount * 4;
}

int XRTP_ParsePacket(xrtp_packet_t *pPacket, uint8_t *pData, size_t nLength)
{
    /* Parse RTP header */
    int nOffset = XRTP_ParseHeader(&pPacket->rtpHeader, pData, nLength);
    if (nOffset < 0) return XSTDERR;

    /* Parse pPayload header */
    pPacket->pPayload = &pData[nOffset];
    pPacket->nIdent = pData[nOffset++] << 16;
    pPacket->nIdent += pData[nOffset++] << 8;
    pPacket->nIdent += pData[nOffset++];
    pPacket->nFragType = (pData[nOffset] & 0xc0) >> 6;
    pPacket->nDataType = (pData[nOffset] & 0x30) >> 4;
    pPacket->nPackets = (pData[nOffset] & 0x0F);
    pPacket->nPayloadSize = (int)nLength - 4 * ((int)pPacket->rtpHeader.nSCRCCount + 3);
 
    nOffset++;
    int i;

    /* Get data bytes from the blocks */
    for (i = 0; i < pPacket->nPackets; i++)
    {
        /* Corrupt packet (?) */
        if (nOffset >= nLength) return XSTDERR;
        pPacket->nLength = pData[nOffset++] << 8;
        pPacket->nLength += pData[nOffset++];
        nOffset += pPacket->nLength;
    }

    /* Get data bytes from the fragment */
    if (pPacket->nPackets == 0)
    {
        pPacket->nLength = pData[nOffset++] << 8;
        pPacket->nLength += pData[nOffset++];
        nOffset += pPacket->nLength;
    }

    /* Unused bytes in the packet */
    if (nLength - nOffset > 0)
        pPacket->nUnusedBytes = (int)nLength - nOffset;

    return nOffset;
}

uint8_t *XRTP_AssemblePacket(xrtp_header_t *pHeader, const uint8_t *pData, size_t nLength)
{
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

    size_t nCopySize = XSTD_MIN(XRTP_PACKET_SIZE - 16, nLength);
    if (nCopySize) memcpy(&pPacket[16], &pData, nCopySize);

    return pPacket;
}
