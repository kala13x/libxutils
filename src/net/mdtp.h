/*!
 *  @file libxutils/src/net/mdtp.h
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of Modern Data Transmit Protocol 
 * (MDTP) packet parser and assembler functionality
 */

#ifndef __XUTILS_XPACKET_H__
#define __XUTILS_XPACKET_H__

#include <stdlib.h>
#include <stdint.h>
#include "json.h"
#include "buf.h"

#define XPACKET_VERSION_STR     "1.0"
#define XPACKET_INFO_BYTES      4

#define XPACKET_HDR_INITIAL     256
#define XPACKET_PROTO_MAX       32
#define XPACKET_TYPE_MAX        128
#define XPACKET_TIME_MAX        64
#define XPACKET_VER_MAX         8
#define XPACKET_TZ_MAX          8

#define XPACKET_CB_PARSED       0
#define XPACKET_CB_UPDATE       1
#define XPACKET_CB_CLEAR        2

typedef struct XPacket xpacket_t;
typedef void(*xpacket_cb_t)(xpacket_t *pPacket, uint8_t nCallback);

typedef enum {
    XPACKET_ERR_NONE,
    XPACKET_ERR_ALLOC,
    XPACKET_INCOMPLETE,
    XPACKET_INVALID_ARGS,
    XPACKET_INVALID,
    XPACKET_BIGDATA,
    XPACKET_COMPLETE,
    XPACKET_PARSED
} xpacket_status_t;

typedef enum {
    XPACKET_TYPE_LITE = 0,
    XPACKET_TYPE_INVALID,
    XPACKET_TYPE_INCOMPLETE,
    XPACKET_TYPE_MULTY,
    XPACKET_TYPE_ERROR,
    XPACKET_TYPE_DUMMY,
    XPACKET_TYPE_DATA,
    XPACKET_TYPE_PING,
    XPACKET_TYPE_PONG,
    XPACKET_TYPE_INFO,
    XPACKET_TYPE_CMD,
    XPACKET_TYPE_EOS,
    XPACKET_TYPE_KA
} xpacket_type_t;

typedef struct XPacketHeader {
    xpacket_type_t eType;
    uint32_t nPacketID;
    uint32_t nSessionID;
    uint32_t nTimeStamp;
    uint32_t nPayloadSize;
    uint32_t nSSRCHash;
    xbool_t bCrypted;

    char sPayloadType[XPACKET_TYPE_MAX];
    char sVersion[XPACKET_VER_MAX];
    char sTime[XPACKET_TIME_MAX];
    char sTZ[XPACKET_TZ_MAX];
} xpacket_header_t;

struct XPacket {
    xpacket_header_t header;            // Packet header information
    xbyte_buffer_t rawData;             // Raw data of assembled packet
    xjson_obj_t *pHeaderObj;            // JSON object of parsed header
    xpacket_cb_t callback;              // Packet callback for parse/update
    uint32_t nHeaderLength;             // The length of raw packet header
    uint32_t nPacketSize;               // The size of whole packet
    uint8_t nAllocated;                 // Flag to check if packet is allocated
    uint8_t *pPayload;                  // Payload pointed from original raw data
    void *pUserData;                    // User data pointer for packet extension
};

#ifdef __cplusplus
extern "C" {
#endif

xpacket_type_t XPacket_GetType(const char *pType);
const char *XPacket_GetTypeStr(xpacket_type_t eType);
const char* XPacket_GetStatusStr(xpacket_status_t eStatus);

xpacket_status_t XPacket_Init(xpacket_t *pPacket, uint8_t *pData, uint32_t nSize);
xpacket_t *XPacket_New(uint8_t *pData, uint32_t nSize);
void XPacket_Clear(xpacket_t *pPacket);
void XPacket_Free(xpacket_t **pPacket);

xpacket_status_t XPacket_Parse(xpacket_t *pPacket, const uint8_t *pData, size_t nSize);
xpacket_status_t XPacket_UpdateHeader(xpacket_t *pPacket);
void XPacket_ParseHeader(xpacket_header_t *pHeader, xjson_obj_t *pHeaderObj);

xpacket_status_t XPacket_Create(xbyte_buffer_t *pBuffer, const char *pHeader, size_t nHdrLen, uint8_t *pData, size_t nSize);
xbyte_buffer_t *XPacket_Assemble(xpacket_t *pPacket);

const uint8_t *XPacket_GetHeader(xpacket_t *pPacket);
const uint8_t *XPacket_GetPayload(xpacket_t *pPacket);
size_t XPacket_GetSize(xpacket_t *pPacket);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XPACKET_H__ */
