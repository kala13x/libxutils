/*!
 *  @file libxutils/src/data/hash.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of hash tables
 */

#ifndef __XUTILS_HASH_H__
#define __XUTILS_HASH_H__

#ifdef __cplusplus
extern "C" {
#endif

#define XHASH_MIX(num, range) ((num ^ (num >> 8) ^ (num >> 16)) % range)

#include "xstd.h"
#include "list.h"

#ifdef _XUTILS_HASH_MODULES
#define XHASH_MODULES _XUTILS_HASH_MODULES
#else
#define XHASH_MODULES 32
#endif

typedef struct XHashData {
    void* pData;
    size_t nSize;
    int nKey;
} xhash_pair_t;

typedef void(*xhash_clearcb_t)(void *, void*, int);
typedef int(*xhash_itfunc_t)(xhash_pair_t*, void*);

typedef struct XHash {
    xhash_clearcb_t clearCb;
    void *pUserContext;
    size_t nPairCount;
    xlist_t tables[XHASH_MODULES];
} xhash_t;

int XHash_Init(xhash_t *pHash, xhash_clearcb_t clearCb, void *pCtx);
void XHash_Iterate(xhash_t *pHash, xhash_itfunc_t itfunc, void *pCtx);
void XHash_Destroy(xhash_t *pHash);

xhash_pair_t* XHash_GetPair(xhash_t *pHash, int nKey);
xlist_t* XHash_GetNode(xhash_t *pHash, int nKey);
void* XHash_GetData(xhash_t *pHash, int nKey);
int XHash_GetSize(xhash_t *pHash, int nKey);

xhash_pair_t* XHash_NewPair(void* pData, size_t nSize, int nKey);
int XHash_InsertPair(xhash_t *pHash, xhash_pair_t *pData);
int XHash_Insert(xhash_t *pHash, void *pData, size_t nSize, int nKey);
int XHash_Delete(xhash_t *pHash, int nKey);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_HASH_H__ */