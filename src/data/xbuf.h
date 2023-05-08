/*!
 *  @file libxutils/src/data/buf.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Dynamically allocated byte and data buffers 
 */

#ifndef __XUTILS_BUFFER_H__
#define __XUTILS_BUFFER_H__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "xstr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void(*xdata_clear_cb_t)(void *pData);

typedef struct XByteBuffer {
    uint8_t *pData;
    size_t nSize;
    size_t nUsed;
    uint16_t nAlloc;
    uint16_t nFast;
    int nStatus;
} xbyte_buffer_t;

uint8_t *XByteData_Dup(const uint8_t *pBuff, size_t nSize);
xbyte_buffer_t* XByteBuffer_New(size_t nSize, int nFastAlloc);
void XByteBuffer_Free(xbyte_buffer_t **pBuffer);
void XByteBuffer_Clear(xbyte_buffer_t *pBuffer);
void XByteBuffer_Reset(xbyte_buffer_t *pBuffer);
int XByteBuffer_Terminate(xbyte_buffer_t *pBuffer, size_t nPosit);
int XByteBuffer_Resize(xbyte_buffer_t *pBuffer, size_t nSize);
int XByteBuffer_Reserve(xbyte_buffer_t *pBuffer, size_t nSize);
int XByteBuffer_Init(xbyte_buffer_t *pBuffer, size_t nSize, int nFastAlloc);
int XByteBuffer_SetData(xbyte_buffer_t *pBuffer, uint8_t *pData, size_t nSize);
int XByteBuffer_OwnData(xbyte_buffer_t *pBuffer, uint8_t *pData, size_t nSize);
int XByteBuffer_Set(xbyte_buffer_t *pBuffer, xbyte_buffer_t *pSrc);
int XByteBuffer_Own(xbyte_buffer_t *pBuffer, xbyte_buffer_t *pSrc);
int XByteBuffer_Add(xbyte_buffer_t *pBuffer, const uint8_t *pData, size_t nSize);
int XByteBuffer_AddByte(xbyte_buffer_t *pBuffer, uint8_t nByte);
int XByteBuffer_AddStr(xbyte_buffer_t *pBuffer, xstring_t *pStr);
int XByteBuffer_AddFmt(xbyte_buffer_t *pBuffer, const char *pFmt, ...);
int XByteBuffer_AddBuff(xbyte_buffer_t *pBuffer, xbyte_buffer_t *pSrc);
int XByteBuffer_Insert(xbyte_buffer_t *pBuffer, size_t nPosit, const uint8_t *pData, size_t nSize);
int XByteBuffer_Remove(xbyte_buffer_t *pBuffer, size_t nPosit, size_t nSize);
int XByteBuffer_Delete(xbyte_buffer_t *pBuffer, size_t nPosit, size_t nSize);
int XByteBuffer_Advance(xbyte_buffer_t *pBuffer, size_t nSize);
int XByteBuffer_NullTerm(xbyte_buffer_t *pBuffer);

typedef struct XDataBuffer {
    xdata_clear_cb_t clearCb;
    void **pData;
    size_t nSize;
    size_t nUsed;
    int nStatus;
    int nFixed;
} xdata_buffer_t;

void XDataBuffer_Destroy(xdata_buffer_t *pBuffer);
void XDataBuffer_Clear(xdata_buffer_t *pBuffer);
int XDataBuffer_Init(xdata_buffer_t *pBuffer, size_t nSize, int nFixed);
int XDataBuffer_Add(xdata_buffer_t *pBuffer, void *pData);
void* XDataBuffer_Set(xdata_buffer_t *pBuffer, unsigned int nIndex, void *pData);
void* XDataBuffer_Get(xdata_buffer_t *pBuffer, unsigned int nIndex);
void* XDataBuffer_Pop(xdata_buffer_t *pBuffer, unsigned int nIndex);

typedef struct XRingBuffer {
    xbyte_buffer_t **pData;
    size_t nUsed;
    size_t nSize;
    size_t nFront;
    size_t nBack;
} xring_buffer_t;

int XRingBuffer_Init(xring_buffer_t *pBuffer, size_t nSize);
void XRingBuffer_Reset(xring_buffer_t *pBuffer);
void XRingBuffer_Destroy(xring_buffer_t *pBuffer);
void XRingBuffer_Update(xring_buffer_t *pBuffer, int nAdd);
void XRingBuffer_Advance(xring_buffer_t *pBuffer);
int XRingBuffer_AddData(xring_buffer_t *pBuffer, const uint8_t* pData, size_t nSize);
int XRingBuffer_AddDataAdv(xring_buffer_t *pBuffer, const uint8_t* pData, size_t nSize);
int XRingBuffer_GetData(xring_buffer_t *pBuffer, uint8_t** pData, size_t* pSize);
int XRingBuffer_Pop(xring_buffer_t *pBuffer, uint8_t* pData, size_t nSize);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_BUFFER_H__ */