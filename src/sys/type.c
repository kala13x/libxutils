/*!
 *  @file libxutils/src/sys/type.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of various 
 * types and converting operations.
 */

#include "type.h"
#include "str.h"

union {
    float fValue;
    uint32_t u32;
} XTypeConvert;

uint32_t XFloatToU32(float fValue)
{
    XTypeConvert.fValue = fValue;
    return XTypeConvert.u32;
}

float XU32ToFloat(uint32_t nValue)
{
    XTypeConvert.u32 = nValue;
    return XTypeConvert.fValue;
}

xbool_t XTypeIsPrint(const uint8_t *pData, size_t nSize)
{
    xbool_t bPrintable = XTRUE;
    size_t i;

    for (i = 0; i < nSize; i++)
    {
        if (pData[i] == XSTR_NUL) break;

        if (!isprint(pData[i]))
        {
            bPrintable = XFALSE;
            break;
        }
    }

    return bPrintable;
}

size_t XBytesToUnit(char *pDst, size_t nSize, size_t nBytes, xbool_t bShort)
{
    const char *pUnit;
    double fVal = 0.;
    xbool_t bIsGB = XFALSE;

    if (nBytes > 1073741824)
    {
        fVal = (double)nBytes / (double)1073741824;
        pUnit = bShort ? "G" : "GB";
        bIsGB = XTRUE;

    }
    else if (nBytes > 1048576)
    {
        fVal = (double)nBytes / (double)1048576;
        pUnit = bShort ? "M" : "MB";
    }
    else if (nBytes > 1024)
    {
        fVal = (double)nBytes / (double)1024;
        pUnit = bShort ? "K" : "KB";
    }
    else
    {
        fVal = (double)nBytes;
        pUnit = bShort ? "B" : " B";
    }

    if (bIsGB)
    {
        return bShort ?
            xstrncpyf(pDst, nSize, "%.0f%s", fVal, pUnit) :
            xstrncpyf(pDst, nSize, "%.0f %s", fVal, pUnit);
    }

    return bShort ?
        xstrncpyf(pDst, nSize, "%.1f%s", fVal, pUnit) :
        xstrncpyf(pDst, nSize, "%.2f %s", fVal, pUnit);
}


size_t XKBToUnit(char *pDst, size_t nSize, size_t nKB, xbool_t bShort)
{
    const char *pUnit;
    double fVal = 0.;

    if (nKB > 1073741824)
    {
        fVal = (double)nKB / (double)1073741824;
        pUnit = bShort ? "T" : " TB";
    }
    else if (nKB > 1048576)
    {
        fVal = (double)nKB / (double)1048576;
        pUnit = bShort ? "G" : " GB";
    }
    else if (nKB > 1024)
    {
        fVal = (double)nKB / (double)1024;
        pUnit = bShort ? "M" : " MB";
    }
    else
    {
        fVal = (double)nKB;
        pUnit = bShort ? "K" : " KB";
    }

    return bShort ?
        xstrncpyf(pDst, nSize, "%.1f%s", fVal, pUnit) :
        xstrncpyf(pDst, nSize, "%.2f %s", fVal, pUnit);
}
