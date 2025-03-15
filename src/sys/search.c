/*!
 *  @file libxutils/src/sys/search.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Advanced file search implementation.
 */

#include "search.h"
 
typedef struct {
    const char *pBuffer;
    const char *pPath;
    const char *pName;
    xstat_t *pStat;
    size_t nLength;
    size_t nPosit;
} xsearch_context_t;

void XSearch_InitEntry(xsearch_entry_t *pEntry)
{
    pEntry->sPath[0] = XSTR_NUL;
    pEntry->sName[0] = XSTR_NUL;
    pEntry->sLink[0] = XSTR_NUL;
    pEntry->sLine[0] = XSTR_NUL;
    pEntry->eType = XF_UNKNOWN;
    pEntry->pRealPath = NULL;
    pEntry->nLinkCount = 0;
    pEntry->nLineNum = 0;
    pEntry->nTime = 0;
    pEntry->nSize = 0;
    pEntry->nGID = 0;
    pEntry->nUID = 0;
}

void XSearch_CreateEntry(xsearch_entry_t *pEntry, const char *pName, const char *pPath, xstat_t *pStat)
{
    XSearch_InitEntry(pEntry);

    XPath_ModeToPerm(pEntry->sPerm, sizeof(pEntry->sPerm), pStat->st_mode);
    if (pName != NULL) xstrncpy(pEntry->sName, sizeof(pEntry->sName), pName);
    if (pPath != NULL) xstrncpy(pEntry->sPath, sizeof(pEntry->sPath), pPath);

    pEntry->eType = XFile_GetType(pStat->st_mode);
    pEntry->nLinkCount = pStat->st_nlink;
    pEntry->nTime = pStat->st_mtime;
    pEntry->nSize = pStat->st_size;
    pEntry->nGID = pStat->st_gid;
    pEntry->nUID = pStat->st_uid;

#ifndef _WIN32
    if (pEntry->eType == XF_SYMLINK && pName != NULL && pPath != NULL)
    {
        char sPath[XPATH_MAX];
        xstrncpyf(sPath, sizeof(sPath), "%s%s", pPath, pName);

        int nLen = readlink(sPath, pEntry->sLink, sizeof(pEntry->sLink)-1);
        if (nLen < 0) nLen = 0;

        pEntry->sLink[nLen] = XSTR_NUL;
        pEntry->pRealPath = realpath(sPath, NULL);
    }
#endif
}

xsearch_entry_t* XSearch_AllocEntry()
{
    xsearch_entry_t *pEntry = (xsearch_entry_t*)malloc(sizeof(xsearch_entry_t));
    if (pEntry == NULL) return NULL;

    XSearch_InitEntry(pEntry);
    return pEntry;
}

xsearch_entry_t* XSearch_NewEntry(const char *pName, const char *pPath, xstat_t *pStat)
{
    xsearch_entry_t *pEntry = (xsearch_entry_t*)malloc(sizeof(xsearch_entry_t));
    if (pEntry == NULL) return NULL;

    XSearch_CreateEntry(pEntry, pName, pPath, pStat);
    return pEntry;
}

void XSearch_FreeEntry(xsearch_entry_t *pEntry)
{
    if (pEntry != NULL)
    {
        if (pEntry->pRealPath != NULL)
            free(pEntry->pRealPath);

        free(pEntry);
    }
}

static void XFile_ArrayClearCb(xarray_data_t *pItem)
{
    if (pItem == NULL) return;
    free(pItem->pData);
}

static int XFile_ErrorCallback(xsearch_t *pSearch, const char *pStr, ...)
{
    if (pSearch == NULL) return XSTDERR;
    else if (pSearch->callback == NULL) return XSTDOK;

    char sMessage[XSTR_MID];
    va_list args;

    va_start(args, pStr);
    xstrncpyarg(sMessage, sizeof(sMessage), pStr, args);
    va_end(args);

    if (pSearch->callback(pSearch, NULL, sMessage) < 0)
    {
        XSYNC_ATOMIC_SET(pSearch->pInterrupted, 1);
        return XSTDERR;
    }

    return XSTDOK;
}

static int XSearchCallback(xsearch_t *pSearch, xsearch_entry_t *pEntry)
{
    if (pSearch == NULL || pEntry == NULL) return XSTDERR;

    int nReturnValue = (pSearch->callback != NULL) ?
        pSearch->callback(pSearch, pEntry, NULL) : XSTDOK;

    if (nReturnValue > 0)
    {
        if (XArray_AddData(&pSearch->fileArray, pEntry, 0) < 0)
        {
            XFile_ErrorCallback(pSearch, "Failed to append entry: %s%s", pEntry->sPath, pEntry->sName);
            return XSTDERR;
        }

        return XSTDOK;
    }
    else if (nReturnValue < 0)
    {
        XSYNC_ATOMIC_SET(pSearch->pInterrupted, 1);
        XSearch_FreeEntry(pEntry);
        return XSTDERR;
    }

    XSearch_FreeEntry(pEntry);
    return XSTDNON;
}

static xbool_t XSearchTokens(xarray_t *pTokens, const char *pName, size_t nLength)
{
    size_t nUsed = XArray_Used(pTokens);
    if (!nUsed) return XFALSE;
    size_t i, nOffset = 0;

    for (i = 0; i < nUsed; i++)
    {
        const char *pTok = (const char*)XArray_GetData(pTokens, i);
        if (xstrused(pTok) && !xstrcmp(pTok, "*"))
        {
            if (!i && !xstrncmp(pTok, pName, strlen(pTok))) return XFALSE;
            int nPosit = xstrnsrc(pName, nLength, pTok, nOffset);
            if (nPosit < 0) return XFALSE;

            nOffset += nPosit;
            if (nOffset >= nLength) return XFALSE;

            const char *pOffset = (const char*)&pName[nOffset];
            if (i && i + 1 == nUsed && !xstrcmp(pTok, pOffset)) return XFALSE;
            nOffset += strlen(pTok);
        }
    }

    return XTRUE;
}

static xbool_t XSearchMulty(xarray_t *pTokens, const char *pName, size_t nLength)
{
    size_t i, nCount = XArray_Used(pTokens);
    xbool_t bFound = XFALSE;

    for (i = 0; i < nCount; i++)
    {
        xarray_data_t *pArrData = XArray_Get(pTokens, i);
        if (pArrData == NULL || pArrData->pData == NULL) continue;

        bFound = pArrData->nKey != XSTDOK ?
            xstrncmp((const char *)pArrData->pData, pName, nLength) :
            XSearchTokens((xarray_t*)pArrData->pData, pName, nLength);

        if (bFound) break;
    }

    return bFound;
}

static xbool_t XSearchName(xsearch_t *pSearch, const char *pFileName)
{
    size_t nLength = strlen(pFileName);
    xbool_t bFound = pSearch->bMulty ?
        XSearchMulty(&pSearch->nameTokens, pFileName, nLength) :
        XSearchTokens(&pSearch->nameTokens, pFileName, nLength);

    return bFound;
}

static XSTATUS XSearchLines(xsearch_t *pSearch, xsearch_context_t *pCtx)
{
    XSTATUS nStatus = XSTDNON;
    xarray_t *pArr = xstrsplite(pCtx->pBuffer, XSTR_NEW_LINE);

    if (pArr != NULL)
    {
        size_t i = 0;
        for (i = 0; i < pArr->nUsed; i++)
        {
            char *pLine = (char*)XArray_GetData(pArr, i);
            if (xstrsrc(pLine, pSearch->sText) >= 0)
            {
                xsearch_entry_t *pEntry = XSearch_NewEntry(pCtx->pName, pCtx->pPath, pCtx->pStat);
                if (pEntry == NULL)
                {
                    XFile_ErrorCallback(pSearch, "Failed to alloc entry: %s", pCtx->pPath);
                    XArray_Destroy(pArr);
                    return XSTDERR;
                }

                xstrncpy(pEntry->sLine, sizeof(pEntry->sLine), pLine);
                pEntry->nLineNum = i + 1;
                nStatus = XSTDOK;

                if (XSearchCallback(pSearch, pEntry) < 0)
                {
                    XArray_Destroy(pArr);
                    return XSTDERR;
                }
            }
        }
    }

    if (nStatus == XSTDNON)
    {
        xsearch_entry_t *pEntry = XSearch_NewEntry(pCtx->pName, pCtx->pPath, pCtx->pStat);
        if (pEntry == NULL)
        {
            XFile_ErrorCallback(pSearch, "Failed to alloc entry: %s", pCtx->pPath);
            XArray_Destroy(pArr);
            return XSTDERR;
        }

        xstrncpy(pEntry->sLine, sizeof(pEntry->sLine), "Binary file matches");
        if (XSearchCallback(pSearch, pEntry) < 0)
        {
            XArray_Destroy(pArr);
            return XSTDERR;
        }
    }

    XArray_Destroy(pArr);
    return XSTDNON;
}

static XSTATUS XSearchBuffer(xsearch_t *pSearch, xsearch_context_t *pCtx)
{
    const char *pData = pCtx->pBuffer;
    size_t nLength = pCtx->nLength;
    XSTATUS nStatus = XSTDNON;

    while (pCtx->nPosit < (int)nLength)
    {
        while (pCtx->nPosit > 1 && pData[pCtx->nPosit] != '\n') pCtx->nPosit--;
        if (pData[pCtx->nPosit] == '\n') pCtx->nPosit++;

        int nEnd = xstrsrc(&pData[pCtx->nPosit], "\n");
        if (nEnd <= 0 || (pCtx->nPosit + nEnd) > (int)nLength) break;

        char sLine[XSTR_MAX];
        xstrncpys(sLine, sizeof(sLine), &pData[pCtx->nPosit], nEnd);

        xsearch_entry_t *pEntry = XSearch_NewEntry(pCtx->pName, pCtx->pPath, pCtx->pStat);
        if (pEntry == NULL)
        {
            XFile_ErrorCallback(pSearch, "Failed to alloc entry: %s", pCtx->pPath);
            return XSTDERR;
        }

        xstrncpy(pEntry->sLine, sizeof(pEntry->sLine), sLine);
        if (XSearchCallback(pSearch, pEntry) < 0) return XSTDERR;

        nStatus = XSTDOK;
        pCtx->nPosit += nEnd;
        if (pCtx->nPosit >= nLength) break;

        int nNewPosit = xstrsrc(&pData[pCtx->nPosit], pSearch->sText);
        if (nNewPosit < 0) break;
        pCtx->nPosit += nNewPosit;
    }

    if (nStatus == XSTDNON)
    {
        xsearch_entry_t *pEntry = XSearch_NewEntry(pCtx->pName, pCtx->pPath, pCtx->pStat);
        if (pEntry == NULL)
        {
            XFile_ErrorCallback(pSearch, "Failed to alloc entry: %s", pCtx->pPath);
            return XSTDERR;
        }

        xstrncpy(pEntry->sLine, sizeof(pEntry->sLine), "Binary file matches");
        if (XSearchCallback(pSearch, pEntry) < 0) return XSTDERR;
    }

    return XSTDNON;
}

static XSTATUS XSearchText(xsearch_t *pSearch, const char *pPath, const char *pName, xstat_t *pStat)
{
    char sPath[XPATH_MAX];
    xbyte_buffer_t buffer;
    XSTATUS nStatus = XSTDOK;

    xstrncpyf(sPath, sizeof(sPath), "%s%s", pPath, pName);
    XPath_LoadBufferSize(sPath, &buffer, pSearch->nBufferSize);
    if (buffer.pData == NULL || !buffer.nUsed) return XSTDNON;

    if (pSearch->bInsensitive) xstrcase((char*)buffer.pData, XSTR_LOWER);
    int nPosit = xstrsrcb((char*)buffer.pData, buffer.nUsed, pSearch->sText);

    if (nPosit < 0 || nPosit >= (int)buffer.nUsed)
    {
        XByteBuffer_Clear(&buffer);
        return XSTDNON;
    }

    if (!pSearch->bFilesOnly)
    {
        xsearch_context_t ctx;
        ctx.pBuffer = (char*)buffer.pData;
        ctx.nLength = buffer.nUsed;
        ctx.nPosit = nPosit;
        ctx.pPath = pPath;
        ctx.pName = pName;
        ctx.pStat = pStat;

        nStatus = pSearch->bSearchLines ?
            XSearchLines(pSearch, &ctx) :
            XSearchBuffer(pSearch, &ctx);
    }

    XByteBuffer_Clear(&buffer);
    return nStatus;
}

static XSTATUS XFile_CheckCriteria(xsearch_t *pSearch, const char *pPath, const char *pName, xstat_t *pStat)
{
    if (pSearch->nLinkCount >= 0 && pSearch->nLinkCount != pStat->st_nlink) return XSTDNON;
    if (pSearch->nFileSize >= 0 && pSearch->nFileSize != pStat->st_size) return XSTDNON;
    if (pSearch->nMaxSize > 0 && pSearch->nMaxSize < pStat->st_size) return XSTDNON;
    if (pSearch->nMinSize > 0 && pSearch->nMinSize > pStat->st_size) return XSTDNON;

    if (pSearch->nPermissions)
    {
        char buff[XPERM_MAX];
        XPath_ModeToChmod(buff, sizeof(buff), pStat->st_mode);
        if (atoi(buff) != pSearch->nPermissions) return XSTDNON;
    }

    if (pSearch->nFileTypes)
    {
        xfile_type_t eType = XFile_GetType(pStat->st_mode);
        if (!XFILE_CHECK_FL(pSearch->nFileTypes, eType)) return XSTDNON;
    }

    if (xstrused(pSearch->sName))
    {
        char sName[XPATH_MAX];
        if (pSearch->bInsensitive) xstrncase(sName, sizeof(sName), XSTR_LOWER, pName);
        const char *pSearchName = pSearch->bInsensitive ? (const char *)sName : pName;

        xbool_t bFound = pSearch->nameTokens.nUsed ?
            XSearchName(pSearch, pSearchName) :
            xstrcmp(pSearch->sName, pSearchName);

        if (!bFound) return XSTDNON;
    }

    if (xstrused(pSearch->sText))
    {
        xfile_type_t eType = XFile_GetType(pStat->st_mode);
        if (eType != XF_REGULAR) return XSTDNON;

        XSTATUS nStatus = XSearchText(pSearch, pPath, pName, pStat);
        if (nStatus <= XSTDNON) return nStatus;
    }

    return XSTDOK;
}

void XSearchClearCb(xarray_data_t *pArrData)
{
    if (pArrData == NULL || pArrData->pData == NULL) return;
    if (!pArrData->nKey) xfree(pArrData->pPool, pArrData->pData);
    else XArray_Destroy((xarray_t*)pArrData->pData);
}

static size_t XFile_TokenizeName(xsearch_t *pSrcCtx, const char *pFileName)
{
    xarray_t *pTokens = &pSrcCtx->nameTokens;
    if (xstrsrc(pFileName, ";") >= 0) xstrsplita(pFileName, ";", pTokens, XFALSE, XFALSE);
    else if (xstrsrc(pFileName, "*") >= 0) return xstrsplita(pFileName, "*", pTokens, XTRUE, XFALSE);

    if (!pTokens->nUsed) return XSTDNON;
    pSrcCtx->bMulty = XTRUE;
    size_t i;

    for (i = 0; i < pTokens->nUsed; i++)
    {
        char *pToken = XArray_GetData(pTokens, i);
        if (xstrsrc(pToken, "*") >= 0)
        {
            xarray_t *pSubTokens = xstrsplitd(pToken, "*");
            if (pSubTokens != NULL)
            {
                xarray_data_t *pArrData = XArray_Get(pTokens, i);
                pArrData->pData = pSubTokens;
                pArrData->nKey = XSTDOK;

                pTokens->clearCb = XSearchClearCb;
                xfree(pTokens->pPool, pToken);
            }
        }
    }

    return pTokens->nUsed;
}

void XSearch_Init(xsearch_t *pSrcCtx, const char *pFileName)
{
    pSrcCtx->pInterrupted = &pSrcCtx->nInterrupted;
    pSrcCtx->bInsensitive = XFALSE;
    pSrcCtx->bSearchLines = XFALSE;
    pSrcCtx->bFilesOnly = XFALSE;
    pSrcCtx->bRecursive = XFALSE;
    pSrcCtx->bMulty = XFALSE;
    pSrcCtx->callback = NULL;
    pSrcCtx->pUserCtx = NULL;

    XArray_InitPool(&pSrcCtx->nameTokens, XSTDNON, XSTDNON, XFALSE);
    XArray_InitPool(&pSrcCtx->fileArray, XSTDNON, XSTDNON, XFALSE);

    pSrcCtx->fileArray.clearCb = XFile_ArrayClearCb;
    pSrcCtx->nameTokens.clearCb = XSearchClearCb;

    pSrcCtx->sName[0] = XSTR_NUL;
    pSrcCtx->sText[0] = XSTR_NUL;

    XFile_TokenizeName(pSrcCtx, pFileName);
    xstrncpy(pSrcCtx->sName, sizeof(pSrcCtx->sName), pFileName);

    pSrcCtx->nBufferSize = 0;
    pSrcCtx->nPermissions = 0;
    pSrcCtx->nFileTypes = 0;
    pSrcCtx->nLinkCount = -1;
    pSrcCtx->nFileSize = -1;
    pSrcCtx->nMaxSize = 0;
    pSrcCtx->nMinSize = 0;
}

void XSearch_Destroy(xsearch_t *pSrcCtx)
{
    XASSERT_VOID_RET(pSrcCtx);
    XSYNC_ATOMIC_SET(pSrcCtx->pInterrupted, 1);

    pSrcCtx->nameTokens.clearCb = XSearchClearCb;
    pSrcCtx->fileArray.clearCb = XFile_ArrayClearCb;

    XArray_Destroy(&pSrcCtx->fileArray);
    XArray_Destroy(&pSrcCtx->nameTokens);
}

xsearch_entry_t* XSearch_GetEntry(xsearch_t *pSearch, int nIndex)
{
    if (nIndex >= XArray_Used(&pSearch->fileArray)) return NULL;
    return (xsearch_entry_t*)XArray_GetData(&pSearch->fileArray, nIndex);
}

int XSearch(xsearch_t *pSearch, const char *pDirectory)
{
    if (XSYNC_ATOMIC_GET(pSearch->pInterrupted) ||
        pDirectory == NULL) return XSTDERR;

    xdir_t dirHandle;
    char sDirPath[XPATH_MAX];

    size_t nDirLen = strlen(pDirectory);
    while (nDirLen && pDirectory[nDirLen--] == XSTR_SPACE_CHAR);
    const char *pSlash = pDirectory[nDirLen] == '/' ? XSTR_EMPTY : "/";
    xstrncpyf(sDirPath, sizeof(sDirPath), "%s%s", pDirectory, pSlash);

    if (XDir_Open(&dirHandle, sDirPath) < 0)
    {
        XFile_ErrorCallback(pSearch, "Failed to open directory: %s", sDirPath);
        return XSYNC_ATOMIC_GET(pSearch->pInterrupted) ? XSTDERR : XSTDOK;
    }

    if (pSearch->bInsensitive)
    {
        xstrcase(pSearch->sName, XSTR_LOWER);
        xstrcase(pSearch->sText, XSTR_LOWER);
    }

    while (XDir_Read(&dirHandle, NULL, 0) > 0 && !XSYNC_ATOMIC_GET(pSearch->pInterrupted))
    {
        char sFullPath[XPATH_MAX];
        xstat_t statbuf;

        const char *pEntryName = dirHandle.pCurrEntry;
        xstrncpyf(sFullPath, sizeof(sFullPath), "%s%s", sDirPath, pEntryName);

        if (xstat(sFullPath, &statbuf) < 0)
        {
            XFile_ErrorCallback(pSearch, "Failed to stat file: %s", sFullPath);
            if (XSYNC_ATOMIC_GET(pSearch->pInterrupted)) return XSTDERR;
            continue;
        }

        int nMatch = XFile_CheckCriteria(pSearch, sDirPath, pEntryName, &statbuf);
        if (nMatch > 0)
        {
            xsearch_entry_t *pEntry = XSearch_NewEntry(pEntryName, sDirPath, &statbuf);
            if (pEntry == NULL)
            {
                XFile_ErrorCallback(pSearch, "Failed to alloc entry: %s", sFullPath);
                XDir_Close(&dirHandle);
                return XSTDERR;
            }

            if (XSearchCallback(pSearch, pEntry) < 0)
            {
                XDir_Close(&dirHandle);
                return XSTDERR;
            }
        }
        else if (nMatch < 0)
        {
            XDir_Close(&dirHandle);
            return XSTDERR;
        }

        /* Recursive search */
        if (pSearch->bRecursive && 
            S_ISDIR(statbuf.st_mode) && 
            XSearch(pSearch, sFullPath) < 0)
        {
            XDir_Close(&dirHandle);
            return XSTDERR;
        }
    }

    XDir_Close(&dirHandle);
    return XSTDOK;
}