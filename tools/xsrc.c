/*!
 *  @file libxutils/examples/xsrc.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2022  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Implementation of advanced file search based on the xUtils.
 */

#include "xstd.h"
#include "type.h"
#include "srch.h"
#include "str.h"
#include "log.h"
#include "xfs.h"

#define XSEARCH_VERSION_MAX     1
#define XSEARCH_VERSION_MIN     0
#define XSEARCH_BUILD_NUMBER    16

#define XSEARCH_MAX_READ_SIZE   1024 * 1024 * 1024
#define XSEARCH_INFO_LEN        128
#define XSEARCH_TIME_LEN        12
#define XSEARCH_SIZE_LEN        32

#define XSEARCH_ARG_COLORING \
    XSTR_CLR_CYAN, \
    XSTR_FMT_RESET, \
    XSTR_FMT_DIM, \
    XSTR_FMT_RESET

static xatomic_t g_interrupted = 0;
extern char *optarg;

typedef struct {
    char sDirectory[XPATH_MAX];
    char sFileName[XNAME_MAX];
    char sText[XSTR_MID];
    xbool_t bInsensitive;
    xbool_t bSearchLines;
    xbool_t bJumpSpace;
    xbool_t bReadStdin;
    xbool_t bMatchOnly;
    xbool_t bRecursive;
    xbool_t bVerbose;
    size_t nMaxRead;
    size_t nMaxSize;
    size_t nMinSize;
    size_t nMaxBuffer;
    int nPermissions;
    int nLinkCount;
    int nFileTypes;
    int nFileSize;
} xsearch_args_t;

void signal_callback(int sig)
{
    printf("\nInterrupted with signal: %d\n", sig);
    XSYNC_ATOMIC_SET(&g_interrupted, 1);
}

static int XSearch_GetFileTypes(const char *pTypes)
{
    size_t i, nLen = strlen(pTypes);
    int nTypes = 0;

    for (i = 0; i < nLen; i++)
    {
        switch (pTypes[i])
        {
            case 'b': nTypes |= XF_BLOCK_DEVICE; break;
            case 'c': nTypes |= XF_CHAR_DEVICE; break;
            case 'd': nTypes |= XF_DIRECTORY; break;
            case 'f': nTypes |= XF_REGULAR; break;
            case 'l': nTypes |= XF_SYMLINK; break;
            case 'p': nTypes |= XF_PIPE; break;
            case 's': nTypes |= XF_SOCKET; break;
            case 'x': nTypes |= XF_EXEC; break;
            default:
            {
                xloge("Invalid file type");
                return XSTDERR;
            }
        }
    }

    return nTypes;
}

static int XSearch_GetPermissins(const char *pPerm)
{
    xmode_t nMode = 0;
    if (XPath_PermToMode(pPerm, &nMode) < 0)
    {
        xloge("Invalid permissions");
        return XSTDERR;
    }

    char sChmod[32];
    XPath_ModeToChmod(sChmod, sizeof(sChmod), nMode);

    return atoi(sChmod);
}

void XSearch_Usage(const char *pName)
{
    printf("============================================================\n");
    printf(" Advanced File Search - Version: %d.%d build %d (%s)\n",
        XSEARCH_VERSION_MAX, XSEARCH_VERSION_MIN, XSEARCH_BUILD_NUMBER, __DATE__);
    printf("============================================================\n");

    int i, nLength = strlen(pName) + 6;
    char sWhiteSpace[nLength + 1];

    for (i = 0; i < nLength; i++) sWhiteSpace[i] = ' ';
    sWhiteSpace[nLength] = 0;

    printf("Usage: %s [-f <name>] [-s <size>] [-t <types>] [-g <text>]\n", pName);
    printf(" %s [-l <link_count>] [-p <permissions>] [-d <path>]\n", sWhiteSpace);
    printf(" %s [-m <max_size>] [-z <min_size>] [-b <read_buff>]\n", sWhiteSpace);
    printf(" %s [-i] [-n] [-j] [-o] [-r] [-x] [-v] [-h]\n\n", sWhiteSpace);

    printf("Options are:\n");
    printf("  %s-d%s <target_path>    %s# Target directory path%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-f%s <file_name>      %s# Target file name%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-g%s <grep_text>      %s# Search file containing the text%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-s%s <file_size>      %s# Target file size in bytes%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-l%s <link_count>     %s# Target file link count%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-p%s <permissions>    %s# Target file permissions (e.g. 'rwxr-xr--')%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-b%s <read_buffer>    %s# Max read buffer size%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-m%s <max_size>       %s# Max size of the file to search%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-z%s <min_size>       %s# Min size of the file to search%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-t%s <types>          %s# Target file types (*)%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-i%s                  %s# Case insensitive search%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-n%s                  %s# Line by line search text in file%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-j%s                  %s# Jump empty spaces while printing the line%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-o%s                  %s# In case of full search, show matches only%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-r%s                  %s# Recursive search target directory%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-x%s                  %s# Read input from stdin for text search%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-v%s                  %s# Display additional information (verbose)%s\n", XSEARCH_ARG_COLORING);
    printf("  %s-h%s                  %s# Display version and usage information%s\n\n", XSEARCH_ARG_COLORING);

    printf("File types (*):\n");
    printf("   %sb%s: block device\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sc%s: character device\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sd%s: directory\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sf%s: regular file\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sl%s: symbolic link\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sp%s: pipe\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %ss%s: socket\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);
    printf("   %sx%s: executable\n\n", XSTR_CLR_CYAN, XSTR_FMT_RESET);

    printf("Notes:\n");
    printf("   1) <file_name> option is supporting wildcard character: '%s*%s'\n", XSTR_FMT_BOLD, XSTR_FMT_RESET);
    printf("   2) <file_types> option is supporting one and more file types: %s-t ldb%s\n", XSTR_FMT_BOLD, XSTR_FMT_RESET);
    printf("   3) One or more <file_name> argument can be specified by using delimiter: '%s;%s'\n", XSTR_FMT_BOLD, XSTR_FMT_RESET);
    printf("   4) Max sizes (-m and -z) can be human readable numbers, examples: 1G, 10m, 3k, 11M\n\n");

    printf("Examples:\n");
    printf("%sRecursive search of every symlink or a regular file in the root file%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("%ssystem that has permissions 777 and contains \".log\" in the file name:%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("%s[xutils@examples]$ %s -rvd / -t lf -f \"*.log\" -p rwxrwxrwx%s\n\n", XSTR_FMT_BOLD, pName, XSTR_FMT_RESET);

    printf("%sRecursive search of every .cpp and .java file in the \"/opt\" directory%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("%sthat contains the case insensitive text \"test\" and verbose output:%s\n", XSTR_FMT_DIM, XSTR_FMT_RESET);
    printf("%s[xutils@examples]$ %s -rvd /opt -f \"*.cpp;*.java\" -ig test%s\n\n", XSTR_FMT_BOLD, pName, XSTR_FMT_RESET);
}

static int XSearch_GetSize(char *pSize, size_t *pMaxRead)
{
    if (!isdigit(*pSize)) return XSTDERR;
    size_t nSize = atoi(pSize);

    while (*pSize && isdigit(*pSize)) pSize++;
    while (*pSize && isspace(*pSize)) pSize++;

    if (*pSize)
    {
        char cSize = (char)tolower(*pSize);
        if (cSize == 'k') nSize = nSize * 1024;
        else if (cSize == 'm') nSize = nSize * 1024 * 1024;
        else if (cSize == 'g') nSize = nSize * 1024 * 1024 * 1024;
    }

    *pMaxRead = nSize;
    return XSTDOK;
}

static int XSearch_ParseArgs(xsearch_args_t *pArgs, int argc, char *argv[])
{
    memset(pArgs, 0, sizeof(xsearch_args_t));
    char sMaxReadSize[XSEARCH_SIZE_LEN] = {0};
    char sMaxFileSize[XSEARCH_SIZE_LEN] = {0};
    char sMinFileSize[XSEARCH_SIZE_LEN] = {0};
    pArgs->sDirectory[0] = '.';
    pArgs->sDirectory[1] = '/';
    pArgs->sDirectory[2] = '\0';
    pArgs->nLinkCount = -1;
    pArgs->nFileSize = -1;
    int opt = 0;

    while ((opt = getopt(argc, argv, "b:d:f:g:l:m:p:s:t:z:i1:j1:n1:o1:r1:x1:v1:h1")) != -1)
    {
        switch (opt)
        {
            case 'd':
                xstrncpy(pArgs->sDirectory, sizeof(pArgs->sDirectory), optarg);
                break;
            case 'f':
                xstrncpy(pArgs->sFileName, sizeof(pArgs->sFileName), optarg);
                break;
            case 'g':
                xstrncpy(pArgs->sText, sizeof(pArgs->sText), optarg);
                break;
            case 'b':
                xstrncpy(sMaxReadSize, sizeof(sMaxReadSize), optarg);
                break;
            case 'm':
                xstrncpy(sMaxFileSize, sizeof(sMaxFileSize), optarg);
                break;
            case 'z':
                xstrncpy(sMinFileSize, sizeof(sMinFileSize), optarg);
                break;
            case 'p':
                pArgs->nPermissions = XSearch_GetPermissins(optarg);
                break;
            case 't':
                pArgs->nFileTypes = XSearch_GetFileTypes(optarg);
                break;
            case 'l':
                pArgs->nLinkCount = atol(optarg);
                break;
            case 's':
                pArgs->nFileSize = atol(optarg);
                break;
            case 'n':
                pArgs->bSearchLines = XTRUE;
                break;
            case 'i':
                pArgs->bInsensitive = XTRUE;
                break;
            case 'j':
                pArgs->bJumpSpace = XTRUE;
                break;
            case 'o':
                pArgs->bMatchOnly = XTRUE;
                break;
            case 'r':
                pArgs->bRecursive = XTRUE;
                break;
            case 'x':
                pArgs->bReadStdin = XTRUE;
                break;
            case 'v':
                pArgs->bVerbose = XTRUE;
                break;
            case 'h':
            default:
                return 0;
        }
    }

    if (pArgs->bInsensitive && xstrused(pArgs->sFileName))
    {
        xstrcase(pArgs->sFileName, XSTR_LOWER);
        xstrcase(pArgs->sText, XSTR_LOWER);
    }

    if (xstrused(sMaxFileSize))
    {
        if (XSearch_GetSize(sMaxFileSize, &pArgs->nMaxSize) < 0)
        {
            xloge("Invalid max file size");
            return 0;
        }
    }

    if (xstrused(sMinFileSize))
    {
        if (XSearch_GetSize(sMinFileSize, &pArgs->nMinSize) < 0)
        {
            xloge("Invalid min file size");
            return 0;
        }
    }

    if (xstrused(sMaxReadSize))
    {
        if (XSearch_GetSize(sMaxReadSize, &pArgs->nMaxRead) < 0)
        {
            xloge("Invalid max read buffer size");
            return 0;
        }
    }
    else pArgs->nMaxRead = XSEARCH_MAX_READ_SIZE;

    /* Validate opts */
    if (pArgs->nPermissions < 0 || 
        pArgs->nFileTypes < 0) 
            return 0;

    return 1;
}

static void XSearch_ColorizeEntry(xsearch_t *pSearch, char *pOutput, size_t nSize, xsearch_entry_t *pEntry)
{
    char *pColor = XSTR_EMPTY;
    char *pBack = XSTR_EMPTY;
    char *pFmt = XSTR_EMPTY;
    char sColor[XSTR_MICRO];

    if (pSearch->bReadStdin)
    {
        xstrnrgb(sColor, sizeof(sColor), 198, 145, 255);
        pColor = sColor; // Amethyst color
        pFmt = XSTR_FMT_BOLD;
    }
    else if (pEntry->eType == XF_SYMLINK)
    {
        if (pEntry->pRealPath == NULL)
        {
            pColor = XSTR_CLR_RED;
            pBack = XSTR_BACK_BLACK;
            pFmt = XSTR_FMT_BOLD;
        }
        else
        {
            pColor = XSTR_CLR_CYAN;
            pFmt = XSTR_FMT_BOLD;
        }
    }
    else if (pEntry->eType == XF_REGULAR)
    {
        if (XFile_IsExec(pEntry->nMode))
        {
            pColor = XSTR_CLR_GREEN;
            pFmt = XSTR_FMT_BOLD;
        }
        else if (pEntry->sName[0] == '.')
        {
            xstrnrgb(sColor, sizeof(sColor), 192, 192, 192);
            pColor = sColor; // Silver grey color
            pFmt = XSTR_FMT_DIM;
        }
    }
    else if (pEntry->eType == XF_DIRECTORY)
    {
        pColor = XSTR_CLR_BLUE;
        pFmt = XSTR_FMT_BOLD;
    }
    else if (pEntry->eType == XF_SOCKET)
    {
        pColor = XSTR_CLR_MAGENTA;
        pFmt = XSTR_FMT_BOLD;
    }
    else if (pEntry->eType == XF_PIPE)
    {
        pColor = XSTR_CLR_YELLOW;
        pBack = XSTR_BACK_BLACK;
    }
    else if (pEntry->eType == XF_CHAR_DEVICE ||
            pEntry->eType == XF_BLOCK_DEVICE)
    {
        pColor = XSTR_CLR_YELLOW;
        pBack = XSTR_BACK_BLACK;
        pFmt = XSTR_FMT_BOLD;
    }

    size_t nOffset = 0;
    while (!strncmp(&pEntry->sPath[nOffset], "./", 2)) nOffset += 2;

    char *pQuote = XSTR_EMPTY;
    if (xstrsrc(pEntry->sName, XSTR_SPACE) > 0) pQuote = "'";

    xstrncpyf(pOutput, nSize, "%s%s%s%s%s%s%s%s", pColor, pFmt, pBack,
        &pEntry->sPath[nOffset], pQuote, pEntry->sName, pQuote, XSTR_FMT_RESET);
}

static void XSearch_ColorizeSymlink(xsearch_t *pSearch, char *pSimlink, size_t nSize, xsearch_entry_t *pEntry)
{
    if (pEntry->eType == XF_SYMLINK)
    {
        if (pEntry->pRealPath != NULL)
        {
            xstat_t statbuf;
            xsearch_entry_t linkEntry;
            xstat(pEntry->pRealPath, &statbuf);

            XSearch_CreateEntry(&linkEntry, NULL, pEntry->sLink, &statbuf);
            XSearch_ColorizeEntry(pSearch, pSimlink, nSize, &linkEntry);
        }
        else
        {
            xstrncpyf(pSimlink, nSize, "%s%s%s%s",
                XSTR_FMT_BOLD, XSTR_BACK_RED,
                pEntry->sLink, XSTR_FMT_RESET);
        }
    }
}

static void XSearch_ColorizeLine(char *pDst, size_t nSize, xsearch_entry_t *pEntry, const char *pText, xbool_t bJumpSpace)
{
    if (!xstrused(pText) || !xstrused(pEntry->sLine)) return;

    char *pLine = pEntry->sLine;
    if (bJumpSpace) while (*pLine && isspace((unsigned char)*pLine)) pLine++;

    xarray_t *pArr = xstrsplitd(pLine, pText);
    if (pArr == NULL)
    {
        xstrncpyf(pDst, nSize, "%s%s%s", XSTR_FMT_DIM, pEntry->sLine, XSTR_FMT_RESET);
        return;
    }

    size_t i = 0;
    char sColorized[XLINE_MAX];
    xstrnclr(sColorized, sizeof(sColorized), XSTR_CLR_RED, "%s", pText);
    xstrncatf(pDst, XSTR_NAVAIL(pDst, nSize), "%s", XSTR_FMT_DIM);

    for (i = 0; i < pArr->nUsed; i++)
    {
        const char *pData = (const char*)XArray_GetData(pArr, i);
        if (pData == NULL) continue;

        if (xstrcmp(pData, pText))
        {
            xstrncatf(pDst, XSTR_NAVAIL(pDst, nSize), "%s%s", sColorized, XSTR_FMT_DIM);
            continue;
        }

        xstrncatf(pDst, XSTR_NAVAIL(pDst, nSize), "%s", pData);
    }

    xstrncatf(pDst, XSTR_NAVAIL(pDst, nSize), "%s", XSTR_FMT_RESET);
    XArray_Destroy(pArr);
}

static void XSearch_DisplayEntry(xsearch_t *pSearch, xsearch_entry_t *pEntry)
{
    xsearch_args_t *pArgs = (xsearch_args_t*)pSearch->pUserCtx;
    xbool_t bJumpSpace = pArgs->bJumpSpace;

    char sTime[XSEARCH_TIME_LEN + 1];
    char sLinkPath[XPATH_MAX + 64];
    char sEntry[XPATH_MAX + 64];
    char sLine[XLINE_MAX + 64];

    sLinkPath[0] = sEntry[0] = sTime[0] = sLine[0] = XSTR_NUL;
    const char *pArrow = pEntry->eType == XF_SYMLINK ? " -> " : XSTR_EMPTY;

    XSearch_ColorizeEntry(pSearch, sEntry, sizeof(sEntry), pEntry);
    XSearch_ColorizeSymlink(pSearch, sLinkPath, sizeof(sLinkPath), pEntry);
    XSearch_ColorizeLine(sLine, sizeof(sLine), pEntry, pSearch->sText, bJumpSpace);

    /* Do not display additional info if verbose is not set */
    if (!((xsearch_args_t*)pSearch->pUserCtx)->bVerbose || pSearch->bReadStdin)
    {
        if (pEntry->nLineNum && xstrused(sLine))
            xlog("%s:%s%d%s %s", sEntry, XSTR_FMT_BOLD,
                pEntry->nLineNum, XSTR_FMT_RESET, sLine);
        else if (xstrused(sLine)) xlog("%s: %s", sEntry, sLine);
        else xlog("%s%s%s", sEntry, pArrow, sLinkPath);

        return;
    }

    /* Get username and group from uid and gid */
    struct passwd *pws = getpwuid(pEntry->nUID);
    struct group *grp = getgrgid(pEntry->nGID);
    const char *pUname = pws ? pws->pw_name : XSTR_EMPTY;
    const char *pGname = grp ? grp->gr_name : XSTR_EMPTY;

    /* Get last modification time */
    const char *pTime = ctime(&pEntry->nTime);
    if (pTime != NULL) xstrncpy(sTime, sizeof(sTime), &pTime[4]);

    /* Create size string with indentation */
    char sSize[XSEARCH_SIZE_LEN], sRound[XSEARCH_SIZE_LEN + 8];
    XBytesToUnit(sSize, sizeof(sSize), pEntry->nSize, XTRUE);
    xstrnlcpyf(sRound, sizeof(sRound), 8, XSTR_SPACE_CHAR, "%s", sSize);

    if (pEntry->nLineNum && xstrused(sLine))
    {
        xlog("%c%s %lu %s %s %s [%s] %s:%s%d%s %s", XFile_GetTypeChar(pEntry->eType),
            pEntry->sPerm, pEntry->nLinkCount, pUname, pGname, sRound, sTime,
            sEntry, XSTR_FMT_BOLD, pEntry->nLineNum, XSTR_FMT_RESET, sLine);
    }
    else if (xstrused(sLine))
    {
        xlog("%c%s %lu %s %s %s [%s] %s: %s", XFile_GetTypeChar(pEntry->eType),
            pEntry->sPerm, pEntry->nLinkCount, pUname, pGname, sRound, sTime,
            sEntry, sLine);
    }
    else
    {
        xlog("%c%s %lu %s %s %s [%s] %s%s%s", XFile_GetTypeChar(pEntry->eType),
            pEntry->sPerm, pEntry->nLinkCount, pUname, pGname, sRound, sTime,
            sEntry, pArrow, sLinkPath);
    }
}

int XSearch_Callback(xsearch_t *pSearch, xsearch_entry_t *pEntry, const char *pMsg)
{
    if (XSYNC_ATOMIC_GET(&g_interrupted)) return XSTDERR;
    if (pEntry != NULL) XSearch_DisplayEntry(pSearch, pEntry);
    if (pMsg != NULL) xloge("%s (%s)", pMsg, XSTRERR);
    return XSTDNON;
}

int main(int argc, char* argv[])
{
    xlog_defaults();
    xsearch_args_t args;
    xsearch_t search;

    if (!XSearch_ParseArgs(&args, argc, argv))
    {
        XSearch_Usage(argv[0]);
        return XSTDERR;
    }

    const char *pDirectory = xstrused(args.sDirectory) ? args.sDirectory : NULL;
    const char *pFileName = xstrused(args.sFileName) ? args.sFileName : NULL;

    XSearch_Init(&search, pFileName);
    xstrncpy(search.sText, sizeof(search.sText), args.sText);
    search.nPermissions = args.nPermissions;
    search.bInsensitive = args.bInsensitive;
    search.bSearchLines = args.bSearchLines;
    search.bMatchOnly = args.bMatchOnly;
    search.bRecursive = args.bRecursive;
    search.bReadStdin = args.bReadStdin;
    search.nFileTypes = args.nFileTypes;
    search.nLinkCount = args.nLinkCount;
    search.nFileSize = args.nFileSize;
    search.nMaxSize = args.nMaxSize;
    search.nMinSize = args.nMinSize;
    search.nBufferSize = args.nMaxBuffer;
    search.callback = XSearch_Callback;
    search.pUserCtx = &args;

    search.pInterrupted = &g_interrupted;
    signal(SIGINT, signal_callback);

    XSearch(&search, pDirectory);
    XSearch_Destroy(&search);

    return XSTDNON;
}
