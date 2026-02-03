/*!
 *  @file libxutils/src/crypt/aes.c
 *
 *  This source is part of "libxutils" project
 *  2015-2025  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of AES encryption based on tiny-AES-c project,
 * which was released under The Unlicense (public domain dedication).
 *
 * Modified: Refactored code, adjusted API, and added CBC mode with PKCS#7 padding.
 */

#include "aes.h"

typedef uint8_t xaes_state_t[4][4];

static const uint8_t g_sbox[256] = {
  0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
  0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
  0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
  0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
  0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
  0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
  0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
  0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
  0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
  0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
  0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
  0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
  0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
  0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
  0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
  0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16 };

static const uint8_t g_rsbox[256] = {
  0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
  0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
  0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
  0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
  0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
  0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
  0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
  0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
  0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
  0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
  0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
  0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
  0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
  0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
  0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
  0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d };

static const uint8_t g_rcon[11] = {
  0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };

#define XAES_GetSBoxValue(num) (g_sbox[(num)])
#define XAES_GetSBoxInvert(num) (g_rsbox[(num)])
#define XAES_Time(x) (((x) << 1) ^ ((((x) >> 7) & 1) * 0x1b))

/* Multiply as a function reduces size with the Keil ARM compiler */
#if _XAES_MULTIPLY_FUNCTION
static uint8_t XAES_Multiply(uint8_t x, uint8_t y)
{
    return (((y & 1) * x) ^
        ((y >> 1 & 1) * XAES_Time(x)) ^
        ((y >> 2 & 1) * XAES_Time(XAES_Time(x))) ^
        ((y >> 3 & 1) * XAES_Time(XAES_Time(XAES_Time(x)))) ^
        ((y >> 4 & 1) * XAES_Time(XAES_Time(XAES_Time(XAES_Time(x))))));
        // The last call to XAES_Time() can be omitted
}
#else
#define XAES_Multiply(x, y)                                                 \
        (((y & 1) * x) ^                                                    \
        ((y >> 1 & 1) * XAES_Time(x)) ^                                     \
        ((y >> 2 & 1) * XAES_Time(XAES_Time(x))) ^                          \
        ((y >> 3 & 1) * XAES_Time(XAES_Time(XAES_Time(x)))) ^               \
        ((y >> 4 & 1) * XAES_Time(XAES_Time(XAES_Time(XAES_Time(x))))))     \

#endif

static void XAES_KeyExpansion(xaes_ctx_t* pCtx, const uint8_t* pKey)
{
    uint8_t* pRoundKey = pCtx->roundKey;
    unsigned i, j, k;
    uint8_t temp[4];

    // The first round key is the pKey itself
    for (i = 0; i < pCtx->nNK; ++i)
    {
        pRoundKey[(i * 4) + 0] = pKey[(i * 4) + 0];
        pRoundKey[(i * 4) + 1] = pKey[(i * 4) + 1];
        pRoundKey[(i * 4) + 2] = pKey[(i * 4) + 2];
        pRoundKey[(i * 4) + 3] = pKey[(i * 4) + 3];
    }

    // All other round keys are found from the previous round keys
    for (i = pCtx->nNK; i < pCtx->nNB * (pCtx->nNR + 1); ++i)
    {
        {
            k = (i - 1) * 4;
            temp[0] = pRoundKey[k + 0];
            temp[1] = pRoundKey[k + 1];
            temp[2] = pRoundKey[k + 2];
            temp[3] = pRoundKey[k + 3];
        }

        if (i % pCtx->nNK == 0)
        {
            // RotWord()
            {
                const uint8_t u8tmp = temp[0];
                temp[0] = temp[1];
                temp[1] = temp[2];
                temp[2] = temp[3];
                temp[3] = u8tmp;
            }

            // Subword()
            {
                temp[0] = XAES_GetSBoxValue(temp[0]);
                temp[1] = XAES_GetSBoxValue(temp[1]);
                temp[2] = XAES_GetSBoxValue(temp[2]);
                temp[3] = XAES_GetSBoxValue(temp[3]);
            }

            temp[0] = temp[0] ^ g_rcon[i / pCtx->nNK];
        }

        if (pCtx->nKeySize == 256)
        {
            if (i % pCtx->nNK == 4)
            {
                // Subword()
                {
                    temp[0] = XAES_GetSBoxValue(temp[0]);
                    temp[1] = XAES_GetSBoxValue(temp[1]);
                    temp[2] = XAES_GetSBoxValue(temp[2]);
                    temp[3] = XAES_GetSBoxValue(temp[3]);
                }
            }
        }

        j = i * 4; k = (i - pCtx->nNK) * 4;
        pRoundKey[j + 0] = pRoundKey[k + 0] ^ temp[0];
        pRoundKey[j + 1] = pRoundKey[k + 1] ^ temp[1];
        pRoundKey[j + 2] = pRoundKey[k + 2] ^ temp[2];
        pRoundKey[j + 3] = pRoundKey[k + 3] ^ temp[3];
    }
}

int XAES_SetKey(xaes_ctx_t* pCtx, const uint8_t* pKey, size_t nKeySize, const uint8_t* pIV, uint8_t nSelfContainedIV)
{
    if (nKeySize == 128)
    {
        pCtx->nNB = 4;
        pCtx->nNK = 4;
        pCtx->nNR = 10;
    }
    else if (nKeySize == 192)
    {
        pCtx->nNB = 4;
        pCtx->nNK = 6;
        pCtx->nNR = 12;
    }
    else if (nKeySize == 256)
    {
        pCtx->nNB = 4;
        pCtx->nNK = 8;
        pCtx->nNR = 14;
    }
    else
    {
        // Invalid key size
        return -1;
    }

    pCtx->nSelfContainedIV = nSelfContainedIV;
    pCtx->nKeySize = nKeySize;
    XAES_KeyExpansion(pCtx, pKey);

    if (pIV) memcpy(pCtx->IV, pIV, XAES_BLOCK_SIZE);
    else memset(pCtx->IV, 0, XAES_BLOCK_SIZE);
    
    return 1;
}

static void XAES_AddRoundKey(const xaes_ctx_t* pCtx, uint8_t nRound, xaes_state_t* pState, const uint8_t* pRoundKey)
{
    uint8_t i, j;

    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            (*pState)[i][j] ^= pRoundKey[(nRound * pCtx->nNB * 4) + (i * pCtx->nNB) + j];
        }
    }
}

static void XAES_SubBytes(xaes_state_t* pState)
{
    uint8_t i, j;

    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            (*pState)[j][i] = XAES_GetSBoxValue((*pState)[j][i]);
        }
    }
}

static void XAES_ShiftRows(xaes_state_t* pState)
{
    // Rotate first row 1 columns to left  
    uint8_t nTemp = (*pState)[0][1];
    (*pState)[0][1] = (*pState)[1][1];
    (*pState)[1][1] = (*pState)[2][1];
    (*pState)[2][1] = (*pState)[3][1];
    (*pState)[3][1] = nTemp;

    // Rotate second row 2 columns to left  
    nTemp = (*pState)[0][2];
    (*pState)[0][2] = (*pState)[2][2];
    (*pState)[2][2] = nTemp;

    nTemp = (*pState)[1][2];
    (*pState)[1][2] = (*pState)[3][2];
    (*pState)[3][2] = nTemp;

    // Rotate third row 3 columns to left
    nTemp = (*pState)[0][3];
    (*pState)[0][3] = (*pState)[3][3];
    (*pState)[3][3] = (*pState)[2][3];
    (*pState)[2][3] = (*pState)[1][3];
    (*pState)[1][3] = nTemp;
}

static void XAES_MixColumns(xaes_state_t* pState)
{
    uint8_t i;
    uint8_t nTmp, nTM, nT;

    for (i = 0; i < 4; ++i)
    {  
        nT = (*pState)[i][0];
        nTmp = (*pState)[i][0] ^ (*pState)[i][1] ^ (*pState)[i][2] ^ (*pState)[i][3] ;
        nTM = (*pState)[i][0] ^ (*pState)[i][1] ; nTM = XAES_Time(nTM); (*pState)[i][0] ^= nTM ^ nTmp ;
        nTM = (*pState)[i][1] ^ (*pState)[i][2] ; nTM = XAES_Time(nTM); (*pState)[i][1] ^= nTM ^ nTmp ;
        nTM = (*pState)[i][2] ^ (*pState)[i][3] ; nTM = XAES_Time(nTM); (*pState)[i][2] ^= nTM ^ nTmp ;
        nTM = (*pState)[i][3] ^ nT ; nTM = XAES_Time(nTM);  (*pState)[i][3] ^= nTM ^ nTmp ;
    }
}

static void XAES_InvMixColumns(xaes_state_t* pState)
{
    int i;
    uint8_t a, b, c, d;

    for (i = 0; i < 4; ++i)
    { 
        a = (*pState)[i][0];
        b = (*pState)[i][1];
        c = (*pState)[i][2];
        d = (*pState)[i][3];

        (*pState)[i][0] = XAES_Multiply(a, 0x0e) ^ XAES_Multiply(b, 0x0b) ^ XAES_Multiply(c, 0x0d) ^ XAES_Multiply(d, 0x09);
        (*pState)[i][1] = XAES_Multiply(a, 0x09) ^ XAES_Multiply(b, 0x0e) ^ XAES_Multiply(c, 0x0b) ^ XAES_Multiply(d, 0x0d);
        (*pState)[i][2] = XAES_Multiply(a, 0x0d) ^ XAES_Multiply(b, 0x09) ^ XAES_Multiply(c, 0x0e) ^ XAES_Multiply(d, 0x0b);
        (*pState)[i][3] = XAES_Multiply(a, 0x0b) ^ XAES_Multiply(b, 0x0d) ^ XAES_Multiply(c, 0x09) ^ XAES_Multiply(d, 0x0e);
    }
}

static void XAES_InvSubBytes(xaes_state_t* pState)
{
    uint8_t i, j;

    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            (*pState)[j][i] = XAES_GetSBoxInvert((*pState)[j][i]);
        }
    }
}

static void XAES_InvShiftRows(xaes_state_t* pState)
{
    // Rotate first row 1 columns to right  
    uint8_t nTemp = (*pState)[3][1];
    (*pState)[3][1] = (*pState)[2][1];
    (*pState)[2][1] = (*pState)[1][1];
    (*pState)[1][1] = (*pState)[0][1];
    (*pState)[0][1] = nTemp;

    // Rotate second row 2 columns to right 
    nTemp = (*pState)[0][2];
    (*pState)[0][2] = (*pState)[2][2];
    (*pState)[2][2] = nTemp;

    nTemp = (*pState)[1][2];
    (*pState)[1][2] = (*pState)[3][2];
    (*pState)[3][2] = nTemp;

    // Rotate third row 3 columns to right
    nTemp = (*pState)[0][3];
    (*pState)[0][3] = (*pState)[1][3];
    (*pState)[1][3] = (*pState)[2][3];
    (*pState)[2][3] = (*pState)[3][3];
    (*pState)[3][3] = nTemp;
}

void XAES_EncryptBlock(const xaes_ctx_t* pCtx, uint8_t* pBuffer)
{
    const uint8_t* pRoundKey = pCtx->roundKey;
    xaes_state_t* pState = (xaes_state_t*)pBuffer;
    uint8_t nRound = 0;

    XAES_AddRoundKey(pCtx, 0, pState, pRoundKey);

    for (nRound = 1; ; ++nRound)
    {
        XAES_SubBytes(pState);
        XAES_ShiftRows(pState);
        if (nRound == pCtx->nNR) break;
        XAES_MixColumns(pState);
        XAES_AddRoundKey(pCtx, nRound, pState, pRoundKey);
    }

    // Add round key to last round
    XAES_AddRoundKey(pCtx, pCtx->nNR, pState, pRoundKey);
}

void XAES_DecryptBlock(const xaes_ctx_t* pCtx, uint8_t* pBuffer)
{
    const uint8_t* pRoundKey = pCtx->roundKey;
    xaes_state_t* pState = (xaes_state_t*)pBuffer;
    uint8_t nRound = 0;

    XAES_AddRoundKey(pCtx, pCtx->nNR, pState, pRoundKey);

    for (nRound = (pCtx->nNR - 1); ; --nRound)
    {
        XAES_InvShiftRows(pState);
        XAES_InvSubBytes(pState);
        XAES_AddRoundKey(pCtx, nRound, pState, pRoundKey);
        if (nRound == 0) break;
        XAES_InvMixColumns(pState);
    }
}

static void XAES_ApplyXOR(uint8_t* pBuffer, const uint8_t* pIV)
{
    uint8_t i;

    for (i = 0; i < XAES_BLOCK_SIZE; ++i)
    {
        pBuffer[i] ^= pIV[i];
    }
}

uint8_t* XAES_Encrypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength)
{
    if (pCtx == NULL || pInput == NULL || pLength == NULL || *pLength == 0) return NULL;

    size_t nOriginalLen = *pLength;
    size_t nNewLength = ((nOriginalLen / XAES_BLOCK_SIZE) + 1) * XAES_BLOCK_SIZE;
    size_t nPrefix = pCtx->nSelfContainedIV ? XAES_BLOCK_SIZE : 0;
    size_t nOutLen = nPrefix + nNewLength;

    uint8_t *pOutput = (uint8_t*)malloc(nOutLen + 1);
    if (pOutput == NULL) return NULL;

    uint8_t iv[XAES_BLOCK_SIZE];
    memcpy(iv, pCtx->IV, sizeof(iv));

    if (pCtx->nSelfContainedIV)
        memcpy(pOutput, iv, XAES_BLOCK_SIZE);

    /* Copy input and add PKCS#7 padding */
    uint8_t *pOffset = pOutput + nPrefix;
    memcpy(pOffset, pInput, nOriginalLen);

    uint8_t nPadding = (uint8_t)(nNewLength - nOriginalLen);
    memset(pOffset + nOriginalLen, nPadding, nPadding);

    while (nNewLength > 0)
    {
        XAES_ApplyXOR(pOffset, iv);
        XAES_EncryptBlock(pCtx, pOffset);
        memcpy(iv, pOffset, XAES_BLOCK_SIZE);

        nNewLength -= XAES_BLOCK_SIZE;
        pOffset += XAES_BLOCK_SIZE;
    }

    if (!pCtx->nSelfContainedIV)
        memcpy(pCtx->IV, iv, XAES_BLOCK_SIZE);

    *pLength = nOutLen;
    pOutput[nOutLen] = '\0';

    return pOutput;
}


uint8_t* XAES_Decrypt(xaes_ctx_t *pCtx, const uint8_t *pInput, size_t *pLength)
{
    if (pCtx == NULL || pInput == NULL || pLength == NULL || !(*pLength)) return NULL;

    size_t nInputLength = *pLength;
    uint8_t iv[XAES_BLOCK_SIZE] = { 0 };

    if (pCtx->nSelfContainedIV)
    {
        if (nInputLength <= XAES_BLOCK_SIZE) return NULL;
        size_t nCipherLen = nInputLength - XAES_BLOCK_SIZE;
        if ((nCipherLen % XAES_BLOCK_SIZE) != 0) return NULL;

        memcpy(iv, pInput, XAES_BLOCK_SIZE);
        pInput += XAES_BLOCK_SIZE;
        nInputLength = nCipherLen;
    }
    else
    {
        if (nInputLength < XAES_BLOCK_SIZE) return NULL;
        if (nInputLength % XAES_BLOCK_SIZE != 0) return NULL;
        memcpy(iv, pCtx->IV, sizeof(iv));
    }

    uint8_t *pOutput = (uint8_t*)malloc(nInputLength + 1);
    if (pOutput == NULL) return NULL;

    memcpy(pOutput, pInput, nInputLength);
    uint8_t *pOffset = pOutput;
    size_t nDataLeft = nInputLength;

    while (nDataLeft > 0)
    {
        uint8_t ivTmp[XAES_BLOCK_SIZE];
        memcpy(ivTmp, pOffset, XAES_BLOCK_SIZE);
        XAES_DecryptBlock(pCtx, pOffset);

        XAES_ApplyXOR(pOffset, iv);
        memcpy(iv, ivTmp, XAES_BLOCK_SIZE);

        nDataLeft -= XAES_BLOCK_SIZE;
        pOffset += XAES_BLOCK_SIZE;
    }

    // Remove PKCS#7 padding
    uint8_t nPadding = pOutput[nInputLength - 1];
    if (!nPadding || nPadding > XAES_BLOCK_SIZE)
    {
        free(pOutput);
        return NULL;
    }

    size_t i;
    for (i = 0; i < nPadding; i++)
    {
        if (pOutput[nInputLength - 1 - i] != nPadding)
        {
            free(pOutput);
            return NULL;
        }
    }

    if (!pCtx->nSelfContainedIV)
        memcpy(pCtx->IV, iv, XAES_BLOCK_SIZE);

    *pLength = nInputLength - nPadding;
    pOutput[*pLength] = '\0';

    return pOutput;
}
