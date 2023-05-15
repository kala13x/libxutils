/*!
 *  @file libxutils/src/crypt/sha1.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief SHA-1 algorithm implementation based on the pseudocode 
 * and description provided in the SHA-1 standard (FIPS PUB 180-1)
 */

#include "sha1.h"
#include "xstr.h"

#define XSHA1_ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

#define XSHA1_BLK0(i) (pBlock->wBytes[i] = \
    (XSHA1_ROL(pBlock->wBytes[i], 24) & 0xFF00FF00) | \
    (XSHA1_ROL(pBlock->wBytes[i], 8) & 0x00FF00FF))

#define XSHA1_BLK(i) (pBlock->wBytes[i & 15] = \
    XSHA1_ROL(pBlock->wBytes[i & 15] ^ \
    pBlock->wBytes[(i- 14) & 15] ^ \
    pBlock->wBytes[(i - 8) & 15] ^ \
    pBlock->wBytes[(i - 3) & 15], 1))

#define XSHA_R01(w,x,y) ((w&(x^y))^y)
#define XSHA_R24(w,x,y) (w^x^y)
#define XSHA_R3I(w,x,y) (((w|x)&y)|(w&x))

#define XSHA1_R0(v,w,x,y,z,i) z += XSHA_R01(w,x,y) + XSHA1_BLK0(i) + 0x5A827999 + XSHA1_ROL(v,5); w = XSHA1_ROL(w,30);
#define XSHA1_R1(v,w,x,y,z,i) z += XSHA_R01(w,x,y) + XSHA1_BLK(i) + 0x5A827999 + XSHA1_ROL(v,5); w = XSHA1_ROL(w,30);
#define XSHA1_R2(v,w,x,y,z,i) z += XSHA_R24(w,x,y) + XSHA1_BLK(i) + 0x6ED9EBA1 + XSHA1_ROL(v,5); w = XSHA1_ROL(w,30);
#define XSHA1_R3(v,w,x,y,z,i) z += XSHA_R3I(w,x,y) + XSHA1_BLK(i) + 0x8F1BBCDC + XSHA1_ROL(v,5); w = XSHA1_ROL(w,30);
#define XSHA1_R4(v,w,x,y,z,i) z += XSHA_R24(w,x,y) + XSHA1_BLK(i) + 0xCA62C1D6 + XSHA1_ROL(v,5); w = XSHA1_ROL(w,30);

void XSHA1_Init(xsha1_ctx_t *pCtx)
{
    pCtx->state[0] = 0x67452301;
    pCtx->state[1] = 0xEFCDAB89;
    pCtx->state[2] = 0x98BADCFE;
    pCtx->state[3] = 0x10325476;
    pCtx->state[4] = 0xC3D2E1F0;
    pCtx->count[0] = pCtx->count[1] = 0;
}

void XSHA1_Update(xsha1_ctx_t *pCtx, const uint8_t* pData, uint32_t nLength)
{
    XASSERT_VOID((pCtx && pData && nLength));
    uint32_t i, j = pCtx->count[0];

    if ((pCtx->count[0] += nLength << 3) < j)
        pCtx->count[1]++;

    pCtx->count[1] += (nLength>>29);
    j = (j >> 3) & 63;

    if ((j + nLength) > 63)
    {
        memcpy(&pCtx->buffer[j], pData, (i = 64-j));
        XSHA1_Transform(pCtx->state, pCtx->buffer);

        for (; i + 63 < nLength; i += 64)
            XSHA1_Transform(pCtx->state, &pData[i]);

        j = 0;
    }
    else i = 0;

    memcpy(&pCtx->buffer[j], &pData[i], nLength - i);
}

void XSHA1_Final(xsha1_ctx_t *pCtx, uint8_t uDigest[XSHA1_DIGEST_SIZE])
{
    uint8_t finalCount[8];
    uint32_t i;

    for (i = 0; i < 8; i++)
    {
        /* Endian independent */
        uint32_t nIndex = (i >= 4 ? 0 : 1);
        uint32_t nShift = ((3 - (i & 3)) * 8);
        finalCount[i] = (uint8_t)((pCtx->count[nIndex] >> nShift) & 255);
    }

    XSHA1_Update(pCtx, (uint8_t *)"\200", 1);

    while ((pCtx->count[0] & 504) != 448)
        XSHA1_Update(pCtx, (uint8_t *)"\0", 1);

    XSHA1_Update(pCtx, finalCount, 8);

    for (i = 0; i < XSHA1_DIGEST_SIZE; i++)
    {
        uint32_t nShift = ((3 - (i & 3)) * 8);
        uDigest[i] = (uint8_t)((pCtx->state[i >> 2] >> nShift) & 255);
    }
}

/* Hash a single 512-bit pBlock. This is the core of the algorithm. */
void XSHA1_Transform(uint32_t uState[5], const uint8_t uBuffer[64])
{
    uint32_t uBlock[16];

    xsha1_block_t* pBlock = (xsha1_block_t*)uBlock;
    memcpy(pBlock, uBuffer, 64);

    /* Copy pCtx->state[] to working vars */
    uint32_t a = uState[0];
    uint32_t b = uState[1];
    uint32_t c = uState[2];
    uint32_t d = uState[3];
    uint32_t e = uState[4];

    /* 4 rounds of 20 operations each */
    XSHA1_R0(a,b,c,d,e, 0); XSHA1_R0(e,a,b,c,d, 1); XSHA1_R0(d,e,a,b,c, 2); XSHA1_R0(c,d,e,a,b, 3);
    XSHA1_R0(b,c,d,e,a, 4); XSHA1_R0(a,b,c,d,e, 5); XSHA1_R0(e,a,b,c,d, 6); XSHA1_R0(d,e,a,b,c, 7);
    XSHA1_R0(c,d,e,a,b, 8); XSHA1_R0(b,c,d,e,a, 9); XSHA1_R0(a,b,c,d,e,10); XSHA1_R0(e,a,b,c,d,11);
    XSHA1_R0(d,e,a,b,c,12); XSHA1_R0(c,d,e,a,b,13); XSHA1_R0(b,c,d,e,a,14); XSHA1_R0(a,b,c,d,e,15);
    XSHA1_R1(e,a,b,c,d,16); XSHA1_R1(d,e,a,b,c,17); XSHA1_R1(c,d,e,a,b,18); XSHA1_R1(b,c,d,e,a,19);
    XSHA1_R2(a,b,c,d,e,20); XSHA1_R2(e,a,b,c,d,21); XSHA1_R2(d,e,a,b,c,22); XSHA1_R2(c,d,e,a,b,23);
    XSHA1_R2(b,c,d,e,a,24); XSHA1_R2(a,b,c,d,e,25); XSHA1_R2(e,a,b,c,d,26); XSHA1_R2(d,e,a,b,c,27);
    XSHA1_R2(c,d,e,a,b,28); XSHA1_R2(b,c,d,e,a,29); XSHA1_R2(a,b,c,d,e,30); XSHA1_R2(e,a,b,c,d,31);
    XSHA1_R2(d,e,a,b,c,32); XSHA1_R2(c,d,e,a,b,33); XSHA1_R2(b,c,d,e,a,34); XSHA1_R2(a,b,c,d,e,35);
    XSHA1_R2(e,a,b,c,d,36); XSHA1_R2(d,e,a,b,c,37); XSHA1_R2(c,d,e,a,b,38); XSHA1_R2(b,c,d,e,a,39);
    XSHA1_R3(a,b,c,d,e,40); XSHA1_R3(e,a,b,c,d,41); XSHA1_R3(d,e,a,b,c,42); XSHA1_R3(c,d,e,a,b,43);
    XSHA1_R3(b,c,d,e,a,44); XSHA1_R3(a,b,c,d,e,45); XSHA1_R3(e,a,b,c,d,46); XSHA1_R3(d,e,a,b,c,47);
    XSHA1_R3(c,d,e,a,b,48); XSHA1_R3(b,c,d,e,a,49); XSHA1_R3(a,b,c,d,e,50); XSHA1_R3(e,a,b,c,d,51);
    XSHA1_R3(d,e,a,b,c,52); XSHA1_R3(c,d,e,a,b,53); XSHA1_R3(b,c,d,e,a,54); XSHA1_R3(a,b,c,d,e,55);
    XSHA1_R3(e,a,b,c,d,56); XSHA1_R3(d,e,a,b,c,57); XSHA1_R3(c,d,e,a,b,58); XSHA1_R3(b,c,d,e,a,59);
    XSHA1_R4(a,b,c,d,e,60); XSHA1_R4(e,a,b,c,d,61); XSHA1_R4(d,e,a,b,c,62); XSHA1_R4(c,d,e,a,b,63);
    XSHA1_R4(b,c,d,e,a,64); XSHA1_R4(a,b,c,d,e,65); XSHA1_R4(e,a,b,c,d,66); XSHA1_R4(d,e,a,b,c,67);
    XSHA1_R4(c,d,e,a,b,68); XSHA1_R4(b,c,d,e,a,69); XSHA1_R4(a,b,c,d,e,70); XSHA1_R4(e,a,b,c,d,71);
    XSHA1_R4(d,e,a,b,c,72); XSHA1_R4(c,d,e,a,b,73); XSHA1_R4(b,c,d,e,a,74); XSHA1_R4(a,b,c,d,e,75);
    XSHA1_R4(e,a,b,c,d,76); XSHA1_R4(d,e,a,b,c,77); XSHA1_R4(c,d,e,a,b,78); XSHA1_R4(b,c,d,e,a,79);

    /* Add the working vars back into context.state[] */
    uState[0] += a;
    uState[1] += b;
    uState[2] += c;
    uState[3] += d;
    uState[4] += e;
}

XSTATUS XSHA1_Compute(uint8_t *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength)
{
    XASSERT((nSize >= XSHA1_DIGEST_SIZE &&
            pOutput != NULL), XSTDINV);

    xsha1_ctx_t xsha;
    XSHA1_Init(&xsha);
    XSHA1_Update(&xsha, pInput, nLength);
    XSHA1_Final(&xsha, pOutput);

    return XSTDOK;
}

XSTATUS XSHA1_ComputeSum(char *pOutput, size_t nSize, const uint8_t *pInput, size_t nLength)
{
    XASSERT((nSize >= XSHA1_LENGTH + 1 &&
            pOutput != NULL), XSTDINV);

    uint8_t i, nDigest[XSHA1_DIGEST_SIZE];
    XSHA1_Compute(nDigest, nSize, pInput, nLength);

    for (i = 0; i < XSHA1_DIGEST_SIZE; i++)
        xstrncpyf(pOutput + i * 2, 3, "%02x", (unsigned int)nDigest[i]);

    pOutput[XSHA1_LENGTH] = '\0';
    return XSTDOK;
}

char* XSHA1_Sum(const uint8_t *pInput, size_t nLength)
{
    size_t nSize = XSHA1_LENGTH + 1;
    char *pHash = (char*)malloc(nSize);
    if (pHash == NULL) return NULL;

    XSHA1_ComputeSum(pHash, nSize, pInput, nLength);
    return pHash;
}

uint8_t* XSHA1_Encrypt(const uint8_t *pInput, size_t nLength)
{
    size_t nSize = XSHA1_DIGEST_SIZE + 1;
    uint8_t *pHash = (uint8_t*)malloc(nSize);
    if (pHash == NULL) return NULL;

    XSHA1_Compute(pHash, nSize, pInput, nLength);
    pHash[XSHA1_DIGEST_SIZE] = '\0';

    return pHash;
}