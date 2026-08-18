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

#define XFILE_BUF_SIZE      4096
#define XFILE_FLAGS_LEN     10
#define XFILE_DEFAULT_PERM  "rw-r--r--"
#define XPATH_DIR_MODE      0755

/* Copying is the one operation where the filesystem block size is the wrong
   unit: it costs a read and a write syscall per few kilobytes. */
#define XFILE_COPY_BUF_SIZE (256 * 1024)

int xchmod(const char* pPath, xmode_t nMode)
{
#ifdef _WIN32
    return _chmod(pPath, nMode);
#else
    return chmod(pPath, nMode);
#endif
}

int xchown(const char* pPath, const char* pUser, const char* pGroup)
{
#ifdef _WIN32
    /* No-op on Windows */
    (void)pPath;
    (void)pUser;
    (void)pGroup;
    return XSTDNON;
#else
    struct passwd *pUserEntry = getpwnam(pUser);
    XCHECK((pUserEntry != NULL), XSTDERR);

    struct group *pGroupEntry = getgrnam(pGroup);
    XCHECK((pGroupEntry != NULL), XSTDERR);

    int nStatus = chown(pPath, pUserEntry->pw_uid, pGroupEntry->gr_gid);
    return nStatus < 0 ? XSTDERR : XSTDOK;
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
    if (pFlags == NULL) return 0;

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
            /* Not inherited by a child process: O_CLOEXEC and _O_NOINHERIT
               are the same guarantee spelled differently. */
#ifdef _WIN32
#ifdef _O_NOINHERIT
            case 'i': { nFlags |= _O_NOINHERIT; break; }
#endif
#elif defined(O_CLOEXEC)
            case 'i': { nFlags |= O_CLOEXEC; break; }
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
    if (XFILE_XCHECK_FL(nFlags, O_RDONLY) &&
        XFILE_XCHECK_FL(nFlags, O_WRONLY))
    {
        nFlags &= ~O_RDONLY;
        nFlags &= ~O_WRONLY;
        nFlags |= O_RDWR;
    }
#endif

    return nFlags;
}

/* The Windows CRT opens files in text mode by default, silently translating
   LF<->CRLF and treating Ctrl-Z as end of file. That corrupts any binary
   payload, so every file goes through this: callers that need text semantics
   handle line endings themselves.

   It lives here rather than in the flag parser because a read-only open parses
   to no bits at all - _O_RDONLY is zero - so the parser cannot tell "read
   only" from "no flags given", and both used to end up in text mode. Reading
   a binary file that way stops at the first 0x1A byte. */
static int XFile_BinaryFlags(int nFlags)
{
#ifdef _O_BINARY
    return nFlags | _O_BINARY;
#else
    return nFlags;
#endif
}

int XFile_OpenM(xfile_t *pFile, const char *pPath, const char *pFlags, xmode_t nMode)
{
    if (pFile == NULL || pPath == NULL) return XSTDERR;
    pFile->nFlags = XFile_BinaryFlags(XFile_ParseFlags(pFlags));
    pFile->nBlockSize = XFILE_BUF_SIZE;
    pFile->nMode = nMode;
    pFile->bEOF = XFALSE;
    pFile->nSize = 0;
    pFile->nFD = -1;

#ifdef _WIN32
    _sopen_s(&pFile->nFD, pPath, pFile->nFlags, _SH_DENYNO, pFile->nMode);
#else
    pFile->nFD = open(pPath, pFile->nFlags, pFile->nMode);
#endif

    pFile->nPosit = pFile->nAlloc = 0;
    return pFile->nFD;
}

int XFile_Open(xfile_t *pFile, const char *pPath, const char *pFlags, const char *pPerms)
{
    if (pFile == NULL || pPath == NULL) return XSTDERR;
    pFile->nFlags = XFile_BinaryFlags(XFile_ParseFlags(pFlags));
    pFile->nBlockSize = XFILE_BUF_SIZE;
    pFile->bEOF = XFALSE;
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

int XFile_Reopen(xfile_t *pFile, const char *pPath, const char *pFlags, const char *pPerms)
{
    XCHECK(pFile, XSTDERR);
    XFile_Close(pFile);

    return XFile_Open(pFile, pPath, pFlags, pPerms);
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
    XCHECK(pFile, XFALSE);
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
    XCHECK_VOID_NL((ppFile && *ppFile));
    xfile_t *pFile = *ppFile;

    XFile_Destroy(pFile);
    *ppFile = NULL;
}

size_t XFile_Seek(xfile_t *pFile, uint64_t nPosit, int nOffset)
{
    XCHECK(XFile_IsOpen(pFile), XSTDERR);
#ifdef _WIN32
    return (int)_lseek(pFile->nFD, (long)nPosit, nOffset);
#else
    return lseek(pFile->nFD, nPosit, nOffset);
#endif
}

int XFile_Write(xfile_t *pFile, const void *pBuff, size_t nSize)
{
    XCHECK(XFile_IsOpen(pFile), XSTDERR);
#ifdef _WIN32
    return _write(pFile->nFD, pBuff, (unsigned int)nSize);
#else
    return write(pFile->nFD, pBuff, nSize);
#endif
}

int XFile_Read(xfile_t *pFile, void *pBuff, size_t nSize)
{
    XCHECK(XFile_IsOpen(pFile), XSTDERR);

#ifdef _WIN32
    int nRead = _read(pFile->nFD, pBuff, (unsigned int)nSize);
#elif EINTR
    ssize_t nRead = 0;
    do nRead = read(pFile->nFD, pBuff, nSize);
    while (nRead < 0 && errno == EINTR);
#else
    ssize_t nRead = read(pFile->nFD, pBuff, nSize);
#endif

    if (nRead <= 0 && errno != EAGAIN) pFile->bEOF = XTRUE;
    return (int)nRead;
}


int XFile_Print(xfile_t *pFile, const char *pFmt, ...)
{
    va_list args;
    size_t nLength = 0;

    va_start(args, pFmt);
    char *pDest = xstracpyargs(pFmt, args, &nLength);
    va_end(args);

    XCHECK(pDest, XSTDERR);
    int nStatus = XFile_Write(pFile, pDest, nLength);

    free(pDest);
    return nStatus;
}

int XFile_GetStats(xfile_t *pFile)
{
    XCHECK(XFile_IsOpen(pFile), XSTDERR);

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
        size_t nReadSize = XSTD_MIN(pFile->nBlockSize, nFreeSpace);
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
    /* An empty source is a valid copy, not a failure: XFile_GetStats() answers
       XSTDNON for a zero-length file and only XSTDERR for a broken one. */
    XCHECK((XFile_GetStats(pIn) >= 0), XSTDERR);
    XCHECK((XFile_IsOpen(pOut)), XSTDERR);

    size_t nBufferSize = XSTD_MAX(pIn->nBlockSize, (size_t)XFILE_COPY_BUF_SIZE);
    uint8_t *pBlock = (uint8_t*)malloc(nBufferSize);

    if (pBlock == NULL && nBufferSize > pIn->nBlockSize)
    {
        /* Under memory pressure a block-sized copy is still better than none. */
        nBufferSize = pIn->nBlockSize;
        pBlock = (uint8_t*)malloc(nBufferSize);
    }

    if (pBlock == NULL) return XSTDERR;

    uint64_t nTotalBytes = 0;
    int nStatus = XSTDOK;
    int nRBytes = 0;

    while ((nRBytes = XFile_Read(pIn, pBlock, nBufferSize)) > 0)
    {
        int nOffset = 0;

        /* write() is allowed to accept less than it was handed. Treating that
           as the end of the copy and reporting the bytes that did land turned
           a full disk into a silently truncated file that looked successful,
           so a partial write is finished here and a real one fails the copy. */
        while (nOffset < nRBytes)
        {
            int nWBytes = XFile_Write(pOut, pBlock + nOffset, (size_t)(nRBytes - nOffset));

            if (nWBytes <= 0)
            {
                if (nWBytes < 0 && errno == EINTR) continue;
                nStatus = XSTDERR;
                break;
            }

            nOffset += nWBytes;
        }

        if (nStatus < 0) break;
        nTotalBytes += (uint64_t)nRBytes;
    }

    /* Zero is end of file and a negative value is a read error; a copy that
       stopped because the source could not be read has not copied the file. */
    if (nRBytes < 0) nStatus = XSTDERR;

    free(pBlock);
    if (nStatus < 0) return XSTDERR;

    /* The byte count is reported through an int for compatibility. Counting in
       one would overflow past 2 GB and land on a negative value the caller
       reads as failure, so the total is tracked in 64 bits and saturates. */
    return nTotalBytes > (uint64_t)INT_MAX ? INT_MAX : (int)nTotalBytes;
}

int XFile_GetLine(xfile_t *pFile, char* pLine, size_t nSize)
{
    XCHECK((pLine && nSize), XSTDINV);
    pLine[0] = '\0';

    XCHECK(XFile_IsOpen(pFile), XSTDERR);
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

    XCHECK((XFile_GetStats(pFile) > 0), XSTDERR);
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

int XPath_EnsureDirectory(const char *pPath)
{
    XCHECK((xstrused(pPath)), XSTDERR);

    char sDir[XPATH_MAX];
    xstrncpy(sDir, sizeof(sDir), pPath);

    char *pSlash = strrchr(sDir, '/');
    if (pSlash != NULL)
    {
        *pSlash = XSTR_NUL;
        return XDir_Create(sDir, XPATH_DIR_MODE);
    }

    return XSTDNON;
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

xbool_t XFile_IsExec(xmode_t nMode)
{
#ifdef S_IXUSR
    if (nMode & S_IXUSR) return XTRUE;
#endif
#ifdef S_IXGRP
    if (nMode & S_IXGRP) return XTRUE;
#endif
#ifdef S_IXOTH
    if (nMode & S_IXOTH) return XTRUE;
#endif
#ifdef _WIN32
    if (nMode & _S_IEXEC) return XTRUE;
#endif
    return XFALSE;
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
    XCHECK(pPath, XSTDINV);
    pPath->sPath[0] = XSTR_NUL;
    pPath->sFile[0] = XSTR_NUL;

    XCHECK((xstrused(pPathStr)), XSTDERR);
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
    xbool_t bDirectory = S_ISDIR(nMode);
    xbool_t bRead = bDirectory || (nMode & _S_IREAD);
    xbool_t bWrite = bDirectory || (nMode & _S_IWRITE);
    xbool_t bExec = bDirectory || (nMode & _S_IEXEC);

    nOwner = bRead ? 4 : 0;
    nOwner += bWrite ? 2 : 0;
    nOwner += bExec ? 1 : 0;

    nGroup = 0;
    nOthers = 0;
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
    xbool_t bDirectory = S_ISDIR(nMode);
    xbool_t bRead = bDirectory || (nMode & _S_IREAD);
    xbool_t bWrite = bDirectory || (nMode & _S_IWRITE);
    xbool_t bExec = bDirectory || (nMode & _S_IEXEC);

    pOutput[0] = bRead ? 'r' : '-';
    pOutput[1] = bWrite ? 'w' : '-';
    pOutput[2] = bExec ? 'x' : '-';

    pOutput[3] = '-';
    pOutput[4] = '-';
    pOutput[5] = '-';

    pOutput[6] = '-';
    pOutput[7] = '-';
    pOutput[8] = '-';
#endif

    pOutput[XPERM_LEN] = 0;
    return XPERM_LEN;
}

int XPath_SetPerm(const char *pPath, const char *pPerm)
{
    xmode_t nMode = 0;
    if (XPath_PermToMode(pPerm, &nMode) != XSTDOK) return XSTDERR;
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
    XCHECK((xstrused(pSrc) && xstrused(pDst)), XSTDERR);

    /* Opened non-blocking so a FIFO cannot park the caller inside open()
       until someone writes to it, then validated through the descriptor
       rather than the path: the check and the reads then apply to the same
       object, and anything that is not a regular file has no content to copy. */
    xfile_t srcFile;
    if (XFile_Open(&srcFile, pSrc, "rn", NULL) < 0) return XSTDERR;

    if (XFile_GetStats(&srcFile) < 0 || !S_ISREG(srcFile.nMode))
    {
        int nSavedErrno = S_ISREG(srcFile.nMode) ? errno : EINVAL;
        XFile_Close(&srcFile);
        errno = nSavedErrno;
        return XSTDERR;
    }

    /* Whether the destination is ours decides what happens to its permissions
       below, and it has to be answered before the file is created. */
    xbool_t bExisted = XPath_Exists(pDst);
    xmode_t nMode = srcFile.nMode;

    xfile_t dstFile;
    if (XFile_OpenM(&dstFile, pDst, "cwt", nMode & 0777) < 0)
    {
        int nSavedErrno = errno;
        XFile_Close(&srcFile);
        errno = nSavedErrno;
        return XSTDERR;
    }

    int nRet = XFile_Copy(&srcFile, &dstFile);
    int nSavedErrno = errno;

    XFile_Close(&dstFile);
    XFile_Close(&srcFile);

    if (nRet < 0)
    {
        /* A failed copy leaves no half-written file behind - but only when
           this call is what created it. */
        if (!bExisted) xunlink(pDst);
        errno = nSavedErrno;
        return XSTDERR;
    }

    /* A new destination carries the source permissions. The creation mode
       alone does not get there: open() filters it through the umask, so an
       executable would stop being executable and a private file could end up
       readable by anyone. An existing destination keeps its own permissions,
       the way a file being overwritten normally does. */
    if (!bExisted)
    {
        char sPerm[XPERM_LEN + 1];

        if (XPath_ModeToPerm(sPerm, sizeof(sPerm), nMode) == XPERM_LEN)
            XPath_SetPerm(pDst, sPerm);
    }

    return nRet;
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

int XPath_Write(const char *pPath, const uint8_t *pData, size_t nSize, const char *pFlags)
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

int XPath_WriteBuffer(const char *pPath, xbyte_buffer_t *pBuffer, const char *pFlags)
{
    if (pPath == NULL || pBuffer == NULL) return XSTDERR;
    return XPath_Write(pPath, pBuffer->pData, pBuffer->nUsed, pFlags);
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
    /* FindFirstFile() enumerates a directory only when given a wildcard
       pattern; the bare directory path would match the directory itself
       as a single entry. INVALID_HANDLE_VALUE with ERROR_FILE_NOT_FOUND
       means the pattern matched nothing: for an existing directory that
       is simply an empty listing, which XDir_Read reports as exhausted. */
    char sPattern[XPATH_MAX];
    size_t nLen = strlen(pPath);
    if (!nLen || nLen + 3 > sizeof(sPattern)) return XSTDERR;

    xbool_t bHasSep = (pPath[nLen - 1] == '/' || pPath[nLen - 1] == '\\');
    int nPrinted = snprintf(sPattern, sizeof(sPattern), bHasSep ? "%s*" : "%s\\*", pPath);
    if (nPrinted <= 0 || (size_t)nPrinted >= sizeof(sPattern)) return XSTDERR;

    pDir->pDirectory = FindFirstFile(sPattern, &pDir->entry);
    if (pDir->pDirectory == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() != ERROR_FILE_NOT_FOUND || !XDir_Valid(pPath)) return XSTDERR;
        pDir->nOpen = 1; /* Open but already exhausted: empty directory */
        return XSTDOK;
    }

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
    /* Opened but exhausted (e.g. an empty listing without a find handle) */
    if (pDir->pDirectory == NULL ||
        pDir->pDirectory == INVALID_HANDLE_VALUE) return XSTDNON;

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
            xstrncpy(pFile, nSize, pDir->pEntry->d_name);

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
    if ((XPath_Exists(pPath) == XFALSE) &&
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
    char *pRoot = sDir;

#ifdef _WIN32
    /* A drive prefix is a root, not a directory. Left in the walk, "C:" is
     * handed to mkdir as if it were a component to create, and the failure
     * stops the whole path from being created. */
    if (nLen > 2 && sDir[1] == ':' &&
        ((sDir[0] >= 'A' && sDir[0] <= 'Z') ||
         (sDir[0] >= 'a' && sDir[0] <= 'z')))
            pRoot = sDir + 2;
#endif

    for (pOffset = pRoot + 1; *pOffset; pOffset++)
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
    if (xstat(pPath, &statbuf) == XSTDOK)
    {
        return (S_ISDIR(statbuf.st_mode)) ?
            XDir_Remove(pPath) : xunlink(pPath);
    }

    return XSTDERR;
}

int XDir_Remove(const char *pPath)
{
    if (!xstrused(pPath))
    {
        errno = EINVAL;
        return XSTDERR;
    }

    size_t nLength = strlen(pPath);

    while (XTRUE)
    {
        xdir_t dir;
        if (XDir_Open(&dir, pPath) < 0) return XSTDERR;

        char sName[XNAME_MAX];
        int nRead = XDir_Read(&dir, NULL, 0);
        if (nRead > 0) xstrncpy(sName, sizeof(sName), dir.pCurrEntry);

        XDir_Close(&dir);
        if (nRead <= 0) break;

        size_t nSize = nLength + strlen(sName) + 2;
        char *pNewPath = (char*)malloc(nSize);

        if (pNewPath == NULL)
        {
            if (!errno) errno = ENOMEM;
            return XSTDERR;
        }

        size_t nLen = xstrncpyf(pNewPath, nSize, "%s/%s", pPath, sName);
        if (nLen <= 0)
        {
            free(pNewPath);
            errno = ENAMETOOLONG;
            return XSTDERR;
        }

        if (XPath_Remove(pNewPath) < 0)
        {
            int nErrno = errno;
            free(pNewPath);
            errno = nErrno;
            return XSTDERR;
        }

        free(pNewPath);
    }

    return xrmdir(pPath) == 0 ? XSTDOK : XSTDERR;
}
