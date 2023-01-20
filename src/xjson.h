/*!
 *  @file libxutils/src/xjson.h
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of the lexical analyzer 
 * and recursive descent parser with JSON grammar
 */

#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>

#ifndef __XUTILS_JSON_H__
#define __XUTILS_JSON_H__

#ifdef __cplusplus
extern "C" {
#endif

#define XJSON_SUCCESS     1
#define XJSON_FAILURE     0

typedef enum {
    XJSON_TOKEN_INVALID = (uint8_t)0,
    XJSON_TOKEN_COMMA,
    XJSON_TOKEN_COLON,
    XJSON_TOKEN_QUOTE,
    XJSON_TOKEN_LCURLY,
    XJSON_TOKEN_RCURLY,
    XJSON_TOKEN_LPAREN,
    XJSON_TOKEN_RPAREN,
    XJSON_TOKEN_LSQUARE,
    XJSON_TOKEN_RSQUARE,
    XJSON_TOKEN_INTEGER,
    XJSON_TOKEN_FLOAT,
    XJSON_TOKEN_BOOL,
    XJSON_TOKEN_NULL,
    XJSON_TOKEN_EOF
} xjson_token_type_t;

typedef struct xjson_token_ {
    xjson_token_type_t nType;
    const char *pData;
    size_t nLength;
} xjson_token_t;

typedef enum {
    XJSON_TYPE_INVALID = (uint8_t)0,
    XJSON_TYPE_OBJECT,
    XJSON_TYPE_ARRAY,
    XJSON_TYPE_BOOLEAN,
    XJSON_TYPE_STRING,
    XJSON_TYPE_NUMBER,
    XJSON_TYPE_FLOAT,
    XJSON_TYPE_NULL,
} xjson_type_t;

typedef struct xjson_obj_ {
    xjson_type_t nType;
    uint8_t nAllowUpdate;
    uint8_t nAllocated;
    void *pData;
    char *pName;
} xjson_obj_t;

typedef enum {
    XJSON_ERR_NONE = (uint8_t)0,
    XJSON_ERR_UNEXPECTED,
    XJSON_ERR_INVALID,
    XJSON_ERR_BOUNDS,
    XJSON_ERR_EXITS,
    XJSON_ERR_ALLOC
} xjson_error_t;

xjson_error_t XJSON_AddObject(xjson_obj_t *pDst, xjson_obj_t *pSrc);
xjson_obj_t* XJSON_CreateObject(const char *pName, void *pValue, xjson_type_t nType);
xjson_obj_t* XJSON_NewObject(const char *pName, uint8_t nAllowUpdate);
xjson_obj_t* XJSON_NewArray(const char *pName, uint8_t nAllowUpdate);
xjson_obj_t* XJSON_NewU64(const char *pName, uint64_t nValue);
xjson_obj_t* XJSON_NewU32(const char *pName, uint32_t nValue);
xjson_obj_t* XJSON_NewInt(const char *pName, int nValue);
xjson_obj_t* XJSON_NewFloat(const char *pName, double fValue);
xjson_obj_t* XJSON_NewString(const char *pName, const char *pValue);
xjson_obj_t* XJSON_NewBool(const char *pName, int nValue);
xjson_obj_t* XJSON_NewNull(const char *pName);
void XJSON_FreeObject(xjson_obj_t *pObj);

xjson_error_t XJSON_AddU64(xjson_obj_t *pObject, const char *pName, uint64_t nValue);
xjson_error_t XJSON_AddU32(xjson_obj_t *pObject, const char *pName, uint32_t nValue);
xjson_error_t XJSON_AddInt(xjson_obj_t *pObject, const char *pName, int nValue);
xjson_error_t XJSON_AddFloat(xjson_obj_t *pObject, const char *pName, double fValue);
xjson_error_t XJSON_AddString(xjson_obj_t *pObject, const char *pName, const char *pValue);
xjson_error_t XJSON_AddBool(xjson_obj_t *pObject, const char *pName, int nValue);
xjson_error_t XJSON_AddNull(xjson_obj_t *pObject, const char *pName);

typedef struct xjson_ {
    xjson_token_t lastToken;
    xjson_error_t nError;
    xjson_obj_t *pRootObj;
    const char *pData;
    size_t nDataSize;
    size_t nOffset;
} xjson_t;

size_t XJSON_GetErrorStr(xjson_t *pJson, char *pOutput, size_t nSize);

int XJSON_Parse(xjson_t *pJson, const char *pData, size_t nSize);
void XJSON_Destroy(xjson_t *pJson);
void XJSON_Init(xjson_t *pJson);

size_t XJSON_GetArrayLength(xjson_obj_t *pObj);
int XJSON_RemoveArrayItem(xjson_obj_t *pObj, size_t nIndex);
xjson_obj_t* XJSON_GetObject(xjson_obj_t *pObj, const char *pName);
xjson_obj_t* XJSON_GetArrayItem(xjson_obj_t *pObj, size_t nIndex);
xjson_obj_t *XJSON_GetOrCreateObject(xjson_obj_t *pObj, const char *pName, uint8_t nAllowUpdate);
xjson_obj_t *XJSON_GetOrCreateArray(xjson_obj_t *pObj, const char *pName, uint8_t nAllowUpdate);

const char* XJSON_GetString(xjson_obj_t *pObj);
uint32_t XJSON_GetU32(xjson_obj_t *pObj);
uint64_t XJSON_GetU64(xjson_obj_t *pObj);
uint8_t XJSON_GetBool(xjson_obj_t *pObj);
double XJSON_GetFloat(xjson_obj_t *pObj);
int XJSON_GetInt(xjson_obj_t *pObj);

typedef struct xjson_writer_ {
    size_t nTabSize;
    size_t nIdents;
    size_t nLength;
    size_t nAvail;
    size_t nSize;
    char *pData;
    uint8_t nAlloc;
} xjson_writer_t;

void XJSON_DestroyWriter(xjson_writer_t *pWriter);
int XJSON_InitWriter(xjson_writer_t *pWriter, char *pOutput, size_t nSize);
int XJSON_WriteObject(xjson_obj_t *pObj, xjson_writer_t *pWriter);
int XJSON_Write(xjson_t *pJson, char *pOutput, size_t nSize);

char* XJSON_DumpObj(xjson_obj_t *pJsonObj, size_t nLint, size_t *pLength);
char* XJSON_Dump(xjson_t *pJson, size_t nLint, size_t *pLength);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_JSON_H__ */