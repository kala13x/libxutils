/*!
 *  @file libxutils/src/crypt/sha256.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief SHA-256 computing implementation for C/C++ based
 * on pseudocode for the SHA-256 algorithm from Wikipedia.
 */

#include "sha256.h"
#include "str.h"

#ifdef _WIN32
#if defined(_MSC_VER)
#pragma warning(disable : 4146)
#endif
#define htobe32(x) _byteswap_ulong(x)
#define be32toh(x) _byteswap_ulong(x)
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htobe32(x) OSSwapHostToBigInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#endif

#define XSHA_UPPER(x) w[(x) & 0x0F]
#define XSHA_LOAD32(p) (((uint32_t)(p)[0] << 24) | ((uint32_t)(p)[1] << 16) | ((uint32_t)(p)[2] << 8) | (uint32_t)(p)[3])
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

/* The schedule for round i, from the previous 16 words kept in a ring of 16 */
#define XSHA_SCHEDULE(i) \
    (XSHA_UPPER(i) += XSHA_SIGMA4(XSHA_UPPER((i) + 14)) + XSHA_UPPER((i) + 9) + XSHA_SIGMA3(XSHA_UPPER((i) + 1)))

/* One round. The working variables are renamed from round to round instead of
   being moved: a round only writes the new e (into d) and the new a (into h). */
#define XSHA_ROUND(a, b, c, d, e, f, g, h, i)                                                   \
    do {                                                                                        \
        uint32_t nT1 = h + XSHA_SIGMA2(e) + XSHA_CH(e, f, g) + XSHA256K[i] + XSHA_UPPER(i);     \
        d += nT1;                                                                               \
        h = nT1 + XSHA_SIGMA1(a) + XSHA_MAJ(a, b, c);                                           \
    } while (0)

#define XSHA_ROUNDS8(i)                                     \
    do {                                                    \
        XSHA_ROUND(a, b, c, d, e, f, g, h, (i) + 0);        \
        XSHA_ROUND(h, a, b, c, d, e, f, g, (i) + 1);        \
        XSHA_ROUND(g, h, a, b, c, d, e, f, (i) + 2);        \
        XSHA_ROUND(f, g, h, a, b, c, d, e, (i) + 3);        \
        XSHA_ROUND(e, f, g, h, a, b, c, d, (i) + 4);        \
        XSHA_ROUND(d, e, f, g, h, a, b, c, (i) + 5);        \
        XSHA_ROUND(c, d, e, f, g, h, a, b, (i) + 6);        \
        XSHA_ROUND(b, c, d, e, f, g, h, a, (i) + 7);        \
    } while (0)

/* Compresses one 64 byte block into the state. The block is read as big endian
   bytes, so it can come straight from the caller's data at any alignment. */
static void XSHA256_Compress(uint32_t *pState, const uint8_t *pBlock)
{
    uint32_t a = pState[0], b = pState[1], c = pState[2], d = pState[3];
    uint32_t e = pState[4], f = pState[5], g = pState[6], h = pState[7];
    uint32_t w[16];
    size_t i;

    for (i = 0; i < 16; i++) w[i] = XSHA_LOAD32(pBlock + i * 4);

    XSHA_ROUNDS8(0);
    XSHA_ROUNDS8(8);

    for (i = 16; i < 64; i += 8)
    {
        XSHA_SCHEDULE(i + 0); XSHA_SCHEDULE(i + 1);
        XSHA_SCHEDULE(i + 2); XSHA_SCHEDULE(i + 3);
        XSHA_SCHEDULE(i + 4); XSHA_SCHEDULE(i + 5);
        XSHA_SCHEDULE(i + 6); XSHA_SCHEDULE(i + 7);
        XSHA_ROUNDS8(i);
    }

    pState[0] += a; pState[1] += b; pState[2] += c; pState[3] += d;
    pState[4] += e; pState[5] += f; pState[6] += g; pState[7] += h;
}

void XSHA256_Update(xsha256_t *pSha, const uint8_t *pData, size_t nLength)
{
    if (!nLength) return;
    pSha->nTotalSize += nLength;

    if (pSha->nSize > 0)
    {
        size_t nPart = XSTD_MIN(nLength, XSHA256_BLOCK_SIZE - pSha->nSize);
        memcpy(pSha->uBlock.block + pSha->nSize, pData, nPart);

        pSha->nSize += nPart;
        pData += nPart;
        nLength -= nPart;

        if (pSha->nSize < XSHA256_BLOCK_SIZE) return;
        XSHA256_Compress(pSha->uDigest.hBytes, pSha->uBlock.block);
        pSha->nSize = 0;
    }

    /* Whole blocks are compressed where they are, without a copy */
    while (nLength >= XSHA256_BLOCK_SIZE)
    {
        XSHA256_Compress(pSha->uDigest.hBytes, pData);
        pData += XSHA256_BLOCK_SIZE;
        nLength -= XSHA256_BLOCK_SIZE;
    }

    if (nLength > 0)
    {
        memcpy(pSha->uBlock.block, pData, nLength);
        pSha->nSize = nLength;
    }
}

void XSHA256_Final(xsha256_t *pSha, uint8_t *pDigest)
{
    size_t i, nPaddingSize = (pSha->nSize < 56) ? (56 - pSha->nSize) : (120 - pSha->nSize);
    uint64_t nTotalBits = (uint64_t)pSha->nTotalSize * 8;

    XSHA256_Update(pSha, XSHA256P, nPaddingSize);
    pSha->uBlock.wBytes[14] = htobe32((uint32_t) (nTotalBits >> 32));
    pSha->uBlock.wBytes[15] = htobe32((uint32_t) nTotalBits);
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
    XSHA256_Compress(pSha->uDigest.hBytes, pSha->uBlock.block);
}

XSTATUS XSHA256_Compute(uint8_t *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength)
{
    if (nSize < XSHA256_DIGEST_SIZE ||
        pOutput == NULL) return XSTDERR;

    xsha256_t xsha;
    XSHA256_Init(&xsha);
    XSHA256_Update(&xsha, pInput, nLength);
    XSHA256_Final(&xsha, pOutput);

    return XSTDOK;
}

XSTATUS XSHA256_ComputeSum(char *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength)
{
    if (nSize < XSHA256_LENGTH + 1 ||
        pOutput == NULL) return XSTDERR;

    uint8_t i, nDigest[XSHA256_DIGEST_SIZE];
    XSHA256_Compute(nDigest, nSize, pInput, nLength);

    for (i = 0; i < XSHA256_DIGEST_SIZE; i++)
        xstrncpyf(pOutput + i * 2, 3, "%02x", (unsigned int)nDigest[i]);

    pOutput[XSHA256_LENGTH] = '\0';
    return XSTDOK;
}

char* XSHA256_Sum(const uint8_t *pInput, size_t nLength)
{
    size_t nSize = XSHA256_LENGTH + 1;
    char *pHash = (char*)malloc(nSize);
    if (pHash == NULL) return NULL;

    XSHA256_ComputeSum(pHash, nSize, pInput, nLength);
    return pHash;
}

uint8_t* XSHA256_Encrypt(const uint8_t *pInput, size_t nLength)
{
    size_t nSize = XSHA256_DIGEST_SIZE + 1;
    uint8_t *pHash = (uint8_t*)malloc(nSize);
    if (pHash == NULL) return NULL;

    XSHA256_Compute(pHash, nSize, pInput, nLength);
    pHash[XSHA256_DIGEST_SIZE] = '\0';

    return pHash;
}
