/*!
 *  @file libxutils/src/data/json.c
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of the lexical analyzer
 * and recursive descent parser with JSON grammar
 */

#ifdef _XUTILS_USE_GNU
#define _GNU_SOURCE
#endif

#include "json.h"
#include "str.h"
#include "map.h"

#define XOBJ_INITIAL_SIZE   2
#define XJSON_IDENT_INC     1
#define XJSON_IDENT_DEC     0

#define XJSON_NUMBER_MAX    64
#define XJSON_BOOL_MAX      6
#define XJSON_NULL_MAX      5

typedef struct xjson_iterator_ {
    xjson_writer_t *pWriter;
    size_t nCurrent;
    size_t nUsed;
} xjson_iterator_t;

size_t XJSON_GetErrorStr(xjson_t *pJson, char *pOutput, size_t nSize)
{
    switch (pJson->nError)
    {
        case XJSON_ERR_INVALID:
            return xstrncpyf(pOutput, nSize, "Invalid item at posit(%zu)", pJson->nOffset);
        case XJSON_ERR_EXITS:
            return xstrncpyf(pOutput, nSize, "Duplicate Key at posit(%zu)", pJson->nOffset);
        case XJSON_ERR_BOUNDS:
            return xstrncpyf(pOutput, nSize, "Unexpected EOF at posit(%zu)", pJson->nOffset);
        case XJSON_ERR_ALLOC:
            return xstrncpyf(pOutput, nSize, "Can not allocate memory for object at posit(%zu)", pJson->nOffset);
        case XJSON_ERR_UNEXPECTED:
            return xstrncpyf(pOutput, nSize, "Unexpected symbol '%c' at posit(%zu)", pJson->pData[pJson->nOffset], pJson->nOffset);
        case XJSON_ERR_NONE:
        default:
            break;
    }

    return xstrncpyf(pOutput, nSize, "Undeclared error");
}

/////////////////////////////////////////////////////////////////////////
// Start of lexical parser

static int XJSON_UnexpectedToken(xjson_t *pJson)
{
    xjson_token_t *pToken = &pJson->lastToken;
    pJson->nError = XJSON_ERR_UNEXPECTED;
    pJson->nOffset -= pToken->nLength;

    if (pToken->nType == XJSON_TOKEN_QUOTE)
        pJson->nOffset -= 2;

    return XJSON_FAILURE;
}

static int XJSON_UndoLastToken(xjson_t *pJson)
{
    pJson->nOffset -= pJson->lastToken.nLength;
    return XJSON_SUCCESS;
}

static int XJSON_CheckBounds(xjson_t *pJson)
{
    if (pJson->nOffset >= pJson->nDataSize)
    {
        pJson->nError = XJSON_ERR_BOUNDS;
        return XJSON_FAILURE;
    }

    return XJSON_SUCCESS;
}

static int XJSON_NextChar(xjson_t *pJson, char *pChar)
{
    XASSERT(XJSON_CheckBounds(pJson), XJSON_FAILURE);
    char nCharacter = pJson->pData[pJson->nOffset++];

    /* Skip space and new line characters */
    while (nCharacter == ' ' ||
           nCharacter == '\n' ||
           nCharacter == '\t')
    {
        XASSERT(XJSON_CheckBounds(pJson), XJSON_FAILURE);
        nCharacter = pJson->pData[pJson->nOffset++];
    }

    *pChar = nCharacter;
    return XJSON_SUCCESS;
}

static int XJSON_ParseDigit(xjson_t *pJson, char nCharacter)
{
    xjson_token_t *pToken = &pJson->lastToken;
    pToken->nType = XJSON_TOKEN_INVALID;
    size_t nPosition = pJson->nOffset;
    uint8_t nPoint = 0;

    if (nCharacter == '-') nCharacter = pJson->pData[pJson->nOffset];

    while (isdigit(nCharacter) || (nPoint < 2 && nCharacter == '.'))
    {
        XASSERT(XJSON_CheckBounds(pJson), XJSON_FAILURE);
        nCharacter = pJson->pData[pJson->nOffset++];

        if (nCharacter == '.' && ++nPoint == 2)
        {
            pToken->nLength = pJson->nOffset - nPosition;
            return XJSON_UnexpectedToken(pJson);
        }
    }

    pToken->nLength = pJson->nOffset - nPosition;
    pToken->nType = nPoint ? XJSON_TOKEN_FLOAT : XJSON_TOKEN_INTEGER;
    pToken->pData = &pJson->pData[nPosition - 1];
    pJson->nOffset--;

    return XJSON_SUCCESS;
}

static int XJSON_ParseQuote(xjson_t *pJson)
{
    xjson_token_t *pToken = &pJson->lastToken;
    pToken->nType = XJSON_TOKEN_INVALID;
    XASSERT(XJSON_CheckBounds(pJson), XJSON_FAILURE);

    size_t nStart = pJson->nOffset;
    char nCurr = 0, nPrev = 0;

    for (;;)
    {
        if (nCurr == '"' && nPrev != '\\') break;
        XASSERT(XJSON_CheckBounds(pJson), XJSON_FAILURE);

        nPrev = pJson->pData[pJson->nOffset-1];
        nCurr = pJson->pData[pJson->nOffset++];
    }

    pToken->nLength = pJson->nOffset - nStart - 1;
    pToken->pData = &pJson->pData[nStart];
    pToken->nType = XJSON_TOKEN_QUOTE;

    return XJSON_SUCCESS;
}

static int XJSON_IsAlphabet(char nChar)
{
    return ((nChar >= 'a' && nChar <= 'z') ||
            (nChar >= 'A' && nChar <= 'Z')) ?
                XJSON_SUCCESS : XJSON_FAILURE;
}

static int XJSON_ParseAlphabet(xjson_t *pJson, char nCharacter)
{
    xjson_token_t *pToken = &pJson->lastToken;
    pToken->nType = XJSON_TOKEN_INVALID;
    size_t nPosition = pJson->nOffset;

    while (XJSON_IsAlphabet(nCharacter))
    {
        XASSERT(XJSON_CheckBounds(pJson), XJSON_FAILURE);
        nCharacter = pJson->pData[pJson->nOffset++];
    }

    pToken->nLength = pJson->nOffset - nPosition;
    pToken->pData = &pJson->pData[nPosition - 1];
    pJson->nOffset--;

    if (!strncmp((char*)pToken->pData, "null", 4))
    {
        pToken->nType = XJSON_TOKEN_NULL;
        return XJSON_SUCCESS;
    }

    if (!strncmp((char*)pToken->pData, "true", 4) ||
        !strncmp((char*)pToken->pData, "false", 5))
    {
        pToken->nType = XJSON_TOKEN_BOOL;
        return XJSON_SUCCESS;
    }

    return XJSON_UnexpectedToken(pJson);
}

static int XJSON_GetNextToken(xjson_t *pJson)
{
    xjson_token_t *pToken = &pJson->lastToken;
    pToken->nType = XJSON_TOKEN_INVALID;
    pToken->pData = NULL;
    pToken->nLength = 0;
    char nChar = 0;

    if (!XJSON_NextChar(pJson, &nChar))
    {
        pToken->nType = XJSON_TOKEN_EOF;
        return XJSON_FAILURE; // End of input
    }

    if (nChar == '-' || isdigit(nChar)) return XJSON_ParseDigit(pJson, nChar);
    else if (XJSON_IsAlphabet(nChar)) return XJSON_ParseAlphabet(pJson, nChar);
    else if (nChar == '"') return XJSON_ParseQuote(pJson);

    pToken->pData = &pJson->pData[pJson->nOffset-1];
    pToken->nLength = 1;

    switch ((int)nChar)
    {
        case '\0': case EOF:
            pToken->nType = XJSON_TOKEN_EOF;
            pToken->pData = NULL;
            pToken->nLength = 0;
            break;
        case '{':
            pToken->nType = XJSON_TOKEN_LCURLY;
            break;
        case '}':
            pToken->nType = XJSON_TOKEN_RCURLY;
            break;
        case '[':
            pToken->nType = XJSON_TOKEN_LSQUARE;
            break;
        case ']':
            pToken->nType = XJSON_TOKEN_RSQUARE;
            break;
        case ':':
            pToken->nType = XJSON_TOKEN_COLON;
            break;
        case ',':
            pToken->nType = XJSON_TOKEN_COMMA;
            break;
        default:
            return XJSON_UnexpectedToken(pJson);
    }

    return XJSON_SUCCESS;
}

static int XJSON_Expect(xjson_t *pJson, xjson_token_type_t nType)
{
    XASSERT(XJSON_GetNextToken(pJson), XJSON_FAILURE);
    xjson_token_t *pToken = &pJson->lastToken;

    if (pToken->nType == nType) return XJSON_SUCCESS;
    return XJSON_UnexpectedToken(pJson);
}

// End of lexical parser
/////////////////////////////////////////////////////////////////////////

static xjson_type_t XJSON_GetItemType(xjson_token_type_t eToken)
{
    switch(eToken)
    {
        case XJSON_TOKEN_INTEGER: return XJSON_TYPE_NUMBER;
        case XJSON_TOKEN_QUOTE: return XJSON_TYPE_STRING;
        case XJSON_TOKEN_FLOAT: return XJSON_TYPE_FLOAT;
        case XJSON_TOKEN_BOOL: return XJSON_TYPE_BOOLEAN;
        case XJSON_TOKEN_NULL: return XJSON_TYPE_NULL;
        case XJSON_TOKEN_EOF:
        case XJSON_TOKEN_COMMA:
        case XJSON_TOKEN_COLON:
        case XJSON_TOKEN_LCURLY:
        case XJSON_TOKEN_RCURLY:
        case XJSON_TOKEN_LPAREN:
        case XJSON_TOKEN_RPAREN:
        case XJSON_TOKEN_LSQUARE:
        case XJSON_TOKEN_RSQUARE:
        case XJSON_TOKEN_INVALID:
        default: break;
    }

    return XJSON_TYPE_INVALID;
}

void XJSON_FreeObject(xjson_obj_t *pObj)
{
    if (pObj != NULL)
    {
        xpool_t *pPool = pObj->pPool;

        if (pObj->pData != NULL)
        {
            if (pObj->nType == XJSON_TYPE_OBJECT)
                XMap_Destroy((xmap_t*)pObj->pData);
            else if (pObj->nType == XJSON_TYPE_ARRAY)
                XArray_Destroy((xarray_t*)pObj->pData);
            else xfree(pPool, pObj->pData);
        }

        if (pObj->pName != NULL) xfree(pPool, pObj->pName);
        if (pObj->nAllocated) xfree(pPool, pObj);
    }
}

static void XJSON_ObjectClearCb(xmap_pair_t *pPair)
{
    if (pPair == NULL) return;
    XJSON_FreeObject((xjson_obj_t*)pPair->pData);
}

static void XJSON_ArrayClearCb(xarray_data_t *pItem)
{
    if (pItem == NULL) return;
    XJSON_FreeObject((xjson_obj_t*)pItem->pData);
}

xjson_error_t XJSON_AddObject(xjson_obj_t *pDst, xjson_obj_t *pSrc)
{
    if (pDst == NULL || pSrc == NULL) return XJSON_ERR_INVALID;
    else if (pDst->nType == XJSON_TYPE_OBJECT)
    {
        xmap_t *pMap = (xmap_t*)pDst->pData;
        int nHash = 0;

        xjson_obj_t *pFound = (xjson_obj_t*)XMap_GetIndex(pMap, pSrc->pName, &nHash);
        if (pFound != NULL)
        {
            if (!pDst->nAllowUpdate) return XJSON_ERR_EXITS;
            XJSON_FreeObject(pFound);

            int nStatus = XMap_Update(pMap, nHash, pSrc->pName, (void*)pSrc);
            return nStatus < 0 ? XJSON_ERR_BOUNDS : XJSON_ERR_NONE;
        }

        int nStatus = XMap_Put(pMap, pSrc->pName, (void*)pSrc);
        return nStatus < 0 ? XJSON_ERR_ALLOC : XJSON_ERR_NONE;
    }
    else if (pDst->nType == XJSON_TYPE_ARRAY)
    {
        xarray_t *pArray = (xarray_t*)pDst->pData;
        int nStatus = XArray_AddData(pArray, (void*)pSrc, 0);
        return nStatus < 0 ? XJSON_ERR_ALLOC : XJSON_ERR_NONE;
    }

    return XJSON_ERR_INVALID;
}

xjson_obj_t* XJSON_CreateObject(xpool_t *pPool, const char *pName, void *pValue, xjson_type_t nType)
{
    xjson_obj_t *pObj = (xjson_obj_t*)xalloc(pPool, sizeof(xjson_obj_t));
    if (pObj == NULL) return NULL;

    pObj->nAllowUpdate = 0;
    pObj->nAllowLinter = 1;
    pObj->nAllocated = 1;
    pObj->pName = NULL;
    pObj->pData = pValue;
    pObj->nType = nType;
    pObj->pPool = pPool;

    if (pName == NULL) return pObj;
    size_t nLength = strlen(pName);

    if (nLength > 0)
    {
        pObj->pName = (char*)xalloc(pPool, nLength + 1);
        if (pObj->pName == NULL)
        {
            xfree(pPool, pObj);
            return NULL;
        }

        memcpy(pObj->pName, pName, nLength);
        pObj->pName[nLength] = '\0';
    }

    return pObj;
}

xjson_obj_t* XJSON_NewObject(xpool_t *pPool, const char *pName, uint8_t nAllowUpdate)
{
    xmap_t *pMap = XMap_New(pPool, XOBJ_INITIAL_SIZE);
    if (pMap == NULL) return NULL;

    pMap->clearCb = XJSON_ObjectClearCb;
    xjson_obj_t *pObj = XJSON_CreateObject(pPool, pName, pMap, XJSON_TYPE_OBJECT);

    if (pObj == NULL)
    {
        XMap_Destroy(pMap);
        return NULL;
    }

    pObj->nAllowUpdate = nAllowUpdate;
    return pObj;
}

xjson_obj_t* XJSON_NewArray(xpool_t *pPool, const char *pName, uint8_t nAllowUpdate)
{
    xarray_t *pArray = XArray_New(pPool, XOBJ_INITIAL_SIZE, 0);
    if (pArray == NULL) return NULL;

    pArray->clearCb = XJSON_ArrayClearCb;
    xjson_obj_t *pObj = XJSON_CreateObject(pPool, pName, pArray, XJSON_TYPE_ARRAY);

    if (pObj == NULL)
    {
        XArray_Destroy(pArray);
        return NULL;
    }

    pObj->nAllowUpdate = nAllowUpdate;
    return pObj;
}

xjson_obj_t* XJSON_NewU64(xpool_t *pPool, const char *pName, uint64_t nValue)
{
    char *pValue = (char*)xalloc(pPool, XJSON_NUMBER_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_NUMBER_MAX, "%"PRIu64, nValue);
    xjson_obj_t *pObj = XJSON_CreateObject(pPool, pName, pValue, XJSON_TYPE_NUMBER);

    if (pObj == NULL)
    {
        xfree(pPool, pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddU64(xjson_obj_t *pObject, const char *pName, uint64_t nValue)
{
    xpool_t *pPool = pObject->pPool;
    xjson_obj_t *pNewObj = XJSON_NewU64(pPool, pName, nValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;

    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewU32(xpool_t *pPool, const char *pName, uint32_t nValue)
{
    char *pValue = (char*)xalloc(pPool, XJSON_NUMBER_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_NUMBER_MAX, "%u", nValue);
    xjson_obj_t *pObj = XJSON_CreateObject(pPool, pName, pValue, XJSON_TYPE_NUMBER);

    if (pObj == NULL)
    {
        xfree(pPool, pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddU32(xjson_obj_t *pObject, const char *pName, uint32_t nValue)
{
    xpool_t *pPool = pObject->pPool;
    xjson_obj_t *pNewObj = XJSON_NewU32(pPool, pName, nValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;

    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewInt(xpool_t *pPool, const char *pName, int nValue)
{
    char *pValue = (char*)xalloc(pPool, XJSON_NUMBER_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_NUMBER_MAX, "%d", nValue);
    xjson_obj_t *pObj = XJSON_CreateObject(pPool, pName, pValue, XJSON_TYPE_NUMBER);

    if (pObj == NULL)
    {
        xfree(pPool, pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddInt(xjson_obj_t *pObject, const char *pName, int nValue)
{
    xpool_t *pPool = pObject->pPool;
    xjson_obj_t *pNewObj = XJSON_NewInt(pPool, pName, nValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;

    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewFloat(xpool_t *pPool, const char *pName, double fValue)
{
    char *pValue = (char*)xalloc(pPool, XJSON_NUMBER_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_NUMBER_MAX, "%lf", fValue);
    xjson_obj_t *pObj = XJSON_CreateObject(pPool, pName, pValue, XJSON_TYPE_FLOAT);

    if (pObj == NULL)
    {
        xfree(pPool, pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddFloat(xjson_obj_t *pObject, const char *pName, double fValue)
{
    xpool_t *pPool = pObject->pPool;
    xjson_obj_t *pNewObj = XJSON_NewFloat(pPool, pName, fValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;

    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewString(xpool_t *pPool, const char *pName, const char *pValue)
{
    char *pSaveValue = xstrpdup(pPool, pValue);
    if (pSaveValue == NULL) return NULL;

    xjson_obj_t *pObj = XJSON_CreateObject(pPool, pName, pSaveValue, XJSON_TYPE_STRING);
    if (pObj == NULL)
    {
        xfree(pPool, pSaveValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddString(xjson_obj_t *pObject, const char *pName, const char *pValue)
{
    xpool_t *pPool = pObject->pPool;
    if (pValue == NULL) return XJSON_AddNull(pObject, pName);

    xjson_obj_t *pNewObj = XJSON_NewString(pPool, pName, pValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;

    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_error_t XJSON_AddStrIfUsed(xjson_obj_t *pObject, const char *pName, const char *pValue)
{
    if (!xstrused(pValue)) return XJSON_ERR_NONE;
    xpool_t *pPool = pObject->pPool;

    xjson_obj_t *pNewObj = XJSON_NewString(pPool, pName, pValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;

    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewBool(xpool_t *pPool, const char *pName, int nValue)
{
    char *pValue = (char*)xalloc(pPool, XJSON_BOOL_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_BOOL_MAX, "%s", nValue ? "true" : "false");
    xjson_obj_t *pObj = XJSON_CreateObject(pPool, pName, pValue, XJSON_TYPE_BOOLEAN);

    if (pObj == NULL)
    {
        xfree(pPool, pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddBool(xjson_obj_t *pObject, const char *pName, int nValue)
{
    xpool_t *pPool = pObject->pPool;
    xjson_obj_t *pNewObj = XJSON_NewBool(pPool, pName, nValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;

    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewNull(xpool_t *pPool, const char *pName)
{
    char *pValue = (char*)xalloc(pPool, XJSON_NULL_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_NULL_MAX, "%s", "null");
    xjson_obj_t *pObj = XJSON_CreateObject(pPool, pName, pValue, XJSON_TYPE_NULL);

    if (pObj == NULL)
    {
        xfree(pPool, pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddNull(xjson_obj_t *pObject, const char *pName)
{
    xpool_t *pPool = pObject->pPool;
    xjson_obj_t *pNewObj = XJSON_NewNull(pPool, pName);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;

    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

/* Forward declararions */
int XJSON_ParseObject(xjson_t *pJson, xjson_obj_t *pObj);
int XJSON_ParseArray(xjson_t *pJson, xjson_obj_t *pObj);

static int XJSON_ParseNewObject(xjson_t *pJson, xjson_obj_t *pObj, const char *pName)
{
    xjson_obj_t *pNewObj = XJSON_NewObject(pJson->pPool, pName, 0);
    if (pNewObj == NULL)
    {
        pJson->nError = XJSON_ERR_ALLOC;
        return XJSON_FAILURE;
    }

    if (!XJSON_ParseObject(pJson, pNewObj))
    {
        XJSON_FreeObject(pNewObj);
        return XJSON_FAILURE;
    }

    pJson->nError = XJSON_AddObject(pObj, pNewObj);
    if (pJson->nError != XJSON_ERR_NONE)
    {
        XJSON_FreeObject(pNewObj);
        return XJSON_FAILURE;
    }

    return XJSON_Expect(pJson, XJSON_TOKEN_RCURLY);
}

static int XJSON_ParseNewArray(xjson_t *pJson, xjson_obj_t *pObj, const char *pName)
{
    xjson_obj_t *pNewObj = XJSON_NewArray(pJson->pPool, pName, 0);
    if (pNewObj == NULL)
    {
        pJson->nError = XJSON_ERR_ALLOC;
        return XJSON_FAILURE;
    }

    if (!XJSON_ParseArray(pJson, pNewObj))
    {
        XJSON_FreeObject(pNewObj);
        return XJSON_FAILURE;
    }

    pJson->nError = XJSON_AddObject(pObj, pNewObj);
    if (pJson->nError != XJSON_ERR_NONE)
    {
        XJSON_FreeObject(pNewObj);
        return XJSON_FAILURE;
    }

    return XJSON_Expect(pJson, XJSON_TOKEN_RSQUARE);
}

static int XJSON_CheckObject(xjson_obj_t *pObj, xjson_type_t nType)
{
    return (pObj != NULL &&
            pObj->pData != NULL &&
            pObj->nType == nType) ?
                XJSON_SUCCESS : XJSON_FAILURE;
}

static int XJSON_TokenIsItem(xjson_token_t *pToken)
{
    return (pToken->nType == XJSON_TOKEN_QUOTE ||
            pToken->nType == XJSON_TOKEN_FLOAT ||
            pToken->nType == XJSON_TOKEN_BOOL ||
            pToken->nType == XJSON_TOKEN_NULL ||
            pToken->nType == XJSON_TOKEN_INTEGER) ?
                XJSON_SUCCESS : XJSON_FAILURE;
}

static char* JSON_LastTokenValue(xjson_t *pJson)
{
    xjson_token_t *pToken = &pJson->lastToken;
    char *pValue = xalloc(pJson->pPool, pToken->nLength + 1);

    if (pValue == NULL)
    {
        pJson->nError = XJSON_ERR_ALLOC;
        return NULL;
    }

    memcpy(pValue, pToken->pData, pToken->nLength);
    pValue[pToken->nLength] = '\0';

    return pValue;
}

static int XJSON_PutItem(xjson_t *pJson, xjson_obj_t *pObj, const char *pName)
{
    xpool_t *pPool = pJson->pPool;
    xjson_token_t *pToken = &pJson->lastToken;
    xjson_type_t nType = XJSON_GetItemType(pToken->nType);

    if (nType == XJSON_TYPE_INVALID)
    {
        pJson->nError = XJSON_ERR_INVALID;
        return XJSON_FAILURE;
    }

    char *pValue = JSON_LastTokenValue(pJson);
    if (pValue == NULL) return XJSON_FAILURE;

    xjson_obj_t *pNewObj = XJSON_CreateObject(pPool, pName, pValue, nType);
    if (pNewObj == NULL)
    {
        xfree(pPool, pValue);
        pJson->nError = XJSON_ERR_ALLOC;
        return XJSON_FAILURE;
    }

    pJson->nError = XJSON_AddObject(pObj, pNewObj);
    if (pJson->nError != XJSON_ERR_NONE)
    {
        XJSON_FreeObject(pNewObj);
        return XJSON_FAILURE;
    }

    return XJSON_SUCCESS;
}

int XJSON_ParseArray(xjson_t *pJson, xjson_obj_t *pObj)
{
    xjson_token_t *pToken = &pJson->lastToken;
    XASSERT(XJSON_GetNextToken(pJson), XJSON_FAILURE);

    if (pToken->nType == XJSON_TOKEN_RSQUARE)
        return XJSON_UndoLastToken(pJson);
    else if (XJSON_TokenIsItem(pToken))
        { XASSERT(XJSON_PutItem(pJson, pObj, NULL), XJSON_FAILURE); }
    else if (pToken->nType == XJSON_TOKEN_LCURLY)
        { XASSERT(XJSON_ParseNewObject(pJson, pObj, NULL), XJSON_FAILURE); }
    else if (pToken->nType == XJSON_TOKEN_LSQUARE)
        { XASSERT(XJSON_ParseNewArray(pJson, pObj, NULL), XJSON_FAILURE); }
    else return XJSON_UnexpectedToken(pJson);

    XASSERT(XJSON_GetNextToken(pJson), XJSON_FAILURE);

    if (pToken->nType == XJSON_TOKEN_COMMA)
        return XJSON_ParseArray(pJson, pObj);
    else if (pToken->nType != XJSON_TOKEN_RSQUARE)
        return XJSON_UnexpectedToken(pJson);

    return XJSON_UndoLastToken(pJson);
}

static int XJSON_ParsePair(xjson_t* pJson, xjson_obj_t* pObj)
{
    xjson_token_t* pToken = &pJson->lastToken;
    size_t nSize = pToken->nLength + 1;
    xpool_t* pPool = pJson->pPool;

    char* pPairName = (char*)xalloc(pPool, nSize);
    if (pPairName == NULL)
    {
        pJson->nError = XJSON_ERR_ALLOC;
        return XJSON_FAILURE;
    }

    xstrncpys(pPairName, nSize, pToken->pData, pToken->nLength);

    if (!XJSON_Expect(pJson, XJSON_TOKEN_COLON) ||
        !XJSON_GetNextToken(pJson))
    {
        xfree(pPool, pPairName);
        return XJSON_FAILURE;
    }

    if (XJSON_TokenIsItem(pToken))
    {
        if (!XJSON_PutItem(pJson, pObj, pPairName))
        {
            xfree(pPool, pPairName);
            return XJSON_FAILURE;
        }
    }
    else if (pToken->nType == XJSON_TOKEN_LCURLY)
    {
        if (!XJSON_ParseNewObject(pJson, pObj, pPairName))
        {
            xfree(pPool, pPairName);
            return XJSON_FAILURE;
        }
    }
    else if (pToken->nType == XJSON_TOKEN_LSQUARE)
    {
        if (!XJSON_ParseNewArray(pJson, pObj, pPairName))
        {
            xfree(pPool, pPairName);
            return XJSON_FAILURE;
        }
    }
    else
    {
        xfree(pPool, pPairName);
        return XJSON_UnexpectedToken(pJson);
    }

    xfree(pPool, pPairName);
    XASSERT(XJSON_GetNextToken(pJson), XJSON_FAILURE);

    if (pToken->nType == XJSON_TOKEN_COMMA)
        return XJSON_ParseObject(pJson, pObj);
    else if (pToken->nType != XJSON_TOKEN_RCURLY)
        return XJSON_UnexpectedToken(pJson);

    return XJSON_UndoLastToken(pJson);
}

int XJSON_ParseObject(xjson_t *pJson, xjson_obj_t *pObj)
{
    xjson_token_t *pToken = &pJson->lastToken;
    XASSERT(XJSON_GetNextToken(pJson), XJSON_FAILURE);

    if (pToken->nType == XJSON_TOKEN_RCURLY) return XJSON_UndoLastToken(pJson);
    else if (pToken->nType == XJSON_TOKEN_QUOTE) return XJSON_ParsePair(pJson, pObj);
    else if (pToken->nType == XJSON_TOKEN_COMMA) return XJSON_ParseObject(pJson, pObj);
    else if (pToken->nType == XJSON_TOKEN_EOF) return XJSON_FAILURE;

    return XJSON_UnexpectedToken(pJson);
}

int XJSON_Parse(xjson_t *pJson, xpool_t *pPool, const char *pData, size_t nSize)
{
    pJson->nError = XJSON_ERR_NONE;
    pJson->nDataSize = nSize;
    pJson->pData = pData;
    pJson->nOffset = 0;
    pJson->pPool = pPool;

    xjson_token_t *pToken = &pJson->lastToken;
    XASSERT(XJSON_GetNextToken(pJson), XJSON_FAILURE);

    if (pToken->nType == XJSON_TOKEN_LCURLY)
    {
        pJson->pRootObj = XJSON_NewObject(pPool, NULL, 0);
        if (pJson->pRootObj == NULL)
        {
            pJson->nError = XJSON_ERR_ALLOC;
            return XJSON_FAILURE;
        }

        XASSERT(XJSON_ParseObject(pJson, pJson->pRootObj), XJSON_FAILURE);
        return XJSON_Expect(pJson, XJSON_TOKEN_RCURLY);
    }
    else if (pToken->nType == XJSON_TOKEN_LSQUARE)
    {
        pJson->pRootObj = XJSON_NewArray(pPool, NULL, 0);
        if (pJson->pRootObj == NULL)
        {
            pJson->nError = XJSON_ERR_ALLOC;
            return XJSON_FAILURE;
        }

        XASSERT(XJSON_ParseArray(pJson, pJson->pRootObj), XJSON_FAILURE);
        return XJSON_Expect(pJson, XJSON_TOKEN_RSQUARE);
    }

    return XJSON_UnexpectedToken(pJson);
}

xjson_obj_t *XJSON_FromStr(xpool_t *pPool, const char *pFmt, ...)
{
    size_t nSize = 0;
    va_list args;

    va_start(args, pFmt);
    char *pJson = xstrpcpyargs(pPool, pFmt, args, &nSize);
    va_end(args);

    XASSERT(pJson, NULL);
    xjson_t json;

    int nStatus = XJSON_Parse(&json, pPool, pJson, nSize);
    xfree(pPool, pJson);

    XASSERT_CALL((nStatus == XJSON_SUCCESS),
                XJSON_Destroy, &json, NULL);

    return json.pRootObj;
}

void XJSON_Init(xjson_t *pJson)
{
    pJson->pRootObj = NULL;
    pJson->pData = NULL;

    pJson->nError = XJSON_ERR_NONE;
    pJson->nDataSize = 0;
    pJson->nOffset = 0;

    pJson->lastToken.nType = XJSON_TOKEN_INVALID;
    pJson->lastToken.pData = NULL;
    pJson->lastToken.nLength = 0;
}

void XJSON_Destroy(xjson_t *pJson)
{
    if (pJson == NULL) return;
    XJSON_FreeObject(pJson->pRootObj);
    pJson->pRootObj = NULL;
    pJson->nDataSize = 0;
    pJson->nOffset = 0;
    pJson->pData = NULL;
}

static int XJSON_CollectIt(xmap_pair_t *pPair, void *pContext)
{
    xarray_t *pArray = (xarray_t*)pContext;
    int nStatus = XArray_AddData(pArray, pPair, XSTDNON);
    return nStatus < 0 ? XMAP_STOP : XMAP_OK;
}

xarray_t* XJSON_GetObjects(xjson_obj_t *pObj)
{
    if (!XJSON_CheckObject(pObj, XJSON_TYPE_OBJECT)) return NULL;
    xmap_t *pMap = (xmap_t*)pObj->pData;

    xarray_t *pArray = XArray_New(pObj->pPool, XSTDNON, XFALSE);
    XASSERT(pArray, NULL);

    if (XMap_Iterate(pMap, XJSON_CollectIt, pArray) != XMAP_OK)
    {
        XArray_Destroy(pArray);
        return NULL;
    }

    return pArray;
}

xjson_obj_t* XJSON_GetObject(xjson_obj_t *pObj, const char *pName)
{
    if (!XJSON_CheckObject(pObj, XJSON_TYPE_OBJECT)) return NULL;
    return XMap_Get((xmap_t*)pObj->pData, pName);
}

xjson_obj_t *XJSON_GetOrCreateObject(xjson_obj_t *pObj, const char *pName, uint8_t nAllowUpdate)
{
    xjson_obj_t *pChild = XJSON_GetObject(pObj, pName);
    if (pChild != NULL)
    {
        pChild->nAllowUpdate = nAllowUpdate;
        return pChild;
    }

    pChild = XJSON_NewObject(pObj->pPool, pName, nAllowUpdate);
    if (pChild == NULL) return NULL;

    if (XJSON_AddObject(pObj, pChild) != XJSON_ERR_NONE)
    {
        if (pChild != NULL)
        {
            XJSON_FreeObject(pChild);
            pChild = NULL;
        }
    }

    return pChild;
}

xjson_obj_t *XJSON_GetOrCreateArray(xjson_obj_t *pObj, const char *pName, uint8_t nAllowUpdate)
{
    xjson_obj_t *pChild = XJSON_GetObject(pObj, pName);
    if (pChild != NULL)
    {
        pChild->nAllowUpdate = nAllowUpdate;
        return pChild;
    }

    pChild = XJSON_NewArray(pObj->pPool, pName, nAllowUpdate);
    if (pChild == NULL) return NULL;

    if (XJSON_AddObject(pObj, pChild) != XJSON_ERR_NONE)
    {
        if (pChild != NULL)
        {
            XJSON_FreeObject(pChild);
            pChild = NULL;
        }
    }

    return pChild;
}

xjson_obj_t* XJSON_GetArrayItem(xjson_obj_t *pObj, size_t nIndex)
{
    if (!XJSON_CheckObject(pObj, XJSON_TYPE_ARRAY)) return NULL;
    return XArray_GetData((xarray_t*)pObj->pData, nIndex);
}

int XJSON_RemoveArrayItem(xjson_obj_t *pObj, size_t nIndex)
{
    xjson_obj_t *pTmp = XJSON_GetArrayItem(pObj, nIndex);
    if (pTmp != NULL)
    {
        XArray_Remove((xarray_t*)pObj->pData, nIndex);
        XJSON_FreeObject(pTmp);
    }
    return 0;
}

size_t XJSON_GetArrayLength(xjson_obj_t *pObj)
{
    if (!XJSON_CheckObject(pObj, XJSON_TYPE_ARRAY)) return 0;
    return XArray_Used((xarray_t*)pObj->pData);
}

int XJSON_GetInt(xjson_obj_t *pObj)
{
    if (!XJSON_CheckObject(pObj, XJSON_TYPE_NUMBER)) return 0;
    return atoi((const char*)pObj->pData);
}

double XJSON_GetFloat(xjson_obj_t *pObj)
{
    if (!XJSON_CheckObject(pObj, XJSON_TYPE_FLOAT)) return 0.;
    return atof((const char*)pObj->pData);
}

uint32_t XJSON_GetU32(xjson_obj_t *pObj)
{
    if (!XJSON_CheckObject(pObj, XJSON_TYPE_NUMBER)) return 0;
    return atol((const char*)pObj->pData);
}

uint64_t XJSON_GetU64(xjson_obj_t *pObj)
{
    if (!XJSON_CheckObject(pObj, XJSON_TYPE_NUMBER)) return 0;
    return strtoull((const char*)pObj->pData, NULL, 0);
}

uint8_t XJSON_GetBool(xjson_obj_t *pObj)
{
    if (!XJSON_CheckObject(pObj, XJSON_TYPE_BOOLEAN)) return 0;
    return !strncmp((const char*)pObj->pData, "true", 4);
}

const char* XJSON_GetString(xjson_obj_t *pObj)
{
    if (!XJSON_CheckObject(pObj, XJSON_TYPE_STRING)) return "";
    return (const char*)pObj->pData;
}

static int XJSON_Realloc(xjson_writer_t *pWriter, size_t nSize)
{
    if (nSize < pWriter->nAvail) return XJSON_SUCCESS;
    else if (!pWriter->nAlloc) return XJSON_FAILURE;

    size_t nNewSize = pWriter->nSize + nSize;
    size_t nOldSize = pWriter->nSize;
    xpool_t *pPool = pWriter->pPool;

    char *pNewData = xrealloc(pPool, pWriter->pData, nOldSize, nNewSize);
    if (pNewData == NULL) return XJSON_FAILURE;

    pWriter->nAvail += nSize;
    pWriter->nSize = nNewSize;
    pWriter->pData = pNewData;

    return XJSON_SUCCESS;
}

static int XJSON_AppedSpaces(xjson_writer_t *pWriter)
{
    if (!pWriter->nTabSize) return XJSON_SUCCESS;
    xpool_t *pPool = pWriter->pPool;

    char *pSpaces = (char*)xalloc(pPool, pWriter->nIndents + 1);
    if (pSpaces == NULL) return XJSON_FAILURE;

    size_t nLenght = 0;
    while (nLenght < pWriter->nIndents)
        pSpaces[nLenght++] = ' ';

    pSpaces[nLenght] = '\0';
    if (!XJSON_Realloc(pWriter, nLenght))
    {
        xfree(pPool, pSpaces);
        return XJSON_FAILURE;
    }

    char *pOffset = &pWriter->pData[pWriter->nLength];
    nLenght = xstrncpyf(pOffset, pWriter->nAvail, "%s", pSpaces);
    xfree(pPool, pSpaces);

    pWriter->nLength += nLenght;
    pWriter->nAvail -= nLenght;
    return XJSON_SUCCESS;
}

static int XJSON_WriteString(xjson_writer_t *pWriter, int nIndent, const char *pFmt, ...)
{
    if (nIndent) XASSERT(XJSON_AppedSpaces(pWriter), XJSON_FAILURE);
    xpool_t *pPool = pWriter->pPool;

    char *pBuffer = NULL;
    size_t nBytes = 0;

    va_list args;
    va_start(args, pFmt);
    pBuffer = xstrpcpyargs(pPool, pFmt, args, &nBytes);
    va_end(args);

    if (!nBytes || !XJSON_Realloc(pWriter, nBytes))
    {
        xfree(pPool, pBuffer);
        return XJSON_FAILURE;
    }

    size_t nAvail = pWriter->nSize - pWriter->nLength;
    size_t nWrited = xstrncpys(&pWriter->pData[pWriter->nLength], nAvail, pBuffer, nBytes);

    pWriter->nLength += nWrited;
    pWriter->nAvail -= nWrited;
    pWriter->pData[pWriter->nLength] = '\0';

    xfree(pPool, pBuffer);
    return XJSON_SUCCESS;
}

static int XJSON_WriteName(xjson_obj_t *pObj, xjson_writer_t *pWriter)
{
    if (pObj->pName == NULL) return XJSON_SUCCESS;

    if (pWriter->nPretty)
    {
        xjson_format_t *pFormat = &pWriter->format;

        return XJSON_WriteString(pWriter, 1, "\"%s%s%s%s\":%s",
            pFormat->pNameFmt, pFormat->pNameClr, pObj->pName, XSTR_FMT_RESET,
            pWriter->nTabSize ? XSTR_SPACE : XSTR_EMPTY);
    }

    return XJSON_WriteString(pWriter, 1, "\"%s\":%s",
        pObj->pName, pWriter->nTabSize ? XSTR_SPACE : XSTR_EMPTY);
}

static int XJSON_WriteItem(xjson_obj_t *pObj, xjson_writer_t *pWriter)
{
    if (pObj->nType == XJSON_TYPE_INVALID ||
        pObj->nType == XJSON_TYPE_OBJECT ||
        pObj->nType == XJSON_TYPE_ARRAY)
            return XJSON_FAILURE;

    XASSERT(pObj->pData, XJSON_FAILURE);
    XASSERT(XJSON_WriteName(pObj, pWriter), XJSON_FAILURE);
    int nIndent = (pObj->pName == NULL && pObj->nAllowLinter) ? 1 : 0;

    if (pWriter->nPretty)
    {
        xjson_format_t *pFormat = &pWriter->format;

        if (pObj->nType == XJSON_TYPE_STRING)
        {
            return XJSON_WriteString(pWriter, nIndent, "\"%s%s%s%s\"",
                pFormat->pStrFmt, pFormat->pStrClr,
                (const char*)pObj->pData, XSTR_FMT_RESET);
        }
        else if (pObj->nType == XJSON_TYPE_BOOLEAN)
        {
            return XJSON_WriteString(pWriter, nIndent, "%s%s%s%s",
                pFormat->pBoolFmt, pFormat->pBoolClr,
                (const char*)pObj->pData, XSTR_FMT_RESET);
        }
        else if (pObj->nType == XJSON_TYPE_NUMBER)
        {
            return XJSON_WriteString(pWriter, nIndent, "%s%s%s%s",
                pFormat->pNumFmt, pFormat->pNumClr,
                (const char*)pObj->pData, XSTR_FMT_RESET);
        }
        else if (pObj->nType == XJSON_TYPE_FLOAT)
        {
            return XJSON_WriteString(pWriter, nIndent, "%s%s%s%s",
                pFormat->pFloatFmt, pFormat->pFloatClr,
                (const char*)pObj->pData, XSTR_FMT_RESET);
        }
        else if (pObj->nType == XJSON_TYPE_NULL)
        {
            return XJSON_WriteString(pWriter, nIndent, "%s%s%s%s",
                pFormat->pNullFmt, pFormat->pNullClr,
                (const char*)pObj->pData, XSTR_FMT_RESET);
        }
    }

    return (pObj->nType == XJSON_TYPE_STRING) ?
        XJSON_WriteString(pWriter, nIndent, "\"%s\"", (const char*)pObj->pData):
        XJSON_WriteString(pWriter, nIndent, "%s", (const char*)pObj->pData);
}

static int XJSON_MapIt(xmap_pair_t *pPair, void *pContext)
{
    xjson_iterator_t *pIt = (xjson_iterator_t*)pContext;
    xjson_obj_t *pItem = (xjson_obj_t *)pPair->pData;

    return (!XJSON_WriteObject(pItem, pIt->pWriter) ||
            (++pIt->nCurrent < pIt->nUsed &&
            !XJSON_WriteString(pIt->pWriter, 0, ",")) ||
            (pIt->pWriter->nTabSize &&
            !XJSON_WriteString(pIt->pWriter, 0, "\n"))) ?
                XMAP_STOP : XMAP_OK;
}

static int XJSON_Ident(xjson_writer_t *pWriter, int nIncrease)
{
    if (pWriter->nTabSize)
    {
        if (nIncrease) pWriter->nIndents += pWriter->nTabSize;
        else if (pWriter->nTabSize <= pWriter->nIndents)
            pWriter->nIndents -= pWriter->nTabSize;
        else return XJSON_FAILURE;
    }

    return XJSON_SUCCESS;
}

static int XJSON_WriteHashmap(xjson_obj_t *pObj, xjson_writer_t *pWriter)
{
    XASSERT(XJSON_CheckObject(pObj, XJSON_TYPE_OBJECT), XJSON_FAILURE);
    XASSERT(XJSON_WriteName(pObj, pWriter), XJSON_FAILURE);
    int nIndent = (pObj->pName == NULL && pObj->nAllowLinter) ? 1 : 0;
    xmap_t *pMap = (xmap_t*)pObj->pData;

    XASSERT(XJSON_WriteString(pWriter, nIndent, "{"), XJSON_FAILURE);
    nIndent = (pWriter->nTabSize && pMap->nCount && pObj->nAllowLinter) ? 1 : 0;

    if (nIndent)
    {
        XASSERT(XJSON_WriteString(pWriter, 0, "\n"), XJSON_FAILURE);
        XASSERT(XJSON_Ident(pWriter, XJSON_IDENT_INC), XJSON_FAILURE);
    }

    if (pMap->nCount)
    {
        xpool_t *pPool = pWriter->pPool;
        xjson_iterator_t *pIterator = (xjson_iterator_t*)xalloc(pPool, sizeof(xjson_iterator_t));
        if (pIterator == NULL) return XJSON_FAILURE;

        pIterator->nCurrent = 0;
        pIterator->pWriter = pWriter;
        pIterator->nUsed = pMap->nCount;

        if (XMap_Iterate(pMap, XJSON_MapIt, pIterator) != XMAP_OK)
        {
            xfree(pPool, pIterator);
            return XJSON_FAILURE;
        }

        xfree(pPool, pIterator);
    }

    if (nIndent) XASSERT(XJSON_Ident(pWriter, XJSON_IDENT_DEC), XJSON_FAILURE);
    return XJSON_WriteString(pWriter, nIndent, "}");
}

static int XJSON_WriteArray(xjson_obj_t *pObj, xjson_writer_t *pWriter)
{
    XASSERT(XJSON_CheckObject(pObj, XJSON_TYPE_ARRAY), XJSON_FAILURE);
    XASSERT(XJSON_WriteName(pObj, pWriter), XJSON_FAILURE);
    int nIndent = (pObj->pName == NULL && pObj->nAllowLinter) ? 1 : 0;

    xarray_t* pArray = (xarray_t*)pObj->pData;
    XASSERT(XJSON_WriteString(pWriter, nIndent, "["), XJSON_FAILURE);

    size_t i, nUsed = XArray_Used(pArray);
    nIndent = (pWriter->nTabSize && nUsed && pObj->nAllowLinter) ? 1 : 0;

    if (nIndent)
    {
        XASSERT(XJSON_WriteString(pWriter, 0, "\n"), XJSON_FAILURE);
        XASSERT(XJSON_Ident(pWriter, XJSON_IDENT_INC), XJSON_FAILURE);
    }

    for (i = 0; i < nUsed; i++)
    {
        xjson_obj_t *pItem = XArray_GetData(pArray, i);
        if (pItem == NULL) continue;

        pItem->nAllowLinter = pObj->nAllowLinter;
        XASSERT(XJSON_WriteObject(pItem, pWriter), XJSON_FAILURE);
        if ((i + 1) < nUsed) XASSERT(XJSON_WriteString(pWriter, 0, ","), XJSON_FAILURE);

        if (pWriter->nTabSize && pObj->nAllowLinter)
            XASSERT(XJSON_WriteString(pWriter, 0, "\n"), XJSON_FAILURE);
    }

    if (nIndent) XASSERT(XJSON_Ident(pWriter, XJSON_IDENT_DEC), XJSON_FAILURE);
    return XJSON_WriteString(pWriter, nIndent, "]");
}

int XJSON_WriteObject(xjson_obj_t *pObj, xjson_writer_t *pWriter)
{
    if (pObj == NULL || pWriter == NULL) return XJSON_FAILURE;

    switch (pObj->nType)
    {
        case XJSON_TYPE_ARRAY:
            return XJSON_WriteArray(pObj, pWriter);
        case XJSON_TYPE_OBJECT:
            return XJSON_WriteHashmap(pObj, pWriter);
        case XJSON_TYPE_BOOLEAN:
        case XJSON_TYPE_NUMBER:
        case XJSON_TYPE_STRING:
        case XJSON_TYPE_FLOAT:
        case XJSON_TYPE_NULL:
            return XJSON_WriteItem(pObj, pWriter);
        case XJSON_TYPE_INVALID:
        default: break;
    }

    return XJSON_FAILURE;
}

static void XJSON_FormatCopy(xjson_format_t *pDst, xjson_format_t *pSrc)
{
    if (pDst == NULL || pSrc == NULL) return;
    pDst->pNameFmt = pSrc->pNameFmt;
    pDst->pNameClr = pSrc->pNameClr;

    pDst->pBoolFmt = pSrc->pBoolFmt;
    pDst->pBoolClr = pSrc->pBoolClr;

    pDst->pStrFmt = pSrc->pStrFmt;
    pDst->pStrClr = pSrc->pStrClr;

    pDst->pNumFmt = pSrc->pNumFmt;
    pDst->pNumClr = pSrc->pNumClr;

    pDst->pFloatFmt = pSrc->pFloatFmt;
    pDst->pFloatClr = pSrc->pFloatClr;

    pDst->pNullFmt = pSrc->pNullFmt;
    pDst->pNullClr = pSrc->pNullClr;
}

void XJSON_FormatInit(xjson_format_t *pFormat)
{
    pFormat->pNameFmt = XSTR_FMT_DIM;
    pFormat->pNameClr = XSTR_CLR_LIGHT_MAGENTA;

    pFormat->pBoolFmt = XSTR_EMPTY;
    pFormat->pBoolClr = XSTR_CLR_CYAN;

    pFormat->pStrFmt = XSTR_FMT_DIM;
    pFormat->pStrClr = XSTR_CLR_YELLOW;

    pFormat->pNumFmt = XSTR_EMPTY;
    pFormat->pNumClr = XSTR_CLR_BLUE;

    pFormat->pFloatFmt = XSTR_EMPTY;
    pFormat->pFloatClr = XSTR_CLR_BLUE;

    pFormat->pNullFmt = XSTR_EMPTY;
    pFormat->pNullClr = XSTR_CLR_RED;
}

int XJSON_InitWriter(xjson_writer_t *pWriter, xpool_t *pPool, char *pOutput, size_t nSize)
{
    XJSON_FormatInit(&pWriter->format);
    pWriter->nAvail = nSize;
    pWriter->pData = pOutput;
    pWriter->nSize = nSize;
    pWriter->pPool = pPool;
    pWriter->nAlloc = 0;

    if (pWriter->pData == NULL && pWriter->nSize)
    {
        pWriter->pData = xalloc(pPool, pWriter->nSize);
        if (pWriter->pData == NULL) return 0;
        pWriter->nAlloc = 1;
    }

    if (pWriter->pData != NULL)
        pWriter->pData[0] = '\0';

    pWriter->nTabSize = 0;
    pWriter->nPretty = 0;
    pWriter->nIndents = 0;
    pWriter->nLength = 0;

    return 1;
}

void XJSON_DestroyWriter(xjson_writer_t *pWriter)
{
    if (pWriter && pWriter->nAlloc)
    {
        xpool_t *pPool = pWriter->pPool;
        xfree(pPool, pWriter->pData);
        pWriter->pData = NULL;
        pWriter->nAlloc = 0;
    }
}

int XJSON_Write(xjson_t *pJson, char *pOutput, size_t nSize)
{
    if (pJson == NULL ||
        pOutput == NULL ||
        !nSize) return XJSON_FAILURE;

    xpool_t *pPool = pJson->pPool;
    xjson_writer_t writer;

    XASSERT(XJSON_InitWriter(&writer, pPool, NULL, 1), XJSON_FAILURE);
    int nStatus = XJSON_WriteObject(pJson->pRootObj, &writer);
    if (nStatus == XJSON_SUCCESS) xstrncpy(pOutput, nSize, writer.pData);
    return nStatus;
}

char* XJSON_FormatObj(xjson_obj_t *pJsonObj, size_t nTabSize, xjson_format_t *pFormat, size_t *pLength)
{
    if (pLength) *pLength = 0;
    XASSERT(pJsonObj, NULL);

    xpool_t *pPool = pJsonObj->pPool;
    xjson_writer_t writer;

    XASSERT(XJSON_InitWriter(&writer, pPool, NULL, 1), NULL);
    XJSON_FormatCopy(&writer.format, pFormat);

    writer.nTabSize = nTabSize;
    writer.nPretty = 1;

    if (!XJSON_WriteObject(pJsonObj, &writer))
    {
        XJSON_DestroyWriter(&writer);
        return NULL;
    }

    if (pLength) *pLength = writer.nLength;
    return writer.pData;
}

char* XJSON_Format(xjson_t *pJson, size_t nTabSize, xjson_format_t *pFormat, size_t *pLength)
{
    XASSERT(pJson, NULL);
    return XJSON_FormatObj(pJson->pRootObj, nTabSize, pFormat, pLength);
}

char* XJSON_DumpObj(xjson_obj_t *pJsonObj, size_t nTabSize, size_t *pLength)
{
    if (pLength) *pLength = 0;
    XASSERT(pJsonObj, NULL);

    xpool_t *pPool = pJsonObj->pPool;
    xjson_writer_t writer;

    XASSERT(XJSON_InitWriter(&writer, pPool, NULL, 1), NULL);
    writer.nTabSize = nTabSize;
    writer.nPretty = 0;

    if (!XJSON_WriteObject(pJsonObj, &writer))
    {
        XJSON_DestroyWriter(&writer);
        return NULL;
    }

    if (pLength) *pLength = writer.nLength;
    return writer.pData;
}

char* XJSON_Dump(xjson_t *pJson, size_t nTabSize, size_t *pLength)
{
    XASSERT(pJson, NULL);
    return XJSON_DumpObj(pJson->pRootObj, nTabSize, pLength);
}
