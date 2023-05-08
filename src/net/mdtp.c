/*!
 *  @file libxutils/src/net/mdtp.c
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of Modern Data Transmit Protocol 
 * (MDTP) packet parser and assembler functionality
 */

#include "xstd.h"
#include "xstr.h"
#include "mdtp.h"

const char *XPacket_GetTypeStr(xpacket_type_t eType)
{
    switch(eType)
    {
        case XPACKET_TYPE_DUMMY: return "dummy";
        case XPACKET_TYPE_MULTY: return "multy";
        case XPACKET_TYPE_ERROR: return "error";
        case XPACKET_TYPE_LITE: return "lite";
        case XPACKET_TYPE_DATA: return "data";
        case XPACKET_TYPE_PING: return "ping";
        case XPACKET_TYPE_PONG: return "pong";
        case XPACKET_TYPE_INFO: return "info";
        case XPACKET_TYPE_CMD: return "cmd";
        case XPACKET_TYPE_EOS: return "eos";
        case XPACKET_TYPE_KA: return "ka";
        default: break;
    }

    return "invalid";
}

xpacket_type_t XPacket_GetType(const char *pType)
{
    if (!strncmp(pType, "dummy", 5)) return XPACKET_TYPE_DUMMY;
    else if (!strncmp(pType, "multy", 5)) return XPACKET_TYPE_MULTY;
    else if (!strncmp(pType, "error", 5)) return XPACKET_TYPE_ERROR;
    else if (!strncmp(pType, "lite", 4)) return XPACKET_TYPE_LITE;
    else if (!strncmp(pType, "data", 4)) return XPACKET_TYPE_DATA;
    else if (!strncmp(pType, "ping", 4)) return XPACKET_TYPE_PING;
    else if (!strncmp(pType, "pong", 4)) return XPACKET_TYPE_PONG;
    else if (!strncmp(pType, "info", 4)) return XPACKET_TYPE_INFO;
    else if (!strncmp(pType, "cmd", 3)) return XPACKET_TYPE_CMD;
    else if (!strncmp(pType, "eos", 3)) return XPACKET_TYPE_EOS;
    else if (!strncmp(pType, "ka", 2)) return XPACKET_TYPE_KA;
    return XPACKET_TYPE_INVALID;
}

void XPacket_Clear(xpacket_t *pPacket)
{
    XASSERT_VOID_RET(pPacket);

    if (pPacket->callback != NULL)
    {
        pPacket->callback(pPacket, XPACKET_CB_CLEAR);
        pPacket->callback = NULL;
    }

    if (pPacket->pHeaderObj != NULL)
    {
        XJSON_FreeObject(pPacket->pHeaderObj);
        pPacket->pHeaderObj = NULL;
    }

    pPacket->pUserData = NULL;
    XByteBuffer_Clear(&pPacket->rawData);
}

void XPacket_Free(xpacket_t **pPacket)
{
    XASSERT_VOID_RET((pPacket && *pPacket));
    xpacket_t *pMDTPPacket = *pPacket;

    XPacket_Clear(pMDTPPacket);
    if (pMDTPPacket->nAllocated)
    {
        free(pMDTPPacket);
        *pPacket = NULL;
    }
}

void XPacket_ParseHeader(xpacket_header_t *pHeader, xjson_obj_t *pHeaderObj)
{
    const char *pVersion = XJSON_GetString(XJSON_GetObject(pHeaderObj, "version"));
    if (pVersion != NULL && xstrused(pVersion)) xstrncpy(pHeader->sVersion, sizeof(pHeader->sVersion), pVersion);

    const char *pPacketType = XJSON_GetString(XJSON_GetObject(pHeaderObj, "packetType"));
    if (xstrused(pPacketType)) pHeader->eType = XPacket_GetType(pPacketType);

    pHeader->nTimeStamp = XJSON_GetU32(XJSON_GetObject(pHeaderObj, "timeStamp"));
    pHeader->nSessionID = XJSON_GetU32(XJSON_GetObject(pHeaderObj, "sessionId"));
    pHeader->nPacketID = XJSON_GetU32(XJSON_GetObject(pHeaderObj, "packetId"));

    xjson_obj_t *pPayloadObj = XJSON_GetObject(pHeaderObj, "payload");
    if (pPayloadObj != NULL)
    {
        const char *pPayloadType = XJSON_GetString(XJSON_GetObject(pPayloadObj, "payloadType"));
        if (xstrused(pPayloadType)) xstrncpy(pHeader->sPayloadType, sizeof(pHeader->sPayloadType), pPayloadType);

        pHeader->nPayloadSize = XJSON_GetU32(XJSON_GetObject(pPayloadObj, "payloadSize"));
        pHeader->nCrypted = XJSON_GetBool(XJSON_GetObject(pPayloadObj, "crypted"));
        pHeader->nSSRCHash = XJSON_GetU32(XJSON_GetObject(pPayloadObj, "ssrcHash"));
    }

    xjson_obj_t *pExtraObj = XJSON_GetObject(pHeaderObj, "extension");
    if (pExtraObj != NULL)
    {
        const char *pTime = XJSON_GetString(XJSON_GetObject(pExtraObj, "time"));
        if (xstrused(pTime)) xstrncpy(pHeader->sTime, sizeof(pHeader->sTime), pTime);

        const char *pTZ = XJSON_GetString(XJSON_GetObject(pExtraObj, "timeZone"));
        if (xstrused(pTZ)) xstrncpy(pHeader->sTZ, sizeof(pHeader->sTZ), pTZ);
    }
}

int XPacket_UpdateHeader(xpacket_t *pPacket)
{
    if (pPacket == NULL) return XSTDERR;
    xpacket_header_t *pHeader = &pPacket->header;

    if (pPacket->pHeaderObj == NULL)
    {
        pPacket->pHeaderObj = XJSON_NewObject(NULL, 1);
        if (pPacket->pHeaderObj == NULL) return XSTDERR;
    }

    xjson_obj_t *pHeaderObj = pPacket->pHeaderObj;
    pHeaderObj->nAllowUpdate = 1;

    if (pPacket->callback != NULL) pPacket->callback(pPacket, XPACKET_CB_UPDATE);
    if (pHeader->eType == XPACKET_TYPE_ERROR || pHeader->eType == XPACKET_TYPE_INVALID) return XSTDERR;
    const char *pPacketType = pHeader->eType == XPACKET_TYPE_LITE ? NULL : XPacket_GetTypeStr(pHeader->eType);

    if ((xstrused(pHeader->sVersion) && XJSON_AddString(pHeaderObj, "version", pHeader->sVersion) != XJSON_ERR_NONE) ||
        (xstrused(pPacketType) && XJSON_AddString(pHeaderObj, "packetType", pPacketType) != XJSON_ERR_NONE) ||
        (pHeader->nSessionID && XJSON_AddU32(pHeaderObj, "sessionId", pHeader->nSessionID) != XJSON_ERR_NONE) ||
        (pHeader->nTimeStamp && XJSON_AddU32(pHeaderObj, "timeStamp", pHeader->nTimeStamp) != XJSON_ERR_NONE) ||
        (pHeader->nPacketID && XJSON_AddU32(pHeaderObj, "packetId", pHeader->nPacketID) != XJSON_ERR_NONE))
    {
        pHeader->eType = XPACKET_TYPE_ERROR;
        return XSTDERR;
    }

    uint8_t nHaveDataType = xstrused(pHeader->sPayloadType);
    uint8_t nHaveTime = xstrused(pHeader->sTime);
    uint8_t nHaveTZ = xstrused(pHeader->sTZ);

    if (nHaveTime || nHaveTZ)
    {
        xjson_obj_t *pExtension = XJSON_GetOrCreateObject(pHeaderObj, "extension", 1);
        if (pExtension == NULL)
        {
            pHeader->eType = XPACKET_TYPE_ERROR;
            return XSTDERR;
        }

        if ((nHaveTime && XJSON_AddString(pExtension, "time", pHeader->sTime) != XJSON_ERR_NONE) ||
            (nHaveTZ && XJSON_AddString(pExtension, "timeZone", pHeader->sTZ) != XJSON_ERR_NONE))
        {
            pHeader->eType = XPACKET_TYPE_ERROR;
            return XSTDERR;
        }
    }

    if (pHeader->nPayloadSize)
    {
        xjson_obj_t *pPayloadObj = XJSON_GetOrCreateObject(pHeaderObj, "payload", 1);
        if (pPayloadObj == NULL)
        {
            pHeader->eType = XPACKET_TYPE_ERROR;
            return XSTDERR;
        }

        if ((nHaveDataType && XJSON_AddString(pPayloadObj, "payloadType", pHeader->sPayloadType) != XJSON_ERR_NONE) ||
            (pHeader->nCrypted && XJSON_AddBool(pPayloadObj, "crypted", pHeader->nCrypted) != XJSON_ERR_NONE) ||
            XJSON_AddU32(pPayloadObj, "payloadSize", pHeader->nPayloadSize) != XJSON_ERR_NONE)
        {
            pHeader->eType = XPACKET_TYPE_ERROR;
            return XSTDERR;
        }
    }

    return XSTDOK;
}

int XPacket_Init(xpacket_t *pPacket, uint8_t *pData, uint32_t nSize)
{
    xpacket_header_t *pHeader = &pPacket->header;
    memset(pHeader, 0, sizeof(xpacket_header_t));

    pHeader->eType = XPACKET_TYPE_LITE;
    pHeader->nPayloadSize = nSize;

    XByteBuffer_Init(&pPacket->rawData, 0, 0);
    pPacket->nHeaderLength = 0;
    pPacket->nPacketSize = 0;
    pPacket->nAllocated = 0;
    pPacket->pPayload = pData;
    pPacket->pUserData = NULL;
    pPacket->callback = NULL;

    pPacket->pHeaderObj = XJSON_NewObject(NULL, 1);
    return (pPacket->pHeaderObj == NULL) ? XSTDERR : XSTDOK;
}

xpacket_t *XPacket_New(uint8_t *pData, uint32_t nSize)
{
    xpacket_t *pPacket = (xpacket_t*)malloc(sizeof(xpacket_t));
    if (pPacket == NULL) return NULL;

    if (XPacket_Init(pPacket, pData, nSize) == XSTDERR)
    {
        free(pPacket);
        return NULL;
    }

    pPacket->nAllocated = 1;
    return pPacket;
}

int XPacket_Create(xbyte_buffer_t *pBuffer, const char *pHeader, size_t nHdrLen, uint8_t *pData, size_t nSize)
{
    if ((pBuffer == NULL || pHeader == NULL || !nHdrLen) ||
        (!XByteBuffer_Add(pBuffer, (uint8_t*)&nHdrLen, sizeof(nHdrLen))) ||
        (!XByteBuffer_Add(pBuffer, (uint8_t*)pHeader, nHdrLen)) ||
        (pData != NULL && nSize && !XByteBuffer_Add(pBuffer, pData, nSize)))
    {
        XByteBuffer_Clear(pBuffer);
        return XSTDERR;
    }

    return XSTDOK;
}

xbyte_buffer_t *XPacket_Assemble(xpacket_t *pPacket)
{
    if (XPacket_UpdateHeader(pPacket) == XSTDERR) return NULL;
    xpacket_header_t *pHeader = &pPacket->header;
    xjson_writer_t jsonWriter;

    XJSON_InitWriter(&jsonWriter, NULL, XPACKET_HDR_INITIAL);
    XByteBuffer_Reset(&pPacket->rawData);

    if (XJSON_WriteObject(pPacket->pHeaderObj, &jsonWriter))
    {
        XPacket_Create(
            &pPacket->rawData,
            jsonWriter.pData,
            jsonWriter.nLength,
            pPacket->pPayload,
            pHeader->nPayloadSize
        );

        pPacket->nHeaderLength = (uint32_t)jsonWriter.nLength;
        XPacket_ParseHeader(&pPacket->header, pPacket->pHeaderObj);
    }

    XJSON_DestroyWriter(&jsonWriter);
    return &pPacket->rawData;
}

uint8_t *XPacket_Parse(xpacket_t *pPacket, const uint8_t *pData, size_t nSize)
{
    xpacket_header_t *pHdr = &pPacket->header;
    memset(pHdr, 0, sizeof(xpacket_header_t));
    pHdr->eType = XPACKET_TYPE_INVALID;

    if (pData == NULL || nSize <= 0) return NULL;
    XByteBuffer_Init(&pPacket->rawData, 0, 0);

    pPacket->nHeaderLength = (*(uint32_t*)pData);
    pPacket->pHeaderObj = NULL;
    pPacket->pPayload = NULL;
    pPacket->pUserData = NULL;
    pPacket->callback = NULL;
    pPacket->nPacketSize = 0;
    pPacket->nAllocated = 0;

    if ((int)(nSize - XPACKET_INFO_BYTES) < pPacket->nHeaderLength)
    {
        pHdr->eType = XPACKET_TYPE_INCOMPLETE;
        return NULL;
    }

    xstrncpy(pHdr->sVersion, sizeof(pHdr->sVersion), XPACKET_VERSION_STR);
    const char *pHeader = &((char*)pData)[XPACKET_INFO_BYTES];

    if (pPacket->nHeaderLength > 0)
    {
        xjson_t json;
        if (!XJSON_Parse(&json, pHeader, pPacket->nHeaderLength))
        {
            XJSON_Destroy(&json);
            return NULL;
        }

        pPacket->pHeaderObj = json.pRootObj;
        XPacket_ParseHeader(pHdr, pPacket->pHeaderObj);

        size_t nPayloadOffset = XPACKET_INFO_BYTES + pPacket->nHeaderLength;
        pPacket->nPacketSize = (uint32_t)nPayloadOffset + pHdr->nPayloadSize;

        if (nSize < pPacket->nPacketSize)
        {
            pHdr->eType = XPACKET_TYPE_INCOMPLETE;
            XJSON_FreeObject(pPacket->pHeaderObj);
            pPacket->pHeaderObj = NULL;
            return NULL;
        }

        if (pHdr->nPayloadSize) pPacket->pPayload = (uint8_t*)&pData[nPayloadOffset];
        if (pPacket->callback != NULL) pPacket->callback(pPacket, XPACKET_CB_PARSED);
    }

    return pPacket->pPayload;
}

const uint8_t *XPacket_GetHeader(xpacket_t *pPacket)
{
    if (pPacket == NULL || !pPacket->nHeaderLength) return NULL;
    size_t nRequired = XPACKET_INFO_BYTES + pPacket->nHeaderLength;
    if (pPacket->rawData.nUsed < nRequired) return NULL;
    return &pPacket->rawData.pData[XPACKET_INFO_BYTES];
}

const uint8_t *XPacket_GetPayload(xpacket_t *pPacket)
{
    if (pPacket == NULL || !pPacket->header.nPayloadSize) return NULL;
    size_t nPayloadOffset = XPACKET_INFO_BYTES + pPacket->nHeaderLength;
    size_t nActualSize = nPayloadOffset + pPacket->header.nPayloadSize;
    if (pPacket->rawData.nUsed < nActualSize) return NULL;
    return &pPacket->rawData.pData[nPayloadOffset];
}