/*!
 *  @file libxutils/src/media/rtp.h
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of RTP packet parser functionality
 */

#ifndef __XUTILS_RTP_H__
#define __XUTILS_RTP_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define SCRC_MAX 15

typedef struct XRTPHeader{
    uint32_t nVersion;
    uint32_t nPadding;
    uint32_t nSequence;
    uint32_t nExtension;
    uint32_t nSCRCCount;
    uint32_t nMarkerBit;
    uint32_t nPayloadType;
    uint32_t SCRC[SCRC_MAX];
    uint32_t nTimeStamp;
    uint32_t nSSRC;
} xrtp_header_t;

typedef struct XRTPPacket {
    xrtp_header_t rtpHeader;
    uint32_t nIdent;
    uint8_t *pPayload;
    int nPayloadSize;
    int nUnusedBytes;
    int nFragType;
    int nDataType;
    int nPackets;
    int nLength;
} xrtp_packet_t;

#ifdef __cplusplus
extern "C" {
#endif

uint32_t XRTP_GetTimestamp(float fRate);
int XRTP_ParseHeader(xrtp_header_t *pHeader, const uint8_t *pData, size_t nLength);
int XRTP_ParsePacket(xrtp_packet_t *pPacket, uint8_t *pData, size_t nLength);
uint8_t *XRTP_AssemblePacket(xrtp_header_t *pHeader, const uint8_t *pData, size_t nLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_RTP_H__ */