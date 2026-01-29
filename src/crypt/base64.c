/*!
 *  @file libxutils/src/crypt/base64.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of Base64 encrypt/decrypt functions.
 */

#include "base64.h"

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

char *XBase64_Encrypt(const uint8_t *pInput, size_t *pLength)
{
    XASSERT((pInput && pLength && (*pLength)), NULL);
    size_t nOutLength = ((*pLength + 2) / 3) * 4;
    size_t i, j, nLength = *pLength;

    char *pEncodedData = (char *)calloc(1, nOutLength + 1);
    XASSERT(pEncodedData, NULL);

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
    xbool_t bDec = XFALSE;

    for (i = 0; i < nModTable[nLength % 3]; i++)
        pEncodedData[nOutLength - 1 - i] = '=';

    while (pEncodedData[nOutLength] == '\0')
    {
        bDec = XTRUE;
        nOutLength--;
    }

    *pLength = bDec ? nOutLength + 1 : nOutLength;
    pEncodedData[*pLength] = '\0';

    return pEncodedData;
}

char *XBase64_Decrypt(const uint8_t *pInput, size_t *pLength)
{
    XASSERT((pInput && pLength && (*pLength)), NULL);
    size_t i, j, nLength = *pLength;

    while (nLength % 4 != 0) nLength++;
    size_t nOutLength = nLength / 4 * 3 + 1;
    nLength = *pLength;

    char *pDecodedData = (char *)calloc(1, nOutLength + 1);
    XASSERT(pDecodedData, NULL);

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

    xbool_t bDec = XFALSE;
    while (pDecodedData[nOutLength] == '\0')
    {
        bDec = XTRUE;
        nOutLength--;
    }

    *pLength = bDec ? nOutLength + 1 : nOutLength;
    pDecodedData[*pLength] = '\0';

    return pDecodedData;
}

char *XBase64_UrlDecrypt(const uint8_t *pInput, size_t *pLength)
{
    XASSERT((pInput && pLength && (*pLength)), NULL);
    uint8_t *pUrlDecoded = (uint8_t*)malloc(*pLength + 1);

    XASSERT(pUrlDecoded, NULL);
    size_t i;

    for (i = 0; i < *pLength; i++)
    {
        if (pInput[i] == '-') pUrlDecoded[i] = '+';
        else if (pInput[i] == '_') pUrlDecoded[i] = '/';
        else pUrlDecoded[i] = pInput[i];
    }

    pUrlDecoded[*pLength] = '\0';
    char *pDecoded = XBase64_Decrypt(pUrlDecoded, pLength);

    free(pUrlDecoded);
    return pDecoded;
}

char *XBase64_UrlEncrypt(const uint8_t *pInput, size_t *pLength)
{
    XASSERT((pInput && pLength && (*pLength)), NULL);
    size_t nOutLength = ((*pLength + 2) / 3) * 4;
    size_t i, j, nLength = *pLength;

    char *pEncodedData = (char *)calloc(1, nOutLength + 1);
    XASSERT(pEncodedData, NULL);

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
