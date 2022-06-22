/*!
 *  @file libxutils/src/xmap.h
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of dynamically allocated hash map
 */

#ifndef __XUTILS_XMAP_H__
#define __XUTILS_XMAP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"

#define XMAP_INITIAL_SIZE   16
#define XMAP_CHAIN_LENGTH   32

#define XMAP_EINIT          -6  /* Map is not initialized */
#define XMAP_MISSING        -5  /* No such element */
#define XMAP_OINV           -4  /* Invalid parameter */
#define XMAP_FULL           -3  /* Hashmap is full */
#define XMAP_OMEM           -2  /* Out of Memory */
#define XMAP_STOP           -1  /* Stop iteration */
#define XMAP_EMPTY          0   /* Map is empty */
#define XMAP_OK             1   /* Success */

typedef struct XMapPair {
    char *pKey;
    void *pData;
    int nUsed;
} xmap_pair_t;

typedef int(*xmap_iterator_t)(xmap_pair_t*, void*);
typedef void(*xmap_clear_cb_t)(xmap_pair_t*);

typedef struct XMap {
    xmap_clear_cb_t clearCb;
    xmap_pair_t *pPairs;
    size_t nTableSize;
    size_t nUsed;
    int nAlloc;
} xmap_t;

int XMap_Init(xmap_t *pMap, size_t nSize);
int XMap_Realloc(xmap_t *pMap);
void XMap_Destroy(xmap_t *pMap);

xmap_t *XMap_New(size_t nSize);
void XMap_Free(xmap_t *pMap);

xmap_pair_t *XMap_GetPair(xmap_t *pMap, const char* pKey);
void* XMap_GetIndex(xmap_t *pMap, const char* pKey, int *pIndex);
void* XMap_Get(xmap_t *pMap, const char* pKey);
int XMap_Put(xmap_t *pMap, char* pKey, void *pValue);
int XMap_PutPair(xmap_t *pMap, xmap_pair_t *pPair);
int XMap_Remove(xmap_t *pMap, const char* pKey);
int XMap_Update(xmap_t *pMap, int nHash, char *pKey, void *pValue);

int XMap_GetHash(xmap_t *pMap, const char* pKey);
int XMap_Hash(xmap_t *pMap, const char *pStr);

int XMap_Iterate(xmap_t *pMap, xmap_iterator_t itfunc, void *pCtx);
int XMap_UsedSize(xmap_t *pMap);

#ifdef __cplusplus
}
#endif


#endif /* __XUTILS_XMAP_H__ */