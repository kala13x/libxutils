/*!
 *  @file libxutils/media/ts.h
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of TS packet parser functionality
 */

#ifndef __XUTILS_TS_H__
#define __XUTILS_TS_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define XTSPMT_DESC_DATA_MAX 1024
#define XTSPMT_DESCRIPTIONS_MAX 16
#define XTSPMT_STREAMS_MAX 16
#define XTSPAT_TABLE_MAX 64
#define XTS_PACKET_SIZE 188

typedef struct XAdaptationField {
    uint8_t adaptation_field_length:8;
    uint8_t discontinuity_indicator:1;
    uint8_t random_access_indicator:1;
    uint8_t elementary_stream_priority_indicator:1;
    uint8_t PCR_flag:1;
    uint8_t OPCR_flag:1;
    uint8_t splicing_point_flag:1;
    uint8_t transport_private_data_flag:1;
    uint8_t adaptation_field_extension_flag:1;
    /* Optional fields */
    uint64_t PCR:48;
    uint64_t OPCR:48;
    uint8_t splice_countdown:8;
    uint8_t transport_private_data_length:8;
    uint8_t *transport_private_data;
    /* Adaptation extension */
    uint8_t adaptation_extension_length:8;
    uint8_t legal_time_window:1;
    uint8_t piecewise_rate_flag:1;
    uint8_t seamless_splice_flag:1;
    uint8_t reserved:5;
    uint8_t LTW_valid_flag:1;
    uint16_t LTW_offset:15;
    uint8_t piecewise_reserved:2;
    uint32_t piecewise_rate:22;
    uint8_t splice_type:4;
    uint64_t DTS_next_access_unit:36;
} xadaptation_field_t;

typedef struct xts_packet_header_t_ {
    uint8_t sync_byte:8;
    uint8_t transport_error_indicator:1;
    uint8_t payload_unit_start_indicator:1;
    uint8_t transport_priority:1;
    uint16_t PID:13;
    uint8_t transports_crambling_control:2;
    uint8_t adaptation_field_flag:1;
    uint8_t payload_data_flag:1;
    uint8_t continuty_counter:4;
    xadaptation_field_t adaptationField;
} xts_packet_header_t;

typedef struct XTSParser {
    xts_packet_header_t header;
    uint32_t payload_size;
    uint8_t *payload_data;
} xts_packet_t;

typedef struct XPESPacket {
    /* Common */
    uint32_t packet_start_code_prefix:24;
    uint8_t stream_id:8;
    uint16_t PES_packet_length:16;
    uint8_t PES_scrambling_control:2;
    uint8_t PES_priority:1;
    uint8_t data_alignment_indicator:1;
    uint8_t copyright:1;
    uint8_t original_or_copy:1;
    uint8_t PTS_DTS_flags:2;
    uint8_t ESCR_flag:1;
    uint8_t ES_rate_flag:1;
    uint8_t DSM_trick_mode_flag:1;
    uint8_t additional_copy_info_flag:1;
    uint8_t PES_CRC_flag:1;
    uint8_t PES_extension_flag:1;
    uint8_t PES_header_data_length:1;
    /* Timestamps */
    uint64_t PTS;
    uint64_t DTS;
    /* ESCR */
    uint64_t ESCR_base;
    uint16_t ESCR_ext;
    uint32_t ES_rate:22;
    /* Trick control */
    uint8_t trick_mode_control:3;
    uint8_t field_id:2;
    uint8_t intra_slice_refresh:1;
    uint8_t frequency_truncation:2;
    uint8_t rep_cntrl:5;
    /* Additional */
    uint8_t additional_copy_info:7;
    uint16_t previous_PES_packet_CRC:16;
    uint8_t PES_private_data_flag:1;
    uint8_t pack_header_field_flag:1;
    uint8_t program_packet_sequence_counter_flag:1;
    uint8_t P_STD_buffer_flag:1;
    uint8_t PES_extension_flag_2:1;
    uint8_t private_data[16];
    uint8_t pack_field_length;
    uint8_t pack_field[256];
    uint8_t program_packet_sequence_counter:7;
    uint8_t MPEG1_MPEG2_identifier:1;
    uint8_t original_stuff_length:6;
    uint8_t P_STD_buffer_scale:1;
    uint16_t P_STD_buffer_size:13;
    uint8_t PES_extension_field_length:7;
    uint8_t PES_extension_field[128];
    /* Data and size */
    uint8_t* data;
    uint32_t data_size;
} xpes_packet_t;

typedef struct XPATTable {
    uint16_t program_number:16;
    uint16_t network_PID:13;
    uint16_t program_map_PID:13;
} xpat_table_t;

typedef struct XPAT {
    uint8_t pointer_field:8;
    uint8_t table_id:8;
    uint8_t section_syntax_indicator:1;
    uint8_t private_bit:1;
    uint8_t reserved_bits:2;
    uint16_t section_length:12;
    uint16_t transport_stream_id:16;
    uint8_t reserved:2;
    uint8_t version_number:5;
    uint8_t current_next_indicator:1;
    uint8_t section_number:8;
    uint8_t last_section_number:8;
    uint16_t programs;
    uint32_t CRC_32;
    xpat_table_t patTable[XTSPAT_TABLE_MAX];
} xpat_t;

typedef struct XPMTDesc {
    uint8_t descriptor_tag:8;
    uint8_t descriptor_length:8;
    uint8_t data[XTSPMT_DESC_DATA_MAX];
} xpmt_desc_t;

typedef struct XPMTStream {
    uint8_t stream_type:8;
    uint16_t elementary_PID:13;
    uint16_t ES_info_length:12;
    uint16_t desc_count;
    xpmt_desc_t desc[XTSPMT_DESCRIPTIONS_MAX];
} xpmt_stream_t;

typedef struct XPMT {
    uint8_t pointer_field:8;
    uint8_t table_id:8;
    uint8_t section_syntax_indicator:1;
    uint8_t private_bit:1;
    uint8_t reserved_bits:2;
    uint16_t section_length:12;
    uint16_t program_number:16;
    uint8_t reserved_2:2;
    uint8_t version_number:5;
    uint8_t current_next_indicator:1;
    uint8_t section_number:8;
    uint8_t last_section_number:8;
    uint8_t reserved_3:3;
    uint16_t PCR_PID:13;
    uint8_t reserved_4:4;
    uint16_t program_info_length:12;
    uint32_t desc_count;
    uint16_t stream_count;
    xpmt_desc_t desc[XTSPMT_DESCRIPTIONS_MAX];
    xpmt_stream_t streams[XTSPMT_STREAMS_MAX];
    uint32_t CRC_32;
} xpmt_t;

typedef struct XBitParser {
    uint32_t nOffset;
    uint64_t nData;
    uint8_t *pData;
    uint8_t nError;
    uint8_t nMask;
    size_t nSize;
} xbit_parser_t;

#ifdef __cplusplus
extern "C" {
#endif

void XBitParser_Init(xbit_parser_t *pParser, uint8_t *pData, size_t nSize);
void XBitParser_WriteBits(xbit_parser_t *pParser, uint8_t nBits, uint64_t nData);
uint64_t XBitParser_ReadBits(xbit_parser_t *pParser, uint8_t nBits);

int XTSParser_ParseAdaptationField(xbit_parser_t *pParser, xadaptation_field_t *pField);
int XTSParser_ParseHeader(xbit_parser_t *pParser, xts_packet_header_t *pHdr);
int XTSParser_ParsePES(xpes_packet_t *pPES, uint8_t* pData, int nSize);
int XTSParser_ParsePAT(xpat_t *pPat, uint8_t *pData, uint32_t nSize);
int XTSParser_ParsePMT(xpmt_t *pPmt, uint8_t *pData, uint32_t nSize);
int XTSParser_Parse(xts_packet_t *pTS, uint8_t *pData, uint32_t nSize);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_TS_H__ */