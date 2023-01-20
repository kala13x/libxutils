/*!
 *  @file libxutils/src/crypt.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of the various crypt algorithms
 */

#include "xstd.h"
#include "xstr.h"
#include "xaes.h"
#include "xbuf.h"
#include "crypt.h"

#ifdef _WIN32
#pragma warning(disable : 4146)
#define htobe32(x) _byteswap_ulong(x)
#define be32toh(x) _byteswap_ulong(x)
#endif

#define XMD5_LEFTROTATE(x, c) (((x) << (c)) | ((x) >> (32 - (c))))

#define XSHA_UPPER(x) w[(x) & 0x0F]
#define XSHA_CH(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define XSHA_MAJ(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define XSHA_ROR32(x, n) ((x >> n) | (x << ((sizeof(x) << 3) - n)))

#define XSHA_SIGMA1(x) (XSHA_ROR32(x, 2) ^ XSHA_ROR32(x, 13) ^ XSHA_ROR32(x, 22))
#define XSHA_SIGMA2(x) (XSHA_ROR32(x, 6) ^ XSHA_ROR32(x, 11) ^ XSHA_ROR32(x, 25))
#define XSHA_SIGMA3(x) (XSHA_ROR32(x, 7) ^ XSHA_ROR32(x, 18) ^ (x >> 3))
#define XSHA_SIGMA4(x) (XSHA_ROR32(x, 17) ^ XSHA_ROR32(x, 19) ^ (x >> 10))

static const uint8_t XSHA256P[64] =
    {
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

static const uint32_t XSHA256K[64] =
    {
        0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
        0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
        0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
        0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
        0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
        0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
        0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
        0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
    };

static const unsigned char g_base64DecTable[XBASE64_TABLE_SIZE] =
        {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x3f,
            0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 
            0x3c, 0x3d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 
            0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
            0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 
            0x17, 0x18, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 
            0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
            0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 
            0x31, 0x32, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

static const unsigned char g_base64UrlDecTable[XBASE64_TABLE_SIZE] =
        {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
            0x3c, 0x3d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
            0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
            0x17, 0x18, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
            0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
            0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
            0x31, 0x32, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

static const uint32_t g_crc32Table[] = {
      0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
      0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
      0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
      0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
      0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
      0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
      0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
      0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
      0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
      0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
      0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
      0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
      0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
      0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
      0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
      0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
      0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
      0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
      0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
      0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
      0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
      0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
      0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
      0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
      0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
      0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
      0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
      0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
      0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
      0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
      0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
      0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
      0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
      0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
      0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
      0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
      0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
      0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
      0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
      0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
      0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
      0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
      0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
      0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
      0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
      0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
      0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
      0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
      0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
      0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
      0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
      0x2d02ef8dL
   };

static const uint32_t g_intRadians[64] = 
        {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
            0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
            0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
            0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
            0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
            0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
            0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
            0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
            0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
        };

static const char g_base64EncTable[] = 
        {
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
            'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
            'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
            'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
            'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
            'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
            'w', 'x', 'y', 'z', '0', '1', '2', '3',
            '4', '5', '6', '7', '8', '9', '+', '/'
        };

static const char g_base64UrlEncTable[] =
        {
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
            'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
            'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
            'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
            'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
            'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
            'w', 'x', 'y', 'z', '0', '1', '2', '3',
            '4', '5', '6', '7', '8', '9', '-', '_'
        };

static const char g_charMap[XCHAR_MAP_SIZE] =
        { 
            'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 
            'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 
            'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'
        };

static const uint32_t g_Radians[] = 
        {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
        };

////////////////////////////////////////////////////////
// SHA-256 computing implementation for C/C++ based on
// pseudocode for the SHA-256 algorithm from Wikipedia.
////////////////////////////////////////////////////////

void XSHA256_Init(xsha256_t *pSha)
{
    memset(pSha, 0, sizeof(xsha256_t));
    pSha->uDigest.hBytes[0] = 0x6A09E667;
    pSha->uDigest.hBytes[1] = 0xBB67AE85;
    pSha->uDigest.hBytes[2] = 0x3C6EF372;
    pSha->uDigest.hBytes[3] = 0xA54FF53A;
    pSha->uDigest.hBytes[4] = 0x510E527F;
    pSha->uDigest.hBytes[5] = 0x9B05688C;
    pSha->uDigest.hBytes[6] = 0x1F83D9AB;
    pSha->uDigest.hBytes[7] = 0x5BE0CD19;
}

void XSHA256_Update(xsha256_t *pSha, const uint8_t *pData, size_t nLength)
{
    while (nLength > 0)
    {
        size_t nPart = XSTD_MIN(nLength, XSHA256_BLOCK_SIZE - pSha->nSize);
        memcpy(pSha->uBlock.block + pSha->nSize, pData, nPart);

        pSha->nTotalSize += nPart;
        pSha->nSize += nPart;
        pData = pData + nPart;
        nLength -= nPart;

        if (pSha->nSize == XSHA256_BLOCK_SIZE)
        {
            XSHA256_ProcessBlock(pSha);
            pSha->nSize = 0;
        }
    }
}

void XSHA256_Final(xsha256_t *pSha, uint8_t *pDigest)
{
    size_t nPaddingSize = (pSha->nSize < 56) ? (56 - pSha->nSize) : (120 - pSha->nSize);
    size_t i, nTotalSize = pSha->nTotalSize * 8;

    XSHA256_Update(pSha, XSHA256P, nPaddingSize);
    pSha->uBlock.wBytes[14] = htobe32((uint32_t) (nTotalSize >> 32));
    pSha->uBlock.wBytes[15] = htobe32((uint32_t) nTotalSize);
    XSHA256_ProcessBlock(pSha);

    for (i = 0; i < 8; i++) pSha->uDigest.hBytes[i] = htobe32(pSha->uDigest.hBytes[i]);
    if (pDigest != NULL) memcpy(pDigest, pSha->uDigest.digest, XSHA256_DIGEST_SIZE);
}

void XSHA256_FinalRaw(xsha256_t *pSha, uint8_t *pDigest)
{
    uint8_t i;
    for (i = 0; i < 8; i++) pSha->uDigest.hBytes[i] = htobe32(pSha->uDigest.hBytes[i]);
    memcpy(pDigest, pSha->uDigest.digest, XSHA256_DIGEST_SIZE);
    for (i = 0; i < 8; i++) pSha->uDigest.hBytes[i] = be32toh(pSha->uDigest.hBytes[i]);
}

void XSHA256_ProcessBlock(xsha256_t *pSha)
{
    uint32_t *w = pSha->uBlock.wBytes;
    uint32_t nReg[8];
    uint8_t i;

    for (i = 0; i < 8; i++) nReg[i] = pSha->uDigest.hBytes[i];
    for (i = 0; i < 16; i++) w[i] = be32toh(w[i]);

    for (i = 0; i < 64; i++)
    {
        if (i >= 16) XSHA_UPPER(i) += XSHA_SIGMA4(XSHA_UPPER(i + 14)) + XSHA_UPPER(i + 9) + XSHA_SIGMA3(XSHA_UPPER(i + 1));
        uint32_t nT1 = nReg[7] + XSHA_SIGMA2(nReg[4]) + XSHA_CH(nReg[4], nReg[5], nReg[6]) + XSHA256K[i] + XSHA_UPPER(i);
        uint32_t nT2 = XSHA_SIGMA1(nReg[0]) + XSHA_MAJ(nReg[0], nReg[1], nReg[2]);

        nReg[7] = nReg[6];
        nReg[6] = nReg[5];
        nReg[5] = nReg[4];
        nReg[4] = nReg[3] + nT1;
        nReg[3] = nReg[2];
        nReg[2] = nReg[1];
        nReg[1] = nReg[0];
        nReg[0] = nT1 + nT2;
    }

    for (i = 0; i < 8; i++)
        pSha->uDigest.hBytes[i] += nReg[i];
}

XSTATUS XCrypt_SHA256U(uint8_t *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength)
{
    if (nSize < XSHA256_DIGEST_SIZE ||
        pOutput == NULL) return XSTDERR;

    xsha256_t xsha;
    XSHA256_Init(&xsha);
    XSHA256_Update(&xsha, pInput, nLength);
    XSHA256_Final(&xsha, pOutput);

    return XSTDOK;
}

XSTATUS XCrypt_SHA256H(char *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength)
{
    if (nSize < XSHA256_LENGTH + 1 ||
        pOutput == NULL) return XSTDERR;

    uint8_t i, nDigest[XSHA256_DIGEST_SIZE];
    XCrypt_SHA256U(nDigest, nSize, pInput, nLength);

    for (i = 0; i < XSHA256_DIGEST_SIZE; i++)
        xstrncpyf(pOutput + i * 2, 3, "%02x", (unsigned int)nDigest[i]);

    pOutput[XSHA256_LENGTH] = XSTR_NUL;
    return XSTDOK;
}

char* XCrypt_SHA256(const uint8_t *pInput, size_t nLength)
{
    size_t nSize = XSHA256_LENGTH + 1;
    char *pHash = (char*)malloc(nSize);
    if (pHash == NULL) return NULL;

    XCrypt_SHA256H(pHash, nSize, pInput, nLength);
    return pHash;
}

static XSTATUS XCrypt_HMAC(uint8_t *pOut, size_t nSize, const uint8_t *pFirst, size_t nFLen, const uint8_t *pSec, size_t nSLen)
{
    size_t nBufLen = nFLen + nSLen;
    uint8_t* pPadBuf = (uint8_t*)malloc(nBufLen);
    XASSERT_RET(pPadBuf, XSTDERR);

    memcpy(pPadBuf, pFirst, nFLen);
    memcpy(pPadBuf + nFLen, pSec, nSLen);
    XCrypt_SHA256U(pOut, nSize, pPadBuf, nBufLen);

    free(pPadBuf);
    return XSTDOK;
}

XSTATUS XCrypt_HS256U(uint8_t *pOutput, size_t nSize, const uint8_t* pData, const size_t nLength, const uint8_t* pKey, const size_t nKeyLen)
{
    if (pOutput == NULL || nSize < XSHA256_DIGEST_SIZE) return XSTDERR;
    uint8_t hash[XSHA256_DIGEST_SIZE];
    uint8_t kIpad[XSHA256_BLOCK_SIZE];
    uint8_t kOpad[XSHA256_BLOCK_SIZE];
    uint8_t kBuff[XSHA256_BLOCK_SIZE];

    memset(kIpad, 0x36, XSHA256_BLOCK_SIZE);
    memset(kOpad, 0x5c, XSHA256_BLOCK_SIZE);
    memset(kBuff, 0, sizeof(kBuff));

    if (nKeyLen <= XSHA256_BLOCK_SIZE) memcpy(kBuff, pKey, nKeyLen);
    else XCrypt_SHA256U(kBuff, sizeof(kBuff), pKey, nKeyLen);

    uint8_t i;
    for (i = 0; i < XSHA256_BLOCK_SIZE; i++)
    {
        kIpad[i] ^= kBuff[i];
        kOpad[i] ^= kBuff[i];
    }

    XCrypt_HMAC(hash, sizeof(hash), kIpad, sizeof(kIpad), pData, nLength);
    XCrypt_HMAC(pOutput, nSize, kOpad, sizeof(kOpad), hash, sizeof(hash));

    return XSTDOK;
}

XSTATUS XCrypt_HS256H(char *pOutput, size_t nSize, const uint8_t* pData, const size_t nLength, const uint8_t* pKey, const size_t nKeyLen)
{
    if (pOutput == NULL || nSize < XSHA256_LENGTH + 1) return XSTDERR;
    uint8_t i, hash[XSHA256_DIGEST_SIZE];

    XCrypt_HS256U(hash, sizeof(hash), pData, nLength, pKey, nKeyLen);
    for (i = 0; i < sizeof(hash); i++) xstrncpyf(pOutput + i * 2, 3, "%02x", (unsigned int)hash[i]);

    pOutput[XSHA256_LENGTH] = '\0';
    return XSTDOK;
}

char* XCrypt_HS256B(const uint8_t* pData, const size_t nLength, const uint8_t* pKey, const size_t nKeyLen, size_t *pOutLen)
{
    uint8_t hash[XSHA256_DIGEST_SIZE];
    *pOutLen = sizeof(hash);

    XCrypt_HS256U(hash, sizeof(hash), pData, nLength, pKey, nKeyLen);
    return XCrypt_Base64Url(hash, pOutLen);
}

char* XCrypt_HS256(const uint8_t* pData, const size_t nLength, const uint8_t* pKey, const size_t nKeyLen)
{
    size_t nSize = XSHA256_LENGTH + 1;
    char *pOutput = (char*)malloc(nSize);
    if (pOutput == NULL) return NULL;

    XCrypt_HS256H(pOutput, nSize, pData, nLength, pKey, nKeyLen);
    return pOutput;
}

////////////////////////////////////////////////////////
// End of SHA-256 implementation
////////////////////////////////////////////////////////

uint32_t XCrypt_CRC32(const uint8_t *pInput, size_t nLength)
{
    if (pInput == NULL || !nLength) return 0;
    uint32_t nCRC = 0;
    unsigned int i;

    for (i = 0;  i < nLength; i++)
    {
        uint16_t nIndex = (nCRC ^ pInput[i]) & 0xff;
        nCRC = g_crc32Table[nIndex] ^ (nCRC >> 8);
    }

    return nCRC;
}

uint32_t XCrypt_CRC32B(const uint8_t *pInput, size_t nLength)
{
    if (pInput == NULL || !nLength) return 0;
    uint32_t nCRC = 0xFFFFFFFF;
    unsigned int i;

    for (i = 0;  i < nLength; i++)
    {
        uint32_t nByte = pInput[i];
        nCRC = nCRC ^ nByte;

        nCRC = (nCRC >> 4) ^ (nCRC ^ (nCRC << 4));
        nCRC = (nCRC >> 4) ^ (nCRC ^ (nCRC << 4));
        nCRC = (nCRC >> 4) ^ (nCRC ^ (nCRC << 4));
        nCRC = (nCRC >> 4) ^ (nCRC ^ (nCRC << 4));
    }

    return ~nCRC;
}

char* XCrypt_MD5(const uint8_t *pInput, size_t nLength) 
{
    if (pInput == NULL || !nLength) return NULL;
    size_t nNewLen = 0;

    uint32_t hash0 = 0x67452301;
    uint32_t hash1 = 0xefcdab89;
    uint32_t hash2 = 0x98badcfe;
    uint32_t hash3 = 0x10325476;

    for (nNewLen = (nLength * 8 + 1); (nNewLen % 512) != 448; nNewLen++);
    nNewLen /= 8;

    uint8_t *pMessage = (uint8_t*)calloc(nNewLen + 64, 1);
    if (pMessage == NULL) return NULL;

    memcpy(pMessage, pInput, nLength);
    pMessage[nLength] = 128;

    uint32_t nBitsLen = 8 * (uint32_t)nLength;
    memcpy(pMessage + nNewLen, &nBitsLen, 4);

    uint32_t i, f, g;
    int nOffset = 0;

    for (nOffset = 0; (size_t)nOffset < nNewLen; nOffset += (512 / 8))
    {
        uint32_t *w = (uint32_t*)(pMessage + nOffset);
        uint32_t a = hash0;
        uint32_t b = hash1;
        uint32_t c = hash2;
        uint32_t d = hash3;

        for(i = 0; i < 64; i++) 
        {
            if (i < 16) 
            {
                f = (b & c) | ((~b) & d);
                g = i;
            } 
            else if (i < 32) 
            {
                f = (d & b) | ((~d) & c);
                g = (5 * i + 1) % 16;
            } 
            else if (i < 48) 
            {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;          
            } 
            else 
            {
                f = c ^ (b | (~d));
                g = (7 * i) % 16;
            }

            uint32_t temp = d;
            d = c;
            c = b;
            uint32_t nX = a + f + g_intRadians[i] + w[g];
            b = b + XMD5_LEFTROTATE(nX, g_Radians[i]);
            a = temp;
        }

        hash0 += a;
        hash1 += b;
        hash2 += c;
        hash3 += d;
    }

    char *pOutput = (char*)malloc(XMD5_LENGTH + 1);
    if (pOutput == NULL) return NULL;

    xstrncpyf(pOutput, XMD5_LENGTH + 1,
        "%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x"
        "%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x",
        ((uint8_t *)&hash0)[0], ((uint8_t *)&hash0)[1], 
        ((uint8_t *)&hash0)[2], ((uint8_t *)&hash0)[3],
        ((uint8_t *)&hash1)[0], ((uint8_t *)&hash1)[1], 
        ((uint8_t *)&hash1)[2], ((uint8_t *)&hash1)[3],
        ((uint8_t *)&hash2)[0], ((uint8_t *)&hash2)[1], 
        ((uint8_t *)&hash2)[2], ((uint8_t *)&hash2)[3],
        ((uint8_t *)&hash3)[0], ((uint8_t *)&hash3)[1], 
        ((uint8_t *)&hash3)[2], ((uint8_t *)&hash3)[3]);

    free(pMessage);
    return pOutput;
}

char *XCrypt_Casear(const char *pInput, size_t nLength, size_t nKey)
{
    if (pInput == NULL || !nLength) return NULL;
    char *pRetVal = (char*)malloc(nLength + 1);
    if (pRetVal == NULL) return NULL;

    size_t nChars = sizeof(g_charMap);
    size_t i, x, nHalf = nChars / 2;

    for (i = 0; i < nLength; i++)
    {
        char cByte = pInput[i];

        for (x = 0; x < nChars; x++)
        {
            size_t nStep = x + nKey;
            if (x < nHalf) while (nStep >= nHalf) nStep -= nHalf;
            else { nStep += nHalf; while (nStep >= nChars) nStep -= nHalf; }
            if (pInput[i] == g_charMap[x]) { cByte = g_charMap[nStep]; break; }
        }

        pRetVal[i] = cByte;
    }

    pRetVal[nLength] = '\0';
    return pRetVal;
}

char *XDecrypt_Casear(const char *pInput, size_t nLength, size_t nKey)
{
    if (pInput == NULL || !nLength) return NULL;
    char *pRetVal = (char*)malloc(nLength + 1);
    if (pRetVal == NULL) return NULL;

    size_t nChars = sizeof(g_charMap);
    size_t i, x, nHalf = nChars / 2;

    for (i = 0; i < nLength; i++)
    {
        char cByte = pInput[i];

        for (x = 0; x < nChars; x++)
        {
            size_t nStep = x - nKey;
            while(nStep < XCHAR_MAP_SIZE) nStep += nHalf;

            if (x < nHalf) while (nStep >= nHalf) nStep -= nHalf;
            else { nStep += nHalf; while (nStep >= nChars) nStep -= nHalf; }
            if (pInput[i] == g_charMap[x]) { cByte = g_charMap[nStep]; break; }
        }

        pRetVal[i] = cByte;
    }

    pRetVal[nLength] = '\0';
    return pRetVal;
}

char* XCrypt_Reverse(const char *pInput, size_t nLength)
{
    if (pInput == NULL || !nLength) return NULL;
    char *pReversed = (char*)malloc(nLength + 1);
    if (pReversed == NULL) return NULL;

    size_t i;
    for (i = 0; i < nLength; i++)
        pReversed[i] = pInput[(nLength - (i + 1))];

    pReversed[nLength] = '\0';
    return pReversed;
}

uint8_t* XCrypt_XOR(const uint8_t *pInput, size_t nLength, const uint8_t *pKey, size_t nKeyLen)
{
    if (pInput == NULL || !nLength ||
        pKey == NULL || !nKeyLen) return NULL;

    uint8_t *pCrypted = (uint8_t*)malloc(nLength + 1);
    if (pCrypted == NULL) return NULL;

    size_t i;
    for (i = 0; i < nLength; ++i)
        pCrypted[i] = pInput[i] ^ pKey[i % nKeyLen];

    pCrypted[nLength] = '\0';
    return pCrypted;
}

char *XCrypt_Base64(const uint8_t *pInput, size_t *pLength)
{
    if (pInput == NULL || pLength == NULL || !(*pLength)) return NULL;
    size_t i, j, nLength = *pLength;
    size_t nOutLength = ((nLength + 2) / 3) * 4;
    if (nOutLength < nLength) return NULL;

    char *pEncodedData = (char *)calloc(1, nOutLength + 1);
    if (pEncodedData == NULL) return NULL;

    for (i = 0, j = 0; i < nLength;) 
    {
        uint32_t nOctetA = i < nLength ? (unsigned char)pInput[i++] : 0;
        uint32_t nOctetB = i < nLength ? (unsigned char)pInput[i++] : 0;
        uint32_t nOctetC = i < nLength ? (unsigned char)pInput[i++] : 0;
        uint32_t nTriple = (nOctetA << 0x10) + (nOctetB << 0x08) + nOctetC;

        pEncodedData[j++] = g_base64EncTable[(nTriple >> 3 * 6) & 0x3F];
        pEncodedData[j++] = g_base64EncTable[(nTriple >> 2 * 6) & 0x3F];
        pEncodedData[j++] = g_base64EncTable[(nTriple >> 1 * 6) & 0x3F];
        pEncodedData[j++] = g_base64EncTable[(nTriple >> 0 * 6) & 0x3F];
    }

    uint8_t nModTable[3] = {0, 2, 1};
    for (i = 0; i < nModTable[nLength % 3]; i++)
        pEncodedData[nOutLength - 1 - i] = '=';

    while (pEncodedData[nOutLength] == '\0') nOutLength--;
    *pLength = nOutLength + 1;

    return pEncodedData;
}

char *XDecrypt_Base64(const uint8_t *pInput, size_t *pLength)
{
    if (pInput == NULL || pLength == NULL || !(*pLength)) return NULL;
    size_t i, j, nLength = *pLength;

    while (nLength % 4 != 0) nLength++;
    size_t nOutLength = nLength / 4 * 3 + 1;
    nLength = *pLength;

    while (pInput[--nLength] == '=') nOutLength--;
    nLength = *pLength;

    char *pDecodedData = (char *)calloc(1, nOutLength + 1);
    if (pDecodedData == NULL) return NULL;

    for (i = 0, j = 0; i < nLength;)
    {
        uint32_t nSextetA = pInput[i] == '=' ? 0 & i++ : g_base64DecTable[pInput[i++]];
        uint32_t nSextetB = i >= nLength || pInput[i] == '=' ? 0 & i++ : g_base64DecTable[pInput[i++]];
        uint32_t nSextetC = i >= nLength || pInput[i] == '=' ? 0 & i++ : g_base64DecTable[pInput[i++]];
        uint32_t nSextetD = i >= nLength || pInput[i] == '=' ? 0 & i++ : g_base64DecTable[pInput[i++]];

        uint32_t nTriple = (nSextetA << 3 * 6) + (nSextetB << 2 * 6) 
                         + (nSextetC << 1 * 6) + (nSextetD << 0 * 6);

        if (j < nOutLength-1) pDecodedData[j++] = (nTriple >> 2 * 8) & 0xFF;
        if (j < nOutLength-1) pDecodedData[j++] = (nTriple >> 1 * 8) & 0xFF;
        if (j < nOutLength-1) pDecodedData[j++] = (nTriple >> 0 * 8) & 0xFF;
    }

    while (pDecodedData[nOutLength] == '\0') nOutLength--;
    *pLength = nOutLength + 1;

    return pDecodedData;
}

char *XDecrypt_Base64Url(const uint8_t *pInput, size_t *pLength)
{
    if (pInput == NULL || pLength == NULL || !(*pLength)) return NULL;
    size_t i, j, nLength = *pLength;

    while (nLength % 4 != 0) nLength++;
    size_t nOutLength = nLength / 4 * 3 + 1;
    nLength = *pLength;

    while (pInput[--nLength] == '-') nOutLength--;
    nLength = *pLength;

    char *pDecodedData = (char *)calloc(1, nOutLength + 1);
    if (pDecodedData == NULL) return NULL;

    for (i = 0, j = 0; i < nLength;)
    {
        uint32_t nSextetA = pInput[i] == '-' ? 0 & i++ : g_base64UrlDecTable[pInput[i++]];
        uint32_t nSextetB = i >= nLength || pInput[i] == '-' ? 0 & i++ : g_base64UrlDecTable[pInput[i++]];
        uint32_t nSextetC = i >= nLength || pInput[i] == '-' ? 0 & i++ : g_base64UrlDecTable[pInput[i++]];
        uint32_t nSextetD = i >= nLength || pInput[i] == '-' ? 0 & i++ : g_base64UrlDecTable[pInput[i++]];

        uint32_t nTriple = (nSextetA << 3 * 6) + (nSextetB << 2 * 6) 
                         + (nSextetC << 1 * 6) + (nSextetD << 0 * 6);

        if (j < nOutLength-1) pDecodedData[j++] = (nTriple >> 2 * 8) & 0xFF;
        if (j < nOutLength-1) pDecodedData[j++] = (nTriple >> 1 * 8) & 0xFF;
        if (j < nOutLength-1) pDecodedData[j++] = (nTriple >> 0 * 8) & 0xFF;
    }

    while (pDecodedData[nOutLength] == '\0') nOutLength--;
    *pLength = nOutLength + 1;

    return pDecodedData;
}

char *XCrypt_Base64Url(const uint8_t *pInput, size_t *pLength)
{
    if (pInput == NULL || pLength == NULL || !(*pLength)) return NULL;
    size_t i, j, nLength = *pLength;

    size_t nOutLength = ((nLength + 2) / 3) * 4;
    if (nOutLength < nLength) return NULL;

    char *pEncodedData = (char *)calloc(1, nOutLength + 1);
    if (pEncodedData == NULL) return NULL;

    for (i = 0, j = 0; i < nLength;) 
    {
        uint32_t nOctetA = i < nLength ? (unsigned char)pInput[i++] : 0;
        uint32_t nOctetB = i < nLength ? (unsigned char)pInput[i++] : 0;
        uint32_t nOctetC = i < nLength ? (unsigned char)pInput[i++] : 0;
        uint32_t nTriple = (nOctetA << 0x10) + (nOctetB << 0x08) + nOctetC;

        pEncodedData[j++] = g_base64UrlEncTable[(nTriple >> 3 * 6) & 0x3F];
        pEncodedData[j++] = g_base64UrlEncTable[(nTriple >> 2 * 6) & 0x3F];
        pEncodedData[j++] = g_base64UrlEncTable[(nTriple >> 1 * 6) & 0x3F];
        pEncodedData[j++] = g_base64UrlEncTable[(nTriple >> 0 * 6) & 0x3F];
    }

    uint8_t nModTable[3] = {0, 2, 1};
    for (i = 0; i < nModTable[nLength % 3]; i++)
        pEncodedData[nOutLength - 1 - i] = '\0';

    while (pEncodedData[nOutLength] == '\0') nOutLength--;
    *pLength = nOutLength + 1;

    return pEncodedData;
}

uint8_t* XCrypt_AES(const uint8_t *pInput, size_t *pLength, const uint8_t *pKey, size_t nKeyLen, const uint8_t *pIV)
{
    if (pInput == NULL || pKey == NULL || !nKeyLen ||
        pLength == NULL || !(*pLength)) return NULL;

    xaes_context_t ctx;
    XAES_SetKey(&ctx, pKey, nKeyLen, pIV);
    return XAES_Encrypt(&ctx, pInput, pLength);
}

uint8_t* XDecrypt_AES(const uint8_t *pInput, size_t *pLength, const uint8_t *pKey, size_t nKeyLen, const uint8_t *pIV)
{
    if (pInput == NULL || pKey == NULL || !nKeyLen ||
        pLength == NULL || !(*pLength)) return NULL;

    xaes_context_t ctx;
    XAES_SetKey(&ctx, pKey, nKeyLen, pIV);
    return XAES_Decrypt(&ctx, pInput, pLength);
}

uint8_t* XCrypt_HEX(const uint8_t *pInput, size_t *pLength, const char* pSpace, size_t nColumns, xbool_t bLowCase)
{
    if (pInput == NULL || pLength == NULL || !(*pLength)) return NULL;
    size_t i, nCount = 0, nLength = *pLength;

    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, nLength, 0);
    if (!buffer.nSize) return NULL;

    for (i = 0; i < nLength; i++)
    {
        const char *pFmt = bLowCase ? "%02x%s" : "%02X%s";
        const char *pDlmt = xstrused(pSpace) ? pSpace : XSTR_EMPTY;

        if (XByteBuffer_AddFmt(&buffer, pFmt, pInput[i], pDlmt) <= 0)
        {
            XByteBuffer_Clear(&buffer);
            *pLength = 0;
            return NULL;
        }

        if (!nColumns) continue;
        nCount++;

        if (nCount == nColumns)
        {
            if (XByteBuffer_AddByte(&buffer, '\n') <= 0)
            {
                XByteBuffer_Clear(&buffer);
                *pLength = 0;
                return NULL;
            }

            nCount = 0;
        }
    }

    *pLength = buffer.nUsed;
    return buffer.pData;
}

uint8_t* XDecrypt_HEX(const uint8_t *pInput, size_t *pLength, xbool_t bLowCase)
{
    if (pInput == NULL || pLength == NULL || !(*pLength)) return NULL;
    const char *pFmt = bLowCase ? " %02x%n" : " %02X%n";
    const char *pData = (const char*)pInput;

    xbyte_buffer_t buffer;
    XByteBuffer_Init(&buffer, *pLength, 0);
    if (!buffer.nSize) return NULL;

    uint8_t nVal = 0;
    int nOffset = 0;

#ifdef _WIN32
    while (sscanf_s(pData, pFmt, (unsigned int*)&nVal, &nOffset) == 1)
#else
    while (sscanf(pData, pFmt, (unsigned int*)&nVal, &nOffset) == 1)
#endif
    {
        if (XByteBuffer_AddByte(&buffer, nVal) <= 0)
        {
            XByteBuffer_Clear(&buffer);
            *pLength = 0;
            return NULL;
        }

        pData += nOffset;
    }

    *pLength = buffer.nUsed;
    return buffer.pData;
}

xcrypt_chipher_t XCrypt_GetCipher(const char *pCipher)
{
    if (!strncmp(pCipher, "aes", 3)) return XC_AES;
    else if (!strncmp(pCipher, "hex", 3)) return XC_HEX;
    else if (!strncmp(pCipher, "xor", 3)) return XC_XOR;
    else if (!strncmp(pCipher, "md5", 3)) return XC_MD5;
    else if (!strncmp(pCipher, "crc32", 5)) return XC_CRC32;
    else if (!strncmp(pCipher, "crc32b", 6)) return XC_CRC32B;
    else if (!strncmp(pCipher, "casear", 6)) return XC_CASEAR;
    else if (!strncmp(pCipher, "base64url", 9)) return XC_BASE64URL;
    else if (!strncmp(pCipher, "base64", 6)) return XC_BASE64;
    else if (!strncmp(pCipher, "hs256", 5)) return XC_HS256;
    else if (!strncmp(pCipher, "sha256", 6)) return XC_SHA256;
    else if (!strncmp(pCipher, "reverse", 7)) return XC_REVERSE;
    return XC_INVALID;
}

const char* XCrypt_GetCipherStr(xcrypt_chipher_t eCipher)
{
    switch (eCipher)
    {
        case XC_AES: return "aes";
        case XC_HEX: return "hex";
        case XC_XOR: return "xor";
        case XC_MD5: return "md5";
        case XC_CRC32: return "crc32";
        case XC_CRC32B: return "crc32b";
        case XC_CASEAR: return "casear";
        case XC_BASE64: return "base64";
        case XC_BASE64URL: return "base64url";
        case XC_HS256: return "h256";
        case XC_SHA256: return "sha256";
        case XC_REVERSE: return "reverse";
        case XC_MULTY: return "multy";
        default:
            break;
    }

    return "invalid";
}

static xbool_t XCrypt_NeedsKey(xcrypt_chipher_t eCipher)
{
    switch (eCipher)
    {
        case XC_AES:
        case XC_XOR:
        case XC_CASEAR:
        case XC_HS256:
            return XTRUE;
        case XC_HEX:
        case XC_MD5:
        case XC_CRC32:
        case XC_BASE64:
        case XC_BASE64URL:
        case XC_SHA256:
        case XC_REVERSE:
            return XFALSE;
        default:
            break;
    }

    return XFALSE;
}

void XCrypt_ErrorCallback(xcrypt_ctx_t *pCtx, const char *pStr, ...)
{
    if (pCtx->callback == NULL) return;
    char sBuff[XSTR_MIN];

    va_list args;
    va_start(args, pStr);
    xstrncpyarg(sBuff, sizeof(sBuff), pStr, args);
    va_end(args);

    pCtx->callback(XCB_ERROR, sBuff, pCtx->pUserPtr);
}

xbool_t XCrypt_KeyCallback(xcrypt_ctx_t *pCtx, xcrypt_chipher_t eCipher, xcrypt_key_t *pKey)
{
    memset(pKey, 0, sizeof(xcrypt_key_t));
    pKey->eCipher = eCipher;

    if (!XCrypt_NeedsKey(eCipher) ||
        pCtx->callback == NULL) return XTRUE;

    return pCtx->callback(XCB_KEY, pKey, pCtx->pUserPtr);
}

void XCrypt_Init(xcrypt_ctx_t *pCtx, xbool_t bDecrypt, char *pCiphers, xcrypt_cb_t callback, void *pUser)
{
    pCtx->callback = callback;
    pCtx->pUserPtr = pUser;
    pCtx->bDecrypt = bDecrypt;
    pCtx->pCiphers = pCiphers;
    pCtx->nColumns = 0;
}

uint8_t* XCrypt_Single(xcrypt_ctx_t *pCtx, xcrypt_chipher_t eCipher, const uint8_t *pInput, size_t *pLength)
{
    xcrypt_key_t encKey;
    if (!XCrypt_KeyCallback(pCtx, eCipher, &encKey)) return XFALSE;

    const uint8_t *pKey = (const uint8_t*)encKey.sKey;
    size_t nKeyLength = encKey.nLength;

    uint8_t *pCrypted = NULL;
    uint32_t nCRC32 = 0;

    switch (eCipher)
    {
        case XC_CRC32: nCRC32 = XCrypt_CRC32(pInput, *pLength); break;
        case XC_CRC32B: nCRC32 = XCrypt_CRC32B(pInput, *pLength); break;
        case XC_AES: pCrypted = XCrypt_AES(pInput, pLength, pKey, nKeyLength, NULL); break;
        case XC_HEX: pCrypted = XCrypt_HEX(pInput, pLength, XSTR_SPACE, pCtx->nColumns, XFALSE); break;
        case XC_XOR: pCrypted = XCrypt_XOR(pInput, *pLength, pKey, nKeyLength); break;
        case XC_MD5: pCrypted = (uint8_t*)XCrypt_MD5(pInput, *pLength); *pLength = XMD5_LENGTH; break;
        case XC_SHA256: pCrypted = (uint8_t*)XCrypt_SHA256(pInput, *pLength); *pLength = XSHA256_LENGTH; break;
        case XC_HS256: pCrypted = (uint8_t*)XCrypt_HS256(pInput, *pLength, pKey, nKeyLength); *pLength = XSHA256_LENGTH; break;
        case XC_CASEAR: pCrypted = (uint8_t*)XCrypt_Casear((const char*)pInput, *pLength, atoi(encKey.sKey)); break;
        case XC_BASE64: pCrypted = (uint8_t*)XCrypt_Base64(pInput, pLength); break;
        case XC_BASE64URL: pCrypted = (uint8_t*)XCrypt_Base64Url(pInput, pLength); break;
        case XC_REVERSE: pCrypted = (uint8_t*)XCrypt_Reverse((const char*)pInput, *pLength); break;
        default: break;
    }

    if (nCRC32 > 0)
    {
        pCrypted = (uint8_t*)malloc(XCRC32_MAX_SIZE);
        if (pCrypted == NULL)
        {
            XCrypt_ErrorCallback(pCtx, "Can not allocate memory for CRC32 buffer");
            return NULL;
        }

        *pLength = xstrncpyf((char*)pCrypted, XCRC32_MAX_SIZE, "%u", nCRC32);
    }

    if (pCrypted == NULL)
    {
        XCrypt_ErrorCallback(pCtx,
            "Failed to encrypt data with cipher: %s",
                XCrypt_GetCipherStr(eCipher));

        return NULL;
    }

    return pCrypted;
}

uint8_t* XDecrypt_Single(xcrypt_ctx_t *pCtx, xcrypt_chipher_t eCipher, const uint8_t *pInput, size_t *pLength)
{
    xcrypt_key_t decKey;
    if (!XCrypt_KeyCallback(pCtx, eCipher, &decKey)) return XFALSE;

    const uint8_t *pKey = (const uint8_t*)decKey.sKey;
    size_t nKeyLength = decKey.nLength;
    uint8_t *pDecrypted = NULL;

    switch (eCipher)
    {
        case XC_HEX: pDecrypted = XDecrypt_HEX(pInput, pLength, XFALSE); break;
        case XC_AES: pDecrypted = XDecrypt_AES(pInput, pLength, pKey, nKeyLength, NULL); break;
        case XC_XOR: pDecrypted = XCrypt_XOR(pInput, *pLength, pKey, nKeyLength); break;
        case XC_CASEAR: pDecrypted = (uint8_t*)XDecrypt_Casear((const char*)pInput, *pLength, atoi(decKey.sKey)); break;
        case XC_BASE64: pDecrypted = (uint8_t*)XDecrypt_Base64(pInput, pLength); break;
        case XC_BASE64URL: pDecrypted = (uint8_t*)XDecrypt_Base64Url(pInput, pLength); break;
        case XC_REVERSE: pDecrypted = (uint8_t*)XCrypt_Reverse((const char*)pInput, *pLength); break;
        default: break;
    }

    if (pDecrypted == NULL)
    {
        XCrypt_ErrorCallback(pCtx,
            "Failed to decrypt data with cipher: %s",
                XCrypt_GetCipherStr(eCipher));

        return NULL;
    }

    return pDecrypted;
}

uint8_t* XCrypt_Multy(xcrypt_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength)
{
    uint8_t* (*XCrypt_Func)(xcrypt_ctx_t*, xcrypt_chipher_t, const uint8_t*, size_t*);
    XCrypt_Func = pCtx->bDecrypt ? XDecrypt_Single : XCrypt_Single;

    xarray_t *pCiphersArr = xstrsplit(pCtx->pCiphers, ":");
    if (pCiphersArr == NULL)
    {
        pCiphersArr = XArray_New(XSTDNON, XFALSE);
        if (pCiphersArr == NULL)
        {
            XCrypt_ErrorCallback(pCtx, "Can not allocate memory for cipher array");
            return NULL;
        }

        XArray_AddData(pCiphersArr, pCtx->pCiphers, strlen(pCtx->pCiphers));
        if (!pCiphersArr->nUsed)
        {
            XCrypt_ErrorCallback(pCtx, "Can append cipher to the array");
            XArray_Destroy(pCiphersArr);
            return NULL;
        }
    }

    uint8_t *pData = XByteData_Dup(pInput, *pLength);
    if (pData == NULL)
    {
        XCrypt_ErrorCallback(pCtx, "Can allocate memory for input buffer");
        XArray_Destroy(pCiphersArr);
        return NULL;
    }

    size_t i, nUsed = XArray_Used(pCiphersArr);
    uint8_t *pRet = NULL;

    for (i = 0; i < nUsed && pData != NULL; i++)
    {
        const char *pCipher = XArray_GetData(pCiphersArr, i);
        if (pCipher == NULL) continue;

        xcrypt_chipher_t eCipher = XCrypt_GetCipher(pCipher);
        if (eCipher == XC_INVALID)
        {
            XCrypt_ErrorCallback(pCtx, 
                "Invalid or unsupported cipher: %s", 
                XCrypt_GetCipherStr(eCipher));

            XArray_Destroy(pCiphersArr);
            free(pData);
            return NULL;
        }

        pRet = XCrypt_Func(pCtx, eCipher, pData, pLength);
        free(pData);
        pData = pRet;
    }

    XArray_Destroy(pCiphersArr);
    return pData;
}
