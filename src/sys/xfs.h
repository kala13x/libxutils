/*!
 *  @file libxutils/src/sys/xfs.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of the NIX/POSIX 
 * standart file and directory operations.
 */

#ifndef __XUTILS_XFILE_H__
#define __XUTILS_XFILE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xstd.h"
#include "str.h"
#include "buf.h"
#include "array.h"

#if !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#if !defined(S_ISDIR) && defined(S_IFMT) && defined(S_IFDIR)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#define XFILE_INVALID XSTDERR
#define XFILE_UNSETRC XSTDNON
#define XFILE_SUCCESS XSTDOK

typedef struct XFile {
    uint64_t nPosit;
    xmode_t nMode;
    size_t nBlockSize;
    size_t nSize;
    xbool_t bEOF;
    xbool_t nAlloc;
    int nFlags;
    int nFD;
} xfile_t;

typedef struct XDir {
    const char* pPath;
    char* pCurrEntry;
#ifdef _WIN32
    WIN32_FIND_DATA entry;
    HANDLE pDirectory;
#else
    struct dirent* pEntry;
    DIR* pDirectory;
#endif
    uint8_t nFirstFile;
    uint8_t nOpen;
} xdir_t;

typedef struct XPath {
    char sPath[XPATH_MAX];
    char sFile[XNAME_MAX];
} xpath_t;

typedef struct stat xstat_t;

#define xfprintf(xf, ...) \
    XFile_Print(xf, __VA_ARGS__)

#define XFILE_CHECK_FL(c, f) (((c) & (f)) == (f))

int xstat(const char *pPath, xstat_t *pStat);
int xmkdir(const char* pPath, xmode_t nMode);
int xunlink(const char* pPath);
int xrmdir(const char* pPath);
int xclose(int nFD);

int XFile_Open(xfile_t *pFile, const char *pPath, const char *pFlags, const char *pPerms);
xfile_t* XFile_Alloc(const char *pPath, const char *pFlags, const char *pPerms);

xbool_t XFile_IsOpen(xfile_t *pFile);
void XFile_Destroy(xfile_t *pFile);
void XFile_Close(xfile_t *pFile);
void XFile_Free(xfile_t **ppFile);

size_t XFile_Seek(xfile_t *pFile, uint64_t nPosit, int nOffset);
int XFile_Write(xfile_t *pFile, const void *pBuff, size_t nSize);
int XFile_Read(xfile_t *pFile, void *pBuff, size_t nSize);
int XFile_Print(xfile_t *pFile, const char *pFmt, ...);

int XFile_GetStats(xfile_t *pFile);
int XFile_Copy(xfile_t *pIn, xfile_t *pOut);
int XFile_GetLine(xfile_t *pFile, char* pLine, size_t nSize);
int XFile_GetLineCount(xfile_t *pFile);
int XFile_ReadLine(xfile_t *pFile, char* pLine, size_t nSize, size_t nLineNum);
uint8_t* XFile_Load(xfile_t *pFile, size_t *pSize);
uint8_t* XFile_LoadSize(xfile_t *pFile, size_t nMaxSize, size_t *pSize);

xbool_t XPath_Exists(const char *pPath);
char XPath_GetType(xmode_t nMode);
int XPath_Parse(xpath_t *pPath, const char *pPathStr, xbool_t bStat);
int XPath_SetPerm(const char *pPath, const char *pPerm);
int XPath_GetPerm(char *pOutput, size_t nSize, const char *pPath);
int XPath_PermToMode(const char *pPerm, xmode_t *pMode);
int XPath_ModeToPerm(char *pOutput, size_t nSize, xmode_t nMode);
int XPath_ModeToChmod(char *pOutput, size_t nSize, xmode_t nMode);

long XPath_GetSize(const char *pPath);
uint8_t* XPath_Load(const char *pPath, size_t* pSize);
uint8_t* XPath_LoadSize(const char *pPath, size_t nMaxSize, size_t* pSize);
int XPath_CopyFile(const char *pSrc, const char *pDst);
int XPath_Read(const char *pPath, uint8_t *pBuffer, size_t nSize);
int XPath_Write(const char *pPath, const char *pFlags, const uint8_t *pData, size_t nSize);
int XPath_WriteBuffer(const char *pPath, const char *pFlags, xbyte_buffer_t *pBuffer);
size_t XPath_LoadBuffer(const char *pPath, xbyte_buffer_t *pBuffer);
size_t XPath_LoadBufferSize(const char *pPath, xbyte_buffer_t *pBuffer, size_t nMaxSize);

void XDir_Close(xdir_t *pDir);
int XDir_Open(xdir_t *pDir, const char *pPath);
int XDir_Read(xdir_t *pDir, char *pFile, size_t nSize);
int XDir_Create(const char *pDir, xmode_t mode);
int XPath_Remove(const char *pPath);
int XDir_Valid(const char *pPath);
int XDir_Remove(const char *pPath);

///////////////////////////////////////////////////////////////////////////////////////////////
// Advanced file search implementation
///////////////////////////////////////////////////////////////////////////////////////////////

typedef enum {
    XF_UNKNOWN = 0,
    XF_BLOCK_DEVICE = (1 << 0),
    XF_CHAR_DEVICE = (1 << 1),
    XF_DIRECTORY = (1 << 2),
    XF_REGULAR = (1 << 3),
    XF_SYMLINK = (1 << 4),
    XF_SOCKET = (1 << 5),
    XF_PIPE = (1 << 6)
} xfile_type_t;

typedef struct XFileEntry {
    char sPath[XPATH_MAX];
    char sLink[XPATH_MAX];
    char sName[XNAME_MAX];
    char sPerm[XPERM_MAX];
    char sLine[XLINE_MAX];
    xfile_type_t eType;
    size_t nLinkCount;
    uint32_t nGID;
    uint32_t nUID;
    time_t nTime;
    size_t nSize;
    size_t nLineNum;
    char *pRealPath;
} xfile_entry_t;

void XFile_CreateEntry(xfile_entry_t *pEntry, const char *pName, const char *pPath, xstat_t *pStat);
xfile_entry_t* XFile_NewEntry(const char *pName, const char *pPath, xstat_t *pStat);
xfile_entry_t* XFile_AllocEntry();

void XFile_InitEntry(xfile_entry_t *pEntry);
void XFile_FreeEntry(xfile_entry_t *pEntry);
char XFile_GetTypeChar(xfile_type_t eType);

typedef struct XFileSearch xfile_search_t;
typedef int(*xfile_search_cb_t)(xfile_search_t *pSearch, xfile_entry_t *pEntry, const char *pMsg);

struct XFileSearch {
    /* Search context */
    xfile_search_cb_t callback;     // Search callback
    xarray_t fileArray;             // Found file array
    xbool_t bSearchLines;           // Search in file lines
    xbool_t bInsensitive;           // Case insensitive search
    xbool_t bRecursive;             // Recursive search
    xbool_t bFilesOnly;             // Show files only
    void *pUserCtx;                 // User space

    /* Search criteria */
    const char* pFileName;          // Needed file name
    char sText[XSTR_MID];           // Containing text
    xarray_t *pTokens;              // Search name tokens
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

void XFile_SearchInit(xfile_search_t *pSrcCtx, const char *pFileName);
void XFile_SearchDestroy(xfile_search_t *pSrcCtx);
int XFile_Search(xfile_search_t *pSearch, const char *pDirectory);
xfile_entry_t* XFile_GetEntry(xfile_search_t *pSearch, int nIndex);

///////////////////////////////////////////////////////////////////////////////////////////////
// End of advanced file search implementation
///////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif


#endif /* __XUTILS_XFILE_H__ */
