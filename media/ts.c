/*!
 *  @file libxutils/media/ts.c
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of TS packet parser functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "ts.h"

void XBitParser_Init(xbit_parser_t *pParser, uint8_t *pData, size_t nSize)
{
    pParser->pData = pData;
    pParser->nSize = nSize;
    pParser->nMask = 0x80;
    pParser->nOffset = 0;
    pParser->nError = 0;
    pParser->nData = 0;
}

uint32_t XBitParser_NextBit(xbit_parser_t *pParser)
{
    if (pParser->nMask != 0x01) 
    {
        pParser->nMask >>= 1;
        return pParser->nOffset;
    }

    pParser->nMask = 0x80;
    pParser->nOffset++;
    return pParser->nOffset;
}

uint8_t XBitParser_ReadBit(xbit_parser_t *pParser)
{
    uint8_t nRead = !!(pParser->nMask & pParser->pData[pParser->nOffset]);
    if (XBitParser_NextBit(pParser) >= pParser->nSize) pParser->nError = 1;
    return nRead;
}

uint64_t XBitParser_ReadBits(xbit_parser_t *pParser, uint8_t nBits)
{
    uint64_t nReadBits = 0;
    while(nBits-- && !pParser->nError)
    {
        nReadBits <<= 1;
        nReadBits |= XBitParser_ReadBit(pParser);
    }
    return nReadBits;
}

uint64_t XBitParser_WriteBit(xbit_parser_t *pParser, uint64_t nData, uint64_t nMask)
{
    if (nData & nMask) pParser->pData[pParser->nOffset] |= pParser->nMask;
    else pParser->pData[pParser->nOffset] &= ~pParser->nMask;

    XBitParser_NextBit(pParser);
    return nMask >> 1;
}

void XBitParser_WriteBits(xbit_parser_t *pParser, uint8_t nBits, uint64_t nData)
{
    uint64_t nWriteMask = (uint64_t)(((uint64_t)1) << (uint64_t)(nBits - 1));
    while(nBits--) nWriteMask = XBitParser_WriteBit(pParser, nData, nWriteMask);
}

int XTSParser_ParseHeader(xbit_parser_t *pParser, xts_packet_header_t *pHdr)
{
    pHdr->sync_byte = (uint8_t)XBitParser_ReadBits(pParser, 8);
    pHdr->transport_error_indicator = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pHdr->payload_unit_start_indicator = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pHdr->transport_priority = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pHdr->PID = (uint16_t)XBitParser_ReadBits(pParser, 13);
    pHdr->transports_crambling_control = (uint8_t)XBitParser_ReadBits(pParser, 2);
    pHdr->adaptation_field_flag = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pHdr->payload_data_flag = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pHdr->continuty_counter = (uint8_t)XBitParser_ReadBits(pParser, 4);
    return !pParser->nError;
}

int XTSParser_ParseAdaptationField(xbit_parser_t *pParser, xadaptation_field_t *pField)
{
    pField->adaptation_field_length = (uint8_t)XBitParser_ReadBits(pParser, 8);
    if (!pField->adaptation_field_length) return 0;

    pField->discontinuity_indicator = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pField->random_access_indicator = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pField->elementary_stream_priority_indicator = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pField->PCR_flag = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pField->OPCR_flag = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pField->splicing_point_flag = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pField->transport_private_data_flag = (uint8_t)XBitParser_ReadBits(pParser, 1);
    pField->adaptation_field_extension_flag = (uint8_t)XBitParser_ReadBits(pParser, 1);

    if (pField->PCR_flag) pField->PCR = XBitParser_ReadBits(pParser, 48);;
    if (pField->OPCR_flag) pField->OPCR = XBitParser_ReadBits(pParser, 48);
    if (pField->splicing_point_flag) pField->splice_countdown = (uint8_t)XBitParser_ReadBits(pParser, 8);

    if (pField->transport_private_data_flag && !pParser->nError)
    {
        pField->transport_private_data_length = (uint8_t)XBitParser_ReadBits(pParser, 8);
        pField->transport_private_data = &pParser->pData[pParser->nOffset];
        XBitParser_ReadBits(pParser, 8 * pField->transport_private_data_length);   
    }

    if (pField->adaptation_field_extension_flag)
    {
        pField->adaptation_extension_length = (uint8_t)XBitParser_ReadBits(pParser, 8);
        pField->legal_time_window = (uint8_t)XBitParser_ReadBits(pParser, 1);
        pField->piecewise_rate_flag = (uint8_t)XBitParser_ReadBits(pParser, 1);
        pField->seamless_splice_flag = (uint8_t)XBitParser_ReadBits(pParser, 1);
        pField->reserved = (uint8_t)XBitParser_ReadBits(pParser, 5);

        if (pField->legal_time_window)
        {
            pField->LTW_valid_flag = (uint8_t)XBitParser_ReadBits(pParser, 1);
            pField->LTW_offset = (uint16_t)XBitParser_ReadBits(pParser, 15);
        }

        if (pField->piecewise_rate_flag)
        {
            pField->piecewise_reserved = (uint8_t)XBitParser_ReadBits(pParser, 2);
            pField->piecewise_rate = (uint32_t)XBitParser_ReadBits(pParser, 22);
        }

        if (pField->seamless_splice_flag)
        {
            pField->splice_type = (uint8_t)XBitParser_ReadBits(pParser, 4);
            pField->DTS_next_access_unit = XBitParser_ReadBits(pParser, 36);
        }
    }

    return !pParser->nError;
}

int XTSParser_Parse(xts_packet_t *pTS, uint8_t *pData, uint32_t nSize)
{
    if (pData == NULL || nSize < XTS_PACKET_SIZE) return 0;
    xadaptation_field_t *pField = &pTS->header.adaptationField;
    pTS->payload_data = NULL;
    pTS->payload_size = 0;

    xbit_parser_t parser;
    XBitParser_Init(&parser, pData, nSize);

    XTSParser_ParseHeader(&parser, &pTS->header);
    if (pTS->header.sync_byte != 0x47) return 0;

    if (pTS->header.adaptation_field_flag)
        XTSParser_ParseAdaptationField(&parser, pField);

    if (pTS->header.payload_data_flag)
    {
        if (pTS->header.adaptation_field_flag)
        {
            int nOffset = pField->adaptation_field_length + 5;
            if (nOffset < XTS_PACKET_SIZE)
            {
                pTS->payload_size = XTS_PACKET_SIZE - nOffset;
                pTS->payload_data = &pData[nOffset];
            }
        }
        else
        {
            pTS->payload_size = XTS_PACKET_SIZE - 4;
            pTS->payload_data = &pData[4];
        }
    }

    return !parser.nError;
}

int XTSParser_ParsePAT(xpat_t *pPat, uint8_t *pData, uint32_t nSize)
{
    if (pData == NULL || nSize < 8) return 0;
    unsigned int i;

    xbit_parser_t parser;
    XBitParser_Init(&parser, pData, nSize);

    pPat->pointer_field = (uint8_t)XBitParser_ReadBits(&parser, 8);
    XBitParser_ReadBits(&parser, pPat->pointer_field * 8);
    pPat->table_id = (uint8_t)XBitParser_ReadBits(&parser, 8);
    pPat->section_syntax_indicator = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPat->private_bit = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPat->reserved_bits = (uint8_t)XBitParser_ReadBits(&parser, 2);
    pPat->section_length = (uint16_t)XBitParser_ReadBits(&parser, 12);
    pPat->transport_stream_id = (uint16_t)XBitParser_ReadBits(&parser, 16);
    pPat->reserved = (uint8_t)XBitParser_ReadBits(&parser, 2);
    pPat->version_number = (uint8_t)XBitParser_ReadBits(&parser, 5);
    pPat->current_next_indicator = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPat->section_number = (uint8_t)XBitParser_ReadBits(&parser, 8);
    pPat->last_section_number = (uint8_t)XBitParser_ReadBits(&parser, 8);
    pPat->programs = pPat->section_length/4 - 2;

    for (i = 0; i < pPat->programs && !parser.nError; i++)
    {
        if (i >= XTSPAT_TABLE_MAX) return 0;
        xpat_table_t *pTable = &pPat->patTable[i];

        pTable->program_number = (uint16_t)XBitParser_ReadBits(&parser, 16);
        XBitParser_ReadBits(&parser, 3);

        if (pTable->program_number == 0) 
            pTable->network_PID = (uint16_t)XBitParser_ReadBits(&parser, 13);
        else 
            pTable->program_map_PID = (uint16_t)XBitParser_ReadBits(&parser, 13);
    }

    pPat->CRC_32 = (uint32_t)XBitParser_ReadBits(&parser, 32);
    return !parser.nError;
}

int XTSParser_ParsePMT(xpmt_t *pPmt, uint8_t *pData, uint32_t nSize)
{
    if (pData == NULL || nSize < 8) return 0;
    uint32_t nRead, nDiff;
    nRead = nDiff = 0;

    xbit_parser_t parser;
    XBitParser_Init(&parser, pData, nSize);

    pPmt->pointer_field = (uint8_t)XBitParser_ReadBits(&parser, 8);
    XBitParser_ReadBits(&parser, pPmt->pointer_field * 8);
    pPmt->table_id = (uint8_t)XBitParser_ReadBits(&parser, 8);
    pPmt->section_syntax_indicator = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPmt->private_bit = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPmt->reserved_bits = (uint8_t)XBitParser_ReadBits(&parser, 2);
    pPmt->section_length = (uint16_t)XBitParser_ReadBits(&parser, 12);

    uint32_t nCurrentSize = (uint32_t)parser.nSize - parser.nOffset;
    if (pPmt->section_length > nCurrentSize) return 0;
    nDiff = nCurrentSize - pPmt->section_length;

    pPmt->program_number = (uint16_t)XBitParser_ReadBits(&parser, 16);
    pPmt->reserved_2 = (uint8_t)XBitParser_ReadBits(&parser, 2);
    pPmt->version_number = (uint8_t)XBitParser_ReadBits(&parser, 5);
    pPmt->current_next_indicator = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPmt->section_number = (uint8_t)XBitParser_ReadBits(&parser, 8);
    pPmt->last_section_number = (uint8_t)XBitParser_ReadBits(&parser, 8);
    pPmt->reserved_3 = (uint8_t)XBitParser_ReadBits(&parser, 3);
    pPmt->PCR_PID = (uint16_t)XBitParser_ReadBits(&parser, 13);
    pPmt->reserved_4 = (uint8_t)XBitParser_ReadBits(&parser, 4);
    pPmt->program_info_length = (uint16_t)XBitParser_ReadBits(&parser, 12);

    while (nRead < pPmt->program_info_length)
    {
        pPmt->desc[pPmt->desc_count].descriptor_tag = (uint8_t)XBitParser_ReadBits(&parser, 8);
        pPmt->desc[pPmt->desc_count].descriptor_length = (uint8_t)XBitParser_ReadBits(&parser, 8);

        uint8_t nSize = pPmt->desc[pPmt->desc_count].descriptor_length;
        uint8_t *pDstData = pPmt->desc[pPmt->desc_count].data;
        uint8_t *pSrcData = &parser.pData[parser.nOffset];
        memcpy(pDstData, pSrcData, nSize);

        XBitParser_ReadBits(&parser, nSize * 8);
        nRead += (nSize + 2);
        pPmt->desc_count++;
    }

    while (((parser.nSize - parser.nOffset) - nDiff) > 4)
    {
        xpmt_stream_t *pStream = &pPmt->streams[pPmt->stream_count];
        pStream->stream_type = (uint8_t)XBitParser_ReadBits(&parser, 8);
        XBitParser_ReadBits(&parser, 3);
        pStream->elementary_PID = (uint16_t)XBitParser_ReadBits(&parser, 13);
        XBitParser_ReadBits(&parser, 4);
        pStream->ES_info_length = (uint16_t)XBitParser_ReadBits(&parser, 12);
        
        int ES_info_length = pStream->ES_info_length;
        while(ES_info_length > 0)
        {
            xpmt_desc_t *pDesc = &pStream->desc[pStream->desc_count];
            if (ES_info_length <= 0) return 0;
            pDesc->descriptor_tag = (uint8_t)XBitParser_ReadBits(&parser, 8);
            ES_info_length--;
            if (ES_info_length <= 0) return 0;
            pDesc->descriptor_length = (uint8_t)XBitParser_ReadBits(&parser, 8); 
            ES_info_length--;
            if (ES_info_length < pDesc->descriptor_length) return 0;

            memcpy(pDesc->data, &parser.pData[parser.nOffset], pDesc->descriptor_length);
            ES_info_length -= pDesc->descriptor_length;
            XBitParser_ReadBits(&parser, pDesc->descriptor_length * 8);
            pStream->desc_count++;
        }

        pPmt->stream_count++;
    }

    return !parser.nError;
}

int XTSParser_ParsePES(xpes_packet_t *pPES, uint8_t* pData, int nSize)
{
    if (pData == NULL || nSize <= 0) return 0;
    memset(pPES, 0, sizeof(xpes_packet_t));

    xbit_parser_t parser;
    XBitParser_Init(&parser, pData, nSize);

    pPES->packet_start_code_prefix = (uint32_t)XBitParser_ReadBits(&parser, 24);
    pPES->stream_id = (uint8_t)XBitParser_ReadBits(&parser, 8);
    pPES->PES_packet_length = (uint16_t)XBitParser_ReadBits(&parser, 16);

    XBitParser_ReadBits(&parser, 2);
    pPES->PES_scrambling_control = (uint8_t)XBitParser_ReadBits(&parser, 2);
    pPES->PES_priority = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPES->data_alignment_indicator = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPES->copyright = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPES->original_or_copy = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPES->PTS_DTS_flags = (uint8_t)XBitParser_ReadBits(&parser, 2);
    pPES->ESCR_flag = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPES->ES_rate_flag = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPES->DSM_trick_mode_flag = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPES->additional_copy_info_flag = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPES->PES_CRC_flag = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPES->PES_extension_flag = (uint8_t)XBitParser_ReadBits(&parser, 1);
    pPES->PES_header_data_length = (uint8_t)XBitParser_ReadBits(&parser, 8);

    uint8_t* pExtPtr = &parser.pData[parser.nOffset];
    uint64_t nBuff[3];
    unsigned int i;

    if (pPES->PTS_DTS_flags == 2)
    {
        nBuff[2] = nBuff[1] = nBuff[0] = 0;
        XBitParser_ReadBits(&parser, 4);
        nBuff[2] = (uint8_t)XBitParser_ReadBits(&parser, 3);
        XBitParser_ReadBits(&parser, 1);
        nBuff[1] = (uint16_t)XBitParser_ReadBits(&parser, 15);
        XBitParser_ReadBits(&parser, 1);
        nBuff[0] = (uint16_t)XBitParser_ReadBits(&parser, 15);
        XBitParser_ReadBits(&parser, 1);
        pPES->PTS = nBuff[0] | (nBuff[1] << 15) | (nBuff[2] << 30);
    }
    else if (pPES->PTS_DTS_flags == 3)
    {
        nBuff[2] = nBuff[1] = nBuff[0] = 0;
        XBitParser_ReadBits(&parser, 4);
        nBuff[2] = (uint8_t)XBitParser_ReadBits(&parser, 3);
        XBitParser_ReadBits(&parser, 1);
        nBuff[1] = (uint16_t)XBitParser_ReadBits(&parser, 15);
        XBitParser_ReadBits(&parser, 1);
        nBuff[0] = (uint16_t)XBitParser_ReadBits(&parser, 15);
        XBitParser_ReadBits(&parser, 1);
        pPES->PTS = nBuff[0] | (nBuff[1] << 15) | (nBuff[2] << 30);

        nBuff[2] = nBuff[1] = nBuff[0] = 0;
        XBitParser_ReadBits(&parser, 4);
        nBuff[2] = (uint8_t)XBitParser_ReadBits(&parser, 3);
        XBitParser_ReadBits(&parser, 1);
        nBuff[1] = (uint16_t)XBitParser_ReadBits(&parser, 15);
        XBitParser_ReadBits(&parser, 1);
        nBuff[0] = (uint16_t)XBitParser_ReadBits(&parser, 15);
        XBitParser_ReadBits(&parser, 1);
        pPES->DTS = nBuff[0] | (nBuff[1] << 15) | (nBuff[2] << 30);
    }

    if (pPES->ESCR_flag == 1)
    {
        nBuff[2] = nBuff[1] = nBuff[0] = 0;
        XBitParser_ReadBits(&parser, 2);
        nBuff[2] = (uint8_t)XBitParser_ReadBits(&parser, 3);
        XBitParser_ReadBits(&parser, 1);
        nBuff[1] = (uint16_t)XBitParser_ReadBits(&parser, 15);
        XBitParser_ReadBits(&parser, 1);
        nBuff[0] = (uint16_t)XBitParser_ReadBits(&parser, 15);
        XBitParser_ReadBits(&parser, 1);
        pPES->ESCR_ext = (uint16_t)XBitParser_ReadBits(&parser, 9);
        XBitParser_ReadBits(&parser, 1);
        pPES->ESCR_base = nBuff[0] | (nBuff[1] << 15) | (nBuff[2] << 30);
    }

    if (pPES->ES_rate_flag == 1)
    {
        XBitParser_ReadBits(&parser, 1);
        pPES->ES_rate = (uint32_t)XBitParser_ReadBits(&parser, 22);
        XBitParser_ReadBits(&parser, 1);
    }

    if (pPES->DSM_trick_mode_flag == 1)
    {
        pPES->trick_mode_control = (uint8_t)XBitParser_ReadBits(&parser, 3);
        if (pPES->trick_mode_control == 0)
        {
            pPES->field_id = (uint8_t)XBitParser_ReadBits(&parser, 2);
            pPES->intra_slice_refresh = (uint8_t)XBitParser_ReadBits(&parser, 1);
            pPES->frequency_truncation = (uint8_t)XBitParser_ReadBits(&parser, 2);
        }
        else if (pPES->trick_mode_control == 1)
        {
            pPES->rep_cntrl = (uint8_t)XBitParser_ReadBits(&parser, 5);
        }
        else if (pPES->trick_mode_control == 2)
        {
            pPES->field_id = (uint8_t)XBitParser_ReadBits(&parser, 2);
            XBitParser_ReadBits(&parser, 3);
        }
        else if (pPES->trick_mode_control == 3)
        {
            pPES->field_id = (uint8_t)XBitParser_ReadBits(&parser, 2);
            pPES->intra_slice_refresh = (uint8_t)XBitParser_ReadBits(&parser, 1);
            pPES->frequency_truncation = (uint8_t)XBitParser_ReadBits(&parser, 2);
        }
        else if (pPES->trick_mode_control == 4)
        {
            pPES->rep_cntrl = (uint8_t)XBitParser_ReadBits(&parser, 5);
        }
        else
        {
            XBitParser_ReadBits(&parser, 5);
        }
    }

    if (pPES->additional_copy_info_flag == 1)
    {
        XBitParser_ReadBits(&parser, 1);
        pPES->additional_copy_info = (uint8_t)XBitParser_ReadBits(&parser, 7);
    }

    if (pPES->PES_CRC_flag == 1)
    {
        pPES->previous_PES_packet_CRC = (uint16_t)XBitParser_ReadBits(&parser, 16);
    }

    if (pPES->PES_extension_flag == 1)
    {
        pPES->PES_private_data_flag = (uint8_t)XBitParser_ReadBits(&parser, 1);
        pPES->pack_header_field_flag = (uint8_t)XBitParser_ReadBits(&parser, 1);
        pPES->program_packet_sequence_counter_flag = (uint8_t)XBitParser_ReadBits(&parser, 1);
        pPES->P_STD_buffer_flag = (uint8_t)XBitParser_ReadBits(&parser, 1);
        XBitParser_ReadBits(&parser, 3);
        pPES->PES_extension_flag_2 = (uint8_t)XBitParser_ReadBits(&parser, 1);

        if (pPES->PES_private_data_flag == 1)
        {
            for (i = 0; i < 16 && !parser.nError; i++)
            {
                pPES->private_data[i] = (uint8_t)XBitParser_ReadBits(&parser, 8);
            }
        }

        if (pPES->pack_header_field_flag == 1)
        {
            pPES->pack_field_length = (uint8_t)XBitParser_ReadBits(&parser, 8);
            for (i = 0; i < pPES->pack_field_length && !parser.nError; i++)
            {
                pPES->pack_field[i] = (uint8_t)XBitParser_ReadBits(&parser, 8);
            }
        }

        if (pPES->program_packet_sequence_counter_flag == 1)
        {
            XBitParser_ReadBits(&parser, 1);
            pPES->program_packet_sequence_counter = (uint8_t)XBitParser_ReadBits(&parser, 7);
            XBitParser_ReadBits(&parser, 1);
            pPES->MPEG1_MPEG2_identifier = (uint8_t)XBitParser_ReadBits(&parser, 1);
            pPES->original_stuff_length = (uint8_t)XBitParser_ReadBits(&parser, 6);
        }

        if (pPES->P_STD_buffer_flag == 1)
        {
            XBitParser_ReadBits(&parser, 2);
            pPES->P_STD_buffer_scale = (uint8_t)XBitParser_ReadBits(&parser, 1);
            pPES->P_STD_buffer_size = (uint16_t)XBitParser_ReadBits(&parser, 13);
        }

        if (pPES->PES_extension_flag_2 == 1)
        {
            XBitParser_ReadBits(&parser, 1);
            pPES->PES_extension_field_length = (uint8_t)XBitParser_ReadBits(&parser, 7);
            for (i = 0; i < pPES->PES_extension_field_length && !parser.nError; i++)
            {
                pPES->PES_extension_field[i] = (uint8_t)XBitParser_ReadBits(&parser, 8);
            }
        }
    }

    uint8_t *pOffset = &parser.pData[parser.nOffset];
    while((pOffset - pExtPtr) < pPES->PES_header_data_length)
    {
        XBitParser_ReadBits(&parser, 8);
        if (parser.nError) return 0;
        pOffset = &parser.pData[parser.nOffset];
    }

    pPES->data = &parser.pData[parser.nOffset];
    pPES->data_size = (uint32_t)parser.nSize - parser.nOffset;
    if (pPES->packet_start_code_prefix == 1) return 1;

    return !parser.nError;
}