/*!
 *  @file libxutils/src/xjson.c
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of the lexical analyzer 
 * and recursive descent parser with JSON grammar
 */

#ifdef _XUTILS_USE_GNU
#define _GNU_SOURCE
#endif

#include "xstd.h"
#include "xmap.h"
#include "xstr.h"
#include "array.h"
#include "xjson.h"

#define XOBJ_INITIAL_SIZE   2
#define XJSON_IDENT_INC     1
#define XJSON_IDENT_DEC     0

#define XJSON_NUMBER_MAX    64
#define XJSON_BOOL_MAX      6
#define XJSON_NULL_MAX      5

#define XJSON_ASSERT(condition) \
    if (!condition) return XJSON_FAILURE

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
    XJSON_ASSERT(XJSON_CheckBounds(pJson));
    char nCharacter = pJson->pData[pJson->nOffset++];

    /* Skip space and new line characters */
    while (nCharacter == ' ' || 
           nCharacter == '\n' || 
           nCharacter == '\t')
    {
        XJSON_ASSERT(XJSON_CheckBounds(pJson));
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
        XJSON_ASSERT(XJSON_CheckBounds(pJson));
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
    XJSON_ASSERT(XJSON_CheckBounds(pJson));

    size_t nStart = pJson->nOffset;
    char nCurr = 0, nPrev = 0;

    for (;;)
    {
        if (nCurr == '"' && nPrev != '\\') break;
        XJSON_ASSERT(XJSON_CheckBounds(pJson));

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
        XJSON_ASSERT(XJSON_CheckBounds(pJson));
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

    switch (nChar)
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
    XJSON_ASSERT(XJSON_GetNextToken(pJson));
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
        if (pObj->pData != NULL)
        {
            if (pObj->nType == XJSON_TYPE_OBJECT)
                XMap_Destroy((xmap_t*)pObj->pData);
            else if (pObj->nType == XJSON_TYPE_ARRAY)
                XArray_Destroy((xarray_t*)pObj->pData);
            else free(pObj->pData);
        }

        if (pObj->pName != NULL) free(pObj->pName);
        if (pObj->nAllocated) free(pObj);
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

xjson_obj_t* XJSON_CreateObject(const char *pName, void *pValue, xjson_type_t nType)
{
    xjson_obj_t *pObj = (xjson_obj_t*)malloc(sizeof(xjson_obj_t));
    if (pObj == NULL) return NULL;

    pObj->nAllowUpdate = 0;
    pObj->nAllocated = 1;
    pObj->pName = NULL;
    pObj->pData = pValue;
    pObj->nType = nType;

    if (pName == NULL) return pObj;
    size_t nLength = strlen(pName);

    if (nLength > 0)
    {
        pObj->pName = (char*)malloc(nLength + 1);
        if (pObj->pName == NULL)
        {
            free(pObj);
            return NULL;
        }

        memcpy(pObj->pName, pName, nLength);
        pObj->pName[nLength] = '\0';
    }

    return pObj;
}

xjson_obj_t* XJSON_NewObject(const char *pName, uint8_t nAllowUpdate)
{
    xmap_t *pMap = XMap_New(XOBJ_INITIAL_SIZE);
    if (pMap == NULL) return NULL;

    pMap->clearCb = XJSON_ObjectClearCb;
    xjson_obj_t *pObj = XJSON_CreateObject(pName, pMap, XJSON_TYPE_OBJECT);

    if (pObj == NULL)
    {
        XMap_Destroy(pMap);
        return NULL;
    }

    pObj->nAllowUpdate = nAllowUpdate;
    return pObj;
}

xjson_obj_t* XJSON_NewArray(const char *pName, uint8_t nAllowUpdate)
{
    xarray_t *pArray = XArray_New(XOBJ_INITIAL_SIZE, 0);
    if (pArray == NULL) return NULL;

    pArray->clearCb = XJSON_ArrayClearCb;
    xjson_obj_t *pObj = XJSON_CreateObject(pName, pArray, XJSON_TYPE_ARRAY);

    if (pObj == NULL)
    {
        XArray_Destroy(pArray);
        return NULL;
    }

    pObj->nAllowUpdate = nAllowUpdate;
    return pObj;
}

xjson_obj_t* XJSON_NewU64(const char *pName, uint64_t nValue)
{
    char *pValue = (char*)malloc(XJSON_NUMBER_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_NUMBER_MAX, "%"PRIu64, nValue);
    xjson_obj_t *pObj = XJSON_CreateObject(pName, pValue, XJSON_TYPE_NUMBER);

    if (pObj == NULL)
    {
        free(pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddU64(xjson_obj_t *pObject, const char *pName, uint64_t nValue)
{
    xjson_obj_t *pNewObj = XJSON_NewU64(pName, nValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;
    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewU32(const char *pName, uint32_t nValue)
{
    char *pValue = (char*)malloc(XJSON_NUMBER_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_NUMBER_MAX, "%u", nValue);
    xjson_obj_t *pObj = XJSON_CreateObject(pName, pValue, XJSON_TYPE_NUMBER);

    if (pObj == NULL)
    {
        free(pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddU32(xjson_obj_t *pObject, const char *pName, uint32_t nValue)
{
    xjson_obj_t *pNewObj = XJSON_NewU32(pName, nValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;
    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewInt(const char *pName, int nValue)
{
    char *pValue = (char*)malloc(XJSON_NUMBER_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_NUMBER_MAX, "%d", nValue);
    xjson_obj_t *pObj = XJSON_CreateObject(pName, pValue, XJSON_TYPE_NUMBER);

    if (pObj == NULL)
    {
        free(pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddInt(xjson_obj_t *pObject, const char *pName, int nValue)
{
    xjson_obj_t *pNewObj = XJSON_NewInt(pName, nValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;
    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewFloat(const char *pName, double fValue)
{
    char *pValue = (char*)malloc(XJSON_NUMBER_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_NUMBER_MAX, "%lf", fValue);
    xjson_obj_t *pObj = XJSON_CreateObject(pName, pValue, XJSON_TYPE_FLOAT);

    if (pObj == NULL)
    {
        free(pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddFloat(xjson_obj_t *pObject, const char *pName, double fValue)
{
    xjson_obj_t *pNewObj = XJSON_NewFloat(pName, fValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;
    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewString(const char *pName, const char *pValue)
{
    char *pSaveValue = xstrdup(pValue);
    if (pSaveValue == NULL) return NULL;

    xjson_obj_t *pObj = XJSON_CreateObject(pName, pSaveValue, XJSON_TYPE_STRING);
    if (pObj == NULL)
    {
        free(pSaveValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddString(xjson_obj_t *pObject, const char *pName, const char *pValue)
{
    xjson_obj_t *pNewObj = XJSON_NewString(pName, pValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;
    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewBool(const char *pName, int nValue)
{
    char *pValue = (char*)malloc(XJSON_BOOL_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_BOOL_MAX, "%s", nValue ? "true" : "false");
    xjson_obj_t *pObj = XJSON_CreateObject(pName, pValue, XJSON_TYPE_BOOLEAN);

    if (pObj == NULL)
    {
        free(pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddBool(xjson_obj_t *pObject, const char *pName, int nValue)
{
    xjson_obj_t *pNewObj = XJSON_NewBool(pName, nValue);
    if (pNewObj == NULL) return XJSON_ERR_ALLOC;
    xjson_error_t status = XJSON_AddObject(pObject, pNewObj);
    if (status != XJSON_ERR_NONE) XJSON_FreeObject(pNewObj);
    return status;
}

xjson_obj_t* XJSON_NewNull(const char *pName)
{
    char *pValue = (char*)malloc(XJSON_NULL_MAX);
    if (pValue == NULL) return NULL;

    xstrncpyf(pValue, XJSON_NULL_MAX, "%s", "null");
    xjson_obj_t *pObj = XJSON_CreateObject(pName, pValue, XJSON_TYPE_NULL);

    if (pObj == NULL)
    {
        free(pValue);
        return NULL;
    }

    return pObj;
}

xjson_error_t XJSON_AddNull(xjson_obj_t *pObject, const char *pName)
{
    xjson_obj_t *pNewObj = XJSON_NewNull(pName);
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
    xjson_obj_t *pNewObj = XJSON_NewObject(pName, 0);
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
    xjson_obj_t *pNewObj = XJSON_NewArray(pName, 0);
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
    char *pValue = malloc(pToken->nLength + 1);

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
    xjson_token_t *pToken = &pJson->lastToken;
    xjson_type_t nType = XJSON_GetItemType(pToken->nType);

    if (nType == XJSON_TYPE_INVALID)
    {
        pJson->nError = XJSON_ERR_INVALID;
        return XJSON_FAILURE;
    }

    char *pValue = JSON_LastTokenValue(pJson);
    if (pValue == NULL) return XJSON_FAILURE;

    xjson_obj_t *pNewObj = XJSON_CreateObject(pName, pValue, nType);
    if (pNewObj == NULL)
    {
        free(pValue);
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
    XJSON_ASSERT(XJSON_GetNextToken(pJson));

    if (pToken->nType == XJSON_TOKEN_RSQUARE)
        return XJSON_UndoLastToken(pJson);
    else if (XJSON_TokenIsItem(pToken))
        { XJSON_ASSERT(XJSON_PutItem(pJson, pObj, NULL)); }
    else if (pToken->nType == XJSON_TOKEN_LCURLY)
        { XJSON_ASSERT(XJSON_ParseNewObject(pJson, pObj, NULL)); }
    else if (pToken->nType == XJSON_TOKEN_LSQUARE)
        { XJSON_ASSERT(XJSON_ParseNewArray(pJson, pObj, NULL)); }
    else return XJSON_UnexpectedToken(pJson);

    XJSON_ASSERT(XJSON_GetNextToken(pJson));

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

    char* pPairName = (char*)malloc(nSize);
    if (pPairName == NULL)
    {
        pJson->nError = XJSON_ERR_ALLOC;
        return XJSON_FAILURE;
    }

    xstrncpys(pPairName, nSize, pToken->pData, pToken->nLength);

    if (!XJSON_Expect(pJson, XJSON_TOKEN_COLON) ||
        !XJSON_GetNextToken(pJson))
    {
        free(pPairName);
        return XJSON_FAILURE;
    }

    if (XJSON_TokenIsItem(pToken))
    {
        if (!XJSON_PutItem(pJson, pObj, pPairName))
        {
            free(pPairName);
            return XJSON_FAILURE;
        }
    }
    else if (pToken->nType == XJSON_TOKEN_LCURLY)
    {
        if (!XJSON_ParseNewObject(pJson, pObj, pPairName))
        {
            free(pPairName);
            return XJSON_FAILURE;
        }
    }
    else if (pToken->nType == XJSON_TOKEN_LSQUARE)
    {
        if (!XJSON_ParseNewArray(pJson, pObj, pPairName))
        {
            free(pPairName);
            return XJSON_FAILURE;
        }
    }
    else
    {
        free(pPairName);
        return XJSON_UnexpectedToken(pJson);
    }

    free(pPairName);
    XJSON_ASSERT(XJSON_GetNextToken(pJson));

    if (pToken->nType == XJSON_TOKEN_COMMA)
        return XJSON_ParseObject(pJson, pObj);
    else if (pToken->nType != XJSON_TOKEN_RCURLY)
        return XJSON_UnexpectedToken(pJson);

    return XJSON_UndoLastToken(pJson);
}

int XJSON_ParseObject(xjson_t *pJson, xjson_obj_t *pObj)
{
    xjson_token_t *pToken = &pJson->lastToken;
    XJSON_ASSERT(XJSON_GetNextToken(pJson));

    if (pToken->nType == XJSON_TOKEN_RCURLY) return XJSON_UndoLastToken(pJson);
    else if (pToken->nType == XJSON_TOKEN_QUOTE) return XJSON_ParsePair(pJson, pObj);
    else if (pToken->nType == XJSON_TOKEN_COMMA) return XJSON_ParseObject(pJson, pObj);
    else if (pToken->nType == XJSON_TOKEN_EOF) return XJSON_FAILURE;

    return XJSON_UnexpectedToken(pJson);
}

int XJSON_Parse(xjson_t *pJson, const char *pData, size_t nSize)
{
    pJson->nError = XJSON_ERR_NONE;
    pJson->nDataSize = nSize;
    pJson->pData = pData;
    pJson->nOffset = 0;

    xjson_token_t *pToken = &pJson->lastToken;
    XJSON_ASSERT(XJSON_GetNextToken(pJson));

    if (pToken->nType == XJSON_TOKEN_LCURLY)
    {
        pJson->pRootObj = XJSON_NewObject(NULL, 0);
        if (pJson->pRootObj == NULL)
        {
            pJson->nError = XJSON_ERR_ALLOC;
            return XJSON_FAILURE;
        }

        XJSON_ASSERT(XJSON_ParseObject(pJson, pJson->pRootObj));
        return XJSON_Expect(pJson, XJSON_TOKEN_RCURLY);
    }
    else if (pToken->nType == XJSON_TOKEN_LSQUARE)
    {
        pJson->pRootObj = XJSON_NewArray(NULL, 0);
        if (pJson->pRootObj == NULL)
        {
            pJson->nError = XJSON_ERR_ALLOC;
            return XJSON_FAILURE;
        }

        XJSON_ASSERT(XJSON_ParseArray(pJson, pJson->pRootObj));
        return XJSON_Expect(pJson, XJSON_TOKEN_RSQUARE);
    }

    return XJSON_UnexpectedToken(pJson);
}

void XJSON_Destroy(xjson_t *pJson)
{
    XJSON_FreeObject(pJson->pRootObj);
    pJson->nDataSize = 0;
    pJson->nOffset = 0;
    pJson->pData = NULL;
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

    pChild = XJSON_NewObject(pName, nAllowUpdate);
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

    pChild = XJSON_NewArray(pName, nAllowUpdate);
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
    char* pDataOld = pWriter->pData;

    pWriter->pData = realloc(pWriter->pData, nNewSize);
    if (pWriter->pData == NULL)
    {
        pWriter->pData = pDataOld;
        return XJSON_FAILURE;
    }

    pWriter->nAvail += nSize;
    pWriter->nSize = nNewSize;
    return XJSON_SUCCESS;
} 

static int XJSON_AppedSpaces(xjson_writer_t *pWriter)
{
    if (!pWriter->nTabSize) return XJSON_SUCCESS;
    char *pSpaces = (char*)calloc(pWriter->nIdents + 1, sizeof(char));
    if (pSpaces == NULL) return XJSON_FAILURE;

    size_t nLenght = 0;
    while (nLenght < pWriter->nIdents)
        pSpaces[nLenght++] = ' ';

    pSpaces[nLenght] = '\0';
    if (!XJSON_Realloc(pWriter, nLenght))
    {
        free(pSpaces);
        return XJSON_FAILURE;
    }

    char *pOffset = &pWriter->pData[pWriter->nLength];
    nLenght = xstrncpyf(pOffset, pWriter->nAvail, "%s", pSpaces);
    free(pSpaces);

    pWriter->nLength += nLenght;
    pWriter->nAvail -= nLenght;
    return XJSON_SUCCESS;
}

static int XJSON_WriteString(xjson_writer_t *pWriter, int nIdent, const char *pFmt, ...)
{
    if (nIdent) XJSON_ASSERT(XJSON_AppedSpaces(pWriter));

    char *pBuffer = NULL;
    size_t nBytes = 0;

    va_list args;
    va_start(args, pFmt);
#ifdef _XUTILS_USE_GNU
    nBytes += vasprintf(&pBuffer, pFmt, args);
#else
    pBuffer = xstracpyargs(pFmt, args, &nBytes);
#endif
    va_end(args);

    if (!nBytes || !XJSON_Realloc(pWriter, nBytes))
    {
        free(pBuffer);
        return XJSON_FAILURE;
    }

    size_t nAvail = pWriter->nSize - pWriter->nLength;
    size_t nWrited = xstrncpys(&pWriter->pData[pWriter->nLength], nAvail, pBuffer, nBytes);

    pWriter->nLength += nWrited;
    pWriter->nAvail -= nWrited;
    pWriter->pData[pWriter->nLength] = '\0';

    free(pBuffer);
    return XJSON_SUCCESS;
}

static int XJSON_WriteName(xjson_obj_t *pObj, xjson_writer_t *pWriter)
{
    if (pObj->pName == NULL) return XJSON_SUCCESS;
    return XJSON_WriteString(pWriter, 1, "\"%s\":%s", 
        pObj->pName, pWriter->nTabSize ? " " : "");
}

static int XJSON_WriteItem(xjson_obj_t *pObj, xjson_writer_t *pWriter)
{  
    if (pObj->nType == XJSON_TYPE_INVALID ||
        pObj->nType == XJSON_TYPE_OBJECT ||
        pObj->nType == XJSON_TYPE_ARRAY)
            return XJSON_FAILURE;

    XJSON_ASSERT(pObj->pData);
    XJSON_ASSERT(XJSON_WriteName(pObj, pWriter));
    int nIdent = pObj->pName == NULL ? 1 : 0;

    return (pObj->nType == XJSON_TYPE_STRING) ? 
        XJSON_WriteString(pWriter, nIdent, "\"%s\"", (const char*)pObj->pData):
        XJSON_WriteString(pWriter, nIdent, "%s", (const char*)pObj->pData);
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
        if (nIncrease) pWriter->nIdents += pWriter->nTabSize;
        else if (pWriter->nTabSize <= pWriter->nIdents) 
            pWriter->nIdents -= pWriter->nTabSize;
        else return XJSON_FAILURE;
    }

    return XJSON_SUCCESS;
}

static int XJSON_WriteHashmap(xjson_obj_t *pObj, xjson_writer_t *pWriter)
{
    XJSON_ASSERT(XJSON_CheckObject(pObj, XJSON_TYPE_OBJECT));
    XJSON_ASSERT(XJSON_WriteName(pObj, pWriter));
    int nIdent = pObj->pName == NULL ? 1 : 0;
    xmap_t *pMap = (xmap_t*)pObj->pData;

    XJSON_ASSERT(XJSON_WriteString(pWriter, nIdent, "{"));
    nIdent = (pWriter->nTabSize && pMap->nUsed) ? 1 : 0;

    if (nIdent)
    {
        XJSON_ASSERT(XJSON_WriteString(pWriter, 0, "\n"));
        XJSON_ASSERT(XJSON_Ident(pWriter, XJSON_IDENT_INC));
    }

    if (pMap->nUsed)
    {
        xjson_iterator_t *pIterator = (xjson_iterator_t*)malloc(sizeof(xjson_iterator_t));
        if (pIterator == NULL) return XJSON_FAILURE;

        pIterator->nCurrent = 0;
        pIterator->pWriter = pWriter;
        pIterator->nUsed = pMap->nUsed;

        if (XMap_Iterate(pMap, XJSON_MapIt, pIterator) != XMAP_OK)
        {
            free(pIterator);
            return XJSON_FAILURE;
        }

        free(pIterator);
    }

    if (nIdent) XJSON_ASSERT(XJSON_Ident(pWriter, XJSON_IDENT_DEC));
    return XJSON_WriteString(pWriter, nIdent, "}");
}

static int XJSON_WriteArray(xjson_obj_t *pObj, xjson_writer_t *pWriter)
{
    XJSON_ASSERT(XJSON_CheckObject(pObj, XJSON_TYPE_ARRAY));
    XJSON_ASSERT(XJSON_WriteName(pObj, pWriter));
    int nIdent = pObj->pName == NULL ? 1 : 0;

    xarray_t* pArray = (xarray_t*)pObj->pData;
    XJSON_ASSERT(XJSON_WriteString(pWriter, nIdent, "["));

    size_t i, nUsed = XArray_Used(pArray);
    nIdent = (pWriter->nTabSize && nUsed) ? 1 : 0;

    if (nIdent)
    {
        XJSON_ASSERT(XJSON_WriteString(pWriter, 0, "\n"));
        XJSON_ASSERT(XJSON_Ident(pWriter, XJSON_IDENT_INC));
    }

    for (i = 0; i < nUsed; i++)
    {
        xjson_obj_t *pItem = XArray_GetData(pArray, i);
        XJSON_ASSERT(XJSON_WriteObject(pItem, pWriter));
        if ((i + 1) < nUsed) XJSON_ASSERT(XJSON_WriteString(pWriter, 0, ","));
        if (pWriter->nTabSize) XJSON_ASSERT(XJSON_WriteString(pWriter, 0, "\n"));
    }

    if (nIdent) XJSON_ASSERT(XJSON_Ident(pWriter, XJSON_IDENT_DEC));
    return XJSON_WriteString(pWriter, nIdent, "]");
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

int XJSON_InitWriter(xjson_writer_t *pWriter, char *pOutput, size_t nSize)
{
    pWriter->nAvail = nSize;
    pWriter->pData = pOutput;
    pWriter->nSize = nSize;
    pWriter->nAlloc = 0;

    if (pWriter->pData == NULL && pWriter->nSize)
    {
        pWriter->pData = malloc(pWriter->nSize);
        if (pWriter->pData == NULL) return 0;
        pWriter->nAlloc = 1;
    }

    if (pWriter->pData != NULL)
        pWriter->pData[0] = '\0';

    pWriter->nTabSize = 0;
    pWriter->nIdents = 0;
    pWriter->nLength = 0;

    return 1;
}

void XJSON_DestroyWriter(xjson_writer_t *pWriter)
{
    if (pWriter && pWriter->nAlloc)
    {
        free(pWriter->pData);
        pWriter->pData = NULL;
        pWriter->nAlloc = 0;
    }
}

int XJSON_Write(xjson_t *pJson, char *pOutput, size_t nSize)
{
    if (pJson == NULL || 
        pOutput == NULL || 
        !nSize) return XJSON_FAILURE;

    xjson_writer_t writer;
    XJSON_InitWriter(&writer, pOutput, nSize);
    return XJSON_WriteObject(pJson->pRootObj, &writer);;
}
