/*!
 *  @file libxutils/src/xtype.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of various 
 * types and converting operations.
 */

#include "xstd.h"
#include "xstr.h"
#include "xtype.h"

uint32_t XFloatToU32(float fValue)
{
    uint16_t nIntegral = (uint16_t)floor(fValue);
    float fBalance = fValue - (float)nIntegral;
    uint16_t nDecimal = (uint16_t)(fBalance * 100);

    uint32_t nRetVal;
    nRetVal = (uint32_t)nIntegral;
    nRetVal <<= 16;
    nRetVal += (uint32_t)nDecimal;
    return nRetVal;
}

float XU32ToFloat(uint32_t nValue)
{
    uint16_t nIntegral = (uint16_t)(nValue >> 16);
    uint16_t nDecimal = (uint16_t)(nValue & 0xFF);
    float fBalance = (float)nDecimal / (float)100;
    return (float)((float)nIntegral + fBalance);
}

int XTypeIsAlphabet(char nChar)
{
    return ((nChar >= 'a' && nChar <= 'z') || 
            (nChar >= 'A' && nChar <= 'Z')) ? 1 : 0;
}

xbool_t XTypeIsPrint(const uint8_t *pData, size_t nSize)
{
    xbool_t bPrintable = XTRUE;
    size_t i;

    for (i = 0; i < nSize; i++)
    {
        if (pData[i] == XSTR_NUL) break;

        if (!isascii(pData[i]) &&
            !isprint(pData[i]))
        {
            bPrintable = XFALSE;
            break;
        }
    }

    return bPrintable;
}

size_t XBytesToUnit(char *pDst, size_t nSize, size_t nBytes, xbool_t bShort)
{
    if (bShort)
    {
        if (nBytes > 1073741824)
            return xstrncpyf(pDst, nSize, "%.1fG", (double)nBytes / (double)1073741824);
        else if (nBytes > 1048576)
            return xstrncpyf(pDst, nSize, "%.1fM", (double)nBytes / (double)1048576);
        else if (nBytes > 1024)
            return xstrncpyf(pDst, nSize, "%.1fK", (double)nBytes / (double)1024);

        return xstrncpyf(pDst, nSize, "%zuB", nBytes);
    }

    if (nBytes > 1073741824)
        return xstrncpyf(pDst, nSize, "%.2f GB", (double)nBytes / (double)1073741824);
    else if (nBytes > 1048576)
        return xstrncpyf(pDst, nSize, "%.2f MB", (double)nBytes / (double)1048576);
    else if (nBytes > 1024)
        return xstrncpyf(pDst, nSize, "%.2f KB", (double)nBytes / (double)1024);

    return xstrncpyf(pDst, nSize, "%zu  B", nBytes);
}

size_t XKBToUnit(char *pDst, size_t nSize, size_t nKB, xbool_t bShort)
{
    if (bShort)
    {
        if (nKB > 1073741824)
            return xstrncpyf(pDst, nSize, "%.1fT", (double)nKB / (double)1073741824);
        else if (nKB > 1048576)
            return xstrncpyf(pDst, nSize, "%.1fG", (double)nKB / (double)1048576);
        else if (nKB > 1024)
            return xstrncpyf(pDst, nSize, "%.1fM", (double)nKB / (double)1024);

        return xstrncpyf(pDst, nSize, "%zuK", nKB);
    }

    if (nKB > 1073741824)
        return xstrncpyf(pDst, nSize, "%.2f TB", (double)nKB / (double)1073741824);
    else if (nKB > 1048576)
        return xstrncpyf(pDst, nSize, "%.2f GB", (double)nKB / (double)1048576);
    else if (nKB > 1024)
        return xstrncpyf(pDst, nSize, "%.2f MB", (double)nKB / (double)1024);

    return xstrncpyf(pDst, nSize, "%zu KB", nKB);
}
