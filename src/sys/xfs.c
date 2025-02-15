/*!
 *  @file libxutils/src/sys/xfs.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Implementation of the NIX/POSIX 
 * standart file and directory operations.
 */

#include "xfs.h"
#include "str.h"
#include "sync.h"

#define XFILE_BUF_SIZE      4096
#define XFILE_FLAGS_LEN     10
#define XFILE_DEFAULT_PERM  "rw-r--r--"

typedef struct {
    xbyte_buffer_t *pBuffer;
    const char *pPath;
    const char *pName;
    xstat_t *pStat;
    size_t nPosit;
} xsearch_context_t;

int xchmod(const char* pPath, xmode_t nMode)
{
#ifdef _WIN32
    return _chmod(pPath, nMode);
#else
    return chmod(pPath, nMode);
#endif
}

int xunlink(const char* pPath)
{
#ifdef _WIN32
    return _unlink(pPath);
#else
    return unlink(pPath);
#endif
}

int xrmdir(const char* pPath)
{
#ifdef _WIN32
    return _rmdir(pPath);
#else
    return rmdir(pPath);
#endif
}

int xmkdir(const char* pPath, xmode_t nMode)
{
#ifdef _WIN32
    (void)nMode;
    return _mkdir(pPath);
#else
    return mkdir(pPath, nMode);
#endif
}

int xclose(int nFD)
{
#ifdef _WIN32
    return _close(nFD);
#else
    return close(nFD);
#endif
}

int xstat(const char *pPath, xstat_t *pStat)
{
    memset(pStat, 0, sizeof(xstat_t));
#ifdef _WIN32
    if (stat(pPath, pStat) < 0) return XSTDERR;
#else
    if (lstat(pPath, pStat) < 0) return XSTDERR;
#endif
    return XSTDOK;
}

int XFile_ParseFlags(const char *pFlags)
{
    size_t i, nLen = strnlen(pFlags, XFILE_FLAGS_LEN);
    int nFlags = 0;

    for (i = 0; i < nLen; i++)
    {
        switch(pFlags[i])
        {
#ifdef O_APPEND
            case 'a': { nFlags |= O_APPEND; break; }
#endif
#ifdef O_CREAT
            case 'c': { nFlags |= O_CREAT; break; }
#endif
#ifdef O_NDELAY
            case 'd': { nFlags |= O_NDELAY; break; }
#endif
#ifdef O_EXCL
            case 'e': { nFlags |= O_EXCL; break; }
#endif
#ifdef O_NONBLOCK
            case 'n': { nFlags |= O_NONBLOCK; break; }
#endif
#ifdef O_RDONLY
            case 'r': { nFlags |= O_RDONLY; break; }
#endif
#ifdef O_TRUNC
            case 't': { nFlags |= O_TRUNC; break; }
#endif
#ifdef O_SYNC
            case 's': { nFlags |= O_SYNC; break; }
#endif
#ifdef O_WRONLY
            case 'w': { nFlags |= O_WRONLY; break; }
#endif
#ifdef O_RDWR
            case 'x': { nFlags |= O_RDWR; break; }
#endif
            default: break;
        }
    }

#if defined(O_RDONLY) && defined(O_WRONLY) && defined(O_RDWR)
    if (XFILE_CHECK_FL(nFlags, O_RDONLY) &&
        XFILE_CHECK_FL(nFlags, O_WRONLY))
    {
        nFlags &= ~O_RDONLY;
        nFlags &= ~O_WRONLY;
        nFlags |= O_RDWR;
    }
#endif

    return nFlags;
}

int XFile_Open(xfile_t *pFile, const char *pPath, const char *pFlags, const char *pPerms)
{
    if (pFile == NULL || pPath == NULL) return XSTDERR;
    pFile->nFlags = (pFlags != NULL) ? XFile_ParseFlags(pFlags) : 0;
    pFile->nBlockSize = XFILE_BUF_SIZE;
    pFile->nSize = 0;
    pFile->nFD = -1;

    const char *pPerm = (pPerms != NULL) ? pPerms : XFILE_DEFAULT_PERM;
    if (!XPath_PermToMode(pPerm, &pFile->nMode)) return XSTDERR;

#ifdef _WIN32
    _sopen_s(&pFile->nFD, pPath, pFile->nFlags, _SH_DENYNO, pFile->nMode);
#else
    pFile->nFD = open(pPath, pFile->nFlags, pFile->nMode);
#endif

    pFile->nPosit = pFile->nAlloc = 0;
    return pFile->nFD;
}

xfile_t* XFile_Alloc(const char *pPath, const char *pFlags, const char *pPerms)
{
    xfile_t *pFile = (xfile_t*)malloc(sizeof(xfile_t));
    if (pFile == NULL) return NULL;

    if (XFile_Open(pFile, pPath, pFlags, pPerms) < 0)
    {
        free(pFile);
        return NULL;
    }

    pFile->nAlloc = 1;
    return pFile;
}

xbool_t XFile_IsOpen(xfile_t *pFile)
{
    XASSERT(pFile, XFALSE);
    return (pFile->nFD >= 0) ?
            XTRUE : XFALSE;
}

void XFile_Close(xfile_t *pFile)
{
    if (pFile != NULL)
    {
        if (pFile->nFD >= 0)
        {
            xclose(pFile->nFD);
            pFile->nFD = -1;
        }

        pFile->nFlags = 0;
        pFile->nPosit = 0;
    }
}

void XFile_Destroy(xfile_t *pFile)
{
    if (pFile != NULL)
    {
        XFile_Close(pFile);

        if (pFile->nAlloc)
            free(pFile);
    }
}

void XFile_Free(xfile_t **ppFile)
{
    XASSERT_VOID_RET((ppFile && *ppFile));
    xfile_t *pFile = *ppFile;

    XFile_Destroy(pFile);
    *ppFile = NULL;
}

size_t XFile_Seek(xfile_t *pFile, uint64_t nPosit, int nOffset)
{
    XASSERT(XFile_IsOpen(pFile), XSTDERR);
#ifdef _WIN32
    return (int)_lseek(pFile->nFD, (long)nPosit, nOffset);
#else
    return lseek(pFile->nFD, nPosit, nOffset);
#endif
}

int XFile_Write(xfile_t *pFile, const void *pBuff, size_t nSize)
{
    XASSERT(XFile_IsOpen(pFile), XSTDERR);
#ifdef _WIN32
    return _write(pFile->nFD, pBuff, (unsigned int)nSize);
#else
    return write(pFile->nFD, pBuff, nSize);
#endif
}

int XFile_Read(xfile_t *pFile, void *pBuff, size_t nSize)
{
    XASSERT(XFile_IsOpen(pFile), XSTDERR);
#ifdef _WIN32
    return _read(pFile->nFD, pBuff, (unsigned int)nSize);
#else
    return read(pFile->nFD, pBuff, nSize);
#endif
}

int XFile_Print(xfile_t *pFile, const char *pFmt, ...)
{
    va_list args;
    size_t nLength = 0;

    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, &nLength);
    va_end(args);

    XASSERT(pDest, XSTDERR);
    int nStatus = XFile_Write(pFile, pDest, nLength);

    free(pDest);
    return nStatus;
}

int XFile_GetStats(xfile_t *pFile)
{
    XASSERT(XFile_IsOpen(pFile), XSTDERR);

    xstat_t fileStat;
    if (fstat(pFile->nFD, &fileStat) < 0) return XSTDERR;

#ifdef _WIN32
    pFile->nBlockSize = XFILE_BUF_SIZE;
#else
    pFile->nBlockSize = fileStat.st_blksize ?
        fileStat.st_blksize : XFILE_BUF_SIZE;
#endif

    pFile->nMode = fileStat.st_mode;
    pFile->nSize = fileStat.st_size;
    return pFile->nSize ? XSTDOK : XSTDNON;
}

uint8_t* XFile_LoadSize(xfile_t *pFile, size_t nMaxSize, size_t *pSize)
{
    if (pSize) *pSize = 0;
    if (XFile_GetStats(pFile) <= 0 || !S_ISREG(pFile->nMode)) return NULL;

    size_t nAllowedToRead = XSTD_MIN(nMaxSize, pFile->nSize);
    nAllowedToRead = nAllowedToRead ? nAllowedToRead : pFile->nSize;

    uint8_t *pBuffer = (uint8_t*)malloc(nAllowedToRead + 1);
    if (pBuffer == NULL) return NULL;

    size_t nOffset = 0;
    int nBytes = 0;

    do
    {
        size_t nFreeSpace = nAllowedToRead - nOffset;
        size_t nReadSize = pFile->nBlockSize < nFreeSpace ?
                           pFile->nBlockSize : nFreeSpace;

        nReadSize = nReadSize < nMaxSize ? nReadSize : nMaxSize;
        if (nReadSize == 0) break;

        nBytes = XFile_Read(pFile, &pBuffer[nOffset], nReadSize);
        if (nBytes > 0) nOffset += nBytes;

    } while (nBytes > 0);

    if (!nOffset)
    {
        free(pBuffer);
        return NULL;
    }

    pBuffer[nOffset] = '\0';
    if (pSize) *pSize = nOffset;

    return pBuffer;
}

uint8_t* XFile_Load(xfile_t *pFile, size_t *pSize)
{
    // Use XSTDNON as MaxSize to read all file
    return XFile_LoadSize(pFile, XSTDNON, pSize);
}

int XFile_Copy(xfile_t *pIn, xfile_t *pOut)
{
    XASSERT((XFile_GetStats(pIn) > 0), XSTDERR);
    XASSERT((XFile_IsOpen(pOut)), XSTDERR);

    uint8_t *pBlock = (uint8_t*)malloc(pIn->nBlockSize);
    if (pBlock == NULL) return XSTDERR;

    int nTotalBytes = 0;
    int nRBytes = 0;

    while ((nRBytes = XFile_Read(pIn, pBlock, pIn->nBlockSize)) > 0)
    {
        int nWBytes = XFile_Write(pOut, pBlock, nRBytes);
        if (nWBytes != nRBytes) break;
        nTotalBytes += nWBytes;
    }

    free(pBlock);
    return nTotalBytes;
}

int XFile_GetLine(xfile_t *pFile, char* pLine, size_t nSize)
{
    XASSERT((pLine && nSize), XSTDINV);
    pLine[0] = '\0';

    XASSERT(XFile_IsOpen(pFile), XSTDERR);
    int nAvail = (int)nSize - 1;
    int nRead = 0;
    char cByte;

    while (nRead < nAvail)
    {
        if (XFile_Read(pFile, &cByte, sizeof(char)) <= 0) break;
        pLine[nRead++] = cByte;
        pLine[nRead] = '\0';
        if (pLine[nRead-1] == '\n') break;
    }

    return nRead;
}

int XFile_GetLineCount(xfile_t *pFile)
{
    char sLine[XLINE_MAX];
    int nLineNum = 0;

    XASSERT((XFile_GetStats(pFile) > 0), XSTDERR);
    while (XFile_GetLine(pFile, sLine, sizeof(sLine)) > 0) nLineNum++;

    return nLineNum;
}

int XFile_ReadLine(xfile_t *pFile, char* pLine, size_t nSize, size_t nLineNum)
{
    size_t nRet, nLine = 0;

    while (XTRUE)
    {
        nRet = XFile_GetLine(pFile, pLine, nSize);
        if (nRet <= 0) return XSTDERR;
        if (++nLine == nLineNum) return (int)nRet;
    }

    return XSTDERR;
}

xbool_t XPath_Exists(const char *pPath)
{
    if (!xstrused(pPath)) return XFALSE;
    xstat_t statbuf;

    memset(&statbuf, 0, sizeof(xstat_t));
    return (stat(pPath, &statbuf) < 0) ? XFALSE : XTRUE;
}

char XPath_GetType(xmode_t nMode)
{
#ifdef S_IFMT
    switch (nMode & S_IFMT)
    {
#ifdef S_IFREG        
        case S_IFREG: return '-';
#endif
#ifdef S_IFBLK
        case S_IFBLK: return 'b';
#endif
#ifdef S_IFCHR
        case S_IFCHR: return 'c';
#endif
#ifdef S_IFDIR
        case S_IFDIR: return 'd';
#endif
#ifdef S_IFIFO
        case S_IFIFO: return 'p';
#endif
#ifdef S_IFLNK
        case S_IFLNK: return 'l';
#endif
#ifdef S_IFSOCK
        case S_IFSOCK: return 's';
#endif
        default: break;
    }
#endif
    return '?';
}

xfile_type_t XFile_GetType(xmode_t nMode)
{
    char type = XPath_GetType(nMode);

    switch (type)
    {
        case '-':
            return XF_REGULAR;
        case 'b':
            return XF_BLOCK_DEVICE;
        case 'c':
            return XF_CHAR_DEVICE;
        case 'd':
            return XF_DIRECTORY;
        case 'p':
            return XF_PIPE;
        case 'l':
            return XF_SYMLINK;
        case 's':
            return XF_SOCKET;
        default: break;
    }

    return 0; // Unknown file format
}

char XFile_GetTypeChar(xfile_type_t eType)
{
    switch (eType)
    {
        case XF_REGULAR:
            return '-';
        case XF_BLOCK_DEVICE:
            return 'b';
        case XF_CHAR_DEVICE:
            return 'c';
        case XF_DIRECTORY:
            return 'd';
        case XF_PIPE:
            return 'p';
        case XF_SYMLINK:
            return 'l';
        case XF_SOCKET:
            return 's';
        default: break;
    }

    return '?'; // Unknown file format
}

int XPath_Parse(xpath_t *pPath, const char *pPathStr, xbool_t bStat)
{
    XASSERT(pPath, XSTDINV);
    pPath->sPath[0] = XSTR_NUL;
    pPath->sFile[0] = XSTR_NUL;

    XASSERT((xstrused(pPathStr)), XSTDERR);
    size_t nLength = strlen(pPathStr);
    xbool_t bIsDir = XFALSE;

    if (bStat)
    {
        xstat_t st;
        if (xstat(pPathStr, &st) >= 0)
            bIsDir = S_ISDIR(st.st_mode);
    }

    if (bIsDir || pPathStr[nLength - 1] == '/')
        return (int)xstrncpy(pPath->sPath, sizeof(pPath->sPath), pPathStr);

    xarray_t *pArr = xstrsplit(pPathStr, "/");
    if (pArr == NULL) return (int)xstrncpy(pPath->sFile, sizeof(pPath->sFile), pPathStr);

    if (pPathStr[0] == '/')
    {
        pPath->sPath[0] = '/';
        pPath->sPath[1] = XSTR_NUL;
    }

    size_t i, nUsed = XArray_Used(pArr);
    size_t nAvail = sizeof(pPath->sPath);

    for (i = 0; i < nUsed; i++)
    {
        const char *pEntry = (const char*)XArray_GetData(pArr, i);
        if (pEntry == NULL) continue;

        if (i + 1 < nUsed)
        {
            nAvail = xstrncatf(pPath->sPath, nAvail, "%s/", pEntry);
            continue;
        }

        return (int)xstrncpy(pPath->sFile, sizeof(pPath->sFile), pEntry);
    }

    return XSTDNON;
}

int XPath_PermToMode(const char *pPerm, xmode_t *pMode)
{
    *pMode = 0;

    size_t nPermLen = strnlen(pPerm, XPERM_LEN);
    if (nPermLen < XPERM_LEN) return XSTDERR;

#ifndef _WIN32
    *pMode |= pPerm[0] == 'r' ? S_IRUSR : 0;
    *pMode |= pPerm[1] == 'w' ? S_IWUSR : 0;
    *pMode |= pPerm[2] == 'x' ? S_IXUSR : 0;

    *pMode |= pPerm[3] == 'r' ? S_IRGRP : 0;
    *pMode |= pPerm[4] == 'w' ? S_IWGRP : 0;
    *pMode |= pPerm[5] == 'x' ? S_IXGRP : 0;

    *pMode |= pPerm[6] == 'r' ? S_IROTH : 0;
    *pMode |= pPerm[7] == 'w' ? S_IWOTH : 0;
    *pMode |= pPerm[8] == 'x' ? S_IXOTH : 0;
#else
    *pMode |= pPerm[0] == 'r' ? _S_IREAD : 0;
    *pMode |= pPerm[1] == 'w' ? _S_IWRITE : 0;
#endif
    return XSTDOK;
}

int XPath_ModeToChmod(char *pOutput, size_t nSize, xmode_t nMode)
{
    int nOwner, nGroup, nOthers;
    nOwner = nGroup = nOthers = 0;

#ifndef _WIN32
    nOwner = (nMode & S_IRUSR) ? 4 : 0;
    nOwner += (nMode & S_IWUSR) ? 2 : 0;
    nOwner += (nMode & S_IXUSR) ? 1 : 0;

    nGroup = (nMode & S_IRGRP) ? 4 : 0;
    nGroup += (nMode & S_IWGRP) ? 2 : 0;
    nGroup += (nMode & S_IXGRP) ? 1 : 0;

    nOthers = (nMode & S_IROTH) ? 4 : 0;
    nOthers += (nMode & S_IWOTH) ? 2 : 0;
    nOthers += (nMode & S_IXOTH) ? 1 : 0;
#else
    nOwner = (nMode & _S_IREAD) ? 4 : 0;
    nOwner += (nMode & _S_IWRITE) ? 2 : 0;
#endif

    return (int)xstrncpyf(pOutput, nSize, "%d%d%d", nOwner, nGroup, nOthers);
}

int XPath_ModeToPerm(char *pOutput, size_t nSize, xmode_t nMode)
{
    pOutput[0] = '\0';
    if (nSize < XPERM_LEN + 1) return 0;
 
#ifndef _WIN32
    pOutput[0] = (nMode & S_IRUSR) ? 'r' : '-';
    pOutput[1] = (nMode & S_IWUSR) ? 'w' : '-';
    pOutput[2] = (nMode & S_IXUSR) ? 'x' : '-';

    pOutput[3] = (nMode & S_IRGRP) ? 'r' : '-';
    pOutput[4] = (nMode & S_IWGRP) ? 'w' : '-';
    pOutput[5] = (nMode & S_IXGRP) ? 'x' : '-';

    pOutput[6] = (nMode & S_IROTH) ? 'r' : '-';
    pOutput[7] = (nMode & S_IWOTH) ? 'w' : '-';
    pOutput[8] = (nMode & S_IXOTH) ? 'x' : '-';
#else
    pOutput[0] = (nMode & _S_IREAD) ? 'r' : '-';
    pOutput[1] = (nMode & _S_IWRITE) ? 'w' : '-';
#endif

    pOutput[XPERM_LEN] = 0;
    return XPERM_LEN;   
}

int XPath_SetPerm(const char *pPath, const char *pPerm)
{
    xmode_t nMode = 0;
    if (!XPath_PermToMode(pPerm, &nMode)) return XSTDERR;
    return (xchmod(pPath, nMode) < 0) ? XSTDERR : XSTDOK;
}

int XPath_GetPerm(char *pOutput, size_t nSize, const char *pPath)
{
    xstat_t statbuf;
    xstat(pPath, &statbuf);
    return XPath_ModeToPerm(pOutput, nSize, statbuf.st_mode);
}

long XPath_GetSize(const char *pPath)
{
    xfile_t srcFile;

    if (XFile_Open(&srcFile, pPath, NULL, NULL) >= 0)
    {
        XFile_GetStats(&srcFile);
        size_t nSize = srcFile.nSize;

        XFile_Close(&srcFile);
        return (long)nSize;
    }

    return XSTDERR;
}

int XPath_CopyFile(const char *pSrc, const char *pDst)
{
    xfile_t srcFile;

    if (XFile_Open(&srcFile, pSrc, NULL, NULL) >= 0)
    {
        int nRet = XSTDERR;
        xfile_t dstFile;

        if (XFile_Open(&dstFile, pDst, "cwt", NULL) >= 0)
        {
            nRet = XFile_Copy(&srcFile, &dstFile);
            XFile_Close(&dstFile);
        }

        XFile_Close(&srcFile);
        return nRet;
    }

    return XSTDERR;
}

int XPath_Read(const char *pPath, uint8_t *pBuffer, size_t nSize)
{
    xfile_t file;
    if (XFile_Open(&file, pPath, NULL, NULL) < 0) return XSTDERR;

    int nBytes = XFile_Read(&file, pBuffer, nSize);
    size_t nTermPosit = (nBytes > 0) ? nBytes : 0;
    pBuffer[nTermPosit] = '\0';

    XFile_Close(&file);
    return nBytes;
}

uint8_t* XPath_Load(const char *pPath, size_t* pSize)
{
    if (pSize) *pSize = 0;
    xfile_t file;

    if (XFile_Open(&file, pPath, NULL, NULL) < 0) return NULL;
    uint8_t *pData = XFile_Load(&file, pSize);

    XFile_Close(&file);
    return pData;
}

uint8_t* XPath_LoadSize(const char *pPath, size_t nMaxSize, size_t* pSize)
{
    if (pSize) *pSize = 0;
    xfile_t file;

    if (XFile_Open(&file, pPath, NULL, NULL) < 0) return NULL;
    uint8_t *pData = XFile_LoadSize(&file, nMaxSize, pSize);

    XFile_Close(&file);
    return pData;
}

size_t XPath_LoadBuffer(const char *pPath, xbyte_buffer_t *pBuffer)
{
    if (pPath == NULL || pBuffer == NULL) return 0;
    XByteBuffer_Init(pBuffer, XSTDNON, XFALSE);
    size_t nSize = 0;

    uint8_t* pData = XPath_Load(pPath, &nSize);
    if (pData != NULL)
    {
        pBuffer->nSize = nSize + 1;
        pBuffer->nUsed = nSize;
        pBuffer->pData = pData;
    }

    return nSize;
}

size_t XPath_LoadBufferSize(const char *pPath, xbyte_buffer_t *pBuffer, size_t nMaxSize)
{
    if (pPath == NULL || pBuffer == NULL) return 0;
    XByteBuffer_Init(pBuffer, XSTDNON, XFALSE);
    size_t nSize = 0;

    uint8_t* pData = XPath_LoadSize(pPath, nMaxSize, &nSize);
    if (pData != NULL)
    {
        pBuffer->nSize = nSize + 1;
        pBuffer->nUsed = nSize;
        pBuffer->pData = pData;
    }

    return nSize;
}

int XPath_Write(const char *pPath, const char *pFlags, const uint8_t *pData, size_t nSize)
{
    if (pPath == NULL || pData == NULL || !nSize) return XSTDERR;

    xfile_t file;
    if (XFile_Open(&file, pPath, pFlags, NULL) < 0) return XSTDERR;

    int nLeft = (int)nSize;
    int nDone = 0;

    while (nLeft > 0)
    {
        int nBytes = XFile_Write(&file, &pData[nDone], nSize);
        if (nBytes <= 0)
        {
            XFile_Close(&file);
            return nDone;
        }

        nDone += nBytes;
        nLeft -= nBytes;
    }

    XFile_Close(&file);
    return nDone;
}

int XPath_WriteBuffer(const char *pPath, const char *pFlags, xbyte_buffer_t *pBuffer)
{
    if (pPath == NULL || pBuffer == NULL) return XSTDERR;
    return XPath_Write(pPath, pFlags, pBuffer->pData, pBuffer->nUsed);
}

int XDir_Open(xdir_t *pDir, const char *pPath)
{
    pDir->nOpen = XSTDNON;
    pDir->pPath = pPath;
    pDir->nFirstFile = 0;
    pDir->pDirectory = NULL;
    pDir->pCurrEntry = NULL;
    if (pPath == NULL) return XSTDERR;

#ifdef _WIN32
    pDir->pDirectory = FindFirstFile(pPath, &pDir->entry);
    if (pDir->pDirectory == INVALID_HANDLE_VALUE) return XSTDERR;
    pDir->nFirstFile = 1;
#else
    pDir->pDirectory = opendir(pPath);
    if (pDir->pDirectory == NULL) return XSTDERR;
    pDir->pEntry = NULL;
#endif

    pDir->nOpen = 1;
    return XSTDOK;
}

void XDir_Close(xdir_t *pDir)
{
    if (pDir->nOpen && pDir->pDirectory)
    {
#ifdef _WIN32
        FindClose(pDir->pDirectory);
#else
        closedir(pDir->pDirectory);
        pDir->pEntry = NULL;
#endif
        pDir->pDirectory = NULL;
        pDir->pCurrEntry = NULL;
        pDir->nFirstFile = 0;
        pDir->nOpen = 0;
    }
}

int XDir_Read(xdir_t *pDir, char *pFile, size_t nSize)
{
    if (!pDir || !pDir->nOpen) return XSTDERR;

#ifdef _WIN32
    if (pDir->nFirstFile)
    {
        pDir->nFirstFile = 0;

        if (strcmp(".", pDir->entry.cFileName) &&
            strcmp("..", pDir->entry.cFileName))
        {
            if (pFile != NULL && nSize > 0)
                xstrncpy(pFile, nSize, pDir->entry.cFileName);

            pDir->pCurrEntry = pDir->entry.cFileName;
            return XSTDOK;
        }
    }

    while (FindNextFile(pDir->pDirectory, &pDir->entry))
    {
        /* Found an entry, but ignore . and .. */
        if (!strcmp(".", pDir->entry.cFileName) ||
            !strcmp("..", pDir->entry.cFileName))
            continue;

        if (pFile != NULL && nSize > 0)
            xstrncpy(pFile, nSize, pDir->entry.cFileName);

        pDir->pCurrEntry = pDir->entry.cFileName;
        return XSTDOK;
    }
#else
    while((pDir->pEntry = readdir(pDir->pDirectory)) != NULL) 
    {
        /* Found an entry, but ignore . and .. */
        if (strcmp(".", pDir->pEntry->d_name) == 0 ||
            strcmp("..", pDir->pEntry->d_name) == 0)
            continue;

        if (pFile != NULL && nSize > 0)
            strncpy(pFile, pDir->pEntry->d_name, nSize);

        pDir->pCurrEntry = pDir->pEntry->d_name;
        return XSTDOK;
    }
#endif

    return XSTDNON;
}

int XDir_Valid(const char *pPath)
{
    xstat_t statbuf = {0};
    int nStatus = stat(pPath, &statbuf);
    if (nStatus < 0) return nStatus;

    nStatus = S_ISDIR(statbuf.st_mode);
    if (!nStatus) errno = ENOTDIR;
    return nStatus;
}

int XDir_Make(char *pPath, xmode_t mode)
{
    if ((XPath_Exists(pPath) == 0) &&
        (xmkdir(pPath, mode) < 0) &&
        (errno != EEXIST)) return 0;

    return 1;
}

int XDir_Create(const char *pDir, xmode_t nMode)
{
    if (XPath_Exists(pDir)) return 1;
    char sDir[XPATH_MAX];
    int nStatus = 0;

    size_t nLen = xstrncpyf(sDir, sizeof(sDir), "%s", pDir);
    if (!nLen) return nStatus;

    if (sDir[nLen-1] == '/') sDir[nLen-1] = 0;
    char *pOffset = NULL;

    for (pOffset = sDir + 1; *pOffset; pOffset++)
    {
        if (*pOffset == '/')
        {
            *pOffset = 0;
            nStatus = XDir_Make(sDir, nMode);
            if (nStatus <= 0) return nStatus;
            *pOffset = '/';
        }
    }

    return XDir_Make(sDir, nMode);
}

int XPath_Remove(const char *pPath)
{
    xstat_t statbuf;
    if (!stat(pPath, &statbuf))
    {
        return (S_ISDIR(statbuf.st_mode)) ? 
            XDir_Remove(pPath) : xunlink(pPath);
    }

    return XSTDERR;
}

int XDir_Remove(const char *pPath)
{
    size_t nLength = strlen(pPath);
    int nStatus = XSTDERR;
    xdir_t dir;

    if (XDir_Open(&dir, pPath) > 0)
    {
        while (XDir_Read(&dir, NULL, 0) > 0)
        {
            size_t nSize = nLength + strlen(dir.pCurrEntry) + 2;
            char *pNewPath = (char*)malloc(nSize);

            if (pNewPath == NULL)
            {
                XDir_Close(&dir);
                return XSTDERR;
            }

            size_t nLen = xstrncpyf(pNewPath, nSize, "%s/%s", pPath, dir.pCurrEntry);
            if (nLen > 0) nStatus = XPath_Remove(pNewPath);
            free(pNewPath);
        }

        XDir_Close(&dir);
        xrmdir(pPath);
    }

    return nStatus;
}

///////////////////////////////////////////////////////////////////////////////////////////////
// Advanced file search implementation
///////////////////////////////////////////////////////////////////////////////////////////////

void XFile_InitEntry(xfile_entry_t *pEntry)
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

void XFile_CreateEntry(xfile_entry_t *pEntry, const char *pName, const char *pPath, xstat_t *pStat)
{
    XFile_InitEntry(pEntry);

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

xfile_entry_t* XFile_AllocEntry()
{
    xfile_entry_t *pEntry = (xfile_entry_t*)malloc(sizeof(xfile_entry_t));
    if (pEntry == NULL) return NULL;

    XFile_InitEntry(pEntry);
    return pEntry;
}

xfile_entry_t* XFile_NewEntry(const char *pName, const char *pPath, xstat_t *pStat)
{
    xfile_entry_t *pEntry = (xfile_entry_t*)malloc(sizeof(xfile_entry_t));
    if (pEntry == NULL) return NULL;

    XFile_CreateEntry(pEntry, pName, pPath, pStat);
    return pEntry;
}

void XFile_FreeEntry(xfile_entry_t *pEntry)
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

static int XFile_ErrorCallback(xfile_search_t *pSearch, const char *pStr, ...)
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

static int XFile_SearchCallback(xfile_search_t *pSearch, xfile_entry_t *pEntry)
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
        XFile_FreeEntry(pEntry);
        return XSTDERR;
    }

    XFile_FreeEntry(pEntry);
    return XSTDNON;
}

static xbool_t XFile_SearchTokens(xarray_t *pTokens, const char *pName, size_t nLength)
{
    size_t nUsed = XArray_Used(pTokens);
    if (!nUsed) return XFALSE;
    size_t i, nOffset = 0;

    for (i = 0; i < nUsed; i++)
    {
        const char *pTok = (const char*)XArray_GetData(pTokens, i);
        if (xstrused(pTok))
        {
            int nPosit = xstrnsrc(pName, nLength, pTok, nOffset);
            if (nPosit < 0) return XFALSE;
            nOffset += nPosit;
        }
    }

    return XTRUE;
}

static xbool_t XFile_SearchMulty(xarray_t *pTokens, const char *pName, size_t nLength)
{
    size_t i, nCount = XArray_Used(pTokens);
    xbool_t bFound = XFALSE;

    for (i = 0; i < nCount; i++)
    {
        xarray_data_t *pArrData = XArray_Get(pTokens, i);
        if (pArrData == NULL || pArrData->pData == NULL) continue;

        bFound = pArrData->nKey != XSTDOK ?
            !strncmp((const char *)pArrData->pData, pName, nLength) :
            XFile_SearchTokens((xarray_t*)pArrData->pData, pName, nLength);

        if (bFound) break;
    }

    return bFound;
}

static xbool_t XFile_SearchName(xfile_search_t *pSearch, const char *pFileName)
{
    size_t nLength = strlen(pFileName);
    xbool_t bFound = pSearch->bMulty ?
        XFile_SearchMulty(pSearch->pTokens, pFileName, nLength) :
        XFile_SearchTokens(pSearch->pTokens, pFileName, nLength);

    return bFound;
}

static XSTATUS XFile_SearchLines(xfile_search_t *pSearch, xsearch_context_t *pCtx)
{
    XSTATUS nStatus = XSTDNON;
    xbyte_buffer_t *pBuffer = pCtx->pBuffer;
    xarray_t *pArr = xstrsplite((char*)pBuffer->pData, XSTR_NEW_LINE);

    if (pArr != NULL)
    {
        size_t i = 0;
        for (i = 0; i < pArr->nUsed; i++)
        {
            char *pLine = (char*)XArray_GetData(pArr, i);
            if (xstrsrc(pLine, pSearch->sText) >= 0)
            {
                xfile_entry_t *pEntry = XFile_NewEntry(pCtx->pName, pCtx->pPath, pCtx->pStat);
                if (pEntry == NULL)
                {
                    XFile_ErrorCallback(pSearch, "Failed to alloc entry: %s", pCtx->pPath);
                    XArray_Destroy(pArr);
                    return XSTDERR;
                }

                xstrncpy(pEntry->sLine, sizeof(pEntry->sLine), pLine);
                pEntry->nLineNum = i + 1;
                nStatus = XSTDOK;

                if (XFile_SearchCallback(pSearch, pEntry) < 0)
                {
                    XArray_Destroy(pArr);
                    return XSTDERR;
                }
            }
        }
    }

    if (nStatus == XSTDNON)
    {
        xfile_entry_t *pEntry = XFile_NewEntry(pCtx->pName, pCtx->pPath, pCtx->pStat);
        if (pEntry == NULL)
        {
            XFile_ErrorCallback(pSearch, "Failed to alloc entry: %s", pCtx->pPath);
            XArray_Destroy(pArr);
            return XSTDERR;
        }

        xstrncpy(pEntry->sLine, sizeof(pEntry->sLine), "Binary file matches");
        if (XFile_SearchCallback(pSearch, pEntry) < 0)
        {
            XArray_Destroy(pArr);
            return XSTDERR;
        }
    }

    XArray_Destroy(pArr);
    return XSTDNON;
}

static XSTATUS XFile_SearchBuffer(xfile_search_t *pSearch, xsearch_context_t *pCtx)
{
    xbyte_buffer_t *pBuffer = pCtx->pBuffer;
    XASSERT((pBuffer && pBuffer->pData), XSTDNON);

    const char *pData = (char*)pBuffer->pData;
    XSTATUS nStatus = XSTDNON;

    while (pCtx->nPosit < (int)pBuffer->nUsed)
    {
        while (pCtx->nPosit > 1 && pData[pCtx->nPosit] != '\n') pCtx->nPosit--;
        if (pData[pCtx->nPosit] == '\n') pCtx->nPosit++;

        int nEnd = xstrsrc(&pData[pCtx->nPosit], "\n");
        if (nEnd <= 0 || (pCtx->nPosit + nEnd) > (int)pBuffer->nUsed) break;

        char sLine[XSTR_MAX];
        xstrncpys(sLine, sizeof(sLine), &pData[pCtx->nPosit], nEnd);

        xfile_entry_t *pEntry = XFile_NewEntry(pCtx->pName, pCtx->pPath, pCtx->pStat);
        if (pEntry == NULL)
        {
            XFile_ErrorCallback(pSearch, "Failed to alloc entry: %s", pCtx->pPath);
            return XSTDERR;
        }

        xstrncpy(pEntry->sLine, sizeof(pEntry->sLine), sLine);
        if (XFile_SearchCallback(pSearch, pEntry) < 0) return XSTDERR;

        nStatus = XSTDOK;
        pCtx->nPosit += nEnd;
        if (pCtx->nPosit >= (int)pBuffer->nUsed) break;

        int nNewPosit = xstrsrc(&pData[pCtx->nPosit], pSearch->sText);
        if (nNewPosit < 0) break;
        pCtx->nPosit += nNewPosit;
    }

    if (nStatus == XSTDNON)
    {
        xfile_entry_t *pEntry = XFile_NewEntry(pCtx->pName, pCtx->pPath, pCtx->pStat);
        if (pEntry == NULL)
        {
            XFile_ErrorCallback(pSearch, "Failed to alloc entry: %s", pCtx->pPath);
            return XSTDERR;
        }

        xstrncpy(pEntry->sLine, sizeof(pEntry->sLine), "Binary file matches");
        if (XFile_SearchCallback(pSearch, pEntry) < 0) return XSTDERR;
    }

    return XSTDNON;
}

static int XFile_CheckCriteria(xfile_search_t *pSearch, const char *pPath, const char *pName, xstat_t *pStat)
{
    if (pSearch->nLinkCount >= 0 && pSearch->nLinkCount != pStat->st_nlink) return XSTDNON;
    if (pSearch->nFileSize >= 0 && pSearch->nFileSize != pStat->st_size) return XSTDNON;
    if (pSearch->nMaxSize > 0 && pSearch->nMaxSize < pStat->st_size) return XSTDNON;

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

    if (xstrused(pSearch->pFileName))
    {
        char sName[XPATH_MAX];
        if (pSearch->bInsensitive) xstrncase(sName, sizeof(sName), XSTR_LOWER, pName);
        const char *pSearchName = pSearch->bInsensitive ? (const char *)sName : pName;

        xbool_t bFound = pSearch->pTokens != NULL ?
            XFile_SearchName(pSearch, pSearchName) :
            !strcmp(pSearch->pFileName, pSearchName);

        if (!bFound && !xstrused(pSearch->sText)) return XSTDNON;
    }

    XSTATUS nStatus = XSTDOK;
    int nPosit = XSTDERR;

    if (xstrused(pSearch->sText))
    {
        xfile_type_t eType = XFile_GetType(pStat->st_mode);
        if (eType != XF_REGULAR) return XSTDNON;

        xbyte_buffer_t buffer;
        char sPath[XPATH_MAX];

        xstrncpyf(sPath, sizeof(sPath), "%s%s", pPath, pName);
        XPath_LoadBufferSize(sPath, &buffer, pSearch->nMaxRead);
        if (buffer.pData == NULL) return XSTDNON;

        if (pSearch->bInsensitive) xstrcase((char*)buffer.pData, XSTR_LOWER);
        size_t nTextLen = strlen(pSearch->sText);

        void *pOffset = memmem(buffer.pData, buffer.nUsed, pSearch->sText, nTextLen);
        if (pOffset != NULL) nPosit = (int)((uint8_t *)pOffset - buffer.pData);

        if (nPosit < 0 || nPosit >= (int)buffer.nUsed)
        {
            XByteBuffer_Clear(&buffer);
            return XSTDNON;
        }

        if (!pSearch->bFilesOnly)
        {
            xsearch_context_t ctx;
            ctx.pBuffer = &buffer;
            ctx.nPosit = nPosit;
            ctx.pPath = pPath;
            ctx.pName = pName;
            ctx.pStat = pStat;

            nStatus = pSearch->bSearchLines ?
                XFile_SearchLines(pSearch, &ctx) :
                XFile_SearchBuffer(pSearch, &ctx);
        }

        XByteBuffer_Clear(&buffer);
    }

    return nStatus;
}

void XFile_SearchClearCb(xarray_data_t *pArrData)
{
    if (pArrData == NULL || pArrData->pData == NULL) return;
    else if (!pArrData->nKey) free(pArrData->pData);
    else XArray_Destroy((xarray_t*)pArrData->pData);
}

static xarray_t* XFile_TokenizeName(xfile_search_t *pSrcCtx, const char *pFileName)
{
    xarray_t *pTokens = NULL;
    if (xstrsrc(pFileName, "*") >= 0) return xstrsplit(pFileName, "*");
    if (xstrsrc(pFileName, ";") >= 0) pTokens = xstrsplit(pFileName, ";");

    if (pTokens == NULL) return NULL;
    size_t i, nUsed = XArray_Used(pTokens);
    pSrcCtx->bMulty = XTRUE;

    for (i = 0; i < nUsed; i++)
    {
        char *pToken = XArray_GetData(pTokens, i);
        if (xstrsrc(pToken, "*") >= 0)
        {
            xarray_t *pSubTokens = xstrsplit(pToken, "*");
            if (pSubTokens != NULL)
            {
                xarray_data_t *pArrData = XArray_Get(pTokens, i);
                pArrData->pData = pSubTokens;
                pArrData->nKey = XSTDOK;

                pTokens->clearCb = XFile_SearchClearCb;
                free(pToken);
            }
        }
    }

    return pTokens;
}

void XFile_SearchInit(xfile_search_t *pSrcCtx, const char *pFileName)
{
    pSrcCtx->bInsensitive = XFALSE;
    pSrcCtx->bSearchLines = XFALSE;
    pSrcCtx->bFilesOnly = XFALSE;
    pSrcCtx->bRecursive = XFALSE;
    pSrcCtx->bMulty = XFALSE;
    pSrcCtx->callback = NULL;
    pSrcCtx->pUserCtx = NULL;
    pSrcCtx->nInterrupted = 0;

    pSrcCtx->pInterrupted = &pSrcCtx->nInterrupted;
    pSrcCtx->pTokens = XFile_TokenizeName(pSrcCtx, pFileName);

    pSrcCtx->pFileName = pFileName;
    pSrcCtx->sText[0] = XSTR_NUL;
    pSrcCtx->nPermissions = 0;
    pSrcCtx->nFileTypes = 0;
    pSrcCtx->nLinkCount = -1;
    pSrcCtx->nFileSize = -1;
    pSrcCtx->nMaxRead = 0;
    pSrcCtx->nMaxSize = 0;

    XArray_InitPool(&pSrcCtx->fileArray, XSTDNON, XSTDNON, XFALSE);
    pSrcCtx->fileArray.clearCb = XFile_ArrayClearCb;
}

void XFile_SearchDestroy(xfile_search_t *pSrcCtx)
{
    if (pSrcCtx == NULL) return;
    XSYNC_ATOMIC_SET(pSrcCtx->pInterrupted, 1);

    if (pSrcCtx->pTokens != NULL)
    {
        XArray_Destroy(pSrcCtx->pTokens);
        pSrcCtx->pTokens = NULL;
    }

    pSrcCtx->fileArray.clearCb = XFile_ArrayClearCb;
    XArray_Destroy(&pSrcCtx->fileArray);
}

xfile_entry_t* XFile_GetEntry(xfile_search_t *pSearch, int nIndex)
{
    if (nIndex >= XArray_Used(&pSearch->fileArray)) return NULL;
    return (xfile_entry_t*)XArray_GetData(&pSearch->fileArray, nIndex);
}

int XFile_Search(xfile_search_t *pSearch, const char *pDirectory)
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

    if (xstrused(pSearch->sText) && pSearch->bInsensitive)
        xstrcase(pSearch->sText, XSTR_LOWER);

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
            xfile_entry_t *pEntry = XFile_NewEntry(pEntryName, sDirPath, &statbuf);
            if (pEntry == NULL)
            {
                XFile_ErrorCallback(pSearch, "Failed to alloc entry: %s", sFullPath);
                XDir_Close(&dirHandle);
                return XSTDERR;
            }

            if (XFile_SearchCallback(pSearch, pEntry) < 0)
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
            XFile_Search(pSearch, sFullPath) < 0)
        {
            XDir_Close(&dirHandle);
            return XSTDERR;
        }
    }

    XDir_Close(&dirHandle);
    return XSTDOK;
}