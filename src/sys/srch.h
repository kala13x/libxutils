/*!
 *  @file libxutils/src/sys/srch.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Advanced file search implementation.
 */

#ifndef __XUTILS_SEARCH_H__
#define __XUTILS_SEARCH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"
#include "str.h"
#include "buf.h"
#include "xfs.h"
#include "sync.h"
#include "array.h"

typedef struct XSearchEntry {
    char sPath[XPATH_MAX];
    char sLink[XPATH_MAX];
    char sName[XNAME_MAX];
    char sPerm[XPERM_MAX];
    char sLine[XLINE_MAX];
    xfile_type_t eType;
    xmode_t nMode;
    size_t nLinkCount;
    uint32_t nGID;
    uint32_t nUID;
    time_t nTime;
    size_t nSize;
    size_t nLineNum;
    char *pRealPath;
} xsearch_entry_t;

typedef struct XSearchCtx xsearch_t;
typedef int(*xsearch_cb_t)(xsearch_t *pSearch, xsearch_entry_t *pEntry, const char *pMsg);

xsearch_entry_t* XSearch_AllocEntry();
void XSearch_FreeEntry(xsearch_entry_t *pEntry);
void XSearch_InitEntry(xsearch_entry_t *pEntry);
void XSearch_CreateEntry(xsearch_entry_t *pEntry, const char *pName, const char *pPath, xstat_t *pStat);
xsearch_entry_t* XSearch_NewEntry(const char *pName, const char *pPath, xstat_t *pStat);
xsearch_entry_t* XSearch_GetEntry(xsearch_t *pSearch, int nIndex);

struct XSearchCtx {
    /* Search context */
    xarray_t fileArray;             // Found file array
    xarray_t nameTokens;            // Search name tokens
    xbool_t bSearchLines;           // Search in file lines
    xbool_t bInsensitive;           // Case insensitive search
    xbool_t bRecursive;             // Recursive search
    xbool_t bMatchOnly;             // Search match only
    xbool_t bReadStdin;             // Read from stdin

    xsearch_cb_t callback;     // Search callback
    void *pUserCtx;                 // User space

    /* Search criteria */
    char sName[XPATH_MAX];          // Needed file name
    char sText[XSTR_MID];           // Containing text
    int nPermissions;               // Needed file permissions
    int nLinkCount;                 // Needed file link count
    int nFileTypes;                 // Needed file types
    int nFileSize;                  // Needed file size
    size_t nBufferSize;             // Max read buffer size
    size_t nMaxSize;                // Max file size to search
    size_t nMinSize;                // Min file size to search
    xbool_t bMulty;                 // Multy file name search

    xatomic_t *pInterrupted;        // Interrupt flag pointer
    xatomic_t nInterrupted;         // Interrupt flag
};

void XSearch_Init(xsearch_t *pSrcCtx, const char *pFileName);
void XSearch_Destroy(xsearch_t *pSrcCtx);
int XSearch(xsearch_t *pSearch, const char *pDirectory);

#ifdef __cplusplus
}
#endif


#endif /* __XUTILS_SEARCH_H__ */
