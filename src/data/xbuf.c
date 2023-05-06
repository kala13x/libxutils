/*!
 *  @file libxutils/src/data/buf.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Dynamically allocated byte and data buffers 
 */

#include "xstd.h"
#include "xbuf.h"

uint8_t *XByteData_Dup(const uint8_t *pBuff, size_t nLength)
{
    if (pBuff == NULL || !nLength) return NULL;

    uint8_t *pData = (uint8_t*)malloc(nLength + 1);
    if (pData == NULL) return NULL;

    memcpy(pData, pBuff, nLength);
    pData[nLength] = XSTR_NUL;

    return pData;
}

int XByteBuffer_Resize(xbyte_buffer_t *pBuffer, size_t nSize)
{
    if (!nSize)
    {
        XByteBuffer_Clear(pBuffer);
        return XSTDNON;
    }

    if (pBuffer->pData == NULL)
    {
        pBuffer->pData = (uint8_t*)malloc(nSize);
        if (pBuffer->pData == NULL)
        {
            pBuffer->nStatus = XSTDERR;
            return pBuffer->nStatus;
        }

        pBuffer->nSize = nSize;
        pBuffer->nUsed = 0;
        return (int)nSize;
    }

    if (!pBuffer->nSize)
    {
        if (pBuffer->nUsed >= nSize)
            return (int)pBuffer->nUsed;

        pBuffer->nStatus = XSTDERR;
        return pBuffer->nStatus;
    }

    uint8_t* pOldData = pBuffer->pData;
    pBuffer->pData = (uint8_t*)realloc(pBuffer->pData, nSize);

    if (pBuffer->pData == NULL)
    {
        pBuffer->pData = pOldData;
        pBuffer->nStatus = XSTDERR;
        return pBuffer->nStatus;
    }

    pBuffer->nUsed = (pBuffer->nUsed >= nSize) ? (nSize - 1) : pBuffer->nUsed;
    if (pBuffer->nUsed < nSize) pBuffer->pData[pBuffer->nUsed] = XSTR_NUL;

    pBuffer->nSize = nSize;
    return (int)pBuffer->nSize;
}

int XByteBuffer_Terminate(xbyte_buffer_t *pBuffer, size_t nPosit)
{
    if (pBuffer->pData == NULL || !pBuffer->nUsed) return XSTDERR;
    size_t nTerminatePosit = XSTD_MIN(pBuffer->nUsed, nPosit);
    pBuffer->pData[nTerminatePosit] = XSTR_NUL;
    pBuffer->nUsed = nTerminatePosit;
    return XSTDOK;
}

int XByteBuffer_Reserve(xbyte_buffer_t *pBuffer, size_t nSize)
{
    if (pBuffer->nStatus < 0) return pBuffer->nStatus;
    size_t nNewSize = pBuffer->nUsed + nSize;
    if (nNewSize <= pBuffer->nSize) return (int)pBuffer->nSize;
    else if (pBuffer->nFast) nNewSize *= 2;
    return XByteBuffer_Resize(pBuffer, nNewSize);
}

xbyte_buffer_t* XByteBuffer_New(size_t nSize, int nFastAlloc)
{
    xbyte_buffer_t *pBuffer = (xbyte_buffer_t*)malloc(sizeof(xbyte_buffer_t));
    if (pBuffer == NULL) return NULL;

    if (XByteBuffer_Init(pBuffer, nSize, nFastAlloc) < 0)
    {
        free(pBuffer);
        return NULL;
    }

    pBuffer->nAlloc = 1;
    return pBuffer;
}

int XByteBuffer_Init(xbyte_buffer_t *pBuffer, size_t nSize, int nFastAlloc)
{
    pBuffer->nStatus = XSTDOK;
    pBuffer->nAlloc = XSTDNON;
    pBuffer->nFast = nFastAlloc;
    pBuffer->nSize = 0;
    pBuffer->nUsed = 0;
    pBuffer->pData = NULL;
    return XByteBuffer_Reserve(pBuffer, nSize);
}

void XByteBuffer_Clear(xbyte_buffer_t *pBuffer)
{
    XASSERT_VOID(pBuffer);

    if (pBuffer->pData != NULL &&
        pBuffer->nSize > 0)
        free(pBuffer->pData);

    pBuffer->nStatus = 0;
    pBuffer->nSize = 0;
    pBuffer->nUsed = 0;
    pBuffer->pData = NULL;
}

void XByteBuffer_Free(xbyte_buffer_t *pBuffer)
{
    XASSERT_VOID(pBuffer);
    XByteBuffer_Clear(pBuffer);
    if (pBuffer->nAlloc) free(pBuffer);
}

void XByteBuffer_Reset(xbyte_buffer_t *pBuffer)
{
    XASSERT_VOID(pBuffer);
    if (pBuffer->pData == NULL)
        pBuffer->nSize = 0;

    pBuffer->nStatus = 0;
    pBuffer->nUsed = 0;
}

int XByteBuffer_Set(xbyte_buffer_t *pBuffer, uint8_t *pData, size_t nSize)
{
    XASSERT(pBuffer, XSTDINV);
    pBuffer->nStatus = 1;
    pBuffer->nAlloc = 0;
    pBuffer->nFast = 0;
    pBuffer->nSize = 0;
    pBuffer->nUsed = nSize;
    pBuffer->pData = pData;
    return (int)pBuffer->nUsed;
}

int XByteBuffer_Own(xbyte_buffer_t *pBuffer, uint8_t *pData, size_t nSize)
{
    XASSERT(pBuffer, XSTDINV);
    XByteBuffer_Clear(pBuffer);
    XByteBuffer_Set(pBuffer, pData, nSize);
    pBuffer->nSize = nSize;
    return pBuffer->nSize;
}

int XByteBuffer_Add(xbyte_buffer_t *pBuffer, const uint8_t *pData, size_t nSize)
{
    if (pData == NULL || !nSize) return XSTDNON;
    else if (XByteBuffer_Reserve(pBuffer, nSize + 1) <= 0) return XSTDERR;
    memcpy(&pBuffer->pData[pBuffer->nUsed], pData, nSize);
    pBuffer->nUsed += nSize;
    pBuffer->pData[pBuffer->nUsed] = '\0';
    return (int)pBuffer->nUsed;
}

int XByteBuffer_AddStr(xbyte_buffer_t *pBuffer, xstring_t *pStr)
{
    const uint8_t *pData = (const uint8_t *)pStr->pData;
    return XByteBuffer_Add(pBuffer, pData, pStr->nLength);
}

int XByteBuffer_AddByte(xbyte_buffer_t *pBuffer, uint8_t nByte)
{
    if (XByteBuffer_Reserve(pBuffer, 2) <= 0) return XSTDERR;
    pBuffer->pData[pBuffer->nUsed++] = nByte;
    pBuffer->pData[pBuffer->nUsed] = '\0';
    return (int)pBuffer->nUsed;
}

int XByteBuffer_AddFmt(xbyte_buffer_t *pBuffer, const char *pFmt, ...)
{
    size_t nBytes = 0;
    va_list args;

    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, &nBytes);
    va_end(args);

    if (pDest == NULL)
    {
        pBuffer->nStatus = XSTDERR;
        return XSTDERR;
    }

    int nStatus = XByteBuffer_Add(pBuffer, (uint8_t*)pDest, nBytes);
    free(pDest);

    return nStatus;
}

int XByteBuffer_NullTerm(xbyte_buffer_t *pBuffer)
{
    if (XByteBuffer_Reserve(pBuffer, 1) <= 0) return XSTDERR;
    pBuffer->pData[pBuffer->nUsed] = '\0';
    return (int)pBuffer->nUsed;
}

int XByteBuffer_AddBuff(xbyte_buffer_t *pBuffer, xbyte_buffer_t *pSrc)
{
    if (pSrc->pData == NULL || !pSrc->nUsed) return XSTDERR;
    XByteBuffer_Add(pBuffer, pSrc->pData, pSrc->nUsed);
    return pBuffer->nStatus;
}

int XByteBuffer_Insert(xbyte_buffer_t *pBuffer, size_t nPosit, const uint8_t *pData, size_t nSize)
{
    if (nPosit >= pBuffer->nUsed) return XByteBuffer_Add(pBuffer, pData, nSize);
    else if (XByteBuffer_Reserve(pBuffer, nSize + 1) <= 0) return XSTDERR;

    uint8_t *pOffset = &pBuffer->pData[nPosit];
    size_t nTailSize = pBuffer->nUsed - nPosit;

    memmove(pOffset + nSize, pOffset, nTailSize);
    memcpy(&pBuffer->pData[nPosit], pData, nSize);

    pBuffer->nUsed += nSize;
    pBuffer->pData[pBuffer->nUsed] = '\0';
    return (int)pBuffer->nUsed;
}

int XByteBuffer_Remove(xbyte_buffer_t *pBuffer, size_t nPosit, size_t nSize)
{
    if (!nSize || nPosit >= pBuffer->nUsed) return 0;
    nSize = ((nPosit + nSize) > pBuffer->nUsed) ? 
            pBuffer->nUsed - nPosit : nSize;

    size_t nTailOffset = nPosit + nSize;
    if (nTailOffset >= pBuffer->nUsed)
    {
        pBuffer->nUsed = nPosit;
        return (int)pBuffer->nUsed;
    }

    size_t nTailSize = pBuffer->nUsed - nTailOffset;
    const uint8_t *pTail = &pBuffer->pData[nTailOffset];
    memmove(&pBuffer->pData[nPosit], pTail, nTailSize);

    pBuffer->nUsed -= nSize;
    pBuffer->pData[pBuffer->nUsed] = '\0';
    return (int)nSize;
}

int XByteBuffer_Delete(xbyte_buffer_t *pBuffer, size_t nPosit, size_t nSize)
{
    XByteBuffer_Remove(pBuffer, nPosit, nSize);
    XByteBuffer_Resize(pBuffer, pBuffer->nUsed + 1);
    pBuffer->pData[pBuffer->nUsed] = '\0';
    return pBuffer->nStatus;
}

int XByteBuffer_Advance(xbyte_buffer_t *pBuffer, size_t nSize)
{
    XByteBuffer_Delete(pBuffer, 0, nSize);
    return (int)pBuffer->nUsed;
}

int XDataBuffer_Init(xdata_buffer_t *pBuffer, size_t nSize, int nFixed)
{
    pBuffer->pData = (void**)calloc(nSize, sizeof(void*));
    if (pBuffer->pData == NULL)
    {
        pBuffer->nSize = 0;
        return XSTDERR;
    }

    unsigned int i;
    for (i = 0; i < nSize; i++)
        pBuffer->pData[i] = NULL;

    pBuffer->nStatus = 0;
    pBuffer->clearCb = NULL;
    pBuffer->nFixed = nFixed;
    pBuffer->nSize = nSize;
    pBuffer->nUsed = 0;
    return (int)nSize;
}

int XDataBuffer_Realloc(xdata_buffer_t *pBuffer)
{
    if (pBuffer->nFixed) return (int)pBuffer->nSize;
    void* pOldData = pBuffer->pData;
    void *pData = NULL;
    size_t nSize = 0;

    if (pBuffer->nUsed == pBuffer->nSize)
    {
        nSize = pBuffer->nSize * 2;
        pData = realloc(pBuffer->pData, sizeof(void*) * nSize);
    }
    else if (pBuffer->nUsed > 0 && ((float)pBuffer->nUsed / (float)pBuffer->nSize) < 0.25)
    {
        nSize = pBuffer->nSize / 2;
        pData = realloc(pBuffer->pData, sizeof(void*) * nSize);
    }
    else return (int)pBuffer->nSize;

    if (pData != NULL)
    {
        pBuffer->pData = (void**)pData;
        pBuffer->nSize = nSize;

        size_t i;
        for (i = pBuffer->nUsed; i < nSize; i++) 
            pBuffer->pData[i] = NULL;

        return (int)pBuffer->nSize;
    }

    pBuffer->pData = pOldData;
    return XSTDERR;
}

void XDataBuffer_Clear(xdata_buffer_t *pBuffer)
{
    unsigned int i;
    for (i = 0; i < pBuffer->nSize; i++)
    {
        if (pBuffer->clearCb != NULL)
            pBuffer->clearCb(pBuffer->pData[i]);
        pBuffer->pData[i] = NULL;
    }
    pBuffer->nUsed = 0;
}

void XDataBuffer_Destroy(xdata_buffer_t *pBuffer)
{
    XDataBuffer_Clear(pBuffer);
    if (pBuffer->pData != NULL)
    {
        free(pBuffer->pData);
        pBuffer->pData = NULL;
    }
}

int XDataBuffer_Add(xdata_buffer_t *pBuffer, void *pData)
{
    if (pBuffer->nUsed >= pBuffer->nSize) return XSTDERR;
    pBuffer->pData[pBuffer->nUsed++] = pData;
    int nStat = XDataBuffer_Realloc(pBuffer);
    return nStat > 0 ? (int)pBuffer->nUsed-1 : -2;
}

void* XDataBuffer_Set(xdata_buffer_t *pBuffer, unsigned int nIndex, void *pData)
{
    void *pOldData = NULL;
    if (nIndex < pBuffer->nSize || nIndex > 0)
    {
        pOldData = pBuffer->pData[nIndex];
        pBuffer->pData[nIndex] = pData;
        pBuffer->nUsed += pOldData ? 0 : 1;
    }

    return pOldData;
}

void* XDataBuffer_Get(xdata_buffer_t *pBuffer, unsigned int nIndex)
{
    if (nIndex >= pBuffer->nUsed || !nIndex) return NULL;
    return pBuffer->pData[nIndex];
}

void* XDataBuffer_Pop(xdata_buffer_t *pBuffer, unsigned int nIndex)
{
    void *pRetVal = XDataBuffer_Get(pBuffer, nIndex);
    if (pRetVal == NULL) return NULL;
    size_t i;

    for (i = (size_t)nIndex; i < pBuffer->nUsed; i++)
    {
        if ((i + 1) >= pBuffer->nUsed) break;
        pBuffer->pData[i] = pBuffer->pData[i+1];
    }

    pBuffer->pData[--pBuffer->nUsed] = NULL;
    XDataBuffer_Realloc(pBuffer);
    return pRetVal;
}

int XRingBuffer_Init(xring_buffer_t *pBuffer, size_t nSize)
{
    pBuffer->nSize = nSize;
    pBuffer->nFront = 0;
    pBuffer->nBack = 0;
    pBuffer->nUsed = 0;
    unsigned int i;

    pBuffer->pData = (xbyte_buffer_t**)calloc(nSize, sizeof(xbyte_buffer_t*));
    if (pBuffer->pData == NULL) return 0;

    for (i = 0; i < pBuffer->nSize; i++)
    {
        pBuffer->pData[i] = (xbyte_buffer_t*)malloc(sizeof(xbyte_buffer_t));
        if (pBuffer->pData[i]) XByteBuffer_Init(pBuffer->pData[i], 0, 0);
    }

    return (int)pBuffer->nSize;
}

void XRingBuffer_Reset(xring_buffer_t *pBuffer)
{
    unsigned long i;
    for (i = 0; i < pBuffer->nSize; i++)
    {
        if (pBuffer->pData[i] != NULL)
        {
            XByteBuffer_Clear(pBuffer->pData[i]);
            free(pBuffer->pData[i]);
            pBuffer->pData[i] = NULL;
        }
    }

    pBuffer->nFront = 0;
    pBuffer->nBack = 0;
    pBuffer->nUsed = 0;
}

void XRingBuffer_Destroy(xring_buffer_t *pBuffer)
{
    XRingBuffer_Reset(pBuffer);
    free(pBuffer->pData);
    pBuffer->pData = NULL;
    pBuffer->nSize = 0;
}

void XRingBuffer_Update(xring_buffer_t *pBuffer, int nAdd)
{
    if (nAdd) pBuffer->nBack++;
    else pBuffer->nFront++;

    if ((size_t)pBuffer->nFront >= pBuffer->nSize) pBuffer->nFront = 0;
    if ((size_t)pBuffer->nBack >= pBuffer->nSize) pBuffer->nBack = 0;

    pBuffer->nUsed = (pBuffer->nBack > pBuffer->nFront) ? 
        (pBuffer->nBack - pBuffer->nFront) : 
        (pBuffer->nSize - (pBuffer->nFront - pBuffer->nBack));
}

void XRingBuffer_Advance(xring_buffer_t *pBuffer)
{
    if (!pBuffer->nUsed || !pBuffer->nSize) return;
    xbyte_buffer_t *pBuffData = pBuffer->pData[pBuffer->nFront];
    if (pBuffData != NULL) XByteBuffer_Clear(pBuffData);
    XRingBuffer_Update(pBuffer, 0);
}

int XRingBuffer_AddData(xring_buffer_t *pBuffer, const uint8_t* pData, size_t nSize)
{
    if (pBuffer->nUsed >= pBuffer->nSize) return 0;
    xbyte_buffer_t *pBuffData = pBuffer->pData[pBuffer->nBack];

    if (pBuffData == NULL)
    {
        pBuffData = malloc(sizeof(xbyte_buffer_t));
        if (pBuffData == NULL) return 0;
    }

    int nStatus = XByteBuffer_Add(pBuffData, pData, nSize);
    if (nStatus > 0) XRingBuffer_Update(pBuffer, 1);
    return nStatus;
}

int XRingBuffer_AddDataAdv(xring_buffer_t *pBuffer, const uint8_t* pData, size_t nSize)
{
    if (pBuffer->nUsed >= pBuffer->nSize) XRingBuffer_Advance(pBuffer);
    return XRingBuffer_AddData(pBuffer, pData, nSize);
}

int XRingBuffer_GetData(xring_buffer_t *pBuffer, uint8_t** pData, size_t* pSize)
{
    if (pBuffer->nUsed >= pBuffer->nSize) return 0;
    xbyte_buffer_t *pBuffData = pBuffer->pData[pBuffer->nFront];
    if (pBuffData == NULL) return 0;
    *pData = pBuffData->pData;
    *pSize = pBuffData->nSize;
    return 1;
}

int XRingBuffer_Pop(xring_buffer_t *pBuffer, uint8_t* pData, size_t nSize)
{
    size_t nCopySize = 0;
    if (pBuffer->nUsed >= pBuffer->nSize) return (int)nCopySize;
    xbyte_buffer_t *pBuffData = pBuffer->pData[pBuffer->nFront];

    if (pBuffData != NULL)
    {
        nCopySize = (nSize > pBuffData->nSize) ? pBuffData->nSize : nSize;
        memcpy(pData, pBuffData->pData, nCopySize);
        XByteBuffer_Clear(pBuffData);
    }

    XRingBuffer_Update(pBuffer, 0);
    return (int)nCopySize;
}
