/*!
 *  @file libxutils/src/crypt/md5.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief MD5 algorithm implementation for C/C++
 */

#include "md5.h"
#include "xstr.h"

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

static const uint32_t g_radians[] = 
    {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };

#define XMD5_LEFTROTATE(x, c) (((x) << (c)) | ((x) >> (32 - (c))))

XSTATUS XMD5_Compute(uint8_t *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength)
{
    XASSERT((nSize >= XMD5_DIGEST_SIZE &&
        nLength && pOutput && pInput), XSTDINV);

    uint32_t hash0 = 0x67452301;
    uint32_t hash1 = 0xefcdab89;
    uint32_t hash2 = 0x98badcfe;
    uint32_t hash3 = 0x10325476;
    size_t nNewLen = 0;

    for (nNewLen = (nLength * 8 + 1);
        (nNewLen % 512) != 448; nNewLen++);

    nNewLen /= 8;
    uint8_t *pMessage = (uint8_t*)calloc(nNewLen + 64, 1);
    XASSERT(pMessage, XSTDERR);

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
            b = b + XMD5_LEFTROTATE(nX, g_radians[i]);
            a = temp;
        }

        hash0 += a;
        hash1 += b;
        hash2 += c;
        hash3 += d;
    }

    for (i = 0; i < 4; i ++) pOutput[i] = ((uint8_t *)&hash0)[i];
    for (i = 0; i < 4; i ++) pOutput[i + 4] = ((uint8_t *)&hash1)[i];
    for (i = 0; i < 4; i ++) pOutput[i + 8] = ((uint8_t *)&hash2)[i];
    for (i = 0; i < 4; i ++) pOutput[i + 12] = ((uint8_t *)&hash3)[i];

    free(pMessage);
    return XSTDOK;
}

char* XMD5_Sum(const uint8_t *pInput, size_t nLength)
{
    size_t nHashSize = XMD5_LENGTH + 1;
    char *pOutput = (char*)malloc(nHashSize);
    XASSERT(pOutput, NULL);

    uint8_t digest[XMD5_DIGEST_SIZE];
    XMD5_Compute(digest, sizeof(digest), pInput, nLength);

    xstrncpyf(pOutput, nHashSize,
        "%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x"
        "%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x",
        digest[0], digest[1], digest[2], digest[3],
        digest[4], digest[5], digest[6], digest[7],
        digest[8], digest[9], digest[10], digest[11],
        digest[12], digest[13], digest[14], digest[15]);

    return pOutput;
}

uint8_t* XMD5_Encrypt(const uint8_t *pInput, size_t nLength)
{
    size_t nDigestSize = XMD5_DIGEST_SIZE + 1;
    uint8_t *pOutput = (uint8_t*)malloc(nDigestSize);
    XASSERT(pOutput, NULL);

    XMD5_Compute(pOutput, nDigestSize, pInput, nLength);
    pOutput[XMD5_DIGEST_SIZE] = '\0';

    return pOutput;
}